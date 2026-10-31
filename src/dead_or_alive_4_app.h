// dead_or_alive_4 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/rex_app.h>
#include <rex/ui/keybinds.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/user_module.h>
#include <rex/system/util/object_table.h>
#include <rex/system/xthread.h>

#include "doa4_diag_overlay.h"
#include "doa4_lowmem.h"

REXCVAR_DECLARE(bool, log_noisy);

extern "C" void doa4_arm_shutdown_watchdog();  // doa4_app_hooks.cpp

class DeadOrAlive4App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<DeadOrAlive4App>(new DeadOrAlive4App(ctx, "dead_or_alive_4",
        PPCImageConfig));
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (paths.game_data_root.empty()) {
      paths.game_data_root = "E:/Downloads/doa4/xex/DOA4";
    }
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  void OnPostSetup() override {
    REXLOG_INFO("[doa4][LIFE] OnPostSetup: runtime ready");
    // NOTE: log_noisy intentionally left OFF. It was force-enabled during boot
    // bring-up to trace kernel imports; that trace is hot-path spam now and is
    // a no-op at the relwithdebinfo log level (info) anyway.
    MountDiscRoot();
    MountCacheDevice();
    StartThreadSnapshotter();

    // Photo/snapshot capture reads the rendered frame back from GUEST memory.
    // rexglue's GPU defaults readback_resolve="none", so render-to-texture
    // resolve results never get copied from the host GPU into guest RAM — the
    // in-match photo buffer stays zeroed and saved photos come out solid black.
    // Enable readback so the captured frame reaches CPU memory.
    //
    // Mode = "some": copy a resolve result to guest RAM only ON DEMAND (when the
    // CPU/texture cache actually reads that region). "fast" (copy EVERY frame)
    // also fixes the black photo but continuously overwrites the save-slot
    // preview surfaces with the live capture, so every saved slot's thumbnail
    // showed the most recent photo. "some" copies only when the save path reads
    // the captured frame, which keeps photos working without that overwrite —
    // and stays cheaper than "full" (per-resolve sync stall that hurts gameplay).
    if (rex::cvar::SetFlagByName("readback_resolve", "some")) {
      REXLOG_INFO("[doa4][GPU] readback_resolve=some (enables photo/snapshot "
                  "framebuffer capture)");
    } else {
      REXLOG_WARN("[doa4][GPU] failed to set readback_resolve cvar");
    }
  }
  void OnPostLoadXexImage() override {
    REXLOG_INFO("[doa4][LIFE] OnPostLoadXexImage: XEX mapped into guest memory");
    DumpGuestStrings();
    // Before any guest code allocates: keeps fighter data out of low addresses,
    // where the game's relocation test skips tables (Ryu Hayabusa / Brad Wong crash).
    doa4::ReserveLowGuestMemory(runtime() ? runtime()->memory() : nullptr);
  }
  void OnPreLaunchModule() override {
    REXLOG_INFO("[doa4][LIFE] OnPreLaunchModule: about to spawn main guest thread");
  }
  void OnPostLaunchModule(rex::system::XThread* thread) override {
    REXLOG_INFO("[doa4][LIFE] OnPostLaunchModule: main thread tid={} created (suspended)",
                thread ? thread->thread_id() : 0u);
  }
  void OnGuestThreadExit(rex::system::XThread* thread) override {
    REXLOG_INFO("[doa4][LIFE] OnGuestThreadExit: main thread tid={} exited",
                thread ? thread->thread_id() : 0u);
  }
  void OnShutdown() override {
    doa4_arm_shutdown_watchdog();
    rex::ui::UnregisterBind("bind_doa4_diag");
    diag_overlay_.reset();  // before the base app destroys the ImGui drawer
    snap_stop_.store(true, std::memory_order_release);
    if (snap_thread_.joinable()) snap_thread_.join();
  }
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}

  // The diagnostics overlay is off until F6 (bind_doa4_diag) or
  // DOA4_DIAG_OVERLAY=1 turns it on: while any ImGui dialog exists the
  // presenter paints from the UI thread (every guest frame waits for a UI
  // repaint, and the UI repaints continuously), instead of presenting each
  // frame straight from the GPU thread.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    if (!drawer) return;
    rex::ui::RegisterBind("bind_doa4_diag", "F6", "Toggle DOA4 diagnostics overlay",
                          [this, drawer] { ToggleDiagOverlay(drawer); });
    const char* env = std::getenv("DOA4_DIAG_OVERLAY");
    if (env && env[0] == '1') ToggleDiagOverlay(drawer);
  }

  void ToggleDiagOverlay(rex::ui::ImGuiDrawer* drawer) {
    if (diag_overlay_) {
      diag_overlay_.reset();
      REXLOG_INFO("[doa4][UI] DiagOverlayDialog closed");
    } else {
      diag_overlay_ = std::make_unique<doa4::DiagOverlayDialog>(drawer);
      REXLOG_INFO("[doa4][UI] DiagOverlayDialog attached");
    }
  }

 private:
  std::unique_ptr<doa4::DiagOverlayDialog> diag_overlay_;
  std::atomic<bool> snap_stop_{false};
  std::thread snap_thread_;

  // DOA4 accesses its data via paths like d:\chrone.afs, D:\Sound\... and
  // d:\dic\... where the disc root *is* the game data directory (NOT a
  // parent containing a DOA4\ subdir). Mount our game_data_root directly at
  // \Device\Cdrom0 and point d: there.
  void MountDiscRoot() {
    auto* rt = runtime();
    if (!rt || !rt->file_system()) return;
    std::filesystem::path game_root = std::filesystem::absolute(rt->game_data_root());
    if (game_root.empty()) {
      REXLOG_WARN("[doa4][DISC] game_data_root empty; leaving d: as-is");
      return;
    }
    const char* disc_mount = "\\Device\\Cdrom0";
    auto disc = std::make_unique<rex::filesystem::HostPathDevice>(disc_mount, game_root, true);
    if (!disc->Initialize() || !rt->file_system()->RegisterDevice(std::move(disc))) {
      REXLOG_WARN("[doa4][DISC] failed to mount {} at {}", game_root.string(), disc_mount);
      return;
    }
    rt->file_system()->UnregisterSymbolicLink("d:");
    rt->file_system()->RegisterSymbolicLink("d:", disc_mount);
    REXLOG_INFO("[doa4][DISC] mounted {} at {}; d: -> {} (d:\\foo == game:\\foo)",
                game_root.string(), disc_mount, disc_mount);
  }

  // DOA4 opens cache:\chrone.afs, cmn.afs, mot.afs, etc. and treats failure
  // as fatal (KeBugCheck(14)). On Xbox 360 these are pre-staged from the
  // game disc into the HDD cache partition; for us the original files live
  // in the game data directory, so just symlink cache: to the same place.
  void MountCacheDevice() {
    auto* rt = runtime();
    if (!rt || !rt->file_system()) return;
    rt->file_system()->RegisterSymbolicLink("cache:", "\\Device\\Harddisk0\\Partition1");
    REXLOG_INFO("[doa4][CACHE] symlinked cache: -> \\Device\\Harddisk0\\Partition1 (game data)");
  }

  static std::string ReadGuestString(uint32_t addr, size_t max_len = 128) {
    if (addr == 0) return "<null>";
    auto* ks = rex::system::kernel_state();
    if (!ks || !ks->memory()) return "<no-base>";
    auto* base = ks->memory()->virtual_membase();
    if (!base) return "<no-base>";
    std::string out;
    out.reserve(max_len);
    for (size_t i = 0; i < max_len; ++i) {
      uint8_t c = base[addr + i];
      if (c == 0) break;
      if (c < 0x20 || c > 0x7e) {
        out.push_back('?');
      } else {
        out.push_back(static_cast<char>(c));
      }
    }
    return out;
  }

  void DumpGuestStrings() {
    // Assertion string-literal triplets passed to sub_82892670 by various
    // getters. lis -32254 -> 0x82020000 base.
    auto base_addr = [](int32_t off) -> uint32_t { return 0x82020000u + (uint32_t)off; };
    struct Entry { const char* tag; uint32_t addr; };
    Entry exact[] = {
        {"828980E0.file(20808)", base_addr(20808)},
        {"828980E0.func(27924)", base_addr(27924)},
        {"828980E0.msg (28760)", base_addr(28760)},
        {"828976D8.file(20808)", base_addr(20808)},
        {"828976D8.func(28180)", base_addr(28180)},
        {"828976D8.msg (27944)", base_addr(27944)},
    };
    for (auto& e : exact) {
      REXLOG_INFO("[doa4][STR] {} @ {:#x} = '{}'", e.tag, e.addr,
                  ReadGuestString(e.addr, 200));
    }
  }

  void StartThreadSnapshotter() {
    REXLOG_INFO("[doa4][SNAP] starting host snapshotter thread");
    snap_thread_ = std::thread([this]() {
      using namespace std::chrono_literals;
      REXLOG_INFO("[doa4][SNAP] host thread alive");
      // Let guest threads spin up + game to reach the SSM shader-compile gate.
      std::this_thread::sleep_for(8000ms);

      auto symbolize = [](uint32_t addr) -> std::string {
        if (!addr) return "0";

        // Build (addr, name) table on first call by parsing the codegen-
        // generated init header for `DECLARE_REX_FUNC(sub_XXXXXXXX)` lines.
        // ~50k entries; one-time cost.
        static std::vector<std::pair<uint32_t, std::string>> syms;
        static std::once_flag init_once;
        std::call_once(init_once, [] {
          std::vector<std::filesystem::path> candidates = {
              "../../../generated/default/dead_or_alive_4_init.h",
              "generated/default/dead_or_alive_4_init.h",
              "E:/Downloads/doa4/rexglue-sdk/deadoralive/generated/default/"
              "dead_or_alive_4_init.h",
          };
          std::filesystem::path found;
          for (auto& c : candidates) {
            std::error_code ec;
            if (std::filesystem::exists(c, ec)) { found = c; break; }
          }
          if (found.empty()) {
            REXLOG_WARN("[doa4][SYM] init.h not found; symbols disabled");
            return;
          }
          std::ifstream in(found);
          std::string line;
          std::regex re(R"(sub_([0-9A-Fa-f]{8}))");
          while (std::getline(in, line)) {
            std::smatch m;
            if (std::regex_search(line, m, re)) {
              uint32_t a = (uint32_t)std::stoul(m[1].str(), nullptr, 16);
              syms.emplace_back(a, "sub_" + m[1].str());
            }
          }
          std::sort(syms.begin(), syms.end());
          syms.erase(std::unique(syms.begin(), syms.end(),
                                 [](auto& a, auto& b) { return a.first == b.first; }),
                     syms.end());
          REXLOG_INFO("[doa4][SYM] loaded {} symbols from {}", syms.size(),
                      found.string());
        });

        if (syms.empty()) return fmt::format("{:#x}", addr);
        // upper_bound -> first > addr; predecessor is largest <= addr.
        auto it = std::upper_bound(syms.begin(), syms.end(), addr,
                                   [](uint32_t v, auto& p) { return v < p.first; });
        if (it == syms.begin()) return fmt::format("{:#x}", addr);
        --it;
        uint32_t off = addr - it->first;
        if (off > 0x10000) return fmt::format("{:#x}", addr);  // probably not in this func
        return fmt::format("{}+{:#x}", it->second, off);
      };

      // Steady state emits one summary line per round (main guest thread's
      // PC + running count) — enough to spot a wedged main thread without the
      // 39-line-per-round flood. The full per-thread dump runs ONCE on round 0
      // to confirm every guest thread spun up, and again on demand when the
      // running-thread count collapses (likely-hang signal).
      // Sleeps end early once OnShutdown sets snap_stop_ (it joins this
      // thread, and a plain 5 s sleep held every window close for up to 5 s).
      auto nap = [this] {
        for (int i = 0; i < 50 && !snap_stop_.load(std::memory_order_acquire); ++i) {
          std::this_thread::sleep_for(100ms);
        }
      };
      size_t prev_running = 0;
      for (int round = 0; round < 240 && !snap_stop_.load(std::memory_order_acquire); ++round) {
        auto* ks = rex::system::kernel_state();
        auto* ot = ks ? ks->object_table() : nullptr;
        if (!ot) { nap(); continue; }
        std::vector<rex::system::object_ref<rex::system::XThread>> threads;
        try { threads = ot->GetObjectsByType<rex::system::XThread>(); }
        catch (...) { nap(); continue; }

        uint32_t main_lr = 0;
        size_t guest_total = 0, guest_running = 0;
        for (auto& t : threads) {
          if (!t || !t->is_guest_thread()) continue;
          ++guest_total;
          if (t->is_running()) ++guest_running;
          if (t->main_thread()) {
            auto* ts = t->thread_state();
            auto* pctx = ts ? ts->context() : nullptr;
            if (pctx) main_lr = (uint32_t)pctx->lr;
          }
        }

        // Full dump only on first round or when running count drops sharply.
        bool collapse = round > 0 && prev_running >= 2 && guest_running * 2 < prev_running;
        if (round == 0 || collapse) {
          REXLOG_INFO("[doa4][SNAP] full dump (round {}{}) guest={} running={}",
                      round, collapse ? ", running collapsed" : "", guest_total, guest_running);
          for (auto& t : threads) {
            if (!t || !t->is_guest_thread()) continue;
            auto* ts = t->thread_state();
            auto* pctx = ts ? ts->context() : nullptr;
            uint32_t lr = pctx ? (uint32_t)pctx->lr : 0u;
            uint32_t r1 = pctx ? (uint32_t)pctx->r1.u64 : 0u;
            REXLOG_INFO("[doa4][SNAP]  tid={:2} {}{} lr={} r1={:#x}",
                        t->thread_id(), t->main_thread() ? "M" : " ",
                        t->is_running() ? "R" : "-", symbolize(lr), r1);
          }
        } else {
          REXLOG_INFO("[doa4][SNAP] guest run={}/{} main_lr={}",
                      guest_running, guest_total, symbolize(main_lr));
        }
        prev_running = guest_running;
        nap();
      }
    });
  }
};

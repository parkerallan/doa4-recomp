// DOA4 application hooks — REAL FIXES ONLY.
//
// This file collects the guest-function overrides that are required for the
// recompiled title to make forward progress.  Diagnostic / scratch tracing
// belongs in `doa4_debug_hooks.cpp`.
//
// Each hook is annotated with what it does and *why* it is necessary.

#include <atomic>
#include <cstdint>
#include <cstring>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xmemory.h>

#include "generated/default/dead_or_alive_4_init.h"


namespace {

uint32_t Tid() {
  auto* ts = rex::runtime::ThreadState::Get();
  return ts ? ts->thread_id() : 0;
}

uint32_t LoadU32(uint8_t* base, uint32_t guest_addr) {
  if (guest_addr == 0) return 0;
  return rex::memory::load_and_swap<uint32_t>(base + guest_addr);
}

}  // namespace

// ===========================================================================
// FIX 1 — DbgBreakPoint trampoline neutralizer.
//
// sub_82790420 is literally `b DbgBreakPoint`.  The record parser in
// sub_82881C38 hits it when it encounters invalid opcodes triggered by our
// zero-buffer shader bypass (FIX 2 below).  We let the parser skip past
// failed assertions instead of trapping.
// ===========================================================================
extern "C" {
REX_EXTERN(__imp__sub_82790420);
}

REX_HOOK_RAW(sub_82790420) {
  static std::atomic<uint64_t> s_n{0};
  uint64_t n = s_n.fetch_add(1, std::memory_order_relaxed);
  if (n < 40 || (n % 1000) == 0) {
    REXLOG_INFO("[doa4][FIX][DbgBreak] sub_82790420 suppressed #{} tid={} lr={:#x}",
                n, Tid(), (uint32_t)ctx.lr);
  }
  ctx.r3.s64 = 0;
}

// ===========================================================================
// FIX 2 — CompiledShader getter (sub_828980E0) NULL-buffer bypass.
//
// When *(self+44) is NULL, the caller sub_82882968 reads *(NULL+0) as a
// state-block count and busy-loops in a pure-asm byte-swap.  We install a
// pointer to a one-time 16 KiB zero buffer (allocated from the GAME'S heap
// via sub_827930E0) so the caller reads count=0 and exits cleanly.
//
// Workaround, NOT a real shader-compile fix — unblocks the main thread so
// we can observe the next bottleneck.
// ===========================================================================
extern "C" {
REX_EXTERN(__imp__sub_828980E0);
REX_EXTERN(__imp__sub_827930E0);  // game's malloc: r3=size, r4=flags
}

namespace {

constexpr uint32_t kBypassBufBytes = 0x4000;  // 16 KiB

uint32_t GuestHeapAllocZero(PPCContext& ctx, uint8_t* base) {
  uint64_t saved_r3 = ctx.r3.u64;
  uint64_t saved_r4 = ctx.r4.u64;
  uint64_t saved_lr = ctx.lr;

  ctx.r3.s64 = kBypassBufBytes;
  ctx.r4.s64 = 0x64200000;
  ctx.lr = 0;
  __imp__sub_827930E0(ctx, base);
  uint32_t addr = (uint32_t)ctx.r3.u64;

  ctx.r3.u64 = saved_r3;
  ctx.r4.u64 = saved_r4;
  ctx.lr = saved_lr;

  if (addr) {
    std::memset(base + addr, 0, kBypassBufBytes);
  }
  return addr;
}

}  // namespace

REX_HOOK_RAW(sub_828980E0) {
  static std::atomic<uint64_t> s_n{0};
  static std::atomic<uint64_t> s_bypass{0};
  uint64_t n = s_n.fetch_add(1, std::memory_order_relaxed);
  uint32_t self = (uint32_t)ctx.r3.u64;
  uint32_t lr = (uint32_t)ctx.lr;
  uint32_t cs = LoadU32(base, self + 44);
  if (cs == 0 && self != 0) {
    uint32_t buf = GuestHeapAllocZero(ctx, base);
    if (buf) {
      rex::memory::store_and_swap<uint32_t>(base + self + 44, buf);
      uint64_t b = s_bypass.fetch_add(1, std::memory_order_relaxed);
      if (b < 20 || (b % 100) == 0) {
        REXLOG_INFO("[doa4][FIX][CS] #{} self={:#x} +44 was NULL, installed buf={:#x} (lr={:#x})",
                    b, self, buf, lr);
      }
    } else {
      REXLOG_ERROR("[doa4][FIX][CS] #{} self={:#x} GuestHeapAlloc FAILED (lr={:#x})",
                   n, self, lr);
    }
  } else if (n < 20) {
    REXLOG_INFO("[doa4][FIX][CS] passthrough #{} tid={} self={:#x} *(self+44)={:#x} lr={:#x}",
                n, Tid(), self, cs, lr);
  }
  __imp__sub_828980E0(ctx, base);
}

// ===========================================================================
// FIX 3 — KiSwapContext via Win32 fibers (cooperative scheduler real-fix).
//
// sub_82793F80 is the title's KiSwapContext: save current PPC NV regs to
// the current fiber's save buffer (looked up via *(*(r13+256)+356)), load
// NV regs from r3's buffer, update KPCR's "current fiber" field, then
// `mtlr r7 ; b KeSetCurrentStackPointers` — which on real PPC performs a
// cross-stack jump into the destination fiber's saved LR.
//
// Our recompiler translates `blr` as plain C++ `return`, so the cross-stack
// jump is lost and execution falls back into the original C++ caller chain
// with the destination fiber's PPC register set grafted on. That doesn't
// run the destination fiber — at best it confuses the source's caller, at
// worst it AVs.
//
// Real fix: every guest fiber gets its own Win32 fiber (i.e. its own host
// stack). Our hook
//   1. invokes the original swap inline (which correctly saves outgoing NV
//      regs, loads incoming NV regs, updates KPCR, and calls
//      KeSetCurrentStackPointers — so ctx is left in destination-fiber
//      state); then
//   2. SwitchToFiber()s to the destination's host fiber so the destination
//      runs on its own C++ stack with ctx already containing its state.
//
// When the destination later yields back to us via the same hook, the
// SwitchToFiber call here resumes; ctx will have been re-loaded with our
// state by whoever switched us back in. We just return from the hook and
// the original recompiled caller chain continues.
//
// PPCContext is per-host-thread (lives in ThreadState, TLS-bound) and is
// SHARED across all fibers running on the same host thread — which is
// exactly the cooperative-scheduling model we want.
// ===========================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <mutex>
#include <unordered_map>

extern "C" {
REX_EXTERN(__imp__sub_82793F80);  // original swap (unhooked alias)
}

namespace {

struct GuestFiber {
  void* host_fiber = nullptr;  // Win32 fiber handle
  uint32_t buf = 0;            // guest save-buffer address (= map key)
  uint8_t* base = nullptr;     // captured for GuestFiberProc
  bool is_initial = false;     // true if this represents the host thread's
                               // original stack (no GuestFiberProc — we just
                               // SwitchToFiber back to its handle on resume).
  bool dispatched = false;     // true once GuestFiberProc has dispatched the
                               // entry on this host fiber. Used to detect the
                               // guest reusing a save-buffer for a *fresh*
                               // fiber while our host fiber is still parked in
                               // the previous execution (see FIX 7).
};

std::mutex g_fiber_mu;
std::unordered_map<uint32_t, std::unique_ptr<GuestFiber>> g_fibers;

}  // namespace

// Diagnostic heartbeat lives in doa4_debug_hooks.cpp; we just notify it on
// each successful fiber resume so it can update its per-fiber tables.
extern "C" void doa4_record_fiber_tick(uint32_t dst_buf, uint32_t entry_pc);
extern "C" void doa4_start_fiber_beat_thread();
extern "C" void doa4_record_fiber_swap_out(uint32_t out_buf, uint32_t lr, uint32_t r1,
                                           uint32_t r3, uint32_t host_tid);
extern "C" void doa4_record_fiber_swap_in(uint32_t dst_buf, uint32_t lr, uint32_t r1,
                                          uint32_t r3, uint32_t host_tid);

namespace {

// Per-host-thread: the Win32 fiber handle obtained from ConvertThreadToFiber
// on first entry to the swap hook. Represents the "initial" fiber — the
// scheduler / first guest fiber whose stack is the OS thread's original.
thread_local void* tls_initial_fiber = nullptr;

static void WINAPI GuestFiberProc(void* param);

GuestFiber* GetOrCreateFiber(uint32_t buf, uint8_t* base) {
  std::lock_guard<std::mutex> lk(g_fiber_mu);
  auto it = g_fibers.find(buf);
  if (it != g_fibers.end()) return it->second.get();
  auto p = std::make_unique<GuestFiber>();
  p->buf = buf;
  p->base = base;
  // 64 KiB host stack — fibers in DOA4 don't recurse deeply on the host side.
  p->host_fiber = CreateFiber(64 * 1024, &GuestFiberProc, p.get());
  if (!p->host_fiber) {
    REXLOG_ERROR("[doa4][Fiber] CreateFiber FAILED for buf={:#x}", buf);
  }
  GuestFiber* raw = p.get();
  g_fibers.emplace(buf, std::move(p));
  REXLOG_DEBUG("[doa4][Fiber] created fiber for buf={:#x} host={:p}", buf, raw->host_fiber);
  return raw;
}

// FIX 7 — recreate a stale host fiber when the guest re-dispatches its buffer.
// The guest reuses a KiSwapContext save-buffer address for a brand-new fiber
// (fresh stack top, bootstrap LR). Our host fiber for that buffer is still
// parked deep in the PREVIOUS execution. SwitchToFiber-ing into it would resume
// that old C++ call chain with the new (fresh) ctx.r1, so every guest epilogue
// unwinds from the wrong stack pointer (observed: sub_824F3448 restores r31
// from the wrong slot -> 0 -> null store crash on the title screen). Tear down
// the parked host fiber and create a clean one so GuestFiberProc runs the new
// entry from scratch. The abandoned execution's guest state was already
// discarded by the guest when it reinitialised the buffer.
void RecreateHostFiber(GuestFiber* fs) {
  std::lock_guard<std::mutex> lk(g_fiber_mu);
  void* old = fs->host_fiber;
  void* fresh = CreateFiber(64 * 1024, &GuestFiberProc, fs);
  if (!fresh) {
    REXLOG_ERROR("[doa4][Fiber][FIX7] CreateFiber FAILED recreating buf={:#x}; keeping old", fs->buf);
    return;
  }
  fs->host_fiber = fresh;
  fs->dispatched = false;
  if (old) DeleteFiber(old);  // old is parked (not current/running) -> safe
  static std::atomic<uint64_t> s_n{0};
  uint64_t n = s_n.fetch_add(1, std::memory_order_relaxed);
  if (n < 40 || (n % 200) == 0) {
    REXLOG_INFO("[doa4][Fiber][FIX7] #{} recreated stale host fiber for buf={:#x} "
                "(old={:p} new={:p})", n, fs->buf, old, fresh);
  }
}

void EnsureInitialFiber(uint32_t out_buf, uint8_t* base) {
  if (tls_initial_fiber) return;
  tls_initial_fiber = ConvertThreadToFiber(nullptr);
  if (!tls_initial_fiber) {
    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_FIBER /* 1280 */) {
      // Host thread was already converted to a fiber by XThread::Execute().
      // Use the existing current fiber handle as our initial fiber.
      tls_initial_fiber = GetCurrentFiber();
    } else {
      REXLOG_ERROR("[doa4][Fiber] ConvertThreadToFiber FAILED on tid={} err={}", Tid(), err);
      return;
    }
  }
  REXLOG_DEBUG("[doa4][Fiber] host tid={} converted to fiber={:p}, initial buf={:#x}",
               Tid(), tls_initial_fiber, out_buf);
  if (out_buf) {
    std::lock_guard<std::mutex> lk(g_fiber_mu);
    if (g_fibers.find(out_buf) == g_fibers.end()) {
      auto p = std::make_unique<GuestFiber>();
      p->buf = out_buf;
      p->base = base;
      p->is_initial = true;
      p->host_fiber = tls_initial_fiber;
      g_fibers.emplace(out_buf, std::move(p));
    }
  }
}

void DoFiberSwap(PPCContext& ctx, uint8_t* base) {
  uint32_t dst_buf = (uint32_t)ctx.r3.u64;
  if (!dst_buf) {
    __imp__sub_82793F80(ctx, base);
    return;
  }

  doa4_start_fiber_beat_thread();

  uint32_t kpcr = LoadU32(base, ctx.r13.u32 + 256);
  uint32_t out_buf = kpcr ? LoadU32(base, kpcr + 356) : 0;
  EnsureInitialFiber(out_buf, base);

  GuestFiber* dst = GetOrCreateFiber(dst_buf, base);

  // Record the outgoing fiber's last guest state BEFORE the swap overwrites
  // ctx — this is what powers the freeze-dump / per-fiber gap diagnostics.
  uint32_t host_tid = Tid();
  if (out_buf) {
    doa4_record_fiber_swap_out(out_buf, (uint32_t)ctx.lr, ctx.r1.u32, ctx.r3.u32, host_tid);
  }

  // Let the original swap do all the bookkeeping: save outgoing NV regs to
  // out_buf, load NV regs from dst_buf, update KPCR's current-fiber pointer
  // to dst_buf, and call KeSetCurrentStackPointers which sets ctx.r1 to
  // dst's guest stack pointer. After this returns, ctx is in destination
  // state.
  __imp__sub_82793F80(ctx, base);

  doa4_record_fiber_tick(dst_buf, ctx.r31.u32);
  doa4_record_fiber_swap_in(dst_buf, (uint32_t)ctx.lr, ctx.r1.u32, ctx.r3.u32, host_tid);

  // FIX 7: the guest is dispatching this buffer FRESH (bootstrap LR) but our
  // host fiber for it is still parked in a previous execution. Resuming it
  // would run the old chain with the new stack pointer -> corruption. Recreate
  // a clean host fiber so GuestFiberProc runs the new entry from scratch.
  if ((uint32_t)ctx.lr == 0x82793F70u && dst->dispatched && !dst->is_initial &&
      dst->host_fiber != GetCurrentFiber()) {
    RecreateHostFiber(dst);
  }

  // If destination is the fiber we're already running, nothing to do.
  if (!dst->host_fiber || dst->host_fiber == GetCurrentFiber()) {
    return;
  }

  // Actually transfer host execution to the destination fiber's stack.
  // When some other fiber later switches back to us, SwitchToFiber resumes
  // here with ctx already loaded by that swapper to our state.
  SwitchToFiber(dst->host_fiber);
}

static void WINAPI GuestFiberProc(void* param) {
  GuestFiber* fs = (GuestFiber*)param;
  PPCContext& ctx = *rex::runtime::current_ppc_context();
  uint8_t* base = fs->base;

  REXLOG_DEBUG("[doa4][Fiber] proc start buf={:#x} lr={:#x} r1={:#x} r3={:#x} r31={:#x}",
               fs->buf, (uint32_t)ctx.lr, ctx.r1.u32, ctx.r3.u32, ctx.r31.u32);

  // The original swap has already loaded the fiber's saved state into ctx.
  // Only a buffer whose saved LR is the bootstrap sub_82793F70 is a *fresh*
  // fiber (one that hasn't run yet). For those we need to dispatch the entry
  // function (ctx.r31) ourselves; for anything else, the fiber was
  // previously running on a different host stack we never knew about, and
  // the only sensible thing is to park here -- the title shouldn't actually
  // SwitchToFiber to such a buffer since we register every fiber the first
  // time we see it, but we guard just in case.
  if ((uint32_t)ctx.lr == 0x82793F70u) {
    uint32_t entry = ctx.r31.u32;
    uint32_t arg = LoadU32(base, fs->buf + 0);
    ctx.r3.s64 = (int32_t)arg;
    ctx.ctr.u64 = entry;
    ctx.lr = 0;
    fs->dispatched = true;  // FIX 7: mark this host fiber as carrying a live
                            // dispatch so a later fresh reuse of the buffer
                            // recreates it instead of resuming this chain.

    REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);

    REXLOG_ERROR("[doa4][Fiber] buf={:#x} body returned (entry={:#x} arg={:#x}); parking",
                 fs->buf, entry, arg);
  } else {
    REXLOG_ERROR("[doa4][Fiber] buf={:#x} GuestFiberProc on non-fresh fiber (lr={:#x}); parking",
                 fs->buf, (uint32_t)ctx.lr);
  }

  // Fibers MUST NOT let their proc return. Park forever, yielding to the
  // initial fiber so the rest of the title can keep running.
  while (true) {
    if (tls_initial_fiber && tls_initial_fiber != GetCurrentFiber()) {
      SwitchToFiber(tls_initial_fiber);
    } else {
      Sleep(1000);
    }
  }
}

}  // namespace

REX_HOOK_RAW(sub_82793F80) { DoFiberSwap(ctx, base); }

// ===========================================================================
// FIX 4 — Secondary KiSwapContext entry point `sub_827925E8`.
//
// The title has a second fiber-swap symbol that shares the same calling
// convention and semantics as sub_82793F80 (it's a sibling trampoline,
// reached via a different call site). It hits the same `blr`-vs-`return`
// mismatch problem and needs the same Win32-fiber treatment as FIX 3.
// Route it through the shared DoFiberSwap helper.
// ===========================================================================
REX_HOOK_RAW(sub_827925E8) { DoFiberSwap(ctx, base); }

// ===========================================================================
// FIX 5 — Optional-callback gate `sub_82A04560` (root cause of black screen).
//
// The function dispatches through a function-pointer slot at guest 0x835A5008:
//     lis  r4, -31910        ; r4 = 0x835A0000
//     lwz  r0, 20488(r4)     ; r0 = *(0x835A5008)
//     cmpwi r0, 0
//     mtctr r0
//     bnectr                 ; tail-call if non-zero
//     ;; falls off the end without touching r3
//
// The slot is never written by any PPC code in the title — it ships NULL
// (typical "optional validation/perf-trace callback" idiom). The fall-through
// path leaves r3 untouched, so callers see whatever r3 they passed in.
//
// The SSM shader-bytecode loader `sub_8290DDA0` uses this as a gate:
//     addi  r3, r31, 16
//     bl    0x82a04560
//     cmpwi r3, 0
//     bne   error_path       ; skips `bl 0x82897FB8` (SetBytecode)
// Because the gate's r3 stays as `self+16` (non-zero), the setter is never
// called, `*(self+44)` stays NULL, and FIX 2's zero-buffer bypass kicks in —
// giving the GPU stub shader bytecode and a permanently black framebuffer.
//
// The correct semantics for an empty callback slot is "no callback installed
// → success (0)". Override the function to return r3=0 in that case.
// ===========================================================================
extern "C" {
REX_EXTERN(__imp__sub_82A04560);
}

REX_HOOK_RAW(sub_82A04560) {
  constexpr uint32_t kSlot = 0x835A5008;
  uint32_t slot = LoadU32(base, kSlot);
  if (slot == 0) {
    static std::atomic<uint64_t> s_n{0};
    uint64_t n = s_n.fetch_add(1, std::memory_order_relaxed);
    if (n < 8 || (n & 0xFFFF) == 0) {
      REXLOG_INFO("[doa4][FIX][CbGate] sub_82A04560 #{} slot empty -> r3=0 (lr={:#x})",
                  n, (uint32_t)ctx.lr);
    }
    ctx.r3.s64 = 0;
    return;
  }
  __imp__sub_82A04560(ctx, base);
}

// NOTE: a former "FIX 6" hooked MmDeleteKernelStack to retain (never free)
// kernel stacks, on the theory that the Start-screen crash was a reaped-while-
// yielded stack. That theory was DISPROVEN (the stack was intact; the real bug
// was stale host-fiber reuse, fixed in FIX 7 in this file). The retain hook
// never fired before the crash and only risked leaking the 0x70000000 stack
// heap over long sessions, so it has been removed — kernel stacks free normally.

// ===========================================================================
// FIX 8 — sub_8279CDF8 missing-epilogue ABI repair (snapshot-save crash).
//
// sub_8279CDF8 (a vtable method used by the snapshot/photo save path) was
// recompiled WITHOUT its function epilogue: it ends with `bl sub_82A04220` and
// then returns, never restoring r1, r31, or lr (compare the very next function
// sub_8279CE30, which restores them). sub_82A04220 is an ordinary function that
// returns normally, so this is a genuine codegen miss, not a tail call. The
// result: sub_8279CDF8 returns to its caller (sub_827DD6D0) with the stack
// pointer and non-volatile registers clobbered. sub_827DD6D0 then does
// `stw r29,420(r30)` with r30 no longer the object -> AV writing guest 0x1a4.
//
// Per the PPC ABI a normal call must preserve r1, lr and the non-volatile GPRs
// (r14-r31). We enforce exactly that: snapshot them on entry, run the (buggy)
// recompiled body for its side effects, then restore them. r3 (return value) is
// left as-is; the sole caller overwrites r3 immediately, so it doesn't matter.
// ===========================================================================
extern "C" {
REX_EXTERN(__imp__sub_8279CDF8);
}

REX_HOOK_RAW(sub_8279CDF8) {
  const uint32_t s_r1 = ctx.r1.u32;
  const uint64_t s_lr = ctx.lr;
  const uint64_t s_r14 = ctx.r14.u64, s_r15 = ctx.r15.u64, s_r16 = ctx.r16.u64,
                 s_r17 = ctx.r17.u64, s_r18 = ctx.r18.u64, s_r19 = ctx.r19.u64,
                 s_r20 = ctx.r20.u64, s_r21 = ctx.r21.u64, s_r22 = ctx.r22.u64,
                 s_r23 = ctx.r23.u64, s_r24 = ctx.r24.u64, s_r25 = ctx.r25.u64,
                 s_r26 = ctx.r26.u64, s_r27 = ctx.r27.u64, s_r28 = ctx.r28.u64,
                 s_r29 = ctx.r29.u64, s_r30 = ctx.r30.u64, s_r31 = ctx.r31.u64;

  __imp__sub_8279CDF8(ctx, base);

  ctx.r1.u32 = s_r1;  ctx.lr = s_lr;
  ctx.r14.u64 = s_r14; ctx.r15.u64 = s_r15; ctx.r16.u64 = s_r16; ctx.r17.u64 = s_r17;
  ctx.r18.u64 = s_r18; ctx.r19.u64 = s_r19; ctx.r20.u64 = s_r20; ctx.r21.u64 = s_r21;
  ctx.r22.u64 = s_r22; ctx.r23.u64 = s_r23; ctx.r24.u64 = s_r24; ctx.r25.u64 = s_r25;
  ctx.r26.u64 = s_r26; ctx.r27.u64 = s_r27; ctx.r28.u64 = s_r28; ctx.r29.u64 = s_r29;
  ctx.r30.u64 = s_r30; ctx.r31.u64 = s_r31;

  static std::atomic<uint64_t> s_n{0};
  uint64_t n = s_n.fetch_add(1, std::memory_order_relaxed);
  if (n < 8) {
    REXLOG_INFO("[doa4][FIX8] sub_8279CDF8 #{} NV/SP restored (r1={:#x} r30={:#x} r31={:#x})",
                n, s_r1, (uint32_t)s_r30, (uint32_t)s_r31);
  }
}

// NOTE: a former "FIX 9" hooked the pool allocator sub_827962D0 to bypass its
// per-heap process-type KeBugCheck(0xF4) guard by transiently clearing the
// 0x40000 "process-restricted" flag bit. It was superseded by FIX 11 below,
// which fixes the shared root (KeGetCurrentProcessType) and so covers ALL the
// sibling allocators that carry the same guard. FIX 9 was removed to drop its
// per-allocation overhead on that hot path.

// ===========================================================================
// FIX 10 — image decoder graceful-fail on format mismatch (snapshot save).
//
// Saving a snapshot must decode the game's embedded 64x64 PNG save-icon. The
// image loader sub_827A26F0 AUTO-DETECTS the format by trying each decoder in a
// fixed order (format list {0,5,4,1,3,8,7,2,6}); a decoder that returns a
// negative value means "not my format — try the next one".
//
// Format 1 in that list is sub_8279FAA8, the JPEG decoder. When handed the PNG
// it correctly detects the mismatch and heads for its E_FAIL (0x80004005)
// return — but on the way out its finalize step (sub_827DBB50 -> sub_827DB998)
// UNCONDITIONALLY dereferences the decoder's output-stream object at
// session+0x1A8 (offset 424). On an aborted/failed decode that object was never
// created, so the field is NULL and sub_827DB998 faults (observed: AV read at
// guest 0x0). That crash happens *instead of* the clean E_FAIL return, so the
// auto-detect loop never advances to the PNG decoder later in the list.
//
// (The earlier-tried decoders 0/5/4 already fail gracefully on the PNG — that's
// how execution even reaches format 1 — so only the JPEG path is broken.)
//
// sub_827DB998 exists solely to flush that output-stream object; when it is NULL
// there is nothing to flush. Skip the whole function in that case, letting the
// JPEG decoder return E_FAIL cleanly so format auto-detect proceeds to (and
// succeeds with) the PNG decoder. The normal success path always has a valid
// +424 object, so it is unaffected.
// ===========================================================================
extern "C" {
REX_EXTERN(__imp__sub_827DB998);
}

REX_HOOK_RAW(sub_827DB998) {
  const uint32_t session = ctx.r3.u32;
  if (session && LoadU32(base, session + 424) == 0) {
    static std::atomic<uint64_t> s_n{0};
    uint64_t n = s_n.fetch_add(1, std::memory_order_relaxed);
    if (n < 8) {
      REXLOG_INFO("[doa4][FIX10] sub_827DB998 skipped: session={:#x} +424 NULL "
                  "(aborted decode, nothing to flush) (lr={:#x})",
                  session, (uint32_t)ctx.lr);
    }
    return;  // leave r3 as-is; caller ignores it and proceeds to E_FAIL return
  }
  __imp__sub_827DB998(ctx, base);
}

// ===========================================================================
// FIX 11 — KeGetCurrentProcessType returns the title process type (generalizes
// FIX 9).
//
// The title's pool-allocator family guards every "process-restricted" heap
// (flag bit 0x40000 at heap+0x14) with:
//     if (KeGetCurrentProcessType() != *(heap+0x17B)) KeBugCheckEx(0xF4, ...)
// This guard appears in SEVERAL sibling allocators (recomp.45 has KeBugCheckEx
// sites at ~0x827963xx [FIX 9's sub_827962D0], 0x82796C28, and more), so the
// surgical per-function flag-clear in FIX 9 only covered one of them and the
// snapshot save still bugchecked from a different allocator.
//
// All of the title's heaps are owned by the title process (observed owner type =
// X_PROCTYPE_USER = 1). The panic happens only because rexglue's
// KeGetCurrentProcessType reads the per-thread KTHREAD.process_type (or
// processtype_value_in_dpc when a DPC/APC frame is active), which on the save
// path doesn't equal the title type. In a single-title recompilation the
// "current process" is ALWAYS the title, so the correct return is simply the
// title process type. Overriding the import to return it satisfies every such
// guard at once (and is what the per-thread value should already be).
// ===========================================================================
extern "C" {
REX_EXTERN(__imp__KeGetCurrentProcessType);
}

REX_HOOK_RAW(KeGetCurrentProcessType) {
  uint32_t pt = 1;  // X_PROCTYPE_USER (fallback if kernel state not ready)
  if (auto* ks = rex::system::kernel_state()) {
    pt = ks->process_type();
  }
  ctx.r3.u64 = pt;
}

// ===========================================================================
// FIX 12 — JPEG decoder rejects non-JPEG input up front (snapshot save).
//
// Saving a snapshot decodes the game's embedded 64x64 PNG save-icon through an
// auto-detect image loader (sub_827A26F0) that tries each decoder in turn and
// expects a negative return to mean "not my format, try the next". Format 1 is
// sub_8279FAA8, the JPEG decoder (r3=loader ctx, r4=input buffer, r5=size).
//
// On the PNG, sub_8279FAA8 does NOT bail at its `FF D8` SOI check — it keeps
// going, interpreting PNG bytes as JPEG markers/segment-lengths. A segment
// handler (sub_827DC5F0) then reads a bogus 16-bit length, walks off into a
// wild pointer, and does an out-of-bounds store (observed: AV write at random
// guest addresses; also previously a NULL deref in finalize, see FIX 10). So
// the decoder crashes instead of returning its own E_FAIL, and auto-detect
// never reaches the PNG decoder.
//
// A JPEG file ALWAYS begins with the SOI marker 0xFF 0xD8. Reject any input
// that doesn't, returning sub_8279FAA8's own failure code (E_FAIL 0x80004005,
// negative) so the loader advances to the next decoder (the PNG one). Genuine
// JPEGs (0xFF 0xD8 ...) still run the real decoder unchanged.
// ===========================================================================
extern "C" {
REX_EXTERN(__imp__sub_8279FAA8);
}

REX_HOOK_RAW(sub_8279FAA8) {
  const uint32_t buf = ctx.r4.u32;
  const uint32_t size = ctx.r5.u32;
  if (buf && size >= 2) {
    uint8_t b0 = base[buf];
    uint8_t b1 = base[buf + 1];
    if (!(b0 == 0xFF && b1 == 0xD8)) {  // not a JPEG (SOI marker missing)
      static std::atomic<uint64_t> s_n{0};
      uint64_t n = s_n.fetch_add(1, std::memory_order_relaxed);
      if (n < 8) {
        REXLOG_INFO("[doa4][FIX12] sub_8279FAA8 not-a-JPEG (magic {:#04x} {:#04x}) -> "
                    "E_FAIL so auto-detect tries next decoder (buf={:#x} size={:#x})",
                    b0, b1, buf, size);
      }
      ctx.r3.u64 = 0x80004005;  // E_FAIL (negative) = "not my format, try next"
      return;
    }
  }
  __imp__sub_8279FAA8(ctx, base);
}

// Shutdown watchdog. ReXGlue's teardown sometimes deadlocks in SDL audio
// shutdown (SDL_DestroyAudioStream waits for the SDL audio thread, which,
// like other exiting threads, waits forever on the Windows loader lock), so
// the process outlives its window. Seen in 5 of 6 window closes on the
// unmodified v0.8.0 build. The guest has stopped by the time this is armed,
// so if teardown has not finished after 5 s the process is ended outright
// (TerminateProcess: ExitProcess would need the same loader lock).
extern "C" void doa4_arm_shutdown_watchdog() {
  REXLOG_INFO("[doa4][LIFE] shutdown watchdog armed (5 s)");
  HANDLE thread = CreateThread(
      nullptr, 0,
      [](LPVOID) -> DWORD {
        Sleep(5000);
        TerminateProcess(GetCurrentProcess(), 0);
        return 0;
      },
      nullptr, 0, nullptr);
  if (thread) CloseHandle(thread);
}

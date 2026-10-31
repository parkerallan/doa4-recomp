// In-game menu (Escape): Video and Extras tabs. See doa4_menu.h.

#include "doa4_menu.h"

#include <imgui.h>

#include <array>
#include <cfloat>
#include <optional>
#include <string>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/window.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "doa4_config.h"
#include "doa4_graphics.h"
#include "doa4_unlocks.h"

namespace doa4 {

namespace {

constexpr ImVec4 kErrorColor(1.0f, 0.45f, 0.4f, 1.0f);
constexpr ImVec4 kNoteColor(1.0f, 0.85f, 0.4f, 1.0f);
constexpr ImVec4 kDoneColor(0.45f, 0.9f, 0.5f, 1.0f);

struct UnlockRow {
  unlocks::Unlock what;
  const char* label;
};
constexpr std::array<UnlockRow, 5> kUnlockRows = {{
    {unlocks::Unlock::kCharacters, "Unlock all characters"},
    {unlocks::Unlock::kCostumes, "Unlock all costumes"},
    {unlocks::Unlock::kVoices, "Unlock all system voices"},
    {unlocks::Unlock::kStages, "Unlock all stages"},
    {unlocks::Unlock::kEverything, "Unlock everything"},
}};

}  // namespace

MenuDialog::MenuDialog(rex::ui::ImGuiDrawer* drawer, MenuHost host)
    : rex::ui::ImGuiDialog(drawer), host_(std::move(host)) {
}

MenuDialog::~MenuDialog() = default;

void MenuDialog::OnDraw(ImGuiIO& io) {
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                          ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
  if (ImGui::Begin("DOA4###doa4_menu", nullptr, ImGuiWindowFlags_NoCollapse)) {
    if (ImGui::BeginTabBar("tabs")) {
      if (ImGui::BeginTabItem("Video")) {
        DrawVideoTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Extras")) {
        DrawExtrasTab();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    if (!status_.empty()) {
      ImGui::Separator();
      ImGui::TextColored(kErrorColor, "%s", status_.c_str());
    }
    ImGui::Separator();
    if (ImGui::Button("Close (Esc)")) {
      host_.defer(host_.close_menu);
    }
  }
  ImGui::End();
}

void MenuDialog::DrawVideoTab() {
  ImGui::SeparatorText("Display mode");
  const bool fullscreen = host_.window->IsFullscreen();
  bool want = fullscreen;
  if (ImGui::RadioButton("Windowed", !fullscreen)) want = false;
  if (ImGui::RadioButton("Borderless fullscreen", fullscreen)) want = true;
  if (want != fullscreen) {
    rex::ui::Window* window = host_.window;
    host_.defer([window, want] { window->SetFullscreen(want); });
    SetAndSave("fullscreen", want ? "true" : "false", false);
  }

  ImGui::SeparatorText("Render scale");
  int& saved_scale = *host_.saved_scale;
  for (int scale = 1; scale <= int(graphics::kRenderScales.size()); ++scale) {
    if (ImGui::RadioButton(graphics::kRenderScales[scale - 1], saved_scale == scale) &&
        saved_scale != scale) {
      // resolution_scale is read once at start-up: save it for the next start
      // without changing the running value.
      std::string error =
          SaveTomlKey(host_.config_path, "resolution_scale", std::to_string(scale));
      if (error.empty()) {
        saved_scale = scale;
        status_.clear();
      } else {
        status_ = "Could not save settings: " + error;
      }
    }
  }
  if (saved_scale != host_.running_scale) {
    ImGui::TextColored(kNoteColor, "Applies after a restart (running at x%d).",
                       host_.running_scale);
    if (ImGui::Button("Restart now")) {
      Restart();
    }
    if (const char* warning = UnsavedUnlocksWarning(*host_.extras)) {
      ImGui::TextColored(kErrorColor, "%s", warning);
    }
  }

  ImGui::SeparatorText("Anti-aliasing");
  ImGui::PushID("aa");
  const int aa = graphics::AntiAliasingIndex(rex::cvar::Query<std::string>("swap_post_effect"));
  for (int i = 0; i < int(graphics::kAntiAliasing.size()); ++i) {
    if (i) ImGui::SameLine();
    if (ImGui::RadioButton(graphics::kAntiAliasing[i].label, aa == i) && aa != i &&
        SaveThenSet("swap_post_effect", graphics::kAntiAliasing[i].value, true) &&
        host_.set_post_effect) {
      host_.set_post_effect(i);
    }
  }
  ImGui::PopID();
  ImGui::TextDisabled("The game uses 2x MSAA; FXAA Extreme suits it best. FXAA also softens");
  ImGui::TextDisabled("text a little.");

  ImGui::SeparatorText("Texture filtering");
  ImGui::PushID("aniso");
  const int32_t aniso = rex::cvar::Query<int32_t>("anisotropic_override");
  const int row = graphics::TextureFilteringIndex(aniso);
  for (int i = 0; i < int(graphics::kTextureFiltering.size()); ++i) {
    if (i) ImGui::SameLine();
    if (ImGui::RadioButton(graphics::kTextureFiltering[i].label, row == i) && row != i) {
      // Read by the SDK each time a texture is bound: no further call needed.
      SaveThenSet("anisotropic_override", graphics::kTextureFiltering[i].value, false);
    }
  }
  ImGui::PopID();
  if (row < 0) {
    ImGui::TextColored(kNoteColor, "Custom (%d) set in dead_or_alive_4.toml.", aniso);
  }
  ImGui::TextDisabled("Applies to textures as the game draws them; you may see it after the");
  ImGui::TextDisabled("next screen loads.");

  ImGui::SeparatorText("Diagnostics");
  // Mirrors F6 both ways: read from the overlay itself every frame.
  bool shown = host_.diag_overlay_shown && host_.diag_overlay_shown();
  if (ImGui::Checkbox("Show diagnostics overlay (F6)", &shown) && host_.toggle_diag_overlay) {
    host_.defer(host_.toggle_diag_overlay);
  }
  ImGui::TextDisabled("Swaps, draws, audio and scheduler figures. Off at every start.");
}

void MenuDialog::DrawExtrasTab() {
  ImGui::SeparatorText("Unlocks");
  const std::optional<uint32_t> profile =
      host_.guest ? unlocks::FindProfile(*host_.guest) : std::nullopt;
  if (!profile) {
    ImGui::TextDisabled("Load a profile first (reach the main menu).");
  }
  for (const UnlockRow& row : kUnlockRows) {
    const bool done = profile && unlocks::IsUnlocked(*host_.guest, *profile, row.what);
    ImGui::BeginDisabled(!profile || done);
    if (ImGui::Button(row.label, ImVec2(220.0f, 0.0f))) {
      std::string error;
      if (UnlockWithBackup(*host_.guest, *profile, row.what, host_.save_dir, host_.backup_root,
                           BackupStamp(), host_.extras, &error)) {
        status_.clear();
        REXLOG_INFO("[doa4][EXTRAS] {}: {}", row.label, host_.extras->last_change);
      } else {
        status_ = error;
        REXLOG_ERROR("[doa4][EXTRAS] {}: {}", row.label, error);
      }
    }
    ImGui::EndDisabled();
    if (done) {
      ImGui::SameLine();
      ImGui::TextColored(kDoneColor, "Unlocked");
    }
    if (row.what == unlocks::Unlock::kStages) {
      ImGui::Indent();
      ImGui::TextWrapped(
          "Also unlocks characters, costumes and voices, and records one Story clear for each "
          "Story character (shown in Records).");
      ImGui::Unindent();
    }
  }

  ExtrasState& state = *host_.extras;
  if (ImGui::GetTime() >= next_save_poll_) {
    if (host_.guest) PollSaved(*host_.guest, host_.save_dir, &state);
    next_save_poll_ = ImGui::GetTime() + 1.0;
  }
  if (!state.last_change.empty()) {
    ImGui::Spacing();
    ImGui::TextWrapped("%s", state.last_change.c_str());
  }
  if (state.waiting_for_save) {
    ImGui::TextColored(kNoteColor, "Waiting for the game to save...");
    ImGui::TextWrapped(
        "Change any Options setting, or finish a mode, to save now. Re-enter character or "
        "stage select to see the change.");
  } else if (state.saved) {
    ImGui::TextColored(kDoneColor, "Saved.");
  }
  if (!state.backup.empty()) {
    ImGui::TextWrapped("Backup of your save from before the first unlock: %s",
                       state.backup.string().c_str());
  } else if (!state.backup_note.empty()) {
    ImGui::TextWrapped("%s", state.backup_note.c_str());
  }

  ImGui::SeparatorText("Game tweaks");
  for (size_t i = 0; i < tweaks::AllTweaks().size(); ++i) {
    const tweaks::Tweak& tweak = tweaks::AllTweaks()[i];
    bool on = rex::cvar::Query<bool>(tweak.cvar);
    ImGui::BeginDisabled(!host_.guest);  // a tweak is applied to guest memory
    if (ImGui::Checkbox(tweak.label, &on)) {
      SetTweak(i, on);
    }
    ImGui::EndDisabled();
    if (i == tweaks::kHelenaOpera) {
      ImGui::TextDisabled("Her 6th costume, once her costumes are unlocked (Unlock all costumes");
      ImGui::TextDisabled("above, or by playing).");
    }
  }
}

void MenuDialog::SetTweak(size_t index, bool on) {
  const tweaks::Tweak& tweak = tweaks::AllTweaks()[index];
  const char* value = on ? "true" : "false";
  // Saved first: when the save fails the running game keeps its state.
  std::string error = SaveTomlKey(host_.config_path, tweak.cvar, value);
  if (!error.empty()) {
    status_ = "Could not save settings: " + error;
    return;
  }
  status_.clear();
  rex::cvar::SetFlagByName(tweak.cvar, value);
  std::string apply_error = host_.patcher->Apply(*host_.guest, index, on, host_.protect);
  if (!apply_error.empty()) {
    status_ = fmt::format("{}: {}", tweak.label, apply_error);
  }
  REXLOG_INFO("[doa4][EXTRAS] {} = {}: {}", tweak.cvar, value,
              apply_error.empty() ? "applied" : apply_error);
}

void MenuDialog::Restart() {
  wchar_t exe[MAX_PATH];
  DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
  if (length == 0 || length == MAX_PATH) {
    status_ = "Restart failed: cannot find the game's executable.";
    return;
  }
  std::wstring command_line = GetCommandLineW();
  STARTUPINFOW startup_info{};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process{};
  // The new instance waits for this one to exit (DeadOrAlive4App::OnPreSetup)
  // before it opens the save data and the shader cache.
  SetEnvironmentVariableW(L"DOA4_WAIT_PID", std::to_wstring(GetCurrentProcessId()).c_str());
  BOOL created = CreateProcessW(exe, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                nullptr, &startup_info, &process);
  DWORD error = GetLastError();
  SetEnvironmentVariableW(L"DOA4_WAIT_PID", nullptr);
  if (!created) {
    status_ = fmt::format("Restart failed (Windows error {}).", error);
    REXLOG_ERROR("[doa4][menu] restart: CreateProcessW failed, error {}", error);
    return;
  }
  REXLOG_INFO("[doa4][menu] restarting: new process {}", process.dwProcessId);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  rex::ui::Window* window = host_.window;
  host_.defer([window] { window->RequestClose(); });
}

bool MenuDialog::SaveThenSet(std::string_view name, std::string_view value, bool quote) {
  std::string error =
      SaveTomlKey(host_.config_path, name, quote ? TomlString(value) : std::string(value));
  if (!error.empty()) {
    status_ = "Could not save settings: " + error;
    return false;
  }
  status_.clear();
  if (!rex::cvar::SetFlagByName(name, value)) {
    status_ = fmt::format("Could not set {} to {}.", name, value);
    return false;
  }
  REXLOG_INFO("[doa4][GFX] {} = {} saved", name, value);
  return true;
}

void MenuDialog::SetAndSave(std::string_view name, std::string_view value, bool quote) {
  if (!rex::cvar::SetFlagByName(name, value)) {
    status_ = fmt::format("Could not set {} to {}.", name, value);
    return;
  }
  std::string error =
      SaveTomlKey(host_.config_path, name, quote ? TomlString(value) : std::string(value));
  status_ = error.empty() ? std::string() : "Could not save settings: " + error;
}

}  // namespace doa4

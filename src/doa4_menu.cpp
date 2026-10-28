// In-game menu (Escape): Video, Controls and Extras tabs. See doa4_menu.h.

#include "doa4_menu.h"

#include <imgui.h>

#include <array>
#include <cfloat>
#include <optional>
#include <string>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/input/pad_remap.h>
#include <rex/logging.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/window.h>
#include <rex/ui/window_win.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "doa4_config.h"
#include "doa4_gamefiles.h"
#include "doa4_graphics.h"
#include "doa4_photo_win.h"
#include "doa4_utf8.h"
#include "doa4_unlocks.h"

namespace doa4 {

namespace {

using rex::input::PadControl;
using rex::input::PadRemap;
using rex::X_RESULT;  // X_ERROR_SUCCESS expands to an unqualified X_RESULT

constexpr std::array<std::string_view, 4> kPadRemapCvars = {"pad_remap_p1", "pad_remap_p2",
                                                            "pad_remap_p3", "pad_remap_p4"};

// Indexed by PadControl.
constexpr std::array<const char*, rex::input::kPadControlCount> kPadLabels = {
    "A",     "B",    "X",           "Y",           "LB",       "RB",         "LT",         "RT",
    "Back",  "Start", "L-stick click", "R-stick click", "D-pad up", "D-pad down", "D-pad left",
    "D-pad right"};

struct KeyRow {
  const char* cvar;
  const char* label;
};
// The SDK's keyboard-as-controller binds (src/input/mnk/mnk_input_driver.cpp).
constexpr std::array<KeyRow, 21> kKeyRows = {{
    {"keybind_a", "A"},
    {"keybind_b", "B"},
    {"keybind_x", "X"},
    {"keybind_y", "Y"},
    {"keybind_left_shoulder", "LB"},
    {"keybind_right_shoulder", "RB"},
    {"keybind_left_trigger", "LT"},
    {"keybind_right_trigger", "RT"},
    {"keybind_back", "Back"},
    {"keybind_start", "Start"},
    {"keybind_guide", "Guide"},
    {"keybind_lstick_press", "L-stick click"},
    {"keybind_rstick_press", "R-stick click"},
    {"keybind_lstick_up", "L-stick up"},
    {"keybind_lstick_down", "L-stick down"},
    {"keybind_lstick_left", "L-stick left"},
    {"keybind_lstick_right", "L-stick right"},
    {"keybind_dpad_up", "D-pad up"},
    {"keybind_dpad_down", "D-pad down"},
    {"keybind_dpad_left", "D-pad left"},
    {"keybind_dpad_right", "D-pad right"},
}};

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

uint32_t PressedControls(const rex::input::X_INPUT_GAMEPAD& pad) {
  uint32_t mask = 0;
  for (size_t i = 0; i < rex::input::kPadControlCount; ++i) {
    if (rex::input::IsPadControlPressed(pad, static_cast<PadControl>(i))) mask |= 1u << i;
  }
  return mask;
}

PadRemap CurrentRemap(int slot) {
  PadRemap remap;
  rex::input::ParsePadRemap(rex::cvar::Query<std::string>(kPadRemapCvars[slot]), &remap);
  return remap;
}

}  // namespace

MenuDialog::MenuDialog(rex::ui::ImGuiDrawer* drawer, MenuHost host)
    : rex::ui::ImGuiDialog(drawer), host_(std::move(host)) {
  host_.window->AddInputListener(this, kInputZOrder);
}

MenuDialog::~MenuDialog() {
  host_.window->RemoveInputListener(this);
}

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
      if (ImGui::BeginTabItem("Controls")) {
        DrawControlsTab();
        ImGui::EndTabItem();
      } else if (listen_ != Listen::kNone) {
        listen_ = Listen::kNone;  // left the tab while a row was listening
      }
      if (ImGui::BeginTabItem("Photo")) {
        DrawPhotoTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Extras")) {
        DrawExtrasTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Game")) {
        DrawGameTab();
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
}

void MenuDialog::DrawControlsTab() {
  DrawPadSection();
  ImGui::Spacing();
  DrawKeyboardSection();
}

void MenuDialog::DrawPadSection() {
  ImGui::SeparatorText("Controller");
  for (int slot = 0; slot < 4; ++slot) {
    if (slot) ImGui::SameLine();
    std::string label = fmt::format("Player {}", slot + 1);
    if (ImGui::RadioButton(label.c_str(), slot_ == slot) && slot_ != slot) {
      slot_ = slot;
      pad_connected_ = false;
      if (listen_ == Listen::kPad) listen_ = Listen::kNone;
    }
  }
  if (rex::cvar::Query<std::string>("input_backend") != "sdl") {
    ImGui::TextWrapped("Controller remapping needs the SDL input backend (input_backend = sdl).");
    return;
  }

  rex::input::InputSystem* input = host_.input ? host_.input() : nullptr;
  rex::input::X_INPUT_STATE raw{};
  const X_RESULT result = input ? input->GetRawState(slot_, &raw) : X_ERROR_DEVICE_NOT_CONNECTED;
  if (result != X_ERROR_BUSY) {  // busy: keep last frame's answer
    pad_connected_ = result == X_ERROR_SUCCESS;
    if (pad_connected_ && listen_ == Listen::kPad) {
      PollPadListen(raw.gamepad);
    }
  }
  const bool connected = pad_connected_ && result == X_ERROR_SUCCESS;
  if (!pad_connected_) {
    ImGui::TextDisabled("No controller in this slot.");
  }

  const PadRemap remap = CurrentRemap(slot_);
  if (ImGui::BeginTable("pad", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthFixed, 140.0f);
    ImGui::TableSetupColumn("Driven by");
    for (size_t row = 0; row < rex::input::kPadControlCount; ++row) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(kPadLabels[row]);
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(row));
      const bool listening = listen_ == Listen::kPad && listen_row_ == row;
      const char* text = listening ? "Press a button... (Esc cancels)"
                                   : kPadLabels[static_cast<size_t>(remap.source[row])];
      if (ImGui::Button(text, ImVec2(-FLT_MIN, 0.0f)) && !listening) {
        StartPadListen(row, connected ? &raw.gamepad : nullptr);
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  std::string reset = fmt::format("Reset Player {}", slot_ + 1);
  if (ImGui::Button(reset.c_str())) {
    listen_ = Listen::kNone;
    SetAndSave(kPadRemapCvars[slot_], "", true);
  }
}

void MenuDialog::DrawKeyboardSection() {
  ImGui::SeparatorText("Keyboard");
  bool mnk_mode = rex::cvar::Query<bool>("mnk_mode");
  if (ImGui::Checkbox("Use keyboard as controller", &mnk_mode)) {
    SetAndSave("mnk_mode", mnk_mode ? "true" : "false", false);
  }
  const int32_t user = rex::cvar::Query<int32_t>("mnk_user_index");
  ImGui::TextUnformatted("Keyboard is");
  for (int32_t slot = 0; slot < 4; ++slot) {
    ImGui::SameLine();
    std::string label = fmt::format("P{}##kbd", slot + 1);
    if (ImGui::RadioButton(label.c_str(), user == slot) && user != slot) {
      SetAndSave("mnk_user_index", std::to_string(slot), false);
    }
  }

  if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthFixed, 140.0f);
    ImGui::TableSetupColumn("Key");
    for (size_t row = 0; row < kKeyRows.size(); ++row) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(kKeyRows[row].label);
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(100 + row));
      const bool listening = listen_ == Listen::kKey && listen_row_ == row;
      std::string key = rex::cvar::Query<std::string>(kKeyRows[row].cvar);
      std::string text = listening ? "Press a key or mouse button... (Esc cancels)"
                                   : (key.empty() ? "(none)" : key);
      if (ImGui::Button(text.c_str(), ImVec2(-FLT_MIN, 0.0f)) && !listening) {
        listen_ = Listen::kKey;
        listen_row_ = row;
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (ImGui::Button("Reset keyboard")) {
    listen_ = Listen::kNone;
    for (size_t row = 0; row < kKeyRows.size(); ++row) {
      const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(kKeyRows[row].cvar);
      if (!info) continue;
      std::string value = info->default_value;
      if (value == "Escape") value = "Return";  // Escape opens this menu
      SetAndSave(kKeyRows[row].cvar, value, true);
    }
  }
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
                       Utf8(state.backup).c_str());
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
  }
}

void MenuDialog::DrawGameTab() {
  const gamefiles::GameTab tab = gamefiles::DescribeGameTab(
      host_.game_files, host_.game_files_from_command_line,
      PathFromUtf8(rex::cvar::Query<std::string>("doa4_game_files")));
  ImGui::SeparatorText("Game files");
  ImGui::TextWrapped("%s", Utf8(tab.now).c_str());
  if (!tab.note.empty()) ImGui::TextWrapped("%s", tab.note.c_str());
  ImGui::BeginDisabled(!tab.can_change || !host_.change_game_files);
  if (ImGui::Button("Change...")) {
    // After this frame, with the menu closed first (the setup screen takes
    // its place; the menu must not be destroyed while it is drawing).
    host_.defer([close = host_.close_menu, change = host_.change_game_files] {
      close();
      change();
    });
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Open folder")) {
    host_.defer([close = host_.close_menu, dir = tab.now] {
      close();
      photo::OpenInExplorer(dir);
    });
  }
  ImGui::TextDisabled("A new location is used from the next start.");
  ImGui::SeparatorText("Saves");
  ImGui::TextWrapped("%s", Utf8(host_.user_data).c_str());
}

void MenuDialog::DrawPhotoTab() {
  ImGui::SeparatorText("Save a photo");
  ImGui::TextWrapped(
      "Press F12 anywhere in the game. The photo is the game's own frame at the render scale, "
      "without this menu or any other overlay.");

  ImGui::SeparatorText("Format");
  ImGui::PushID("photo_format");
  const int format =
      photo::ChoiceIndex(photo::kFormats, rex::cvar::Query<std::string>("doa4_photo_format"));
  for (int i = 0; i < int(photo::kFormats.size()); ++i) {
    if (ImGui::RadioButton(photo::kFormats[i].label, format == i) && format != i) {
      SaveThenSet("doa4_photo_format", photo::kFormats[i].value, true);
    }
  }
  ImGui::PopID();
  ImGui::TextDisabled("Monitor size: at x4 the same clean, supersampled image you see on screen.");

  ImGui::SeparatorText("Folder");
  const std::string custom = rex::cvar::Query<std::string>("doa4_photo_dir");
  const std::filesystem::path folder =
      custom.empty() ? host_.photo_default_dir
                     : std::filesystem::path(std::u8string(custom.begin(), custom.end()));
  const std::u8string shown = folder.u8string();
  ImGui::TextWrapped("%s%s", reinterpret_cast<const char*>(shown.c_str()),
                     custom.empty() ? "  (default)" : "");
  if (ImGui::Button("Choose folder...")) {
    // After this frame: the folder dialog is modal and must not run inside
    // the menu's drawing.
    auto* win32 = dynamic_cast<rex::ui::Win32Window*>(host_.window);
    void* hwnd = win32 ? win32->hwnd() : nullptr;
    host_.defer([this, hwnd, folder] {
      if (std::optional<std::filesystem::path> chosen = photo::PickFolder(hwnd, folder)) {
        const std::u8string text = chosen->u8string();
        SaveThenSet("doa4_photo_dir", std::string(text.begin(), text.end()), true);
      }
    });
  }
  ImGui::SameLine();
  if (ImGui::Button("Open folder")) {
    // Close the menu first: while it is open the game is drawn from the UI
    // thread, and with Explorer in front the picture stopped until the menu
    // closed. After this frame, not inside the menu's drawing.
    host_.defer([close = host_.close_menu, folder] {
      if (close) close();
      photo::OpenInExplorer(folder);
    });
  }
  if (!custom.empty()) {
    ImGui::SameLine();
    if (ImGui::Button("Use default")) {
      SaveThenSet("doa4_photo_dir", "", true);
    }
  }

  ImGui::SeparatorText("In-game snapshots");
  bool auto_copy = rex::cvar::Query<bool>("doa4_photo_auto");
  if (ImGui::Checkbox("Also save a full-quality copy when you save a snapshot to the in-game Album",
                      &auto_copy)) {
    SaveThenSet("doa4_photo_auto", auto_copy ? "true" : "false", false);
  }
  ImGui::TextDisabled("The copy is the game's 3:2 photo, at the render scale's full quality.");

  if (host_.photo_result) {
    const photo::Result result = host_.photo_result();
    if (result.serial) {
      ImGui::Spacing();
      if (result.error) {
        ImGui::TextColored(kErrorColor, "%s", result.text.c_str());
      } else {
        ImGui::TextColored(kDoneColor, "%s", result.text.c_str());
      }
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

void MenuDialog::StartPadListen(size_t row, const rex::input::X_INPUT_GAMEPAD* pad) {
  listen_ = Listen::kPad;
  listen_row_ = row;
  // Inputs already held count only after they are released.
  pad_held_ = pad ? PressedControls(*pad) : 0;
}

void MenuDialog::PollPadListen(const rex::input::X_INPUT_GAMEPAD& pad) {
  const uint32_t pressed = PressedControls(pad);
  const uint32_t fresh = pressed & ~pad_held_;
  pad_held_ &= pressed;
  if (!fresh) return;
  size_t source = 0;
  while (!(fresh & (1u << source))) ++source;
  listen_ = Listen::kNone;
  AssignPad(listen_row_, source);
}

void MenuDialog::AssignPad(size_t target, size_t source) {
  PadRemap remap = CurrentRemap(slot_);
  rex::input::AssignPadControl(&remap, static_cast<PadControl>(target),
                               static_cast<PadControl>(source));
  SetAndSave(kPadRemapCvars[slot_], rex::input::FormatPadRemap(remap), true);
}

void MenuDialog::AssignKey(size_t row, const std::string& key_name) {
  const char* cvar = kKeyRows[row].cvar;
  const std::string previous = rex::cvar::Query<std::string>(cvar);
  if (previous == key_name) return;
  // A key already bound elsewhere swaps, so no key drives two controls.
  for (size_t other = 0; other < kKeyRows.size(); ++other) {
    if (other != row && rex::cvar::Query<std::string>(kKeyRows[other].cvar) == key_name) {
      SetAndSave(kKeyRows[other].cvar, previous, true);
    }
  }
  SetAndSave(cvar, key_name, true);
}

void MenuDialog::OnKeyDown(rex::ui::KeyEvent& e) {
  if (listen_ == Listen::kNone) return;
  if (e.virtual_key() == rex::ui::VirtualKey::kEscape) {
    listen_ = Listen::kNone;  // cancels the row; the menu stays open
    e.set_handled(true);
    return;
  }
  if (listen_ != Listen::kKey) return;
  std::string name = rex::ui::VirtualKeyToString(e.virtual_key());
  e.set_handled(true);
  if (name.empty()) return;  // a key the SDK cannot name: keep listening
  listen_ = Listen::kNone;
  AssignKey(listen_row_, name);
}

void MenuDialog::OnMouseDown(rex::ui::MouseEvent& e) {
  if (listen_ == Listen::kPad) {
    listen_ = Listen::kNone;  // a click elsewhere cancels; the click itself goes on to ImGui
    return;
  }
  if (listen_ != Listen::kKey) return;
  const char* name = nullptr;
  switch (e.button()) {
    case rex::ui::MouseEvent::Button::kLeft:
      name = "LMB";
      break;
    case rex::ui::MouseEvent::Button::kRight:
      name = "RMB";
      break;
    case rex::ui::MouseEvent::Button::kMiddle:
      name = "MMB";
      break;
    default:
      return;
  }
  e.set_handled(true);
  listen_ = Listen::kNone;
  AssignKey(listen_row_, name);
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
  REXLOG_INFO("[doa4][UI] {} = {} saved", name, value);
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

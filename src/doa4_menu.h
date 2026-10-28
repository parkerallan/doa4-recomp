// In-game menu, opened and closed with Escape (bind_doa4_menu).
//
// Tabs: Video (display mode, render scale), Controls (per-slot controller
// remap, keyboard binds), Photo, Extras (unlocks, game tweaks) and Game (where
// the game files are). Every setting is
// applied to its cvar and saved to dead_or_alive_4.toml with doa4::SaveTomlKey. The game keeps running while
// the menu is open; its input is blocked (DeadOrAlive4App::OnBlocksGuestInput),
// so the game sees an untouched controller and keyboard.
//
// Owned by DeadOrAlive4App: created when opened, destroyed when closed, so the
// presenter goes back to direct GPU-thread presents as soon as it closes.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include <rex/input/input.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/window_listener.h>

#include "doa4_extras.h"
#include "doa4_guest.h"
#include "doa4_photo_service.h"
#include "doa4_tweaks.h"

namespace rex::input {
class InputSystem;
}
namespace rex::ui {
class ImGuiDrawer;
class Window;
}  // namespace rex::ui

namespace doa4 {

struct MenuHost {
  rex::ui::Window* window = nullptr;
  std::filesystem::path config_path;  // <exe dir>/dead_or_alive_4.toml
  int running_scale = 1;              // resolution_scale the game started with
  // resolution_scale saved for the next start; owned by the app so it survives
  // the menu being closed and reopened. Changed only after a successful save.
  int* saved_scale = nullptr;
  // The runtime's input system, or null (before the runtime exists).
  std::function<rex::input::InputSystem*()> input;
  // Runs `fn` on the UI thread after the current frame: window changes and
  // closing the menu must not happen while the menu is drawing.
  std::function<void(std::function<void()> fn)> defer;
  // Destroys the menu (called through `defer`).
  std::function<void()> close_menu;
  // Switches the running game's anti-aliasing to graphics::kAntiAliasing[index]
  // (the SDK reads swap_post_effect only at start).
  std::function<void(int index)> set_post_effect;
  // Photo tab: the photo service's last result, and the folder photos go to
  // when doa4_photo_dir is empty.
  std::function<photo::Result()> photo_result;
  std::filesystem::path photo_default_dir;

  // Extras tab. `guest` is null until the XEX is loaded into guest memory.
  const GuestBytes* guest = nullptr;
  tweaks::TweakPatcher* patcher = nullptr;  // owned by the app
  tweaks::ProtectFn protect;
  std::filesystem::path save_dir;  // the ups.dat save package folder, or empty
  // Where the one-time save backup goes: outside the folder the game lists
  // its saves from.
  std::filesystem::path backup_root;
  ExtrasState* extras = nullptr;   // owned by the app: survives closing the menu
  // Game tab: the folder the game started from, whether --game_data_root
  // chose it, and the action that closes the menu and opens the setup screen
  // to choose another (used from the next start).
  std::filesystem::path game_files;
  bool game_files_from_command_line = false;
  std::filesystem::path user_data;  // saves and the shader cache (shown only)
  std::function<void()> change_game_files;
};

class MenuDialog : public rex::ui::ImGuiDialog, public rex::ui::WindowInputListener {
 public:
  // Input listener z-order: above the ImGui drawer (64), so a key or click
  // that answers "press a key" never reaches ImGui, the binds or the game.
  static constexpr size_t kInputZOrder = 65;

  MenuDialog(rex::ui::ImGuiDrawer* drawer, MenuHost host);
  ~MenuDialog() override;

 protected:
  void OnDraw(ImGuiIO& io) override;
  void OnKeyDown(rex::ui::KeyEvent& e) override;
  void OnMouseDown(rex::ui::MouseEvent& e) override;

 private:
  enum class Listen { kNone, kPad, kKey };

  void DrawVideoTab();
  void DrawControlsTab();
  void DrawPadSection();
  void DrawKeyboardSection();
  void DrawExtrasTab();
  void DrawPhotoTab();
  void DrawGameTab();
  // Saves the tweak's cvar; on success sets it and, for a live tweak, writes
  // or reverts its bytes. Failures go to status_.
  void SetTweak(size_t tweak, bool on);
  // Saves `name` first; only on success sets the cvar. Failures go to
  // status_ and leave the running game as it was. Returns whether it saved.
  bool SaveThenSet(std::string_view name, std::string_view value, bool quote);
  void StartPadListen(size_t row, const rex::input::X_INPUT_GAMEPAD* pad);
  void PollPadListen(const rex::input::X_INPUT_GAMEPAD& pad);
  void AssignPad(size_t target, size_t source);
  void AssignKey(size_t row, const std::string& key_name);
  void Restart();
  // Sets the cvar and saves it; `quote` writes the value as a TOML string.
  // Failures go to status_.
  void SetAndSave(std::string_view name, std::string_view value, bool quote);

  MenuHost host_;
  int slot_ = 0;  // controller slot shown in the Controls tab
  Listen listen_ = Listen::kNone;
  size_t listen_row_ = 0;
  uint32_t pad_held_ = 0;  // PadControl bits held since pad listening started
  bool pad_connected_ = false;  // last non-busy GetRawState answer for slot_
  double next_save_poll_ = 0.0;  // ImGui time of the next "has the game saved?" check
  std::string status_;     // last error, shown in red
};

}  // namespace doa4

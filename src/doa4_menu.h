// In-game menu, opened and closed with Escape (bind_doa4_menu).
//
// Tabs: Video (display mode, render scale) and Extras (unlocks, game tweaks).
// Every setting is applied to its cvar and saved to dead_or_alive_4.toml with
// doa4::SaveTomlKey. The game keeps running while the menu is open; its pad
// input is blocked (doa4_input_block.h), so the game sees an untouched
// controller.
//
// Owned by DeadOrAlive4App: created when opened, destroyed when closed, so the
// presenter goes back to direct GPU-thread presents as soon as it closes.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include <rex/ui/imgui_dialog.h>

#include "doa4_extras.h"
#include "doa4_guest.h"
#include "doa4_tweaks.h"

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
  // Runs `fn` on the UI thread after the current frame: window changes and
  // closing the menu must not happen while the menu is drawing.
  std::function<void(std::function<void()> fn)> defer;
  // Destroys the menu (called through `defer`).
  std::function<void()> close_menu;
  // Switches the running game's anti-aliasing to graphics::kAntiAliasing[index]
  // (the SDK reads swap_post_effect only at start).
  std::function<void(int index)> set_post_effect;
  // The diagnostics overlay F6 toggles: whether it is shown, and a toggle
  // (called through `defer`: a dialog is not created or destroyed while the
  // menu draws).
  std::function<bool()> diag_overlay_shown;
  std::function<void()> toggle_diag_overlay;

  // Extras tab. `guest` is null until the XEX is loaded into guest memory.
  const GuestBytes* guest = nullptr;
  tweaks::TweakPatcher* patcher = nullptr;  // owned by the app
  tweaks::ProtectFn protect;
  std::filesystem::path save_dir;  // the ups.dat save package folder, or empty
  // Where the one-time save backup goes: outside the folder the game lists
  // its saves from.
  std::filesystem::path backup_root;
  ExtrasState* extras = nullptr;   // owned by the app: survives closing the menu
};

class MenuDialog : public rex::ui::ImGuiDialog {
 public:
  MenuDialog(rex::ui::ImGuiDrawer* drawer, MenuHost host);
  ~MenuDialog();  // ImGuiDialog's destructor is not virtual: owned as MenuDialog

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void DrawVideoTab();
  void DrawExtrasTab();
  // Saves the tweak's cvar; on success sets it and, for a live tweak, writes
  // or reverts its bytes. Failures go to status_.
  void SetTweak(size_t tweak, bool on);
  // Saves `name` first; only on success sets the cvar. Failures go to
  // status_ and leave the running game as it was. Returns whether it saved.
  bool SaveThenSet(std::string_view name, std::string_view value, bool quote);
  void Restart();
  // Sets the cvar and saves it; `quote` writes the value as a TOML string.
  // Failures go to status_.
  void SetAndSave(std::string_view name, std::string_view value, bool quote);

  MenuHost host_;
  double next_save_poll_ = 0.0;  // ImGui time of the next "has the game saved?" check
  std::string status_;  // last error, shown in red
};

}  // namespace doa4

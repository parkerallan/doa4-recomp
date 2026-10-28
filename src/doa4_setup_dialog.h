// The first-run setup screen (ImGui, in the game window): shown before the
// game starts when no game files are configured, and from the Esc menu's Game
// tab to choose another location. Draws a doa4::setup::Flow; owns its worker
// thread. See notes/specs/2026-10-28-first-run-setup-design.md.
//
// Test switch DOA4_SETUP_TEST (no file dialogs are opened):
//   folder=<path>               choose that extracted folder
//   iso=<path>|target=<path>    choose that image, extract it to <path>
// Each step is logged with [doa4][SETUP].

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <rex/ui/imgui_dialog.h>

#include "doa4_setup_flow.h"

namespace doa4 {

struct SetupHost {
  enum class Mode { kFirstRun, kChange };
  Mode mode = Mode::kFirstRun;
  void* hwnd = nullptr;   // owner of the file dialogs
  std::string reason;     // UTF-8; shown above the buttons when not empty
  std::filesystem::path current;  // kChange: the folder in use now
  // Runs `fn` on the UI thread after the current frame (file dialogs and
  // closing must not happen while the dialog is drawing).
  std::function<void(std::function<void()> fn)> defer;
  // Runs `fn` on the UI thread; callable from any thread.
  std::function<void(std::function<void()> fn)> post;
  // The chosen folder (checked or just extracted and verified). UI thread.
  // Returns "" or why it could not be saved (shown in kChange).
  std::function<std::string(const std::filesystem::path& folder)> done;
  // Quit (kFirstRun) or close the dialog (kChange). UI thread, deferred.
  std::function<void()> close;
  std::function<void(const std::string& line)> log;
};

class SetupDialog : public rex::ui::ImGuiDialog {
 public:
  SetupDialog(rex::ui::ImGuiDrawer* drawer, SetupHost host);
  ~SetupDialog();  // cancels a running copy and joins the worker (held as SetupDialog)

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  setup::Effects MakeEffects();
  void RunTestScript();
  void LogStage();

  SetupHost host_;
  // Guards posts: the destructor resets alive_ on the UI thread while the
  // worker may be posting, so the worker only ever copies alive_weak_, which
  // is written once here and never again.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  const std::weak_ptr<bool> alive_weak_ = alive_;
  setup::Flow flow_;
  std::thread worker_;
  setup::TestScript test_;
  bool test_started_ = false, test_extract_started_ = false;
  std::string save_error_;  // what `done` returned
  setup::Stage logged_stage_ = setup::Stage::kChoose;
  std::string logged_message_;
};

}  // namespace doa4

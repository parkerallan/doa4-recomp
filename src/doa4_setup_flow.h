// The first-run setup screen's logic, without ImGui: choose a disc image or
// an extracted folder, check it, pick where to extract, extract with progress
// and cancel. Every outside effect (file dialogs, the worker thread, the
// checks, the copy) is injected, so the host test drives it synchronously.
// See notes/specs/2026-10-28-first-run-setup-design.md.
//
// Threading: every Flow method and every `post`ed callback runs on the UI
// thread; `run_async` work runs on a worker and touches only its arguments,
// the cancel flag and the progress snapshot (locked). The owner (the setup
// dialog) joins the worker and drops pending posts before destroying a Flow.

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

#include "doa4_gamefiles.h"

namespace doa4::setup {

// Space asked for beyond the image's file bytes (file-system overhead and the
// one .part file being written).
inline constexpr uint64_t kSpaceSlack = 64ull << 20;

enum class Stage {
  kChoose,      // the two buttons (and a message, if any)
  kChecking,    // reading an image or folder
  kTarget,      // an image was accepted: where to extract it
  kExtracting,  // copying, with progress and Cancel
  kFinished,    // a folder was chosen (done was called)
};

struct Effects {
  // File dialogs (UI thread); nullopt = the user cancelled.
  std::function<std::optional<std::filesystem::path>(const std::filesystem::path& start)> pick_iso;
  std::function<std::optional<std::filesystem::path>(const std::filesystem::path& start)>
      pick_folder;
  std::function<void(std::function<void()> work)> run_async;  // on the worker
  std::function<void(std::function<void()> fn)> post;         // back to the UI thread
  std::function<gamefiles::ImageCheck(const std::filesystem::path& iso)> check_image;
  std::function<gamefiles::Verdict(const std::filesystem::path& dir)> check_folder;
  std::function<gamefiles::TargetState(const std::filesystem::path& target)> inspect_target;
  std::function<std::optional<uint64_t>(const std::filesystem::path& target)> free_bytes;
  // The game can start from this path (gamefiles::PathFitsCodePage).
  std::function<bool(const std::filesystem::path& target)> path_fits;
  std::function<gamefiles::ExtractResult(
      const std::filesystem::path& iso, const xdvdfs::Volume& volume,
      const std::filesystem::path& target, const std::atomic<bool>& cancel,
      const std::function<void(const gamefiles::Progress&)>& progress)>
      extract;
  // The game files are in this folder (checked, or just extracted and
  // verified). Called once, on the UI thread.
  std::function<void(const std::filesystem::path& folder)> done;
};

class Flow {
 public:
  explicit Flow(Effects effects) : fx_(std::move(effects)) {}

  void ChooseImage();   // file dialog, then check (async)
  void ChooseFolder();  // folder dialog, then check (async)
  // Folder dialog for the extraction target: an empty folder is used as it
  // is; any other gets a new DOA4 folder inside it.
  void ChangeTarget();
  void Extract();       // only when CanExtract()
  void Cancel();        // while extracting
  void Back();          // from the target stage to the start

  Stage stage() const { return stage_; }
  // The last refusal or failure (UTF-8; empty when none). Cleared when the
  // user starts another choice.
  const std::string& message() const { return message_; }
  const std::filesystem::path& image() const { return image_; }
  const std::filesystem::path& target() const { return target_; }
  uint64_t need_bytes() const { return volume_.file_bytes + kSpaceSlack; }
  std::optional<uint64_t> free_bytes() const { return free_; }
  // Why the target cannot be used (empty when it can).
  const std::string& target_note() const { return target_note_; }
  bool CanExtract() const { return stage_ == Stage::kTarget && target_note_.empty(); }
  gamefiles::Progress progress() const;

 private:
  void OnImageChecked(const std::filesystem::path& iso, gamefiles::ImageCheck check);
  void OnFolderChecked(const std::filesystem::path& dir, gamefiles::Verdict verdict);
  void OnExtracted(gamefiles::ExtractResult result);
  void InspectTarget();

  Effects fx_;
  Stage stage_ = Stage::kChoose;
  std::string message_;
  std::filesystem::path image_, target_, last_dir_;
  xdvdfs::Volume volume_;
  std::optional<uint64_t> free_;
  std::string target_note_;
  std::atomic<bool> cancel_{false};
  mutable std::mutex progress_mutex_;
  gamefiles::Progress progress_;
};

// The DOA4_SETUP_TEST switch (see doa4_setup_dialog.h): "folder=<path>" or
// "iso=<path>|target=<path>" (ASCII paths). Unset or malformed: nothing set.
struct TestScript {
  std::optional<std::filesystem::path> folder, iso, target;
  bool active() const { return folder.has_value() || (iso.has_value() && target.has_value()); }
};
TestScript ParseTestScript(const char* value);

// "7.5 GB" (GiB, one decimal).
std::string FormatGB(uint64_t bytes);

// What the screen says for a refused image (`image` true) or folder.
std::string VerdictMessage(gamefiles::Verdict verdict, bool image);

}  // namespace doa4::setup

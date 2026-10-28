// The game files: which folder the game starts from, checking a disc image or
// folder against the supported version, and extracting a disc image. No SDK;
// host-tested. See notes/specs/2026-10-28-first-run-setup-design.md.

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "doa4_xdvdfs.h"

namespace doa4::gamefiles {

// The only game the recompiled code matches: Dead or Alive 4 (USA, Europe)
// (Rev 1), title 544307D1.
inline constexpr std::string_view kSupportedXexSha1 = "af8011945037b37c98fb8a78d5a4e844dd56c33f";
inline constexpr std::string_view kSupportedDisc = "Dead or Alive 4 (USA, Europe) (Rev 1)";
inline constexpr std::string_view kXexName = "default.xex";

// SHA-1 (Windows CNG). HexDigest is lowercase hex and ends the hash.
class Sha1 {
 public:
  Sha1();
  ~Sha1();
  Sha1(const Sha1&) = delete;
  Sha1& operator=(const Sha1&) = delete;
  void Update(const void* data, size_t size);
  std::string HexDigest();

 private:
  struct State;
  std::unique_ptr<State> state_;
};

std::string Sha1Hex(const void* data, size_t size);

// `dir`/default.xex exists and is a regular file.
bool HasXex(const std::filesystem::path& dir);

// Where the game starts from. `path` is empty when the setup screen must open;
// `reason` then says why (empty on a first run).
struct StartChoice {
  std::filesystem::path path;
  bool from_command_line = false;
  std::string reason;  // UTF-8
};

// The command line's --game_data_root first (used when it holds default.xex;
// otherwise the setup screen, naming it), then the saved doa4_game_files (the
// same), then the setup screen. With `fits` (PathFitsCodePage in the game), a
// saved folder it rejects opens the setup screen with that reason.
StartChoice ChooseStartPath(const std::filesystem::path& command_line,
                            const std::filesystem::path& saved,
                            const std::function<bool(const std::filesystem::path&)>& has_xex,
                            const std::function<bool(const std::filesystem::path&)>& fits = {});

// The folder for saves and the shader cache. `sdk_value` is what the SDK
// resolved (the command line's --user_data_root when `from_command_line`,
// else its default); `saved` is doa4_user_data. Order: command line, saved
// (when usable), default.
enum class UserDataSource { kCommandLine, kSaved, kDefault };
struct UserDataChoice {
  std::filesystem::path path;
  UserDataSource source = UserDataSource::kDefault;
  std::string note;  // UTF-8; why a saved folder was not used, for the log
};
UserDataChoice ChooseUserData(const std::filesystem::path& sdk_value, bool from_command_line,
                              const std::filesystem::path& saved);

// The path converts to the Windows ANSI code page without loss. The SDK's
// runtime turns the game folder into ANSI text while it starts and throws
// (ending the game) on a name it cannot convert, so such folders are refused.
bool PathFitsCodePage(const std::filesystem::path& path);

// What the Esc menu's Game tab shows: the folder in use, a note (UTF-8; empty
// for none) and whether Change is offered. `saved` is doa4_game_files now
// (possibly changed since start-up).
struct GameTab {
  std::filesystem::path now;
  std::string note;
  bool can_change = true;
};
GameTab DescribeGameTab(const std::filesystem::path& now, bool from_command_line,
                        const std::filesystem::path& saved);

enum class Verdict {
  kOk,
  kNotXbox,       // not an Xbox disc image
  kDamaged,       // an Xbox image, but damaged or incomplete
  kNoXex,         // no default.xex in it
  kWrongVersion,  // default.xex is not the supported one
  kUnreadable,    // the file or folder could not be read
  kIncomplete,    // a folder still holding a .part file (an interrupted extraction)
  kPathUnsupported,  // a folder whose path PathFitsCodePage rejects
};

struct ImageCheck {
  Verdict verdict = Verdict::kUnreadable;
  xdvdfs::Volume volume;  // filled when the directory was read
};

// Reads the image's directory and hashes its default.xex.
ImageCheck CheckImage(const std::filesystem::path& iso,
                      std::string_view expected_sha1 = kSupportedXexSha1);

// Hashes `dir`/default.xex.
Verdict CheckFolder(const std::filesystem::path& dir,
                    std::string_view expected_sha1 = kSupportedXexSha1);

enum class TargetState { kNew, kEmpty, kNotEmpty, kNotAFolder };
TargetState InspectTarget(const std::filesystem::path& target);

struct Progress {
  uint64_t done = 0;   // file bytes written so far
  uint64_t total = 0;  // Volume::file_bytes
  std::string file;    // the file being written ('/'-separated)
};

enum class ExtractOutcome { kDone, kCancelled, kFailed };

struct ExtractResult {
  ExtractOutcome outcome = ExtractOutcome::kFailed;
  std::string error;  // UTF-8, set when kFailed
};

// Writes every folder and file of `volume` under `target`, which must not
// exist or be an empty folder (anything else fails before writing). Each file
// is written as <name>.part and renamed when complete. Afterwards
// `target`/default.xex must hash to `expected_sha1`. On failure or cancel,
// every file and folder this call created is removed (folders only when
// empty); nothing else is touched. `progress` (may be null) is called after
// each chunk, on the calling thread; `cancel` is checked between chunks.
ExtractResult Extract(xdvdfs::Source& image, const xdvdfs::Volume& volume,
                      const std::filesystem::path& target, std::string_view expected_sha1,
                      const std::atomic<bool>& cancel,
                      const std::function<void(const Progress&)>& progress);

}  // namespace doa4::gamefiles

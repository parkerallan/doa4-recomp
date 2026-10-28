// One-time backup of the game's save folder before the Extras tab first
// writes unlocks into the profile.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace doa4 {

// ups.dat is always this size; a shorter file is one the game is writing.
inline constexpr std::uintmax_t kSaveFileSize = 81920;

struct BackupResult {
  bool ok = false;               // false: do not write anything
  std::filesystem::path backup;  // the backup folder, or empty when there was no save
  std::string message;           // why not ok, or a note ("no save existed yet")
};

// Makes sure a backup of `save_dir` (the save package folder, e.g.
// .../00000001/ups.dat) exists in `backup_root`, which must be outside the
// folder the game lists its saves from (the game would see a second save).
// A backup is a folder in `backup_root` named "<save folder name>.backup-<anything>"
// holding a complete ups.dat; the first one is kept forever and never
// overwritten. Otherwise copies the folder to
// "<backup_root>/<save folder name>.backup-<stamp>", only from a complete
// ups.dat, and checks the copy. When `save_dir` does not exist there is
// nothing to lose: ok, with no backup and a note.
BackupResult EnsureSaveBackup(const std::filesystem::path& save_dir,
                              const std::filesystem::path& backup_root, std::string_view stamp);

}  // namespace doa4

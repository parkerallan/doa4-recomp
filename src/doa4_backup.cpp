// One-time save backup. See doa4_backup.h.

#include "doa4_backup.h"

#include <system_error>

namespace doa4 {

namespace {

// Whether `dir`/ups.dat is a complete save file.
bool HoldsCompleteSave(const std::filesystem::path& dir) {
  std::error_code ec;
  const std::uintmax_t size = std::filesystem::file_size(dir / "ups.dat", ec);
  return !ec && size == kSaveFileSize;
}

}  // namespace

BackupResult EnsureSaveBackup(const std::filesystem::path& save_dir,
                              const std::filesystem::path& backup_root, std::string_view stamp) {
  namespace fs = std::filesystem;
  BackupResult result;
  std::error_code ec;
  if (!fs::is_directory(save_dir, ec)) {
    result.ok = true;
    result.message = "No save existed yet, so there was nothing to back up.";
    return result;
  }
  const std::string prefix = save_dir.filename().string() + ".backup-";
  if (fs::is_directory(backup_root, ec)) {
    for (const auto& entry : fs::directory_iterator(backup_root, ec)) {
      if (entry.is_directory(ec) && entry.path().filename().string().rfind(prefix, 0) == 0 &&
          HoldsCompleteSave(entry.path())) {
        result.ok = true;
        result.backup = entry.path();
        return result;
      }
    }
    if (ec) {
      result.message = "cannot list " + backup_root.string() + ": " + ec.message();
      return result;
    }
  }
  if (!HoldsCompleteSave(save_dir)) {
    result.message =
        "the save file is incomplete (the game may be saving right now); try again in a moment";
    return result;
  }
  const fs::path backup = backup_root / (prefix + std::string(stamp));
  if (fs::exists(backup, ec)) {
    result.message = "cannot back up the save: " + backup.string() + " is in the way";
    return result;
  }
  fs::create_directories(backup_root, ec);
  if (!ec) fs::copy(save_dir, backup, fs::copy_options::recursive, ec);
  if (ec || !HoldsCompleteSave(backup)) {
    result.message = "cannot back up the save to " + backup.string() +
                     (ec ? ": " + ec.message() : ": the copy is incomplete");
    std::error_code ignored;
    fs::remove_all(backup, ignored);
    return result;
  }
  result.ok = true;
  result.backup = backup;
  return result;
}

}  // namespace doa4

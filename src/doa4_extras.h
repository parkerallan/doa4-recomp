// The Extras tab's actions, apart from drawing: unlock with a backup first,
// and notice when the game has saved the result.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "doa4_backup.h"
#include "doa4_guest.h"
#include "doa4_unlocks.h"

namespace doa4 {

// What the Extras tab shows. Owned by the app, so it survives the menu being
// closed (the user closes it to make the game save) and reopened.
struct ExtrasState {
  std::string last_change;       // "Unlocked 6 characters, ..." or ""
  std::filesystem::path backup;  // the backup folder, once one exists
  std::string backup_note;       // e.g. "No save existed yet ..."
  bool waiting_for_save = false;
  bool saved = false;
  // What "saved" must find in ups.dat: the profile, and a bit (1 << Unlock)
  // per kind unlocked since the last confirmed save.
  uint32_t profile = 0;
  uint32_t pending_unlocks = 0;
  // ups.dat's modified time at the last click that changed something;
  // nullopt when ups.dat did not exist then.
  std::optional<std::filesystem::file_time_type> save_time_at_click;
};

// <user_data_root>/<XUID as %016X>/544307D1/00000001/ups.dat: the save
// package folder (ContentManager::ResolvePackagePath). Empty when xuid is 0.
std::filesystem::path SaveDir(const std::filesystem::path& user_data_root, uint64_t xuid);

// Local time as YYYY-MM-DD-HHMMSS, for backup folder names.
std::string BackupStamp();

// "Unlocked 6 characters, 61 costumes, 23 system voices, 3 stages." or
// "Nothing to unlock: it was all unlocked already."
std::string DescribeUnlock(const unlocks::UnlockResult& result);

// Backs the save up into `backup_root` (EnsureSaveBackup), then applies the
// unlock. Returns false with *error set, having written nothing, when the
// backup could not be made or `save_dir` is empty.
bool UnlockWithBackup(const GuestBytes& guest, uint32_t profile, unlocks::Unlock what,
                      const std::filesystem::path& save_dir,
                      const std::filesystem::path& backup_root, std::string_view stamp,
                      ExtrasState* state, std::string* error);

// While waiting, marks the state saved once ups.dat has changed since the
// click AND holds every pending unlock (read through the game's tables in
// `guest`). A newer file without them - a save that copied the record before
// the click, or a reload - keeps it waiting.
void PollSaved(const GuestBytes& guest, const std::filesystem::path& save_dir,
               ExtrasState* state);

// The warning shown beside "Restart now" while unlocks are not saved yet
// (a restart loses them), or nullptr.
const char* UnsavedUnlocksWarning(const ExtrasState& state);

}  // namespace doa4

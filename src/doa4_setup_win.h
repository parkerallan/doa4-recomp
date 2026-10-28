// The first-run setup's Windows parts: the disc image file dialog and the
// free space on the drive a folder will be created on. See
// notes/specs/2026-10-28-first-run-setup-design.md.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace doa4::setup {

// Windows file dialog for a disc image (*.iso first, any file allowed),
// starting in `start` when it is a folder. nullopt when cancelled or on error.
std::optional<std::filesystem::path> PickIsoFile(void* owner_hwnd,
                                                 const std::filesystem::path& start);

// Free bytes for the current user on the drive of `target`'s nearest
// existing folder (the target itself usually does not exist yet). nullopt
// when no folder on the way exists or the drive cannot be queried.
std::optional<uint64_t> FreeBytes(const std::filesystem::path& target);

}  // namespace doa4::setup

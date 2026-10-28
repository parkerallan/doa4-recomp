// Full-quality photos: the Windows parts. Image files through the Windows
// Imaging Component (WIC), the default Pictures folder, the folder dialog,
// Explorer, and the monitor size.

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "doa4_photo.h"

namespace doa4::photo {

enum class FileType { kPng, kJpg };

// Writes `image` to `path` (PNG lossless, JPG at quality 0.95) through a
// "<path>.tmp" file renamed into place, creating the folder if needed.
// Returns "" or what went wrong; on failure no file is left at `path`.
std::string WriteImage(const Image& image, const std::filesystem::path& path, FileType type);

// Reads a PNG or JPG back (for tests and checks). Returns false on failure.
bool ReadImage(const std::filesystem::path& path, Image* image);

// <the user's Pictures folder>\DOA4, or empty if Windows does not say.
std::filesystem::path DefaultPhotoDir();

// The Windows folder dialog, owned by `owner_hwnd` (may be null), titled
// `title`. Must be called on the UI thread. nullopt when cancelled or on
// failure.
std::optional<std::filesystem::path> PickFolder(void* owner_hwnd,
                                                const std::filesystem::path& start,
                                                const wchar_t* title = L"Folder for DOA4 photos");

// Opens `dir` in Explorer.
void OpenInExplorer(const std::filesystem::path& dir);

// A path as UTF-8 text, for logs and messages. (path::string() converts to
// the ANSI code page and throws for characters it cannot hold.)
std::string PathText(const std::filesystem::path& path);

// The size of the monitor showing `hwnd` (the primary monitor when null).
Size MonitorSize(void* hwnd);

}  // namespace doa4::photo

// A path as UTF-8 text, for logs, messages and the menu.
//
// std::filesystem::path::string() converts to the Windows ANSI code page and
// throws when a character has no mapping (a Japanese or Chinese folder name on
// a Western Windows, for example); a throw on a worker thread ends the game.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace doa4 {

inline std::string Utf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

// Utf8 with forward slashes (the generic form; Windows accepts both).
inline std::string Utf8Generic(const std::filesystem::path& path) {
  const std::u8string text = path.generic_u8string();
  return std::string(text.begin(), text.end());
}

// The reverse: UTF-8 text (a TOML value, for example) as a path. Converting a
// std::string straight to a path would read it in the ANSI code page.
inline std::filesystem::path PathFromUtf8(std::string_view text) {
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

}  // namespace doa4

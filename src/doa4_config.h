// Writes single settings into dead_or_alive_4.toml (the config the SDK loads
// at start-up) without touching the rest of the file. rex::cvar::SaveConfig is
// not used: it writes every non-default cvar, including the launcher's
// --game_data_root / --user_data_root.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace doa4 {

// `text` with the top-level line for `key` set to `key = value_literal`.
// A top-level line is one before the first [table] header; its key is the
// text before its first '=', trimmed. Other lines are kept byte for byte.
// A missing key is inserted before the first table header, or appended (a
// missing final newline is added first). New lines use "\r\n" if `text`
// contains one, else "\n".
std::string UpsertTomlKey(std::string_view text, std::string_view key,
                          std::string_view value_literal);

// A TOML basic string: "..." with \ and " escaped.
std::string TomlString(std::string_view value);

// UpsertTomlKey on the file at `path` (created if missing), written to
// `path` + ".tmp" and renamed over `path`. Returns "" on success, else a
// description of the failure.
std::string SaveTomlKey(const std::filesystem::path& path, std::string_view key,
                        std::string_view value_literal);

}  // namespace doa4

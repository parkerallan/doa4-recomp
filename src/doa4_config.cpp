#include "doa4_config.h"

#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>

#include <toml++/toml.hpp>

namespace doa4 {

namespace {

std::string_view Trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

// The SDK loads the config with toml++ and drops the WHOLE file on a parse
// error (rex::cvar::LoadConfig), so `updated` must parse and must hold exactly
// `key = value_literal`. Returns "" when it does, else why not.
std::string CheckLoadable(const std::string& updated, std::string_view key,
                          std::string_view value_literal) {
  try {
    toml::table expected = toml::parse(std::string(key) + " = " + std::string(value_literal));
    toml::table table = toml::parse(updated);
    if (table[key] != expected[key]) {
      return "the file has a form the menu cannot edit (" + std::string(key) + ")";
    }
  } catch (const toml::parse_error& e) {
    return "the file would not load (" + std::string(e.description()) + ")";
  }
  return {};
}

}  // namespace

std::string UpsertTomlKey(std::string_view text, std::string_view key,
                          std::string_view value_literal) {
  const std::string_view nl = text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
  const std::string new_line = std::string(key) + " = " + std::string(value_literal);

  std::string out;
  out.reserve(text.size() + new_line.size() + 2);
  // Set once the key is written. The first table header writes a missing key,
  // so nothing inside a table is ever matched.
  bool done = false;
  size_t pos = 0;
  if (text.substr(0, kUtf8Bom.size()) == kUtf8Bom) {
    out += kUtf8Bom;  // kept, and not part of the first line's key
    pos = kUtf8Bom.size();
  }
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    size_t next = end == std::string_view::npos ? text.size() : end + 1;
    std::string_view line = text.substr(pos, next - pos);  // with its line ending
    std::string_view content = Trim(line);
    if (!done && !content.empty() && content.front() == '[') {
      out += new_line;
      out += nl;
      done = true;
    }
    size_t eq = content.find('=');
    if (!done && !content.empty() && content.front() != '#' && eq != std::string_view::npos &&
        Trim(content.substr(0, eq)) == key) {
      out += new_line;
      // Keep this line's own ending ("\r\n", "\n" or none at end of file).
      size_t body = line.find_last_not_of("\r\n");
      out += line.substr(body == std::string_view::npos ? 0 : body + 1);
      done = true;
    } else {
      out += line;
    }
    pos = next;
  }
  if (!done) {
    if (!out.empty() && out.back() != '\n') out += nl;
    out += new_line;
    out += nl;
  }
  return out;
}

std::string TomlString(std::string_view value) {
  std::string out = "\"";
  for (char c : value) {
    if (c == '\\' || c == '"') out += '\\';
    out += c;
  }
  out += '"';
  return out;
}

std::string SaveTomlKey(const std::filesystem::path& path, std::string_view key,
                        std::string_view value_literal) {
  std::string text;
  {
    std::ifstream in(path, std::ios::binary);
    if (in) {
      text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    } else if (std::filesystem::exists(path)) {
      return "cannot read " + path.string();
    }
  }
  std::string updated = UpsertTomlKey(text, key, value_literal);
  if (std::string error = CheckLoadable(updated, key, value_literal); !error.empty()) {
    return error + "; dead_or_alive_4.toml was left unchanged";
  }
  std::filesystem::path tmp = path;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return "cannot write " + tmp.string();
    out << updated;
    out.flush();
    if (!out) return "cannot write " + tmp.string();
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::string error = "cannot replace " + path.string() + ": " + ec.message();
    std::filesystem::remove(tmp, ec);
    return error;
  }
  return {};
}

}  // namespace doa4

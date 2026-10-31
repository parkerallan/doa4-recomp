// Host test for src/doa4_config.cpp (UpsertTomlKey, TomlString, SaveTomlKey).
// Built and run by run_toml_upsert_test.bat.

#include "../../src/doa4_config.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using doa4::TomlString;
using doa4::UpsertTomlKey;

static void TestUpsert() {
  // Empty file.
  CHECK(UpsertTomlKey("", "fullscreen", "true") == "fullscreen = true\n");
  // Replace in place, every other line byte for byte.
  CHECK(UpsertTomlKey("a = 1\nfullscreen = false\nb = 2\n", "fullscreen", "true") ==
        "a = 1\nfullscreen = true\nb = 2\n");
  // Whole-key match only: resolution_scale_x is not resolution_scale.
  CHECK(UpsertTomlKey("resolution_scale_x = 1\n", "resolution_scale", "2") ==
        "resolution_scale_x = 1\nresolution_scale = 2\n");
  // Comments are not keys.
  CHECK(UpsertTomlKey("# fullscreen = false\n", "fullscreen", "true") ==
        "# fullscreen = false\nfullscreen = true\n");
  // A missing key goes before the first table; keys inside tables are not top-level.
  CHECK(UpsertTomlKey("a = 1\n[GPU]\nfullscreen = false\n", "fullscreen", "true") ==
        "a = 1\nfullscreen = true\n[GPU]\nfullscreen = false\n");
  // CRLF files get CRLF lines; a replaced line keeps its own ending.
  CHECK(UpsertTomlKey("a = 1\r\nb = 2\r\n", "c", "3") == "a = 1\r\nb = 2\r\nc = 3\r\n");
  CHECK(UpsertTomlKey("a = 1\r\nb = 2\r\n", "a", "9") == "a = 9\r\nb = 2\r\n");
  // No final newline.
  CHECK(UpsertTomlKey("a = 1", "c", "3") == "a = 1\nc = 3\n");
  CHECK(UpsertTomlKey("a = 1\nc = 2", "c", "3") == "a = 1\nc = 3");
  // Spacing around the key does not matter; the line is rewritten canonically.
  CHECK(UpsertTomlKey("  fullscreen=false  \n", "fullscreen", "true") == "fullscreen = true\n");
  // Only the first match is replaced (a duplicate key is left for the SDK's
  // parser to reject, as it would have before).
  CHECK(UpsertTomlKey("a = 1\na = 2\n", "a", "3") == "a = 3\na = 2\n");
}

static void TestTomlString() {
  CHECK(TomlString("Space") == "\"Space\"");
  CHECK(TomlString("A=B,B=A") == "\"A=B,B=A\"");
  CHECK(TomlString("a\"b\\c") == "\"a\\\"b\\\\c\"");
  CHECK(TomlString("") == "\"\"");
}

static std::string ReadAll(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static void TestSave() {
  auto dir = std::filesystem::temp_directory_path() / "doa4_toml_upsert_test_data";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  auto path = dir / "dead_or_alive_4.toml";
  CHECK(doa4::SaveTomlKey(path, "resolution_scale", "2").empty());
  CHECK(doa4::SaveTomlKey(path, "keybind_a", TomlString("Space")).empty());
  CHECK(doa4::SaveTomlKey(path, "resolution_scale", "3").empty());
  CHECK(ReadAll(path) == "resolution_scale = 3\nkeybind_a = \"Space\"\n");
  auto tmp = path;
  tmp += ".tmp";
  CHECK(!std::filesystem::exists(tmp));
  // A directory in the file's place cannot be written: an error, not a crash.
  auto blocked = dir / "blocked.toml";
  std::filesystem::create_directories(blocked);
  CHECK(!doa4::SaveTomlKey(blocked, "a", "1").empty());
  std::filesystem::remove_all(dir);
}

// The SDK loads the file with toml++ and drops ALL of it on a parse error, so a
// save must never leave a file that toml++ rejects or reads differently.
static std::string SaveInto(const std::filesystem::path& path, const std::string& before,
                            const char* key, const char* value) {
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << before;
  }
  return doa4::SaveTomlKey(path, key, value);
}

static void TestSaveKeepsFileLoadable() {
  auto dir = std::filesystem::temp_directory_path() / "doa4_toml_upsert_test_data";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  auto path = dir / "dead_or_alive_4.toml";
  // A UTF-8 BOM (some editors add one) is kept and the key after it is found.
  const std::string bom = "\xEF\xBB\xBF";
  CHECK(SaveInto(path, bom + "fullscreen = false\n", "fullscreen", "true").empty());
  CHECK(ReadAll(path) == bom + "fullscreen = true\n");
  // Shapes the line editor cannot handle are refused and the file is left untouched.
  const std::string quoted = "\"fullscreen\" = false\n";
  CHECK(!SaveInto(path, quoted, "fullscreen", "true").empty());
  CHECK(ReadAll(path) == quoted);
  const std::string array_with_bracket_line = "list = [\n  [1, 2],\n]\n";
  CHECK(!SaveInto(path, array_with_bracket_line, "fullscreen", "true").empty());
  CHECK(ReadAll(path) == array_with_bracket_line);
  const std::string string_with_key_line = "note = \"\"\"\nfullscreen = 1\n\"\"\"\nfullscreen = false\n";
  CHECK(!SaveInto(path, string_with_key_line, "fullscreen", "true").empty());
  CHECK(ReadAll(path) == string_with_key_line);
  // A file that is already broken is not made worse.
  const std::string broken = "fullscreen = \n";
  CHECK(!SaveInto(path, broken, "resolution_scale", "2").empty());
  CHECK(ReadAll(path) == broken);
  auto tmp = path;
  tmp += ".tmp";
  CHECK(!std::filesystem::exists(tmp));
  std::filesystem::remove_all(dir);
}

int main() {
  TestUpsert();
  TestTomlString();
  try {
    TestSave();
    TestSaveKeepsFileLoadable();
  } catch (const std::exception& e) {
    std::printf("FAIL TestSave threw: %s\n", e.what());
    ++g_failures;
  }
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("toml_upsert: all checks passed\n");
  return 0;
}

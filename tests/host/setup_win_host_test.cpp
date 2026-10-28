// Host test for src/doa4_setup_win.cpp (free space). The file dialog is
// modal and is checked in the game, not here. Built and run by
// run_setup_win_test.bat.

#include "../../src/doa4_setup_win.h"

#include <windows.h>

#include <cstdio>
#include <filesystem>

static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

namespace fs = std::filesystem;

static uint64_t ApiFree(const fs::path& dir) {
  ULARGE_INTEGER free{};
  GetDiskFreeSpaceExW(dir.c_str(), &free, nullptr, nullptr);
  return free.QuadPart;
}

// Free space can change between two reads (other programs write); a small
// window is allowed.
static bool Near(uint64_t a, uint64_t b) {
  const uint64_t d = a > b ? a - b : b - a;
  return d < (64ull << 20);
}

int main() {
  const fs::path temp = fs::temp_directory_path();
  const fs::path dir = temp / "doa4_setup_win_test_data";
  fs::remove_all(dir);
  fs::create_directories(dir);

  // An existing folder.
  auto here = doa4::setup::FreeBytes(dir);
  CHECK(here.has_value() && *here > 0 && Near(*here, ApiFree(dir)));
  // A target that does not exist yet (the usual case): its nearest existing
  // folder's drive.
  auto future = doa4::setup::FreeBytes(dir / "a" / "b" / "DOA4");
  CHECK(future.has_value() && Near(*future, ApiFree(dir)));
  // A non-ASCII name on the way.
  auto unicode = doa4::setup::FreeBytes(dir / fs::path(u8"ゲーム") / "DOA4");
  CHECK(unicode.has_value());
  // A drive letter that is not there.
  const DWORD drives = GetLogicalDrives();
  wchar_t missing = 0;
  for (wchar_t d = L'Z'; d >= L'D'; --d) {
    if (!(drives & (1u << (d - L'A')))) {
      missing = d;
      break;
    }
  }
  CHECK(missing != 0);
  if (missing) {
    const std::wstring root = std::wstring(1, missing) + L":\\DOA4";
    CHECK(!doa4::setup::FreeBytes(root).has_value());
  }
  // An empty path.
  CHECK(!doa4::setup::FreeBytes(fs::path()).has_value());

  fs::remove_all(dir);
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("setup_win: all checks passed\n");
  return 0;
}

// Host test for src/doa4_photo_win.cpp (WIC image files). Built and run by
// run_photo_win_test.bat.

#include "../../src/doa4_photo_win.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using namespace doa4::photo;
namespace fs = std::filesystem;

// A gradient with a distinct colour per pixel, so a swapped channel order,
// a flipped row or a shifted column shows up.
static Image Pattern(uint32_t w, uint32_t h) {
  Image image{w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      uint8_t* p = &image.rgbx[(size_t(y) * w + x) * 4];
      p[0] = uint8_t(x * 255 / (w - 1));  // red grows to the right
      p[1] = uint8_t(y * 255 / (h - 1));  // green grows downwards
      p[2] = 40;
      p[3] = 0;  // X: ignored
    }
  }
  return image;
}

static int MaxDiff(const Image& a, const Image& b) {
  int diff = 0;
  for (size_t i = 0; i < a.rgbx.size(); ++i) {
    if (i % 4 == 3) continue;
    diff = std::max(diff, std::abs(int(a.rgbx[i]) - int(b.rgbx[i])));
  }
  return diff;
}

static void TestRoundTrip(const fs::path& dir) {
  const Image image = Pattern(64, 36);

  // PNG is lossless: every byte comes back.
  const fs::path png = dir / "a.png";
  CHECK(WriteImage(image, png, FileType::kPng).empty());
  CHECK(!fs::exists(dir / "a.png.tmp"));
  Image back;
  CHECK(ReadImage(png, &back));
  CHECK(back.width == 64 && back.height == 36 && back.rgbx.size() == image.rgbx.size());
  if (back.rgbx.size() == image.rgbx.size()) CHECK(MaxDiff(image, back) == 0);
  // Channel order: the top-right pixel is red, not blue.
  if (back.rgbx.size() == image.rgbx.size()) {
    const uint8_t* p = &back.rgbx[(63) * 4];
    CHECK(p[0] == 255 && p[2] == 40);
  }
  {
    std::ifstream in(png, std::ios::binary);
    char magic[4] = {};
    in.read(magic, 4);
    CHECK(magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G');
  }

  // JPG: same size, close colours, and really a JPEG.
  const fs::path jpg = dir / "sub" / "b.jpg";  // the folder does not exist yet
  CHECK(WriteImage(image, jpg, FileType::kJpg).empty());
  Image jback;
  CHECK(ReadImage(jpg, &jback));
  CHECK(jback.width == 64 && jback.height == 36);
  if (jback.rgbx.size() == image.rgbx.size()) CHECK(MaxDiff(image, jback) <= 12);
  {
    std::ifstream in(jpg, std::ios::binary);
    unsigned char magic[2] = {};
    in.read(reinterpret_cast<char*>(magic), 2);
    CHECK(magic[0] == 0xFF && magic[1] == 0xD8);
  }
}

static void TestErrors(const fs::path& dir) {
  const Image image = Pattern(8, 8);
  // A folder that cannot be created (a file is in the way): an error, no file.
  std::ofstream(dir / "blocker") << "x";
  const fs::path bad = dir / "blocker" / "c.png";
  CHECK(!WriteImage(image, bad, FileType::kPng).empty());
  CHECK(!fs::exists(bad));
  // The file encodes but cannot take its name (a folder is in the way): an
  // error, and the temporary file is cleaned up.
  fs::create_directories(dir / "e.png");
  CHECK(!WriteImage(image, dir / "e.png", FileType::kPng).empty());
  CHECK(!fs::exists(dir / "e.png.tmp"));
  // An empty image is refused.
  CHECK(!WriteImage(Image{}, dir / "d.png", FileType::kPng).empty());
  CHECK(!fs::exists(dir / "d.png"));
  // Reading a file that is not an image fails cleanly.
  Image nothing;
  CHECK(!ReadImage(dir / "blocker", &nothing));
}

// A folder name outside the Windows ANSI code page (e.g. a Japanese profile
// or a chosen folder in Chinese or Japanese): saving works, a failure is reported as text, and
// nothing throws (path::string() throws for such names on MSVC's library).
static void TestNonAnsiFolder(const fs::path& dir) {
  const fs::path photos = dir / fs::path(u8"\u5199\u771F");  // two CJK characters
  const Image image = Pattern(8, 8);
  CHECK(WriteImage(image, photos / "a.png", FileType::kPng).empty());
  CHECK(fs::exists(photos / "a.png"));
  std::ofstream(photos / "blocker") << "x";
  std::string error;
  try {
    error = WriteImage(image, photos / "blocker" / "c.png", FileType::kPng);
  } catch (const std::exception& e) {
    std::printf("FAIL WriteImage threw: %s\n", e.what());
    ++g_failures;
  }
  CHECK(!error.empty());
  // PathText gives the UTF-8 bytes, for logs and messages.
  const std::string text = PathText(photos);
  CHECK(text.size() >= 6 && text.substr(text.size() - 6) == "\xE5\x86\x99\xE7\x9C\x9F");
}

static void TestFolders() {
  const fs::path pictures = DefaultPhotoDir();
  CHECK(!pictures.empty());
  CHECK(pictures.filename() == "DOA4");
  const Size monitor = MonitorSize(nullptr);
  CHECK(monitor.width > 0 && monitor.height > 0);
}

int main() {
  const fs::path dir = fs::temp_directory_path() / "doa4_photo_win_test_data";
  fs::remove_all(dir);
  fs::create_directories(dir);
  try {
    TestRoundTrip(dir);
    TestErrors(dir);
    TestNonAnsiFolder(dir);
    TestFolders();
  } catch (const std::exception& e) {
    std::printf("FAIL threw: %s\n", e.what());
    ++g_failures;
  }
  fs::remove_all(dir);
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("photo_win: all checks passed\n");
  return 0;
}

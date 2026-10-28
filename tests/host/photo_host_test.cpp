// Host test for src/doa4_photo.cpp. Built and run by run_photo_test.bat.

#include "../../src/doa4_photo.h"

#include <cstdio>
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

static Image Make(uint32_t w, uint32_t h) {
  Image image{w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
  return image;
}
static void Set(Image& image, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
  uint8_t* p = &image.rgbx[(size_t(y) * image.width + x) * 4];
  p[0] = r; p[1] = g; p[2] = b; p[3] = 0;
}
static const uint8_t* At(const Image& image, uint32_t x, uint32_t y) {
  return &image.rgbx[(size_t(y) * image.width + x) * 4];
}

static void TestCropRect() {
  // The game's own rectangle at its 1280x720 frame (photo_findings.md).
  CHECK(GameCropRect(1280, 720) == (Rect{265, 110, 750, 500}));
  CHECK(GameCropRect(2560, 1440) == (Rect{530, 220, 1500, 1000}));
  CHECK(GameCropRect(5120, 2880) == (Rect{1060, 440, 3000, 2000}));
  // x3: 3840x2160 -> exact thirds as well.
  CHECK(GameCropRect(3840, 2160) == (Rect{795, 330, 2250, 1500}));
}

static void TestCrop() {
  Image image = Make(4, 3);
  for (uint32_t y = 0; y < 3; ++y)
    for (uint32_t x = 0; x < 4; ++x) Set(image, x, y, uint8_t(10 * x), uint8_t(10 * y), 7);
  Image cut = Crop(image, Rect{1, 1, 2, 2});
  CHECK(cut.width == 2 && cut.height == 2 && cut.rgbx.size() == 16);
  CHECK(At(cut, 0, 0)[0] == 10 && At(cut, 0, 0)[1] == 10);
  CHECK(At(cut, 1, 1)[0] == 20 && At(cut, 1, 1)[1] == 20);
}

static void TestFitWithin() {
  CHECK(FitWithin({5120, 2880}, {2560, 1440}) == (Size{2560, 1440}));
  CHECK(FitWithin({3000, 2000}, {2560, 1440}) == (Size{2160, 1440}));  // 3:2 limited by height
  CHECK(FitWithin({2560, 1440}, {2560, 1440}) == (Size{2560, 1440}));
  // Never upscaled: x1 on a 1440p monitor stays 1280x720.
  CHECK(FitWithin({1280, 720}, {2560, 1440}) == (Size{1280, 720}));
  // A narrower box limits by width.
  CHECK(FitWithin({5120, 2880}, {1920, 1200}) == (Size{1920, 1080}));
}

static void TestResize() {
  // 2:1 exactly averages each 2x2 block. Distinct values per pixel, so a
  // nearest-neighbour or a shifted block would give a different answer.
  Image image = Make(4, 2);
  const uint8_t r[2][4] = {{0, 40, 100, 104}, {80, 120, 8, 12}};
  for (uint32_t y = 0; y < 2; ++y)
    for (uint32_t x = 0; x < 4; ++x) Set(image, x, y, r[y][x], uint8_t(255 - r[y][x]), 9);
  Image half = Resize(image, {2, 1});
  CHECK(half.width == 2 && half.height == 1);
  CHECK(At(half, 0, 0)[0] == 60);  // (0 + 40 + 80 + 120) / 4
  CHECK(At(half, 1, 0)[0] == 56);  // (100 + 104 + 8 + 12) / 4
  CHECK(At(half, 0, 0)[1] == 195);
  CHECK(At(half, 1, 0)[2] == 9);
  CHECK(At(half, 0, 0)[3] == 255);  // X written as opaque
  // Same size: unchanged.
  Image same = Resize(image, {4, 2});
  CHECK(At(same, 3, 1)[0] == 12 && At(same, 0, 1)[0] == 80);
  // A non-integer ratio keeps the mean colour of a flat image and the overall
  // mean of a gradient (3 -> 2: the middle pixel is split between outputs).
  Image row = Make(3, 1);
  Set(row, 0, 0, 30, 0, 0);
  Set(row, 1, 0, 60, 0, 0);
  Set(row, 2, 0, 90, 0, 0);
  Image two = Resize(row, {2, 1});
  CHECK(At(two, 0, 0)[0] == 40);  // (30 + 60/2) / 1.5
  CHECK(At(two, 1, 0)[0] == 80);  // (60/2 + 90) / 1.5
  // Halves round up: (10 + 11) / 2 = 10.5 -> 11 (a truncating resize gives 10).
  Image pair = Make(2, 1);
  Set(pair, 0, 0, 10, 0, 0);
  Set(pair, 1, 0, 11, 0, 0);
  CHECK(At(Resize(pair, {1, 1}), 0, 0)[0] == 11);
}

static void TestFileNames() {
  std::tm t{};
  t.tm_year = 2026 - 1900; t.tm_mon = 9; t.tm_mday = 28;
  t.tm_hour = 14; t.tm_min = 3; t.tm_sec = 2;
  CHECK(PhotoFileName(t, "", "png") == "DOA4_2026-10-28_14-03-02.png");
  CHECK(PhotoFileName(t, "photo", "jpg") == "DOA4_2026-10-28_14-03-02_photo.jpg");

  const fs::path dir = fs::temp_directory_path() / "doa4_photo_test_data";
  fs::remove_all(dir);
  fs::create_directories(dir);
  CHECK(UniquePath(dir, "a.png") == dir / "a.png");
  std::ofstream(dir / "a.png") << "x";
  CHECK(UniquePath(dir, "a.png") == dir / "a_2.png");
  std::ofstream(dir / "a_2.png") << "x";
  CHECK(UniquePath(dir, "a.png") == dir / "a_3.png");
  fs::remove_all(dir);
}

static void TestChoices() {
  CHECK(ChoiceIndex(kFormats, "png_monitor") == 0);
  CHECK(ChoiceIndex(kFormats, "jpg_full") == 2);
  CHECK(ChoiceIndex(kFormats, "bmp") == 0);
}

static void TestSnapshotEdge() {
  SnapshotEdge edge;
  // Before any photo the state is 0; a photo: 1 for its 30 frames, then 2.
  const uint32_t first[] = {0, 0, 1, 1, 1, 2, 2, 2};
  int edges = 0, at = -1;
  for (int i = 0; i < 8; ++i) {
    if (edge.OnSwap(first[i])) { ++edges; at = i; }
  }
  CHECK(edges == 1 && at == 5);
  // The state stays 2 between photos; a second photo gives a second edge.
  CHECK(!edge.OnSwap(2));
  CHECK(!edge.OnSwap(1));
  CHECK(edge.OnSwap(2));
  // A state that is already 2 at the first swap seen is not an edge.
  SnapshotEdge late;
  CHECK(!late.OnSwap(2));
  CHECK(!late.OnSwap(0) && !late.OnSwap(2));
}

int main() {
  TestCropRect();
  TestCrop();
  TestFitWithin();
  TestResize();
  try {
    TestFileNames();
  } catch (const std::exception& e) {
    std::printf("FAIL TestFileNames threw: %s\n", e.what());
    ++g_failures;
  }
  TestChoices();
  TestSnapshotEdge();
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("photo: all checks passed\n");
  return 0;
}

// Host test for src/doa4_xdvdfs.cpp (reading Xbox disc images). Built and run
// by run_xdvdfs_test.bat.

#include "../../src/doa4_xdvdfs.h"
#include "xdvdfs_fixtures.h"

#include <windows.h>
#include <winioctl.h>

#include <cstdio>
#include <cstring>
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

using namespace doa4::xdvdfs;
using fixtures::BuildImage;
using fixtures::FakeFile;
namespace fs = std::filesystem;

// An image held in memory from `base` on; the bytes before `base` read as
// zeros, so a volume at 0xFD90000 needs no 265 MB buffer.
class SparseSource : public Source {
 public:
  explicit SparseSource(fixtures::BuiltImage image) : image_(std::move(image)) {}
  uint64_t size() const override { return image_.base + image_.bytes.size(); }
  bool Read(uint64_t offset, void* dst, size_t n) override {
    if (offset + n > size()) return false;
    auto* out = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < n; ++i) {
      const uint64_t at = offset + i;
      out[i] = at < image_.base ? 0 : image_.bytes[size_t(at - image_.base)];
    }
    return true;
  }
  fixtures::BuiltImage image_;
};

static std::string Bytes(size_t n, char seed) {
  std::string s(n, '\0');
  for (size_t i = 0; i < n; ++i) s[i] = char(seed + i * 7);
  return s;
}

static const std::vector<FakeFile> kFiles = {
    {"default.xex", Bytes(5000, 1)},  // crosses two sector boundaries
    {"anm.afs", Bytes(10, 2)},
    {"movie/ayane.sfd", Bytes(3000, 3)},
    {"SOUND/BGM/title.xma", Bytes(2048, 4)},  // exactly one sector
    {"SOUND/se.xma", ""},                     // an empty file
};

static std::string ReadEntry(Source& src, const Entry& e) {
  std::string data(size_t(e.size), '\0');
  if (e.size && !src.Read(e.offset, data.data(), data.size())) return "<read failed>";
  return data;
}

static void TestReadsEveryFile() {
  SparseSource src(BuildImage(kFiles));
  Error error = Error::kNone;
  auto volume = ReadVolume(src, &error);
  CHECK(volume.has_value() && error == Error::kNone);
  if (!volume) return;
  CHECK(volume->base == 0);
  CHECK(volume->file_count == 5);
  CHECK(volume->file_bytes == 5000 + 10 + 3000 + 2048);
  int folders = 0;
  for (const auto& e : volume->entries) folders += e.folder;
  CHECK(folders == 3);  // movie, SOUND, SOUND/BGM
  for (const auto& f : kFiles) {
    const Entry* e = FindFile(*volume, f.path);
    CHECK(e != nullptr);
    if (e) CHECK(!e->folder && ReadEntry(src, *e) == f.data);
  }
  // A folder comes before everything inside it (extraction relies on it).
  size_t sound = 0, bgm = 0, title = 0;
  for (size_t i = 0; i < volume->entries.size(); ++i) {
    if (volume->entries[i].path == "SOUND") sound = i;
    if (volume->entries[i].path == "SOUND/BGM") bgm = i;
    if (volume->entries[i].path == "SOUND/BGM/title.xma") title = i;
  }
  CHECK(sound < bgm && bgm < title);
  // Lookup ignores ASCII case, like the game's file system.
  CHECK(FindFile(*volume, "DEFAULT.XEX") != nullptr);
  CHECK(FindFile(*volume, "sound/bgm/TITLE.xma") != nullptr);
  CHECK(FindFile(*volume, "movie") == nullptr);  // a folder is not a file
  CHECK(FindFile(*volume, "missing.bin") == nullptr);
}

// Every disc layout the reader knows: plain, XGD2 (DOA4), XGD3, XGD1.
static void TestFindsEachVolumeOffset() {
  for (const uint64_t base : kVolumeOffsets) {
    SparseSource src(BuildImage(kFiles, base));
    Error error = Error::kNone;
    auto volume = ReadVolume(src, &error);
    CHECK(volume.has_value());
    if (!volume) continue;
    CHECK(volume->base == base);
    const Entry* e = FindFile(*volume, "movie/ayane.sfd");
    CHECK(e && e->offset >= base && ReadEntry(src, *e) == kFiles[2].data);
  }
}

static void TestNotAnXboxImage() {
  fixtures::BuiltImage zeros{0, std::vector<uint8_t>(40 * 2048, 0)};
  SparseSource src(zeros);
  Error error = Error::kNone;
  CHECK(!ReadVolume(src, &error).has_value());
  CHECK(error == Error::kNotXbox);
  // Too short to hold a volume descriptor at all.
  SparseSource tiny(fixtures::BuiltImage{0, std::vector<uint8_t>(100, 0)});
  CHECK(!ReadVolume(tiny, &error).has_value() && error == Error::kNotXbox);
}

// A file whose data runs past the end of the image (directory tables intact).
static void TestFileBeyondTheEnd() {
  auto image = BuildImage(kFiles);
  uint32_t root_sector = 0;
  std::memcpy(&root_sector, &image.bytes[32 * 2048 + 20], 4);
  const size_t t = size_t(root_sector) * 2048;  // first root entry: default.xex
  uint32_t start = 0;
  std::memcpy(&start, &image.bytes[t + 4], 4);
  const uint32_t too_long = uint32_t(image.bytes.size() - size_t(start) * 2048 + 1);
  std::memcpy(&image.bytes[t + 8], &too_long, 4);
  SparseSource src(image);
  Error error = Error::kNone;
  CHECK(!ReadVolume(src, &error).has_value());
  CHECK(error == Error::kDamaged);
  // Control: one byte shorter ends exactly at the end of the image and reads.
  const uint32_t fits = too_long - 1;
  std::memcpy(&image.bytes[t + 8], &fits, 4);
  SparseSource ok(image);
  CHECK(ReadVolume(ok, &error).has_value());
}

// The image cut short: the last bytes (the root directory table, which the
// fixture writes last) are missing.
static void TestTruncatedImage() {
  auto image = BuildImage(kFiles);
  // Cut inside the last file's data: its entry points past the end.
  Entry last;  // the file whose data ends furthest into the image
  {
    SparseSource whole(image);
    Error e = Error::kNone;
    auto v = ReadVolume(whole, &e);
    CHECK(v.has_value());
    if (!v) return;
    for (const auto& entry : v->entries) {
      if (!entry.folder && entry.offset + entry.size > last.offset + last.size) last = entry;
    }
  }
  CHECK(last.size > 0);
  image.bytes.resize(size_t(last.offset + last.size - 1));
  SparseSource src(image);
  Error error = Error::kNone;
  CHECK(!ReadVolume(src, &error).has_value());
  CHECK(error == Error::kDamaged);
}

// The root table, found through the descriptor, patched by `edit`.
template <typename Edit>
static Error ReadPatched(Edit edit) {
  auto image = BuildImage(kFiles);
  uint32_t root_sector = 0;
  std::memcpy(&root_sector, &image.bytes[32 * 2048 + 20], 4);
  edit(image.bytes, size_t(root_sector) * 2048);
  SparseSource src(image);
  Error error = Error::kNone;
  auto v = ReadVolume(src, &error);
  return v ? Error::kNone : error;
}

static void TestUnsafeNamesAreRefused() {
  // The first root entry is "default.xex": name at +14, 11 bytes.
  CHECK(ReadPatched([](std::vector<uint8_t>& b, size_t t) {
          b[t + 13] = 2;
          std::memcpy(&b[t + 14], "..", 2);
        }) == Error::kDamaged);
  CHECK(ReadPatched([](std::vector<uint8_t>& b, size_t t) { b[t + 14 + 3] = '\\'; }) ==
        Error::kDamaged);
  CHECK(ReadPatched([](std::vector<uint8_t>& b, size_t t) { b[t + 14 + 3] = '/'; }) ==
        Error::kDamaged);
  CHECK(ReadPatched([](std::vector<uint8_t>& b, size_t t) { b[t + 14 + 1] = ':'; }) ==
        Error::kDamaged);
  CHECK(ReadPatched([](std::vector<uint8_t>& b, size_t t) { b[t + 14] = 0x01; }) ==
        Error::kDamaged);
  CHECK(ReadPatched([](std::vector<uint8_t>& b, size_t t) { b[t + 13] = 0; }) ==
        Error::kDamaged);
  // Names Windows cannot create: a trailing dot or space, device names.
  CHECK(ReadPatched([](std::vector<uint8_t>& b, size_t t) { b[t + 14 + 10] = '.'; }) ==
        Error::kDamaged);  // "default.xe."
  CHECK(ReadPatched([](std::vector<uint8_t>& b, size_t t) { b[t + 14 + 10] = ' '; }) ==
        Error::kDamaged);
  CHECK(ReadPatched([](std::vector<uint8_t>& b, size_t t) {
          b[t + 13] = 7;
          std::memcpy(&b[t + 14], "nul.xex", 7);
        }) == Error::kDamaged);
  CHECK(ReadPatched([](std::vector<uint8_t>& b, size_t t) {
          b[t + 13] = 4;
          std::memcpy(&b[t + 14], "COM1", 4);
        }) == Error::kDamaged);
  // Control: a name that only starts like a device name is fine.
  CHECK(ReadPatched([](std::vector<uint8_t>& b, size_t t) {
          b[t + 13] = 7;
          std::memcpy(&b[t + 14], "console", 7);
        }) == Error::kNone);
  // A name running past the end of its table.
  CHECK(ReadPatched([](std::vector<uint8_t>& b, size_t t) { b[t + 13] = 255; }) ==
        Error::kDamaged);
  // Control: the unpatched image reads.
  CHECK(ReadPatched([](std::vector<uint8_t>&, size_t) {}) == Error::kNone);
}

// A folder whose table is the root's own: a loop, refused instead of followed.
static void TestFolderLoop() {
  auto image = BuildImage(kFiles);
  uint32_t root_sector = 0, root_size = 0;
  std::memcpy(&root_sector, &image.bytes[32 * 2048 + 20], 4);
  std::memcpy(&root_size, &image.bytes[32 * 2048 + 24], 4);
  // Find the "movie" entry in the root table and point it at the root table.
  const size_t t = size_t(root_sector) * 2048;
  bool patched = false;
  for (size_t off = 0; off + 14 < root_size;) {
    const uint8_t nlen = image.bytes[t + off + 13];
    if (std::string(reinterpret_cast<char*>(&image.bytes[t + off + 14]), nlen) == "movie") {
      std::memcpy(&image.bytes[t + off + 4], &root_sector, 4);
      std::memcpy(&image.bytes[t + off + 8], &root_size, 4);
      patched = true;
      break;
    }
    off += (14 + nlen + 3) & ~size_t(3);
  }
  CHECK(patched);
  SparseSource src(image);
  Error error = Error::kNone;
  CHECK(!ReadVolume(src, &error).has_value());
  CHECK(error == Error::kDamaged);
}

// FileSource reads past 4 GB (a real DOA4 image is 7.8 GB). The file is made
// sparse, so the test writes a few bytes, not gigabytes.
static void TestFileSourceBeyond4GB(const fs::path& dir) {
  const fs::path path = dir / "big.iso";
  const uint64_t beyond = (uint64_t(5) << 30) + 12345;
  {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD ret = 0;
    DeviceIoControl(h, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &ret, nullptr);
    LARGE_INTEGER at;
    at.QuadPart = LONGLONG(beyond);
    SetFilePointerEx(h, at, nullptr, FILE_BEGIN);
    DWORD written = 0;
    WriteFile(h, "FARDATA", 7, &written, nullptr);
    CloseHandle(h);
  }
  FileSource src;
  CHECK(src.Open(path));
  CHECK(src.size() == beyond + 7);
  char buf[8] = {};
  CHECK(src.Read(beyond, buf, 7) && std::string(buf, 7) == "FARDATA");
  CHECK(!src.Read(beyond, buf, 8));  // past the end
  CHECK(src.Read(1000, buf, 4) && std::memcmp(buf, "\0\0\0\0", 4) == 0);
  FileSource missing;
  CHECK(!missing.Open(dir / "missing.iso"));
}

int main() {
  const fs::path dir = fs::temp_directory_path() / "doa4_xdvdfs_test_data";
  fs::remove_all(dir);
  fs::create_directories(dir);
  try {
    TestReadsEveryFile();
    TestFindsEachVolumeOffset();
    TestNotAnXboxImage();
    TestFileBeyondTheEnd();
    TestTruncatedImage();
    TestUnsafeNamesAreRefused();
    TestFolderLoop();
    TestFileSourceBeyond4GB(dir);
  } catch (const std::exception& e) {
    std::printf("FAIL threw: %s\n", e.what());
    ++g_failures;
  }
  fs::remove_all(dir);
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("xdvdfs: all checks passed\n");
  return 0;
}

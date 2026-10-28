// Reading Xbox / Xbox 360 disc images (the XDVDFS file system). Pure C++, no
// Windows calls; host-tested with synthetic images.
//
// A volume starts at one of four byte offsets depending on the disc layout
// (plain, XGD2 - Dead or Alive 4 is one - XGD3, XGD1). Its descriptor at
// sector 32 holds "MICROSOFT*XBOX*MEDIA" and the root directory's sector and
// size. A directory table is a binary tree of entries: left and right subtree
// offsets (in 4-byte units), the start sector, the size, attributes (0x10 =
// folder) and a name. See notes/specs/2026-10-28-first-run-setup-design.md.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace doa4::xdvdfs {

inline constexpr uint32_t kSector = 2048;
inline constexpr std::array<uint64_t, 4> kVolumeOffsets = {0, 0xFD90000, 0x2080000, 0x18300000};

// A readable image. Read fails (false) when any byte of the range is past the
// end or the read itself fails.
class Source {
 public:
  virtual ~Source() = default;
  virtual uint64_t size() const = 0;
  virtual bool Read(uint64_t offset, void* dst, size_t n) = 0;
};

// An image file on disk (64-bit offsets).
class FileSource : public Source {
 public:
  bool Open(const std::filesystem::path& path);
  uint64_t size() const override { return size_; }
  bool Read(uint64_t offset, void* dst, size_t n) override;

 private:
  std::ifstream file_;
  uint64_t size_ = 0;
};

struct Entry {
  std::string path;  // '/'-separated, as stored on the disc (ASCII)
  bool folder = false;
  uint64_t offset = 0;  // absolute byte offset of the data in the image
  uint64_t size = 0;
};

struct Volume {
  uint64_t base = 0;  // the volume's byte offset in the image
  // Every folder and file; a folder comes before everything inside it.
  std::vector<Entry> entries;
  size_t file_count = 0;
  uint64_t file_bytes = 0;
};

enum class Error {
  kNone,
  kNotXbox,  // no volume descriptor at any known offset
  kDamaged,  // a table or file outside the image, an unsafe name, a loop
};

// Reads the whole directory tree. On failure returns nullopt and sets *error.
std::optional<Volume> ReadVolume(Source& source, Error* error);

// The file entry at `path` ('/'-separated, ASCII case ignored), or null.
const Entry* FindFile(const Volume& volume, std::string_view path);

}  // namespace doa4::xdvdfs

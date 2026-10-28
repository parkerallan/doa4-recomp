// Reading Xbox disc images. See doa4_xdvdfs.h.

#include "doa4_xdvdfs.h"

#include <cstring>
#include <set>

namespace doa4::xdvdfs {

namespace {

constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";
constexpr size_t kMagicSize = 20;
constexpr int kMaxDepth = 32;
constexpr size_t kMaxEntries = 1000000;
constexpr uint32_t kMaxTableSize = 64u << 20;

uint16_t Get16(const uint8_t* p) {
  uint16_t v;
  std::memcpy(&v, p, 2);
  return v;
}

uint32_t Get32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}

char Lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

bool EqualNoCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (Lower(a[i]) != Lower(b[i])) return false;
  }
  return true;
}

// A name that is safe to create as one path component on Windows: printable
// ASCII, none of \ / : * ? " < > |, not "." or "..", not ending in a dot or a
// space, not a device name (CON, NUL, COM1, ...).
bool SafeName(std::string_view name) {
  if (name.empty() || name == "." || name == "..") return false;
  for (const char c : name) {
    if (c < 0x20 || c > 0x7E) return false;
    if (std::strchr("\\/:*?\"<>|", c)) return false;
  }
  if (name.back() == '.' || name.back() == ' ') return false;
  const std::string_view stem = name.substr(0, name.find('.'));
  for (const char* device : {"con", "prn", "aux", "nul"}) {
    if (EqualNoCase(stem, device)) return false;
  }
  if (stem.size() == 4 && (EqualNoCase(stem.substr(0, 3), "com") ||
                           EqualNoCase(stem.substr(0, 3), "lpt")) &&
      stem[3] >= '1' && stem[3] <= '9') {
    return false;
  }
  return true;
}

class Walker {
 public:
  Walker(Source& source, uint64_t base, Volume* volume)
      : source_(source), base_(base), volume_(volume) {}

  // Appends the table's entries (and everything under its folders).
  bool Walk(uint32_t sector, uint32_t size, const std::string& prefix, int depth) {
    if (size == 0) return true;
    if (depth > kMaxDepth || size > kMaxTableSize || ancestors_.count(sector)) return false;
    const uint64_t at = base_ + uint64_t(sector) * kSector;
    std::vector<uint8_t> table(size);
    if (at + size > source_.size() || !source_.Read(at, table.data(), size)) return false;
    ancestors_.insert(sector);
    std::vector<size_t> stack = {0};
    std::set<size_t> seen;
    while (!stack.empty()) {
      const size_t off = stack.back();
      stack.pop_back();
      if (off + 14 > table.size() || !seen.insert(off).second) continue;
      const uint8_t* e = &table[off];
      const uint16_t left = Get16(e), right = Get16(e + 2);
      if (left == 0xFFFF && right == 0xFFFF) continue;  // padding
      const uint32_t start = Get32(e + 4), length = Get32(e + 8);
      const uint8_t attributes = e[12], name_size = e[13];
      if (off + 14 + name_size > table.size()) return false;
      const std::string name(reinterpret_cast<const char*>(e + 14), name_size);
      if (!SafeName(name)) return false;
      if (left) stack.push_back(size_t(left) * 4);
      if (right) stack.push_back(size_t(right) * 4);
      if (volume_->entries.size() >= kMaxEntries) return false;
      Entry entry;
      entry.path = prefix.empty() ? name : prefix + "/" + name;
      entry.folder = (attributes & 0x10) != 0;
      entry.offset = base_ + uint64_t(start) * kSector;
      entry.size = length;
      if (entry.folder) {
        volume_->entries.push_back(entry);
        if (!Walk(start, length, entry.path, depth + 1)) return false;
      } else {
        if (length && entry.offset + length > source_.size()) return false;
        volume_->file_count += 1;
        volume_->file_bytes += length;
        volume_->entries.push_back(std::move(entry));
      }
    }
    ancestors_.erase(sector);
    return true;
  }

 private:
  Source& source_;
  uint64_t base_;
  Volume* volume_;
  std::set<uint32_t> ancestors_;  // tables on the current path (loop guard)
};

}  // namespace

bool FileSource::Open(const std::filesystem::path& path) {
  file_.open(path, std::ios::binary);
  if (!file_) return false;
  file_.seekg(0, std::ios::end);
  const std::streamoff end = file_.tellg();
  if (end < 0) return false;
  size_ = uint64_t(end);
  return true;
}

bool FileSource::Read(uint64_t offset, void* dst, size_t n) {
  if (!file_.is_open() || offset > size_ || n > size_ - offset) return false;
  file_.clear();
  file_.seekg(std::streamoff(offset), std::ios::beg);
  file_.read(static_cast<char*>(dst), std::streamsize(n));
  return file_.gcount() == std::streamsize(n);
}

std::optional<Volume> ReadVolume(Source& source, Error* error) {
  for (const uint64_t base : kVolumeOffsets) {
    const uint64_t at = base + 32 * uint64_t(kSector);
    uint8_t descriptor[28];
    if (at + sizeof(descriptor) > source.size() || !source.Read(at, descriptor, sizeof(descriptor)))
      continue;
    if (std::memcmp(descriptor, kMagic, kMagicSize) != 0) continue;
    Volume volume;
    volume.base = base;
    Walker walker(source, base, &volume);
    if (!walker.Walk(Get32(descriptor + 20), Get32(descriptor + 24), "", 0)) {
      *error = Error::kDamaged;
      return std::nullopt;
    }
    *error = Error::kNone;
    return volume;
  }
  *error = Error::kNotXbox;
  return std::nullopt;
}

const Entry* FindFile(const Volume& volume, std::string_view path) {
  for (const auto& entry : volume.entries) {
    if (!entry.folder && EqualNoCase(entry.path, path)) return &entry;
  }
  return nullptr;
}

}  // namespace doa4::xdvdfs

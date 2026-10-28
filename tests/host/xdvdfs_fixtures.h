// Synthetic Xbox disc images (XDVDFS) for the host tests. Header-only.
//
// BuildImage lays out a volume at `base`: the volume descriptor at sector 32,
// then the directory tables and file data, each starting on its own sector.
// A directory's entries are chained through their right-subtree offsets (a
// valid, if unbalanced, XDVDFS tree), in the order given.

#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace doa4::xdvdfs::fixtures {

struct FakeFile {
  std::string path;  // '/'-separated; parent folders are implied
  std::string data;
};

// The volume's bytes, starting at `base` (bytes before `base` are not stored:
// see SparseImage).
struct BuiltImage {
  uint64_t base = 0;
  std::vector<uint8_t> bytes;  // image bytes from `base` on
};

inline constexpr uint32_t kSector = 2048;

namespace detail {

struct Dir {
  std::map<std::string, Dir> dirs;               // ordered by name
  std::vector<std::pair<std::string, std::string>> files;  // in given order
  std::vector<std::string> order;                 // names in given order (dirs and files)
};

inline void Insert(Dir& root, const FakeFile& file) {
  Dir* dir = &root;
  size_t start = 0;
  for (;;) {
    const size_t slash = file.path.find('/', start);
    const std::string name = file.path.substr(start, slash - start);
    if (slash == std::string::npos) {
      dir->files.emplace_back(name, file.data);
      dir->order.push_back(name);
      return;
    }
    if (!dir->dirs.count(name)) dir->order.push_back(name);
    dir = &dir->dirs[name];
    start = slash + 1;
  }
}

inline void Put16(std::vector<uint8_t>& b, size_t at, uint16_t v) { std::memcpy(&b[at], &v, 2); }
inline void Put32(std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); }

struct Writer {
  std::vector<uint8_t> out;  // from sector 0 of the volume
  uint32_t next_sector = 33;

  uint32_t Allocate(size_t bytes) {
    const uint32_t first = next_sector;
    const uint32_t sectors = uint32_t((bytes + kSector - 1) / kSector);
    next_sector += sectors == 0 ? 0 : sectors;
    if (out.size() < size_t(next_sector) * kSector) out.resize(size_t(next_sector) * kSector);
    return first;
  }

  // Writes the directory's table and everything under it; returns {sector, size}.
  std::pair<uint32_t, uint32_t> WriteDir(const Dir& dir) {
    if (dir.order.empty()) return {0, 0};
    // Children first, so their sectors are known when the table is written.
    std::map<std::string, std::pair<uint32_t, uint32_t>> where;
    for (const auto& name : dir.order) {
      auto it = dir.dirs.find(name);
      if (it != dir.dirs.end()) {
        where[name] = WriteDir(it->second);
      }
    }
    for (const auto& [name, data] : dir.files) {
      const uint32_t sector = Allocate(data.size());
      if (!data.empty()) std::memcpy(&out[size_t(sector) * kSector], data.data(), data.size());
      where[name] = {data.empty() ? 0 : sector, uint32_t(data.size())};
    }
    std::vector<uint8_t> table;
    for (size_t i = 0; i < dir.order.size(); ++i) {
      const std::string& name = dir.order[i];
      const bool is_dir = dir.dirs.count(name) != 0;
      const size_t at = table.size();
      const size_t len = (14 + name.size() + 3) & ~size_t(3);
      table.resize(at + len, 0xFF);
      const size_t next = i + 1 < dir.order.size() ? (at + len) / 4 : 0;
      Put16(table, at, 0);
      Put16(table, at + 2, uint16_t(next));
      Put32(table, at + 4, where[name].first);
      Put32(table, at + 8, where[name].second);
      table[at + 12] = is_dir ? 0x10 : 0x20;
      table[at + 13] = uint8_t(name.size());
      std::memcpy(&table[at + 14], name.data(), name.size());
    }
    const uint32_t sector = Allocate(table.size());
    std::memcpy(&out[size_t(sector) * kSector], table.data(), table.size());
    return {sector, uint32_t(table.size())};
  }
};

}  // namespace detail

inline BuiltImage BuildImage(const std::vector<FakeFile>& files, uint64_t base = 0) {
  detail::Dir root;
  for (const auto& f : files) detail::Insert(root, f);
  detail::Writer w;
  w.out.resize(33 * kSector);
  const auto [sector, size] = w.WriteDir(root);
  std::memcpy(&w.out[32 * kSector], "MICROSOFT*XBOX*MEDIA", 20);
  detail::Put32(w.out, 32 * kSector + 20, sector);
  detail::Put32(w.out, 32 * kSector + 24, size);
  std::memcpy(&w.out[32 * kSector + 0x7EC], "MICROSOFT*XBOX*MEDIA", 20);
  return {base, std::move(w.out)};
}

}  // namespace doa4::xdvdfs::fixtures

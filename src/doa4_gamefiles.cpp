// The game files. See doa4_gamefiles.h.

#include "doa4_gamefiles.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <fstream>
#include <optional>
#include <system_error>
#include <vector>

#include "doa4_utf8.h"

#pragma comment(lib, "bcrypt.lib")

namespace doa4::gamefiles {

namespace fs = std::filesystem;

namespace {

constexpr size_t kChunk = 8u << 20;

std::string Hex(const uint8_t* bytes, size_t n) {
  static const char kDigits[] = "0123456789abcdef";
  std::string out(n * 2, '0');
  for (size_t i = 0; i < n; ++i) {
    out[i * 2] = kDigits[bytes[i] >> 4];
    out[i * 2 + 1] = kDigits[bytes[i] & 15];
  }
  return out;
}

std::optional<std::string> Sha1OfFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  Sha1 hash;
  std::vector<char> buf(1 << 20);
  while (in) {
    in.read(buf.data(), std::streamsize(buf.size()));
    if (in.gcount() > 0) hash.Update(buf.data(), size_t(in.gcount()));
  }
  if (in.bad()) return std::nullopt;
  return hash.HexDigest();
}

// What one Extract call created, so a failure or a cancel removes exactly
// that.
struct Created {
  std::vector<fs::path> folders;  // in creation order
  std::vector<fs::path> files;

  // Creates `dir` and any missing parents, recording each one created.
  bool MakeFolders(const fs::path& dir) {
    std::vector<fs::path> missing;
    std::error_code ec;
    for (fs::path p = dir; !p.empty() && !fs::exists(p, ec); p = p.parent_path()) {
      missing.push_back(p);
      if (p == p.parent_path()) break;
    }
    for (auto it = missing.rbegin(); it != missing.rend(); ++it) {
      if (!fs::create_directory(*it, ec) && !fs::is_directory(*it, ec)) return false;
      folders.push_back(*it);
    }
    return fs::is_directory(dir, ec);
  }

  void Remove() {
    std::error_code ec;
    for (const auto& f : files) fs::remove(f, ec);
    for (auto it = folders.rbegin(); it != folders.rend(); ++it) fs::remove(*it, ec);  // only if empty
  }
};

}  // namespace

struct Sha1::State {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
};

Sha1::Sha1() : state_(std::make_unique<State>()) {
  if (BCryptOpenAlgorithmProvider(&state_->alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) == 0) {
    BCryptCreateHash(state_->alg, &state_->hash, nullptr, 0, nullptr, 0, 0);
  }
}

Sha1::~Sha1() {
  if (state_->hash) BCryptDestroyHash(state_->hash);
  if (state_->alg) BCryptCloseAlgorithmProvider(state_->alg, 0);
}

void Sha1::Update(const void* data, size_t size) {
  const auto* p = static_cast<const uint8_t*>(data);
  while (size > 0 && state_->hash) {
    const ULONG n = ULONG(std::min<size_t>(size, 1u << 30));
    BCryptHashData(state_->hash, const_cast<PUCHAR>(p), n, 0);
    p += n;
    size -= n;
  }
}

std::string Sha1::HexDigest() {
  uint8_t digest[20] = {};
  if (!state_->hash || BCryptFinishHash(state_->hash, digest, sizeof(digest), 0) != 0) return {};
  return Hex(digest, sizeof(digest));
}

std::string Sha1Hex(const void* data, size_t size) {
  Sha1 h;
  h.Update(data, size);
  return h.HexDigest();
}

bool HasXex(const fs::path& dir) {
  std::error_code ec;
  return fs::is_regular_file(dir / kXexName, ec);
}

UserDataChoice ChooseUserData(const fs::path& sdk_value, bool from_command_line,
                              const fs::path& saved) {
  if (from_command_line) return {sdk_value, UserDataSource::kCommandLine, ""};
  if (!saved.empty()) {
    if (PathFitsCodePage(saved)) return {saved, UserDataSource::kSaved, ""};
    return {sdk_value, UserDataSource::kDefault,
            "The saved data folder " + Utf8(saved) +
                " has characters in its path that this build cannot use; using the default. Choose a "
                "folder whose path uses plain letters and digits."};
  }
  return {sdk_value, UserDataSource::kDefault, ""};
}

bool PathFitsCodePage(const fs::path& path) {
  const std::wstring& text = path.native();
  if (text.empty() || GetACP() == CP_UTF8) return true;  // UTF-8 code page: everything fits
  BOOL lossy = FALSE;
  const int n = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, text.data(), int(text.size()),
                                    nullptr, 0, nullptr, &lossy);
  return n > 0 && !lossy;
}

StartChoice ChooseStartPath(const fs::path& command_line, const fs::path& saved,
                            const std::function<bool(const fs::path&)>& has_xex,
                            const std::function<bool(const fs::path&)>& fits) {
  StartChoice choice;
  if (!command_line.empty()) {
    if (has_xex(command_line)) {
      choice.path = command_line;
      choice.from_command_line = true;
    } else {
      choice.reason = "The game files were not found at " + Utf8(command_line) +
                      " (given with --game_data_root).";
    }
    return choice;
  }
  if (!saved.empty()) {
    if (fits && !fits(saved)) {
      choice.reason = "The saved game folder " + Utf8(saved) +
                      " has characters in its path that this build cannot use. Choose a folder "
                      "whose path uses plain letters and digits.";
    } else if (has_xex(saved)) {
      choice.path = saved;
    } else {
      choice.reason = "The game files were not found at " + Utf8(saved) + ".";
    }
  }
  return choice;
}

GameTab DescribeGameTab(const fs::path& now, bool from_command_line, const fs::path& saved) {
  GameTab tab;
  tab.now = now;
  if (from_command_line) {
    tab.can_change = false;
    tab.note =
        "This start used --game_data_root (for example from run_doa4.bat), which overrides a "
        "saved location, so Change is off. Start dead_or_alive_4.exe directly to use the saved "
        "location and change it here.";
  } else if (!saved.empty() && saved != now) {
    tab.note = "Next start: " + Utf8(saved);
  }
  return tab;
}

ImageCheck CheckImage(const fs::path& iso, std::string_view expected_sha1) {
  ImageCheck check;
  xdvdfs::FileSource src;
  if (!src.Open(iso)) return check;  // kUnreadable
  xdvdfs::Error error = xdvdfs::Error::kNone;
  auto volume = xdvdfs::ReadVolume(src, &error);
  if (!volume) {
    check.verdict = error == xdvdfs::Error::kNotXbox ? Verdict::kNotXbox : Verdict::kDamaged;
    return check;
  }
  check.volume = std::move(*volume);
  const xdvdfs::Entry* xex = xdvdfs::FindFile(check.volume, kXexName);
  if (!xex) {
    check.verdict = Verdict::kNoXex;
    return check;
  }
  Sha1 hash;
  std::vector<char> buf(std::min<uint64_t>(xex->size, kChunk) + 1);
  for (uint64_t done = 0; done < xex->size;) {
    const size_t n = size_t(std::min<uint64_t>(xex->size - done, kChunk));
    if (!src.Read(xex->offset + done, buf.data(), n)) {
      check.verdict = Verdict::kDamaged;
      return check;
    }
    hash.Update(buf.data(), n);
    done += n;
  }
  check.verdict = hash.HexDigest() == expected_sha1 ? Verdict::kOk : Verdict::kWrongVersion;
  return check;
}

Verdict CheckFolder(const fs::path& dir, std::string_view expected_sha1) {
  if (!PathFitsCodePage(dir)) return Verdict::kPathUnsupported;
  if (!HasXex(dir)) return Verdict::kNoXex;
  // An extraction that was killed (a crash, power loss) leaves .part files.
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    if (it->path().extension() == ".part") return Verdict::kIncomplete;
  }
  const auto digest = Sha1OfFile(dir / kXexName);
  if (!digest) return Verdict::kUnreadable;
  return *digest == expected_sha1 ? Verdict::kOk : Verdict::kWrongVersion;
}

TargetState InspectTarget(const fs::path& target) {
  std::error_code ec;
  const auto status = fs::status(target, ec);
  if (!fs::exists(status)) return TargetState::kNew;
  if (!fs::is_directory(status)) return TargetState::kNotAFolder;
  return fs::directory_iterator(target, ec) == fs::directory_iterator() && !ec
             ? TargetState::kEmpty
             : TargetState::kNotEmpty;
}

ExtractResult Extract(xdvdfs::Source& image, const xdvdfs::Volume& volume, const fs::path& target,
                      std::string_view expected_sha1, const std::atomic<bool>& cancel,
                      const std::function<void(const Progress&)>& progress) {
  ExtractResult result;
  switch (InspectTarget(target)) {
    case TargetState::kNotEmpty:
      result.error = "The folder " + Utf8(target) + " is not empty.";
      return result;
    case TargetState::kNotAFolder:
      result.error = Utf8(target) + " is a file, not a folder.";
      return result;
    default:
      break;
  }
  Created created;
  auto fail = [&](std::string error) {
    created.Remove();
    result.outcome = ExtractOutcome::kFailed;
    result.error = std::move(error);
    return result;
  };
  auto cancelled = [&] {
    created.Remove();
    result.outcome = ExtractOutcome::kCancelled;
    return result;
  };
  if (!created.MakeFolders(target)) return fail("Could not create the folder " + Utf8(target) + ".");

  // default.xex goes last: until every other file is complete the target
  // holds no default.xex, so a killed extraction never looks like the game.
  std::vector<const xdvdfs::Entry*> order;
  const xdvdfs::Entry* xex = xdvdfs::FindFile(volume, kXexName);
  for (const auto& entry : volume.entries) {
    if (&entry != xex) order.push_back(&entry);
  }
  if (xex) order.push_back(xex);

  Progress p;
  p.total = volume.file_bytes;
  std::vector<char> buf;
  for (const xdvdfs::Entry* entry_ptr : order) {
    const xdvdfs::Entry& entry = *entry_ptr;
    if (cancel.load()) return cancelled();
    fs::path out = target;
    for (size_t start = 0;;) {
      const size_t slash = entry.path.find('/', start);
      out /= entry.path.substr(start, slash - start);
      if (slash == std::string::npos) break;
      start = slash + 1;
    }
    if (entry.folder) {
      if (!created.MakeFolders(out)) return fail("Could not create the folder " + Utf8(out) + ".");
      continue;
    }
    if (!created.MakeFolders(out.parent_path()))
      return fail("Could not create the folder " + Utf8(out.parent_path()) + ".");
    fs::path part = out;
    part += ".part";
    created.files.push_back(part);
    std::ofstream file(part, std::ios::binary | std::ios::trunc);
    if (!file) return fail("Could not create " + Utf8(part) + ".");
    if (buf.empty() && entry.size) buf.resize(kChunk);
    for (uint64_t done = 0; done < entry.size;) {
      if (cancel.load()) {
        file.close();
        return cancelled();
      }
      const size_t n = size_t(std::min<uint64_t>(entry.size - done, kChunk));
      if (!image.Read(entry.offset + done, buf.data(), n)) {
        file.close();
        return fail("Could not read " + entry.path + " from the disc image.");
      }
      file.write(buf.data(), std::streamsize(n));
      if (!file) {
        file.close();
        return fail("Could not write " + Utf8(part) + " (is the drive full?).");
      }
      done += n;
      p.done += n;
      p.file = entry.path;
      if (progress) progress(p);
    }
    file.close();
    if (!file) return fail("Could not write " + Utf8(part) + ".");
    std::error_code ec;
    fs::rename(part, out, ec);
    if (ec) return fail("Could not rename " + Utf8(part) + ".");
    created.files.back() = out;
  }
  const auto digest = Sha1OfFile(target / kXexName);
  if (!digest || *digest != expected_sha1) {
    return fail("The copied default.xex did not match the disc image's; the copy was removed.");
  }
  result.outcome = ExtractOutcome::kDone;
  return result;
}

}  // namespace doa4::gamefiles

// Host test for src/doa4_backup.cpp. Built and run by run_backup_test.bat.

#include "../../src/doa4_backup.h"

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

namespace fs = std::filesystem;

static void WriteFile(const fs::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

static std::string ReadAll(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static int CountBackups(const fs::path& dir) {
  int n = 0;
  if (!fs::exists(dir)) return 0;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (entry.path().filename().string().rfind("ups.dat.backup-", 0) == 0) ++n;
  }
  return n;
}

// A complete save file: kSaveFileSize bytes.
static std::string Save(char fill) { return std::string(doa4::kSaveFileSize, fill); }

static void TestBackup() {
  const fs::path root = fs::temp_directory_path() / "doa4_backup_test_data";
  fs::remove_all(root);
  const fs::path content = root / "B13EBABEBABEBABE" / "544307D1" / "00000001";
  const fs::path save = content / "ups.dat";
  const fs::path backups = root / "doa4_save_backups";
  fs::create_directories(save);
  WriteFile(save / "ups.dat", Save('1'));
  WriteFile(save / "__thumbnail.png", "png");

  // First call copies the whole folder into the backup folder, outside the
  // folder the game lists its saves from.
  doa4::BackupResult first = doa4::EnsureSaveBackup(save, backups, "2026-10-28-120000");
  CHECK(first.ok);
  CHECK(first.backup == backups / "ups.dat.backup-2026-10-28-120000");
  CHECK(ReadAll(first.backup / "ups.dat") == Save('1'));
  CHECK(ReadAll(first.backup / "__thumbnail.png") == "png");
  CHECK(ReadAll(save / "ups.dat") == Save('1'));  // the save itself untouched
  CHECK(CountBackups(content) == 0);              // nothing new beside the save

  // Later calls keep the first backup and make no new one, even after the
  // save has changed.
  WriteFile(save / "ups.dat", Save('2'));
  doa4::BackupResult second = doa4::EnsureSaveBackup(save, backups, "2026-10-29-090000");
  CHECK(second.ok);
  CHECK(second.backup == first.backup);
  CHECK(ReadAll(first.backup / "ups.dat") == Save('1'));
  CHECK(CountBackups(backups) == 1);

  // No save folder yet: nothing to lose, nothing copied.
  const fs::path missing = root / "other" / "ups.dat";
  doa4::BackupResult none = doa4::EnsureSaveBackup(missing, root / "b_none", "s");
  CHECK(none.ok);
  CHECK(none.backup.empty());
  CHECK(!none.message.empty());
  CHECK(!fs::exists(root / "b_none"));

  // A save file that is not complete (caught mid-save) is not copied.
  const fs::path partial = root / "partial" / "ups.dat";
  fs::create_directories(partial);
  WriteFile(partial / "ups.dat", "only part of it");
  doa4::BackupResult cut = doa4::EnsureSaveBackup(partial, root / "b_partial", "s");
  CHECK(!cut.ok);
  CHECK(cut.message.find("the game may be saving") != std::string::npos);
  CHECK(CountBackups(root / "b_partial") == 0);

  // An existing backup counts only if it holds a complete save; an empty or
  // partial one does not stop a real backup being made.
  const fs::path save2 = root / "two" / "ups.dat";
  fs::create_directories(save2);
  WriteFile(save2 / "ups.dat", Save('3'));
  const fs::path backups2 = root / "b_two";
  fs::create_directories(backups2 / "ups.dat.backup-empty");
  fs::create_directories(backups2 / "ups.dat.backup-short");
  WriteFile(backups2 / "ups.dat.backup-short" / "ups.dat", "short");
  doa4::BackupResult real = doa4::EnsureSaveBackup(save2, backups2, "s2");
  CHECK(real.ok);
  CHECK(real.backup == backups2 / "ups.dat.backup-s2");
  CHECK(ReadAll(real.backup / "ups.dat") == Save('3'));

  // A copy that cannot be made is an error: the backup name is taken by a file.
  const fs::path save3 = root / "three" / "ups.dat";
  fs::create_directories(save3);
  WriteFile(save3 / "ups.dat", Save('4'));
  const fs::path backups3 = root / "b_three";
  fs::create_directories(backups3);
  WriteFile(backups3 / "ups.dat.backup-blocked", "a file, not a folder");
  doa4::BackupResult blocked = doa4::EnsureSaveBackup(save3, backups3, "blocked");
  CHECK(!blocked.ok);
  CHECK(!blocked.message.empty());
  CHECK(fs::is_regular_file(backups3 / "ups.dat.backup-blocked"));

  // A Windows profile or folder whose name is outside the ANSI code page:
  // nothing throws (path::string() would), errors are reported as text.
  const fs::path wide = root / fs::path(u8"\u5199\u771F");
  const fs::path save4 = wide / "ups.dat";
  fs::create_directories(save4);
  WriteFile(save4 / "ups.dat", Save('5'));
  const fs::path backups4 = wide / "b";
  fs::create_directories(backups4 / fs::path(u8"\u5199\u771F"));  // a foreign entry in the folder
  WriteFile(backups4 / "ups.dat.backup-x", "a file in the way");
  try {
    doa4::BackupResult r = doa4::EnsureSaveBackup(save4, backups4, "x");
    CHECK(!r.ok && !r.message.empty());
    doa4::BackupResult r2 = doa4::EnsureSaveBackup(save4, backups4, "y");
    CHECK(r2.ok && fs::exists(r2.backup / "ups.dat"));
  } catch (const std::exception& e) {
    std::printf("FAIL EnsureSaveBackup threw: %s\n", e.what());
    ++g_failures;
  }

  fs::remove_all(root);
}

int main() {
  try {
    TestBackup();
  } catch (const std::exception& e) {
    std::printf("FAIL TestBackup threw: %s\n", e.what());
    ++g_failures;
  }
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("backup: all checks passed\n");
  return 0;
}

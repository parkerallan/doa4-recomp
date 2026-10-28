// Host test for src/doa4_setup_flow.cpp (the first-run setup screen's
// logic, without ImGui). Built and run by run_setup_flow_test.bat.

#include "../../src/doa4_setup_flow.h"

#include <cstdio>
#include <deque>
#include <map>
#include <set>

static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using namespace doa4::setup;
using doa4::gamefiles::ExtractOutcome;
using doa4::gamefiles::ExtractResult;
using doa4::gamefiles::ImageCheck;
using doa4::gamefiles::Progress;
using doa4::gamefiles::TargetState;
using doa4::gamefiles::Verdict;
namespace fs = std::filesystem;

constexpr uint64_t kGB = 1ull << 30;

// Scripted answers for every effect; work and posts run inline.
struct Fake {
  std::deque<std::optional<fs::path>> isos, folders;  // dialog answers, in order
  std::vector<fs::path> dialog_starts;
  std::map<fs::path, Verdict> image_verdicts, folder_verdicts;
  uint64_t image_bytes = 7 * kGB;
  std::map<fs::path, TargetState> targets;  // default kNew
  std::map<fs::path, uint64_t> free;        // default 100 GB
  std::set<fs::path> unusable;              // paths PathFitsCodePage would reject
  ExtractResult extract_result{ExtractOutcome::kDone, ""};
  bool saw_cancel = false;
  int extracts = 0;
  int asyncs = 0;
  std::vector<fs::path> done;
  Flow* flow = nullptr;  // for an extract that cancels part-way
  bool cancel_during_extract = false;

  Effects Make() {
    Effects e;
    e.pick_iso = [this](const fs::path& start) {
      dialog_starts.push_back(start);
      if (isos.empty()) return std::optional<fs::path>();  // more dialogs than scripted
      auto r = isos.front();
      isos.pop_front();
      return r;
    };
    e.pick_folder = [this](const fs::path& start) {
      dialog_starts.push_back(start);
      if (folders.empty()) return std::optional<fs::path>();
      auto r = folders.front();
      folders.pop_front();
      return r;
    };
    e.run_async = [this](std::function<void()> work) {
      ++asyncs;
      work();
    };
    e.post = [](std::function<void()> fn) { fn(); };
    e.check_image = [this](const fs::path& iso) {
      ImageCheck c;
      c.verdict = image_verdicts.count(iso) ? image_verdicts[iso] : Verdict::kOk;
      c.volume.file_bytes = image_bytes;
      c.volume.file_count = 3;
      return c;
    };
    e.check_folder = [this](const fs::path& dir) {
      return folder_verdicts.count(dir) ? folder_verdicts[dir] : Verdict::kOk;
    };
    e.inspect_target = [this](const fs::path& t) {
      return targets.count(t) ? targets[t] : TargetState::kNew;
    };
    e.free_bytes = [this](const fs::path& t) -> std::optional<uint64_t> {
      return free.count(t) ? free[t] : 100 * kGB;
    };
    e.path_fits = [this](const fs::path& t) { return !unusable.count(t); };
    e.extract = [this](const fs::path&, const doa4::xdvdfs::Volume& v, const fs::path&,
                       const std::atomic<bool>& cancel,
                       const std::function<void(const Progress&)>& progress) {
      ++extracts;
      Progress p{v.file_bytes / 2, v.file_bytes, "anm.afs"};
      progress(p);
      if (cancel_during_extract) flow->Cancel();
      saw_cancel = cancel.load();
      return extract_result;
    };
    e.done = [this](const fs::path& p) { done.push_back(p); };
    return e;
  }
};

static void TestImageHappyPath() {
  Fake f;
  f.isos = {fs::path("D:/isos/doa4.iso")};
  Flow flow(f.Make());
  CHECK(flow.stage() == Stage::kChoose && flow.message().empty());
  flow.ChooseImage();
  CHECK(f.asyncs == 1);  // the image is read off the UI thread
  CHECK(flow.stage() == Stage::kTarget);
  CHECK(flow.target() == fs::path("D:/isos/DOA4"));  // default: beside the image
  CHECK(flow.need_bytes() == 7 * kGB + kSpaceSlack);
  CHECK(flow.CanExtract());
  flow.Extract();
  CHECK(f.extracts == 1 && f.done == std::vector<fs::path>{fs::path("D:/isos/DOA4")});
  CHECK(flow.stage() == Stage::kFinished);
  CHECK(flow.progress().done == 7 * kGB / 2 && flow.progress().total == 7 * kGB);
}

static void TestImageRefusals() {
  const fs::path iso = "D:/isos/x.iso";
  for (const auto& [verdict, needle] : std::vector<std::pair<Verdict, std::string>>{
           {Verdict::kWrongVersion, "af8011945037b37c98fb8a78d5a4e844dd56c33f"},
           {Verdict::kNotXbox, "not an Xbox 360 disc image"},
           {Verdict::kDamaged, "damaged or incomplete"},
           {Verdict::kNoXex, "does not contain default.xex"},
           {Verdict::kUnreadable, "could not be read"}}) {
    Fake f;
    f.isos = {iso};
    f.image_verdicts[iso] = verdict;
    Flow flow(f.Make());
    flow.ChooseImage();
    CHECK(flow.stage() == Stage::kChoose);
    CHECK(flow.message().find(needle) != std::string::npos);
    CHECK(f.done.empty() && f.extracts == 0);
  }
  // The wrong-version text names the supported disc and why.
  Fake f;
  f.isos = {iso, fs::path("D:/isos/good.iso")};
  f.image_verdicts[iso] = Verdict::kWrongVersion;
  Flow flow(f.Make());
  flow.ChooseImage();
  CHECK(flow.message().find("Dead or Alive 4 (USA, Europe) (Rev 1)") != std::string::npos);
  CHECK(flow.message().find("crash") != std::string::npos);
  // ... and the user can pick again; the message clears.
  flow.ChooseImage();
  CHECK(flow.stage() == Stage::kTarget && flow.message().empty());
}

static void TestDialogCancelChangesNothing() {
  Fake f;
  f.isos = {std::nullopt};
  f.folders = {std::nullopt};
  Flow flow(f.Make());
  flow.ChooseImage();
  CHECK(flow.stage() == Stage::kChoose && f.asyncs == 0);
  flow.ChooseFolder();
  CHECK(flow.stage() == Stage::kChoose && f.asyncs == 0 && f.done.empty());
}

static void TestFolderPath() {
  Fake f;
  const fs::path good = "E:/DOA4", wrong = "E:/other", empty = "E:/empty";
  f.folders = {wrong, empty, good};
  f.folder_verdicts[wrong] = Verdict::kWrongVersion;
  f.folder_verdicts[empty] = Verdict::kNoXex;
  Flow flow(f.Make());
  flow.ChooseFolder();
  CHECK(flow.stage() == Stage::kChoose && flow.message().find("af801194") != std::string::npos);
  flow.ChooseFolder();
  CHECK(flow.message().find("folder does not contain default.xex") != std::string::npos);
  CHECK(f.done.empty());
  flow.ChooseFolder();
  CHECK(f.done == std::vector<fs::path>{good} && flow.stage() == Stage::kFinished);
  CHECK(f.extracts == 0);  // a folder is used as it is
}

static void TestTargetRules() {
  Fake f;
  const fs::path iso = "D:/isos/doa4.iso", full = "D:/isos/DOA4", other = "F:/games/DOA4";
  f.isos = {iso};
  f.targets[full] = TargetState::kNotEmpty;
  f.targets[other] = TargetState::kEmpty;  // an empty folder the user picked: used as it is
  f.folders = {other};
  Flow flow(f.Make());
  flow.ChooseImage();
  CHECK(flow.target() == full && !flow.CanExtract());
  CHECK(flow.target_note().find("not empty") != std::string::npos);
  flow.Extract();  // refused while it cannot extract
  CHECK(f.extracts == 0);
  flow.ChangeTarget();
  CHECK(!f.dialog_starts.empty() && f.dialog_starts.back() == fs::path("D:/isos"));
  CHECK(flow.target() == other && flow.CanExtract() && flow.target_note().empty());
  // Changing it again starts beside the current target (not the image's
  // folder, which the first change happened to share).
  flow.ChangeTarget();
  CHECK(f.dialog_starts.back() == fs::path("F:/games"));
  CHECK(flow.target() == other);  // the dialog was cancelled: unchanged
  // A file in the way.
  Fake g;
  g.isos = {iso};
  g.targets[full] = TargetState::kNotAFolder;
  Flow flow2(g.Make());
  flow2.ChooseImage();
  CHECK(!flow2.CanExtract() && !flow2.target_note().empty());
}

// The folder dialog can only pick existing folders: an empty one is the
// target itself; any other gets a new DOA4 folder inside it.
static void TestChangeTargetPicksOrNests() {
  Fake f;
  const fs::path iso = "D:/isos/doa4.iso", games = "F:/games", empty = "G:/new";
  f.isos = {iso};
  f.targets[games] = TargetState::kNotEmpty;
  f.targets[empty] = TargetState::kEmpty;
  f.folders = {games, empty};
  Flow flow(f.Make());
  flow.ChooseImage();
  flow.ChangeTarget();
  CHECK(flow.target() == games / "DOA4" && flow.CanExtract());
  flow.ChangeTarget();
  CHECK(flow.target() == empty && flow.CanExtract());
  // A DOA4 folder inside the picked one that is itself not empty is refused.
  Fake g;
  g.isos = {iso};
  g.targets[games] = TargetState::kNotEmpty;
  g.targets[games / "DOA4"] = TargetState::kNotEmpty;
  g.folders = {games};
  Flow flow2(g.Make());
  flow2.ChooseImage();
  flow2.ChangeTarget();
  CHECK(flow2.target() == games / "DOA4" && !flow2.CanExtract());
}

// Free space: the boundary is need_bytes(); one byte short refuses.
static void TestFreeSpace() {
  const fs::path iso = "D:/isos/doa4.iso", target = "D:/isos/DOA4";
  const uint64_t need = 7 * kGB + kSpaceSlack;
  for (const auto& [free, ok] : std::vector<std::pair<uint64_t, bool>>{
           {need, true}, {need - 1, false}, {2 * kGB, false}}) {
    Fake f;
    f.isos = {iso};
    f.free[target] = free;
    Flow flow(f.Make());
    flow.ChooseImage();
    CHECK(flow.free_bytes() == free);
    CHECK(flow.CanExtract() == ok);
    if (!ok) CHECK(flow.target_note().find("free") != std::string::npos);
  }
  // Free space unknown (the drive cannot be queried): refused, with a note.
  Fake f;
  f.isos = {iso};
  Effects e = f.Make();
  e.free_bytes = [](const fs::path&) -> std::optional<uint64_t> { return std::nullopt; };
  Flow flow(std::move(e));
  flow.ChooseImage();
  CHECK(!flow.CanExtract() && !flow.target_note().empty());
}

static void TestExtractEndings() {
  const fs::path iso = "D:/isos/doa4.iso";
  // Failed: back to the target stage with the error; nothing chosen.
  Fake f;
  f.isos = {iso};
  f.extract_result = {ExtractOutcome::kFailed, "Could not write X (is the drive full?)."};
  Flow flow(f.Make());
  flow.ChooseImage();
  flow.Extract();
  CHECK(flow.stage() == Stage::kTarget && f.done.empty());
  CHECK(flow.message().find("is the drive full") != std::string::npos);
  // Cancelled, through Cancel() while extracting: the flag reaches the copy.
  Fake g;
  g.isos = {iso};
  g.extract_result = {ExtractOutcome::kCancelled, ""};
  g.cancel_during_extract = true;
  Flow flow2(g.Make());
  g.flow = &flow2;
  flow2.ChooseImage();
  flow2.Extract();
  CHECK(g.saw_cancel);
  CHECK(flow2.stage() == Stage::kTarget && g.done.empty());
  CHECK(flow2.message().find("cancelled") != std::string::npos);
  // Control: without Cancel() the copy does not see the flag.
  Fake h;
  h.isos = {iso};
  Flow flow3(h.Make());
  h.flow = &flow3;
  flow3.ChooseImage();
  flow3.Extract();
  CHECK(!h.saw_cancel && h.done.size() == 1);
  // A second extraction starts with the flag cleared.
  Fake k;
  k.isos = {iso};
  k.extract_result = {ExtractOutcome::kCancelled, ""};
  k.cancel_during_extract = true;
  Flow flow4(k.Make());
  k.flow = &flow4;
  flow4.ChooseImage();
  flow4.Extract();
  k.cancel_during_extract = false;
  k.extract_result = {ExtractOutcome::kDone, ""};
  flow4.Extract();
  CHECK(!k.saw_cancel && k.done.size() == 1);
}

static void TestBack() {
  Fake f;
  f.isos = {fs::path("D:/isos/doa4.iso")};
  Flow flow(f.Make());
  flow.ChooseImage();
  flow.Back();
  CHECK(flow.stage() == Stage::kChoose && f.done.empty());
}

// A target the game could not start from (final review I2) is refused before
// anything is extracted; the refusals for unusable folders say what to do.
static void TestUnusablePaths() {
  Fake f;
  const fs::path iso = "D:/isos/doa4.iso", target = "D:/isos/DOA4", plain = "E:/games";
  f.isos = {iso};
  f.unusable.insert(target);
  f.targets[plain] = TargetState::kEmpty;
  f.folders = {plain};
  Flow flow(f.Make());
  flow.ChooseImage();
  CHECK(!flow.CanExtract() && flow.target_note().find("plain") != std::string::npos);
  flow.Extract();
  CHECK(f.extracts == 0);
  flow.ChangeTarget();  // an empty, usable folder
  CHECK(flow.target() == plain && flow.CanExtract());
  CHECK(VerdictMessage(Verdict::kPathUnsupported, false).find("plain") != std::string::npos);
  CHECK(VerdictMessage(Verdict::kIncomplete, false).find("unfinished") != std::string::npos);
  // The not-empty note says what to do with an unfinished earlier attempt.
  CHECK(flow.target_note().empty());
  Fake g;
  g.isos = {iso};
  g.targets[target] = TargetState::kNotEmpty;
  Flow flow2(g.Make());
  flow2.ChooseImage();
  CHECK(flow2.target_note().find("unfinished") != std::string::npos);
}

static void TestFormatGB() {
  CHECK(FormatGB(7 * kGB) == "7.0 GB");
  CHECK(FormatGB(7 * kGB + kGB / 2) == "7.5 GB");
  CHECK(FormatGB(0) == "0.0 GB");
}

static void TestParseTestScript() {
  TestScript t = ParseTestScript(nullptr);
  CHECK(!t.active());
  t = ParseTestScript("");
  CHECK(!t.active());
  t = ParseTestScript("folder=C:/doa4/xex/DOA4");
  CHECK(t.active() && t.folder == fs::path("C:/doa4/xex/DOA4") && !t.iso && !t.target);
  t = ParseTestScript("iso=C:/s/game.iso|target=C:/s/out");
  CHECK(t.active() && !t.folder && t.iso == fs::path("C:/s/game.iso") &&
        t.target == fs::path("C:/s/out"));
  // An image without a target, or an unknown key: not active.
  CHECK(!ParseTestScript("iso=C:/s/game.iso").active());
  CHECK(!ParseTestScript("disk=C:/x").active());
  CHECK(!ParseTestScript("folder=").active());
}

int main() {
  TestImageHappyPath();
  TestImageRefusals();
  TestDialogCancelChangesNothing();
  TestFolderPath();
  TestTargetRules();
  TestChangeTargetPicksOrNests();
  TestFreeSpace();
  TestExtractEndings();
  TestBack();
  TestFormatGB();
  TestUnusablePaths();
  TestParseTestScript();
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("setup_flow: all checks passed\n");
  return 0;
}

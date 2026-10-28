// Host test for src/doa4_photo_watch.cpp. Built and run by run_photo_watch_test.bat.

#include "../../src/doa4_photo_watch.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using namespace doa4::photo;

// A GPU that lags `lag` swaps behind the guest and catches up by one swap per
// sleep; the captured frame is tagged with the GPU's swap count (its width), so
// a test can see WHICH frame was captured.
struct Rig {
  uint32_t guest = 0;     // swaps issued
  uint32_t gpu = 0;       // swaps processed
  uint32_t lag = 3;
  bool stuck = false;     // the GPU never catches up
  bool auto_on = true;
  int sleeps = 0;
  uint64_t clock = 0;
  std::vector<std::pair<uint32_t, std::string>> submitted;  // (frame tag, suffix)
  std::vector<Source> sources;
  std::vector<std::string> logs;

  SnapshotWatcher::Hooks Hooks(bool debug) {
    SnapshotWatcher::Hooks h;
    h.gpu_swaps = [this] { return gpu; };
    h.capture = [this](Image* image) {
      image->width = gpu;  // the frame the GPU last produced
      image->height = 1;
      image->rgbx.assign(size_t(gpu) * 4, 0);
      return true;
    };
    h.auto_enabled = [this] { return auto_on; };
    h.submit = [this](Image image, Source source, std::string suffix) {
      submitted.emplace_back(image.width, suffix);
      sources.push_back(source);
    };
    h.log = [this](const std::string& line) { logs.push_back(line); };
    h.sleep = [this] {
      ++sleeps;
      clock += 1;
      if (!stuck && gpu < guest) ++gpu;
    };
    h.now_ms = [this] { return clock; };
    h.debug = debug;
    return h;
  }
  // One guest swap with the state the game had before it.
  void Swap(SnapshotWatcher& w, uint32_t state) {
    ++guest;
    if (guest > lag && gpu < guest - lag) gpu = guest - lag;  // the GPU runs `lag` behind
    w.OnSwap(state);
  }
};

static void TestCapturesTheFinishedFrame() {
  Rig rig;
  SnapshotWatcher w(rig.Hooks(false));
  for (int i = 0; i < 10; ++i) rig.Swap(w, 0);
  for (int i = 0; i < 30; ++i) rig.Swap(w, 1);  // the 30 blended frames
  rig.Swap(w, 2);  // swap 41: the state turns to 2 (the photo area is still black)
  CHECK(rig.submitted.empty());
  rig.Swap(w, 2);  // swap 42: the finished photo is shown in the photo panel
  rig.Swap(w, 2);
  CHECK(rig.submitted.empty());  // captured and held, not written
  w.OnAlbumSave();               // the player saves it to the in-game Album
  CHECK(rig.submitted.size() == 1);
  if (rig.submitted.size() == 1) {
    // Captured after the GPU reached swap 42, and not later: frame 42 exactly
    // (seen in the user's Watch Mode test: frame n is the black photo area).
    CHECK(rig.submitted[0].first == 42);
    CHECK(rig.submitted[0].second.empty());
    CHECK(rig.sources[0] == Source::kSnapshot);
  }
  CHECK(rig.sleeps == 3);  // held while the GPU caught up the 3 swaps it lagged
  CHECK(w.swaps() == 43);
}

static void TestOffMeansNoHold() {
  Rig rig;
  rig.auto_on = false;
  SnapshotWatcher w(rig.Hooks(false));
  rig.Swap(w, 1);
  rig.Swap(w, 2);
  CHECK(rig.submitted.empty());
  CHECK(rig.sleeps == 0);  // the game is never held when the option is off
}

static void TestTimeout() {
  Rig rig;
  rig.stuck = true;
  SnapshotWatcher w(rig.Hooks(false));
  for (int i = 0; i < 5; ++i) rig.Swap(w, 1);
  rig.Swap(w, 2);
  rig.Swap(w, 2);  // the photo frame: the wait for the GPU happens here
  CHECK(rig.submitted.empty());
  // Held no longer than the limit, and it says so.
  CHECK(rig.clock <= 501);  // the spec's 500 ms, not the code's own constant
  CHECK(rig.clock >= 500);
  bool logged = false;
  for (const auto& line : rig.logs) logged |= line.find("timed out") != std::string::npos;
  CHECK(logged);
}

static void TestDebugFrames() {
  Rig rig;
  rig.auto_on = false;  // debug saves even with the option off
  SnapshotWatcher w(rig.Hooks(true));
  rig.Swap(w, 0);
  rig.Swap(w, 1);
  rig.Swap(w, 1);
  rig.Swap(w, 2);  // swap 4: the photo frame
  rig.Swap(w, 2);  // swap 5: the frame after
  rig.Swap(w, 2);
  CHECK(rig.submitted.size() == 2);
  if (rig.submitted.size() == 2) {
    CHECK(rig.submitted[0].first == 4 && rig.submitted[0].second == "n");
    CHECK(rig.submitted[1].first == 5 && rig.submitted[1].second == "n1");
    CHECK(rig.sources[0] == Source::kDebug && rig.sources[1] == Source::kDebug);
  }
  // Every state change is logged with its swap number.
  int changes = 0;
  for (const auto& line : rig.logs) changes += line.find("state") != std::string::npos;
  CHECK(changes == 2);  // 0 -> 1 at swap 2, 1 -> 2 at swap 4
}

static void TestDebugAndAutoShareOneCapture() {
  Rig rig;
  SnapshotWatcher w(rig.Hooks(true));
  rig.Swap(w, 1);
  rig.Swap(w, 2);
  rig.Swap(w, 2);
  w.OnAlbumSave();
  // The edge frame once (debug "n"); the photo frame twice (debug "n1" and
  // the copy, written at the Album save), from one capture.
  CHECK(rig.submitted.size() == 3);
  if (rig.submitted.size() == 3) {
    CHECK(rig.submitted[0].first == 2 && rig.submitted[0].second == "n");
    CHECK(rig.submitted[1].first == 3 && rig.submitted[2].first == 3);
    int copies = 0;
    for (const Source s : rig.sources) copies += s == Source::kSnapshot;
    CHECK(copies == 1);
  }
}

static void TestDriftLog() {
  Rig rig;
  rig.auto_on = false;
  SnapshotWatcher w(rig.Hooks(false));
  for (uint32_t i = 0; i < SnapshotWatcher::kDriftLogEvery * 2; ++i) rig.Swap(w, 0);
  int lines = 0;
  for (const auto& line : rig.logs) lines += line.find("gpu swaps") != std::string::npos;
  CHECK(lines == 2);
}

// One snapshot taken and saved, one taken and not saved, one more saved: only
// the saved ones are written, each with its own frame, once.
static void TestOnlySavedSnapshotsAreWritten() {
  Rig rig;
  SnapshotWatcher w(rig.Hooks(false));
  auto snapshot = [&] {  // 30 blend frames, the edge, the photo frame, one more
    for (int i = 0; i < 30; ++i) rig.Swap(w, 1);
    rig.Swap(w, 2);
    rig.Swap(w, 2);
    const uint32_t photo_frame = rig.guest;
    rig.Swap(w, 2);
    return photo_frame;
  };
  const uint32_t first = snapshot();
  w.OnAlbumSave();
  CHECK(rig.submitted.size() == 1 && rig.submitted[0].first == first);
  w.OnAlbumSave();  // a second save call without a new snapshot writes nothing
  CHECK(rig.submitted.size() == 1);
  snapshot();       // taken, not saved (the player backs out)
  const uint32_t third = snapshot();
  w.OnAlbumSave();
  CHECK(rig.submitted.size() == 2);
  if (rig.submitted.size() == 2) CHECK(rig.submitted[1].first == third);
}

// A new snapshot drops the held photo, so a failed capture never saves the
// previous snapshot under the new one's save.
static void TestNewSnapshotDropsTheHeldPhoto() {
  Rig rig;
  SnapshotWatcher w(rig.Hooks(false));
  rig.Swap(w, 1);
  rig.Swap(w, 2);
  rig.Swap(w, 2);  // held: the first snapshot's photo
  rig.stuck = true;
  rig.Swap(w, 1);
  rig.Swap(w, 2);  // a second snapshot whose capture will time out
  rig.Swap(w, 2);
  w.OnAlbumSave();
  CHECK(rig.submitted.empty());
}

// With the option off nothing is held, and a save writes nothing.
static void TestOffSavesNothing() {
  Rig rig;
  rig.auto_on = false;
  SnapshotWatcher w(rig.Hooks(false));
  rig.Swap(w, 1);
  rig.Swap(w, 2);
  rig.Swap(w, 2);
  w.OnAlbumSave();
  CHECK(rig.submitted.empty());
}

int main() {
  TestOnlySavedSnapshotsAreWritten();
  TestNewSnapshotDropsTheHeldPhoto();
  TestOffSavesNothing();
  TestCapturesTheFinishedFrame();
  TestOffMeansNoHold();
  TestTimeout();
  TestDebugFrames();
  TestDebugAndAutoShareOneCapture();
  TestDriftLog();
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("photo_watch: all checks passed\n");
  return 0;
}

// Host test for src/doa4_photo_service.cpp (with doa4_photo.cpp and
// doa4_photo_win.cpp). Built and run by run_photo_service_test.bat.

#include "../../src/doa4_photo_service.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>

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

static Image Frame(uint32_t w, uint32_t h, uint8_t shade) {
  Image image{w, h, std::vector<uint8_t>(size_t(w) * h * 4, shade)};
  return image;
}

static void TestPlanOutputs() {
  // A x4-like frame at reduced size: 1280x720 standing in for 5120x2880, and a
  // "monitor" of 640x360 standing in for 2560x1440 (the same 2:1 ratio).
  const Image frame = Frame(1280, 720, 100);
  const Size monitor{640, 360};
  Settings s;

  // Hotkey: always the whole frame, no suffix.
  s.format = Format::kPngMonitor;
  auto out = PlanOutputs(frame, Source::kHotkey, "", s, monitor);
  CHECK(out.size() == 1);
  CHECK(out[0].image.width == 640 && out[0].image.height == 360);
  CHECK(out[0].suffix.empty() && out[0].type == FileType::kPng);
  s.format = Format::kPngFull;
  out = PlanOutputs(frame, Source::kHotkey, "", s, monitor);
  CHECK(out[0].image.width == 1280 && out[0].type == FileType::kPng);
  s.format = Format::kJpgFull;
  out = PlanOutputs(frame, Source::kHotkey, "", s, monitor);
  CHECK(out[0].image.width == 1280 && out[0].type == FileType::kJpg);
  // Snapshot copies: always the game's 3:2 crop (750x500 of 1280x720), then
  // the format.
  s.format = Format::kPngFull;
  out = PlanOutputs(frame, Source::kSnapshot, "", s, monitor);
  CHECK(out.size() == 1 && out[0].suffix == "photo");
  CHECK(out[0].image.width == 750 && out[0].image.height == 500);
  s.format = Format::kPngMonitor;
  out = PlanOutputs(frame, Source::kSnapshot, "", s, monitor);
  CHECK(out[0].image.width == 540 && out[0].image.height == 360);  // 3:2 fitted to 640x360
  s.format = Format::kJpgFull;
  out = PlanOutputs(frame, Source::kSnapshot, "", s, monitor);
  CHECK(out.size() == 1 && out[0].type == FileType::kJpg && out[0].image.width == 750);

  // Debug frames: the whole frame with the given suffix, whatever the framing.
  out = PlanOutputs(frame, Source::kDebug, "n1", s, monitor);
  CHECK(out.size() == 1 && out[0].suffix == "n1" && out[0].image.width == 1280);

  // A frame smaller than the monitor is never upscaled.
  s.format = Format::kPngMonitor;
  out = PlanOutputs(Frame(320, 180, 5), Source::kHotkey, "", s, monitor);
  CHECK(out[0].image.width == 320);
}

struct Harness {
  fs::path dir;
  std::atomic<int> captures{0};
  bool capture_ok = true;
  Settings settings;
  std::mutex mutex;
  std::vector<Result> results;
  uint64_t clock = 1000;  // ms, for the hotkey repeat rule
  int throw_results = 0;  // on_result throws this many times
  Service::Hooks Hooks() {
    Service::Hooks h;
    h.capture = [this](Image* image) {
      ++captures;
      if (!capture_ok) return false;
      *image = Frame(64, 36, 200);
      return true;
    };
    h.monitor = [] { return Size{32, 18}; };
    h.settings = [this] {
      std::lock_guard<std::mutex> lock(mutex);
      return settings;
    };
    h.now = [] {
      std::tm t{};
      t.tm_year = 126; t.tm_mon = 9; t.tm_mday = 28; t.tm_hour = 9; t.tm_min = 5; t.tm_sec = 7;
      return t;
    };
    h.on_result = [this](const Result& r) {
      std::lock_guard<std::mutex> lock(mutex);
      results.push_back(r);
      if (throw_results > 0) {
        --throw_results;
        throw std::runtime_error("a logger failed");
      }
    };
    h.clock_ms = [this] { return clock; };
    return h;
  }
};

static void TestServiceEndToEnd() {
  Harness h;
  h.dir = fs::temp_directory_path() / "doa4_photo_service_test_data";
  fs::remove_all(h.dir);
  h.settings.dir = h.dir / "photos";  // created on first save

  {
    Service service(h.Hooks());
    // Hotkey at monitor size: one PNG, fitted to the 32x18 "monitor".
    service.RequestHotkeyPhoto();
    service.Flush();
    const fs::path first = h.dir / "photos" / "DOA4_2026-10-28_09-05-07.png";
    CHECK(fs::exists(first));
    Image back;
    CHECK(ReadImage(first, &back) && back.width == 32 && back.height == 18);
    CHECK(h.captures == 1);
    CHECK(service.last_result().serial == 1 && !service.last_result().error);
    CHECK(service.last_result().path == first);
    CHECK(service.last_result().text.find("DOA4_2026-10-28_09-05-07.png") != std::string::npos);

    // The settings are read per request: switch to JPG at full size.
    {
      std::lock_guard<std::mutex> lock(h.mutex);
      h.settings.format = Format::kJpgFull;
    }
    h.clock += 1000;  // a new press, not a key repeat
    service.RequestHotkeyPhoto();
    service.Flush();
    const fs::path second = h.dir / "photos" / "DOA4_2026-10-28_09-05-07.jpg";
    CHECK(ReadImage(second, &back) && back.width == 64);

    // Same second, same name: the next file gets _2.
    h.clock += 1000;  // a new press, not a key repeat
    service.RequestHotkeyPhoto();
    service.Flush();
    CHECK(fs::exists(h.dir / "photos" / "DOA4_2026-10-28_09-05-07_2.jpg"));

    // An in-game snapshot copy: the 3:2 photo; the service does not capture
    // again (the frame is handed over).
    {
      std::lock_guard<std::mutex> lock(h.mutex);
      h.settings.format = Format::kPngFull;
    }
    const int before = h.captures;
    service.SubmitCaptured(Frame(1280, 720, 50), Source::kSnapshot);
    service.Flush();
    CHECK(h.captures == before);
    CHECK(ReadImage(h.dir / "photos" / "DOA4_2026-10-28_09-05-07_photo.png", &back) &&
          back.width == 750 && back.height == 500);
    CHECK(!fs::exists(h.dir / "photos" / "DOA4_2026-10-28_09-05-07_full.png"));

    // A failed capture: an error result, no new file.
    h.capture_ok = false;
    const size_t files_before = size_t(std::distance(fs::directory_iterator(h.dir / "photos"),
                                                     fs::directory_iterator{}));
    h.clock += 1000;  // a new press, not a key repeat
    service.RequestHotkeyPhoto();
    service.Flush();
    CHECK(service.last_result().error);
    CHECK(service.last_result().text.find("No frame") != std::string::npos);
    const size_t files_after = size_t(std::distance(fs::directory_iterator(h.dir / "photos"),
                                                    fs::directory_iterator{}));
    CHECK(files_after == files_before);
    CHECK(service.last_result().serial == 5);
  }
  // Every result reached the callback, in order.
  CHECK(h.results.size() == 5);
  for (size_t i = 0; i < h.results.size(); ++i) CHECK(h.results[i].serial == i + 1);

  // An unwritable folder (a file is in the way): an error result.
  {
    std::ofstream(h.dir / "blocked") << "x";
    std::lock_guard<std::mutex> lock(h.mutex);
    h.settings.dir = h.dir / "blocked" / "photos";
  }
  h.capture_ok = true;
  {
    Service service(h.Hooks());
    service.RequestHotkeyPhoto();
    service.Flush();
    CHECK(service.last_result().error);
  }
  fs::remove_all(h.dir);
}

// Holding F12 repeats the key about every 33 ms: one photo per hold, not one
// per repeat. A new press after a short pause is a new photo.
static void TestHotkeyRepeat() {
  Harness h;
  h.dir = fs::temp_directory_path() / "doa4_photo_service_repeat_data";
  fs::remove_all(h.dir);
  h.settings.dir = h.dir;
  {
    Service service(h.Hooks());
    for (int i = 0; i < 30; ++i) {  // a 1 s hold
      service.RequestHotkeyPhoto();
      h.clock += 33;
    }
    service.Flush();
    CHECK(h.captures == 1);
    h.clock += 600;  // released, pressed again
    service.RequestHotkeyPhoto();
    service.Flush();
    CHECK(h.captures == 2);
  }
  fs::remove_all(h.dir);
}

// A failure on the worker (here the result callback throws) is reported and
// the worker keeps going; it never ends the process.
static void TestWorkerSurvivesAThrow() {
  Harness h;
  h.dir = fs::temp_directory_path() / "doa4_photo_service_throw_data";
  fs::remove_all(h.dir);
  h.settings.dir = h.dir;
  h.throw_results = 1;
  {
    Service service(h.Hooks());
    service.RequestHotkeyPhoto();
    service.Flush();
    h.clock += 1000;
    service.RequestHotkeyPhoto();
    service.Flush();
    CHECK(h.captures == 2);
    CHECK(h.results.size() == 2);
  }
  fs::remove_all(h.dir);
}

int main() {
  TestPlanOutputs();
  try {
    TestHotkeyRepeat();
    TestWorkerSurvivesAThrow();
  } catch (const std::exception& e) {
    std::printf("FAIL threw: %s\n", e.what());
    ++g_failures;
  }
  try {
    TestServiceEndToEnd();
  } catch (const std::exception& e) {
    std::printf("FAIL threw: %s\n", e.what());
    ++g_failures;
  }
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("photo_service: all checks passed\n");
  return 0;
}

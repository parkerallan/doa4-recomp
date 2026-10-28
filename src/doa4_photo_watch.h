// Full-quality copies of the game's own snapshots: what runs on the guest
// thread after each guest swap (the sub_8275AEE0 hook in
// doa4_photo_hook.cpp). No SDK headers: the app hands it the GPU's swap count,
// the frame capture and the service, so a host test drives it.
//
// The game's snapshot state turns from 1 to 2 at swap n, when its 30-frame
// photo is finished; swap n still shows the photo area black, and swap n + 1
// shows the finished photo in the game's photo panel (measured with
// DOA4_PHOTO_DEBUG in Watch Mode). At swap n + 1 the guest thread is held
// until the GPU has processed that swap, so the captured frame is that one and
// not the next. The frame is held, not written: it goes to the service only
// when the player saves that snapshot to the game's Album (OnAlbumSave, from
// the hook on the game's photo compressor). A new snapshot drops it. The game
// is already in its slowed snapshot moment, so the short hold is not felt.

#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

#include "doa4_photo.h"
#include "doa4_photo_service.h"

namespace doa4::photo {

class SnapshotWatcher {
 public:
  struct Hooks {
    std::function<uint32_t()> gpu_swaps;  // swaps the GPU thread has processed
    std::function<bool(Image*)> capture;  // the frame the GPU last produced
    std::function<bool()> auto_enabled;   // doa4_photo_auto
    std::function<void(Image, Source, std::string)> submit;  // to the service
    std::function<void(const std::string&)> log;
    std::function<void()> sleep;          // about 1 ms
    std::function<uint64_t()> now_ms;     // a steady clock
    bool debug = false;                   // DOA4_PHOTO_DEBUG=1
  };
  static constexpr uint64_t kWaitLimitMs = 500;
  static constexpr uint32_t kDriftLogEvery = 600;

  explicit SnapshotWatcher(Hooks hooks) : hooks_(std::move(hooks)) {}

  // After guest swap number n (1, 2, ...): `state` is the snapshot state read
  // just before that swap was issued.
  void OnSwap(uint32_t state);

  uint32_t swaps() const { return swaps_; }

  // The game is saving the snapshot to its Album (its photo compressor ran):
  // the held photo goes to the service, once. Any thread.
  void OnAlbumSave();

 private:
  // Holds the calling thread until the GPU has processed swap n (or the wait
  // limit passes), then captures. False on timeout or capture failure.
  bool WaitAndCapture(uint32_t n, Image* image);

  Hooks hooks_;
  SnapshotEdge edge_;
  uint32_t swaps_ = 0;
  uint32_t last_state_ = 0;
  uint32_t photo_swap_ = 0;   // the swap that shows the finished photo (n + 1), or 0
  bool photo_copy_ = false;   // whether that frame is copied (doa4_photo_auto at the edge)
  std::mutex held_mutex_;
  std::optional<Image> held_;  // the last snapshot's photo, until saved or replaced
};

}  // namespace doa4::photo

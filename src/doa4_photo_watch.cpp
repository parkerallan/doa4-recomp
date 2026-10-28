// Full-quality copies of the game's own snapshots. See doa4_photo_watch.h.

#include "doa4_photo_watch.h"

#include <format>
#include <utility>

namespace doa4::photo {

void SnapshotWatcher::OnSwap(uint32_t state) {
  const uint32_t n = ++swaps_;
  if (n % kDriftLogEvery == 0) {
    // The capture waits for "GPU swaps >= n": that only picks the right frame
    // while both counts start together. A gap that grows shows up here.
    hooks_.log(std::format("photo hook: guest swaps {}, gpu swaps {}", n, hooks_.gpu_swaps()));
  }
  if (hooks_.debug && state != last_state_) {
    hooks_.log(std::format("photo hook: snapshot state {} -> {} at guest swap {} (gpu swaps {})",
                           last_state_, state, n, hooks_.gpu_swaps()));
  }
  last_state_ = state;

  // The swap after the edge shows the finished photo in the game's photo
  // panel (the user's Watch Mode test: the edge frame's photo area is black).
  if (photo_swap_ != 0 && n == photo_swap_) {
    photo_swap_ = 0;
    Image frame;
    if (WaitAndCapture(n, &frame)) {
      if (hooks_.debug) hooks_.submit(photo_copy_ ? frame : std::move(frame), Source::kDebug, "n1");
      if (photo_copy_) {
        std::lock_guard<std::mutex> lock(held_mutex_);
        held_ = std::move(frame);
      }
    }
  }

  if (!edge_.OnSwap(state)) return;
  {
    // A new snapshot: the previous one was saved or abandoned.
    std::lock_guard<std::mutex> lock(held_mutex_);
    held_.reset();
  }
  const bool copy = hooks_.auto_enabled();
  if (!copy && !hooks_.debug) return;
  photo_copy_ = copy;
  photo_swap_ = n + 1;
  if (hooks_.debug) {
    Image frame;
    if (WaitAndCapture(n, &frame)) hooks_.submit(std::move(frame), Source::kDebug, "n");
  }
}

void SnapshotWatcher::OnAlbumSave() {
  std::optional<Image> photo;
  {
    std::lock_guard<std::mutex> lock(held_mutex_);
    photo.swap(held_);
  }
  if (hooks_.debug) {
    hooks_.log(std::format("photo hook: Album save at guest swap {} ({})", swaps_,
                           photo ? "copy written" : "no held photo"));
  }
  if (photo) hooks_.submit(std::move(*photo), Source::kSnapshot, "");
}

bool SnapshotWatcher::WaitAndCapture(uint32_t n, Image* image) {
  const uint64_t start = hooks_.now_ms();
  while (hooks_.gpu_swaps() < n) {
    if (hooks_.now_ms() - start > kWaitLimitMs) {
      hooks_.log(std::format("photo hook: timed out waiting for the GPU to reach swap {} "
                             "(gpu swaps {}); no copy of this snapshot",
                             n, hooks_.gpu_swaps()));
      return false;
    }
    hooks_.sleep();
  }
  if (!hooks_.capture(image)) {
    hooks_.log(std::format("photo hook: capture of swap {} failed", n));
    return false;
  }
  return true;
}

}  // namespace doa4::photo

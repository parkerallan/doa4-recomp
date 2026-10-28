// Full-quality copies of in-game snapshots: the guest-swap hook.
//
// sub_8275AEE0 is the game's wrapper around VdSwap (its VdSwap call is
// unconditional; VdSwap itself is a strong SDK export and cannot be
// overridden). The hook reads the game's snapshot state before the swap is
// issued, issues it, and hands the state to the SnapshotWatcher, which acts on
// the swap that presents a finished in-game photo.
//
// sub_823078C8 is the game's photo compressor, called only by the snapshot
// save menu (sub_82535150, its save state) when the player saves the photo to
// the Album; after it runs, the watcher writes the held full-quality copy. See
// notes/research/photo_findings.md.

#include <atomic>
#include <cstdint>

#include <rex/hook.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>

#include "generated/default/dead_or_alive_4_init.h"

#include "doa4_photo.h"
#include "doa4_photo_hook.h"
#include "doa4_photo_watch.h"

namespace {
std::atomic<doa4::photo::SnapshotWatcher*> g_watcher{nullptr};
}  // namespace

namespace doa4::photo {
void InstallSnapshotWatcher(SnapshotWatcher* watcher) {
  g_watcher.store(watcher, std::memory_order_release);
}
}  // namespace doa4::photo

extern "C" {
REX_EXTERN(__imp__sub_8275AEE0);
REX_EXTERN(__imp__sub_823078C8);
}

REX_HOOK_RAW(sub_8275AEE0) {
  const uint32_t state =
      rex::memory::load_and_swap<uint32_t>(base + doa4::photo::kSnapshotStateAddress);
  __imp__sub_8275AEE0(ctx, base);
  if (auto* watcher = g_watcher.load(std::memory_order_acquire)) watcher->OnSwap(state);
}

REX_HOOK_RAW(sub_823078C8) {
  __imp__sub_823078C8(ctx, base);
  if (auto* watcher = g_watcher.load(std::memory_order_acquire)) watcher->OnAlbumSave();
}

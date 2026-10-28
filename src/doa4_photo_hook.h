// The guest-swap hook that feeds the snapshot watcher (doa4_photo_hook.cpp).

#pragma once

namespace doa4::photo {

class SnapshotWatcher;

// The watcher the hook calls after each guest swap; nullptr to stop. The
// watcher must outlive any swap in progress (the app keeps it until exit).
void InstallSnapshotWatcher(SnapshotWatcher* watcher);

}  // namespace doa4::photo

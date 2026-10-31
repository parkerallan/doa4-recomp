// What the game reads from a controller while the Escape menu is open.
//
// The game reads its pads through one wrapper, sub_82792040 (XInputGetState:
// r3 = user index, r4 = guest X_INPUT_STATE*), and its menu keystrokes through
// sub_82792068 (XInputGetKeystroke). doa4_app_hooks.cpp hooks both; while the
// menu is open the state is blanked here, as the SDK's own drivers do while one
// of its overlays has the input (an untouched controller), and keystrokes are
// read and dropped. This lives in the port, not the SDK: the branch builds on
// the unmodified SDK.

#pragma once

#include <array>
#include <cstdint>

namespace doa4::input_block {

class PadBlocker {
 public:
  static constexpr uint32_t kNoSlot = 0xFFFFFFFFu;

  // `state` is the 16-byte guest X_INPUT_STATE (big-endian) the real read just
  // filled: packet number (4 bytes), then the 12-byte gamepad. While `blocked`
  // the gamepad is zeroed. Each change of `blocked` for a slot moves the packet
  // number on by one (and it stays moved), so the game notices the change even
  // when the hardware sent nothing new, and a steady state never churns.
  void Filter(uint32_t user_index, bool blocked, uint8_t* state);

 private:
  struct Slot {
    bool blocked = false;
    uint32_t extra = 0;  // added to the hardware packet number
  };
  std::array<Slot, 4> slots_{};
};

// The controller slot a user index reads (the SDK pins "any user", low byte
// 0xFF, to user 0), or PadBlocker::kNoSlot.
uint32_t SlotFor(uint32_t user_index);

}  // namespace doa4::input_block

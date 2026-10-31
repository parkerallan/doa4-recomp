// What the game reads from a controller while the Escape menu is open. See
// doa4_input_block.h.

#include "doa4_input_block.h"

#include <cstring>

namespace doa4::input_block {

uint32_t SlotFor(uint32_t user_index) {
  if ((user_index & 0xFF) == 0xFF) return 0;
  return user_index < 4 ? user_index : PadBlocker::kNoSlot;
}

void PadBlocker::Filter(uint32_t user_index, bool blocked, uint8_t* state) {
  const uint32_t slot_index = SlotFor(user_index);
  if (slot_index != kNoSlot) {
    Slot& slot = slots_[slot_index];
    if (slot.blocked != blocked) {
      slot.blocked = blocked;
      ++slot.extra;
    }
    if (slot.extra != 0) {
      uint32_t packet = (uint32_t(state[0]) << 24) | (uint32_t(state[1]) << 16) |
                        (uint32_t(state[2]) << 8) | uint32_t(state[3]);
      packet += slot.extra;
      state[0] = uint8_t(packet >> 24);
      state[1] = uint8_t(packet >> 16);
      state[2] = uint8_t(packet >> 8);
      state[3] = uint8_t(packet);
    }
  }
  if (blocked) std::memset(state + 4, 0, 12);
}

}  // namespace doa4::input_block

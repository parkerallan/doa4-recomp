// Host test for src/doa4_input_block.cpp: what the game reads from a
// controller while the Escape menu is open. Built and run by
// run_input_block_test.bat.

#include "../../src/doa4_input_block.h"

#include <array>
#include <cstdint>
#include <cstdio>

static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using doa4::input_block::PadBlocker;
using doa4::input_block::SlotFor;
using State = std::array<uint8_t, 16>;

// A guest X_INPUT_STATE, big-endian: packet number, buttons, LT, RT, LX, LY, RX, RY.
static State MakeState(uint32_t packet, uint16_t buttons, uint8_t lt, uint8_t rt, int16_t lx) {
  State s{};
  s[0] = uint8_t(packet >> 24);
  s[1] = uint8_t(packet >> 16);
  s[2] = uint8_t(packet >> 8);
  s[3] = uint8_t(packet);
  s[4] = uint8_t(buttons >> 8);
  s[5] = uint8_t(buttons);
  s[6] = lt;
  s[7] = rt;
  s[8] = uint8_t(uint16_t(lx) >> 8);
  s[9] = uint8_t(uint16_t(lx));
  for (int i = 10; i < 16; ++i) s[i] = uint8_t(0x30 + i);  // LY, RX, RY: non-zero
  return s;
}

static uint32_t Packet(const State& s) {
  return (uint32_t(s[0]) << 24) | (uint32_t(s[1]) << 16) | (uint32_t(s[2]) << 8) | s[3];
}

static bool GamepadIsZero(const State& s) {
  for (int i = 4; i < 16; ++i) {
    if (s[i] != 0) return false;
  }
  return true;
}

// Never blocked: the state passes through byte for byte.
static void TestUnblockedPassesThrough() {
  PadBlocker blocker;
  for (uint32_t packet : {7u, 7u, 8u, 0xFFFFFFFFu}) {
    State real = MakeState(packet, 0x1010, 200, 3, -1234);
    State seen = real;
    blocker.Filter(0, false, seen.data());
    CHECK(seen == real);
  }
}

// Blocked: an untouched controller (every gamepad byte zero, including the
// sticks and triggers), in a packet the game has not seen yet.
static void TestBlockedIsUntouchedAndNew() {
  PadBlocker blocker;
  State first = MakeState(41, 0x1000, 255, 255, 32767);  // A held, both triggers, stick right
  blocker.Filter(0, false, first.data());
  State seen = MakeState(41, 0x1000, 255, 255, 32767);  // no new hardware packet
  blocker.Filter(0, true, seen.data());
  CHECK(GamepadIsZero(seen));
  CHECK(Packet(seen) != Packet(first));
}

// Blocked with nothing changing: the packet number stays put (no packet churn
// every poll); a real change while blocked still gives a new packet number.
static void TestBlockedPacketStable() {
  PadBlocker blocker;
  State a = MakeState(10, 0x2000, 0, 0, 0);
  blocker.Filter(1, true, a.data());
  State b = MakeState(10, 0x2000, 0, 0, 0);
  blocker.Filter(1, true, b.data());
  CHECK(Packet(a) == Packet(b));
  State c = MakeState(11, 0x4000, 0, 0, 0);
  blocker.Filter(1, true, c.data());
  CHECK(Packet(c) != Packet(b));
  CHECK(GamepadIsZero(c));
}

// Unblocking: the real buttons come back at once (a button held through the
// menu is visible again), in a packet number not seen while blocked.
static void TestUnblockRestoresInANewPacket() {
  PadBlocker blocker;
  State before = MakeState(100, 0x0010, 0, 0, 0);
  blocker.Filter(2, false, before.data());
  State blocked = MakeState(100, 0x0010, 0, 0, 0);
  blocker.Filter(2, true, blocked.data());
  State after = MakeState(100, 0x0010, 0, 0, 0);  // Start still held, no new hardware packet
  blocker.Filter(2, false, after.data());
  CHECK(!GamepadIsZero(after));
  CHECK(after[4] == 0x00 && after[5] == 0x10);
  CHECK(Packet(after) != Packet(blocked));
  CHECK(Packet(after) != Packet(before));
  // Steady again: no churn after the transition.
  State steady = MakeState(100, 0x0010, 0, 0, 0);
  blocker.Filter(2, false, steady.data());
  CHECK(Packet(steady) == Packet(after));
}

// Each controller slot keeps its own transitions.
static void TestSlotsAreIndependent() {
  PadBlocker blocker;
  State p1 = MakeState(5, 0x1000, 0, 0, 0);
  blocker.Filter(0, false, p1.data());
  State p2 = MakeState(5, 0x1000, 0, 0, 0);
  blocker.Filter(1, false, p2.data());
  State p2_blocked = MakeState(5, 0x1000, 0, 0, 0);
  blocker.Filter(1, true, p2_blocked.data());
  State p1_again = MakeState(5, 0x1000, 0, 0, 0);
  blocker.Filter(0, false, p1_again.data());
  CHECK(Packet(p1_again) == Packet(p1));  // slot 0 saw no transition
  CHECK(p1_again == p1);
}

static void TestSlotFor() {
  CHECK(SlotFor(0) == 0);
  CHECK(SlotFor(3) == 3);
  CHECK(SlotFor(0xFF) == 0);        // "any user": the SDK pins it to user 0
  CHECK(SlotFor(0x12345FF) == 0);   // only the low byte names "any user"
  CHECK(SlotFor(4) == PadBlocker::kNoSlot);
  // A state for a slot out of range is still blanked while blocked.
  PadBlocker blocker;
  State s = MakeState(1, 0xFFFF, 9, 9, 9);
  blocker.Filter(9, true, s.data());
  CHECK(GamepadIsZero(s));
}

int main() {
  TestUnblockedPassesThrough();
  TestBlockedIsUntouchedAndNew();
  TestBlockedPacketStable();
  TestUnblockRestoresInANewPacket();
  TestSlotsAreIndependent();
  TestSlotFor();
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("input_block: all checks passed\n");
  return 0;
}

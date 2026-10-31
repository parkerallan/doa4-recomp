// Keeps the bottom of the guest's 4 KB-page virtual heap unused, so no guest
// allocation lands at a low address.
//
// Why: DOA4 loads each fighter's data file into a heap buffer and turns the
// file's offsets into pointers in place (sub_823FB900 and its siblings). Before
// it relocates a table it asks "is this table already relocated?" by comparing
// the table's first entry with the buffer's address: an entry >= the buffer
// address is taken as a pointer and the whole table is skipped. That only works
// while every offset in the file is smaller than the buffer address. The
// runtime hands out 4 KB-page allocations bottom-up from 0x00010000, so the
// game's data pool starts at 0x00010000, and a table whose first entry is a file
// offset of 64 KB or more is never relocated (5 of the 25 anm.afs entries have
// one, in the 1024-entry move-script table). Ryu Hayabusa's move-script table (anm.afs entry 5, offsets up to
// 0x1FFFF) is one: a move whose script lies past file offset 0x10000 runs bytes
// 64 KB too early as its script and the game crashes (guest read of
// 0x42C80000). Brad Wong (first reported as Zack) crashed on the character
// select reading a bad motion record from the same low pool; that crash is gone
// with this reservation too (not traced to its table).
//
// Reserving [kLowGuestFloor0, kLowGuestFloor) before any guest code runs moves
// every bottom-up allocation of that heap to kLowGuestFloor or above, far above
// any offset in the game's files.

#pragma once

#include <cstdint>

namespace rex::memory {
class Memory;
}

namespace doa4 {

// The reserved range: from the end of the runtime's own protected first 64 KB
// up to 256 MB (the 4 KB-page heap is 0x00000000-0x3FFFFFFF; general allocations stop at 0x2FFFFFFF, the
// system heap is above). The game's 4 KB range therefore drops from about 768 MB to 512 MB, the console's
// whole RAM; an out-of-memory from NtAllocateVirtualMemory would point here first. A stray guest pointer into
// the reserved range now faults instead of reading pool data.
inline constexpr uint32_t kLowGuestFloor0 = 0x00010000;
inline constexpr uint32_t kLowGuestFloor = 0x10000000;

// Reserves the range (no access) in the guest heap that holds it. Call once,
// after the runtime is set up and before the first guest thread runs. Logs the
// outcome; returns false when the range could not be reserved (an allocation
// is already inside it), in which case the game runs as before.
bool ReserveLowGuestMemory(rex::memory::Memory* memory);

}  // namespace doa4

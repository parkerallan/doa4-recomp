// Game tweaks for the Extras tab: byte patches to the game's own tables.
//
// The bytes are the Xenia Rev 1 patches by ryu_highabusa ("544307D1 - DEAD OR
// ALIVE 4 (Rev1).patch.toml"). Each tweak is a bool cvar saved to
// dead_or_alive_4.toml; the app applies the ones that are on at
// OnPostLoadXexImage, before any guest code runs, and the menu applies or
// reverts the live ones at once. They change game data, never the save.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "doa4_guest.h"

namespace doa4::tweaks {

struct Patch {
  uint32_t address;
  std::vector<uint8_t> bytes;
};

struct Tweak {
  const char* cvar;
  const char* label;
  std::vector<Patch> patches;
};

inline constexpr size_t kTagAllStages = 0;
inline constexpr size_t kMpHiddenCharacters = 1;
const std::array<Tweak, 2>& AllTweaks();

// Whether the address is in one of the image's READONLY_DATA ranges (from the
// XEX page descriptors: 0x82000000-0x822F0000 and 0x835B0000-0x83600000). The
// SDK maps those pages read-only on the host, so a write needs ProtectFn.
bool IsReadOnly(uint32_t address);

// Makes [address, address + size) writable (true) or read-only again (false).
// Returns false when the protection could not be changed.
using ProtectFn = std::function<bool(uint32_t address, uint32_t size, bool writable)>;

class TweakPatcher {
 public:
  // Records every patch's bytes as they are now. Call once, before Apply.
  void CaptureOriginals(const GuestBytes& guest);
  bool captured() const { return !originals_.empty(); }

  // Writes the tweak's bytes (on) or the captured originals (off). Read-only
  // patches are made writable first and read-only again after; if any cannot
  // be made writable, nothing is written. Returns "" or what went wrong.
  std::string Apply(const GuestBytes& guest, size_t tweak, bool on, const ProtectFn& protect);

  // Whether guest memory holds the tweak's bytes.
  static bool IsOn(const GuestBytes& guest, size_t tweak);

 private:
  // originals_[tweak][patch] = the bytes CaptureOriginals saw.
  std::vector<std::vector<std::vector<uint8_t>>> originals_;
};

}  // namespace doa4::tweaks

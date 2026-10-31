// Game tweaks for the Extras tab. See doa4_tweaks.h.

#include "doa4_tweaks.h"

#include <format>

namespace doa4::tweaks {

namespace {

// Tag battle stage list: 17 x 16 bytes at 0x82C32AF8, byte +1 = "allowed in
// Tag". The patch allows all 17 and puts the default cursor (0x82C32AF7) on
// Random.
std::vector<Patch> TagAllStagesPatches() {
  std::vector<Patch> patches = {{0x82C32AF7, {0xFF}}};
  for (uint32_t k = 0; k < 17; ++k) patches.push_back({0x82C32AF9 + 0x10 * k, {0x01}});
  return patches;
}

// Helena's opera costume, model m3 (files hel03 + helAD: on the disc, used only
// in cutscenes), as her 6th costume. Costume tables (doa4_costume_system.md):
// her model list at 0x82C34758 (05 01 02 00 06, then a spare byte), variants
// per costume at 0x82C34828, max costumes 0x82C34888 and list length
// 0x82C348A4, both indexed by charId 7. The game's lookup reads the list,
// variants and length; the select screen and the unlocks read the max.
// Proven by tables alone in the swimsuit stage-0 probe (swimsuit_stage0.md).
std::vector<Patch> HelenaOperaPatches() {
  return {
      {0x82C3475D, {0x03}},  // list slot 5 = model 3
      {0x82C3482D, {0x01}},  // one variant
      {0x82C3488F, {0x06}},  // max costumes 5 -> 6
      {0x82C348AB, {0x06}},  // list length 5 -> 6
  };
}

}  // namespace

// Alpha-152 (a third patch) was removed on 2026-10-28: her character data
// lacks the motions and script entries a player-controlled fighter needs, and
// selecting her crashed in Survival, Time Attack and Versus. The multiplayer
// hidden-characters patch is not part of this build.
const std::array<Tweak, 2>& AllTweaks() {
  static const std::array<Tweak, 2> tweaks = {{
      {"doa4_tag_all_stages", "All stages in Tag battle", TagAllStagesPatches()},
      {"doa4_helena_opera", "Unlock Helena's opera costume", HelenaOperaPatches()},
  }};
  return tweaks;
}

bool IsReadOnly(uint32_t address) {
  return (address >= 0x82000000 && address < 0x822F0000) ||
         (address >= 0x835B0000 && address < 0x83600000);
}

void TweakPatcher::CaptureOriginals(const GuestBytes& guest) {
  if (captured()) return;  // later calls would capture patched bytes
  for (const Tweak& tweak : AllTweaks()) {
    std::vector<std::vector<uint8_t>> per_patch;
    for (const Patch& patch : tweak.patches) {
      std::vector<uint8_t> bytes(patch.bytes.size());
      for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = guest.Read(patch.address + uint32_t(i));
      per_patch.push_back(std::move(bytes));
    }
    originals_.push_back(std::move(per_patch));
  }
}

std::string TweakPatcher::Apply(const GuestBytes& guest, size_t tweak, bool on,
                                const ProtectFn& protect) {
  if (tweak >= AllTweaks().size()) return std::format("no tweak {}", tweak);
  if (!captured()) return "the original bytes were never recorded";
  const std::vector<Patch>& patches = AllTweaks()[tweak].patches;

  // Make every read-only patch writable first; on any failure undo and write nothing.
  std::vector<const Patch*> unprotected;
  for (const Patch& patch : patches) {
    if (!IsReadOnly(patch.address)) continue;
    if (!protect(patch.address, uint32_t(patch.bytes.size()), true)) {
      for (const Patch* done : unprotected) {
        protect(done->address, uint32_t(done->bytes.size()), false);
      }
      return std::format("could not make {:#010x} writable", patch.address);
    }
    unprotected.push_back(&patch);
  }

  for (size_t p = 0; p < patches.size(); ++p) {
    const std::vector<uint8_t>& bytes = on ? patches[p].bytes : originals_[tweak][p];
    for (size_t i = 0; i < bytes.size(); ++i) {
      guest.Write(patches[p].address + uint32_t(i), bytes[i]);
    }
  }

  std::string error;
  for (const Patch* patch : unprotected) {
    if (!protect(patch->address, uint32_t(patch->bytes.size()), false)) {
      error = std::format("wrote, but could not make {:#010x} read-only again", patch->address);
    }
  }
  return error;
}

bool TweakPatcher::IsOn(const GuestBytes& guest, size_t tweak) {
  if (tweak >= AllTweaks().size()) return false;
  for (const Patch& patch : AllTweaks()[tweak].patches) {
    for (size_t i = 0; i < patch.bytes.size(); ++i) {
      if (guest.Read(patch.address + uint32_t(i)) != patch.bytes[i]) return false;
    }
  }
  return true;
}

}  // namespace doa4::tweaks

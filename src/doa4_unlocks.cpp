// Unlocks for the Extras tab. See doa4_unlocks.h.

#include "doa4_unlocks.h"

namespace doa4::unlocks {

namespace {

uint8_t CharId(const GuestBytes& guest, int index) {
  return guest.Read(kProfileIndexToChar + uint32_t(index));
}

uint8_t MaxCostumes(const GuestBytes& guest, int index) {
  return guest.Read(kMaxCostumes + CharId(guest, index));
}

uint8_t ProfileByte(const GuestBytes& guest, uint32_t profile, uint32_t offset, int index = 0) {
  return guest.Read(profile + offset + uint32_t(index));
}

bool HasStoryMode(const GuestBytes& guest, int index) {
  const uint8_t char_id = CharId(guest, index);
  for (int k = 0; k < kStoryRosterCount; ++k) {
    if (guest.Read(kStoryRoster + kStoryRosterStride * uint32_t(k)) == char_id) return true;
  }
  return false;
}

// Writes `value` where the byte differs; returns whether it wrote.
bool SetByte(const GuestBytes& guest, uint32_t address, uint8_t value) {
  if (guest.Read(address) == value) return false;
  guest.Write(address, value);
  return true;
}

int UnlockCharacters(const GuestBytes& guest, uint32_t profile) {
  int changed = 0;
  for (int i = 0; i < kCharacterCount; ++i) {
    if (i == kAlphaIndex) continue;
    changed += SetByte(guest, profile + kCharacterUnlocked + uint32_t(i), 1);
  }
  return changed;
}

int UnlockCostumes(const GuestBytes& guest, uint32_t profile) {
  int changed = 0;
  for (int i = 0; i < kCharacterCount; ++i) {
    const uint32_t address = profile + kCostumeCount + uint32_t(i);
    const uint8_t count = guest.Read(address);
    const uint8_t max = MaxCostumes(guest, i);
    if (count < max) {
      guest.Write(address, max);
      changed += max - count;
    }
  }
  return changed;
}

int UnlockVoices(const GuestBytes& guest, uint32_t profile) {
  int changed = 0;
  for (int i = 0; i < kCharacterCount; ++i) {
    changed += SetByte(guest, profile + kVoiceUnlocked + uint32_t(i), 1);
  }
  return changed;
}

// One Story clear (at Normal or above) for each Story character without one.
void RecordStoryClears(const GuestBytes& guest, uint32_t profile) {
  for (int i = 0; i < kCharacterCount; ++i) {
    if (!HasStoryMode(guest, i)) continue;
    if (ProfileByte(guest, profile, kStoryClears, i) == 0) {
      guest.Write(profile + kStoryClears + uint32_t(i), 1);
    }
    SetByte(guest, profile + kStoryClearedNormal + uint32_t(i), 1);
  }
  SetByte(guest, profile + kFirstStoryClear, 1);
}

bool AllStoryCleared(const GuestBytes& guest, uint32_t profile) {
  for (int i = 0; i < kCharacterCount; ++i) {
    if (HasStoryMode(guest, i) && ProfileByte(guest, profile, kStoryClears, i) == 0) return false;
  }
  return true;
}

}  // namespace

std::optional<uint32_t> FindProfile(const GuestBytes& guest) {
  const uint32_t profile = guest.ReadBe32(kProfilePointer);
  const uint32_t slot = guest.ReadBe32(kProfileSlot);
  if (slot > 3) return std::nullopt;
  if (profile != kRecordBase + slot * kRecordStride + kRecordToProfile) return std::nullopt;
  // The game fills the record with defaults at boot (costume counts 2) and
  // reads the save over it only after Start; the name is empty until then.
  const uint32_t record = profile - kRecordToProfile;
  if (guest.Read(record + kRecordName) == 0 && guest.Read(record + kRecordName + 1) == 0) {
    return std::nullopt;
  }
  for (int i = 0; i < kCharacterCount; ++i) {
    if (CharId(guest, i) >= kCharIdCount) return std::nullopt;
    const uint8_t count = ProfileByte(guest, profile, kCostumeCount, i);
    if (count < kStartingCostumes || count > MaxCostumes(guest, i)) return std::nullopt;
  }
  return profile;
}

bool IsUnlocked(const GuestBytes& guest, uint32_t profile, Unlock what) {
  switch (what) {
    case Unlock::kCharacters:
      for (int i = 0; i < kCharacterCount; ++i) {
        if (i != kAlphaIndex && ProfileByte(guest, profile, kCharacterUnlocked, i) == 0) {
          return false;
        }
      }
      return true;
    case Unlock::kCostumes:
      for (int i = 0; i < kCharacterCount; ++i) {
        if (ProfileByte(guest, profile, kCostumeCount, i) < MaxCostumes(guest, i)) return false;
      }
      return true;
    case Unlock::kVoices:
      for (int i = 0; i < kCharacterCount; ++i) {
        if (ProfileByte(guest, profile, kVoiceUnlocked, i) == 0) return false;
      }
      return true;
    case Unlock::kStages:
    case Unlock::kEverything:
      return IsUnlocked(guest, profile, Unlock::kCharacters) &&
             IsUnlocked(guest, profile, Unlock::kCostumes) &&
             IsUnlocked(guest, profile, Unlock::kVoices) && AllStoryCleared(guest, profile);
  }
  return false;
}

int OpenStageCount(const GuestBytes& guest, uint32_t profile) {
  // Nassau Station: Spartan-458 unlocked.
  const bool nassau = ProfileByte(guest, profile, kCharacterUnlocked, kSpartanIndex) != 0;
  // Biolab Core: any Story clear.
  bool biolab = false;
  for (int i = 0; i < kCharacterCount; ++i) {
    if (ProfileByte(guest, profile, kStoryClears, i) != 0) biolab = true;
  }
  // Tatami: every costume but Alpha-152's at its maximum, every Story character
  // cleared, every voice but Ein's.
  bool tatami = AllStoryCleared(guest, profile);
  for (int i = 0; i < kCharacterCount && tatami; ++i) {
    if (i != kAlphaIndex &&
        ProfileByte(guest, profile, kCostumeCount, i) < MaxCostumes(guest, i)) {
      tatami = false;
    }
    if (i != kEinIndex && ProfileByte(guest, profile, kVoiceUnlocked, i) == 0) tatami = false;
  }
  return int(nassau) + int(biolab) + int(tatami);
}

UnlockResult ApplyUnlock(const GuestBytes& guest, uint32_t profile, Unlock what) {
  UnlockResult result;
  const int stages_before = OpenStageCount(guest, profile);
  const bool all = what == Unlock::kStages || what == Unlock::kEverything;
  if (all || what == Unlock::kCharacters) result.characters = UnlockCharacters(guest, profile);
  if (all || what == Unlock::kCostumes) result.costumes = UnlockCostumes(guest, profile);
  if (all || what == Unlock::kVoices) result.voices = UnlockVoices(guest, profile);
  if (all) RecordStoryClears(guest, profile);
  result.stages = OpenStageCount(guest, profile) - stages_before;
  return result;
}

}  // namespace doa4::unlocks

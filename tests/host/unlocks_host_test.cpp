// Host test for src/doa4_unlocks.cpp, on the game's real tables and real save
// bytes (doa4_unlock_fixtures.h). Built and run by run_unlocks_test.bat.

#include "../../src/doa4_unlocks.h"

#include <cstdio>
#include <map>

#include "doa4_unlock_fixtures.h"

static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using namespace doa4::unlocks;
namespace fx = doa4_fixtures;

// Guest memory as a map; unset bytes read 0. Counts writes.
struct FakeGuest {
  std::map<uint32_t, uint8_t> bytes;
  int writes = 0;
  doa4::GuestBytes guest{[this](uint32_t a) { return Get(a); },
                         [this](uint32_t a, uint8_t v) {
                           bytes[a] = v;
                           ++writes;
                         }};
  uint8_t Get(uint32_t a) const {
    auto it = bytes.find(a);
    return it == bytes.end() ? 0 : it->second;
  }
  void SetBe32(uint32_t a, uint32_t v) {
    for (int i = 0; i < 4; ++i) bytes[a + i] = uint8_t(v >> (24 - 8 * i));
  }
};

constexpr uint32_t kSlot = 0;
constexpr uint32_t kP = kRecordBase + kSlot * kRecordStride + kRecordToProfile;  // 0x831B0A40
constexpr uint32_t kFixtureStart = 0x3FC0;  // fixtures hold P+0x3FC0..P+0x404B

// The game's tables, the profile pointer and slot, and a profile's bytes at the pointer.
template <size_t N>
static void Setup(FakeGuest& g, const std::array<uint8_t, N>& profile, uint32_t slot = kSlot,
                  uint32_t pointer = kP) {
  for (size_t i = 0; i < fx::kProfileIndexToChar.size(); ++i)
    g.bytes[kProfileIndexToChar + uint32_t(i)] = fx::kProfileIndexToChar[i];
  for (size_t i = 0; i < fx::kMaxCostumes.size(); ++i)
    g.bytes[kMaxCostumes + uint32_t(i)] = fx::kMaxCostumes[i];
  for (size_t k = 0; k < fx::kStoryRosterChars.size(); ++k)
    g.bytes[kStoryRoster + kStoryRosterStride * uint32_t(k)] = fx::kStoryRosterChars[k];
  g.SetBe32(kProfilePointer, pointer);
  g.SetBe32(kProfileSlot, slot);
  // The profile bytes go where the pointer points, so only the pointer checks
  // can reject a bad pointer.
  for (size_t i = 0; i < N; ++i) g.bytes[pointer + kFixtureStart + uint32_t(i)] = profile[i];
  // The profile name, as in a loaded save: "User" in UTF-16BE at record+8.
  const char* name = "User";
  for (uint32_t i = 0; name[i]; ++i) g.bytes[pointer - kRecordToProfile + kRecordName + 2 * i + 1] = uint8_t(name[i]);
  g.writes = 0;
}

static uint8_t At(const FakeGuest& g, uint32_t offset, int index = 0) {
  return g.Get(kP + offset + uint32_t(index));
}
static uint8_t Full(uint32_t offset, int index = 0) {
  return fx::kFullProfile[offset - kFixtureStart + index];
}
static uint8_t Fresh(uint32_t offset, int index = 0) {
  return fx::kFreshProfile[offset - kFixtureStart + index];
}

static void TestFixturesAreWhatTheResearchSays() {
  // Fresh save: every costume count 2; the 7 hidden characters (Helena, Ein,
  // Gen Fu, Leon, Tengu, Alpha-152, Spartan-458) locked.
  for (int i = 0; i < kCharacterCount; ++i) {
    CHECK(Fresh(kCostumeCount, i) == 2);
    const bool hidden = i == 3 || i == 6 || i == 7 || i == 8 || i == 12 || i == 21 || i == 22;
    CHECK(Fresh(kCharacterUnlocked, i) == (hidden ? 0 : 1));
  }
}

static void TestFindProfile() {
  FakeGuest g;
  Setup(g, fx::kFreshProfile);
  CHECK(FindProfile(g.guest) == std::optional<uint32_t>(0x831B0A40));
  CHECK(g.writes == 0);
  // The slot index says 2 but the pointer still names slot 0's record.
  FakeGuest g2;
  Setup(g2, fx::kFreshProfile, 2, kP);
  CHECK(!FindProfile(g2.guest));
  // Null pointer, pointer off the formula, slot index past the last record.
  FakeGuest g3;
  Setup(g3, fx::kFreshProfile, kSlot, 0);
  CHECK(!FindProfile(g3.guest));
  FakeGuest g4;
  Setup(g4, fx::kFreshProfile, kSlot, kP + 4);
  CHECK(!FindProfile(g4.guest));
  FakeGuest g5;
  Setup(g5, fx::kFreshProfile, 4, kRecordBase + 4 * kRecordStride + kRecordToProfile);
  CHECK(!FindProfile(g5.guest));
  // A record the game has not loaded (all zero) is not a profile.
  FakeGuest g6;
  Setup(g6, std::array<uint8_t, 0x8C>{});
  CHECK(!FindProfile(g6.guest));
  // One costume count past its maximum, or one at 1, is not a profile either.
  FakeGuest g7;
  Setup(g7, fx::kFreshProfile);
  g7.bytes[kP + kCostumeCount + kAlphaIndex] = 3;  // Alpha-152's maximum is 2
  CHECK(!FindProfile(g7.guest));
  FakeGuest g8;
  Setup(g8, fx::kFreshProfile);
  g8.bytes[kP + kCostumeCount + kSpartanIndex] = 1;
  CHECK(!FindProfile(g8.guest));
  // The game's default record before the save is read (seen at runtime from
  // 1 s after launch until Start is pressed): fresh costume counts and
  // character flags, but no name. Not a loaded profile.
  FakeGuest g10;
  Setup(g10, fx::kFreshProfile);
  for (uint32_t i = 0; i < 32; ++i) g10.bytes[kP - kRecordToProfile + kRecordName + i] = 0;
  CHECK(!FindProfile(g10.guest));
  // A name is enough: one UTF-16 character.
  g10.bytes[kP - kRecordToProfile + kRecordName + 1] = 'A';
  CHECK(FindProfile(g10.guest).has_value());
  // The 100% save is a profile.
  FakeGuest g9;
  Setup(g9, fx::kFullProfile);
  CHECK(FindProfile(g9.guest).has_value());
}

static void TestEverythingMatchesTheFullSave() {
  FakeGuest g;
  Setup(g, fx::kFreshProfile);
  UnlockResult r = ApplyUnlock(g.guest, kP, Unlock::kEverything);
  for (int i = 0; i < kCharacterCount; ++i) {
    CHECK(At(g, kCostumeCount, i) == Full(kCostumeCount, i));
    CHECK(At(g, kCharacterUnlocked, i) == Full(kCharacterUnlocked, i));
    CHECK(At(g, kVoiceUnlocked, i) == Full(kVoiceUnlocked, i));
    CHECK(At(g, kStoryClearedNormal, i) == Full(kStoryClearedNormal, i));
    // Story clears: 1 where the full save has any, 0 where it has none.
    CHECK(At(g, kStoryClears, i) == (Full(kStoryClears, i) ? 1 : 0));
  }
  CHECK(At(g, kFirstStoryClear) == 1);
  CHECK(r.characters == 6);
  CHECK(r.costumes == 61);  // sum of (max - 2) over the 23 characters
  CHECK(r.voices == 23);
  CHECK(r.stages == 3);
  CHECK(OpenStageCount(g.guest, kP) == 3);
  // Every other profile byte in the fixture range is untouched: Time Attack
  // records (P+0x401E), the bytes between the arrays, play time and the rest.
  for (uint32_t offset = kFixtureStart; offset < kFixtureStart + 0x8C; ++offset) {
    const bool unlock_byte = offset == kFirstStoryClear ||
                             (offset >= kCostumeCount && offset < kCostumeCount + 23) ||
                             (offset >= kCharacterUnlocked && offset < kCharacterUnlocked + 23) ||
                             (offset >= kStoryClears && offset < kStoryClears + 23) ||
                             (offset >= kStoryClearedNormal && offset < kStoryClearedNormal + 23) ||
                             (offset >= kVoiceUnlocked && offset < kVoiceUnlocked + 23);
    if (!unlock_byte) CHECK(At(g, offset) == Fresh(offset));
  }
  // Nothing at all is written outside the profile's unlock range.
  for (const auto& [address, value] : g.bytes) {
    (void)value;
    if (address >= kP + kFixtureStart && address < kP + kFixtureStart + 0x8C) continue;
    FakeGuest pristine;
    Setup(pristine, fx::kFreshProfile);
    CHECK(pristine.bytes.count(address) == 1);
  }
}

// Each of characters / costumes / voices changes only its own array.
static void TestEachButtonOnlyItsOwnBytes() {
  struct Case {
    Unlock what;
    uint32_t own;
  };
  const Case cases[] = {{Unlock::kCharacters, kCharacterUnlocked},
                        {Unlock::kCostumes, kCostumeCount},
                        {Unlock::kVoices, kVoiceUnlocked}};
  const uint32_t arrays[] = {kCostumeCount, kCharacterUnlocked, kStoryClears, kStoryClearedNormal,
                             kVoiceUnlocked};
  for (const Case& c : cases) {
    FakeGuest g;
    Setup(g, fx::kFreshProfile);
    ApplyUnlock(g.guest, kP, c.what);
    CHECK(IsUnlocked(g.guest, kP, c.what));
    for (uint32_t array : arrays) {
      if (array == c.own) continue;
      for (int i = 0; i < kCharacterCount; ++i) CHECK(At(g, array, i) == Fresh(array, i));
    }
    CHECK(At(g, kFirstStoryClear) == Fresh(kFirstStoryClear));
  }
  // Characters leaves Alpha-152 locked; voices include her.
  FakeGuest g;
  Setup(g, fx::kFreshProfile);
  UnlockResult r = ApplyUnlock(g.guest, kP, Unlock::kCharacters);
  CHECK(r.characters == 6 && r.costumes == 0 && r.voices == 0 && r.stages == 1);
  CHECK(At(g, kCharacterUnlocked, kAlphaIndex) == 0);
  CHECK(At(g, kCharacterUnlocked, kSpartanIndex) == 1);
  // Spartan-458 unlocked opens Nassau Station (reported in r.stages above).
  CHECK(OpenStageCount(g.guest, kP) == 1);
  r = ApplyUnlock(g.guest, kP, Unlock::kVoices);
  CHECK(r.voices == 23 && At(g, kVoiceUnlocked, kAlphaIndex) == 1);
}

static void TestStagesDoesWhatItTakes() {
  FakeGuest g;
  Setup(g, fx::kFreshProfile);
  CHECK(OpenStageCount(g.guest, kP) == 0);
  CHECK(!IsUnlocked(g.guest, kP, Unlock::kStages));
  UnlockResult r = ApplyUnlock(g.guest, kP, Unlock::kStages);
  CHECK(r.stages == 3 && r.characters == 6 && r.costumes == 61 && r.voices == 23);
  CHECK(IsUnlocked(g.guest, kP, Unlock::kStages));
  CHECK(IsUnlocked(g.guest, kP, Unlock::kEverything));
  CHECK(IsUnlocked(g.guest, kP, Unlock::kCharacters));
  CHECK(IsUnlocked(g.guest, kP, Unlock::kCostumes));
  CHECK(IsUnlocked(g.guest, kP, Unlock::kVoices));
  // Story clears only for Story characters: Ein, Gen Fu, Leon, Tengu,
  // Alpha-152 and Spartan-458 have no Story mode.
  const int no_story[] = {3, 6, 8, 12, 21, 22};
  for (int i : no_story) {
    CHECK(At(g, kStoryClears, i) == 0);
    CHECK(At(g, kStoryClearedNormal, i) == 0);
  }
  CHECK(At(g, kStoryClears, 7) == 1);  // Helena has Story mode
}

static void TestExistingProgressIsKept() {
  FakeGuest g;
  Setup(g, fx::kFreshProfile);
  g.bytes[kP + kStoryClears + 0] = 7;    // seven Story clears with Zack
  g.bytes[kP + kCostumeCount + 14] = 5;  // Hitomi: 5 of 8 costumes
  UnlockResult r = ApplyUnlock(g.guest, kP, Unlock::kStages);
  CHECK(At(g, kStoryClears, 0) == 7);
  CHECK(At(g, kCostumeCount, 14) == 8);
  CHECK(r.costumes == 58);  // Hitomi had 3 of her 6 extras already
  CHECK(r.stages == 2);     // Biolab Core was open (Zack's clears)
}

static void TestSecondClickChangesNothing() {
  const Unlock all[] = {Unlock::kCharacters, Unlock::kCostumes, Unlock::kVoices, Unlock::kStages,
                        Unlock::kEverything};
  for (Unlock what : all) {
    FakeGuest g;
    Setup(g, fx::kFreshProfile);
    CHECK(!IsUnlocked(g.guest, kP, what));
    ApplyUnlock(g.guest, kP, what);
    CHECK(IsUnlocked(g.guest, kP, what));
    g.writes = 0;
    UnlockResult r = ApplyUnlock(g.guest, kP, what);
    CHECK(r.characters == 0 && r.costumes == 0 && r.voices == 0 && r.stages == 0);
    CHECK(g.writes == 0);
  }
  // The 100% save is fully unlocked already.
  FakeGuest g;
  Setup(g, fx::kFullProfile);
  for (Unlock what : all) CHECK(IsUnlocked(g.guest, kP, what));
  CHECK(OpenStageCount(g.guest, kP) == 3);
}

// Tatami: every voice but Ein's, every costume but Alpha-152's.
static void TestTatamiRule() {
  FakeGuest g;
  Setup(g, fx::kFullProfile);
  g.bytes[kP + kVoiceUnlocked + kEinIndex] = 0;
  CHECK(OpenStageCount(g.guest, kP) == 3);
  g.bytes[kP + kVoiceUnlocked + 0] = 0;
  CHECK(OpenStageCount(g.guest, kP) == 2);
  FakeGuest g2;
  Setup(g2, fx::kFullProfile);
  g2.bytes[kP + kCostumeCount + kAlphaIndex] = 1;  // Alpha's count does not matter
  CHECK(OpenStageCount(g2.guest, kP) == 3);
  g2.bytes[kP + kCostumeCount + 0] = 3;  // Zack one short of 4
  CHECK(OpenStageCount(g2.guest, kP) == 2);
  FakeGuest g3;
  Setup(g3, fx::kFullProfile);
  g3.bytes[kP + kStoryClears + 7] = 0;  // Helena's Story never cleared
  CHECK(OpenStageCount(g3.guest, kP) == 2);
}

int main() {
  TestFixturesAreWhatTheResearchSays();
  TestFindProfile();
  TestEverythingMatchesTheFullSave();
  TestEachButtonOnlyItsOwnBytes();
  TestStagesDoesWhatItTakes();
  TestExistingProgressIsKept();
  TestSecondClickChangesNothing();
  TestTatamiRule();
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("unlocks: all checks passed\n");
  return 0;
}

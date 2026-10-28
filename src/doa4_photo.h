// Full-quality photos: the pure parts (no SDK, no Windows), host-tested.
//
// Image maths (the game's photo crop, fitting to the monitor, area-average
// resize), file names, the settings' choice tables, and the edge detector for
// the game's own snapshot. See notes/specs/2026-10-28-photo-mode-design.md.

#pragma once

#include <array>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace doa4::photo {

// R G B X bytes, stride 4 * width (the layout of rex::ui::RawImage).
struct Image {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgbx;
};

struct Size {
  uint32_t width = 0;
  uint32_t height = 0;
  bool operator==(const Size&) const = default;
};

struct Rect {
  uint32_t x = 0, y = 0, width = 0, height = 0;
  bool operator==(const Rect&) const = default;
};

// The game frames its photos as the centred 750x500 of its 1280x720 frame
// (3:2); this is that rectangle scaled to a w x h frame.
Rect GameCropRect(uint32_t w, uint32_t h);

Image Crop(const Image& image, Rect rect);

// The largest size with `image`'s aspect ratio that fits inside `box`, never
// larger than `image` (no upscaling).
Size FitWithin(Size image, Size box);

// Area-average resize: each output pixel is the mean of the source area it
// covers (fractional edges weighted). Exact for integer ratios.
Image Resize(const Image& image, Size size);

// DOA4_YYYY-MM-DD_HH-MM-SS[_suffix].ext
std::string PhotoFileName(const std::tm& local_time, std::string_view suffix,
                          std::string_view ext);

// dir/name, or dir/<stem>_2.<ext>, _3, ... when the name is taken.
std::filesystem::path UniquePath(const std::filesystem::path& dir, const std::string& name);

// Settings: what each choice shows and the cvar value it saves.
struct Choice {
  const char* label;
  const char* value;
};
enum class Format { kPngMonitor, kPngFull, kJpgFull };
inline constexpr std::array<Choice, 3> kFormats = {{
    {"PNG, monitor size", "png_monitor"},
    {"PNG, full render size", "png_full"},
    {"JPG, full render size", "jpg_full"},
}};
// The row for a saved value; row 0 (the default) when unknown.
int ChoiceIndex(const std::array<Choice, 3>& choices, std::string_view value);

// The game's snapshot capture state (be32 @ 0x831013D8): 1 while it blends
// its 30 frames, 2 once the photo is finished. Fed the value read at each guest
// swap, returns true at the swap where it turns from 1 to 2: the frame that
// shows the finished photo.
class SnapshotEdge {
 public:
  bool OnSwap(uint32_t state) {
    const bool edge = prev_ == 1 && state == 2;
    prev_ = state;
    return edge;
  }

 private:
  uint32_t prev_ = 0;
};

inline constexpr uint32_t kSnapshotStateAddress = 0x831013D8;

}  // namespace doa4::photo

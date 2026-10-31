// The Video tab's graphics choices: what each row shows and the cvar value it
// saves. No SDK headers, so a host test can check the values against the
// SDK's accepted ones.

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace doa4::graphics {

struct Choice {
  const char* label;
  const char* value;  // as saved to dead_or_alive_4.toml
};

// swap_post_effect (rexglue-sdk src/graphics/graphics_system.cpp:44-46).
inline constexpr std::array<Choice, 3> kAntiAliasing = {{
    {"Off", "none"},
    {"FXAA", "fxaa"},
    {"FXAA Extreme", "fxaa_extreme"},
}};

// anisotropic_override (src/graphics/pipeline/texture/cache.cpp:52-64): the
// SDK numbers it by enum, not sample count (3 = 4x, 5 = 16x).
inline constexpr std::array<Choice, 5> kTextureFiltering = {{
    {"Off", "0"},
    {"2x", "2"},
    {"4x", "3"},
    {"8x", "4"},
    {"16x", "5"},
}};

// resolution_scale 1-4, read once at start.
inline constexpr std::array<const char*, 4> kRenderScales = {
    "x1 (1280x720)", "x2 (2560x1440)", "x3 (3840x2160)", "x4 (5120x2880, supersampling)"};

// The kAntiAliasing row for a swap_post_effect value; 0 (Off) when unknown.
int AntiAliasingIndex(std::string_view value);

// The kTextureFiltering row for an anisotropic_override value, or -1 when the
// menu does not offer it (-1 "no override", 1 "1x": only a hand-edited toml).
int TextureFilteringIndex(int32_t value);

}  // namespace doa4::graphics

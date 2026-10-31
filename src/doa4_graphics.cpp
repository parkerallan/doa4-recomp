// The Video tab's graphics choices. See doa4_graphics.h.

#include "doa4_graphics.h"

#include <string>

namespace doa4::graphics {

int AntiAliasingIndex(std::string_view value) {
  for (size_t i = 0; i < kAntiAliasing.size(); ++i) {
    if (value == kAntiAliasing[i].value) return int(i);
  }
  return 0;
}

int TextureFilteringIndex(int32_t value) {
  const std::string text = std::to_string(value);
  for (size_t i = 0; i < kTextureFiltering.size(); ++i) {
    if (text == kTextureFiltering[i].value) return int(i);
  }
  return -1;
}

}  // namespace doa4::graphics

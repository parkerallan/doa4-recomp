// Host test for src/doa4_graphics.cpp (the Video tab's graphics choices).
// Built and run by run_graphics_test.bat.

#include "../../src/doa4_graphics.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using namespace doa4::graphics;

// The SDK's accepted values, copied from rexglue-sdk:
// src/graphics/graphics_system.cpp:44-45  .allowed({"none", "fxaa", "fxaa_extreme"})
// src/graphics/pipeline/texture/cache.cpp:52-63  anisotropic_override .range(-1, 5),
//   " 0 = Disable", " 2 = Force 2x", " 3 = Force 4x", " 4 = Force 8x", " 5 = Force 16x"
static bool SdkAllowsPostEffect(std::string_view v) {
  return v == "none" || v == "fxaa" || v == "fxaa_extreme";
}

static void TestAntiAliasing() {
  CHECK(kAntiAliasing.size() == 3);
  for (size_t i = 0; i < kAntiAliasing.size(); ++i) {
    CHECK(SdkAllowsPostEffect(kAntiAliasing[i].value));
    CHECK(AntiAliasingIndex(kAntiAliasing[i].value) == int(i));
  }
  CHECK(std::string(kAntiAliasing[0].label) == "Off");
  CHECK(std::string(kAntiAliasing[0].value) == "none");
  CHECK(std::string(kAntiAliasing[2].value) == "fxaa_extreme");
  // Unknown or empty values show as Off (the SDK rejects them at load anyway).
  CHECK(AntiAliasingIndex("smaa") == 0);
  CHECK(AntiAliasingIndex("") == 0);
}

static void TestTextureFiltering() {
  CHECK(kTextureFiltering.size() == 5);
  for (size_t i = 0; i < kTextureFiltering.size(); ++i) {
    const int value = std::atoi(kTextureFiltering[i].value);
    CHECK(value >= -1 && value <= 5);
    CHECK(TextureFilteringIndex(value) == int(i));
  }
  // The SDK numbers anisotropy by its enum, not by the sample count: 4x is 3.
  CHECK(std::string(kTextureFiltering[2].label) == "4x");
  CHECK(std::string(kTextureFiltering[2].value) == "3");
  CHECK(std::string(kTextureFiltering[4].label) == "16x");
  CHECK(std::string(kTextureFiltering[4].value) == "5");
  CHECK(std::string(kTextureFiltering[0].value) == "0");
  // The SDK default (3) is a row; -1 (no override) and 1 (1x) are not offered.
  CHECK(TextureFilteringIndex(3) == 2);
  CHECK(TextureFilteringIndex(-1) == -1);
  CHECK(TextureFilteringIndex(1) == -1);
  CHECK(TextureFilteringIndex(6) == -1);
}

static void TestRenderScales() {
  CHECK(kRenderScales.size() == 4);
  CHECK(std::string(kRenderScales[1]).find("2560x1440") != std::string::npos);
  CHECK(std::string(kRenderScales[3]).find("5120x2880") != std::string::npos);
}

int main() {
  TestAntiAliasing();
  TestTextureFiltering();
  TestRenderScales();
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("graphics: all checks passed\n");
  return 0;
}

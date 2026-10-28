// Full-quality photos: the pure parts. See doa4_photo.h.

#include "doa4_photo.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <system_error>

namespace doa4::photo {

namespace {

// round(v * num / den) in integers.
uint32_t Scale(uint32_t v, uint32_t num, uint32_t den) {
  return uint32_t((uint64_t(v) * num + den / 2) / den);
}

// For each output index along one axis, the source indices it covers and
// their weights (the overlap of [o*s/d, (o+1)*s/d) with each source cell).
struct Tap {
  uint32_t index;
  double weight;
};
std::vector<std::vector<Tap>> AxisTaps(uint32_t source, uint32_t dest) {
  std::vector<std::vector<Tap>> taps(dest);
  const double step = double(source) / double(dest);
  for (uint32_t o = 0; o < dest; ++o) {
    const double begin = o * step;
    const double end = (o + 1) * step;
    for (uint32_t s = uint32_t(begin); s < source && s < end; ++s) {
      const double overlap = std::min(end, double(s + 1)) - std::max(begin, double(s));
      if (overlap > 1e-12) taps[o].push_back({s, overlap / step});
    }
  }
  return taps;
}

}  // namespace

Rect GameCropRect(uint32_t w, uint32_t h) {
  return Rect{Scale(w, 265, 1280), Scale(h, 110, 720), Scale(w, 750, 1280), Scale(h, 500, 720)};
}

Image Crop(const Image& image, Rect rect) {
  rect.x = std::min(rect.x, image.width);
  rect.y = std::min(rect.y, image.height);
  rect.width = std::min(rect.width, image.width - rect.x);
  rect.height = std::min(rect.height, image.height - rect.y);
  Image out{rect.width, rect.height, std::vector<uint8_t>(size_t(rect.width) * rect.height * 4)};
  for (uint32_t y = 0; y < rect.height; ++y) {
    const uint8_t* src = &image.rgbx[(size_t(rect.y + y) * image.width + rect.x) * 4];
    std::copy(src, src + size_t(rect.width) * 4, &out.rgbx[size_t(y) * rect.width * 4]);
  }
  return out;
}

Size FitWithin(Size image, Size box) {
  if (image.width == 0 || image.height == 0) return image;
  if (image.width <= box.width && image.height <= box.height) return image;
  const double scale =
      std::min(double(box.width) / image.width, double(box.height) / image.height);
  Size out{uint32_t(std::lround(image.width * scale)), uint32_t(std::lround(image.height * scale))};
  out.width = std::clamp<uint32_t>(out.width, 1, box.width);
  out.height = std::clamp<uint32_t>(out.height, 1, box.height);
  return out;
}

Image Resize(const Image& image, Size size) {
  if (size.width == image.width && size.height == image.height) return image;
  const auto x_taps = AxisTaps(image.width, size.width);
  const auto y_taps = AxisTaps(image.height, size.height);
  Image out{size.width, size.height, std::vector<uint8_t>(size_t(size.width) * size.height * 4)};
  std::vector<double> row(size_t(size.width) * 3);
  std::vector<double> acc(size_t(size.width) * 3);
  for (uint32_t oy = 0; oy < size.height; ++oy) {
    std::fill(acc.begin(), acc.end(), 0.0);
    for (const Tap& ty : y_taps[oy]) {
      const uint8_t* src = &image.rgbx[size_t(ty.index) * image.width * 4];
      for (uint32_t ox = 0; ox < size.width; ++ox) {
        double r = 0, g = 0, b = 0;
        for (const Tap& tx : x_taps[ox]) {
          const uint8_t* p = src + size_t(tx.index) * 4;
          r += p[0] * tx.weight;
          g += p[1] * tx.weight;
          b += p[2] * tx.weight;
        }
        acc[ox * 3 + 0] += r * ty.weight;
        acc[ox * 3 + 1] += g * ty.weight;
        acc[ox * 3 + 2] += b * ty.weight;
      }
    }
    uint8_t* dst = &out.rgbx[size_t(oy) * size.width * 4];
    for (uint32_t ox = 0; ox < size.width; ++ox) {
      for (int c = 0; c < 3; ++c) {
        dst[ox * 4 + c] = uint8_t(std::clamp(std::floor(acc[ox * 3 + c] + 0.5), 0.0, 255.0));
      }
      dst[ox * 4 + 3] = 255;
    }
  }
  return out;
}

std::string PhotoFileName(const std::tm& t, std::string_view suffix, std::string_view ext) {
  std::string name = std::format("DOA4_{:04}-{:02}-{:02}_{:02}-{:02}-{:02}", t.tm_year + 1900,
                                 t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  if (!suffix.empty()) name += "_" + std::string(suffix);
  return name + "." + std::string(ext);
}

std::filesystem::path UniquePath(const std::filesystem::path& dir, const std::string& name) {
  std::error_code ec;
  std::filesystem::path path = dir / name;
  if (!std::filesystem::exists(path, ec)) return path;
  const std::filesystem::path stem = path.stem();
  const std::filesystem::path ext = path.extension();
  for (int n = 2;; ++n) {
    std::filesystem::path next = dir / (stem.string() + "_" + std::to_string(n) + ext.string());
    if (!std::filesystem::exists(next, ec)) return next;
  }
}

int ChoiceIndex(const std::array<Choice, 3>& choices, std::string_view value) {
  for (size_t i = 0; i < choices.size(); ++i) {
    if (value == choices[i].value) return int(i);
  }
  return 0;
}

}  // namespace doa4::photo

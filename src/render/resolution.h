#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace earthsiege {

inline constexpr unsigned screen_width{320}, screen_height{200};

struct rectangle {
  int left{0}, top{0}, right{0}, bottom{0};
  bool contains(int x, int y) const;
  rectangle inset(int margin) const {
    return {left + margin, top + margin, right - margin, bottom - margin};
  }
  int width() const { return right - left + 1; }
  int height() const { return bottom - top + 1; }
  rectangle intersect(rectangle other) const {
    return {std::max(left, other.left), std::max(top, other.top),
            std::min(right, other.right), std::min(bottom, other.bottom)};
  }
};

struct render_size {
  int width{320}, height{200};
  bool operator==(render_size const &) const = default;
  size_t count() const { return static_cast<size_t>(width) * height; }
  rectangle bounds() const { return {0, 0, width - 1, height - 1}; }
  // First output pixel belonging to a logical cell under nearest-neighbour sampling.
  static int edge(int coordinate, int extent, int logical) {
    auto const n{static_cast<int64_t>(coordinate) * extent};
    return static_cast<int>((n + (n > 0 ? logical - 1 : 0)) / logical);
  }
  rectangle from_logical(rectangle r) const {
    return {edge(r.left, width, 320), edge(r.top, height, 200),
            edge(r.right + 1, width, 320) - 1, edge(r.bottom + 1, height, 200) - 1};
  }
  // Largest centred rectangle with the requested proportions, including fractional scales.
  rectangle fit(render_size source) const {
    auto const w{static_cast<int>(std::clamp(static_cast<int64_t>(height) * source.width / source.height, int64_t{1}, int64_t{width}))};
    auto const h{static_cast<int>(std::clamp(static_cast<int64_t>(width) * source.height / source.width, int64_t{1}, int64_t{height}))};
    int const x{(width - w) / 2}, y{(height - h) / 2};
    return {x, y, x + w - 1, y + h - 1};
  }
  render_size artwork_size() const {
    auto const area{fit({})};
    return {area.width(), area.height()};
  }
};
}

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include "render/resolution.h"

namespace earthsiege {

/// One transform for presentation, mouse input and cursor warps, including HiDPI.
struct viewport {
  int x{0};
  int y{0};
  int width{1};
  int height{1};

  static auto fit(int width, int height, render_size canvas = {})->viewport {
    render_size const drawable{std::max(1, width), std::max(1, height)};
    auto const outer{drawable.fit(canvas)};
    auto const inner{canvas.fit({})};
    auto const x{render_size::edge(inner.left, outer.width(), canvas.width)};
    auto const y{render_size::edge(inner.top, outer.height(), canvas.height)};
    auto const right{render_size::edge(inner.right + 1, outer.width(), canvas.width)};
    auto const bottom{render_size::edge(inner.bottom + 1, outer.height(), canvas.height)};
    return {outer.left + x, outer.top + y, std::max(1, right - x), std::max(1, bottom - y)};
  }

  auto logical(double const px, double const py, int const window_width, int const window_height,
               int const buffer_width, int const buffer_height) const->std::array<int, 2> {
    if(window_width <= 0 || window_height <= 0) return {-1, -1};
    return {static_cast<int>(std::floor((px * buffer_width / window_width - x) * 320 / width)),
            static_cast<int>(std::floor((py * buffer_height / window_height - y) * 200 / height))};
  }

  auto cursor(int const px, int const py, int const window_width, int const window_height,
              int const buffer_width, int const buffer_height) const->std::array<double, 2> {
    return {(x + (px + 0.5) * width / 320) * window_width / std::max(1.0, double(buffer_width)),
            (y + (py + 0.5) * height / 200) * window_height / std::max(1.0, double(buffer_height))};
  }
};

} // namespace earthsiege

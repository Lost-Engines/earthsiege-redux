#include "framebuffer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include "resources/artwork.h"

namespace earthsiege {

framebuffer::framebuffer(render_size size) : extent{size}, raster(size.count()) {}
framebuffer::framebuffer(render_size size, std::span<uint8_t const> pixels)
  : extent{size}, raster(pixels.begin(), pixels.end()) {
  if(size.count() != raster.size()) throw std::invalid_argument{"framebuffer dimensions differ from pixel count"};
}

void framebuffer::attach(std::shared_ptr<framebuffer const> image, rectangle area, screen_mask const *window) {
  detail = std::move(image);
  detail_area = area;
  visible.assign(raster.size(), window ? 0 : 1);
  for(int y{0}; y < extent.height; ++y) {
    auto const sy{area.top + (2 * y + 1) * int64_t{area.height()} / (2 * extent.height)};
    for(int x{0}; x < extent.width; ++x) {
      if(window && !window->contains(x, y)) continue;
      auto const i{static_cast<size_t>(y) * extent.width + x};
      int const sx{static_cast<int>(area.left + (2 * x + 1) * int64_t{area.width()} / (2 * extent.width))};
      visible[i] = 1;
      raster[i] = detail->raster[static_cast<size_t>(sy) * detail->extent.width + sx];
    }
  }
}

void framebuffer::restore_world(framebuffer const &source, screen_mask const &window) {
  detail = source.detail;
  detail_area = source.detail_area;
  if(detail) visible.assign(raster.size(), 0);
  for(int y{0}; y < extent.height; ++y) {
    for(auto const span : window.rows[y]) {
      auto const left{std::max(0, span.left)};
      auto const right{std::min(extent.width - 1, span.right)};
      if(left > right) continue;
      auto const first{static_cast<size_t>(y) * extent.width + left};
      auto const count{right - left + 1};
      std::copy_n(source.raster.begin() + first, count, raster.begin() + first);
      if(detail) std::copy_n(source.visible.begin() + first, count, visible.begin() + first);
    }
  }
}

void framebuffer::blit_to(framebuffer &dest, rectangle area) const {
  int const left{std::max(0, area.left)}, top{std::max(0, area.top)};
  int const right{std::min(dest.extent.width - 1, area.right)}, bottom{std::min(dest.extent.height - 1, area.bottom)};
  if(right < left || bottom < top) return;
  std::vector<int> xs(right - left + 1), detail_x(detail ? xs.size() : 0);
  for(int x{left}; x <= right; ++x) {
    xs[x - left] = static_cast<int>(int64_t{x - area.left} * extent.width / area.width());
    if(detail) detail_x[x - left] = detail_area.left + static_cast<int>(int64_t{x - area.left} * detail_area.width() / area.width());
  }
  for(int y{top}; y <= bottom; ++y) {
    auto const row{int64_t{y - area.top} * extent.height / area.height() * extent.width};
    auto *dst{dest.raster.data() + static_cast<size_t>(y) * dest.extent.width + left};
    if(detail) {
      auto const sy{detail_area.top + int64_t{y - area.top} * detail_area.height() / area.height()};
      auto const *hi{detail->raster.data() + sy * detail->extent.width};
      for(size_t i{0}; i < xs.size(); ++i) dst[i] = visible[row + xs[i]] ? hi[detail_x[i]] : raster[row + xs[i]];
    } else {
      for(size_t i{0}; i < xs.size(); ++i) dst[i] = raster[row + xs[i]];
    }
  }
}

framebuffer framebuffer::resampled(render_size size) const {
  if(size == extent && !detail) return *this;
  framebuffer result{size};
  blit_to(result, size.bounds());
  return result;
}

void framebuffer::copy_rows(int source_x, int target_x, rectangle area) {
  int const left{std::max({area.left, -source_x, -target_x})};
  int const right{std::min({area.right, extent.width - 1 - source_x, extent.width - 1 - target_x})};
  if(right < left) return;
  for(int y{std::max(area.top, 0)}; y <= std::min(area.bottom, extent.height - 1); ++y) {
    auto *row{raster.data() + static_cast<size_t>(y) * extent.width + left};
    std::memmove(row + target_x, row + source_x, right - left + 1);
  }
}

auto rectangle::contains(int x, int y) const->bool {
  return x >= left && x <= right && y >= top && y <= bottom;
}

bool screen_region::contains(int x, int y) const {
  int const row{y - first_row};
  if(row < 0 || static_cast<size_t>(row) >= spans.size()) return false;
  auto const span{spans[static_cast<size_t>(row)]};
  return x >= span.left && x <= span.right;
}

void screen_mask::translate(int x, int y) {
  for(auto &row : rows) row.clear();
  for(auto &region : regions) {
    region.first_row += y;
    for(size_t i{0}; i < region.spans.size(); ++i) {
      auto &span{region.spans[i]};
      span.left += x;
      span.right += x;
      int const row{region.first_row + static_cast<int>(i)};
      if(row < 0 || row >= static_cast<int>(screen_height)) continue;
      scanline_span const visible{std::max(0, span.left), std::min(static_cast<int>(screen_width) - 1, span.right)};
      if(visible.left <= visible.right) rows[static_cast<size_t>(row)].push_back(visible);
    }
  }
}

bool screen_mask::contains(int x, int y) const {
  if(y < 0 || y >= static_cast<int>(rows.size())) return false;
  return std::ranges::any_of(rows[static_cast<size_t>(y)], [x](scanline_span span){return x >= span.left && x <= span.right;});
}

screen_mask const *framebuffer::clip() const {
  return mask;
}
void framebuffer::clip(screen_mask const *value) {
  mask = value;
  // A detailed world keeps the cockpit's scan direction. Its logical spans
  // are applied when attaching it to the cockpit page.
  pixel_mask = extent == render_size{} ? value : nullptr;
}
scoped_framebuffer_clip::scoped_framebuffer_clip(framebuffer &buffer, screen_mask const *value)
  : target{buffer},
    previous{buffer.clip()} {
  target.clip(value);
}
scoped_framebuffer_clip::~scoped_framebuffer_clip() {
  target.clip(previous);
}

void framebuffer::clear(uint8_t colour) {
  detail.reset();
  std::ranges::fill(raster, colour);
}

void framebuffer::pixel(int x, int y, uint8_t colour) {
  if(x >= 0 && y >= 0 && x < extent.width && y < extent.height && (!pixel_mask || pixel_mask->contains(x, y))) {
    auto const i{static_cast<size_t>(y) * extent.width + x};
    raster[i] = colour;
    if(detail) visible[i] = 0;
  }
}

void framebuffer::fill(rectangle area, uint8_t colour) {
  area.left = std::max(0, area.left);
  area.top = std::max(0, area.top);
  area.right = std::min(extent.width - 1, area.right);
  area.bottom = std::min(extent.height - 1, area.bottom);
  if(area.left > area.right || area.top > area.bottom) return;
  for(int y{area.top}; y <= area.bottom; ++y) {
    auto const span_fill{[&](int left, int right){
      if(left > right) return;
      auto const start{static_cast<size_t>(y) * extent.width + left};
      std::fill_n(raster.begin() + start, right - left + 1, colour);
      if(detail) std::fill_n(visible.begin() + start, right - left + 1, 0);
    }};
    if(pixel_mask) {
      for(auto const span : pixel_mask->rows[static_cast<size_t>(y)]) span_fill(std::max(area.left, span.left), std::min(area.right, span.right));
    } else {
      span_fill(area.left, area.right);
    }
  }
}

void framebuffer::line(int x0, int y0, int x1, int y1, uint8_t colour) {
  /// Rasterise inclusive line endpoints in native screen coordinates.
  int const dx{std::abs(x1 - x0)};
  int const dy{-std::abs(y1 - y0)};
  int const sx{x0 < x1 ? 1 : -1};
  int const sy{y0 < y1 ? 1 : -1};
  int error{dx + dy};
  for(;;) {
    pixel(x0, y0, colour);
    if(x0 == x1 && y0 == y1) break;
    int const twice{error * 2};
    if(twice >= dy) {
      error += dy;
      x0 += sx;
    }
    if(twice <= dx) {
      error += dx;
      y0 += sy;
    }
  }
}

void framebuffer::image(bitmap const &source, int x, int y, bool transparent) {
  /// Blit original palette indices; zero is the transparent sprite colour.
  for(unsigned int row{0}; row < source.height; ++row) {
    for(unsigned int column{0}; column < source.width; ++column) {
      auto const colour{source.pixels[row * source.width + column]};
      if(!transparent || colour) pixel(x + static_cast<int>(column), y + static_cast<int>(row), colour);
    }
  }
}

void framebuffer::text(font const &face, std::string_view value, int x, int y) {
  for(unsigned char const character : value) {
    if(character < face.first_character || character - face.first_character >= face.glyphs.size()) continue;
    auto const &letter{face.glyphs[character - face.first_character]};
    for(unsigned int row{0}; row < face.height; ++row) {
      for(unsigned int column{0}; column < letter.width; ++column) {
        auto const colour{letter.pixels[row * letter.width + column]};
        if(colour) pixel(x + static_cast<int>(column), y + static_cast<int>(row) - face.baseline, colour);
      }
    }
    x += static_cast<int>(letter.width);
  }
}

void framebuffer::save(std::filesystem::path const &path, palette const &colours, render_size canvas) const {
  auto const area{canvas.fit({})};
  auto const image{resampled({area.width(), area.height()})};
  std::ofstream file{path, std::ios::binary};
  file << "P6\n" << canvas.width << ' ' << canvas.height << "\n255\n";
  std::vector<char> row(static_cast<size_t>(canvas.width) * 3);
  for(int y{0}; y < canvas.height; ++y) {
    if(y == area.bottom + 1) std::ranges::fill(row, 0);
    if(y >= area.top && y <= area.bottom) {
      for(int x{area.left}; x <= area.right; ++x) {
        auto const rgb{colours.colours[image.raster[static_cast<size_t>(y - area.top) * area.width() + x - area.left]]};
        row[x * 3] = static_cast<char>(rgb >> 16);
        row[x * 3 + 1] = static_cast<char>(rgb >> 8);
        row[x * 3 + 2] = static_cast<char>(rgb);
      }
    }
    file.write(row.data(), static_cast<std::streamsize>(row.size()));
  }
  if(!file) throw std::runtime_error{"cannot write screenshot " + path.string()};
}

} // namespace earthsiege

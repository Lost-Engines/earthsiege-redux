#include "framebuffer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include "resources/artwork.h"

namespace earthsiege {

framebuffer::framebuffer(render_size size, uint8_t colour) : extent{size}, raster(size.count(), colour) {}
framebuffer::framebuffer(render_size size, std::span<uint8_t const> pixels)
  : extent{size}, raster(pixels.begin(), pixels.end()) {
  if(size.count() != raster.size()) throw std::invalid_argument{"framebuffer dimensions differ from pixel count"};
}

indexed_region framebuffer::detail_region() const {
  if(!detail) return {{0, 0}, 0, nullptr};
  auto const stride{detail->extent.width};
  return {detail_size(), stride, detail->raster.data() + static_cast<size_t>(detail_area.top) * stride + detail_area.left};
}

void framebuffer::set_surround(std::shared_ptr<framebuffer const> image, rectangle area, cockpit_framing view) {
  if(area.left < 0 || area.top < 0 || area.right >= image->extent.width || area.bottom >= image->extent.height)
    throw std::out_of_range{"cockpit surround exceeds its backing image"};
  surround = std::move(image);
  surround_area = area;
  framing = view;
}
void framebuffer::copy_surround(framebuffer const &source) {
  surround = source.surround;
  surround_area = source.surround_area;
  framing = source.framing;
}
indexed_region framebuffer::surround_region() const {
  if(!surround) return {{0, 0}, 0, nullptr};
  int const stride{surround->extent.width};
  return {{surround_area.width(), surround_area.height()}, stride,
          surround->raster.data() + static_cast<size_t>(surround_area.top) * stride + surround_area.left};
}
rectangle framebuffer::detail_in_surround() const {
  return {detail_area.left - surround_area.left, detail_area.top - surround_area.top,
          detail_area.right - surround_area.left, detail_area.bottom - surround_area.top};
}

framebuffer framebuffer::dialog_backdrop() const {
  if(!surround || (framing.scale == 1 && !framing.pan)) return *this;
  // Freeze the cockpit at its selected FOV. The dialog and its hitboxes use the normal UI scale.
  auto const size{surround_region().size};
  auto image{std::make_shared<framebuffer>(size)};
  blit_presentation(*image, size.bounds());
  framebuffer page;
  page.attach(image, size.cockpit_panel());
  page.set_surround(std::move(image), size.bounds());
  return page;
}

void framebuffer::blit_presentation(framebuffer &dest, rectangle area) const {
  if(!surround) { blit_to(dest, area); return; }
  auto const source{surround_region()};
  auto const clipped{area.intersect(dest.size().bounds())};
  if(clipped.width() <= 0 || clipped.height() <= 0) return;
  for(int y{clipped.top}; y <= clipped.bottom; ++y) {
    auto const *row{source.pixels + int64_t{y - area.top} * source.size.height / area.height() * source.stride};
    auto out{dest.opaque_span(y, clipped.left, clipped.right)};
    if(area.width() == source.size.width) std::copy_n(row + clipped.left - area.left, out.size(), out.begin());
    else for(int x{clipped.left}; x <= clipped.right; ++x) out[x - clipped.left] = row[int64_t{x - area.left} * source.size.width / area.width()];
  }
  auto const panel{render_size{area.width(), area.height()}.cockpit_panel(framing)};
  blit_to(dest, {area.left + panel.left, area.top + panel.top, area.left + panel.right, area.top + panel.bottom});
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
  copy_surround(source);
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
  int const width{right - left + 1};
  bool const direct{detail && detail_area.width() == area.width() && detail_area.height() == area.height()};
  std::vector<int> xs(width), detail_x(detail && !direct ? width : 0);
  for(int x{left}; x <= right; ++x) {
    xs[x - left] = static_cast<int>(int64_t{x - area.left} * extent.width / area.width());
    if(!detail_x.empty()) detail_x[x - left] = detail_area.left + static_cast<int>(int64_t{x - area.left} * detail_area.width() / area.width());
  }
  std::vector<scanline_span> spans;
  int previous_y{-1};
  for(int y{top}; y <= bottom; ++y) {
    auto const sy{static_cast<int>(int64_t{y - area.top} * extent.height / area.height())};
    auto const row{static_cast<size_t>(sy) * extent.width};
    auto *dst{dest.raster.data() + static_cast<size_t>(y) * dest.extent.width + left};
    if(!detail_x.empty()) {
      auto const dy{detail_area.top + int64_t{y - area.top} * detail_area.height() / area.height()};
      auto const *hi{detail->raster.data() + dy * detail->extent.width};
      for(int i{0}; i < width; ++i) dst[i] = visible[row + xs[i]] ? hi[detail_x[i]] : raster[row + xs[i]];
      continue;
    }
    if(sy != previous_y && direct) {
      // All output rows in one cockpit scanline share the same HUD mask.
      spans.clear();
      for(int x{xs.front()}; x <= xs.back();) {
        if(!visible[row + x]) {
          ++x;
          continue;
        }
        auto const first{x};
        do { ++x; } while(x <= xs.back() && visible[row + x]);
        auto const offset{area.left - left};
        int const start{std::max(0, offset + render_size::edge(first, area.width(), extent.width))};
        int const end{std::min(width, offset + render_size::edge(x, area.width(), extent.width))};
        if(end > start) spans.push_back({start, end - 1});
      }
    }
    bool const all_world{spans.size() == 1 && spans.front().left == 0 && spans.front().right == width - 1};
    if(!all_world) {
      if(sy == previous_y) {
        std::copy_n(dst - dest.extent.width, width, dst);
      } else {
        for(int i{0}; i < width; ++i) dst[i] = raster[row + xs[i]];
      }
    }
    if(direct) {
      auto const dy{detail_area.top + y - area.top};
      auto const *hi{detail->raster.data() + static_cast<size_t>(dy) * detail->extent.width + detail_area.left + (left - area.left)};
      for(auto const span : spans) std::copy_n(hi + span.left, span.right - span.left + 1, dst + span.left);
    }
    previous_y = sy;
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
  surround.reset();
  std::ranges::fill(raster, colour);
}

void framebuffer::pixel(int x, int y, uint8_t colour) {
  if(x >= 0 && y >= 0 && x < extent.width && y < extent.height && (!pixel_mask || pixel_mask->contains(x, y))) {
    auto const i{static_cast<size_t>(y) * extent.width + x};
    raster[i] = colour;
    if(detail) visible[i] = 0;
  }
}

std::span<uint8_t> framebuffer::opaque_span(int y, int left, int right) {
  if(pixel_mask || y < 0 || y >= extent.height || left < 0 || right >= extent.width || left > right)
    throw std::invalid_argument{"opaque span must be inside an unmasked raster"};
  auto const first{static_cast<size_t>(y) * extent.width + left};
  auto const count{static_cast<size_t>(right - left + 1)};
  if(detail) std::fill_n(visible.begin() + first, count, 0);
  return std::span{raster}.subspan(first, count);
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

} // namespace earthsiege

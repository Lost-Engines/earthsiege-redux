#include "panorama.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "render/bitmap_raster.h"
#include "render/texture_raster.h"
#include "simulation/fixed_math.h"
#include "simulation/model_math.h"

namespace earthsiege {
std::vector<panorama_placement> place_panorama(std::span<bitmap const> images, background_bands const &bands, background_view const &view, uint16_t heading, int horizon_offset) {
  std::vector<panorama_placement> result;
  if(images.empty()) return result;
  int const count{static_cast<int>(images.size())};
  int const width{static_cast<int>(images.front().width)};
  if(!width) return result;
  auto const divisor{(static_cast<uint64_t>(65536 / count) << 16) / static_cast<unsigned>(width)};
  int x{static_cast<int>((uint64_t{heading} << 16) / divisor)};
  if(view.plane != projection_plane{}) {
    // The tiles form a continuous strip along the banked horizon. Project
    // the four screen corners onto it to find the first and last visible tile.
    double const c{model_cosine(view.roll) / 16384.0};
    double const s{model_cosine(static_cast<uint16_t>(view.roll - 16384)) / 16384.0};
    double const cx{(bands.horizon[0].x + bands.horizon[1].x) * .5};
    double const cy{(bands.horizon[0].y + bands.horizon[1].y) * .5};
    double const phase{double(x - 160)};
    auto const &b{view.bounds};
    auto const along{[&](int px, int py){return ((px - cx) * c - (py - cy) * s - phase) / width;}};
    auto const [first, last]{std::minmax({along(b.left, b.top), along(b.left, b.bottom), along(b.right, b.top), along(b.right, b.bottom)})};
    for(int tile{static_cast<int>(std::floor(first)) - 1}; tile <= static_cast<int>(std::ceil(last)); ++tile) {
      size_t const index{static_cast<size_t>((tile % count + count) % count)};
      double const u{phase + double(tile) * width}, v{horizon_offset - static_cast<double>(images[index].height)};
      result.push_back({index, {static_cast<int>(std::lround(cx + u * c + v * s)), static_cast<int>(std::lround(cy - u * s + v * c))},
                        static_cast<uint16_t>(-view.roll), view.roll != 0});
    }
    return result;
  }
  if(bands.horizon[0].y + horizon_offset <= 0 && bands.horizon[1].y + horizon_offset <= 0) return result;
  int const left{view.bounds.left + view.offset.x};
  while(left < x) x -= count * width;
  int y{bands.horizon[0].y};
  int dx{width};
  int dy{0};
  int extra_x{0};
  int extra_y{0};
  int const sine{model_cosine(static_cast<uint16_t>(view.roll - 16384))};
  int const cosine{model_cosine(view.roll)};
  if(view.roll) {
    dy = (-sine * width) >> 14;
    dx = (cosine * width) >> 14;
    extra_x = (sine * horizon_offset) >> 14;
    extra_y = (cosine * horizon_offset) >> 14;
    int const centre_x{((bands.horizon[0].x + bands.horizon[1].x) >> 1) + view.offset.x};
    int const centre_y{((bands.horizon[0].y + bands.horizon[1].y) >> 1) + view.offset.y};
    int const relative{x - 160};
    x = ((relative * cosine) >> 14) + centre_x;
    y = ((-relative * sine) >> 14) + centre_y;
  }
  // The original strip walk cannot make forward progress at inverted roll.
  if(dx <= 0) throw std::domain_error{"original panorama has nonpositive horizontal step"};
  size_t image{0};
  while(x + dx < left) {
    x += dx;
    y += dy;
    if(++image == images.size()) image = 0;
  }
  for(int drawn{0}; x < left + 320 && drawn < count; ++drawn) {
    int const height{static_cast<int>(images[image].height)};
    vec2<int32_t> position{x - view.offset.x, y - view.offset.y};
    if(view.roll) {
      position.x += extra_x - ((height * sine) >> 14);
      position.y += extra_y - ((height * cosine) >> 14);
    } else {
      position.y += horizon_offset - height;
    }
    result.push_back({image, vec2<int32_t>{position}, static_cast<uint16_t>(-view.roll), view.roll != 0});
    x += dx;
    y += dy;
    if(++image == images.size()) image = 0;
  }
  return result;
}
void draw_panorama(framebuffer &target, std::span<bitmap const> images, std::span<panorama_placement const> placements, rectangle bounds, projection_plane const &plane) {
  for(auto const &p : placements) {
    auto const &image{images[p.frame]};
    int const width{static_cast<int>(image.width)};
    int const height{static_cast<int>(image.height)};
    if(!p.rotated) {
      if(target.size() != render_size{} || plane != projection_plane{}) {
        auto const area{plane.pixels({p.position.x, p.position.y, p.position.x + width - 1, p.position.y + height - 1}, target.size())};
        draw_scaled_bitmap(target, image, {area.left, area.top}, {area.width(), area.height()}, plane.clip_bounds(bounds, target.size()));
        continue;
      }
      for(int y{std::max(bounds.top, p.position.y)}; y <= std::min(bounds.bottom, p.position.y + height - 1); ++y) {
        for(int x{std::max(bounds.left, p.position.x)}; x <= std::min(bounds.right, p.position.x + width - 1); ++x) {
          auto const colour{image.pixels.at(static_cast<size_t>((y - p.position.y) * width + x - p.position.x))};
          if(colour) target.pixel(x, y, colour);
        }
      }
      continue;
    }
    // Original 0x76630 rotates a width-by-height quad; its texture corners end
    // at width-1 / height-1. The affine mapper supplies the native transparency.
    int const c{model_cosine(p.angle)};
    int const s{model_cosine(static_cast<uint16_t>(p.angle - 16384))};
    std::array<vec2<int32_t>, 4> const corners{vec2<int32_t>{0, 0}, vec2<int32_t>{width, 0}, vec2<int32_t>{width, height}, vec2<int32_t>{0, height}};
    std::array<projected_vertex, 4> vertices;
    for(size_t i{0}; i < vertices.size(); ++i) {
      auto const &v{corners[i]};
      vertices[i].position = {p.position.x + wrap_long((static_cast<int64_t>(v.x) * c - static_cast<int64_t>(v.y) * s + 8192) >> 14),
                         p.position.y + wrap_long((static_cast<int64_t>(v.x) * s + static_cast<int64_t>(v.y) * c + 8192) >> 14)};
      vertices[i].texture = {(i == 1 || i == 2) ? width - 1 : 0, i >= 2 ? height - 1 : 0};
      vertices[i].position.x = plane.x_edge(vertices[i].position.x, target.size());
      vertices[i].position.y = plane.y_edge(vertices[i].position.y, target.size());
    }
    draw_textured_polygon(target, vertices, image, plane.clip_bounds(bounds, target.size()), {}, target.clip() ? texture_scan_direction::rows : texture_scan_direction::columns);
  }
}
} // namespace earthsiege

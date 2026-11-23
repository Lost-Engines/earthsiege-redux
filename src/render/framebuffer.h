#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <vector>
#include "render/resolution.h"

namespace earthsiege {

struct bitmap;
struct font;
struct palette;

struct indexed_region {
  render_size size;
  int stride;
  uint8_t const *pixels;
};

struct scanline_span {
  int left;
  int right;
};

struct screen_region {
  int first_row{0};
  std::vector<scanline_span> spans;
  bool rectangular{false};
  bool contains(int x, int y) const;
};

/// Inclusive window spans from an original ED cockpit clipping resource.
struct screen_mask {
  std::vector<screen_region> regions;
  void translate(int x, int y);
  std::array<std::vector<scanline_span>, screen_height> rows;
  bool contains(int x, int y) const;
};

class framebuffer {
  render_size extent;
  std::vector<uint8_t> raster;
  screen_mask const *mask{nullptr};
  screen_mask const *pixel_mask{nullptr};
  // A cockpit page keeps the rendered world behind its native artwork.
  // Later HUD, dialog and cursor writes cover just the cells they touch.
  std::shared_ptr<framebuffer const> detail;
  rectangle detail_area;
  std::vector<uint8_t> visible;

public:
  framebuffer() : framebuffer{render_size{}} {}
  explicit framebuffer(render_size size);
  framebuffer(render_size size, std::span<uint8_t const> pixels);
  render_size size() const { return extent; }
  bool has_detail() const { return static_cast<bool>(detail); }
  render_size detail_size() const { return {detail_area.width(), detail_area.height()}; }
  indexed_region detail_region() const;
  std::span<uint8_t const> detail_mask() const { return detail ? std::span{visible} : std::span<uint8_t const>{}; }
  void attach(std::shared_ptr<framebuffer const> image, rectangle area, screen_mask const *window = nullptr);
  void restore_world(framebuffer const &source, screen_mask const &window);
  void blit_to(framebuffer &dest, rectangle area) const;
  void copy_rows(int source_x, int target_x, rectangle area);
  framebuffer resampled(render_size size) const;
  screen_mask const *clip() const;
  screen_mask const *raster_clip() const { return pixel_mask; }
  void clip(screen_mask const *value);
  void clear(uint8_t colour = 0);
  void pixel(int x, int y, uint8_t colour);
  // For a rasterizer overwriting every pixel of an already clipped span.
  std::span<uint8_t> opaque_span(int y, int left, int right);
  void fill(rectangle area, uint8_t colour);
  void line(int x0, int y0, int x1, int y1, uint8_t colour);
  void image(bitmap const &source, int x, int y, bool transparent = false);
  void text(font const &face, std::string_view text, int x, int y);
  auto pixels() const->std::vector<uint8_t> const & { return raster; }
  void save(std::filesystem::path const &path, palette const &colours, render_size canvas = {}) const;
};

/// Keep cockpit clipping local to scenery drawing; instruments use their own bounds.
class scoped_framebuffer_clip {
  framebuffer &target;
  screen_mask const *previous;
public:
  scoped_framebuffer_clip(framebuffer &target, screen_mask const *mask);
  ~scoped_framebuffer_clip();
  scoped_framebuffer_clip(scoped_framebuffer_clip const &) = delete;
  scoped_framebuffer_clip &operator=(scoped_framebuffer_clip const &) = delete;
};

} // namespace earthsiege

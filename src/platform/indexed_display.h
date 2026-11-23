#pragma once
#include <array>
#include <GLFW/glfw3.h>
#include "render/framebuffer.h"

namespace earthsiege {
struct viewport;

// The software rasterizer supplies palette indices. The display combines the
// cockpit and visible world texels and performs the final VGA palette lookup.
class indexed_display {
  enum plane : unsigned { page, coverage, world, colours, plane_count };
  std::array<GLuint, plane_count> textures{};
  std::array<render_size, plane_count> extents{};
  GLuint program{};
  GLint canvas_size{}, page_size{}, world_size{}, view_rect{};
  void (APIENTRY *active_texture)(GLenum);
  void (APIENTRY *uniform_size)(GLint, GLint, GLint);
  void (APIENTRY *uniform_rect)(GLint, GLint, GLint, GLint, GLint);
  void (APIENTRY *delete_program)(GLuint);
  void upload(plane which, indexed_region image);
public:
  indexed_display();
  ~indexed_display();
  indexed_display(indexed_display const &) = delete;
  indexed_display &operator=(indexed_display const &) = delete;
  void bind(framebuffer const &frame, palette const &palette, render_size canvas, viewport const &view, int drawable_height);
};
}

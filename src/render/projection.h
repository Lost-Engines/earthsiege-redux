#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include "render/resolution.h"
#include "vectorstorm/vector/vector2.h"
#include "vectorstorm/vector/vector3.h"

namespace earthsiege {

// The part of the original 256-unit focal plane covered by a world raster.
// Coordinates stay in the cockpit artist's units through clipping and culling.
struct projection_plane {
  double left{0}, top{0}, width{320}, height{200};
  bool operator==(projection_plane const &) const = default;
  static projection_plane cockpit(render_size output, vec2<int32_t> const &centre, double zoom, int pan = 0);
  rectangle bounds() const;
  rectangle pixels(rectangle area, render_size output) const;
  rectangle clip_bounds(rectangle area, render_size output) const;
  int x_edge(int x, render_size output) const;
  int y_edge(int y, render_size output) const;
};

// Horizontal angle across the original 320-pixel cockpit at focal length 256.
double cockpit_fov_zoom(double degrees);

struct camera_projection {
  unsigned int perspective_shift{8};                                           // zero selects parallel projection
  int32_t near_distance{256};
  int32_t orthographic_divisor{256};
  vec2<int32_t> centre{160, 100};
  render_size output{};
  projection_plane plane{};
};

enum class projection_source {
  model,
  terrain,
};

struct camera_vertex {
  vec3<int32_t> position;
  vec2<int32_t> texture;
  uint16_t shade{0};
};

struct projected_vertex {
  vec2<int32_t> position;
  vec2<int32_t> texture;
  uint16_t shade{0};
};

bool inside_near_plane(vec3<int32_t> const &point, camera_projection const &view, projection_source source);
vec2<int32_t> project_point(vec3<int32_t> const &point, camera_projection const &view, projection_source source);
std::vector<projected_vertex> project_polygon(std::span<camera_vertex const> vertices, camera_projection const &view, projection_source source);

} // namespace earthsiege

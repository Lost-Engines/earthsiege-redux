#include "ground_marks.h"
#include <stdexcept>
#include "simulation/fixed_math.h"
#include "simulation/model_math.h"

namespace earthsiege {
ground_marks::ground_marks(world_environment const &environment)
 : colour{environment.atmosphere_parameters[0]} {
  if(environment.effect_parameters[1] < 0) throw std::runtime_error{"negative ground-mark count"};
  field.resize(static_cast<size_t>(environment.effect_parameters[1]));
}
void ground_marks::draw(framebuffer &target, vec3<int32_t> const &position, vec3<uint16_t> const &angles,
                        camera_projection const &projection, rectangle bounds, uint8_t detail, random_state &random) {
  if(!ready) {
    previous = position;
    for(auto &point : field) {
      auto const angle{static_cast<uint16_t>(random.next())};
      int32_t const radius{static_cast<int32_t>(random.next()) * 2};
      point.x = wrap_long(static_cast<int64_t>(position.x) + ((static_cast<int64_t>(radius) * model_cosine(angle) + 8192) >> 14));
      point.y = wrap_long(static_cast<int64_t>(position.y) + ((static_cast<int64_t>(radius) * model_cosine(static_cast<uint16_t>(angle - 16384)) + 8192) >> 14));
      point.z = 0;
    }
    ready = true;
  }
  // The high-detail path retains both the prior active count and the old anchor.
  if(detail >= 240) return;
  active = static_cast<int16_t>(field.size() * detail / 240);
  bool const moved{position != previous};
  vec2<int32_t> const middle{
  wrap_long(static_cast<int64_t>(position.x) + previous.x) >> 1,
  wrap_long(static_cast<int64_t>(position.y) + previous.y) >> 1
 };
  auto transform{rotation_transform(angles)};
  transform.translation = position;
  transform = inverse_transform(transform);
  for(int16_t i{0}; i < active; ++i) {
    auto &point{field[static_cast<size_t>(i)]};
    auto const distance{fast_distance_2d({wrap_long(static_cast<int64_t>(point.x) - position.x), wrap_long(static_cast<int64_t>(point.y) - position.y)})};
    if(moved && distance > 70000) {
      point.x = wrap_long(static_cast<int64_t>(middle.x) * 2 - point.x);
      point.y = wrap_long(static_cast<int64_t>(middle.y) * 2 - point.y);
    }
    auto camera{rotate_vector(point, transform)};
    for(unsigned axis{0}; axis < 3; ++axis) camera[axis] = wrap_long(static_cast<int64_t>(camera[axis]) + transform.translation[axis]);
    if(camera.y <= projection.near_distance) continue;
    auto const screen{project_point(camera, projection, projection_source::model)};
    auto stamp{[&](int x, int y, int c){
      auto cell{projection.output.from_logical({x, y, x, y})};
      cell = {screen.x + cell.left, screen.y + cell.top, screen.x + cell.right, screen.y + cell.bottom};
      target.fill(cell.intersect(bounds), static_cast<uint8_t>(c));
    }};
    stamp(0, 0, colour);
    if(distance < 30000) stamp(1, 0, colour);
    if(distance < 20000) {
      stamp(2, 0, colour - 2);
      stamp(2, -1, colour - 2);
      stamp(1, -1, colour - 4);
    }
  }
  previous = position;
}
} // namespace earthsiege

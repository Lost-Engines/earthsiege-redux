#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include "resources/resource_store.h"
#include "simulation/ground_actor.h"

namespace {
using namespace earthsiege;

void check_listening_post(resource_store const &resources, ground_catalogue const &catalogue, terrain_kind terrain, int16_t palette_variant) {
  scenery_world world{resources, {.terrain{terrain}, .zone{-1}, .palette_variant{palette_variant}}};
  auto const bindings{read_distance_shade_bindings(resources.read("simvol0", "dat/SHADES.DAT"))};
  framebuffer frame;
  scenery_renderer renderer{frame, world, bindings, scene_shading::distance, 2.0};
  ground_actor post{catalogue, 5, 0, 100, {0, 0, 0}, {0, 0, 4096}};
  std::array actors{post.model()};
  std::array const lights{default_world_light()};

  // Hold framing and geometry steady as the camera crosses real distance bands.
  model_view view;
  view.projection.perspective_shift = 0;
  view.projection.orthographic_divisor = 14000;
  view.projection.centre.y = 150;
  view.detail.distance_multiplier = 100.0;
  auto draw{[&](int distance, std::span<bitmap const> textures) {
    actors.front().textures = textures;
    frame.clear();
    if(renderer.draw({0, -distance, 0}, {}, lights, false, actors, nullptr, {}, view).size() != 1) throw std::runtime_error{"listening post was culled"};
    return frame.pixels();
  }};

  auto const &environment{world.environment()};
  auto const &edges{environment.distance_bands[0]};
  auto const offset{environment.model_distance_offsets[1]}; // large building
  auto const textures{catalogue.textures()};
  auto const near{draw(10000, textures)};
  if(draw(edges.front() - offset - 1, textures) != near) throw std::runtime_error{"textures changed before the first fog band"};
  auto const middle{draw(edges[edges.size() / 2 - 1] - offset, textures)};
  auto const far{draw(edges[edges.size() - 2] - offset, textures)};
  if(draw(edges.back() - offset + 8000, textures) != far) throw std::runtime_error{"textures changed beyond the last fog band"};
  if(draw(10000, textures) != near) throw std::runtime_error{"near model kept the previous distance tint"};

  // Repaint the panels in one known material to identify textured pixels and
  // verify that even dark texels reach that material's own far shade at dusk.
  auto const &materials{bindings.at(actors.front().native_class)};
  auto const column{materials.front().environment_column};
  std::vector<bitmap> painted(textures.begin(), textures.end());
  for(auto &image : painted) std::ranges::fill(image.pixels, static_cast<uint8_t>(environment.near_ranges.at(column).first));
  auto const painted_near{draw(10000, painted)};
  auto const painted_far{draw(edges.back() - offset, painted)};
  size_t marked{0};
  size_t changing{0};
  auto const far_shade{[&](size_t material) { return environment.far_ranges.at(material).first; }};
  for(size_t i{0}; i < near.size(); ++i) {
    if(near[i] == painted_near[i]) continue;
    ++marked;
    if(near[i] != middle[i] && middle[i] != far[i]) ++changing;
    if(painted_far[i] != far_shade(column)) throw std::runtime_error{"texture missed its material's far shade"};
    if(std::ranges::none_of(materials, [&](shade_binding b) { return far_shade(b.environment_column) == far[i]; })) {
      throw std::runtime_error{"original texture stayed visible through full fog"};
    }
  }
  if(!marked || !changing) throw std::runtime_error{"no visible texture passed through an intermediate fog shade"};
}
} // namespace

int main(int argc, char **argv) try {
  if(argc != 2) return EXIT_FAILURE;
  earthsiege::resource_store resources{argv[1]};
  earthsiege::ground_catalogue catalogue{resources};
  check_listening_post(resources, catalogue, earthsiege::terrain_kind::desert, 0);
  check_listening_post(resources, catalogue, earthsiege::terrain_kind::desert, 1);
  if(resources.edition() == earthsiege::game_edition::expansion) check_listening_post(resources, catalogue, earthsiege::terrain_kind::ruins, 1);
  return EXIT_SUCCESS;
} catch(std::exception const &error) {
  std::cerr << error.what() << '\n';
  return EXIT_FAILURE;
}

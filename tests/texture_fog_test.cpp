#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include "resources/resource_store.h"
#include "simulation/ground_actor.h"
#include "simulation/herc_body.h"
#include "simulation/world_effects.h"

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

void check_weapon_fog(resource_store const &resources, ground_catalogue const &catalogue) {
  scenery_world world{resources, {.terrain{terrain_kind::urban}, .zone{-1}}};
  auto const bindings{read_distance_shade_bindings(resources.read("simvol0", "dat/SHADES.DAT"))};
  auto const &environment{world.environment()};
  auto const &edges{environment.distance_bands[0]};
  framebuffer frame;
  scenery_renderer renderer{frame, world, bindings, scene_shading::distance, 10.0};
  model_view view;
  view.projection.perspective_shift = 0;
  view.projection.orthographic_divisor = 9000;
  view.projection.centre.y = 168;
  view.detail.distance_multiplier = 1000.0;
  std::array const lights{default_world_light()};
  uint8_t constexpr background{63};
  auto const draw{[&](scene_model const &model, int distance) {
    frame.clear(background);
    if(renderer.draw({0, -distance, 0}, {0, 0, 0}, lights, false, {&model, 1}, nullptr, {}, view).size() != 1) throw std::runtime_error{"weapon fog test model was culled"};
    return frame.pixels();
  }};
  auto const check_far{[&](scene_model const &model, int distance) {
    auto const image{draw(model, distance)};
    std::array<bool, 256> allowed{};
    for(auto binding : bindings.at(model.native_class)) {
      auto const range{environment.far_ranges.at(binding.environment_column)};
      std::fill_n(allowed.begin() + range.first, range.count, true);
    }
    if(std::ranges::all_of(image, [&](auto ink){return ink == background;})) throw std::runtime_error{"empty distant model"};
    for(auto ink : image) {
      if(ink != background && !allowed[ink]) throw std::runtime_error{"distant model retained an unfogged material"};
    }
  }};

  // The gun tower has two black muzzle faces. Its first firing cell lights them.
  ground_actor tower{catalogue, 8, 1, 100, {0, 0, 0}, {0, 0, 32768}};
  model_instance pose{tower.instance()};
  auto const original{tower.model()};
  scene_model const turret{original.roots, pose, original.placement, original.native_class, original.textures, {}, {}, {}};
  auto &cell{pose.cell_frames()[tower.profile().cell_sequence]};
  int const offset{environment.model_distance_offsets[0]};
  int const middle{edges[edges.size() / 2] - offset};
  int const far{edges.back() - offset + 8000};
  cell = 0;
  auto const idle{draw(turret, 10000)};
  auto const hazy_idle{draw(turret, middle)};
  check_far(turret, far);
  cell = 1;
  auto const flash{draw(turret, 10000)};
  auto const hazy_flash{draw(turret, middle)};
  size_t flashing_pixels{0};
  for(size_t i{0}; i < idle.size(); ++i) {
    if(idle[i] != 0 || flash[i] != 102) continue;
    if(world.colours().dac_colours[hazy_idle[i]] == world.colours().dac_colours[0] ||
       world.colours().dac_colours[hazy_idle[i]] == world.colours().dac_colours[hazy_flash[i]]) throw std::runtime_error{"muzzle stayed black or lost its flash in partial fog"};
    ++flashing_pixels;
  }
  if(!flashing_pixels) throw std::runtime_error{"missing animated muzzle faces in the close view"};
  for(cell = 1; cell < 4; ++cell) check_far(turret, far);
  cell = 0;
  if(draw(turret, 10000) != idle) throw std::runtime_error{"gun tower did not recover its close colours"};

  auto const herc{read_herc_definition(resources, "SCORPION")};
  auto const weapons{read_weapon_catalogue(resources)};
  auto const templates{read_model(resources.read("simvol0", "dts/MECHWPNS.DTS"))};
  auto const textures{read_bitmaps(resources.read("simvol0", "dba/WPNTEX.DBA"))};
  std::array<int16_t, 9> const loadout{}; // weapon 0 in each mount
  herc_body body{herc, weapons, templates, loadout, {0, 0, 0}, {0, 0, 32768}};
  std::vector<model_attachment> mounts;
  for(auto const &part : body.weapons().parts()) mounts.push_back({part.body_part, part.geometry ? &*part.geometry : nullptr, part.cell_frames, textures});
  scene_model const enemy{std::span{herc.models.roots}.first(1), body.model(), body.placement(), 7, herc.textures, mounts, {}, {}};
  auto const close{draw(enemy, 10000)};
  if(std::ranges::none_of(close, [&](auto ink){return world.colours().dac_colours[ink] == world.colours().dac_colours[0];})) throw std::runtime_error{"missing dark HERC details in the close view"};
  check_far(enemy, edges.back());
  if(draw(enemy, 10000) != close) throw std::runtime_error{"HERC did not recover its close colours"};
}
void check_shadow_fog(resource_store const &resources, terrain_kind terrain, int16_t palette_variant = 0) {
  scenery_world world{resources, {.terrain{terrain}, .zone{-1}, .palette_variant{palette_variant}}};
  auto const &env{world.environment()};
  auto const &edges{env.distance_bands[0]};
  auto const &colours{world.colours().dac_colours};
  bool const alternate{env.atmosphere_parameters[1] != 0};
  world_effects effects{resources, {}, alternate};
  framebuffer frame;
  scenery_renderer renderer{frame, world, read_distance_shade_bindings(resources.read("simvol0", "dat/SHADES.DAT")), scene_shading::distance, 10.0};
  model_view view;
  view.projection.perspective_shift = 0;
  view.projection.orthographic_divisor = 4000;
  frame.clear(static_cast<uint8_t>(env.background.ground_palette_start));
  frame.fill({160, 0, 319, 199}, static_cast<uint8_t>(env.background.ground_palette_start + env.background.ground_bands - 1));
  auto const ground{frame};
  vec3<int32_t> caster{};
  vec3<int32_t> foot{100, 200, 0};
  auto const draw{[&](ground_flash const &mark, int height) {
    frame = ground;
    renderer.draw({0, 0, height}, {49152, 0, 0}, {}, false, {}, nullptr, {&mark, 1}, view);
    return frame.pixels();
  }};

  for(size_t shape : {0u, 2u}) {
    auto const shadow{effects.ground_attachment(shape, foot, 0, &caster)};
    auto const close{draw(shadow, 10000)};
    uint8_t const ink{static_cast<uint8_t>(alternate ? 224 : 240)};
    auto const pixel{std::ranges::find(close, ink)};
    if(pixel == close.end()) throw std::runtime_error{"missing authored shadow at close range"};
    if(draw(shadow, edges.front() - 1) != close) throw std::runtime_error{"foot entered fog before its HERC"};
    auto const middle{draw(shadow, edges[edges.size() / 2 - 1])};
    if(middle == close || middle == ground.pixels()) throw std::runtime_error{"missing partial shadow"};
    for(size_t i{0}; i < middle.size(); ++i) {
      auto const &surface{colours[ground.pixels()[i]]};
      auto const &near{colours[close[i]]};
      auto const &faded{colours[middle[i]]};
      for(size_t channel{0}; channel < faded.size(); ++channel) {
        if(faded[channel] < std::min(near[channel], surface[channel]) || faded[channel] > surface[channel])
          throw std::runtime_error{"partial shadow overshot its underlying ground colour"};
      }
    }
    if(draw(shadow, edges[edges.size() - 2]) != ground.pixels() || draw(shadow, edges.back() + 10000) != ground.pixels())
      throw std::runtime_error{"fully fogged shadow covered the ground"};
    if(draw(shadow, 10000) != close) throw std::runtime_error{"returning shadow lost its near colour"};
  }
  auto const flash{effects.ground_attachment(1, foot, 0)};
  auto const flash_near{draw(flash, 10000)};
  if(flash_near == ground.pixels() || draw(flash, edges.back()) != flash_near) throw std::runtime_error{"ground flash picked up shadow fog"};
}
} // namespace

int main(int argc, char **argv) try {
  if(argc != 2) return EXIT_FAILURE;
  earthsiege::resource_store resources{argv[1]};
  earthsiege::ground_catalogue catalogue{resources};
  check_listening_post(resources, catalogue, earthsiege::terrain_kind::desert, 0);
  check_listening_post(resources, catalogue, earthsiege::terrain_kind::desert, 1);
  check_weapon_fog(resources, catalogue);
  check_shadow_fog(resources, earthsiege::terrain_kind::urban);
  check_shadow_fog(resources, earthsiege::terrain_kind::desert);
  check_shadow_fog(resources, earthsiege::terrain_kind::temperate, 2);
  check_shadow_fog(resources, earthsiege::terrain_kind::ice, 1);
  if(resources.edition() == earthsiege::game_edition::expansion) check_listening_post(resources, catalogue, earthsiege::terrain_kind::ruins, 1);
  return EXIT_SUCCESS;
} catch(std::exception const &error) {
  std::cerr << error.what() << '\n';
  return EXIT_FAILURE;
}

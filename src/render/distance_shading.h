#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "render/palette_shading.h"
#include "resources/world.h"

namespace earthsiege {

struct shade_binding {
  uint8_t palette_slot{0};
  uint8_t environment_column{0};
};
using distance_shade_bindings = std::vector<std::vector<shade_binding>>;

distance_shade_bindings read_distance_shade_bindings(bytes source);

/// Authored distance bands for geometry, fog and visibility.
class distance_shading {
  world_environment const &environment;
  distance_shade_bindings bindings;
  std::vector<std::vector<palette_range>> bands;
  std::vector<palette_range> highlights;
  std::vector<std::vector<texture_shade_maps>> texture_bands;
  int64_t terrain_cutoff;
  int64_t actor_cutoff;
  std::array<int64_t, 2> ground_target_cutoffs;

public:
  distance_shading(world_environment const &environment, distance_shade_bindings bindings, double cutoff_multiplier = 1.0);
  void prepare_texture_shades(palette const &colours, texture_shade_maps const &lighting);
  size_t band(int32_t distance, int16_t native_class, int16_t radius) const;
  bool culled(int32_t distance, int16_t native_class, int16_t radius) const;
  // Set the solid material ramps and return the matching texture lookup.
  // A null lookup selects the model renderer's original face lighting.
  texture_shade_maps const *apply(palette &target, int32_t distance, int16_t native_class, int16_t radius, bool split_highlights = false) const;
};

} // namespace earthsiege

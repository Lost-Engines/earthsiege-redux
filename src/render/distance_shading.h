#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "resources/artwork.h"
#include "resources/world.h"

namespace earthsiege {

struct shade_binding {
  uint8_t palette_slot{0};
  uint8_t environment_column{0};
};
using distance_shade_bindings = std::vector<std::vector<shade_binding>>;

distance_shade_bindings read_distance_shade_bindings(bytes source);

/// Precompute the authored distance palette ranges; apply only an object's bound slots
class distance_shading {
  world_environment const &environment;
  distance_shade_bindings bindings;
  std::vector<std::vector<palette_range>> bands;
  std::vector<palette_range> highlights;
  int64_t terrain_cutoff;
  int64_t actor_cutoff;
  std::array<int64_t, 2> ground_target_cutoffs;

public:
  distance_shading(world_environment const &environment, distance_shade_bindings bindings, double cutoff_multiplier = 1.0);
  size_t band(int32_t distance, int16_t native_class, int16_t radius) const;
  bool culled(int32_t distance, int16_t native_class, int16_t radius) const;
  void apply(palette &target, int32_t distance, int16_t native_class, int16_t radius, bool split_highlights = false) const;
};

} // namespace earthsiege

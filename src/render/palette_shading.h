#pragma once
#include <array>
#include <bitset>
#include <cstdint>
#include <span>
#include "resources/artwork.h"

namespace earthsiege {

inline unsigned int constexpr texture_shade_count{8};
using palette_ink_map = std::array<uint8_t, 256>;
using texture_shade_maps = std::array<palette_ink_map, texture_shade_count>;

struct model_fog_band {
  texture_shade_maps textures;
  palette_ink_map fixed_inks;
  std::bitset<256> mapped_ramps;
};

void apply_cockpit_map_shading(palette &target);
uint8_t shade_colour(std::span<uint16_t const> ramp, uint8_t intensity);
unsigned int texture_shade_index(uint8_t intensity);
texture_shade_maps make_texture_shades(palette const &source);

} // namespace earthsiege

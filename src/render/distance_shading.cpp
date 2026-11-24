#include "distance_shading.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include "simulation/fixed_math.h"

namespace earthsiege {
namespace {

int16_t constexpr terrain_class{0};
int16_t constexpr ground_target_class{5};
int16_t constexpr effect_class{8};
int16_t constexpr large_model_radius{5000};

std::vector<palette_range> interpolate_ranges(palette_range const near, palette_range const far, int const levels) {
  if(levels <= 0) throw std::invalid_argument{"nonpositive distance shade levels"};
  std::vector<palette_range> result;
  auto const near_last{wrap_word(near.first + near.count - 1)};
  auto const far_last{wrap_word(far.first + far.count - 1)};
  if(levels > 1) {
    auto const first_step{wrap_word((far.first - near.first + 1) * 256 / (levels - 1))};
    auto const last_step{wrap_word((far_last - near_last + 1) * 256 / (levels - 1))};
    for(int index{0}; index < levels - 1; ++index) {
      auto const first{wrap_word(near.first + ((index * first_step) >> 8))};
      auto const count{wrap_word(near_last + ((index * last_step) >> 8) - first + 1)};
      result.push_back(palette_range{
        .first{first},
        .count{count}
      });
    }
  }
  result.push_back(far);
  return result;
}

void append_range(std::vector<uint16_t> &target, int const first, int const count) {
  for(int index{0}; index < count; ++index) target.push_back(static_cast<uint16_t>(first + index));
}

using dac_colour = std::array<uint8_t, 3>;

int colour_error(dac_colour const &a, dac_colour const &b) {
  int const red{a[0] - b[0]};
  int const green{a[1] - b[1]};
  int const blue{a[2] - b[2]};
  return red * red + green * green + blue * blue;
}

uint8_t nearest_in_range(palette const &colours, uint8_t const ink, palette_range const range) {
  if(ink >= range.first && ink < range.first + range.count) return ink;
  int best{range.first};
  for(int i{best + 1}; i < range.first + range.count; ++i) {
    if(colour_error(colours.dac_colours[i], colours.dac_colours[ink]) < colour_error(colours.dac_colours[best], colours.dac_colours[ink])) best = i;
  }
  return static_cast<uint8_t>(best);
}

// Map a lit pixel through one material's distance ramps. For colours outside
// its near ramp, carry their RGB difference from the closest swatch, fading
// that difference to zero over the same distance. Quantise to world colours
// so a distant marking cannot pick up an animated cockpit palette entry.
std::vector<palette_ink_map> fog_column(palette const &colours, std::vector<palette_range> const &ranges, std::span<uint8_t const> world_inks) {
  auto const near{ranges.front()};
  int const last{static_cast<int>(ranges.size()) - 1};
  int const span{std::max(1, near.count - 1)};
  std::vector<palette_ink_map> result(last);
  for(int ink{0}; ink < 256; ++ink) {
    auto const anchor{nearest_in_range(colours, static_cast<uint8_t>(ink), near)};
    auto const &lit{colours.dac_colours[ink]};
    auto const &reference{colours.dac_colours[anchor]};
    for(int level{1}; level <= last; ++level) {
      auto const range{ranges[level]};
      uint8_t const shade{static_cast<uint8_t>(range.first + ((anchor - near.first) * (range.count - 1) + span / 2) / span)};
      auto &entry{result[level - 1][ink]};
      entry = shade;
      if(lit == reference || level == last) continue;
      dac_colour wanted;
      for(size_t channel{0}; channel < wanted.size(); ++channel) {
        int const residual{(lit[channel] - reference[channel]) * (last - level) / last};
        wanted[channel] = static_cast<uint8_t>(std::clamp(colours.dac_colours[shade][channel] + residual, 0, 63));
      }
      entry = static_cast<uint8_t>(ink);
      auto error{colour_error(lit, wanted)};
      for(auto const candidate : world_inks) {
        auto const next{colour_error(colours.dac_colours[candidate], wanted)};
        if(next < error) {
          error = next;
          entry = candidate;
        }
      }
    }
  }
  return result;
}

// Each entry blends the original FLAT/FLAT2 shadow into a pixel already on
// the ground. A dark patch of terrain can show through even in partial fog.
std::vector<palette_ink_map> ground_shadow_bands(world_environment const &env, palette const &colours) {
  int const last{env.shade_levels - 1};
  int const ink{env.atmosphere_parameters[1] ? 224 : 240};
  auto const &shadow{colours.dac_colours[ink]};
  std::vector<palette_ink_map> result(static_cast<size_t>(env.shade_levels));
  result.front().fill(static_cast<uint8_t>(ink));
  for(int ground{0}; ground < 256; ++ground) {
    auto const &surface{colours.dac_colours[ground]};
    for(int level{1}; level <= last; ++level) {
      // Compare palette swatches with the unrounded blend. Constrain each
      // channel so no swatch can brighten the ground or deepen the shadow.
      auto const error{[&](int swatch) -> int64_t {
        int64_t sum{};
        for(size_t ch{0}; ch < surface.size(); ++ch) {
          int const shade{std::max(0, surface[ch] - shadow[ch])};
          int const actual{surface[ch] - colours.dac_colours[swatch][ch]};
          if(actual < 0 || actual > shade) return std::numeric_limits<int64_t>::max();
          int64_t const delta{static_cast<int64_t>(actual) * last - static_cast<int64_t>(shade) * (last - level)};
          sum += delta * delta;
        }
        return sum;
      }};
      int best{ground};
      auto best_error{error(best)};
      for(int bank : {ink & 0xf0, ground & 0xf0}) {
        for(int swatch{bank}; swatch < bank + 16 && best_error; ++swatch) {
          auto const difference{error(swatch)};
          if(difference < best_error) {
            best = swatch;
            best_error = difference;
          }
        }
      }
      result[level][ground] = static_cast<uint8_t>(best);
    }
  }
  return result;
}

} // anonymous namespace

distance_shade_bindings read_distance_shade_bindings(bytes const source) {
  binary_reader file{source, "distance shade bindings"};
  distance_shade_bindings result(file.integer<uint8_t>());
  auto const count{file.integer<uint8_t>()};
  for(unsigned int index{0}; index != count; ++index) {
    auto const slot{file.integer<uint8_t>()};
    auto const pairs{file.integer<uint8_t>()};
    auto &entry{result.at(slot)};
    entry.clear();
    for(unsigned int pair{0}; pair != pairs; ++pair) {
      entry.push_back(shade_binding{
        .palette_slot{file.integer<uint8_t>()},
        .environment_column{file.integer<uint8_t>()}
      });
    }
  }
  file.require_end();
  return result;
}

distance_shading::distance_shading(world_environment const &source, distance_shade_bindings mapping, double const cutoff_multiplier)
  : environment{source},
    bindings{std::move(mapping)} {
  if(!std::isfinite(cutoff_multiplier) || cutoff_multiplier <= 0) throw std::invalid_argument{"invalid visibility distance multiplier"};
  if(source.distance_bands[0].empty() || source.distance_bands[1].empty()) throw std::invalid_argument{"empty distance bands"};
  // Scale the final world-space cutoff, including each building size's allowance.
  // The palette bands below continue to use the actual camera distance.
  auto const scale_cutoff{[cutoff_multiplier](int64_t distance) {
    if(distance < 0) return int64_t{-1};
    return static_cast<int64_t>(std::min(static_cast<double>(distance) * cutoff_multiplier, static_cast<double>(std::numeric_limits<uint32_t>::max())));
  }};
  int64_t const actor_range{static_cast<uint32_t>(source.distance_bands[0].back())};
  terrain_cutoff = scale_cutoff(static_cast<uint32_t>(source.distance_bands[1].back()));
  actor_cutoff = scale_cutoff(actor_range);
  for(size_t i{0}; i < ground_target_cutoffs.size(); ++i) ground_target_cutoffs[i] = scale_cutoff(actor_range - source.model_distance_offsets[i]);
  if(source.near_ranges.size() != source.far_ranges.size()) throw std::invalid_argument{"distance palette columns differ"};
  for(size_t index{0}; index != source.near_ranges.size(); ++index) bands.push_back(interpolate_ranges(source.near_ranges[index], source.far_ranges[index], source.shade_levels));
  highlights = interpolate_ranges(source.blend_ranges[0], source.blend_ranges[1], source.shade_levels);
}

void distance_shading::prepare_model_shades(palette const &colours, texture_shade_maps const &lighting) {
  shadow_bands = ground_shadow_bands(environment, colours);
  std::array<bool, 256> used{};
  for(auto const &column : bands) {
    for(auto const range : column) {
      if(range.count <= 0 || range.first < 0 || range.first + range.count > 256) throw std::out_of_range{"invalid texture fog ramp"};
      for(int i{range.first}; i < range.first + range.count; ++i) used[i] = true;
    }
  }
  std::vector<uint8_t> world_inks;
  for(size_t i{0}; i < used.size(); ++i) {
    if(used[i]) world_inks.push_back(static_cast<uint8_t>(i));
  }
  std::vector<std::vector<palette_ink_map>> columns;
  for(auto const &ranges : bands) columns.push_back(fog_column(colours, ranges, world_inks));

  model_bands.assign(bindings.size(), {});
  for(size_t kind{0}; kind < bindings.size(); ++kind) {
    auto const &mapping{bindings[kind]};
    if(mapping.empty()) continue;
    auto &tables{model_bands[kind]};
    tables.resize(environment.shade_levels);
    for(auto &table : tables) {
      for(auto binding : mapping) table.mapped_ramps.set(binding.palette_slot);
    }
    tables.front().textures = lighting;
    for(int ink{0}; ink < 256; ++ink) {
      tables.front().fixed_inks[ink] = static_cast<uint8_t>(ink);
      // A texel or literal colour has no material binding. Prefer the ramp
      // containing its original index, even when another ramp shares its RGB.
      auto const match_error{[&](shade_binding binding) {
        auto const near{bands.at(binding.environment_column).front()};
        auto const anchor{nearest_in_range(colours, static_cast<uint8_t>(ink), near)};
        return anchor == ink ? -1 : colour_error(colours.dac_colours[anchor], colours.dac_colours[ink]);
      }};
      auto const material{std::ranges::min_element(mapping, {}, match_error)->environment_column};
      for(size_t level{1}; level < tables.size(); ++level) {
        auto const &fog{columns[material][level - 1]};
        tables[level].fixed_inks[ink] = fog[ink];
        for(size_t light{0}; light < lighting.size(); ++light) tables[level].textures[light][ink] = fog[lighting[light][ink]];
      }
    }
  }
}

size_t distance_shading::band(int32_t distance, int16_t const native_class, int16_t const radius) const {
  if(native_class == ground_target_class) distance = std::max(0, wrap_long(static_cast<int64_t>(distance) + environment.model_distance_offsets[radius >= large_model_radius ? 1 : 0]));
  auto const &distances{environment.distance_bands[native_class == terrain_class ? 1 : 0]};
  if(distances.empty()) throw std::runtime_error{"empty distance bands"};
  size_t index{0};
  while(index + 1 < distances.size() && static_cast<uint32_t>(distance) >= static_cast<uint32_t>(distances[index])) ++index;
  return index;
}

bool distance_shading::culled(int32_t distance, int16_t const native_class, int16_t const radius) const {
  if(native_class == ground_target_class) return distance > ground_target_cutoffs[radius >= large_model_radius ? 1 : 0];
  if(native_class == effect_class) return distance > ground_target_cutoffs[0];
  return static_cast<uint32_t>(distance) > (native_class == terrain_class ? terrain_cutoff : actor_cutoff);
}

model_fog_band const *distance_shading::apply(palette &target, int32_t const distance, int16_t const native_class, int16_t const radius, bool const split_highlights) const {
  auto const &mapping{bindings.at(static_cast<size_t>(native_class))};
  if(mapping.empty()) return nullptr;
  auto const level{band(std::max(0, distance), native_class, radius)};
  for(auto const &binding : mapping) {
    auto const range{bands.at(binding.environment_column).at(level)};
    auto &ramp{target.shade_ramps.at(binding.palette_slot)};
    ramp.clear();
    if(split_highlights) {
      auto const trim{range.count >> 2};
      append_range(ramp, range.first, range.count - trim - 1);
      auto const highlight{highlights.at(level)};
      append_range(ramp, highlight.first + highlight.count - trim, trim + 1);
    } else {
      append_range(ramp, range.first, range.count);
    }
  }
  return model_bands.empty() ? nullptr : &model_bands.at(native_class).at(level);
}

} // namespace earthsiege

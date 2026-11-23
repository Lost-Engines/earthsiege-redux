#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <iostream>
#include <limits>
#include "shell/game_state.h"
#include "shell/mission_view.h"

namespace {
using namespace earthsiege;

void require(bool condition, char const *message) {
  if(!condition) throw std::runtime_error{message};
}

void check_random() {
  auto rolls{random_state::seeded(42)};
  std::mt19937 reference{42};
  require(!rolls.replaying(), "seeded Redux engine");
  for(unsigned i{0}; i < 128; ++i) {
    require(rolls.next() == std::bit_cast<int16_t>(static_cast<uint16_t>(reference())), "full random word");
    require(rolls.bounded(0) == 0, "empty random range");
    for(auto bound : std::array<int16_t, 3>{7, -5, -32768}) {
      auto const max{static_cast<uint32_t>(std::abs(static_cast<int>(bound)) - 1)};
      require(rolls.bounded(bound) == static_cast<int>(std::uniform_int_distribution<uint32_t>{0, max}(reference)), "bounded gameplay distribution");
    }
    require(rolls.inclusive(UINT32_MAX) == std::uniform_int_distribution<uint32_t>{0, UINT32_MAX}(reference), "full burst range");
  }
}

void check_bursts(resource_store const &resources) {
  auto const groups{read_debris(resources.read("simvol0", "dat/DEBRIS.DAT"))};
  auto rolls{random_state::seeded(29)};
  for(auto const &group : groups) {
    for(double multiplier : {0.0, 0.75, 1.0, 2.0}) {
      auto const maximum{static_cast<unsigned>((group.count_limit - 1) * multiplier)};
      std::vector<bool> seen(maximum + 1);
      for(int i{0}; i < 512; ++i) seen.at(create_debris_burst(group, {}, rolls, multiplier, 150).size()) = true;
      require(std::ranges::find(seen, false) == seen.end(), "every scaled burst size is reachable");
    }
  }
  auto const &heavy{groups.back()};
  world_effects defaults{resources};
  require(defaults.live_particles().available() == 600, "default fragment pool size");
  auto expected_rolls{random_state::seeded(29)};
  auto default_rolls{random_state::seeded(29)};
  auto const expected{create_debris_burst(heavy, {}, expected_rolls, 4.0, 600).size()};
  defaults.add_debris(static_cast<int16_t>(groups.size() - 1), {}, default_rolls);
  require(defaults.live_particles().size() == expected, "default fragment burst density");
  require(create_debris_burst(heavy, {}, rolls, std::numeric_limits<double>::max(), 2).size() == 2, "huge multiplier respects free slots");
  require(create_debris_burst(heavy, {}, rolls, 4, 0).empty(), "full pool");

  random_state recorded{0}, tuned{0};
  for(int i{0}; i < 100; ++i) {
    create_debris_burst(heavy, {}, recorded);
    create_debris_burst(heavy, {}, tuned, 4, i % 2 ? 2 : 200);
    require(recorded.snapshot().words == tuned.snapshot().words && recorded.snapshot().first == tuned.snapshot().first, "tuned bursts preserve the pilot's tape");
  }
}

void check_mission(resource_store const &resources) {
  game_state shell{resources};
  require(!shell.presentation_random().replaying(), "interactive shell uses Redux generator");
  shell.new_campaign(resources.edition() == game_edition::expansion ? 1 : 0);
  auto mission{shell.launch({.pool_size{2}, .count_multiplier{0}})};
  auto &world{mission->visual_effects()};
  auto &rolls{mission->interface_random()};
  require(!rolls.replaying(), "interactive mission uses Redux generator");
  world.add_debris(3, {}, rolls);
  require(world.live_particles().size() == 0 && world.live_particles().available() == 2, "campaign fragment settings");
  require(world.add_detached_weapon({}).has_value() && world.add_detached_weapon({}).has_value(), "lost weapons still appear");
  require(!world.add_detached_weapon({}), "lost weapons share the cap");
  for(int i{0}; i < 64 && world.live_particles().size(); ++i) {
    world.advance(256);
    world.collect();
  }
  require(world.live_particles().available() == 2 && world.add_detached_weapon({}).has_value(), "expired slots can be reused");

  world_effects disabled{resources, {}, false, {.pool_size{0}, .count_multiplier{10}}};
  disabled.add_debris(3, {}, rolls);
  require(disabled.live_particles().size() == 0 && !disabled.add_detached_weapon({}), "zero pool suppresses both visuals");
  world_effects large{resources, {}, false, {.pool_size{UINT32_MAX}}};
  require(large.live_particles().available() == UINT32_MAX && large.add_detached_weapon({}) == 0, "large pool allocates on demand");
  mission_view inspect{resources, 1};
  require(!inspect.simulation().interface_random().replaying(), "interactive demo inspection uses Redux generator");
}
}

int main(int argc, char **argv) try {
  if(argc != 2) return EXIT_FAILURE;
  check_random();
  std::filesystem::path const root{argv[1]};
  for(auto const &path : {root, root / "expansion"}) {
    earthsiege::resource_store resources{path};
    check_bursts(resources);
    check_mission(resources);
  }
  std::cout << "Redux randomness, fragment counts, pool limits and recorded bursts passed in both editions\n";
  return EXIT_SUCCESS;
} catch(std::exception const &e) {
  std::cerr << e.what() << '\n';
  return EXIT_FAILURE;
}

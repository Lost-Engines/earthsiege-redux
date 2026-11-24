#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include "shell/game_state.h"
#include "shell/mission_view.h"
#include "simulation/model_math.h"

namespace {
using namespace earthsiege;

void check(bool condition, char const *message) {
  if(!condition) throw std::runtime_error{message};
}

void armour() {
  damage_definition const section{{100, 100}, {{.armour{100}, .cell_sequence{-1}, .dependents{{1, 0}, {1, 1}}}}};
  struct result { int16_t hit, armour, first, second; };
  for(auto const entry : std::array<result, 5>{{{9, 99, 0, 0}, {10, -1, 0, 0}, {15, -1, 5, 0}, {75, -1, 65, 0}, {115, -1, -1, 5}}}) {
    component_damage damage{section, {{0, 0}, {90}, {1}, {}, {}}};
    damage.apply_section(0, entry.hit, [](int16_t){return 0;});
    auto const &s{damage.state()};
    check(s.armour[0] == entry.armour, "armour break threshold");
    check(s.internal[0] == entry.first, "first internal receives excess");
    check(s.internal[1] == entry.second, "next internal receives remaining excess");
  }
  int16_t broken{-1}, hit{15};
  check(accumulate_damage(hit, 100, broken) && hit == 15, "already destroyed armour passes the whole hit");
  int16_t current{29999}, heavy{20000};
  check(accumulate_damage(heavy, 30000, current) && heavy == 19999, "wide intermediate on a heavy hit");
}

void bullet_clock(game_edition edition) {
  projectile_motions motions{{{.lifetime{4}}}, {}, edition};
  for(auto const [ratio, expiry] : std::array<std::pair<frame_ratio, int>, 3>{{{64, 17}, {256, 5}, {512, 3}}}) {
    projectile_flight flight;
    projectile_guidance guidance;
    for(int step{1}; step <= expiry; ++step) {
      auto const result{advance_projectile(flight, motions, 1000, {}, ratio, guidance)};
      check(result == (step == expiry ? projectile_result::expired : projectile_result::flying), "live bullet lifetime");
    }
  }
}

void scanner(resource_store const &resources) {
  // Original floppy and April executables draw this contact at (51,39) under both scene rotations.
  scanner_display display{resources};
  rectangle const bounds{16, 18, 87, 63};
  scanner_display_state state{{}, static_cast<uint16_t>(2048 - 8192), 0, 21000, true, {}};
  framebuffer empty, first, second;
  display.draw(empty, bounds, state);
  state.contacts.push_back({combat_actor_kind::herc, 0, {1000, 2000}, {}, true, true, false, false});
  display.draw(first, bounds, state);
  display.draw(second, bounds, state, rotation_transform({0, 0, angle_quarter_turn}));
  auto const pixel{39 * screen_width + 51};
  check(empty.pixels()[pixel] != 50 && first.pixels()[pixel] == 50, "patched scanner contact position");
  check(first.pixels() == second.pixels(), "scanner contacts independent of scene rotation");
}

void radio() {
  radio_controller receiver;
  radio_descriptor descriptor{.maximum{2}, .expiry{5}};
  receiver.queue().push(descriptor, 1, {}, 0);
  receiver.queue().push(descriptor, 2, {}, 0);
  std::vector<uint32_t> started;
  radio_presentation_callbacks callbacks{
    .start{[&](radio_message const &m){started.push_back(m.text);}},
    .show{[](auto const &){return true;}},
    .finish{[](auto const &){return true;}}
  };
  receiver.advance(1, true, callbacks);
  receiver.cancel();
  receiver.advance(2, true, callbacks);
  receiver.advance(3, true, callbacks);
  check(started == std::vector<uint32_t>{1, 2}, "second radio call opens after an interrupted first call");
}

void orders(resource_store const &resources) {
  auto const tape{read_input_tape(read_file(resources.root() / "TAPES/DEMO2.TAP"), resources.edition())};
  auto const script{read_mission_script(tape.starting_files[std::to_underlying(tape_file::script)])};
  auto const roster{read_player_roster(tape.starting_files[std::to_underlying(tape_file::player)], tape.edition)};
  mission_session mission{resources, script, roster};
  auto const map{mission.tactical_state(0)};
  auto const wing{mission.wingmate_ids().at(0)};
  auto const issuer{mission.player_id()};
  auto const friendly{std::ranges::find(map.contacts, issuer, &tactical_contact::actor)};
  check(friendly != map.contacts.end() && map.projection_shift == 8, "live campaign tactical map");
  tactical_controller controller{mission.rules()};
  controller.wingmate(0);
  controller.choose(12);
  // Place the issuer outside the MFD selection radius, then retask this wingmate to an empty point.
  check(controller.destination({}, issuer, map.contacts, {4000000, 0, 0}, 100), "remote friendly selection");
  controller.commit();
  check(mission.transmit_order(controller.packet(issuer), wing) && mission.order_state(wing).followed == issuer, "DEFEND follows the moving actor");
  vec3<int32_t> const point{123456, 654321, 0};
  check(controller.destination(point, {}, map.contacts, {}, 100), "replace actor with ground point");
  controller.commit();
  check(mission.transmit_order(controller.packet(issuer), wing), "ground DEFEND accepted");
  auto const next{mission.order_state(wing)};
  check(!next.followed && next.command_position == vec2<int32_t>{point.x, point.y}, "DEFEND releases previous actor");
  mission.transmit_order({.command{5}, .issuer{issuer}, .target{}}, wing);
  check(mission.order_state(wing).weapons_free, "WEAPONS FREE retained by wingmate");
  mission.transmit_order({.command{8}, .issuer{issuer}, .target{}}, wing);
  check(!mission.order_state(wing).weapons_free, "hold fire cancels WEAPONS FREE");
  mission_view playback{resources, tape};
  check(playback.simulation().rules() == dos_mission_rules(tape.edition), "tape restores executable rules");
  check(playback.simulation().tactical_state(0).projection_shift == (tape.edition == game_edition::original ? 7U : 8U), "recorded tactical map");
}

void campaign(resource_store const &resources) {
  bool const expansion{resources.edition() == game_edition::expansion};
  game_state shell{resources, 17};
  shell.new_campaign(expansion ? 1 : 0);
  auto const mission{shell.launch()};
  check(mission->rules() == mission_rules::redux, "campaign launch selects live rules");
  mission_view inspect{resources, 2};
  check(inspect.simulation().rules() == mission_rules::redux, "interactive demo inspection selects live rules");

  auto const weapons{read_weapon_catalogue(resources)};
  auto const definition{read_herc_definition(resources, "ROADRUNN")};
  auto const models{read_model(resources.read("simvol0", "dts/MECHWPNS.DTS"))};
  std::array<int16_t, 9> mounts;
  mounts.fill(empty_weapon_slot);
  herc_actor chassis{definition, weapons, models, mounts, {}, {}};
  auto &shields{chassis.power().shields};
  shields.front = shields.rear = 0;
  chassis.update_power(frame_ratio_one);
  check(shields.front + shields.rear == (expansion ? 5 : 4), "edition-specific shield recharge");
}
}

int main(int argc, char **argv) try {
  if(argc != 2) return EXIT_FAILURE;
  armour();
  radio();
  std::filesystem::path const root{argv[1]};
  for(auto const &path : {root, root / "expansion"}) {
    earthsiege::resource_store resources{path};
    bullet_clock(resources.edition());
    scanner(resources);
    orders(resources);
    campaign(resources);
  }
  return EXIT_SUCCESS;
} catch(std::exception const &e) {
  std::cerr << e.what() << '\n';
  return EXIT_FAILURE;
}

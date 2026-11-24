#include <iostream>
#include "render/cockpit_surround.h"
#include "resources/name_table.h"
#include "shell/mission_view.h"

using namespace earthsiege;
namespace {

void check_art(resource_store const &resources, std::string const &name) {
  auto const cockpit{read_cockpit(resources, name)};
  cockpit_surround art{cockpit};
  framebuffer world{{640, 200}, 17};
  art.draw(world, world.size().cockpit_panel(), {0,0});
  for(bool right : {false, true}) {
    auto const view{right ? cockpit_view::right : cockpit_view::left};
    framebuffer expected;
    cockpit.draw_composed(expected, view);
    auto const &window{cockpit.windows[static_cast<size_t>(view)]};
    for(int y{0}; y < 200; ++y) for(int x{0}; x < 160; ++x) {
      int const sx{right ? x : x + 160};
      auto const ink{window.bounds.contains(sx,y) && window.mask.contains(sx,y) ? 17 : expected.pixels()[y * 320 + sx]};
      if(world.pixels()[y * 640 + (right ? x + 480 : x)] != ink) throw std::runtime_error{"side glass or artwork differs on " + name};
    }
  }
  framebuffer forward;
  cockpit.draw_composed(forward, cockpit_view::forward);
  auto const &front{cockpit.windows[0]};
  for(int pan : {-320, 320}) {
    world.clear(17);
    auto const panel{world.size().cockpit_panel({.pan = pan})};
    art.draw(world, panel, {pan, 0});
    int const start{pan < 0 ? 320 : 0};
    for(int y{0}; y < 200; ++y) for(int x{0}; x < 320; ++x) {
      auto const ink{front.bounds.contains(x,y) && front.mask.contains(x,y) ? 17 : forward.pixels()[y * 320 + x]};
      if(world.pixels()[y * 640 + start + x] != ink) throw std::runtime_error{"side view lost the adjoining forward panel on " + name};
    }
    auto gauges{art.forward_panel()};
    gauges.pixel(100, 30, 75);
    gauges.pixel(12, 184, 76);
    art.draw(world, panel, {pan, 0}, nullptr, 0, &gauges);
    if(world.pixels()[30 * 640 + start + 100] != 75 || world.pixels()[184 * 640 + start + 12] != 76)
      throw std::runtime_error{"forward HUD or dashboard missing from the side view on " + name};
  }
  framebuffer wide{{640, 400}, 17};
  int const lower_y{cockpit.windows[1].instrument_origin[1]};
  auto const panel{wide.size().cockpit_panel({.scale = cockpit_fov_zoom(90), .lower = lower_y})};
  auto lower{art.lower_panel()};
  lower.pixel(50, 20, 75);
  art.draw(wide, panel, {0, 0}, nullptr, 0, nullptr, &lower);
  int const x{panel.left + render_size::edge(50, panel.width(), 320)};
  int const y{panel.top + render_size::edge(lower_y + 20, panel.height(), 200)};
  if(wide.pixels()[y * wide.size().width + x] != 75)
    throw std::runtime_error{"lower console missing from the forward view on " + name};
}

void check_fov() {
  for(render_size const size : {render_size{320, 200}, render_size{640, 400}, render_size{960, 540}}) {
    auto const base{size.cockpit_panel()};
    auto const panel{size.cockpit_panel({.scale = cockpit_fov_zoom(90), .lower = 197, .sight_y = 72})};
    if(panel.width() != std::lround(base.width() * .625) || panel.height() != std::lround(base.height() * .625))
      throw std::runtime_error{"90-degree cockpit dimensions"};
    camera_projection const view{.centre = {160, 72}, .output = size,
                                 .plane = projection_plane::through_panel(size, panel, {160, 72})};
    for(int offset : {-256, 0, 256}) {
      auto const target{project_point({offset, 1024, offset}, view, projection_source::model)};
      int const x{panel.left + static_cast<int>(std::lround((160 + offset / 4) * panel.width() / 320.))};
      int const y{panel.top + static_cast<int>(std::lround((72 - offset / 4) * panel.height() / 200.))};
      if(target.x != x || target.y != y) throw std::runtime_error{"target and cockpit sight have different focal lengths"};
      if(offset == 0 && (std::abs(target.x - size.width / 2) > 1 || std::abs(target.y - std::lround(base.height() * 72 / 200.)) > 1))
        throw std::runtime_error{"changing the FOV moved the aim point"};
    }
  }
}

void check_lower_controls(resource_store const &resources) {
  mission_view scene{resources, 1, {}, {.zoom{cockpit_fov_zoom(110)}}};
  render_size const size{640, 400};
  scene.render_resolution(size);
  scene.draw();
  auto const names{read_resource_names(resources.read("simvol0", "nam/MECHS.NAM"))};
  auto const cockpit{read_cockpit(resources, names.at(static_cast<size_t>(scene.simulation().player_chassis())))};
  auto const parent{cockpit.layout.bounds(0x4bc)}, damage{cockpit.layout.bounds(0x51c)};
  int const x{parent.left + (damage.left + damage.right) / 2}, y{parent.top + (damage.top + damage.bottom) / 2};
  auto const panel{size.cockpit_panel(scene.image().cockpit_viewport())};
  if(y < 200 || panel.top + render_size::edge(y, panel.height(), 200) >= size.height)
    throw std::runtime_error{"test requires the lower damage button to be visible from the front"};
  scene.mouse_position(x, y);
  scene.mouse_button(false, true);
  scene.mouse_button(false, false);
  tape_frame input;
  input.delta = 256;
  scene.step_recorded(input, 100);
  scene.draw();
  if(scene.map_refresh().damage_deadline <= 100) throw std::runtime_error{"lower damage button did not respond in the forward view"};
  auto const dialog{scene.image().dialog_backdrop()};
  framebuffer before{size}, after{size};
  scene.image().blit_presentation(before, size.bounds());
  dialog.blit_presentation(after, size.bounds());
  if(dialog.cockpit_viewport().scale != 1 || before.pixels() != after.pixels())
    throw std::runtime_error{"opening a dialog moved or rescaled the cockpit behind it"};
}

void check_brackets() {
  render_size const size{808, 400};
  auto const panel{size.cockpit_panel({.down = 100, .scale = .5, .lower = 197})};
  for(bool locked : {false, true}) for(auto [x, y] : std::array<std::pair<int, int>, 4>{{{-12,40}, {308,40}, {100,-12}, {100,188}}}) {
    rectangle const mark{x, y, x + 24, y + 24};
    framebuffer world{size}, glyph{{25,25}};
    draw_side_target_brackets(world, panel, mark, locked, 1);
    draw_target_brackets(glyph, glyph.size().bounds(), locked, 1);
    for(int j{0}; j < 25; ++j) for(int i{0}; i < 25; ++i) {
      int const px{panel.left + x + i}, py{panel.top + y + j};
      auto const expected{panel.contains(px,py) ? 0 : glyph.pixels()[j * 25 + i]};
      if(world.pixels()[py * size.width + px] != expected) throw std::runtime_error{"target marker differs across the window seam"};
    }
  }
}

void check_banked_sky() {
  render_size const size{640, 200};
  for(auto edition : {game_edition::original, game_edition::expansion}) for(auto roll : std::array<uint16_t, 2>{1024, 63488}) {
    world_environment environment;
    environment.edition = edition;
    environment.background = {4, 16, 3, 8, 32, 3, 8, 0};
    background_view view;
    view.roll = roll;
    view.plane = projection_plane::cockpit(size, view.centre, cockpit_fov_zoom(170));
    view.bounds = view.plane.bounds();
    framebuffer world{size, 251};
    draw_background_bands(world, make_background_bands(environment, view), view.bounds, view.plane);
    auto const &raster{world.pixels()};
    for(int x : {size.width / 4, size.width / 2, 3 * size.width / 4}) {
      if(raster[size.width + x] != 16 || raster[(size.height - 2) * size.width + x] != 32)
        throw std::runtime_error{"banked background at x=" + std::to_string(x) + " has edge colours " +
          std::to_string(raster[size.width + x]) + " and " + std::to_string(raster[(size.height - 2) * size.width + x])};
    }
  }
}

void check_pans(resource_store const &resources, double fov) {
  render_size const size{541, 240};
  mission_view scene{resources, 1, {}, {.zoom{cockpit_fov_zoom(fov)}}};
  scene.render_resolution(size);
  scene.draw();
  uint32_t clock{};
  for(auto command : std::array<uint16_t,6>{65,59,67,68,68,67}) {
    unsigned count{};
    for(int i{0}; i < 6; ++i) {
      tape_frame input;
      input.delta = 256;
      input.command = i == 0 ? command : 0;
      if(!scene.step_recorded(input, clock += 6)) throw std::runtime_error{"unexpected mission exit"};
      scene.draw();
      auto pans{scene.take_pan_frames()};
      if(pans.empty()) continue;
      for(auto const &pan : pans) if(!pan.has_surround()) throw std::runtime_error{"side windows vanished during a pan"};
      framebuffer last{size}, steady{size};
      pans.back().blit_presentation(last, size.bounds());
      scene.image().blit_presentation(steady, size.bounds());
      if(last.pixels() != steady.pixels()) throw std::runtime_error{"pan did not finish on the full incoming cockpit"};
      ++count;
    }
    if(count != 1) throw std::runtime_error{"missing widescreen cockpit transition"};
  }
  scene.render_resolution({320,240});
  scene.draw();
  if(!scene.image().has_surround() || scene.image().cockpit_viewport().scale >= 1)
    throw std::runtime_error{"cockpit lost the requested FOV after resizing to 4:3"};
}
}

int main(int argc, char **argv) try {
  if(argc != 3) return 1;
  check_fov();
  check_brackets();
  check_banked_sky();
  for(auto path : {argv[1], argv[2]}) {
    resource_store resources{path};
    for(auto name : {"ROADRUNN","GUNSLING","RAPTOR","TOMAHAWK","PATRIOT","PANTHER","HERCULES","COLOSSUS"}) check_art(resources, name);
    if(resources.edition() == game_edition::expansion) check_art(resources, "APOCA");
    check_lower_controls(resources);
    for(double fov : {85., 170.}) check_pans(resources, fov);
  }
  return 0;
} catch(std::exception const &e) {
  std::cerr << e.what() << '\n';
  return 1;
}

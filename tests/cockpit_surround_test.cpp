#include <iostream>
#include "render/cockpit_surround.h"
#include "shell/mission_view.h"

using namespace earthsiege;
namespace {

void check_art(resource_store const &resources, std::string const &name) {
  auto const cockpit{read_cockpit(resources, name)};
  cockpit_surround art{cockpit};
  framebuffer world{{640, 200}, 17};
  art.draw_sides(world, {0,0});
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
    art.draw_sides(world, {pan, 0});
    int const start{pan < 0 ? 320 : 0};
    for(int y{0}; y < 200; ++y) for(int x{0}; x < 320; ++x) {
      auto const ink{front.bounds.contains(x,y) && front.mask.contains(x,y) ? 17 : forward.pixels()[y * 320 + x]};
      if(world.pixels()[y * 640 + start + x] != ink) throw std::runtime_error{"side view lost the adjoining forward panel on " + name};
    }
    auto gauges{art.forward_panel()};
    gauges.pixel(100, 30, 75);
    gauges.pixel(12, 184, 76);
    art.draw_sides(world, {pan, 0}, nullptr, 0, &gauges);
    if(world.pixels()[30 * 640 + start + 100] != 75 || world.pixels()[184 * 640 + start + 12] != 76)
      throw std::runtime_error{"forward HUD or dashboard missing from the side view on " + name};
  }
}

void check_brackets() {
  render_size const size{808, 400};
  auto const panel{size.cockpit_panel(0)};
  for(bool locked : {false, true}) for(int x : {-12, 308}) {
    rectangle const mark{x, 40, x + 24, 64};
    framebuffer world{size}, glyph{{25,25}};
    draw_side_target_brackets(world, panel, mark, locked, 1);
    draw_target_brackets(glyph, glyph.size().bounds(), locked, 1);
    for(int y{0}; y < 25; ++y) for(int i{0}; i < 25; ++i) {
      int const px{(x + i) * 2 + panel.left}, py{(40 + y) * 2};
      auto const expected{panel.contains(px,py) ? 0 : glyph.pixels()[y * 25 + i]};
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

void check_pans(resource_store const &resources) {
  render_size const size{541, 240};
  mission_view scene{resources, 1, {}, {.zoom{cockpit_fov_zoom(85)}}};
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
  if(scene.image().has_surround()) throw std::runtime_error{"side windows retained after resizing to 4:3"};
}
}

int main(int argc, char **argv) try {
  if(argc != 3) return 1;
  check_brackets();
  check_banked_sky();
  for(auto path : {argv[1], argv[2]}) {
    resource_store resources{path};
    for(auto name : {"ROADRUNN","GUNSLING","RAPTOR","TOMAHAWK","PATRIOT","PANTHER","HERCULES","COLOSSUS"}) check_art(resources, name);
    if(resources.edition() == game_edition::expansion) check_art(resources, "APOCA");
    check_pans(resources);
  }
  return 0;
} catch(std::exception const &e) {
  std::cerr << e.what() << '\n';
  return 1;
}

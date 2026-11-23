#include <cstdlib>
#include <fstream>
#include <iostream>
#include "render/briefing_map.h"
#include "resources/resource_store.h"
int main(int argc, char **argv) try {
  using earthsiege::binary_reader;
  using earthsiege::briefing_map;
  using earthsiege::crc32;
  using earthsiege::framebuffer;
  using earthsiege::read_file;
  using earthsiege::read_mission_script;
  using earthsiege::resource_store;
  if(argc != 3) return EXIT_FAILURE;
  resource_store resources{argv[1]};
  auto raw{read_file(argv[2])};
  binary_reader f{raw};
  auto magic{f.take(8)};
  if(std::string(magic.begin(), magic.end()) != "ESBRFM02") f.fail("fixture version");
  auto const cases{f.integer<uint32_t>()};
  unsigned failures{0};
  unsigned frames{0};
  auto vec{[&]{return vec3<int32_t>{f.integer<int32_t>(), f.integer<int32_t>(), f.integer<int32_t>()};}};
  for(unsigned c{0}; c < cases; ++c) {
    auto script{read_mission_script(f.take(f.integer<uint32_t>()))};
    auto count{f.integer<uint16_t>()};
    std::vector<std::string> text;
    auto n{f.integer<uint16_t>()};
    for(unsigned j{0}; j < n; ++j) text.push_back(f.cstring());
    briefing_map map{resources, script, count, text};
    auto const &g{map.geometry()};
    for(auto const &p : g.players) {
      if(vec() != p) f.fail("player formation " + std::to_string(c));
    }
    if(vec() != g.full_view || vec() != g.squad_view) f.fail("fitted camera " + std::to_string(c));
    if(vec() != g.minimum || vec() != g.maximum) f.fail("mission bounds " + std::to_string(c));
    auto poses{f.integer<uint32_t>()};
    for(unsigned i{0}; i < poses; ++i) {
      auto action{f.integer<uint32_t>()};
      if(action == 0x9c64) {
        map.restore_view();
      } else if(action) map.navigate(action == 0xd43c ? 108 : action == 0xd469 ? 109 : action == 0xd4b8 ? 1503 : 1506);
      auto eye{vec()};
      vec2<int32_t> pan{f.integer<int32_t>(), f.integer<int32_t>()};
      auto crc{f.integer<uint32_t>()};
      if(eye != map.state().camera || pan != map.state().pan) f.fail("navigation " + std::to_string(c) + "/" + std::to_string(i));
      framebuffer frame;
      frame.clear(37);
      map.draw(frame);
      ++frames;
      if(crc32(frame.pixels()) != crc) {
        {
          std::ofstream out{"/tmp/es-briefing-difference-" + std::to_string(c) + "-" + std::to_string(i) + ".raw", std::ios::binary};
          out.write(reinterpret_cast<char const *>(frame.pixels().data()), frame.pixels().size());
        }
        ++failures;
        std::cerr << "frame " << c << "/" << i << " differs\n";
      }
      if(c == 0 && i == 0) {
        earthsiege::render_size const size{640, 400};
        framebuffer page;
        page.clear(37);
        map.draw(page, briefing_map::map_bounds, size);
        auto const image{page.resampled(size)};
        if(!page.has_detail() || image.pixels().front() != 37) f.fail("briefing scenery window");
        auto const &pixels{image.pixels()};
        bool fine_edge{false};
        auto const bounds{briefing_map::map_bounds};
        for(int y{bounds.top * 2}; y < bounds.bottom * 2 && !fine_edge; y += 2) {
          for(int x{bounds.left * 2}; x < bounds.right * 2; x += 2) {
            auto const index{y * size.width + x};
            if(pixels[index] != pixels[index + size.width + 1]) {
              fine_edge = true;
              break;
            }
          }
        }
        if(!fine_edge) f.fail("briefing scenery has no detail inside a logical pixel");
      }
    }
  }
  f.require_end();
  if(failures) f.fail(std::to_string(failures) + " briefing maps differ");
  std::cout << frames << " original briefing-map frames and navigation states match\n";
  return EXIT_SUCCESS;
} catch(std::exception const &e) {
  std::cerr << e.what() << '\n';
  return EXIT_FAILURE;
}

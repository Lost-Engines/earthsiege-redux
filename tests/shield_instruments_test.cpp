#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "render/shield_instruments.h"

int main(int argc, char **argv) try {
  using earthsiege::binary_reader;
  using earthsiege::crc32;
  using earthsiege::framebuffer;
  using earthsiege::palette;
  using earthsiege::read_cockpit_layout;
  using earthsiege::read_file;
  using earthsiege::resource_store;
  using earthsiege::shield_instruments;
  using earthsiege::update_shield_colours;
  if(argc != 3) return EXIT_FAILURE;
  resource_store resources{argv[1]};
  auto const bytes{read_file(argv[2])};
  binary_reader file{bytes};
  auto const magic{file.take(8)};
  if(std::string(magic.begin(), magic.end()) != "ESSHLD01") file.fail("fixture version");
  auto const count{file.integer<uint32_t>()};
  unsigned int failed{0};
  // The shared base-game fixture includes its six RGB6 colours after each case.
  constexpr size_t base_palette_bytes{6 * 3};
  for(unsigned int i{0}; i < count; ++i) {
    auto const name{file.cstring()};
    file.take(2 * sizeof(int32_t));  // Charge affects the palette only.
    auto const balance{file.integer<int32_t>()};
    std::array<unsigned int, 2> const faces{file.integer<uint8_t>(), file.integer<uint8_t>()};
    auto const expected{file.integer<uint32_t>()};
    auto const layout{read_cockpit_layout(resources.read("simvol0", "gau/" + name + ".GAU"))};
    shield_instruments instruments{resources, layout};
    framebuffer frame;
    frame.clear(123);
    instruments.draw(frame, balance, faces);
    if(crc32(frame.pixels()) != expected) {
      ++failed;
      if(failed < 12) std::cerr << name << " balance " << balance << " differs: " << crc32(frame.pixels()) << " expected " << expected << '\n';
    }
    file.take(base_palette_bytes);
  }
  auto const palette_count{file.integer<uint32_t>()};
  palette colours;
  std::vector<uint8_t> expansion_dac;
  for(unsigned int i{0}; i < palette_count; ++i) {
    auto const front{file.integer<int32_t>()};
    auto const rear{file.integer<int32_t>()};
    file.take(base_palette_bytes);
    update_shield_colours(colours, front, rear);
    for(size_t slot{80}; slot < 86; ++slot) {
      auto const &rgb{colours.dac_colours[slot]};
      expansion_dac.insert(expansion_dac.end(), rgb.begin(), rgb.end());
    }
  }
  file.require_end();
  // RGB6 bytes from all six slots, identical in the expansion CD, floppy and patch.
  if(crc32(expansion_dac) != 0xce6cffe5u) throw std::runtime_error{"expansion shield DAC values differ"};
  if(failed) throw std::runtime_error{std::to_string(failed) + " shield frames differ"};
  std::cout << count << " base-game shield frames and " << palette_count << " expansion shield palettes match\n";
  return EXIT_SUCCESS;
} catch(std::exception const &e) {
  std::cerr << e.what() << '\n';
  return EXIT_FAILURE;
}

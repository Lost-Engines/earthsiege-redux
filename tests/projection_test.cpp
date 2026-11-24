#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include "render/projection.h"
#include "resources/binary_reader.h"

namespace {

template<typename T>
vec2<T> read_pair(earthsiege::binary_reader &file) {
  return {file.integer<T>(), file.integer<T>()};
}

} // anonymous namespace

auto main(int const argc, char **argv)->int try {
  if(argc != 2) return EXIT_FAILURE;
  auto const data{earthsiege::read_file(argv[1])};
  earthsiege::binary_reader file{data};
  auto const magic{file.take(8)};
  if(std::string(magic.begin(), magic.end()) != "ESPROJE1") file.fail("fixture version");
  auto const cases{file.integer<uint32_t>()};
  for(unsigned int row{0}; row != cases; ++row) {
    auto const source{static_cast<earthsiege::projection_source>(file.integer<uint8_t>())};
    earthsiege::camera_projection const view{
      .perspective_shift{file.integer<uint8_t>()},
      .near_distance{file.integer<int32_t>()},
      .orthographic_divisor{file.integer<int32_t>()},
      .centre{read_pair<int32_t>(file)},
    };
    auto const count{file.integer<uint16_t>()};
    std::vector<earthsiege::camera_vertex> vertices;
    for(unsigned int index{0}; index != count; ++index) {
      vertices.push_back({
        .position{file.integer<int32_t>(), file.integer<int32_t>(), file.integer<int32_t>()},
        .texture{read_pair<int32_t>(file)},
        .shade{file.integer<uint16_t>()},
      });
    }
    auto const projected{earthsiege::project_polygon(vertices, view, source)};
    auto const size{file.integer<uint16_t>()};
    if(projected.size() != size) throw std::runtime_error{"clipped vertex count differs in row " + std::to_string(row)};
    for(unsigned int index{0}; index != size; ++index) {
      auto const point{read_pair<int32_t>(file)};
      auto const uv{read_pair<int32_t>(file)};
      auto const shade{file.integer<uint16_t>()};
      auto const &result{projected[index]};
      if(result.position != point || result.texture != uv || result.shade != shade) throw std::runtime_error{"projection/attributes differ in row " + std::to_string(row) + " vertex " + std::to_string(index)};
    }
  }
  file.require_end();
  if(cases != 3200) file.fail("incomplete projection fixture");
  earthsiege::camera_projection enlarged{.output{640, 400}};
  auto const fine{earthsiege::project_point({1, 512, 1}, enlarged, earthsiege::projection_source::model)};
  if(fine != vec2<int32_t>{321, 199}) throw std::runtime_error{"lost subpixel geometry before output projection"};
  for(auto const size : {earthsiege::render_size{640, 400}, {800, 400}, {1279, 419}}) {
    auto const panel{size.fit({})};
    earthsiege::camera_projection camera{.centre{160, 72}, .output{size}};
    camera.plane = earthsiege::projection_plane::cockpit(size, camera.centre, earthsiege::cockpit_fov_zoom(90));
    auto const point{[&](int x, int y, int z){return earthsiege::project_point({x,y,z}, camera, earthsiege::projection_source::model);}};
    if(point(-1024,1024,0).x != panel.left || point(1024,1024,0).x != panel.right + 1 ||
       point(0,1024,0).y != std::lround(72.0 * size.height / 200)) throw std::runtime_error{"90-degree field or reticle alignment differs"};
    std::array<earthsiege::camera_vertex, 3> const tri{{
      {{0, 128, 0}, {}, 0}, {{256, 384, 0}, {}, 0}, {{0, 384, 128}, {}, 0},
    }};
    auto const clipped{earthsiege::project_polygon(tri, camera, earthsiege::projection_source::model)};
    if(clipped.size() != 4 || clipped[1].position != point(128,256,0)) throw std::runtime_error{"near-plane intersection ignores the field of view"};
  }
  std::cout << cases << " native polygon projections and clipped texture/shade attributes agree" << std::endl;
  return EXIT_SUCCESS;
} catch(std::exception const &error) {
  std::cerr << error.what() << std::endl;
  return EXIT_FAILURE;
}

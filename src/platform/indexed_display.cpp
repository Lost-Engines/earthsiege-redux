#define GL_GLEXT_PROTOTYPES
#define GLFW_INCLUDE_GLEXT
#include "indexed_display.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include "viewport.h"
#include "resources/artwork.h"

namespace earthsiege {
namespace {
template<typename Function>
Function entry(char const *name) {
  auto const proc{glfwGetProcAddress(name)};
  if(!proc) throw std::runtime_error{std::string{"OpenGL entry point unavailable: "} + name};
  return reinterpret_cast<Function>(proc);
}
// Windows only exports GL 1.1 functions directly.
#define GL_ENTRY(name) entry<decltype(&name)>(#name)

constexpr char const *fragment_source{R"GLSL(#version 120
uniform sampler2D page, coverage, world, palette;
uniform ivec2 canvasSize, pageSize, worldSize;
uniform ivec4 viewRect;

vec2 texelCentre(ivec2 pixel, ivec2 size) {
  return (vec2(pixel) + 0.5) / vec2(size);
}

void main() {
  // Reproduce both sampling steps: the fixed render canvas can itself be
  // scaled to fit the window. The HUD uses integer cell boundaries within it.
  ivec2 dst = ivec2(int(gl_FragCoord.x) - viewRect.x, viewRect.y - int(gl_FragCoord.y));
  ivec2 pixel = ((2 * dst + 1) * canvasSize) / (2 * viewRect.zw);
  vec2 cell = texelCentre(pixel * pageSize / canvasSize, pageSize);
  float index = texture2D(page, cell).r;
  if (worldSize.x > 0 && texture2D(coverage, cell).r > 0.0) {
    ivec2 worldPixel = pixel;
    if (any(notEqual(worldSize, canvasSize)))
      worldPixel = pixel * worldSize / canvasSize;
    index = texture2D(world, texelCentre(worldPixel, worldSize)).r;
  }
  gl_FragColor = texture2D(palette, vec2((index * 255.0 + 0.5) / 256.0, 0.5));
}
)GLSL"};
}

indexed_display::indexed_display()
  : active_texture{GL_ENTRY(glActiveTexture)}, uniform_size{GL_ENTRY(glUniform2i)},
    uniform_rect{GL_ENTRY(glUniform4i)}, delete_program{GL_ENTRY(glDeleteProgram)} {
  auto const shader{GL_ENTRY(glCreateShader)(GL_FRAGMENT_SHADER)};
  GL_ENTRY(glShaderSource)(shader, 1, &fragment_source, nullptr);
  GL_ENTRY(glCompileShader)(shader);
  GLint ok{};
  GL_ENTRY(glGetShaderiv)(shader, GL_COMPILE_STATUS, &ok);
  std::array<char, 1024> log{};
  if(!ok) {
    GL_ENTRY(glGetShaderInfoLog)(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
    GL_ENTRY(glDeleteShader)(shader);
    throw std::runtime_error{std::string{"Cannot compile the indexed display shader: "} + log.data()};
  }
  program = GL_ENTRY(glCreateProgram)();
  GL_ENTRY(glAttachShader)(program, shader);
  GL_ENTRY(glDeleteShader)(shader);
  GL_ENTRY(glLinkProgram)(program);
  GL_ENTRY(glGetProgramiv)(program, GL_LINK_STATUS, &ok);
  if(!ok) {
    GL_ENTRY(glGetProgramInfoLog)(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
    delete_program(program);
    throw std::runtime_error{std::string{"Cannot link the indexed display shader: "} + log.data()};
  }
  GL_ENTRY(glUseProgram)(program);
  auto const location{GL_ENTRY(glGetUniformLocation)};
  auto const sampler{GL_ENTRY(glUniform1i)};
  canvas_size = location(program, "canvasSize");
  page_size = location(program, "pageSize");
  world_size = location(program, "worldSize");
  view_rect = location(program, "viewRect");
  constexpr std::array names{"page", "coverage", "world", "palette"};
  glGenTextures(plane_count, textures.data());
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  uint8_t const black[3]{};
  for(unsigned i{0}; i < plane_count; ++i) {
    sampler(location(program, names[i]), static_cast<GLint>(i));
    active_texture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_2D, textures[i]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    extents[i] = {0, 0};
    upload(static_cast<plane>(i), {{1, 1}, 1, black});
  }
  active_texture(GL_TEXTURE0);
}
indexed_display::~indexed_display() {
  glDeleteTextures(plane_count, textures.data());
  delete_program(program);
}
void indexed_display::upload(plane which, indexed_region image) {
  active_texture(GL_TEXTURE0 + which);
  auto const format{which == colours ? GL_RGB : GL_LUMINANCE};
  glPixelStorei(GL_UNPACK_ROW_LENGTH, image.stride);
  if(extents[which] == image.size) {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image.size.width, image.size.height, format, GL_UNSIGNED_BYTE, image.pixels);
  } else {
    glTexImage2D(GL_TEXTURE_2D, 0, which == colours ? GL_RGB8 : GL_LUMINANCE8, image.size.width, image.size.height, 0, format, GL_UNSIGNED_BYTE, image.pixels);
    if(glGetError() != GL_NO_ERROR) throw std::runtime_error{"Cannot allocate the indexed display texture"};
    extents[which] = image.size;
  }
}
void indexed_display::bind(framebuffer const &frame, palette const &palette, render_size canvas, viewport const &view, int drawable_height) {
  auto const size{frame.size()};
  upload(page, {size, size.width, frame.pixels().data()});
  auto const mask{frame.detail_mask()};
  render_size detail_size{0, 0};
  if(std::ranges::any_of(mask, [](uint8_t pixel){return pixel != 0;})) {
    auto const region{frame.detail_region()};
    upload(world, region);
    upload(coverage, {size, size.width, mask.data()});
    detail_size = region.size;
  }
  std::array<std::array<uint8_t, 3>, 256> rgb;
  for(size_t i{0}; i < rgb.size(); ++i) {
    auto const c{palette.colours[i]};
    rgb[i] = {static_cast<uint8_t>(c >> 16), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c)};
  }
  upload(colours, {{256, 1}, 256, rgb.front().data()});
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  active_texture(GL_TEXTURE0);
  uniform_size(canvas_size, canvas.width, canvas.height);
  uniform_size(page_size, size.width, size.height);
  uniform_size(world_size, detail_size.width, detail_size.height);
  uniform_rect(view_rect, view.x, drawable_height - view.y - 1, view.width, view.height);
}
}

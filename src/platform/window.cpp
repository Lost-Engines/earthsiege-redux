#include "window.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <GL/glext.h>
#endif // _WIN32
#include "render/software_cursor.h"
#include "resources/artwork.h"
#include "viewport.h"

namespace earthsiege {

glfw_context::glfw_context() {
  glfwSetErrorCallback([](int const code, char const *message){
    std::cerr << "GLFW " << code << ": " << message << '\n';
  });
  glfwInitHint(GLFW_JOYSTICK_HAT_BUTTONS, GLFW_FALSE);
  if(!glfwInit()) throw std::runtime_error{"cannot initialise GLFW"};
}
glfw_context::~glfw_context() {
  glfwTerminate();
}
auto window::self(GLFWwindow *handle)->window & {
  return *static_cast<window *>(glfwGetWindowUserPointer(handle));
}

window::window(unsigned int scale, std::optional<render_size> resolution) : fixed_resolution{resolution} {
  bool const automatic{scale == 0 && !resolution};
  bool const compositor{automatic && glfwGetPlatform() == GLFW_PLATFORM_WAYLAND};
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_MAXIMIZED, compositor ? GLFW_TRUE : GLFW_FALSE);
  int const magnification{static_cast<int>(std::clamp(scale ? scale : 2u, 1u, 16u))};
  auto const initial{scale == 0 && resolution ? *resolution : render_size{320 * magnification, 200 * magnification}};
  handle.reset(glfwCreateWindow(initial.width, initial.height,
    "Metaltech: Earthsiege Redux - Ctrl+Shift+M: music hardware", nullptr, nullptr));
  if(!handle) throw std::runtime_error{"cannot create the Earthsiege window"};
  if(automatic && !compositor) {
    if(auto *monitor{glfwGetPrimaryMonitor()}) {
      int x{0};
      int y{0};
      int width{0};
      int height{0};
      int left{0};
      int top{0};
      int right{0};
      int bottom{0};
      glfwGetMonitorWorkarea(monitor, &x, &y, &width, &height);
      glfwGetWindowFrameSize(handle.get(), &left, &top, &right, &bottom);
      int const fit{std::max(1, std::min((width - left - right) / 320, (height - top - bottom) / 200))};
      glfwSetWindowSize(handle.get(), 320 * fit, 200 * fit);
      glfwSetWindowPos(handle.get(), x + left + (width - left - right - 320 * fit) / 2,
                       y + top + (height - top - bottom - 200 * fit) / 2);
    }
  }
  glfwShowWindow(handle.get());
  if(compositor) {
    int width{0};
    int height{0};
    glfwGetWindowSize(handle.get(), &width, &height);
    glfwRestoreWindow(handle.get());
    double const deadline{glfwGetTime() + 1};
    while(glfwGetWindowAttrib(handle.get(), GLFW_MAXIMIZED) && glfwGetTime() < deadline) glfwWaitEventsTimeout(.01);
    int const fit{std::max(1, std::min(width / 320, height / 200))};
    glfwSetWindowSize(handle.get(), 320 * fit, 200 * fit);
  }
  glfwMakeContextCurrent(handle.get());
  glfwSwapInterval(1);
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
  glDisable(GL_DITHER);
  glEnable(GL_TEXTURE_2D);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
  glfwSetWindowUserPointer(handle.get(), this);
  glfwSetInputMode(handle.get(), GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
  glfwSetWindowCloseCallback(handle.get(), [](GLFWwindow *w){self(w).events.push_back({.type = event_type::close});});
  glfwSetWindowFocusCallback(handle.get(), [](GLFWwindow *w, int active){
    self(w).events.push_back({
      .type = event_type::focus,
      .focused = active != 0
    });
    glfwSetInputMode(w, GLFW_CURSOR, active ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_NORMAL);
  });
  glfwSetKeyCallback(handle.get(), [](GLFWwindow *w, int key, int, int action, int mods){
    self(w).events.push_back({
      .type = action == GLFW_RELEASE ? event_type::key_up : event_type::key_down,
      .key = key,
      .modifiers = mods,
      .repeat = action == GLFW_REPEAT
    });
  });
  glfwSetCharCallback(handle.get(), [](GLFWwindow *w, unsigned int character){
    std::string utf8;
    if(character < 0x80) {
      utf8 += static_cast<char>(character);
    } else {
      if(character < 0x800) {
        utf8 += static_cast<char>(0xc0 | (character >> 6));
      } else {
        if(character < 0x10000) {
          utf8 += static_cast<char>(0xe0 | (character >> 12));
        } else {
          utf8 += static_cast<char>(0xf0 | (character >> 18));
          utf8 += static_cast<char>(0x80 | ((character >> 12) & 63));
        }
        utf8 += static_cast<char>(0x80 | ((character >> 6) & 63));
      }
      utf8 += static_cast<char>(0x80 | (character & 63));
    }
    self(w).events.push_back({
      .type = event_type::text,
      .text = std::move(utf8)
    });
  });
  glfwSetCursorPosCallback(handle.get(), [](GLFWwindow *w, double x, double y){
    auto &owner{self(w)};
    auto const p{owner.logical_position(x, y)};
    owner.events.push_back({
      .type = event_type::motion,
      .x = p[0],
      .y = p[1]
    });
  });
  glfwSetMouseButtonCallback(handle.get(), [](GLFWwindow *w, int button, int action, int mods){
    auto &owner{self(w)};
    auto const p{owner.pointer_position()};
    owner.events.push_back({
      .type = action == GLFW_RELEASE ? event_type::button_up : event_type::button_down,
      .modifiers = mods,
      .button = button,
      .x = p[0],
      .y = p[1]
    });
  });
}

window::~window() {
  glfwMakeContextCurrent(handle.get());
  glDeleteTextures(1, &texture);
}
void window::draw(framebuffer const &frame, palette const &colours, software_cursor const *cursor) {
  int width{0}, height{0};
  glfwGetFramebufferSize(handle.get(), &width, &height);
  if(width <= 0 || height <= 0) return;
  auto const canvas{resolution()};
  auto const area{viewport::fit(width, height, canvas)};
  auto presented{frame};
  if(cursor && focused() && glfwGetWindowAttrib(handle.get(), GLFW_HOVERED)) {
    auto const p{pointer_position()};
    if(p[0] >= 0 && p[0] < 320 && p[1] >= 0 && p[1] < 200) cursor->draw(presented, p[0], p[1]);
  }
  auto const size{canvas.artwork_size()};
  if(resolved.size() != size) resolved = framebuffer{size};
  presented.blit_to(resolved, size.bounds());
  std::array<std::array<uint8_t, 4>, 256> lookup;
  for(size_t i{0}; i < lookup.size(); ++i) {
    auto const rgb{colours.colours[i]};
    lookup[i] = {static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8), static_cast<uint8_t>(rgb), 255};
  }
  display.resize(size.count());
  std::ranges::transform(resolved.pixels(), display.begin(), [&](uint8_t index){return lookup[index];});
  glViewport(0, 0, width, height);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glViewport(area.x, height - area.y - area.height, area.width, area.height);
  glBindTexture(GL_TEXTURE_2D, texture);
  if(size != texture_size) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.width, size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, display.data());
    if(glGetError() != GL_NO_ERROR) throw std::runtime_error{"cannot allocate the display texture"};
    texture_size = size;
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size.width, size.height, GL_RGBA, GL_UNSIGNED_BYTE, display.data());
  }
  glBegin(GL_TRIANGLE_STRIP);
  glTexCoord2f(0, 0);
  glVertex2f(-1, 1);
  glTexCoord2f(0, 1);
  glVertex2f(-1, -1);
  glTexCoord2f(1, 0);
  glVertex2f(1, 1);
  glTexCoord2f(1, 1);
  glVertex2f(1, -1);
  glEnd();
}
void window::present(framebuffer const &frame, palette const &colours, software_cursor const *cursor) {
  if(glfwGetWindowAttrib(handle.get(), GLFW_ICONIFIED)) return;
  draw(frame, colours, cursor);
  glfwSwapBuffers(handle.get());
}
auto window::pointer_position() const->std::array<int, 2> {
  double x{0.0};
  double y{0.0};
  glfwGetCursorPos(handle.get(), &x, &y);
  return logical_position(x, y);
}
auto window::resolution() const->render_size {
  if(!fixed_resolution) {
    int w{0}, h{0};
    glfwGetFramebufferSize(handle.get(), &w, &h);
    if(w > 0 && h > 0) last_drawable = {w, h};
  }
  auto const canvas{fixed_resolution.value_or(last_drawable)};
  auto const size{canvas.artwork_size()};
  if(size.width > max_texture_size || size.height > max_texture_size) throw std::runtime_error{"render resolution exceeds the OpenGL texture limit"};
  return canvas;
}
auto window::logical_position(double x, double y) const->std::array<int, 2> {
  int width{0};
  int height{0};
  int bw{0};
  int bh{0};
  glfwGetWindowSize(handle.get(), &width, &height);
  glfwGetFramebufferSize(handle.get(), &bw, &bh);
  return viewport::fit(bw, bh, resolution()).logical(x, y, width, height, bw, bh);
}
void window::warp_mouse(int x, int y) {
  int width{0};
  int height{0};
  int bw{0};
  int bh{0};
  glfwGetWindowSize(handle.get(), &width, &height);
  glfwGetFramebufferSize(handle.get(), &bw, &bh);
  auto const p{viewport::fit(bw, bh, resolution()).cursor(x, y, width, height, bw, bh)};
  glfwSetCursorPos(handle.get(), p[0], p[1]);
}
void window::pump_events() {
  glfwPollEvents();
}
auto window::poll(window_event &event)->bool {
  if(events.empty()) pump_events();
  if(events.empty()) {
    if(!glfwWindowShouldClose(handle.get())) return false;
    event = {.type = event_type::close};
    return true;
  }
  event = std::move(events.front());
  events.pop_front();
  return true;
}
auto window::focused() const->bool {
  return glfwGetWindowAttrib(handle.get(), GLFW_FOCUSED) != 0;
}
auto window::get() const->GLFWwindow * {
  return handle.get();
}
auto window::modifiers() const->int {
  auto down{[&](int key){return glfwGetKey(handle.get(), key) == GLFW_PRESS;}};
  return ((down(GLFW_KEY_LEFT_CONTROL) || down(GLFW_KEY_RIGHT_CONTROL)) ? GLFW_MOD_CONTROL : 0)
    | ((down(GLFW_KEY_LEFT_SHIFT) || down(GLFW_KEY_RIGHT_SHIFT)) ? GLFW_MOD_SHIFT : 0)
    | ((down(GLFW_KEY_LEFT_ALT) || down(GLFW_KEY_RIGHT_ALT)) ? GLFW_MOD_ALT : 0);
}

} // namespace earthsiege

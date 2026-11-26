#include "window.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>
#include "indexed_display.h"
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

window::window(unsigned int scale, std::optional<render_size> resolution, bool fullscreen) : fixed_resolution{resolution} {
  bool const automatic{scale == 0 && !resolution};
  bool const wayland{glfwGetPlatform() == GLFW_PLATFORM_WAYLAND};
  bool const compositor{automatic && wayland};
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
  if(fullscreen && !wayland) toggle_fullscreen();
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
  // Wayland assigns the window its shell role and initial size when shown.
  if(fullscreen && wayland) toggle_fullscreen();
  glfwMakeContextCurrent(handle.get());
  glfwSwapInterval(1);
  presenter = std::make_unique<indexed_display>();
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
  glDisable(GL_DITHER);
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
    auto &owner{self(w)};
    if(key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
      auto &consumed{owner.fullscreen_keys[key == GLFW_KEY_ENTER ? 0u : 1u]};
      if(action == GLFW_PRESS) {
        consumed = (mods & GLFW_MOD_ALT) != 0;
        if(consumed) owner.fullscreen_toggle_pending = !owner.fullscreen_toggle_pending;
      }
      if(consumed) {
        if(action == GLFW_RELEASE) consumed = false;
        return;
      }
    }
    owner.events.push_back({
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
}
void window::draw(framebuffer const &frame, palette const &colours, software_cursor const *cursor) {
  int width{0}, height{0};
  glfwGetFramebufferSize(handle.get(), &width, &height);
  if(width <= 0 || height <= 0) return;
  auto const canvas{resolution()};
  framing = frame.cockpit_viewport();
  auto const area{viewport::fit(width, height, canvas, frame.has_surround())};
  std::array<int, 2> position{};
  if(cursor && focused() && glfwGetWindowAttrib(handle.get(), GLFW_HOVERED)) {
    position = pointer_position();
    if(!frame.has_surround() && !render_size{}.bounds().contains(position[0], position[1])) cursor = nullptr;
  } else cursor = nullptr;
  presenter->bind(frame, colours, frame.has_surround() ? canvas.mission_size() : canvas.artwork_size(), area, height, cursor, position);
  glViewport(0, 0, width, height);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glViewport(area.x, height - area.y - area.height, area.width, area.height);
  glBegin(GL_TRIANGLE_STRIP);
  glVertex2f(-1, 1);
  glVertex2f(-1, -1);
  glVertex2f(1, 1);
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
  auto const size{canvas.mission_size()};
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
  return viewport::fit(bw, bh, resolution(), false, framing).logical(x, y, width, height, bw, bh);
}
void window::warp_mouse(int x, int y) {
  int width{0};
  int height{0};
  int bw{0};
  int bh{0};
  glfwGetWindowSize(handle.get(), &width, &height);
  glfwGetFramebufferSize(handle.get(), &bw, &bh);
  auto const p{viewport::fit(bw, bh, resolution(), false, framing).cursor(x, y, width, height, bw, bh)};
  glfwSetCursorPos(handle.get(), p[0], p[1]);
}
void window::toggle_fullscreen() {
  auto *const host{handle.get()};
  if(glfwGetWindowMonitor(host)) {
    glfwSetWindowMonitor(host, nullptr, windowed_bounds.left, windowed_bounds.top,
                         windowed_bounds.width(), windowed_bounds.height(), GLFW_DONT_CARE);
    if(windowed_maximized) glfwMaximizeWindow(host);
    return;
  }

  int x{}, y{}, width{}, height{};
  bool const positioned{glfwGetPlatform() != GLFW_PLATFORM_WAYLAND};
  if(positioned) glfwGetWindowPos(host, &x, &y);
  glfwGetWindowSize(host, &width, &height);
  windowed_bounds = {x, y, x + width - 1, y + height - 1};
  windowed_maximized = glfwGetWindowAttrib(host, GLFW_MAXIMIZED) == GLFW_TRUE;

  auto *monitor{glfwGetPrimaryMonitor()};
  if(positioned) {
    int count{};
    auto **monitors{glfwGetMonitors(&count)};
    int64_t largest_area{};
    for(int i{0}; i < count; ++i) {
      auto const *mode{glfwGetVideoMode(monitors[i])};
      if(!mode) continue;
      int left{}, top{};
      glfwGetMonitorPos(monitors[i], &left, &top);
      auto const overlap{windowed_bounds.intersect({left, top, left + mode->width - 1, top + mode->height - 1})};
      auto const area{int64_t{std::max(0, overlap.width())} * std::max(0, overlap.height())};
      if(area > largest_area) {
        monitor = monitors[i];
        largest_area = area;
      }
    }
  }
  if(monitor) {
    if(auto const *mode{glfwGetVideoMode(monitor)}) {
      glfwSetWindowMonitor(host, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    }
  }
}
void window::pump_events() {
  glfwPollEvents();
  if(std::exchange(fullscreen_toggle_pending, false)) toggle_fullscreen();
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

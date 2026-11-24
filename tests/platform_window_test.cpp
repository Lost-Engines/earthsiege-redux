#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include "platform/viewport.h"
#include "platform/window.h"
#include "render/software_cursor.h"
#include "resources/artwork.h"
#include "resources/resource_store.h"

namespace {
void require(bool value, char const *message) {
  if(!value) throw std::runtime_error{message};
}

void check_composite(earthsiege::window &display, earthsiege::render_size region, earthsiege::software_cursor const *arrow = nullptr) {
  auto const canvas{display.resolution()};
  earthsiege::render_size const atlas_size{region.width + 17, region.height + 23};
  std::vector<uint8_t> indices(atlas_size.count());
  for(size_t i{0}; i < indices.size(); ++i) indices[i] = static_cast<uint8_t>(13 * i + i / atlas_size.width);
  auto atlas{std::make_shared<earthsiege::framebuffer>(atlas_size, indices)};
  earthsiege::framebuffer frame;
  frame.attach(atlas, {7, 11, 7 + region.width - 1, 11 + region.height - 1});
  frame.fill({42, 36, 198, 99}, 24);
  frame.line(9, 172, 294, 110, 251);
  earthsiege::palette colours;
  for(unsigned i{0}; i < 256; ++i) colours.colours[i] = 0xff000000u | (i << 16) | ((255 - i) << 8) | (i ^ 99);
  int w{}, h{};
  glfwGetFramebufferSize(display.get(), &w, &h);
  auto const check{[&](earthsiege::software_cursor const *cursor = nullptr) {
    auto const content{frame.has_surround() ? canvas.mission_size() : canvas.artwork_size()};
    auto const view{earthsiege::viewport::fit(w, h, canvas, frame.has_surround())};
    display.draw(frame, colours, cursor);
    auto const pointer{display.pointer_position()};
    auto const ink_area{cursor ? cursor->bounds(pointer[0], pointer[1]) : earthsiege::rectangle{}};
    auto const panel{content.cockpit_panel(frame.cockpit_pan())};
    glReadBuffer(GL_BACK);
    std::vector<std::array<uint8_t, 4>> pixels(static_cast<size_t>(w) * h);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    earthsiege::framebuffer reference{content};
    frame.blit_presentation(reference, content.bounds());
    for(int y{0}; y < h; ++y) {
      for(int x{0}; x < w; ++x) {
        uint32_t expected{0};
        if(x >= view.x && x < view.x + view.width && y >= view.y && y < view.y + view.height) {
          auto const sx{(2 * (x - view.x) + 1) * content.width / (2 * view.width)};
          auto const sy{(2 * (y - view.y) + 1) * content.height / (2 * view.height)};
          auto index{reference.pixels()[sy * content.width + sx]};
          if(cursor) {
            int const lx{static_cast<int>(std::floor((sx - panel.left) * 320. / panel.width()))};
            int const ly{sy * 200 / panel.height()};
            if(ink_area.contains(lx, ly)) {
              auto const ink{cursor->artwork().pixels[(ly - ink_area.top) * ink_area.width() + lx - ink_area.left]};
              if(ink) index = ink;
            }
          }
          expected = colours.colours[index] & 0xffffff;
        }
        auto const &rgb{pixels[(h - y - 1) * w + x]};
        if(((uint32_t{rgb[0]} << 16) | (uint32_t{rgb[1]} << 8) | rgb[2]) != expected) {
          throw std::runtime_error{"indexed display differs from CPU at " + std::to_string(x) + "," + std::to_string(y)
            + " on " + std::to_string(canvas.width) + "x" + std::to_string(canvas.height)};
        }
      }
    }
  }};
  check();
  colours.colours[24] = 0xff31aec4;
  check();
  frame.fill(frame.size().bounds(), 24); // The lift door can cover the entire detailed image.
  check();
  if(canvas.width > canvas.artwork_size().width) {
    earthsiege::render_size const extended{459, 200};
    std::vector<uint8_t> side_pixels(extended.count());
    for(size_t i{0}; i < side_pixels.size(); ++i) side_pixels[i] = static_cast<uint8_t>(i / extended.width + i % extended.width);
    auto sides{std::make_shared<earthsiege::framebuffer>(extended, side_pixels)};
    frame.attach(atlas, {7, 11, 7 + region.width - 1, 11 + region.height - 1});
    frame.fill({147, 13, 159, 77}, 24);
    for(int pan : {0, -320, 160, 320}) {
      frame.set_surround(sides, {3, 0, 452, 199}, pan);
      check();
    }
    if(arrow && glfwGetPlatform() == GLFW_PLATFORM_X11) {
      frame.set_surround(sides, {3, 0, 452, 199});
      check();
      glfwFocusWindow(display.get());
      for(int x : {-8, -1, 160, 319, 326}) {
        display.warp_mouse(x, 90);
        glfwPollEvents();
        require(display.focused() && glfwGetWindowAttrib(display.get(), GLFW_HOVERED), "cursor inside the widescreen viewport");
        check(arrow);
      }
    }
  }
  frame.clear(251);
  check();
}

void check_window() {
  earthsiege::window display{2};
  auto *handle{display.get()};
  earthsiege::window_event event;
  while(display.poll(event)) {}
  // Dispatch through the registered GLFW callbacks, then consume the game queue.
  auto const key{glfwSetKeyCallback(handle, nullptr)};
  glfwSetKeyCallback(handle, key);
  key(handle, GLFW_KEY_P, 0, GLFW_PRESS, 0);
  key(handle, GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0);
  require(display.poll(event) && event.key == GLFW_KEY_P, "pause stays ahead of dismissal");
  require(display.poll(event) && event.key == GLFW_KEY_ESCAPE, "dismissal remains queued for modal");
  key(handle, GLFW_KEY_F1, 0, GLFW_REPEAT, GLFW_MOD_CONTROL);
  require(display.poll(event) && event.repeat && event.modifiers == GLFW_MOD_CONTROL, "repeat and modifiers");
  auto const character{glfwSetCharCallback(handle, nullptr)};
  glfwSetCharCallback(handle, character);
  character(handle, 'R');
  require(display.poll(event) && event.text == "R", "text entry callback");
  character(handle, 0xe9);
  require(display.poll(event) && event.text == "\xc3\xa9", "UTF-8 text entry");
  auto const focus{glfwSetWindowFocusCallback(handle, nullptr)};
  glfwSetWindowFocusCallback(handle, focus);
  focus(handle, GLFW_FALSE);
  require(display.poll(event) && event.type == earthsiege::event_type::focus && !event.focused, "focus loss notification");
  focus(handle, GLFW_TRUE);
  require(display.poll(event) && event.focused, "focus restoration");
  auto const motion{glfwSetCursorPosCallback(handle, nullptr)};
  glfwSetCursorPosCallback(handle, motion);
  motion(handle, 20, 40);
  motion(handle, 40, 80);
  require(display.poll(event) && event.x == 10 && event.y == 20, "first queued cursor position");
  require(display.poll(event) && event.x == 20 && event.y == 40, "second queued cursor position");
  earthsiege::framebuffer frame;
  earthsiege::palette colours;
  colours.colours[0] = 0xff123456;
  colours.colours[1] = 0xffedcba9;
  frame.fill({0, 0, 159, 99}, 1);
  display.draw(frame, colours);
  // Read the rendered back buffer before the compositor takes ownership.
  glReadBuffer(GL_BACK);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  std::array<uint8_t, 3> pixel{};
  glReadPixels(100, 300, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
  require(pixel == std::array<uint8_t, 3>{0xed, 0xcb, 0xa9}, "top-left presentation orientation and RGB channels");
  glReadPixels(500, 100, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
  require(pixel == std::array<uint8_t, 3>{0x12, 0x34, 0x56}, "bottom-right presentation pixels");
  glfwSetWindowSize(handle, 803, 501);
  int w{}, h{};
  double const deadline{glfwGetTime() + 2};
  do {
    glfwWaitEventsTimeout(.01);
    glfwGetWindowSize(handle, &w, &h);
  } while((w != 803 || h != 501) && glfwGetTime() < deadline);
  require(w == 803 && h == 501, "window resize delivered");
  glfwGetFramebufferSize(handle, &w, &h);
  require(display.resolution() == earthsiege::render_size{w, h}, "automatic resolution follows framebuffer pixels");
  auto const size{display.resolution().artwork_size()};
  auto const art{display.resolution().fit({})};
  auto detail{std::make_shared<earthsiege::framebuffer>(size)};
  detail->pixel(100, 100, 1);
  frame.attach(detail, size.bounds());
  // GLX can allocate the resized back buffer at the first swap.
  display.present(frame, colours);
  display.draw(frame, colours);
  glReadPixels(art.left + 100, h - 1 - art.top - 100, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
  require(pixel == std::array<uint8_t, 3>{0xed, 0xcb, 0xa9}, "individual rendered pixel after resize");
  glReadPixels(art.left + 101, h - 1 - art.top - 100, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
  require(pixel == std::array<uint8_t, 3>{0x12, 0x34, 0x56}, "adjacent output pixel remains independent");
  check_composite(display, size);
  glfwSetWindowShouldClose(handle, GLFW_TRUE);
  while(display.poll(event) && event.type != earthsiege::event_type::close) {}
  require(event.type == earthsiege::event_type::close, "close survives nested dialog consumption");
}
}

int main(int argc, char **argv) try {
  if(argc != 2) return EXIT_FAILURE;
  earthsiege::resource_store resources{argv[1]};
  earthsiege::software_cursor arrow{resources.read("shell0", "RES32/ARROW.DCI")};
  check_window();
  earthsiege::window fixed{2, earthsiege::render_size{731, 413}};
  check_composite(fixed, {417, 277}, &arrow);
  std::cout << "GLFW events and OpenGL framebuffer checks pass\n";
} catch(std::exception const &error) {
  std::cerr << error.what() << '\n';
  return EXIT_FAILURE;
}

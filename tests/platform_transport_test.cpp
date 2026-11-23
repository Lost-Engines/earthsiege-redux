#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include "platform/pcm_queue.h"
#include "platform/viewport.h"

namespace {
void require(bool value, char const *message) {
  if(!value) throw std::runtime_error{message};
}
}
int main() try {
  using earthsiege::viewport;
  auto const area{viewport::fit(1920, 1080)};
  require(area.width == 1728 && area.height == 1080 && area.x == 96 && area.y == 0, "8:5 artwork fills the available height");
  auto const fixed{viewport::fit(960, 600, {1920, 1080})};
  require(fixed.width == 864 && fixed.height == 540 && fixed.x == 48 && fixed.y == 30, "fixed widescreen canvas fits a resized window");
  require(fixed.logical(48, 30, 960, 600, 960, 600) == std::array{0, 0}, "input uses both sets of borders");
  for(auto const size : {std::array{960, 540, 1920, 1080}, std::array{803, 601, 803, 601}, std::array{120, 100, 120, 100}}) {
    auto const view{viewport::fit(size[2], size[3])};
    for(auto const pixel : {std::array{0, 0}, std::array{319, 199}, std::array{160, 100}}) {
      auto const point{view.cursor(pixel[0], pixel[1], size[0], size[1], size[2], size[3])};
      require(view.logical(point[0], point[1], size[0], size[1], size[2], size[3]) == pixel, "HiDPI/letterbox cursor round trip");
    }
  }
  require(area.logical(0, 0, 1920, 1080, 1920, 1080)[0] < 0, "letterbox clicks stay outside game");
  earthsiege::pcm_queue queue;
  std::array<int16_t, 4> const first{-32768, 32767, 2, -2};
  std::array<int16_t, 2> const second{3, -3};
  std::array<int16_t, 8> output{};
  queue.queue(first);
  queue.pause(true);
  queue.read(output);
  require(output == std::array<int16_t, 8>{}, "paused output is silence");
  queue.queue(second);
  queue.pause(false);
  queue.read(output);
  require(output == std::array<int16_t, 8>{-32768, 32767, 2, -2, 3, -3, 0, 0}, "PCM ordering, signed range and underrun silence");
  queue.queue(first);
  queue.clear();
  queue.read(output);
  require(output == std::array<int16_t, 8>{}, "clear removes previous scene's audio");
  earthsiege::pcm_queue buffered{6};
  buffered.queue(first);
  buffered.read(output);
  require(output == std::array<int16_t, 8>{}, "short first block is retained while priming");
  buffered.queue(second);
  buffered.read(output);
  require(output == std::array<int16_t, 8>{-32768, 32767, 2, -2, 3, -3, 0, 0}, "primed stream starts in order");
  buffered.queue(second);
  buffered.read(output);
  require(output == std::array<int16_t, 8>{}, "underrun re-primes instead of alternating audio and silence");
  std::cout << "Viewport and PCM transport checks pass\n";
} catch(std::exception const &error) {
  std::cerr << error.what() << '\n';
  return EXIT_FAILURE;
}

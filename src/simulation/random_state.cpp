#include "random_state.h"
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <utility>

namespace earthsiege {

random_state::random_state() : random_state{std::mt19937{std::random_device{}()}} {}
random_state::random_state(std::mt19937 generator) : engine{std::move(generator)} {}
random_state random_state::seeded(uint32_t seed) { return random_state{std::mt19937{seed}}; }
random_state random_state::fresh_stream() {
  if(replaying()) return random_state{0};
  return seeded(static_cast<uint32_t>(std::get<std::mt19937>(engine)()));
}

random_state::random_state(int16_t advances) : engine{state{
  .words{
    // original GO 0x81720, copied by 0x386c8 before the requested warm-up draws
    56611, 38195, 62413, 19394, 63458, 41695, 43632, 24746,
    5195, 35144, 6442, 27405, 40459, 64497, 34311, 18013,
    60148, 27133, 27716, 6951, 56177, 58255, 43956, 56391,
    4062, 19854, 64637, 36152, 22856, 58223, 43554, 12070,
    17402, 46501, 8427, 60805, 62915, 33183, 11676, 45677,
    59691, 38869, 15777, 35309, 58356, 29877, 44, 54111,
    15112, 3573, 62078, 49776, 9609, 11915, 25426, 55853,
  },
  .first{24},
  .second{55},
}} {
  int const draws{advances == std::numeric_limits<int16_t>::min() ? 32768 : std::max(0, static_cast<int>(advances))};
  for(int index{0}; index < draws; ++index) next();
}

random_state::random_state(state const &initial) : engine{initial} {
  if(initial.first >= word_count || initial.second >= word_count) throw std::runtime_error{"invalid original random-state indices"};
}

auto random_state::state::operator()()->result_type {
  auto &word{words[second]};
  word = static_cast<uint16_t>(word + words[first]);
  first = (first + 1) % word_count;
  second = (second + 1) % word_count;
  return word;
}

auto random_state::next()->int16_t {
  return std::visit([](auto &source){return std::bit_cast<int16_t>(static_cast<uint16_t>(source()));}, engine);
}

auto random_state::inclusive(uint32_t maximum)->uint32_t {
  return std::visit([maximum](auto &source){return std::uniform_int_distribution<uint32_t>{0, maximum}(source);}, engine);
}

auto random_state::bounded(int16_t limit)->int16_t {
  /// GO 0x38774 does not advance the generator when the requested limit is zero.
  if(!limit) return 0;
  if(!replaying()) return static_cast<int16_t>(inclusive(static_cast<uint32_t>(std::abs(static_cast<int>(limit)) - 1)));
  int const value{std::bit_cast<uint16_t>(next()) & 0x7fff};
  return static_cast<int16_t>(value % limit);
}

auto random_state::snapshot() const->state const & {
  return std::get<state>(engine);
}

} // namespace earthsiege

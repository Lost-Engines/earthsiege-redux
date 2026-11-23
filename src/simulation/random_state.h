#pragma once
#include <array>
#include <cstdint>
#include <random>
#include <variant>

namespace earthsiege {

class random_state {
public:
  static unsigned int constexpr word_count{56};
  struct state {
    std::array<uint16_t, word_count> words{};
    unsigned int first{0};
    unsigned int second{0};
    using result_type = uint16_t;
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return UINT16_MAX; }
    result_type operator()();
  };

private:
  std::variant<state, std::mt19937> engine;
  explicit random_state(std::mt19937 generator);

public:
  random_state();
  static random_state seeded(uint32_t seed);
  /// DOS input tapes and original-executable fixtures start at these draws.
  explicit random_state(int16_t advances);
  explicit random_state(state const &initial);
  auto next()->int16_t;
  auto bounded(int16_t limit)->int16_t;
  auto inclusive(uint32_t maximum)->uint32_t;
  bool replaying() const { return std::holds_alternative<state>(engine); }
  random_state fresh_stream();
  /// Original-format state for comparing a tape or fixture with DOS.
  auto snapshot() const->state const &;
};

} // namespace earthsiege

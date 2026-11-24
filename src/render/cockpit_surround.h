#pragma once
#include "resources/cockpit.h"

namespace earthsiege {

// The forward, mirrored left, right and lower DOS pages in shared cockpit
// coordinates. Negative entries are glass in the corresponding ED mask.
class cockpit_surround {
  int first_x, width, height;
  std::vector<int16_t> artwork;
  framebuffer front;
public:
  explicit cockpit_surround(cockpit_artwork const &cockpit);
  framebuffer const &forward_panel() const { return front; }
  void draw_sides(framebuffer &world, std::array<int32_t, 2> origin, bitmap const *lift = nullptr, int cover_y = 0,
                  framebuffer const *instruments = nullptr) const;
};

} // namespace earthsiege

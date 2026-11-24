#include "cockpit_surround.h"
#include <algorithm>

namespace earthsiege {

cockpit_surround::cockpit_surround(cockpit_artwork const &cockpit)
  : first_x{cockpit.windows[3].instrument_origin[0]}, width{cockpit.windows[2].instrument_origin[0] + 320 - first_x},
    height{cockpit.windows[1].instrument_origin[1] + 200}, artwork(static_cast<size_t>(width) * height, 63) {
  // Front artwork owns the few rows where it overlaps the lower page.
  for(auto view : {cockpit_view::lower, cockpit_view::left, cockpit_view::right, cockpit_view::forward}) {
    auto const &window{cockpit.windows[static_cast<size_t>(view)]};
    framebuffer page;
    cockpit.draw_composed(page, view);
    for(int y{0}; y < 200; ++y) {
      auto *row{artwork.data() + static_cast<size_t>(window.instrument_origin[1] + y) * width};
      for(int x{0}; x < 320; ++x)
        row[window.instrument_origin[0] - first_x + x] = window.bounds.contains(x, y) && window.mask.contains(x, y)
          ? -1 : page.pixels()[static_cast<size_t>(y) * 320 + x];
    }
    if(view == cockpit_view::forward) {
      front = page;
      front.attach(std::make_shared<framebuffer>(std::move(page)), front.size().bounds(), &window.mask);
    }
  }
  // Below the side glass the only available artwork is the lower console.
  for(int y{200}; y < height; ++y) {
    auto *row{artwork.data() + static_cast<size_t>(y) * width};
    std::fill_n(row, -first_x, row[-first_x]);
    std::fill(row - first_x + 320, row + width, row[-first_x + 319]);
  }
}

void cockpit_surround::draw_sides(framebuffer &world, std::array<int32_t, 2> origin, bitmap const *lift, int cover_y,
                                  framebuffer const *instruments) const {
  auto const size{world.size()};
  auto const middle{size.cockpit_panel(origin[0])};
  auto const glass{instruments ? instruments->detail_mask() : std::span<uint8_t const>{}};
  for(bool right : {false, true}) {
    int const first{right ? middle.right + 1 : 0}, end{right ? size.width : middle.left};
    if(first == end) continue;
    std::vector<int> columns;
    columns.reserve(end - first);
    for(int x{first}; x < end; ++x) {
      int64_t const n{int64_t{x - middle.left} * 320};
      int const local{static_cast<int>((n - (n < 0 ? middle.width() - 1 : 0)) / middle.width())};
      columns.push_back(std::clamp(origin[0] + local - first_x, 0, width - 1));
    }
    for(int y{0}; y < size.height; ++y) {
      int const local{static_cast<int>(int64_t{y} * 200 / size.height)};
      int const row{std::clamp(origin[1] + local, 0, height - 1)};
      auto const *art{artwork.data() + static_cast<size_t>(row) * width};
      int const door_y{local - cover_y};
      int const door{!lift || door_y < 0 ? -1 : door_y >= static_cast<int>(lift->height) ? 63
                     : lift->pixels[static_cast<size_t>(door_y) * lift->width + (right ? lift->width - 1 : 0)]};
      auto out{world.opaque_span(y, first, end - 1)};
      for(size_t i{0}; i < out.size(); ++i) {
        int ink{art[columns[i]]};
        int const front_x{columns[i] + first_x};
        if(instruments && row < 200 && front_x >= 0 && front_x < 320) {
          size_t const p{static_cast<size_t>(row) * 320 + front_x};
          if(!glass[p]) ink = instruments->pixels()[p];
        }
        if(ink >= 0) out[i] = static_cast<uint8_t>(ink);
        else if(door >= 0) out[i] = static_cast<uint8_t>(door);
      }
    }
  }
}

} // namespace earthsiege

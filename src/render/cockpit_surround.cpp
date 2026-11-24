#include "cockpit_surround.h"
#include <algorithm>

namespace earthsiege {

cockpit_surround::cockpit_surround(cockpit_artwork const &cockpit)
  : first_x{cockpit.windows[3].instrument_origin[0]}, width{cockpit.windows[2].instrument_origin[0] + 320 - first_x},
    height{cockpit.windows[1].instrument_origin[1] + 200}, sight{cockpit.windows[0].projection_centre()},
    artwork(static_cast<size_t>(width) * height, 63) {
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
    if(view == cockpit_view::forward || view == cockpit_view::lower) {
      auto &copy{view == cockpit_view::forward ? front : lower};
      copy = page;
      copy.attach(std::make_shared<framebuffer>(std::move(page)), copy.size().bounds(), &window.mask);
    }
  }
  // Below the side glass the only available artwork is the lower console.
  for(int y{200}; y < height; ++y) {
    auto *row{artwork.data() + static_cast<size_t>(y) * width};
    std::fill_n(row, -first_x, row[-first_x]);
    std::fill(row - first_x + 320, row + width, row[-first_x + 319]);
  }
}

void cockpit_surround::draw(framebuffer &world, rectangle panel, std::array<int32_t, 2> origin, bitmap const *lift, int cover_y,
                            framebuffer const *forward_instruments, framebuffer const *lower_instruments) const {
  auto const size{world.size()};
  bool const pulled_back{panel.height() < size.height};
  // Negative coordinates lie in the page above or to the left. Round them down.
  auto const cell{[](int px, int logical, int extent) { return -render_size::edge(-px, logical, extent); }};
  std::vector<int> columns(size.width);
  int const last_x{first_x + width - 1};
  // Past either side panel, repeat its last column of artwork and glass at every FOV.
  for(int x{0}; x < size.width; ++x)
    columns[x] = std::clamp(origin[0] + cell(x - panel.left, 320, panel.width()), first_x, last_x);
  for(int y{0}; y < size.height; ++y) {
    int const local{cell(y - panel.top, 200, panel.height())};
    int const row{pulled_back ? origin[1] + local : std::clamp(origin[1] + local, 0, height - 1)};
    auto const *art{artwork.data() + static_cast<size_t>(std::clamp(row, 0, height - 1)) * width};
    // Above the artwork, follow the ray back to the top edge of the canopy.
    // This continues its roof and open glass without repeating the overhead instruments.
    double const roof_scale{row < 0 ? double(sight[1]) / (sight[1] - row) : 1.0};
    auto const *instruments{row < 200 ? forward_instruments : lower_instruments};
    int const instrument_y{row < 200 ? row : row - (height - 200)};
    int const door_y{local - cover_y};
    auto const draw_span{[&](int start, int end) {
      if(end <= start) return;
      auto out{world.opaque_span(y, start, end - 1)};
      for(int x{start}; x < end; ++x) {
        int const sx{columns[x]};
        int const sample_x{row < 0 ? sight[0] + static_cast<int>(std::floor((sx - sight[0]) * roof_scale)) : sx};
        int ink{art[std::clamp(sample_x - first_x, 0, width - 1)]};
        if(row < 0) ink = ink >= 0 ? 63 : -1;
        else if(row >= height) ink = 63;
        else if(instruments && sx >= 0 && sx < 320) {
          size_t const p{static_cast<size_t>(instrument_y) * 320 + sx};
          if(!instruments->detail_mask()[p]) ink = instruments->pixels()[p];
        }
        if(ink < 0 && lift && door_y >= 0) {
          auto const door_x{std::clamp(sx - origin[0], 0, static_cast<int>(lift->width) - 1)};
          ink = door_y >= static_cast<int>(lift->height) ? 63 : lift->pixels[static_cast<size_t>(door_y) * lift->width + door_x];
        }
        if(ink >= 0) out[x - start] = static_cast<uint8_t>(ink);
      }
    }};
    if(y < panel.top || y > panel.bottom) draw_span(0, size.width);
    else {
      draw_span(0, panel.left);
      draw_span(panel.right + 1, size.width);
    }
  }
}

} // namespace earthsiege

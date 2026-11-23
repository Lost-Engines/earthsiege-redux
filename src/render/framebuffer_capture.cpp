#include "framebuffer.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include "resources/artwork.h"

namespace earthsiege {

void framebuffer::save(std::filesystem::path const &path, palette const &colours, render_size canvas) const {
  auto const area{canvas.fit({})};
  auto const image{resampled({area.width(), area.height()})};
  std::ofstream file{path, std::ios::binary};
  file << "P6\n" << canvas.width << ' ' << canvas.height << "\n255\n";
  std::vector<char> row(static_cast<size_t>(canvas.width) * 3);
  for(int y{0}; y < canvas.height; ++y) {
    if(y == area.bottom + 1) std::ranges::fill(row, 0);
    if(y >= area.top && y <= area.bottom) {
      for(int x{area.left}; x <= area.right; ++x) {
        auto const rgb{colours.colours[image.raster[static_cast<size_t>(y - area.top) * area.width() + x - area.left]]};
        row[x * 3] = static_cast<char>(rgb >> 16);
        row[x * 3 + 1] = static_cast<char>(rgb >> 8);
        row[x * 3 + 2] = static_cast<char>(rgb);
      }
    }
    file.write(row.data(), static_cast<std::streamsize>(row.size()));
  }
  if(!file) throw std::runtime_error{"cannot write screenshot " + path.string()};
}

} // namespace earthsiege

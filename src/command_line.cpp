#include "command_line.h"
#include <algorithm>
#include <boost/program_options.hpp>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include "render/projection.h"

namespace earthsiege {
namespace {

auto resolution_option(std::string_view text)->std::optional<render_size> {
  if(text == "window" || text == "auto") return {};
  auto const separator{text.find_first_of("xX")};
  auto dimension{[](std::string_view value) {
    int n{};
    auto const [end, error]{std::from_chars(value.data(), value.data() + value.size(), n)};
    return error == std::errc{} && end == value.data() + value.size() && n > 0 && n <= 32767 ? n : 0;
  }};
  if(separator != text.npos) {
    render_size const size{dimension(text.substr(0, separator)), dimension(text.substr(separator + 1))};
    if(size.width && size.height) return size;
  }
  throw std::invalid_argument{"--resolution expects WIDTHxHEIGHT (1-32767 per dimension) or window"};
}

auto path_value(auto &destination) {
  return boost::program_options::value<std::string>()->value_name("PATH")->notifier([&destination](std::string const &path){
    destination = std::filesystem::path{path};
  });
}

auto count_value(char const *name, auto &destination) {
  // Boost accepts a leading minus for unsigned targets. Check before narrowing.
  return boost::program_options::value<int64_t>()->value_name("N")->notifier([name, &destination](int64_t value){
    if(!std::in_range<unsigned int>(value)) {
      throw std::invalid_argument{std::string{name} + " expects an integer from 0 to " + std::to_string(std::numeric_limits<unsigned int>::max())};
    }
    destination = static_cast<unsigned int>(value);
  });
}

auto multiplier_value(char const *name, double &destination, bool allow_zero = false) {
  return boost::program_options::value<double>()->value_name("N")->notifier([name, &destination, allow_zero](double value){
    if(!std::isfinite(value) || value < 0 || (!allow_zero && value == 0)) {
      throw std::invalid_argument{std::string{name} + (allow_zero ? " expects a finite nonnegative number" : " expects a finite number greater than zero")};
    }
    destination = value;
  });
}

} // namespace

auto parse_command_line(int argc, char const *const argv[], std::ostream &help)->std::optional<launch_options> {
  launch_options result;
  boost::program_options::options_description options{"Options", 110};
  options.add_options()
    ("help", "Show the available options")
    ("data-dir", path_value(result.data_dir), "Original game data (default: bundled assets)")
    ("original", "Start the original game directly")
    ("expansion", "Start the expansion directly")
    ("save-dir", path_value(result.save_dir), "Override the user save directory")
    ("demo", count_value("--demo", result.demo), "Play recording 1-4 (default: original game)")
    ("inspect-demo", count_value("--inspect-demo", result.inspect_demo), "Take the controls in the starting mission of recording 1-4")
    ("skip-intro", boost::program_options::bool_switch(&result.skip_intro), "Skip opening and pilot-death cinematics")
    ("music", boost::program_options::value<std::string>()->value_name("NAME"), "Choose opl2, opl3, sc55, scc1a, mt32, mt32-mpu or soundfont")
    ("rom-dir", path_value(result.rom_dir), "Override automatic Roland ROM discovery")
    ("soundfont", path_value(result.soundfont), "SoundFont file for General MIDI")
    ("audio-setup", boost::program_options::bool_switch(&result.audio_setup), "Open the music-device selector")
    ("resolution", boost::program_options::value<std::string>()->value_name("WxH"), "Set a fixed render size (default: window framebuffer)")
    ("fullscreen", boost::program_options::bool_switch(&result.fullscreen), "Start fullscreen (Alt+Enter toggles during play)")
    ("scale", count_value("--scale", result.scale), "Initial window size as a multiple of 320x200")
    ("fov", boost::program_options::value<double>()->value_name("DEGREES"), "Horizontal field of view at 16:10 (10-170, default 64.01)")
    ("lod-distance-multiplier", multiplier_value("--lod-distance-multiplier", result.distances.lod), "Scale model LOD switch distances (default: 10)")
    ("distance-cutoff-multiplier", multiplier_value("--distance-cutoff-multiplier", result.distances.cutoff), "Scale scenery and actor visibility distances (default: 10)")
    ("fragment-pool-size", count_value("--fragment-pool-size", result.fragments.pool_size), "Maximum airborne fragments and detached weapons (default: 600)")
    ("fragment-count-multiplier", multiplier_value("--fragment-count-multiplier", result.fragments.count_multiplier, true), "Scale fragments per burst (default: 4, zero disables bursts)");
#ifndef NDEBUG
  boost::program_options::options_description developer{"Developer options", 110};
  developer.add_options()
    ("screenshot", path_value(result.screenshot), "Save a PPM at the render resolution")
    ("frames", count_value("--frames", result.frame_limit), "Exit after N frames")
    ("headless", boost::program_options::bool_switch(&result.headless), "Render without creating a window")
    ("list-resources", boost::program_options::bool_switch(&result.list_resources), "Print original resource sizes and CRCs");
  options.add(developer);
#endif

  auto parsed{boost::program_options::command_line_parser(argc, argv).options(options)
    .style(boost::program_options::command_line_style::unix_style & ~boost::program_options::command_line_style::allow_guessing)
    .positional(boost::program_options::positional_options_description{}).run()};
  // Launch scripts can supply defaults first, then override them on the command line.
  std::ranges::reverse(parsed.options);
  std::set<std::string> seen;
  std::erase_if(parsed.options, [&](auto const &option){
    auto const &name{option.string_key};
    return !seen.insert(name == "original" || name == "expansion" ? "edition" : name).second;
  });
  boost::program_options::variables_map arguments;
  boost::program_options::store(parsed, arguments);
  if(arguments.contains("help")) {
    help << "Earthsiege Redux\nUsage: earthsiege [options]\n\n" << options;
    return {};
  }
  boost::program_options::notify(arguments);
  if(arguments.contains("original")) result.edition = game_edition::original;
  if(arguments.contains("expansion")) result.edition = game_edition::expansion;
  if(arguments.contains("music")) result.music = parse_music_device(arguments["music"].as<std::string>());
  if(arguments.contains("resolution")) result.resolution = resolution_option(arguments["resolution"].as<std::string>());
  if(arguments.contains("fov")) result.distances.zoom = cockpit_fov_zoom(arguments["fov"].as<double>());
  if(result.demo && (*result.demo < 1 || *result.demo > 4)) throw std::invalid_argument{"--demo must be between 1 and 4"};
  if(result.inspect_demo && (*result.inspect_demo < 1 || *result.inspect_demo > 4)) throw std::invalid_argument{"--inspect-demo must be between 1 and 4"};
  if(result.demo && result.inspect_demo) throw std::invalid_argument{"--demo and --inspect-demo cannot be used together"};
  return result;
}

} // namespace earthsiege

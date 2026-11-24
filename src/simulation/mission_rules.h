#pragma once
#include "resources/edition.h"

namespace earthsiege {

// Input tapes also depend on the simulation rules of the executable that recorded them.
enum class mission_rules { redux, retail_dos, expansion_dos };

constexpr mission_rules dos_mission_rules(game_edition edition) {
  return edition == game_edition::expansion ? mission_rules::expansion_dos : mission_rules::retail_dos;
}

constexpr bool uses_revised_tactics(mission_rules rules) {
  return rules != mission_rules::retail_dos;
}

} // namespace earthsiege

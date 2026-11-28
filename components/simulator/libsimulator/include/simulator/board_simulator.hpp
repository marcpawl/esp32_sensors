#pragma once

#include <cstdint>
#include "logic/board.hpp"

namespace thermo::simulator {

class BoardSimulator : public logic::Board {
  public:
    enum class Power : std::uint8_t { Off = 0, On };

    Power power_{Power::Off};
};

}  // namespace thermo::simulator

// LED status indication (§7).
#pragma once

#include <cstdint>

#include "esp_err.h"

namespace thermo {

enum class TxState : std::uint8_t {
    kTxOk = 0,         // Transmission permitted.
    kTxInhibited = 1,  // Low battery; buffering only.
};

}  // namespace thermo

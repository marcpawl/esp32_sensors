// LED status indication (§7).
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <cstdint>

namespace thermo {

enum class TxState : std::uint8_t {
    kTxOk = 0,         // Transmission permitted.
    kTxInhibited = 1,  // Low battery; buffering only.
};

}  // namespace thermo

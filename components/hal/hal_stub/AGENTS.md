# Mirrored ESP-IDF headers in `hal_stub`

`components/hal/hal_stub/include/driver/`, `.../include/hal/`, and
`.../include/soc/` are **mirrors of ESP-IDF include paths**, not a separate
stub API.

Sources keep the ESP-IDF spelling in their includes:

```cpp
#include "driver/gpio.h"
#include "soc/gpio_num.h"
```

On the `linux` target, `hal_stub` puts its `include/` directory on the include
path, so that spelling resolves to the mirrored header in `hal_stub`. On the
chip, the same line resolves to the real ESP-IDF header. Never rename the
include to a stub-only path — one source line must build for both targets.

A header under these three directories exists **only** when the real ESP-IDF
header cannot be used on the `linux` target (for example, it is not
self-contained or pulls in chip-only headers). Needing a mocked *function* is
not by itself a reason to copy its header: include the real header and supply
the implementation in a `*_stub.cpp` instead. A copied header can drift from
ESP-IDF, so every mirror needs a concrete host-only reason.

The same rule applies to all three mirrored trees: `driver/`, `hal/`, and
`soc/`. Declarations with no ESP-IDF counterpart belong under
`hal_stub/include/hal_stub/`, not in a mirrored tree.

# espidf_cpp

Small **C++ wrappers around ESP-IDF / FreeRTOS C APIs**.

It collects the thin, RAII-flavoured shims that let the firmware use the IDF
and FreeRTOS C interfaces with C++ ownership and lifetime rules, instead of
scattering raw handle management and `xTaskCreate`/`xQueueCreate` calls across
the application.

Most of it is **header-only**, but the component is not: it also compiles two
source files — `https_server.cpp` (the TLS router) and
`certs/self_signed_cert.cpp` (the bundled development certificate) — so the
Configure-mode HTTPS server and the certificate it uses live in this component
rather than in `main`.

## What lives here

| Header / source | Provides | Wraps |
|---|---|---|
| `handle_deleter.hpp` | `HandleDeleter`, `UniqueTaskHandle`, `UniqueQueueHandle` — `std::unique_ptr` deleters for FreeRTOS handles | FreeRTOS handles |
| `one_message_queue.hpp` | `OneMessageQueue<T>` — a single-slot queue whose `send()` overwrites any unread message (`send`, `send_from_isr`, `receive`) | `freertos/queue.h` |
| `task.hpp` | `TaskParams` and `Task<Runner>` — runs `Runner::run()` (`[[noreturn]]`) on a freshly created FreeRTOS task, owning the handle | `freertos/task.h` |
| `thermo_static_vector.hpp` | `static_vector<T, N>` — fixed-capacity, inline-storage sequence container standing in for C++26 `std::static_vector` | *(none)* |
| `thermo_static_string.hpp` | `static_string<N>` — fixed-capacity, always-NUL-terminated string built on `static_vector` | *(none)* |
| `https_server.hpp` + `https_server.cpp` | `HttpsServer`, `HttpRequest`, `HttpResponse`, `HttpMethod`, `HttpHandler` — a thin, page-agnostic router over `esp_https_server`. `HttpsServer` takes a `Scheme` (`kHttps` by default; `kHttp` serves plain HTTP on a chosen port, used by the native tests) | `esp_https_server` |
| `self_signed_cert.hpp` + `certs/self_signed_cert.cpp` | the bundled, self-signed development certificate (key + cert bytes) used by the Configure-mode HTTPS server | *(none — data only)* |

The two `thermo_static_*` containers carry no ESP-IDF dependency; they live here
because `task.hpp` uses `static_string` to own a copy of the task name, and
keeping them in this component avoids a dependency back on the application's
`main` component.

`https_server` and `self_signed_cert` are the exceptions to the header-only
rule: their definitions are compiled into this component's archive
(`https_server.cpp`, `certs/self_signed_cert.cpp`), so the headers only declare
them. The router owns a listener and a small URI-handler table, but knows
nothing about the Configure-mode pages — those are registered by the client via
`register_handler`/`deregister_handler` and live in `main/configure_mode.cpp`.
The firmware serves over TLS (`Scheme::kHttps`, port 443); `Scheme::kHttp` runs
a plain-HTTP listener instead, which is how the native `espidf_cpp_test` suite
drives the router over loopback (the TLS listener has no `linux` port).
The certificate is not security-sensitive for a development build, but it is
shipped as code so the server always has a usable identity.

## Relationship to the HAL

This component is **not** part of the hardware abstraction layer. The HAL
(`hal_common` / `hal_board` / `hal_stub`) abstracts *hardware*; this component
abstracts *the RTOS and ESP-IDF runtime*. The two are independent — nothing here
references the HAL, and the HAL does not reference `espidf_cpp`.

## Usage

List `espidf_cpp` in the `REQUIRES` (or `PRIV_REQUIRES`) of any component that
needs these headers, then include them by name:

```cpp
#include "task.hpp"
#include "one_message_queue.hpp"
#include "https_server.hpp"
```

The wrapper headers are header-only, so callers compile those implementations
inline; requiring the underlying IDF components keeps the FreeRTOS and
`esp_log` symbols visible to whoever includes the headers. The `https_server`
and certificate sources (`https_server.cpp`, `certs/self_signed_cert.cpp`) are
compiled into this component's archive instead, so requiring `espidf_cpp` is
enough to link the router and the certificate — no further wiring is needed by
the caller. (Because the router needs the TLS stack, requiring `espidf_cpp`
transitively pulls in `esp_https_server`.)

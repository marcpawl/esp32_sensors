# The ESP-IDF C++ wrappers

This file documents `components/espidfcpp/` — the `libespidfcpp` component and
its host test project. Repository-wide guidance (the build, the workflow, the
quality gates) is in the root `AGENTS.md`; the layering rules — which layer may
require which — are in the `platform_layers` skill
(`.opencode/skills/platform_layers/SKILL.md`).

## Layout

- `libespidfcpp/` — the wrapper component. Public headers sit in
  `libespidfcpp/include/espidf_cpp/`, so every include names the
  `espidf_cpp/` prefix; the compiled sources are in `libespidfcpp/src/`.
- `tests/test-espidf-cpp/` — the component's host test project: an ESP-IDF
  `linux` project built with Unity. It is its own project so it builds (and
  fails) independently of every other test suite.

The two live together here the way the HAL components and their tests live
together under `components/hal/`. ESP-IDF scans a component directory only one
level deep, so a project that requires `libespidfcpp` must name
`components/espidfcpp` on its `EXTRA_COMPONENT_DIRS` even though
`components/` is already there; the test project computes its own entry from
its depth in the tree.

## The component

`libespidfcpp` provides C++ wrappers around ESP-IDF and FreeRTOS code, to make
usage less error prone. The headers are in namespace `thermo::espidf_cpp`; the
compiled sources are listed in the component's `SRCS`:

| Header | Provides |
|---|---|
| `espidf_cpp/task.hpp` | `Task<Runner>` (typed `[[noreturn]]` task body), `TaskParams` |
| `espidf_cpp/mutex.hpp` | `Mutex` (owned normal or recursive FreeRTOS mutex; acquired only through `Lock`) and `Lock` (scope lock, with static `try_lock` / `try_lock_for` factories returning `std::optional<Lock>`) |
| `espidf_cpp/one_message_queue.hpp` | `OneMessageQueue<T>` (single-slot, trivially-copyable messages) |
| `espidf_cpp/timer.hpp` | `Timer` (RAII `esp_timer` owner: `start_once()`, `start_periodic()`, `restart()`, `stop()`; `restart()` is `[[thermo::isr_safe]]` and does not query the timer) |
| `espidf_cpp/handle.hpp` | `HandleDeleter`, `UniqueTaskHandle`, `UniqueQueueHandle`, `UniqueSemaphoreHandle` |
| `espidf_cpp/http_server_types.hpp` | `HttpServer` vocabulary: `HttpMethod`, `HttpField(s)`, `HttpRequest`, `HttpStatus`, `HttpResponse`, `HttpHandler`, `parse_int`, `normalize_url` (no ESP-IDF dependency) |
| `espidf_cpp/http_server.hpp` | `HttpServer<BodyCapacity>` — prefix routes over `esp_http_server` with one GET/POST handler each; fixed-size request/response buffers, bad input becomes an HTTP error (never an abort) |

The fixed-capacity containers (`static_vector`, `static_string`) live in
the lower `libutility` layer, not here; the wrappers use them from
`utility/`. See `components/utility/AGENTS.md`.

**New `.cpp` files must be added to the `SRCS` list in
`libespidfcpp/CMakeLists.txt`.** The headers are always included with the
`espidf_cpp/` prefix; the include path is the component's public one, so
consumers never name the component directory.

## The host tests

The test project lives in `tests/test-espidf-cpp/` and covers the wrappers:

- `test_one_message_queue.cpp` — the single-slot queue: creation, send and
  receive, overwrite semantics, the tick and chrono timeouts, the ISR send,
  and that copies are disabled while moves transfer the handle.
- `test_timer.cpp` — `Timer`: construction, the start/stop/query
  delegations, move-only ownership, and the restart path. The restart tests
  use the fake's `is_active_calls` / `get_period_calls` counters to prove that
  `restart()` queries neither, so it stays usable from an ISR.
- `test_mutex.cpp` — `Mutex` and `Lock`:
  - normal and recursive creation;
  - blocking acquisition through the `Lock` constructor;
  - the static `try_lock` and `try_lock_for` factories;
  - recursive re-entry;
  - releasing in a `Lock`'s destructor;
  - move-only ownership.

  `Lock` releases only when it is destroyed or moved from, so there is no
  early-unlock path to test.
- `test_http_server_types.cpp` — the vocabulary header, which has no ESP-IDF
  dependency: method and status text, `normalize_url`, `parse_int`, field
  lookup, and the fixed-capacity request and field types.
- `test_http_server.cpp` — drives a real server over a loopback socket:
  registration and unregistration, start/stop, routing (prefix and root),
  404/405, GET query fields, POST form decoding, and the bad-input responses
  (400, 413, 414, 415, 505) that must become an HTTP error rather than a
  crash.

`task.hpp` needs a real FreeRTOS scheduler, so the host suite does not exercise
it; it is verified on the target.

Build and run the suite with
`scripts/dev.py test-linux components/espidfcpp/tests/test-espidf-cpp`, or
directly in the container:

```bash
scripts/dev.py shell -C components/espidfcpp/tests/test-espidf-cpp \
    idf.py -B build_linux --preview set-target linux
scripts/dev.py shell -C components/espidfcpp/tests/test-espidf-cpp \
    idf.py -B build_linux build
./components/espidfcpp/tests/test-espidf-cpp/build_linux/test-espidf-cpp.elf
```

# The ESP-IDF C++ wrappers

This file documents `components/espidfcpp/` — the `libespidfcpp` component and
its host and on-target test projects. Repository-wide guidance (the build, the workflow, the
quality gates) is in the root `AGENTS.md`; the layering rules — which layer may
require which — are in the `platform_layers` skill
(`.opencode/skills/platform_layers/SKILL.md`).

## Layout

- `libespidfcpp/` — the wrapper component. Public headers sit in
  `libespidfcpp/include/espidf_cpp/`, so every include names the
  `espidf_cpp/` prefix; the compiled sources are in `libespidfcpp/src/`.
- `tests/test-espidf-cpp-lib/` — the shared Unity cases, registered as the
  `test-espidf-cpp-lib` component and linked WHOLE_ARCHIVE by both test
  projects.
- `tests/test-espidf-cpp-linux/` — the host project: an ESP-IDF `linux`
  project with the runner and the cases that need POSIX sockets or the
  recording `esp_timer` fake.
- `tests/test-espidf-cpp-esp32/` — the on-target project: ESP32-S3 with its
  own runner and serial-log driver for the shared cases.

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
| `espidf_cpp/ring_buffer.hpp` | `RingBuffer<T>` (no-split, trivially-copyable items, configurable item count; blocking `send`/`receive` plus ISR-only `sendFromIsr`/`receiveFromIsr`) |
| `espidf_cpp/timer.hpp` | `Timer` (RAII `esp_timer` owner: `start_once()`, `start_periodic()`, `restart()`, `stop()`; `restart()` is `[[thermo::isr_safe]]` and does not query the timer) |
| `espidf_cpp/handle.hpp` | `HandleDeleter`, `UniqueTaskHandle`, `UniqueQueueHandle`, `UniqueSemaphoreHandle` |
| `espidf_cpp/interrupt_handle.hpp` | `InterruptHandle` (RAII owner of a GPIO ISR registration, released in the destructor) |
| `espidf_cpp/http_server_types.hpp` | `HttpServer` vocabulary: `HttpMethod`, `HttpField(s)`, `HttpRequest`, `HttpStatus`, `HttpResponse`, `HttpHandler`, `parse_int`, `normalize_url` (no ESP-IDF dependency) |
| `espidf_cpp/http_server.hpp` | `HttpServer<BodyCapacity>` — prefix routes over `esp_http_server` with one GET/POST handler each; fixed-size request/response buffers, bad input becomes an HTTP error (never an abort) |

The fixed-capacity containers (`static_vector`, `static_string`) live in
the lower `libutility` layer, not here; the wrappers use them from
`utility/`. See `components/utility/AGENTS.md`.

**New `.cpp` files must be added to the `SRCS` list in
`libespidfcpp/CMakeLists.txt`.** The headers are always included with the
`espidf_cpp/` prefix; the include path is the component's public one, so
consumers never name the component directory.

## The test projects

`test-espidf-cpp-lib/src/` holds the target-neutral Unity cases: the HTTP
vocabulary, `Mutex`/`Lock`, `OneMessageQueue`, `RingBuffer`, and the
`vTaskDelay` wrapper. Both executables link it WHOLE_ARCHIVE, so a case only
has to be written once.

The host project keeps the pieces that cannot run on the chip:

- `test_main.cpp` — the runner; the host process exits with Unity's result.
- `test_http_server.cpp` — drives a real server over POSIX loopback sockets.
- `test_timer.cpp` and `fake_esp_timer.*` — the recording `esp_timer` fake,
  which cannot link beside the chip's real timer implementation.

The ESP32-S3 project keeps only its own runner and `test.py` serial driver; the
runner prints `ESPIDF_CPP_ESP32_TESTS: PASS/FAIL` for the driver to read.

The shared library also holds the `InterruptHandle` cases. They use the GPIO
ISR API: the chip project uses the real driver, while the host project runs the
same sources against `hal_stub`'s software model of ISR registration. Both
executors therefore exercise identical case code; only the driver beneath it
differs.

The component under test is one directory below `tests/`, so every project
lists `components/espidfcpp/` and `components/espidfcpp/tests/` separately in
`EXTRA_COMPONENT_DIRS`: ESP-IDF scans a component directory one level deep.

Run the host suite with
`scripts/dev.py test-linux components/espidfcpp/tests/test-espidf-cpp-linux`,
or directly in the container:

```bash
scripts/dev.py shell -C components/espidfcpp/tests/test-espidf-cpp-linux \
    idf.py -B build_linux --preview set-target linux
scripts/dev.py shell -C components/espidfcpp/tests/test-espidf-cpp-linux \
    idf.py -B build_linux build
./components/espidfcpp/tests/test-espidf-cpp-linux/build_linux/\
    test-espidf-cpp-linux.elf
```

Build the chip suite with `scripts/dev.py build-test-espidf-cpp-esp32`, then
flash/run it with
`scripts/dev.py test-esp32 components/espidfcpp/tests/test-espidf-cpp-esp32`.

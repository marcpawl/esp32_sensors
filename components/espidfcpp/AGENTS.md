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
usage less error prone. The headers are in namespace `thermo::espidf_cpp`
(`HttpServer` has one `.cpp`, listed in the component's `SRCS`):

| Header | Provides |
|---|---|
| `espidf_cpp/task.hpp` | `Task<Runner>` (typed `[[noreturn]]` task body), `TaskParams` |
| `espidf_cpp/one_message_queue.hpp` | `OneMessageQueue<T>` (single-slot, trivially-copyable messages) |
| `espidf_cpp/handle.hpp` | `HandleDeleter`, `UniqueTaskHandle`, `UniqueQueueHandle` |
| `espidf_cpp/static_vector.hpp` | fixed-capacity, inline-storage vector |
| `espidf_cpp/static_string.hpp` | fixed-capacity, NUL-terminated string (`try_push_back` / `try_append` for the non-aborting path) |
| `espidf_cpp/http_server_types.hpp` | `HttpServer` vocabulary: `HttpMethod`, `HttpField(s)`, `HttpRequest`, `HttpStatus`, `HttpResponse`, `HttpHandler`, `parse_int`, `normalize_url` (no ESP-IDF dependency) |
| `espidf_cpp/http_server.hpp` | `HttpServer<BodyCapacity>` — prefix routes over `esp_http_server` with one GET/POST handler each; fixed-size request/response buffers, bad input becomes an HTTP error (never an abort) |

**New `.cpp` files must be added to the `SRCS` list in
`libespidfcpp/CMakeLists.txt`.** The headers are always included with the
`espidf_cpp/` prefix; the include path is the component's public one, so
consumers never name the component directory.

## The host tests

The test project lives in `tests/test-espidf-cpp/` and covers the wrappers:

- `test_one_message_queue.cpp` — the single-slot queue: creation, send and
  receive, overwrite semantics, the tick and chrono timeouts, the ISR send,
  and that copies are disabled while moves transfer the handle.
- `test_static_vector.cpp` — growth, clearing, `try_push_back`, `emplace_back`,
  `resize`, iterators, and the abort paths when the capacity is exceeded.
- `test_static_string.cpp` — construction, appending, assignment,
  `try_push_back` / `try_append`, comparison and ordering, embedded NULs, and
  the abort paths past capacity.
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

The abort paths use standalone programs whose names end in `_assert`
(`test_static_vector_assert.cpp`, `test_static_string_assert.cpp`), with the
case tables in the matching `*_assert_cases.hpp` headers. Each program takes a
test-case name as its only argument and exits 0 when the argument is missing or
unknown; the Unity test forks and execs it with a case name and confirms the
child aborted. The forking helpers are in `assert_test_support.hpp` (plain
program side, no Unity) and `assert_unity_support.hpp` (test side). Keep new
abort coverage in that shape.

Build and run the suite with `scripts/dev.py test-espidf-cpp`, or directly in
the container:

```bash
scripts/dev.py shell -C components/espidfcpp/tests/test-espidf-cpp \
    idf.py -B build_linux --preview set-target linux
scripts/dev.py shell -C components/espidfcpp/tests/test-espidf-cpp \
    idf.py -B build_linux build
./components/espidfcpp/tests/test-espidf-cpp/build_linux/test-espidf-cpp.elf
```

# The shared containers

This file documents `components/utility/` — the `libutility` component and its
host test project. Repository-wide guidance is in the root `AGENTS.md`; the
layering rules are in the `platform_layers` skill
(`.opencode/skills/platform_layers/SKILL.md`).

## Why this is its own layer

`libutility` is the **lowest** layer in the tree: it owns the fixed-capacity
containers and nothing else, and it depends on no other component.

It is separate from `libespidfcpp` because the HAL needs it. The shared 1-Wire
vocabulary (`hal/onewire_types.hpp`, in `hal_common`) names `static_vector` in
the `OneWireLike` concept, and both `hal_board` and `hal_stub` return it. A
component may only use layers below it, so the container cannot live in
`libespidfcpp` (layer 3) and be used by the HAL (layers 1 and 2).

## Layout

- `libutility/` — the component. Headers sit in `libutility/include/utility/`,
  so every include names the `utility/` prefix and never the component
  directory.
- `tests/test-utility/` — the component's host test project: an ESP-IDF
  `linux` project built with Unity, runnable on its own.

ESP-IDF scans a component directory only one level deep, so a project that
requires `libutility` names `components/utility` on its `EXTRA_COMPONENT_DIRS`
even though `components/` is already there.

## The containers

The headers are in namespace `thermo::utility`. Storage is inline, so neither
type allocates:

| Header | Provides |
|---|---|
| `utility/static_vector.hpp` | `static_vector<T, N>`: fixed-capacity, inline-storage sequence; `try_push_back` reports overflow instead of aborting |
| `utility/static_string.hpp` | `static_string<N>`: fixed-capacity, NUL-terminated string (`try_push_back` / `try_append` for the non-aborting path) |

Neither depends on ESP-IDF or FreeRTOS, so both build for the chip and the
host. Exceptions are disabled in the firmware; exceeding a capacity is a hard
error, which is why the `try_` forms exist for input that is not under the
caller's control.

Use these instead of `std::vector` and `std::string` whenever the maximum size
is known (see the `cpp-coding-standards` skill, "ESP guidelines").

## The host tests

`tests/test-utility/` covers both containers:

- `test_static_vector.cpp` — construction (default and iterator range),
  growth, clearing, `try_push_back`, `emplace_back`, `resize`, iterators,
  and the abort paths when the capacity is exceeded.
- `test_static_string.cpp` — construction, appending, assignment,
  `try_push_back` / `try_append`, comparison and ordering, embedded NULs, and
  the abort paths past capacity.

The abort paths use standalone programs whose names end in `_assert`
(`test_static_vector_assert.cpp`, `test_static_string_assert.cpp`), with the
case tables in the matching `*_assert_cases.hpp` headers. Each program takes a
test case name as its only argument and exits 0 when the argument is missing or
unknown; the Unity test forks and execs it with a case name and confirms the
child aborted. The forking helpers are in `assert_test_support.hpp` (plain
program side, no Unity) and `assert_unity_support.hpp` (test side). Keep new
abort coverage in that shape.

Run the suite with `scripts/dev.py test-utility`, or directly in the container:

```bash
scripts/dev.py shell -C components/utility/tests/test-utility \
    idf.py -B build_linux --preview set-target linux
scripts/dev.py shell -C components/utility/tests/test-utility \
    idf.py -B build_linux build
./components/utility/tests/test-utility/build_linux/test-utility.elf
```

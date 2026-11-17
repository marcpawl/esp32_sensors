// `[[thermo::isr_safe]]` — the repository's interrupt-safety contract.
//
// The attribute has no compiler meaning: it records a promise made by the
// declaration it annotates. `[[thermo::isr_safe]]` on a function means the
// function may be called both from an interrupt handler and from task context;
// on a function-typed parameter it means the function passed in must itself be
// ISR-safe.
//
// An ISR-safe function must not block, allocate on the heap, take a mutex, or
// call a non-ISR-safe API. It may take a critical section and use the
// `...FromISR` FreeRTOS entry points.
//
// Unknown attributes are normally diagnosed (`-Wattributes` on GCC,
// `-Wunknown-attributes` on clang). Because this annotation is deliberate and
// has no compiler semantics, `cmake/compiler-warnings.cmake` disables that
// diagnostic on this repository's own targets. Do not use an unknown attribute
// for anything else.
//
// Include this header where the annotation is used, so the convention has one
// definition point.
#pragma once

# Plan — Implicit stepping on the shared Δ

Spec: `docs/superpowers/specs/2026-07-03-implicit-stepper-design.md`

1. **Test first** — `tests/sim/test_implicit.cpp` (spec §4) + registration in
   tests/CMakeLists.txt. Fails to compile (no `aleph.sim:implicit` yet).
2. **`aleph.sim-implicit.cppm`** — `ImplicitError`, `ShiftedLaplacian`,
   `ImplicitDiffuseStepper`, `ImplicitWaveStepper` (spec §1); register in
   graph/src/aleph.sim/CMakeLists.txt + umbrella `export import :implicit;`.
3. **Gate** — full ctest; then the ASan preset
   (`LSAN_OPTIONS=suppressions=tests/asan.supp`).
4. **Doc** — one line in README run table is NOT needed (no new binary); the
   spec is the reference.

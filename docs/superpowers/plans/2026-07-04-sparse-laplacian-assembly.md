# Plan — Sparse assembly of the weighted Laplacian

Spec: `docs/superpowers/specs/2026-07-04-sparse-laplacian-assembly-design.md`

1. **Test first** — `tests/flow/test_sparse_laplacian.cpp` (spec §3) +
   registration. Fails to compile (no SparseWeightedLaplacian yet).
2. **laplacian.cppm** — factor `bounded_curvatures`/`local_curvatures` (pure
   code motion; dense outputs byte-identical), add `SparseWeightedLaplacian`,
   `assemble_sparse`, `build_laplacian_bounded_sparse`,
   `build_laplacian_local_sparse`.
3. **sim steppers** — template `step` over the operator (wave/diffuse/vector),
   `cfl_ok` CsrMatrix overload.
4. **Gate** — full ctest (existing Tier-1 dense byte-exactness must be
   untouched) + new suite; ASan subset for flow+sim.
5. **Bench** — sparse series + per-row value-exactness assert; rerun both
   sweeps; overlay in fig_a; draft §7/§8 + LaTeX update; push; CI.

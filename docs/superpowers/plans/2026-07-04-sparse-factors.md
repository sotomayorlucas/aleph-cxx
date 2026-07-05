# Plan — Sparse factors for the implicit path

Spec: `docs/superpowers/specs/2026-07-04-sparse-factors-design.md`

1. **Test first** — `tests/sim/test_implicit_sparse.cpp` (spec §2) +
   registration. Fails to compile (no CsrMatrix make yet).
2. **implicit.cppm** — factor → `std::variant<LDLT, SparseLdlt>`; CSR `make`
   overload (O(nnz) shift with diagonal insertion); templated stepper `make`s.
3. **Gate** — full ctest (dense implicit suite untouched) + ASan subset.
4. **Bench** — `--family factors` mode + data/factors.csv; numbers into
   draft §6/§7/§8 + LaTeX; push; CI.

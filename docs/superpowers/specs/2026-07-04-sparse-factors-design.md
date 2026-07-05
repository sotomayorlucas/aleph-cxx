# Design Spec — Sparse factors for the implicit path (I + βΔ via SparseLdlt)

**Goal:** the operator assembly is now sparse (spec 2026-07-04a) but every
SOLVER factor is still dense: `ShiftedLaplacian` (implicit steppers) builds a
dense `I + βΔ` and dense-LDLᵀ-factors it — O(n²) memory and O(n³) factor per
(Δ, dt). This slice gives the implicit-stepping path an end-to-end sparse
pipeline: CSR assembly → CSR shift → `SparseLdlt` factor → sparse solve.

Date 2026-07-04 · Status: approved (user: "dale con los factores sparse").
Already present: `SparseLdlt` (`aleph.linalg.sparse:ldlt_sparse`, Davis-style
elimination tree + symbolic factor, PSD, kernel-aware, deterministic natural
ordering — ported from Rust and used by `IncrementalLaplacian` above its
threshold).

## 1. Components

### 1.1 `ShiftedLaplacian` carrier (aleph.sim:implicit)
`factor` becomes `std::variant<LDLT, SparseLdlt>` (the `LaplacianFactor`
precedent, flow.cppm). Two `make` overloads:
- `make(const DMatrix& delta, f64 beta)` — unchanged dense path (existing
  bits preserved; existing tests must stay green).
- `make(const CsrMatrix& delta, f64 beta)` — builds `H = I + β·Δ` directly in
  CSR, O(nnz): per row, copy (col, β·value) pairs; if the row has a diagonal
  entry add 1.0 to it, else insert one at its sorted position (our
  SparseWeightedLaplacian always stores diagonals, but make() must not
  assume it). `SparseLdlt::factorize(H)` → `FactorFailed` on error.
`solve` dispatches via `std::visit` (both alternatives expose
`solve(span) -> optional<vector<f64>>`).

### 1.2 Implicit steppers (aleph.sim:implicit)
`ImplicitDiffuseStepper::make` / `ImplicitWaveStepper::make` become templates
over the operator carrier (`DMatrix` or `CsrMatrix`), delegating to the
matching `ShiftedLaplacian::make`. `step` bodies unchanged (they only call
`op.solve`).

### 1.3 Honest FP contract (same shape as the matvec finding)
A sparse-factor solve is NOT bitwise-equal to the dense-factor solve
(different elimination/computation order). Contract: solutions agree within
solver roundoff on well-conditioned SPD systems (`I + βΔ` has eigenvalues in
[1, 1+βλ_max]; gate 1e-9), and each path is individually byte-deterministic
run-to-run. Trajectories: same — equivalent within tolerance, reproducible
bitwise against themselves.

### 1.4 Bench: `--family factors` (bench_scaling)
Own CSV (`data/factors.csv`):
`grid,nodes,edges,t_dense_factor_ms,t_sparse_factor_ms,t_dense_solve_ms,t_sparse_solve_ms`.
Per lattice grid (8..64): assemble both carriers, time
`ShiftedLaplacian::make` dense (median; SINGLE run ≥ grid 48 — O(n³) is the
point being demonstrated) vs sparse (median), and one `solve` each (median).
Correctness cross-check per grid: dense-vs-sparse solve of the same rhs
within 1e-9 (exit nonzero otherwise).

## 2. Tests (tests/sim/test_implicit_sparse.cpp)
1. Sparse make + uniform fixed point; mass conservation at huge dt; variance
   decreasing; stability 100× past CFL (mirrors the dense suite on the CSR
   carrier).
2. Dense-vs-sparse step agreement ≤ 1e-9 per entry (single step and
   32-step trajectory ≤ 1e-8), and bitwise self-reproducibility of the
   sparse trajectory.
3. `make(CsrMatrix)` on a diagonal-less CSR (from_parts) still succeeds
   (diagonal inserted); non-symmetric CSR → FactorFailed; beta < 0 →
   InvalidShift; dt ≤ 0 → InvalidShift.
4. Wave sparse path: uniform zero-velocity fixed point; huge-dt boundedness.

## 3. Non-goals
Helmholtz's indefinite Bunch–Kaufman stays dense (sparse symmetric-indefinite
factorization — pivoting destroys the symbolic structure — is a research
slice of its own; documented in the paper's §8). `IncrementalLaplacian`
migration to the sparse carrier (its dense `.laplacian` field is load-bearing
for Helmholtz) is follow-up. Fill-reducing orderings (AMD etc.) are follow-up
— natural ordering is deterministic and sufficient at editor scale.

# Design Spec — Sparse assembly of the weighted Laplacian (kill the O(n²) knee)

**Goal:** the per-edit operator rebuild is now O(touched) in curvature work
(spec 2026-07-03) but still O(n²) in ASSEMBLY: `detail::assemble` allocates and
zeroes a dense n×n `DMatrix` per rebuild, and the steppers matvec that dense
matrix every frame. This is the visible knee in the paper's fig_a (t_local
climbs from ~20 ms to ~110–130 ms between |E|≈2k and 8k at CONSTANT dirty).
This slice adds a **sparse carrier + sparse assembly with bit-identical
values** and generic stepper matvec, and measures both.

Date 2026-07-04 · Status: approved (user: "dale con el operador sparse").
Infrastructure already present: `CsrMatrix` (`aleph.linalg.sparse:csr`, with
`matvec`/`get`/`is_symmetric`) and `SparseLdlt` (used by IncrementalLaplacian
above SPARSE_THRESHOLD). What is missing is the sparse *assembly* and a
sparse-capable sim path.

## 1. Bit-exactness argument (the load-bearing part)

- **Diagonal:** dense `assemble` iterates the curvature map in canonical
  skeleton-edge order doing `at(a,a) += w; at(b,b) += w`. For a given node v,
  the sequence of additions is the subsequence of incident edges in canonical
  order. `assemble_sparse` iterates the SAME map in the SAME order
  accumulating `diag[idx] += w` — identical FP addition sequence per node ⇒
  bit-identical diagonal values.
- **Off-diagonals:** single writes of `-w` — order-free.
- **matvec:** NOT bit-identical across carriers, and the spec must not claim
  it (measured: 1 ulp on a 5×5 grid row). Both loops visit columns ascending
  and zero entries are exact no-ops, but ISO-mode FP contraction
  (`-ffp-contract=on`) leaves fma-vs-mul+add per expression to the compiler's
  discretion, and it may choose differently for the two loop shapes (dense
  contiguous vs CSR gather). Forcing contraction off in the dense matvec
  would change existing golden-baseline bits — off the table. The honest
  contract: per-entry agreement within a few ulps (relative 1e-14 gate), and
  EACH carrier individually byte-deterministic run-to-run. Stepper
  trajectories on the sparse carrier are therefore value-equivalent (tight
  tolerance) to dense, and bit-reproducible against themselves.

## 2. Components

### 2.1 Shared curvature construction (pure refactor, aleph.flow:laplacian)
Factor the κ-map construction out of `build_laplacian_bounded` /
`build_laplacian_local` into `detail::bounded_curvatures(skel, shared,
radius)` and `detail::local_curvatures(skel, shared, prev_curv, dirty,
weight-independent; rc, radius)`, preserving iteration/insertion order
token-exactly (the existing dense outputs MUST stay byte-identical — gated by
the existing Tier-1 suite).

### 2.2 `SparseWeightedLaplacian` (exported, aleph.flow:laplacian)
```cpp
struct SparseWeightedLaplacian {
    std::vector<NodeId> node_order;   // same canonical order as dense
    CsrMatrix           matrix;       // Δ = D_w − A_w; diagonal ALWAYS stored (nnz = n + 2|E|)
    RicciMap            curvatures;
    [[nodiscard]] bool is_symmetric(f64 eps) const;   // CsrMatrix::is_symmetric
    [[nodiscard]] bool ones_in_kernel(f64 eps) const; // matvec(ones) ≈ 0
};
```
`detail::assemble_sparse(skel, curvatures, weight_fn)`: one pass over the map
(canonical order) accumulating `diag[]` + collecting off-diagonal (row, col,
−w) triples; per-row sort by column index (unique keys, deterministic);
diagonal inserted at its sorted position, ALWAYS present (isolated vertex ⇒
explicit 0.0). Cost O(V + E log deg) — no n² anywhere.

### 2.3 Sparse builds (exported)
`build_laplacian_bounded_sparse(g, weight_fn, radius)` and
`build_laplacian_local_sparse(g_after, const SparseWeightedLaplacian& prev,
dirty, weight_fn, rc, radius)` — same curvature code as 2.1, sparse assembly.
Theorem 1's proof carries over verbatim (same κ values, same canonical
assembly order).

### 2.4 Generic stepper matvec (aleph.sim)
`WaveStepper::step`, `DiffuseStepper::step`, `VectorDiffuseStepper::step`
become templates over the operator type (constrained: `.rows()`, `.cols()`,
`.matvec(span)->vector`). Call sites are unchanged (deduction). Add a
`CsrMatrix` overload of `WaveStepper::cfl_ok` (Gershgorin = max row abs-sum
over stored values). `ShiftedLaplacian`/Helmholtz stay dense (their factors
are dense; conversion via `to_dense()` when needed — out of scope).

### 2.5 Bench + figures
`measure_edit` additionally times sparse-full and sparse-local (threading a
sparse `prev` alongside the dense one), asserts VALUE-exactness sparse vs
dense (`get(i,j) == at(i,j)` for all i,j) per row, and appends two CSV
columns `t_full_sp_ms,t_local_sp_ms`. plot_scaling.py overlays the sparse
local series in fig_a (slot 4) — the knee should flatten to
O(dirty·ball + V + E).

## 3. Tests (tests/flow/test_sparse_laplacian.cpp)
1. Value-exactness: `sparse.matrix.get(i,j) == dense.matrix.at(i,j)` for ALL
   (i,j), bitwise, on the 6×6-grid-plus-attachment fixture; nnz == n + 2|E|;
   is_symmetric; ones_in_kernel.
2. Localized sparse == full sparse, bitwise, over the multi-edit trace
   (mirror of the Tier-1 dense gate).
3. matvec bitwise: `sparse.matrix.matvec(x) == dense.matrix.matvec(x)`
   entry-bitwise for a non-trivial finite x.
4. Stepper-carrier equivalence: N wave steps on dense vs sparse carriers give
   bit-identical u, v trajectories (same for diffuse).

## 4. Non-goals (follow-ups)
Migrating `EditorController::operator_`, `IncrementalLaplacian`, and
Helmholtz to sparse-first storage (dense factors remain the solver
bottleneck; a sparse-factor slice is separate); localizing the MV
certificate.

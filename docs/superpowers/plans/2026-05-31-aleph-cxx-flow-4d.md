# Sub-phase 4d — `aleph.flow` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: subagent-driven-development / executing-plans. **PORT, not new math** — port each partition from its Rust file and pin it with the oracle values from the Rust tests.

**Goal:** Port the dynamical layer `aleph.flow` (optimal transport, Ollivier-Ricci curvature, weighted Laplacian, heat flow with incremental rank-k updates) from Rust to C++26, closing Phase 4.

**Rust reference:** `/home/lkz/aleph-engine/aleph-flow/src/*.rs` + ground-truth oracles in `/home/lkz/aleph-engine/aleph-flow/tests/{wasserstein_cycles,wasserstein2_correctness,flow_smoke,incremental_property,sparse_*}.rs`.

**Spec:** section 7 of `docs/superpowers/specs/2026-05-27-aleph-cxx-graph-design.md`.

**Tech:** GCC 16, C++26 modules, `aleph_flags_isa` (no exceptions; `std::expected`/`std::optional`). Builds on merged `aleph.linalg.sparse`, `aleph.sheaf` (`:skeleton`), `aleph.graph/types`, `aleph.dpo` (`RewriteRecord`).

**Scope:** full 4d (all 5 flow partitions + the sparse extension). Tag `v0.3.3-flow` → closes Phase 4.

---

## Cross-cutting (apply throughout)
- **Determinism (spec 7.5):** all f64, no parallelism, fixed column-major iteration order, stable `node_order`. Tolerances copied VERBATIM from Rust tests: `1e-9` for direct factor/solve round-trips, `1e-6/1e-7` for incremental-vs-fresh rank-k, `1e-4` for the 64-case proptest (compounded over rewrites). Sinkhorn `tol=1e-9` (guaranteed only for `ε≥1e-3`).
- No exceptions: `std::expected<T,E>` for errors; kernel-aware `solve` returns `std::optional` ("b not in range").
- CMake: new `graph/src/aleph.flow/CMakeLists.txt` — `add_library(aleph_flow)`, link `PUBLIC aleph_graph aleph_types aleph_containers aleph_linalg_sparse aleph_sheaf aleph_dpo aleph_math`, `PRIVATE aleph_flags_isa`; `add_subdirectory(src/aleph.flow)` in `graph/CMakeLists.txt`; register `tests/flow/test_*.cpp` + `aleph_iso_test(flow aleph_flow)`.

---

## Wave 0 — extend `aleph.linalg.sparse` (BLOCKING; reworks already-merged code)

**Files:** Modify `foundation/src/aleph.linalg.sparse/{aleph.linalg.sparse-dense,-csr,-ldlt}.cppm` + umbrella; CREATE `aleph.linalg.sparse-ldlt_sparse.cppm`; UPDATE `tests/linalg/test_sparse.cpp` (the existing strict-PD test changes).

- [ ] **`:dense` helpers** (port `linalg_f64.rs`): `DMatrix::{from_rows, identity, zeros, add, scale, approx_eq, frobenius_norm_diff}`. Oracle: `identity(3).matmul(identity(3)).approx_eq(identity(3),1e-12)`.
- [ ] **`:ldlt` REWORK** (port `ldlt.rs` — **the critical item**): replace the strict-PD factorize with the Rust **work-copy Gaussian-elimination** LDLᵀ (`PIVOT_EPS=1e-12`, **accepts zero pivots as kernel directions** — graph Laplacians are singular), add `reconstruct()` (=L·diag(D)·Lᵀ), rework `solve()→std::optional` (kernel guard), add `rank_1_update(α,v)` (Davis 1999 eq.13) + `rank_k_update`. Extend `LdltError` with `NotSymmetric`, `NotPsd(index)`, `DimMismatch`. **Update `test_sparse.cpp`** (the old NotPositiveDefinite-on-singular expectation no longer holds; a singular PSD now factorizes). Oracles (from `ldlt.rs` tests): `factorize([[4,2,0,0],[2,5,0,0],[0,0,1,0],[0,0,0,9]]).reconstruct()≈M (1e-9)`; rank-1 update matches fresh factorize of `A+α vvᵀ` to `1e-7`.
- [ ] **`:csr` helpers** (port `linalg_f64_sparse.rs`): `CsrMatrix::{to_dense, get(binary_search), transpose, is_symmetric, from_dense_eps, empty}`. Oracle: `from_dense→to_dense` round-trip to `1e-12` over 5 seeds.
- [ ] **`:ldlt_sparse`** (NEW, port `sparse_ldlt.rs`): `elimination_tree(CsrMatrix)`, `symbolic_factor` (Davis §4.3, `std::set` for sorted merge), `SparseLdlt{factorize→expected, solve→optional}`. Oracles (from `sparse_ldlt.rs`, `sparse_solve_consistency.rs`, `sparse_dense_parity.rs`): elim-tree of diagonal = all roots; sparse solve matches dense solve; `path_64` uses sparse path.
- [ ] Umbrella `export import :ldlt_sparse;`; CMake FILE_SET += new file. Build + ctest + **release-strict 0 warnings** + commit.

---

## Wave 1 — optimal transport `:wasserstein1` + `:wasserstein2` (~280 LOC, independent)

- [ ] **`:wasserstein1`** (`graph/src/aleph.flow/aleph.flow-wasserstein1.cppm`, port `wasserstein.rs`): W₁ via primal transportation simplex over a dense cost matrix. **Port the Charnes RHS perturbation (1e-12) and EPS (1e-10) constants exactly** (LP determinism). Oracles (`wasserstein_cycles.rs`): `W1(dirac,dirac)=0`; `W1=cost` for a 2-point transport; symmetry.
- [ ] **`:wasserstein2`** (port `wasserstein2.rs`): W₂ via **log-domain Sinkhorn-Knopp** (log-sum-exp), params `ε/tol/max_iter`. Oracles (`wasserstein2_correctness.rs`, 12 cases): `log_sum_exp([0,0])=ln2`; marginal constraints; converges for `ε≥1e-3`.
- [ ] Build + ctest, strict 0 warnings, commit.

---

## Wave 2 — `:ollivier_ricci` (~220 LOC) — depends on Wave 1 + sheaf:skeleton

- [ ] Port `ollivier_ricci.rs`: for each `Adjacent` edge `(u,v)`, `κ(u,v)=1 − W_p(μ_u,μ_v)/d_shortest(u,v)` with uniform neighbor measures, `p∈{1,2}`; returns `OrderedMap<pair<NodeId,NodeId>, f64>`. Uses Wave-1 W₁/W₂ + `aleph.sheaf:skeleton` (mesh 1-skeleton) + graph shortest paths. Oracle (`flow_smoke.rs`): 2 adjacent meshes → `κ≈0` (μ₀=(0,1),μ₁=(1,0),W₁=1,κ=0); known small graph.
- [ ] Build + ctest, strict 0 warnings, commit.

---

## Wave 3 — `:laplacian` (~180 LOC) — depends on Wave 2

- [ ] Port `laplacian.rs`: `WeightedLaplacian Δ=D_w−A_w` with `w(u,v)=exp(−κ(u,v))` (weight fn as a plain function pointer, not `std::function`, to stay `aleph_flags_isa`-clean); dense `DMatrix`, stable `node_order`. Oracles: `Δ` symmetric on a 4-cycle; `Δ` PSD on 256 random graphs; row sums = 0.
- [ ] Build + ctest, strict 0 warnings, commit.

---

## Wave 4 — `:flow` (~300 LOC, HIGH risk) — depends on Wave 3 + sparse(extended) + dpo

- [ ] Port `flow.rs`: heat IVP `∂φ/∂t=−Δφ` via implicit Euler `(I+dt·Δ)φ_{n+1}=φ_n`; cached LDLᵀ factorization (dense for `n<SPARSE_THRESHOLD=32`, else `:ldlt_sparse`); `apply_rewrite(RewriteRecord)` does the rank-k Laplacian update (or refactor). **Port `jacobi_eigh` tie-breaking (first-found p<q) exactly** for bit-identical eigvecs. Oracles (`flow_smoke.rs`, `incremental_property.rs`): empty graph → empty Laplacian; **heat conserves total mass to `1e-9`** over many steps on a closed graph; **rank-k updated factorization matches full recompute to `1e-6`** across 256 graphs+rewrites.
- [ ] Build + ctest, strict 0 warnings, commit. **Tag `v0.3.3-flow` → Phase 4 complete.**

---

## Success criteria (4d)
- `ctest` green incl. `iso_flow` + per-partition tests + the Rust-ported oracle suites (`wasserstein*`, `sparse_*`, `flow_smoke`, `incremental_property`).
- `release-strict` 0 warnings.
- Determinism: heat-flow + Sinkhorn bit-identical across runs (same machine).
- Numerical certificates: `Δ` PSD; mass conserved `1e-9`; rank-k vs fresh `1e-6`; W between diracs = distance.

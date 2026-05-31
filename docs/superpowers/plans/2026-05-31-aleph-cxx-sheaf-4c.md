# Sub-phase 4c — `aleph.sheaf` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development or executing-plans. Steps use `- [ ]` checkboxes. **This is a PORT, not new math** — implement each partition by porting its Rust reference and pinning it with the oracle values copied from the Rust tests.

**Goal:** Port the topological layer `aleph.sheaf` (cellular Z/2 sheaf cohomology over the scene's flag complex) from Rust to C++26 modules, preserving determinism and the numerical certificates.

**Rust reference (authoritative — READ THE MATCHING FILE before each task):** `/home/lkz/aleph-engine/aleph-sheaf/src/*.rs` (+ ground-truth oracle values in `aleph-engine/aleph-sheaf/tests/{cohomology_regression,connecting_morphism}.rs`).

**Spec:** section 6 of `docs/superpowers/specs/2026-05-27-aleph-cxx-graph-design.md`.

**Tech:** GCC 16, C++26 modules, `aleph_flags_isa` (no exceptions; `std::expected`). Builds on the already-merged `aleph.linalg.gf2`, `aleph.graph`, `aleph.types`, `aleph.containers`.

**Scope recommendation:** implement **Wave 0–4 below (cohomology core + DEC + Mayer-Vietoris)** as sub-phase 4c. **Defer `:zigzag`** (760 LOC, highest risk) to a follow-on **4c.1** — the spec's own risk table reserves this fallback. Tag `v0.3.2-sheaf` after Wave 4.

---

## Cross-cutting prerequisites & adaptations (apply throughout)

1. **`std::hash<Simplex>` + `SimplexLess`** ship in `:simplex`; every `OrderedMap<Simplex,V>` stalk depends on them. `NodeId` has `<,==,!=` + `std::hash` but no `<=>`.
2. **Containers:** `OrderedMap` is **move-only** (copy `=delete`). Cover pieces that must be cloned (MV's U/K/R) use `std::vector<Simplex>` / `FlatSet`, not `OrderedMap`. There is **no `OrderedSet`** — use sorted+deduped `std::vector` / `FlatSet<Simplex,SimplexLess>`; use `OrderedMap<K,std::monostate>` only where insertion order is load-bearing.
3. **API drift (Rust→C++ gf2):** the C++ `BitMatrix` uses different names than Rust and is missing methods (Wave 0 fixes this). The sheaf trait method is `stalk_dim`/`dim_stalk` + `restriction(sigma,tau)->BitMatrix` — pick `dim_stalk`/`restriction` and use consistently across `:sheaf_trait` and cluster C (`:connecting` calls `lift_basis_index`).
4. **Subgraph seam:** `:subgraph` is a pure value-type view (induced OneSkeleton/FlagComplex) only. Its `h0_with_dim` becomes a free function `compute_subgraph_h0(sg, host, sheaf)` in `:cohomology` (avoids `:subgraph`→cohomology dependency inversion; keeps `iso_sheaf` clean).
5. **Determinism (spec 6.5):** `Simplex` = sorted-ascending `std::vector<NodeId>`; FlagComplex levels sorted via `SimplexLess`; cohomology bases emitted in row-reduce discovery order. H⁰ golden snapshots must be byte-identical across runs.
6. **CMake:** new `graph/src/aleph.sheaf/CMakeLists.txt` — `add_library(aleph_sheaf)` + `FILE_SET CXX_MODULES`; link `PUBLIC aleph_graph aleph_types aleph_containers aleph_linalg_gf2 aleph_math` (math for SPD/DEC R3), `PRIVATE aleph_flags_isa`. Add `add_subdirectory(src/aleph.sheaf)` to `graph/CMakeLists.txt`; register `tests/sheaf/test_*.cpp` in `tests/CMakeLists.txt` and `aleph_iso_test(sheaf aleph_sheaf)` in `tests/isolation/CMakeLists.txt`.

---

## Wave 0 — extend `aleph.linalg.gf2::BitMatrix` (BLOCKING prereq for cluster C)

**Files:** Modify `foundation/src/aleph.linalg.gf2/aleph.linalg.gf2-bitmatrix.cppm` (+ `:bitvec` if needed). Port the missing ops from `aleph-engine/aleph-sheaf/src/linalg_gf2.rs`.

- [ ] **Write failing tests** in `tests/linalg/test_gf2.cpp` for the new ops (oracles below).
- [ ] **Add methods** (GF(2) semantics, all mod 2):
  - `BitVec apply(const BitVec& x) const;` — M·x (x sized cols → result sized rows; each result bit = parity of row∧x).
  - `BitMatrix mul(const BitMatrix& rhs) const;` — matrix product over GF(2).
  - `bool is_zero() const noexcept;`
  - `static BitMatrix from_cols(std::span<const BitVec> cols, std::size_t nrows);` — build from column vectors.
  - `std::vector<BitVec> image_basis() const;` — basis of the column space (independent columns after reduction).
  - `std::vector<BitVec> kernel_basis() const;` — alias/confirm existing `null_space()` returns a kernel basis (cols-wide).
  - `BitVec reduce_modulo_image(const BitVec& v, std::span<const BitVec> image) const;` (or a free helper) — reduce `v` by an image basis; zero result ⇔ `v ∈ span(image)`.
- [ ] **Oracles:** `apply(I_n, x) == x`; `mul` associativity on a small example; `M.apply(k)==0` for every `k` in `kernel_basis()`; `image_basis().size() == rank()`; `reduce_modulo_image(col_i)==0` for each original column. Build + `ctest`, **release-strict 0 warnings**, commit.

---

## Wave 1 — cluster A: combinatorial substrate (~570 LOC)

Port near-1:1 from `simplex.rs / union_find.rs / skeleton.rs / flag_complex.rs / subgraph.rs`. Parallelizable after `:simplex` lands (others key on it).

- [ ] **`:simplex`** (`graph/src/aleph.sheaf/aleph.sheaf-simplex.cppm`, port `simplex.rs`): `using Simplex = std::vector<NodeId>` (sorted/deduped); `make_simplex(span)`, `dim`, `faces_of_dim(s,k)` (lexicographic combination order), `struct SimplexLess`, `std::hash<Simplex>` specialization.
  Oracles (from `simplex.rs` tests): `make_simplex({2,0,2,1})=={0,1,2}`; `dim` of vertex/edge/triangle = 0/1/2; `faces_of_dim(triangle,1)=={{0,1},{0,2},{1,2}}`; `faces_of_dim(s,k).size()==C(|s|,k+1)`; permutation-invariance of hash/==.
- [ ] **`:union_find`** (port `union_find.rs`): `UnionFind{make_set,find,union_,component_count}` backed by `OrderedMap<NodeId,NodeId>` (deterministic roots). Rename `union`→`union_`. Oracles: singletons → n components; chain of unions → 1; idempotent `make_set`.
- [ ] **`:skeleton`** (port `skeleton.rs`): `OneSkeleton` over Mesh-vertex pairs from `EdgeKind::Adjacent`; filter endpoints to mesh nodes. Oracles: 4-cycle of meshes → 4 verts/4 edges; non-Adjacent edges ignored.
- [ ] **`:flag_complex`** (port `flag_complex.rs`, **MEDIUM risk** — Bron-Kerbosch-with-pivot): `FlagComplex` = all-dim clique closure of the 1-skeleton; levels `std::vector<std::vector<Simplex>>` sorted by `SimplexLess`. Oracles: triangle → one 2-simplex; 4-cycle → no 2-simplex; tetra K4 → one 3-simplex + 4 triangles; clique count closed-form.
- [ ] **`:subgraph`** (port `subgraph.rs`, value-type surface ONLY): `Subgraph{vertices,edges}` + induced `one_skeleton()`/`flag_complex()`. **Do NOT** port `h0_with_dim` here (→ free fn in `:cohomology`). Oracles: induced complex of a known subset matches by hand.
- [ ] Build + `ctest` (+ `iso_sheaf` smoke importing only `:simplex`/etc.), strict 0 warnings, commit.

---

## Wave 2 — cluster B: sheaf interface + sheaves (~820 LOC)

- [ ] **`:sheaf_trait`** (port `sheaf_trait.rs`): C++26 concept `CellularZ2Sheaf` requiring `dim_stalk(const Simplex&)->size_t` and `restriction(const Simplex& sigma, const Simplex& tau)->BitMatrix`. Reconcile `lift_basis_index` naming with `:connecting`.
- [ ] **`:sheaf_constant`** (port `constant_sheaf.rs`, low risk): `ConstantZ2Sheaf` — `dim_stalk≡1`, identity restrictions. Oracles: `H^1(ConstantZ2Sheaf)` counts 1-cycles (4-cycle→1, tree→0).
- [ ] **`:sheaf_visibility`** (port `sheaf.rs`): `VisibilitySheaf` — `F(σ)=⋂_{v∈σ} lights influencing v` via `EdgeKind::Influences`; stalks `OrderedMap<Simplex, FlatSet<NodeId>>` (no OrderedSet → FlatSet). `restriction` = inclusion as a `BitMatrix`. Oracles (popcount-per-row): stalk dims on the canonical fixture match by hand; restriction is a 0/1 inclusion matrix.
- [ ] **`:sheaf_spd`** (port `spd_sheaf.rs`, **HIGHEST risk in B** — Householder QR): SPD 3×3 stalks (use `aleph::math::Vec3`/float[3] to stay render-free), restrictions per Adjacent pair. Port the QR sign choice (`alpha=-alpha` when `a(k,k)>0`), `tau=2/vᵀv`, and `1e-300` degenerate guards EXACTLY. Oracles: QR reconstructs A (Q·R≈A, 1e-9); restriction SPD-preserving on a known pair.
- [ ] Build + `ctest`, strict 0 warnings, commit.

---

## Wave 3 — cluster C: cohomology core (~540 LOC) — depends on Wave 0 + A + B

Port `cochain.rs / cohomology.rs / connecting.rs`. Oracle dim values are **verbatim from the Rust regression tests** (ground truth).

- [ ] **`:cochain`** (port `cochain.rs`): `CochainLayout` (k-simplex index maps from FlagComplex) + `coboundary_matrix(sheaf, complex, k) -> BitMatrix` for δ⁰/δ¹. Oracle: **δ∘δ == 0** (self-validating) on every fixture; uses the Wave-0 `mul`/`is_zero`.
- [ ] **`:cohomology`** (port `cohomology.rs`): `compute_h0` (union-find over the sheaf on the complex) and `compute_hk` templated on `CellularZ2Sheaf` via rank-nullity: `dim H^k = nullity(δ^k) − rank(δ^{k-1})` using `BitMatrix::rank`/`kernel_basis`. Also the free `compute_subgraph_h0` (the moved Subgraph seam). Oracles (from `cohomology_regression.rs`): H⁰/H¹ of cube, tetra, 4-cycle, triangulated disk == the table's exact integers; H⁰ golden snapshots byte-identical.
- [ ] **`:connecting`** (port `connecting.rs`, **HIGH risk**): `∂: H^0(F|_K) → H^1(F)` as a `BitMatrix`; consumes Wave-0 `apply`/`image_basis`/`from_cols`/`reduce_modulo_image` + `lift_basis_index`. Oracles (from `connecting_morphism.rs`): `rank(∂)` matches the test's values on the glue fixtures.
- [ ] Build + `ctest`, strict 0 warnings, commit.

---

## Wave 4 — cluster D (partial): DEC + Mayer-Vietoris (~700 LOC) — defer `:zigzag`

- [ ] **`:dec`** (port `dec.rs`, FEASIBLE, can start right after Wave 1): `Coeffs` concept (`zero/add/neg/==`), `Form<C>{k, coeffs}`, `d(FlagComplex, Form)`; impls `Z2`(bool), `R64`(double), `R3`(`aleph::math::Vec3`). Oracle: **`d(d(form))==0`** for Z2/R64/R3 over 100 random forms (watch R3/float exactness — use exact small-integer coeffs in the oracle).
- [ ] **`:mayer_vietoris`** (port `mayer_vietoris.rs`, depends on C): `decompose_rewrite(RewriteRecord) -> {U,K,R}` (value-type cover) + the certificate `dim H⁰(G') == dim H⁰(U)+dim H⁰(R)−dim H⁰(K)+rank(∂)`. Oracle: the integer identity holds exactly on the 4 fixtures + 256 random rewrites (`ε_sheaf == rank(∂)`).
- [ ] Build + `ctest`, strict 0 warnings, commit. **Tag `v0.3.2-sheaf`.**

## Deferred — 4c.1: `:zigzag` (~760 LOC, highest risk)
Port `zigzag.rs` (zigzag persistence over DPO trajectories; barcode births/deaths; trajectory MV invariant `Σ ε_sheaf ≥ deaths_forward`). Its own plan + careful review; trajectory passed by pointer (Graph is move-only).

---

## Success criteria (4c)
- `ctest` green incl. `iso_sheaf` + per-partition tests + the regression/connecting oracle tables.
- `release-strict` 0 warnings (the gate we hold).
- H⁰ golden snapshots byte-identical across runs (determinism).
- MV dim formula + `d²=0` certificates pass on all fixtures.
- `tla_cxx_sync` still passes (no new TLA-tracked enums here).

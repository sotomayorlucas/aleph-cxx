# Helmholtz Acoustics on the Shared Δ — Implementation Plan (physics slice)

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use `- [ ]`. This is a **faithful term-for-term port** from Rust — READ the Rust source as you port; the Rust tests are the oracle.

**Goal:** port a dense Bunch-Kaufman indefinite LDLᵀ + a `HelmholtzOperator` solving `(Δ−k²I)φ=source` on the shared `IncrementalLaplacian` + minimal audio source/receiver.

**Rust ground truth:** `/home/lkz/aleph-engine/aleph-audio/src/{bk_ldlt.rs, helmholtz.rs, source.rs, receiver.rs}` + tests `/home/lkz/aleph-engine/aleph-audio/tests/{bk_ldlt_properties.rs, analytical.rs, audio_smoke.rs}` + in-file `#[cfg(test)]`.
**Spec:** `docs/superpowers/specs/2026-06-06-helmholtz-acoustics-design.md` (REVISED-2 — read §2/§5).
**Port rules (load-bearing):** Rust `DMatrix::{get,set}` → C++ `DMatrix::at(i,j)` (read) / `at(i,j)=v` (write) — NO get/set (cf. ldlt.cppm:27); `kBkAlpha = 0.640'388'2` LITERAL (not computed); reverse loops `for (std::size_t i=n; i-->0;)` (ldlt.cppm:148); error idiom = bare `enum class` + `Info{kind,index}` struct (ldlt.cppm:50); `aleph_flags_isa` → `std::expected`/`std::optional`, no exceptions; inline-in-class. **Conventions:** `cmake --build build-release && ctest --test-dir build-release`; strict `cmake --build build-release-strict 2>&1 | grep "warning:" | grep -v '^ninja:' | wc -l` → 0 (watch `-Wsign-conversion`/`-Wshadow`).

---

## Task 1: BkLdlt — dense Bunch-Kaufman indefinite LDLᵀ

**Files:** `foundation/src/aleph.linalg.sparse/aleph.linalg.sparse-bk_ldlt.cppm` (new), `…/aleph.linalg.sparse.cppm` + `…/CMakeLists.txt`, `tests/linalg/test_bk_ldlt.cpp` (new) + `tests/CMakeLists.txt`.

- [ ] **Step 1 — partition skeleton.** New `aleph.linalg.sparse-bk_ldlt.cppm`: `module;` + `#include <vector>/<optional>/<expected>/<cmath>/<cstddef>`; `export module aleph.linalg.sparse:bk_ldlt;` `import :dense;` `export namespace aleph::linalg::sparse { … }`. Register in the module's `CMakeLists.txt` `FILE_SET CXX_MODULES`; add `export import :bk_ldlt;` to `aleph.linalg.sparse.cppm`. Build (empty) to confirm wiring.

- [ ] **Step 2 — types.** Port from `bk_ldlt.rs`: `kBkAlpha = 0.640'388'2`; `enum class BkError { NotSquare, NotSymmetric, Singular }`; `struct BkErrorInfo { BkError kind; std::size_t index{0}; friend bool operator==(...) = default; }`; a `DBlock` tagged 1×1/2×2 (e.g. a struct with a `bool is_two` + `f64 a` (1×1) / `f64 d11,d12,d22` (2×2), or a small variant — match the Rust `enum DBlock { One(f64), Two(f64,f64,f64) }`); `struct BkLdlt { std::size_t n; DMatrix l; std::vector<DBlock> d; std::vector<std::size_t> perm; }`.

- [ ] **Step 3 — `factorize`** (port `bk_ldlt.rs` factorize + choose_pivot term-for-term, `get/set`→`at`): square + symmetric(1e-7) validation → `std::unexpected(BkErrorInfo{...})`. The `choose_pivot` FULL sequence (spec §2.1: `j==n-1`→1×1; `ω₁<1e-14`→1×1; the α-cascade with `kBkAlpha`; 1×1-swap-r; 2×2-swap-r→j+1). Symmetric Schur updates (1×1 and 2×2); 2×2 uses `inv_det=1.0/det` ONCE then multiply (`l₁=inv_det*(d22*a−d12*b)`, `l₂=inv_det*(−d12*a+d11*b)`). Singularity guards (1×1 `|pivot|<1e-14`, 2×2 `|det|<1e-14`) → `Singular(j)`.

- [ ] **Step 4 — `solve`** (port term-for-term): P (forward) → `L·z=y` forward-subst → D block-apply (1×1 divide; 2×2 `inv_det` multiply form; temp named `b2`) → `Lᵀ·u=w` backward-subst with `for (std::size_t i=n; i-->0;)` → Pᵀ (inverse). Returns `std::optional<std::vector<f64>>`.

- [ ] **Step 5 — tests** (`tests/linalg/test_bk_ldlt.cpp`; register in `tests/CMakeLists.txt`). Port `bk_ldlt.rs` in-file tests + `bk_ldlt_properties.rs` + the relocated Green's-function (spec §5 BkLdlt list): `diag(1,2,3)`→3×One d[i]==i+1; SPD 3×3→`d.size()==3`; 2×3→`NotSquare`; `[[ε,1],[1,ε]]` ε=1e-3→one `DBlock::Two`; indefinite 3×3→dims sum 3; **round-trips** diag(1,−2,3,−4) b=[1,2,3,4]→x=[1,−1,1,−1] (rel<1e-9), the 3×3 + 5×5 residual<1e-9; **Dirichlet Green's** — hand-build the 5×5 tridiag(2,−1), solve b[2]=1 → `[0.5,1.0,1.5,1.0,0.5]` (per-node abs<1e-9, `G=lo*(n−hi)/n`, `n=n_interior+1=6`); **determinism** (factor+solve twice byte-identical). Build a `DMatrix` from rows for these (use `DMatrix::from_rows`/`at`).

- [ ] **Step 6 — build + run + strict.** `--test-case="*bk*"` all pass; full `ctest`; strict 0. **Commit** `feat(linalg): dense Bunch-Kaufman indefinite LDLᵀ (bk_ldlt) — ported from aleph-audio`.

---

## Task 2: HelmholtzOperator + AudioSource + Microphone

**Files:** `graph/src/aleph.sim/aleph.sim-helmholtz.cppm` (new), `…/aleph.sim.cppm` + `…/CMakeLists.txt`, `tests/sim/test_helmholtz.cpp` (new) + `tests/CMakeLists.txt`.

- [ ] **Step 1 — partition + build edge.** New `aleph.sim-helmholtz.cppm`: `export module aleph.sim:helmholtz;` `import aleph.flow; import aleph.linalg.sparse; import aleph.types; import :section;` Register in `CMakeLists.txt`; add `export import :helmholtz;` to `aleph.sim.cppm`. **ADD `aleph_flow` PUBLIC to `aleph_sim` `target_link_libraries`** (new acyclic edge — confirm it builds; `aleph_linalg_sparse` already PUBLIC). Confirm `IncrementalLaplacian`'s exact API first: `git grep "struct IncrementalLaplacian" -A40 graph/src/aleph.flow/aleph.flow-flow.cppm` — note `.laplacian` (DMatrix), `from_graph`, and `solve`'s signature/return (`std::optional<std::vector<f64>>`).

- [ ] **Step 2 — `AudioSource`/`Microphone`** (port `source.rs`/`receiver.rs`): `kSpeedOfSound=340.0`; `AudioSource{mesh_anchor, frequency_hz, amplitude}` with `k_squared()=(2π·f/340)²` (use `(2π·f/340)` squared — one mult, no `pow`) and `source_vector(order)` one-hot (find the anchor's index in `order`, amplitude there else 0); `Microphone{mesh_anchor}` with `sample(phi, order)` (phi at the anchor's index, 0 if absent).

- [ ] **Step 3 — `HelmholtzOperator`** (port `helmholtz.rs`, spec §2.2): `HelmholtzError{FactorFailed}`; `HelmholtzFactor{enum Kind{Psd,Indefinite}; std::optional<BkLdlt> bk;}`; `make(const aleph::flow::IncrementalLaplacian& flow, f64 k²)` — `|k²|<1e-12` → `{Psd, nullopt}`; else copy `flow.laplacian`, `for i: H.at(i,i)-=k²`, `BkLdlt::factorize(H)` → unexpected(FactorFailed) on error, else `{Indefinite, bk}`. `solve(source, flow)` — Psd: `m=sum/n; projected[i]=source[i]−m; return flow.solve(projected);` (UNCHANGED result, no post-projection). Indefinite: `return factor.bk->solve(source);`. (Optional `matrix(flow)` reconstructing `flow.laplacian−k²I` for the residual test.)

- [ ] **Step 4 — tests** (`tests/sim/test_helmholtz.cpp`; register). Build `IncrementalLaplacian::from_graph` (like the Rust) for the Helmholtz tests; the audio/source tests are pure. From spec §5: **branching** (path graph: k²=0→`factor.kind==Psd`; k²=0.5→`Indefinite`); **residual** (5-node path, k²=0.1, source=[0,0,1,0,0] → `‖(flow.laplacian−k²I)·φ−source‖₂<1e-9`); **PSD projection** (path graph Laplacian k²=0, a source → `solve` finite AND `‖flow.laplacian·φ−projected_source‖₂<1e-9`); **audio** (`k_squared` monotonic/zero; `source_vector`/`sample` exact values per spec §5); **smoke** (4-node path → from_graph → AudioSource → make(flow,0) → solve(source_vector,flow) → Microphone.sample is `std::isfinite`); **determinism**.

- [ ] **Step 5 — build + run + strict.** `--test-case="*[hH]elmholtz*"` + `--test-case="*udio*"` pass; full `ctest`; strict 0. Confirm the wave/diffuse/section sim tests are unaffected. **Commit** `feat(sim): Helmholtz acoustics on the shared Δ + AudioSource/Microphone — ported from aleph-audio`.

---

## Final verification
- [ ] BkLdlt: all round-trip + Dirichlet Green's + diag/SPD/forced-2×2/indefinite oracles pass; determinism byte-identical.
- [ ] Helmholtz: branch (Psd@k=0 / Indefinite@k>0), residual<1e-9, PSD-projection residual<1e-9, audio bridge values, end-to-end smoke finite.
- [ ] `ctest` all pass (sim/linalg/flow unaffected); release-strict 0. The one-substrate point holds: Helmholtz consumes the same `IncrementalLaplacian` Δ.

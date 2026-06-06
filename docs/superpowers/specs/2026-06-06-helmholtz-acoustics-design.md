# Design Spec — Helmholtz Acoustics on the Shared Δ (physics slice)

**Goal:** solve the **Helmholtz equation `(Δ − k²I) φ = source`** on the SAME Ollivier-Ricci-weighted Laplacian Δ that drives graphics + the wave/heat physics — *sound* on the one shared substrate (the manifesto thesis). `Δ − k²I` is **indefinite** for k² past the spectral gap, and the C++ only has PSD LDLᵀ — so this slice ports a **dense Bunch-Kaufman symmetric-indefinite LDLᵀ** + a `HelmholtzOperator` + minimal `AudioSource`/`Microphone`. Date 2026-06-06 · Status: REVISED-2 (adversarial review vs the Rust source applied). Faithful port of `/home/lkz/aleph-engine/aleph-audio/` — its tests are the oracle ([[aleph-engine-rust-port-source]]). Real-valued f64. Next slice of [[one-substrate-physics-direction]].

Context (verified, explorer `ad8876bf` + review vs the Rust): the C++ flow layer ALREADY has **`IncrementalLaplacian`** (`aleph.flow-flow.cppm:178`) — a 1:1 analog of the Rust `IncrementalLaplacian`: field `.laplacian` (the dense `DMatrix` Δ, :180), a cached `LaplacianFactor factor` (`std::variant<LDLT, SparseLdlt>`, :159), `from_graph` factory (:186), and a public **`solve(b) → std::optional<std::vector<f64>>`** (:334, kernel-aware: returns nullopt when b ∉ range(Δ)). Helmholtz reuses this cached factor for the PSD case — the one-substrate point. `DMatrix` exposes **`at(i,j)` (read-write ref), NOT get/set** (`aleph.linalg.sparse-dense.cppm:60`; the Rust `get/set` map to `at` — ldlt.cppm:27 documents this). The error idiom is a bare `enum class` + an `Info` struct carrying an index (`LdltError`/`LdltErrorInfo`, ldlt.cppm:50). `aleph.sim` (`:section`/`:wave`/`:diffuse`, umbrella re-export, `aleph_flags_isa` → no exceptions/`std::expected`). No `std::complex` anywhere (port stays real). aleph.sim partitions are single-defined (the dual-IR gotcha is aleph.lowering-only).

## 1. Approach — faithful port, three pieces mirroring `aleph-audio/`
- **`BkLdlt`** (new partition `aleph.linalg.sparse:bk_ldlt`) — dense Bunch-Kaufman indefinite `P·H·Pᵀ = L·D·Lᵀ` (L unit-lower-tri; D block-diagonal of 1×1/2×2 pivots), `factorize`/`solve`. The general indefinite solver the codebase lacks (lives in linalg, reusable). Port `bk_ldlt.rs` (241 lines) term-for-term, mapping Rust `DMatrix::{get,set}` → C++ `DMatrix::at(i,j)` (read) / `at(i,j) = v` (write).
- **`HelmholtzOperator`** (new partition `aleph.sim:helmholtz`) — `make(const IncrementalLaplacian& flow, f64 k²)`: if `|k²| < 1e-12` → **PSD** (`HelmholtzFactor::Psd`, stores NOTHING — reuses `flow`'s cached factor at solve time, exactly as the Rust); else → clone `flow.laplacian`, subtract k² on the diagonal, `BkLdlt::factorize` → **Indefinite** (holds the `BkLdlt`). `solve(source, const IncrementalLaplacian& flow)` (takes `flow` again, like Rust `solve(&source,&flow)`): PSD → mean-subtract the source, then `flow.solve(projected)`; Indefinite → `bk.solve(source)`.
- **`AudioSource` + `Microphone`** (in `:helmholtz`, the physics↔frequency bridge) — `AudioSource{mesh_anchor, frequency_hz, amplitude}`: `k_squared()=(2π·f/kSpeedOfSound)²` (`kSpeedOfSound=340`), `source_vector(node_order)`=one-hot. `Microphone{mesh_anchor}`: `sample(φ, node_order)`.

**One-substrate:** Helmholtz consumes the SAME `IncrementalLaplacian` the flow/sim layers cache — sound is a different solve on the shared, already-factored Δ (PSD reuses the cached factor; indefinite re-factors the k²-shifted matrix). **Deterministic** (fixed BK pivot order, pure f64). Real-valued frequency-domain (complex/absorbing boundaries out, §7).

## 2. Components

### 2.1 `BkLdlt` — Bunch-Kaufman LDLᵀ (`aleph.linalg.sparse:bk_ldlt`)
Port `bk_ldlt.rs` exactly. Dense (`DMatrix`), real f64. **All element access is `DMatrix::at(i,j)`** (no get/set).
```cpp
inline constexpr aleph::math::f64 kBkAlpha = 0.640'388'2;  // Rust BK_ALPHA literal (truncated (1+√17)/8); DO NOT recompute via std::sqrt — match the oracle's pivot decisions bit-for-bit.
enum class BkError { NotSquare, NotSymmetric, Singular };
struct BkErrorInfo { BkError kind; std::size_t index{0};   // index meaningful only for Singular (Rust Singular(j))
    [[nodiscard]] friend bool operator==(const BkErrorInfo&, const BkErrorInfo&) = default; };  // mirror LdltErrorInfo, ldlt.cppm:50
struct DBlock { /* tagged: One(f64) | Two(f64 d11, d12, d22) */ };
struct BkLdlt {
    std::size_t n; DMatrix l; std::vector<DBlock> d; std::vector<std::size_t> perm;  // perm = Rust Vec<usize>
    [[nodiscard]] static std::expected<BkLdlt, BkErrorInfo> factorize(const DMatrix& H);
    [[nodiscard]] std::optional<std::vector<f64>> solve(const std::vector<f64>& b) const;
};
```
- **`factorize`:** validate square (→`BkError::NotSquare`) + symmetric within `1e-7` (→`NotSymmetric`). Iterate j, selecting a pivot via the FULL `choose_pivot` sequence (do not abbreviate — the diag oracle needs the ω₁ guard):
  1. `if j == n-1` → 1×1 at j (last column is always 1×1).
  2. `ω₁ = max_{i>j} |A(i,j)|`, argmax `r`. `if ω₁ < 1e-14` → 1×1 at j (already-eliminated/zero-below column).
  3. `if |A(j,j)| ≥ kBkAlpha·ω₁` → 1×1 at j.
  4. `ω_r = max_{i∈[j,n), i≠r} |A(i,r)|`. `if |A(j,j)|·ω_r ≥ kBkAlpha·ω₁²` → 1×1 at j.
  5. `if |A(r,r)| ≥ kBkAlpha·ω_r` → 1×1, swap r→j.
  6. else → 2×2, swap r→j+1.
  Symmetric Schur-complement update for BOTH 1×1 and 2×2; the 2×2 L-column update uses `inv_det = 1.0/det` computed ONCE then MULTIPLIED: `l₁ = inv_det·(d₂₂·a − d₁₂·b)`, `l₂ = inv_det·(−d₁₂·a + d₁₁·b)` (mirror bk_ldlt.rs:93 exactly — multiply, not `/det`, for FP-order fidelity). Singularity: 1×1 `|pivot| < 1e-14` → `BkErrorInfo{Singular, j}`; 2×2 `|det| < 1e-14` → `Singular(j)`.
- **`solve`:** apply P (forward `y[i]=b[perm[i]]`) → forward-subst `L·z=y` → block-diagonal D (1×1 divide; 2×2 block-inverse with the same `inv_det·(…)` multiply form) → backward-subst `Lᵀ·u=w` → apply Pᵀ (`x[perm[i]]=u[i]`). **size_t porting idioms:** the backward loop MUST use `for (std::size_t i = n; i-- > 0;)` (cf. ldlt.cppm:148 — `i >= 0` underflows); name the D-block temp `b2` (Rust bk_ldlt.rs:158) to avoid shadowing `b` under `-Wshadow`.
- **CRITICAL (load-bearing risk):** the 2×2 pivot branch (index/symmetry bookkeeping) — a subtle error gives wrong-but-finite results caught ONLY by the §5 residual oracles. Port the Rust elimination loop term-for-term.

### 2.2 `HelmholtzOperator` (`aleph.sim:helmholtz`)
Mirror `helmholtz.rs` 1:1. Carrier mirrors the Rust `HelmholtzFactor` enum (cite the variant precedent `LaplacianFactor`, flow.cppm:159):
```cpp
enum class HelmholtzError { FactorFailed };   // value-less tag (Rust map_err(|_| FactorFailed) DISCARDS the BkError)
struct HelmholtzFactor { enum class Kind { Psd, Indefinite } kind; std::optional<BkLdlt> bk; };  // Psd stores NOTHING
struct HelmholtzOperator {
    std::size_t n; aleph::math::f64 k_squared; HelmholtzFactor factor;
    [[nodiscard]] static std::expected<HelmholtzOperator, HelmholtzError>
        make(const aleph::flow::IncrementalLaplacian& flow, aleph::math::f64 k_squared);
    [[nodiscard]] std::optional<std::vector<f64>>
        solve(const std::vector<f64>& source, const aleph::flow::IncrementalLaplacian& flow) const;
    // optional: matrix(flow) reconstructing flow.laplacian − k²I for the residual test.
};
```
- **`make`:** `if |k²| < 1e-12` → `factor{Psd, nullopt}` (no factorization — the PSD solve reuses `flow`'s cached factor). Else: `DMatrix H = flow.laplacian;` (copy) then `for i: H.at(i,i) -= k²;` and `BkLdlt::factorize(H)` → on `BkErrorInfo` return `std::unexpected(HelmholtzError::FactorFailed)` (discard the index, per Rust); else `factor{Indefinite, std::move(bk)}`.
- **`solve`:** **PSD** — subtract the **mean of the SOURCE only** (`m = sum(source)/n; projected[i] = source[i] − m`), then `return flow.solve(projected);` (the cached kernel-aware LDLT; return its result UNCHANGED — NO post-projection). Apply this **unconditionally** at PSD (do not "detect kernel first"). The result φ is defined up to a constant kernel component (the kernel-aware LDLT pins the kernel coordinate). **Indefinite** — `return factor.bk->solve(source);`.
- Reuses `IncrementalLaplacian` read-only; NO flow-layer change.

### 2.3 `AudioSource` / `Microphone` (`aleph.sim:helmholtz`)
```cpp
inline constexpr aleph::math::f64 kSpeedOfSound = 340.0;
struct AudioSource { aleph::types::NodeId mesh_anchor; f64 frequency_hz; f64 amplitude;
    [[nodiscard]] f64 k_squared() const;                                          // (2π·f/340)²
    [[nodiscard]] std::vector<f64> source_vector(const std::vector<aleph::types::NodeId>& order) const; };  // one-hot @ anchor
struct Microphone { aleph::types::NodeId mesh_anchor;
    [[nodiscard]] f64 sample(const std::vector<f64>& phi, const std::vector<aleph::types::NodeId>& order) const; };  // 0 if absent
```

### 2.4 Module/build wiring
- `foundation/src/aleph.linalg.sparse/`: add `aleph.linalg.sparse-bk_ldlt.cppm` (`export module aleph.linalg.sparse:bk_ldlt;` importing `:dense`), register in its `CMakeLists.txt` `FILE_SET CXX_MODULES`, add `export import :bk_ldlt;` to `aleph.linalg.sparse.cppm`.
- `graph/src/aleph.sim/`: add `aleph.sim-helmholtz.cppm` (`export module aleph.sim:helmholtz;` importing `aleph.flow`, `aleph.linalg.sparse`, `aleph.types`, `:section`), register in `CMakeLists.txt`, add `export import :helmholtz;` to `aleph.sim.cppm`. **ADD `aleph_flow` to `aleph_sim`'s `target_link_libraries` as PUBLIC** (new edge — `IncrementalLaplacian` is in `make`'s exported signature; ACYCLIC: aleph_flow does NOT depend on aleph_sim, verified). `aleph_linalg_sparse` is already PUBLIC-linked (covers BkLdlt/LDLT).
- **Definitions inline-in-class** — `BkLdlt`/`HelmholtzOperator` are concrete (f64), so the sim *template* hazard (section.cppm:33) does not strictly apply, but every existing concrete factor (`LDLT`, the steppers) is inline-in-struct: follow that uniform house style (don't "optimize" to out-of-line).
- Tests: `tests/linalg/test_bk_ldlt.cpp`, `tests/sim/test_helmholtz.cpp` — register in `tests/CMakeLists.txt` source list (the `aleph_tests` target already links `aleph_flow`+`aleph_sim`, tests/CMakeLists.txt:112 — no new link edges).

## 3. Determinism
Bunch-Kaufman pivot selection is a deterministic function of the entries (fixed tie-breaks: first max); `kBkAlpha` is a copied compile-time **literal** (no `std::sqrt` at runtime — bit-identical to the Rust BK_ALPHA, so pivot decisions match the oracle bit-for-bit); pure f64, no RNG. Same `(Δ, k²)` ⇒ byte-identical factor + solve run-to-run. `k_squared` uses one `*`/division (no `pow`). Consistent with SPEC §7.

## 4. Error handling (`aleph_flags_isa`)
No exceptions/RTTI. `BkLdlt::factorize` → `std::expected<BkLdlt, BkErrorInfo>` (bare enum + Info-with-index, mirroring LdltErrorInfo); `solve` → `std::optional`. `HelmholtzOperator::make` → `std::expected<…, HelmholtzError>`; `HelmholtzError::FactorFailed` is a **value-less tag** — `make` maps any `BkErrorInfo` to it (Rust `map_err(|_| FactorFailed)`, discarding the index). `solve` → `std::optional` (PSD nullopt if the projected RHS is still out of range; indefinite nullopt only on a degenerate D-block). No allocation beyond the dense factor + solution vectors.

## 5. Testing — port the Rust oracles with their EXACT expected values
**BkLdlt (`tests/linalg/test_bk_ldlt.cpp`)** — from `bk_ldlt.rs` in-file tests + `tests/bk_ldlt_properties.rs` + the relocated Green's-function oracle:
- `factorize(diag(1,2,3))` → 3× `DBlock::One`, d[i]==i+1 (exact, <1e-12). *(Depends on the ω₁<1e-14 guard.)*
- 3×3 SPD `[[4,2,0],[2,5,1],[0,1,6]]` → `d.size()==3` (all 1×1).
- 2×3 zeros → `BkErrorInfo{NotSquare}`.
- `[[ε,1],[1,ε]]`, ε=1e-3 → `d.size()==1`, a `DBlock::Two` (forced 2×2).
- indefinite `[[1,2,3],[2,-1,4],[3,4,5]]` → block dims sum to 3.
- **round-trip oracles:** diag(1,−2,3,−4), b=[1,2,3,4] → x=[1,−1,1,−1] (rel <1e-9); `[[1,2,3],[2,-1,4],[3,4,5]]`, b=[6,5,12] → `‖H·x−b‖/‖b‖ < 1e-9`; 5×5 `[[4,1,-1,2,0],[1,-3,1,0,1],[-1,1,2,-1,0],[2,0,-1,5,1],[0,1,0,1,-2]]`, b=[1,2,3,4,5] → rel residual <1e-9.
- **Green's function (relocated here — a DIRECT BkLdlt round-trip on a PD matrix, NOT a Helmholtz/PSD test):** hand-build the 5×5 **Dirichlet tridiagonal** (diag 2.0, off-diag −1.0; PD, no kernel), `BkLdlt::factorize`, `solve(b)` with `b[2]=1.0` → per-node `|φ[i] − G[i,2]| < 1e-9` where `G[i,j] = lo·(n − hi)/n` with `lo=min(i+1,j+1)`, `hi=max(i+1,j+1)`, `n = n_interior + 1 = 6` (compute from n_interior=5, NOT a bare literal 6). Expected `φ = [0.5, 1.0, 1.5, 1.0, 0.5]`. (Mirrors `analytical.rs`; it pins BkLdlt on a PD matrix — it does NOT exercise the PSD projection.)
**HelmholtzOperator (`tests/sim/test_helmholtz.cpp`)** — from `tests/analytical.rs` branch/residual + the audio bridge; tests build `IncrementalLaplacian::from_graph` like the Rust:
- **Branching:** a path-graph `IncrementalLaplacian` at k²=0 → `factor.kind == Psd`; at k²=0.5 → `Indefinite` (assert on `.kind`, mirroring Rust `matches!(…, Psd)`).
- **Residual (k²>0):** 5-node path, k²=0.1, source=[0,0,1,0,0] → `‖(flow.laplacian − k²I)·φ − source‖₂ < 1e-9` (self-check against the reconstructed matrix, weighting-independent — Ricci `default_weight` is fine).
- **PSD projection (the projection has NO Rust value-oracle — add a C++ residual check):** a path-graph graph Laplacian (constant in kernel) at k²=0, a source → assert `solve` returns a finite φ AND `‖flow.laplacian·φ − projected_source‖₂ < 1e-9` (range-of-Δ RHS; pins the mean-projection numerically, which the Rust suite leaves to the branch-tag + finiteness smoke only). Do NOT assert it reproduces the Dirichlet Green's function.
**AudioSource/Microphone** (same file): `k_squared(0)≈0`; `k_squared(200) > k_squared(100)`; `source_vector(anchor=5, order=[2,5,9], amp=2.5)==[0,2.5,0]`; missing anchor → all-zero; `sample(anchor=7, phi=[.1,.4,.9], order=[3,7,11])==0.4`; missing → 0.
**End-to-end smoke:** 4-node path graph → `IncrementalLaplacian::from_graph` → `AudioSource(ids[0], f, amp)`, k²=0 → `HelmholtzOperator::make(flow, 0)` → `solve(source_vector, flow)` → `Microphone(ids[3]).sample(...)` is `std::isfinite`.
**Determinism:** factorize+solve twice on the same matrix → byte-identical.

## 6. Cost / when it runs
Dense Bunch-Kaufman: `O(n³)` factorize, `O(n²)` solve — on the bounded-support Δ the sim uses (n ≤ a few hundred), a one-shot steady-state solve (NOT per-frame; recomputed when k or the graph changes). The bounded Δ is already dense (`DMatrix`). **§7 hook:** a sparse blocked indefinite factorization for large n is a separate, larger slice.

## 7. Scope boundary (YAGNI)
**In:** dense real `BkLdlt` (the general indefinite solver), `HelmholtzOperator` `(Δ−k²I)` on the shared `IncrementalLaplacian` (PSD-via-cached-factor @ k≈0 / indefinite-BkLdlt @ k>0), minimal `AudioSource`/`Microphone`, the ported oracles. **Out (hooks kept):**
- *Sparse/blocked indefinite factorization* — dense BK is fine at bounded-Δ sizes.
- *Complex Helmholtz / absorbing (impedance) boundaries / radiation conditions* — the Rust + this port are real-valued frequency-domain.
- *Full audio rendering* — convolution, multi-frequency synthesis, spatialization, time-domain coupling to the wave stepper. `AudioSource`/`Microphone` are just the RHS/sampling bridge.
- *A dedicated `aleph.audio` module* — source/receiver live in `aleph.sim:helmholtz` for now; promote if audio grows.
- *Flow-layer changes* — Helmholtz consumes `IncrementalLaplacian` read-only.

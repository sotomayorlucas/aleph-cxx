# Design Spec — Helmholtz Acoustics on the Shared Δ (physics slice)

**Goal:** solve the **Helmholtz equation `(Δ − k²I) φ = source`** on the SAME Ollivier-Ricci-weighted Laplacian Δ that drives graphics + the wave/heat physics — i.e. *sound* on the one shared substrate (the manifesto thesis: one factorized Δ, many physical signals). The blocker is numerical: `Δ − k²I` is **indefinite** for k² past the spectral gap, and the C++ only has PSD LDLᵀ. So this slice ports a **Bunch-Kaufman symmetric-indefinite LDLᵀ** + a `HelmholtzOperator` + a minimal `AudioSource`/`Microphone`. Date 2026-06-06 · Status: DRAFT. Port from the Rust ground truth `/home/lkz/aleph-engine/aleph-audio/` — its tests are the oracle (see [[aleph-engine-rust-port-source]]). Real-valued f64 (the Rust is real; frequency-domain, no complex/absorbing boundary). Next slice of [[one-substrate-physics-direction]].

Context (verified, explorer `ad8876bf`): `aleph.flow::WeightedLaplacian.matrix` is the dense `aleph::linalg::sparse::DMatrix` Δ that `WaveStepper`/`LDLT` already consume — Helmholtz **clones it + subtracts k² on the diagonal** (reuses the SAME Δ; one-substrate). The C++ PSD solvers (`LDLT` dense, `SparseLdlt`) reject negative pivots → can't do indefinite. `aleph.sim` is the physics layer (`:section`/`:wave`/`:diffuse` partitions, umbrella re-export, inline-in-class, `aleph_flags_isa` → no exceptions/`std::expected`). No `std::complex` anywhere (port stays real). The dual-IR gotcha ([[lowering-ir-structs-dual-defined]]) is aleph.lowering-only — aleph.sim/aleph.linalg.sparse partitions are single-defined.

## 1. Approach
Three ported pieces, mirroring `aleph-audio/`:
- **`BkLdlt`** (new partition `aleph.linalg.sparse:bk_ldlt`) — dense Bunch-Kaufman symmetric-indefinite factorization `P·H·Pᵀ = L·D·Lᵀ` (L unit-lower-tri, D block-diagonal of 1×1/2×2 pivots), with `factorize`/`solve`. The general indefinite solver the codebase lacks (lives in linalg, not audio — reusable). Port `bk_ldlt.rs` (241 lines) faithfully.
- **`HelmholtzOperator`** (new partition `aleph.sim:helmholtz`) — `new(const WeightedLaplacian&, k_squared)`: clones `Δ.matrix`, subtracts `k²` from each diagonal. If `|k²| < 1e-12` → **PSD path** (factor Δ with the existing `LDLT`, which accepts the zero/kernel pivot; `solve` projects out the constant kernel exactly as `helmholtz.rs`). Else → **indefinite path** (`BkLdlt::factorize(Δ − k²I)`). `solve(source) → φ`.
- **`AudioSource` + `Microphone`** (in `:helmholtz`, the physics↔frequency bridge) — `AudioSource{mesh_anchor, frequency_hz, amplitude}`: `k_squared() = (2π·f / kSpeedOfSound)²` (`kSpeedOfSound=340`), `source_vector(node_order)` = one-hot RHS (amplitude at the anchor's index, else 0). `Microphone{mesh_anchor}`: `sample(φ, node_order)` reads φ at the anchor's index (0 if absent).

**One-substrate:** Helmholtz takes the SAME `WeightedLaplacian` the renderer's importance channel + the wave/heat steppers use — sound is just a different solve on the shared operator. **Deterministic** (fixed BK pivot order, pure f64). Real-valued (frequency-domain Helmholtz; complex/absorbing boundaries are out, §7).

## 2. Components

### 2.1 `BkLdlt` — Bunch-Kaufman LDLᵀ (`aleph.linalg.sparse:bk_ldlt`)
Port `bk_ldlt.rs` exactly. Dense (`DMatrix`), real f64.
```cpp
enum class BkError { NotSquare, NotSymmetric, Singular /*+ index*/ };
struct DBlock { /* tagged: One(f64) | Two(f64 d11,d12,d22) */ };
struct BkLdlt {
    std::size_t n; DMatrix l; std::vector<DBlock> d; std::vector<std::size_t> perm;
    [[nodiscard]] static std::expected<BkLdlt, BkErrorInfo> factorize(const DMatrix& H);
    [[nodiscard]] std::optional<std::vector<f64>> solve(const std::vector<f64>& b) const;
};
```
- **`factorize`:** validate square + symmetric (tolerance `1e-7`, → `NotSquare`/`NotSymmetric`). Iterate Bunch-Kaufman pivot selection with `kBkAlpha = (1 + std::sqrt(17.0)) / 8.0 ≈ 0.6404`: per the Rust `choose_pivot` (ω₁ = max |A_{ij}| below the diagonal at row r; the α-threshold cascade → 1×1 at j / 1×1 swap-r / 2×2 swap-r-to-j+1). Schur-complement updates symmetrically for BOTH 1×1 and 2×2 (the 2×2 uses the block inverse `l₁=(d₂₂a−d₁₂b)/det, l₂=(−d₁₂a+d₁₁b)/det`). Singularity guard: 1×1 `|pivot| < 1e-14` → `Singular(j)`; 2×2 `|det| < 1e-14` → `Singular(j)`.
- **`solve`:** apply P (forward `y[i]=b[perm[i]]`) → forward-subst `L·z=y` → block-diagonal `D` (1×1 divide; 2×2 block-inverse) → backward-subst `Lᵀ·u=w` → apply Pᵀ (`x[perm[i]]=u[i]`). Returns `nullopt` only on a degenerate D block (shouldn't occur post-factorize).
- **CRITICAL (the load-bearing risk):** the 2×2 pivot branch (index/symmetry bookkeeping) — a subtle error gives wrong-but-finite results caught ONLY by the §5 residual oracles. Port the Rust elimination loop term-for-term; the round-trip tests pin it.

### 2.2 `HelmholtzOperator` (`aleph.sim:helmholtz`)
Mirror `helmholtz.rs`.
```cpp
enum class HelmholtzError { FactorFailed };
struct HelmholtzOperator {  // holds the chosen factor (PSD LDLT or indefinite BkLdlt)
    [[nodiscard]] static std::expected<HelmholtzOperator, HelmholtzError>
        make(const aleph::flow::WeightedLaplacian& lap, f64 k_squared);
    [[nodiscard]] std::optional<std::vector<f64>> solve(const std::vector<f64>& source) const;
    // optional: matrix() reconstructing Δ−k²I for residual checks.
};
```
- `make`: `H = lap.matrix` (clone); if `|k²| ≥ 1e-12`, `for i: H(i,i) -= k²`. Branch: `|k²| < 1e-12` → store an `LDLT::factorize(lap.matrix)` (PSD; the existing dense LDLT accepts Δ's zero/kernel pivot); else → `BkLdlt::factorize(H)` (→ `FactorFailed` on `BkError`).
- `solve`: **PSD path** — port `helmholtz.rs`'s projection EXACTLY (it subtracts the constant/mean component before/after the LDLT solve because Δ has the constant kernel); the §5 Green's-function oracle pins this. **Indefinite path** — `bk.solve(source)` directly.
- Reuses `WeightedLaplacian` unchanged (no flow-layer change). A `Section<f64>` wrapper of the result is optional sugar (the field IS a per-node `vector<f64>` aligned to `lap.node_order`).

### 2.3 `AudioSource` / `Microphone` (`aleph.sim:helmholtz`)
```cpp
inline constexpr aleph::math::f64 kSpeedOfSound = 340.0;
struct AudioSource { aleph::types::NodeId mesh_anchor; f64 frequency_hz; f64 amplitude;
    [[nodiscard]] f64 k_squared() const;                                        // (2πf/340)²
    [[nodiscard]] std::vector<f64> source_vector(const std::vector<NodeId>& order) const; }; // one-hot
struct Microphone { aleph::types::NodeId mesh_anchor;
    [[nodiscard]] f64 sample(const std::vector<f64>& phi, const std::vector<NodeId>& order) const; };
```
(Tiny; the physics↔frequency bridge + the end-to-end smoke. If audio grows — convolution/spatialization — a dedicated `aleph.audio` module is the future home; not now.)

### 2.4 Module/build wiring
- `foundation/src/aleph.linalg.sparse/`: add `aleph.linalg.sparse-bk_ldlt.cppm` (`export module aleph.linalg.sparse:bk_ldlt;` importing `:dense`), register in its `CMakeLists.txt` `FILE_SET CXX_MODULES`, add `export import :bk_ldlt;` to `aleph.linalg.sparse.cppm`.
- `graph/src/aleph.sim/`: add `aleph.sim-helmholtz.cppm` (`export module aleph.sim:helmholtz;` importing `aleph.flow`, `aleph.linalg.sparse`, `aleph.types`, `:section`), register in `CMakeLists.txt`, add `export import :helmholtz;` to `aleph.sim.cppm`; ensure `aleph_sim` links `aleph_flow` (for `WeightedLaplacian`) + `aleph_linalg_sparse` (BkLdlt/LDLT). Inline-in-class per the sim convention.
- Tests: `tests/linalg/test_bk_ldlt.cpp`, `tests/sim/test_helmholtz.cpp` registered in `tests/CMakeLists.txt`.

## 3. Determinism
Bunch-Kaufman pivot selection is a deterministic function of the matrix entries (fixed tie-breaks: the Rust takes the first max); pure f64; no RNG. Same `(Δ, k²)` ⇒ byte-identical factor + solve run-to-run. (Cross-platform bit-identity is libm-independent here — no `pow`/transcendentals in the factorization; `sqrt(17)` in the constant is a single deterministic value. `k_squared` uses one `*`/division, no `pow`.) Consistent with SPEC §7.

## 4. Error handling (`aleph_flags_isa`)
No exceptions/RTTI. `BkLdlt::factorize` → `std::expected<…, BkError(+index)>`; `solve` → `std::optional`. `HelmholtzOperator::make` → `std::expected<…, HelmholtzError::FactorFailed>` (wraps a `BkError`). `Section`'s `StepError` is the sim convention but Helmholtz is a solve, not a step — use its own `HelmholtzError`/`std::optional`. No allocation beyond the dense factor + solution vectors.

## 5. Testing — port the Rust oracles with their EXACT expected values
**BkLdlt (`tests/linalg/test_bk_ldlt.cpp`)** — from `bk_ldlt.rs` tests + `tests/bk_ldlt_properties.rs`:
- `factorize(diag(1,2,3))` → 3× `DBlock::One`, d[i]==i+1 (exact <1e-12).
- 3×3 SPD `[[4,2,0],[2,5,1],[0,1,6]]` → 3 blocks (all 1×1).
- 2×3 zeros → `BkError::NotSquare`.
- `[[ε,1],[1,ε]]`, ε=1e-3 → ONE `DBlock::Two` (forced 2×2).
- indefinite `[[1,2,3],[2,-1,4],[3,4,5]]` → block dims sum to 3.
- **round-trip oracles (the pins):** diag(1,−2,3,−4), b=[1,2,3,4] → x=[1,−1,1,−1] (rel <1e-9); `[[1,2,3],[2,-1,4],[3,4,5]]`, b=[6,5,12] → `‖H·x−b‖/‖b‖ < 1e-9`; 5×5 `[[4,1,-1,2,0],[1,-3,1,0,1],[-1,1,2,-1,0],[2,0,-1,5,1],[0,1,0,1,-2]]`, b=[1,2,3,4,5] → rel residual <1e-9.
**HelmholtzOperator (`tests/sim/test_helmholtz.cpp`)** — from `tests/analytical.rs`:
- **Green's function (k²=0):** a 5-interior-node Dirichlet path (tridiagonal `[2,-1]`), source one-hot at index 2 → matches the closed form `G[i,2]` = `[0.5, 1.0, 1.5, 1.0, 0.5]` (per-node abs err <1e-9). (This pins the PSD path + the projection logic — port `helmholtz.rs`'s PSD solve exactly.)
- **Branching:** a path-graph WeightedLaplacian at k²=0 → PSD factor; at k²=0.5 → indefinite factor (assert which branch via a tag/accessor).
- **Residual (k²>0):** 5-node path, k²=0.1, source=[0,0,1,0,0] → `‖(Δ−k²I)φ − source‖₂ < 1e-9`.
**AudioSource/Microphone** (same test file): `k_squared(f=0)≈0`; `k_squared(200) > k_squared(100)`; `source_vector(anchor=5, order=[2,5,9], amp=2.5)==[0,2.5,0]`; missing anchor → all-zero; `sample(anchor=7, phi=[.1,.4,.9], order=[3,7,11])==0.4`; missing → 0.
**End-to-end smoke:** a 4-node path graph → `WeightedLaplacian` → `AudioSource(ids[0], f, amp)` k²=0 → `HelmholtzOperator::make` → `solve(source_vector)` → `Microphone(ids[3]).sample(...)` is FINITE (`std::isfinite`).
**Determinism:** factorize+solve twice on the same matrix → byte-identical.

## 6. Cost / when it runs
Dense Bunch-Kaufman: `O(n³)` factorize, `O(n²)` solve — on the **bounded-support Δ** the sim uses (n ≤ a few hundred nodes), a one-shot steady-state solve (NOT per-frame; recomputed when k or the graph changes). Acceptable. The bounded Δ is already dense (`DMatrix`), so dense BK exploits nothing it shouldn't. **§7 hook:** a sparse blocked indefinite factorization (Ashcraft-Grimes-Lewis) for large n is a separate, larger undertaking.

## 7. Scope boundary (YAGNI)
**In:** dense real-valued `BkLdlt` (the general indefinite solver), `HelmholtzOperator` `(Δ−k²I)` on the shared Δ (PSD@k≈0 / indefinite@k>0), minimal `AudioSource`/`Microphone`, the ported oracles. **Out (hooks kept):**
- *Sparse/blocked indefinite factorization* — dense BK is fine at bounded-Δ sizes; sparse BK is a future perf slice.
- *Complex Helmholtz / absorbing (impedance) boundaries / radiation conditions* — the Rust + this port are real-valued frequency-domain; complex k is a larger numerics change.
- *Full audio rendering* — convolution, multi-frequency synthesis, HRTF/spatialization, time-domain coupling to the wave stepper. `AudioSource`/`Microphone` are just the operator's RHS/sampling bridge.
- *A dedicated `aleph.audio` module* — source/receiver live in `aleph.sim:helmholtz` for now; promote to `aleph.audio` if audio grows.
- *Flow-layer changes* — Helmholtz consumes `WeightedLaplacian` read-only; no `aleph.flow` change.

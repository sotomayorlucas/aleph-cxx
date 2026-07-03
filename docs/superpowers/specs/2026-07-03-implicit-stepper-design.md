# Design Spec — Implicit (unconditionally stable) stepping on the shared Δ

**Goal:** steppers with NO CFL bound: backward-Euler heat and backward-Euler
wave on the SAME bounded-κ_R Laplacian Δ, via one new solver-holding carrier.
The explicit steppers (`WaveStepper`, `DiffuseStepper`) are matvec-only and
CFL-bounded (heat: dt·α·λ_max < 2; wave: c²dt²λ_max < 4) — a stiff Δ or a big
dt diverges. Implicit steps solve `(I + β·Δ) u⁺ = rhs` instead; `I + βΔ` is SPD
for β ≥ 0 (Δ PSD), so the LDLᵀ always exists and the solve never touches the
kernel guard. Date 2026-07-03 · Status: approved. C++-native slice (no Rust
oracle — like `vector_field`); follow-up (2) of [[one-substrate-physics-direction]].

## 1. Components (new partition `aleph.sim:implicit`)

### 1.1 `ShiftedLaplacian` — the factored `(I + β·Δ)` carrier
```cpp
enum class ImplicitError { InvalidShift, FactorFailed };  // value-less tags (HelmholtzError precedent)
struct ShiftedLaplacian {
    std::size_t n{0}; f64 beta{0.0}; aleph::linalg::sparse::LDLT ldlt;
    [[nodiscard]] static std::expected<ShiftedLaplacian, ImplicitError>
        make(const DMatrix& delta, f64 beta);           // H = I + beta*delta; LDLT::factorize
    [[nodiscard]] std::optional<std::vector<f64>> solve(std::span<const f64> b) const;
};
```
- `make`: `beta` must be finite and ≥ 0 → else `InvalidShift`. `H.at(i,j) =
  beta·delta.at(i,j)`, `H.at(i,i) += 1`, `LDLT::factorize(H)`; any
  `LdltErrorInfo` → `FactorFailed` (index discarded, Helmholtz convention).
- One factorization per (Δ, β); reused across MANY steps (steps ≫ edits — the
  amortization argument). On an edit the caller re-`make`s, same as the
  operator itself is rebuilt.

### 1.2 `ImplicitDiffuseStepper` — backward-Euler heat
`(I + dt·α·Δ) u⁺ = u`. Holds `DiffuseParams`, `ShiftedLaplacian op`, `f64 dt`.
`make(delta, params, dt)` (dt > 0, else `InvalidShift`);
`step(Section<f64>& u)` → `std::expected<void, StepError>` (StepError reused
from `:section`). Properties: unconditionally stable (eigenvalues of
`(I+βΔ)⁻¹` in (0,1]); mass Σu conserved exactly in exact arithmetic
(1ᵀΔ = 0); variance monotonically decreasing.

### 1.3 `ImplicitWaveStepper` — backward-Euler wave
Damp-then-force convention consistent with the explicit `WaveStepper`:
`v⁺ = damping·v − dt·c²·Δu⁺`, `u⁺ = u + dt·v⁺` ⇒
**`(I + dt²c²Δ) u⁺ = u + dt·damping·v`**, then `v⁺ = (u⁺ − u)/dt`.
Holds `WaveParams`, `ShiftedLaplacian op` (β = dt²c²), `f64 dt`. A-stable
(adds numerical damping — acceptable: the model already damps by convention).

### 1.4 Step contract (STRONGER than the explicit steppers)
The solve produces a complete candidate vector before any write-back, so on
ANY error (`EmptyField`/`DimMismatch`/`NonFinite`) the sections are left
**UNCHANGED** (the explicit steppers leave them partially updated). NonFinite
covers a defensive nullopt from `solve` (unreachable for SPD) and non-finite
candidate entries (e.g. a seeded inf in the rhs).

## 2. Determinism
Pure f64, fixed elimination/solve order (LDLT), no RNG: same (Δ, params, dt,
field) ⇒ byte-identical trajectories run-to-run.

## 3. Build wiring
`aleph.sim-implicit.cppm` (`export module aleph.sim:implicit;` importing
`aleph.math`, `aleph.linalg.sparse`, `:section`, `:wave`, `:diffuse` — reuses
`WaveParams`/`DiffuseParams`); register in aleph.sim CMakeLists FILE_SET; add
`export import :implicit;` to the umbrella. No new link edges
(aleph_linalg_sparse already PUBLIC). Definitions inline-in-struct (house
style). No defaulted friend `operator==` on exported structs (GCC-16 ICE).

## 4. Tests (tests/sim/test_implicit.cpp; path-graph Δ fixture as test_diffuse)
1. Diffuse: uniform field is a fixed point (≈1e-12).
2. Diffuse: dt 100× beyond the explicit CFL — implicit stays finite, mass
   conserved (1e-9), variance strictly decreasing; the EXPLICIT stepper at the
   same dt diverges (NonFinite or norm blow-up) — the payoff test.
3. Diffuse: small-dt consistency vs explicit (|imp−exp| = O(dt²) per entry).
4. Wave: kick then many huge-dt steps — finite and bounded; uniform u with
   v=0 is a fixed point; v stays 0.
5. Determinism: byte-identical repeat runs (== on every entry).
6. Errors: dt ≤ 0 → InvalidShift; non-symmetric Δ → FactorFailed; wrong-size
   section → DimMismatch; sections unchanged after a failed step.

## 5. Non-goals
Controller/editor integration (the 60 fps demo is happily explicit); sparse
factorization; Crank-Nicolson (energy-conserving wave) — noted as follow-ups.

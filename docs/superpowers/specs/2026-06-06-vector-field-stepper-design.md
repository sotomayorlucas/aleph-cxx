# Design Spec — Vector-Field (Section<Vec3>) Diffusion Stepper (physics slice)

**Goal:** evolve a **per-node Vec3 field** on the SAME shared Δ that drives light/wave/heat/sound — extending the scalar physics (`DiffuseStepper`/`WaveStepper`) to a *vector* field. The `Section<Vec3>` storage is already proven (the Section-abstraction slice); the missing piece is a **stepper**. A `VectorDiffuseStepper` applies the scalar Laplacian Δ **component-wise** (x/y/z) — forward-Euler heat per component, matvec-only, sharing the same operator. Date 2026-06-06 · Status: DRAFT. C++-native (no Rust port — `aleph.sim` steppers are the C++ one-substrate contribution). Next slice of [[one-substrate-physics-direction]].

Context (verified): `Section<Vec3>` (aleph.sim:section) is AoS — `std::vector<aleph::math::Vec3> data` (components are **f32**: `.x/.y/.z`; `operator+(Vec3)`, `operator*(f32)`) + `std::vector<NodeId> order`. `section.cppm:27-28` explicitly flags that a Vec3 stepper must handle Vec3's **f32 scaling** (scalar steppers use f64). `DiffuseStepper` (aleph.sim:diffuse) is the model: `step(Section<f64>& u, const DMatrix& delta, f64 dt) → std::expected<void, StepError>`, forward-Euler `u[i] -= dt·α·(Δu)[i]`, matvec-only, guards `n==0→EmptyField`, `size≠delta.rows()→DimMismatch`, `!isfinite→NonFinite` (StepError lives in :section, shared by peer steppers). Δ = `WeightedLaplacian.matrix` (`DMatrix`, f64; a valid graph Laplacian — row sums 0, constant in kernel). `aleph_flags_isa` → no exceptions; inline-in-class (the Section template + steppers are all inline).

## 1. Approach
A `Section<Vec3>` field is, on a scalar operator Δ, three independent scalar fields (x, y, z). `VectorDiffuseStepper::step` applies the **same scalar Δ** to each component: extract component c into an f64 scratch, `delta.matvec`, update `u[i].c -= dt·α·(Δu_c)[i]`, for c ∈ {x, y, z} in fixed order. Forward-Euler heat (`∂u/∂t = −α·Δu`), matvec-only — the exact scalar `DiffuseStepper` physics, lifted to Vec3. The **payoff** (one-substrate): the same factorized Δ + the `Section` abstraction now drive a vector field, not just scalars. **f64 matvec → f32 component store** (Vec3 components are f32 — the §section comment's flagged detail): the matvec runs in f64, the per-component update is computed in f64, then cast to f32 on write-back. **Deterministic** (fixed x→y→z order; pure f64 matvec + deterministic f32 cast). Component-wise = each component evolves independently (NO cross-component coupling — a connection/Hodge Laplacian that mixes components via curvature is a §7 hook, not this slice).

## 2. Components (`aleph.sim:vector_field`)
New partition `aleph.sim-vector_field.cppm` (`export module aleph.sim:vector_field;` importing `aleph.math`, `aleph.linalg.sparse`, `:section`); umbrella `export import :vector_field;`.
```cpp
struct VectorDiffuseParams { aleph::math::f64 alpha = 1.0; };
struct VectorDiffuseStepper {
    VectorDiffuseParams params{};
    // Component-wise forward-Euler heat on a Vec3 field over the shared scalar Δ.
    // u[i].c -= dt·α·(Δ u_c)[i] for c ∈ {x,y,z}. matvec-only. Stable for dt·α·λ_max < 2.
    [[nodiscard]] std::expected<void, StepError>
    step(Section<aleph::math::Vec3>& u, const DMatrix& delta, aleph::math::f64 dt) const noexcept {
        const std::size_t n = u.size();
        if (n == 0) return std::unexpected(StepError::EmptyField);
        if (delta.rows() != n || delta.cols() != n) return std::unexpected(StepError::DimMismatch);
        // For each component (fixed order x,y,z): f64 scratch -> matvec -> f32 write-back.
        // Component accessor: a small lambda or 3 explicit blocks reading/writing .x/.y/.z.
        for (each component c in {x, y, z}) {
            std::vector<f64> comp(n);
            for (i) comp[i] = static_cast<f64>(u.data[i].<c>);
            const std::vector<f64> lap = delta.matvec(std::span<const f64>(comp.data(), n));
            for (i) u.data[i].<c> = static_cast<f32>(static_cast<f64>(u.data[i].<c>) - dt * params.alpha * lap[i]);
        }
        for (i) if (!std::isfinite(u.data[i].x) || !std::isfinite(u.data[i].y) || !std::isfinite(u.data[i].z))
            return std::unexpected(StepError::NonFinite);
        return {};
    }
};
```
**Implement as 3 EXPLICIT blocks (x, then y, then z)** — the `for each component c` above is pseudocode; you cannot iterate struct members, and a `f32 Vec3::*` member-pointer adds indirection across the module boundary on the `alignas(16)` Vec3 against the §section inline-simple convention. Three unrolled blocks are the `-Wshadow`/`-Wsign-conversion`-clean choice. **Scope:** the partition declares a using-block `using aleph::math::f64; using aleph::math::f32; using aleph::math::Vec3; using aleph::linalg::sparse::DMatrix;` (mirroring `diffuse.cppm`; `f32`/`Vec3` are NOT otherwise in scope — only `f64` is in the sibling steppers) — OR fully-qualify `static_cast<aleph::math::f32>(...)`. `delta.matvec` returns `std::vector<f64>` taking a `std::span<const f64>` (dense.cppm:68), as `DiffuseStepper` uses it. Reuses `StepError` from `:section` (no new error type).

## 3. Determinism
Fixed component order (x→y→z), fixed node order; `delta.matvec` is pure f64; the per-component update is f64 arithmetic then a deterministic `static_cast<f32>`. Same `(Section<Vec3>, Δ, dt)` ⇒ byte-identical result run-to-run. (The f64→f32 store is the one precision step — deterministic; the field is f32-valued by design.) Consistent with SPEC §7. No RNG, no allocation beyond the per-component f64 scratch + matvec result.

## 4. Error handling (`aleph_flags_isa`)
No exceptions. Reuse `StepError` (`:section`): `EmptyField` (n==0), `DimMismatch` (section size ≠ Δ dims), `NonFinite` (post-step blow-up). `EmptyField`/`DimMismatch` are checked up front (as in `DiffuseStepper`); the `NonFinite` check is a SINGLE post-update pass over all 3 components (it can't be interleaved across the 3 component passes the way `DiffuseStepper` interleaves its single field — a deliberate restructure, functionally equivalent). `step` returns `std::expected<void, StepError>` exactly like `DiffuseStepper`. (CflViolation is a caller pre-check code, not emitted here.)

## 5. Testing (`tests/sim/test_vector_field.cpp`)
- **Component independence == scalar physics (ONE step):** a `Section<Vec3>` with an x-spike (one node x=1, y=z=0) and a `Section<f64>` with the same spike, on the SAME Δ/dt/α. Run ONE `VectorDiffuseStepper.step` (Vec3) and ONE `DiffuseStepper.step` (scalar). Assert: the Vec3 field's **x-components ≈ the scalar field's data** (tol ~1e-5; measured one-step f32-vs-f64 divergence is ~1e-8 → ~3 orders of margin), and **y,z components stay exactly 0.0**. **Do NOT mirror the 200-step sibling loop** — the f32 write-back truncates ~6e-8/step, so over many steps the Vec3 path drifts from the f64 scalar path past the 1e-5 tol (the y==z==0 half is exact + step-count-independent).
- **Per-component sum conservation (ONE step):** Δ is a graph Laplacian (row sums 0 → Σ(Δu_c)=0 exactly in f64), so heat preserves Σ_i u[i].c per component; the only error is f32 store round-off (~n·3e-8). After ONE step assert each component's sum is unchanged (tol ~1e-5·n — a ~300× loose single-step bound; a multi-step loop would accumulate ~steps·n·3e-8).
- **Smoothing:** a single-node Vec3 spike on a path graph → after a step, neighbours gain (each component spreads), the spike node decreases. Assert a neighbour's component grew from 0 and the spike shrank.
- **Guards:** `EmptyField` (empty section); `DimMismatch` (section size ≠ Δ rows); `NonFinite` — **seed a non-finite input** (`u.data[k].x = std::numeric_limits<float>::infinity()` before the step → the matvec/update propagates inf → guard fires). PRIMARY because it's dt-independent + robust. (A merely-past-CFL dt yields a large-but-FINITE value in one step — `std::isfinite` passes — so it does NOT trigger the guard; only an overflow-magnitude dt ~1e40 would. This is why the sibling `test_diffuse.cpp` has no NonFinite test.)
- **Determinism:** two steps on identical inputs → `data` compares equal element-wise via `Vec3::operator==` (compares x,y,z; w stays 0 throughout — the stepper never writes w).
- **Test Δ:** build it like the siblings — `build_laplacian(make_path(...), default_weight)` + the Gershgorin-based CFL `dt` helper (mirror `test_diffuse.cpp`), not `build_laplacian_bounded` (both share `assemble()` = row-sums-0, but mirroring the harness keeps it consistent).
- (The existing `Section<Vec3>` storage test stands; this adds the stepper.)

## 6. Cost / when it runs
**3 matvecs** per step (one per component) — `O(3·n²)` dense on the bounded Δ, or `O(3·nnz)` if the matvec is sparse-aware; matvec-only (no factorization). The same per-frame budget shape as the scalar steppers (explicit, CFL-bounded). Per-component f64 scratch (n) + matvec result (n), reused across components.

## 7. Scope boundary (YAGNI)
**In:** a `VectorDiffuseStepper` evolving `Section<Vec3>` by the scalar Δ applied **component-wise** (independent x/y/z heat), f64 matvec → f32 store, reusing `StepError`. **Out (hooks kept):**
- *Cross-component coupling (connection / Hodge / vector Laplacian)* — a true vector Laplacian mixes components via the graph's curvature/connection; that is a separate, larger numerics change (Δ here is scalar). Component-wise is the honest first vector stepper.
- *Vector wave* — `∂²u/∂t² = −c²Δu` on Vec3 needs two `Section<Vec3>` (displacement + velocity), mirroring `WaveStepper`; a clean follow-up once vector diffusion lands.
- *Advection / non-linear vector PDEs* — out.
- *Implicit vector stepping* — the scalar implicit solver (a separate hook) would generalize component-wise too; not now.
- *Unifying `lowering::importance` as a `Section<f64>`* — a separate renderer-coupling slice.
`VectorDiffuseParams::alpha` is the tunable.

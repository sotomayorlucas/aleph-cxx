# Design Spec — `Section<T>` field abstraction (physics slice 2)

**Goal:** generalize the concrete `ScalarField` into a `Section<T>` — a value-per-node section over the cell complex, generic over the per-node payload `T` (scalar `f64`, vector `Vec3`, …) — so the wave, future physics fields, and rendering signals are all the SAME type over the SAME Δ. Refactor the wave onto it **byte-identically**, and prove generality with a second field (diffusion) + a `Section<Vec3>` test. Date 2026-06-05 · Status: DRAFT.

Context: physics slice on the "one substrate" direction (after the wave field). Today `aleph.sim::ScalarField { vector<NodeId> order; vector<f64> phi, phi_dot; }` hard-bundles displacement+velocity and is scalar-only. This slice extracts the reusable "field over the complex" concept.

## 1. Approach

Introduce `aleph.sim::Section<T>` (one quantity per node, generic `T`). The wave's state becomes **two** `Section<f64>` (`u`=displacement, `v`=velocity) instead of one bundled `ScalarField`. `WaveStepper` operates on `(u, v)`; its arithmetic is unchanged (same `f64`, same loop order) so `--wave` stays byte-identical. A new `DiffuseStepper` operates on a single `Section<f64>` over the same Δ, demonstrating field-agnostic reuse. `Section<Vec3>` is exercised by a unit test (a vector field's storage/reproject) to prove `T`-genericity. The controller swaps its `ScalarField field_` for `Section<f64> u_, v_`.

## 2. Components

### 2.1 `aleph.sim:section` — the generic type
```cpp
export module aleph.sim:section;
import aleph.math;   // f64, Vec3
import aleph.types;  // NodeId
import std;
namespace aleph::sim {
using aleph::types::NodeId;
// A section of the cell complex: one value of type T per node, dense + ordered to
// match a WeightedLaplacian::node_order. T must be zero-initializable (T{} == 0)
// and support `T operator+(T)` — that is ALL Section's own four methods use.
// (Scalar scaling lives only in the steppers, instantiated for Section<f64> this
// slice; aleph::math::Vec3 scales by f32, not f64 — a future Vec3 stepper handles
// that.) BOTH f64 and Vec3 satisfy the Section contract.
// ALL members are defined INLINE-IN-CLASS (this codebase exports class templates —
// SmallVector, dense_index — with every member inline; out-of-line class-template
// member definitions across C++26 modules are an under-tested toolchain path).
template<class T>
struct Section {
    std::vector<NodeId> order;
    std::vector<T>      data;
    [[nodiscard]] std::size_t size() const noexcept { return order.size(); }
    [[nodiscard]] static Section zeros(std::vector<NodeId> order);   // data(n, T{})
    // Accumulate `v` at node `n`; false (no-op) if n ∉ order. (Generalizes kick.)
    [[nodiscard]] bool add(NodeId n, const T& v) noexcept;            // data[i] = data[i] + v
    // Value at node n, or nullptr if absent.
    [[nodiscard]] const T* at(NodeId n) const noexcept;
    // Re-project onto a new node order: survivors keep their value; new nodes T{};
    // deleted drop. Deterministic O(n·m). (Same semantics as ScalarField::reproject.)
    void reproject(const std::vector<NodeId>& new_order);
};
}
```
`ScalarField` is removed; `Section<f64>` replaces it.

### 2.2 `aleph.sim:wave` — operate on two `Section<f64>`
```cpp
[[nodiscard]] std::expected<void, StepError>
step(Section<f64>& u, Section<f64>& v, const DMatrix& delta, f64 dt) const noexcept;
```
Body (byte-identical arithmetic to today): `lap = delta.matvec(u.data)`; per i `v.data[i] = damping*v.data[i] − dt*c²*lap[i]; u.data[i] += dt*v.data[i];` + NonFinite guard. `cfl_ok` unchanged. **The `u.data`/`v.data` are the same `vector<f64>` the old `phi`/`phi_dot` were, stepped in the same order → the wave trajectory and `--wave` PPMs are byte-identical.**

### 2.3 `aleph.sim:diffuse` — second field, same Δ (proves reuse)
```cpp
struct DiffuseParams { f64 alpha = 1.0; };
struct DiffuseStepper {
    DiffuseParams params{};
    // Explicit heat step ∂u/∂t = −α·Δu  ⇒  u[i] -= dt·α·(Δu)[i]. matvec-only.
    [[nodiscard]] std::expected<void, StepError>
    step(Section<f64>& u, const DMatrix& delta, f64 dt) const noexcept;
};
```
Same `StepError`, same shared Δ matvec, a *different* physics — the abstraction's payoff.

### 2.4 Controller (`aleph.edit`)
Replace `aleph::sim::ScalarField field_` with `aleph::sim::Section<f64> u_, v_;`. `enable_sim(true)` zeros both over `operator_.node_order`. `kick(NodeId, amp)` → `v_.add(n, amp)` (a velocity impulse — same effect as before). `step(dt)` → `stepper_.step(u_, v_, operator_.matrix, dt)` then re-bake. `rebuild_operator_and_reproject` re-projects BOTH `u_` and `v_`. A `[[nodiscard]] const aleph::sim::Section<f64>& displacement() const` accessor (replaces `field()`) feeds the φ→vcol projection (uses `u_.order`/`u_.data`). Public `enable_sim`/`kick`/`step` signatures unchanged → `apps/aleph_edit/main.cpp` (`--wave`, `--wave-live`) needs no change (it only calls kick/step/raster_scene).

### 2.5 Build wiring (REQUIRED — build breaks without these)
- `graph/src/aleph.sim/CMakeLists.txt` FILE_SET: drop `aleph.sim-field.cppm`, add `aleph.sim-section.cppm` and `aleph.sim-diffuse.cppm`.
- `graph/src/aleph.sim/aleph.sim.cppm`: `export import :section; export import :wave; export import :diffuse;` (was `:field; :wave;`).
- `tests/CMakeLists.txt`: `sim/test_field.cpp` → `sim/test_section.cpp`, and add `sim/test_diffuse.cpp` (an unlisted test is silently never compiled).
- `tests/isolation/iso_sim.cpp` is a bare `import aleph.sim;` smoke test — survives unchanged.

## 3. Determinism (the load-bearing requirement)
The refactor must not change any arithmetic: `Section<f64>::data` is the identical `vector<f64>`, matvec/step loops identical, occluder/reproject order identical. Verify: (a) `test_wave`'s trajectory-determinism (`==`) still passes; (b) `--wave /tmp/a` vs `/tmp/b` byte-identical (`cmp`); (c) `--wave` frames byte-identical vs the PRE-refactor build (capture on `main` first, compare) — the strongest check that behavior is unchanged.

## 4. Error handling (`aleph_flags_isa`)
No exceptions; `std::expected`/`bool`/`std::optional`. `Section::add` returns false on a missing node. Steppers return `EmptyField`/`DimMismatch`/`NonFinite` as today. `Section<Vec3>` NonFinite (if a vector stepper is added later) checks each component; not needed for this slice's scalar steppers.

## 5. Testing
- **`tests/sim/test_section.cpp` (renamed from test_field):** `Section<f64>` zeros/add(in & out-of-range)/at/reproject (survivor/new/deleted) — same oracles as the old `ScalarField` test, on `data`. PLUS `Section<Vec3>` zeros/add/reproject (proves `T`-genericity: a vector field stores/reprojects correctly; e.g. add `Vec3{1,0,0}` twice → `{2,0,0}`).
- **`tests/sim/test_wave.cpp`:** update to two `Section<f64>` (u,v); keep ALL oracles (operator symmetry, 2-node energy bound, damped monotonicity, determinism `==`, error paths). The determinism `==` test is the refactor's guard.
- **`tests/sim/test_diffuse.cpp` (new):** on a path graph, an initial spike `Section<f64>` diffused by `DiffuseStepper` has **non-increasing variance** (heat smooths) and conserves the sum (Δ's all-ones kernel: `1ᵀΔ = 0` ⇒ `Σu` constant); deterministic.
- **`tests/edit/test_sim_controller.cpp` (concrete renames — compile-break otherwise):** `ctl.field().size()` → `ctl.displacement().size()`; `ctl.field().phi` → `ctl.displacement().data`; the `phi_of` helper's `f.order`/`f.phi` → `displacement().order`/`.data`. Keep the evolve + edit-reproject oracles. (`test_wave.cpp` also has direct buffer writes `f.phi[0]=1.0; f.phi[1]=-1.0` → become `u.data[i]` writes; `Section::data` is a public member so direct indexing is fine.)
- **Determinism artifact:** `--wave` PPMs byte-identical vs pre-refactor `main`.

## 6. Scope boundary (YAGNI)
**In:** `Section<T>` (scalar + vector), wave refactored onto it (byte-identical), a `DiffuseStepper` second field, `Section<Vec3>` storage test. **Out (hooks kept):** a general vector-field *stepper* (the Vec3 Section storage is proven; a Vec3 physics stepper is a follow-up); unifying the *renderer's* importance/curvature signal as a `Section<f64>` (a clean next step — `lowering::importance` is already a `vector<double>` aligned to entities); a `Section`-of-cochains over higher simplices (this slice is 0-cells/nodes only); implicit/Helmholtz steppers (separate slices). `Section` is a struct template, not a runtime-polymorphic field (compile-time `T` is enough).

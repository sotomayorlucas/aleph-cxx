# Section<T> Abstraction — Implementation Plan (physics slice 2)

> TDD, frequent commits. **Spec:** `docs/superpowers/specs/2026-06-05-section-abstraction-design.md`. The load-bearing requirement: the wave refactor is **byte-identical** (`--wave` PPMs unchanged vs `main`).

**Conventions:** `cmake --build build-release && ctest --test-dir build-release`; one case `./build-release/tests/aleph_tests --test-case="<name>"`; strict `cmake --build build-release-strict 2>&1 | grep -c "warning:"` → 0. No exceptions/RTTI.

**FIRST — capture the byte-identical baseline (before any change):**
```bash
git stash list >/dev/null; mkdir -p /tmp/base; ./build-release/apps/aleph_edit/aleph_edit --wave /tmp/base >/dev/null
```
(After the refactor, `--wave /tmp/after` then `diff -rq /tmp/base /tmp/after` must be EMPTY.)

---

## Task 1: `Section<T>` (replaces ScalarField) + test

**Files:** rename `graph/src/aleph.sim/aleph.sim-field.cppm` → `aleph.sim-section.cppm`; rename `tests/sim/test_field.cpp` → `tests/sim/test_section.cpp`; edit `graph/src/aleph.sim/CMakeLists.txt`, `graph/src/aleph.sim/aleph.sim.cppm`, `tests/CMakeLists.txt`.

- [ ] **Step 1 — `aleph.sim-section.cppm`** (all members inline-in-class):
```cpp
module;
#include <cstddef>
#include <vector>
export module aleph.sim:section;
import aleph.math;     // f64, Vec3
import aleph.types;    // NodeId
export namespace aleph::sim {
using aleph::math::f64;
using aleph::types::NodeId;

// A section of the cell complex: one value of type T per node, dense + ordered to
// match a WeightedLaplacian::node_order. T must be zero-initializable (T{}==0) and
// support `T operator+(T)`. Scalar scaling lives in the steppers (f64 this slice).
template<class T>
struct Section {
    std::vector<NodeId> order;
    std::vector<T>      data;

    [[nodiscard]] std::size_t size() const noexcept { return order.size(); }

    [[nodiscard]] static Section zeros(std::vector<NodeId> node_order) {
        const std::size_t n = node_order.size();
        return Section{std::move(node_order), std::vector<T>(n, T{})};
    }
    // Accumulate `v` at node n; false (no-op) if n ∉ order.
    [[nodiscard]] bool add(NodeId n, const T& v) noexcept {
        for (std::size_t i = 0; i < order.size(); ++i)
            if (order[i] == n) { data[i] = data[i] + v; return true; }
        return false;
    }
    [[nodiscard]] const T* at(NodeId n) const noexcept {
        for (std::size_t i = 0; i < order.size(); ++i)
            if (order[i] == n) return &data[i];
        return nullptr;
    }
    // Survivors keep value; new nodes T{}; deleted drop. Deterministic O(n·m).
    // Precondition: new_order has no duplicate NodeIds (node_order guarantees it).
    void reproject(const std::vector<NodeId>& new_order) {
        std::vector<T> nd(new_order.size(), T{});
        for (std::size_t i = 0; i < new_order.size(); ++i)
            for (std::size_t j = 0; j < order.size(); ++j)
                if (order[j] == new_order[i]) { nd[i] = data[j]; break; }
        order = new_order;
        data = std::move(nd);
    }
};
}  // namespace aleph::sim
```
- [ ] **Step 2 — build wiring:** in `CMakeLists.txt` FILE_SET replace `aleph.sim-field.cppm` with `aleph.sim-section.cppm` (and later add `aleph.sim-diffuse.cppm` in Task 3); in `aleph.sim.cppm` replace `export import :field;` with `export import :section;` (keep `:wave;`; add `:diffuse;` in Task 3); in `tests/CMakeLists.txt` replace `sim/test_field.cpp` with `sim/test_section.cpp` (add `sim/test_diffuse.cpp` in Task 3).
- [ ] **Step 3 — `tests/sim/test_section.cpp`** (port the field tests to `Section<f64>` + add a `Section<Vec3>` case):
```cpp
#include "doctest.h"
#include <vector>
import aleph.sim;
import aleph.types;
import aleph.math;
using aleph::sim::Section;
using aleph::types::NodeId;
using aleph::math::Vec3;

TEST_CASE("Section<f64>: zeros/add(in & out of range)/at") {
    Section<double> f = Section<double>::zeros(std::vector<NodeId>{NodeId{10},NodeId{20},NodeId{30}});
    REQUIRE(f.size()==3); CHECK(f.data[1]==doctest::Approx(0.0));
    CHECK(f.add(NodeId{20}, 2.5)); CHECK(f.data[1]==doctest::Approx(2.5));
    CHECK(f.add(NodeId{20}, 0.5)); CHECK(f.data[1]==doctest::Approx(3.0));   // accumulates
    CHECK(!f.add(NodeId{999}, 1.0));
    REQUIRE(f.at(NodeId{20})); CHECK(*f.at(NodeId{20})==doctest::Approx(3.0));
    CHECK(f.at(NodeId{999})==nullptr);
}
TEST_CASE("Section<f64>: reproject survivor/new/deleted") {
    Section<double> f = Section<double>::zeros(std::vector<NodeId>{NodeId{1},NodeId{2}});
    f.data[0]=5.0; f.data[1]=7.0;
    f.reproject(std::vector<NodeId>{NodeId{1},NodeId{3}});
    REQUIRE(f.size()==2); CHECK(f.order[0]==NodeId{1}); CHECK(f.order[1]==NodeId{3});
    CHECK(f.data[0]==doctest::Approx(5.0)); CHECK(f.data[1]==doctest::Approx(0.0));
}
TEST_CASE("Section<Vec3>: T-genericity (storage/add/reproject)") {
    Section<Vec3> g = Section<Vec3>::zeros(std::vector<NodeId>{NodeId{1},NodeId{2}});
    CHECK(g.add(NodeId{1}, Vec3{1,0,0})); CHECK(g.add(NodeId{1}, Vec3{1,0,0}));
    CHECK(g.data[0].x==doctest::Approx(2.0));
    g.reproject(std::vector<NodeId>{NodeId{1}});
    REQUIRE(g.size()==1); CHECK(g.data[0].x==doctest::Approx(2.0));
}
```
- [ ] **Step 4 — Task 1 won't fully build yet** (wave/controller still reference ScalarField). That's expected; do Tasks 2 & 4 before the suite goes green. (To check Task 1 in isolation: `cmake --build build-release --target aleph_sim` should compile the module.) Proceed to Task 2.

---

## Task 2: Refactor `WaveStepper` onto two `Section<f64>`

**Files:** `graph/src/aleph.sim/aleph.sim-wave.cppm`, `tests/sim/test_wave.cpp`.

- [ ] **Step 1 — change the step signature + body** (arithmetic identical):
```cpp
import :section;   // was :field
// ...
[[nodiscard]] std::expected<void, StepError>
step(Section<f64>& u, Section<f64>& v, const DMatrix& delta, f64 dt) const noexcept {
    const std::size_t n = u.size();
    if (n == 0) return std::unexpected(StepError::EmptyField);
    if (delta.rows() != n || delta.cols() != n || v.size() != n)
        return std::unexpected(StepError::DimMismatch);
    const std::vector<f64> lap = delta.matvec(std::span<const f64>(u.data.data(), n));
    const f64 c2 = params.c * params.c;
    for (std::size_t i = 0; i < n; ++i) {
        v.data[i] = params.damping * v.data[i] - dt * c2 * lap[i];
        u.data[i] += dt * v.data[i];
        if (!std::isfinite(u.data[i]) || !std::isfinite(v.data[i]))
            return std::unexpected(StepError::NonFinite);
    }
    return {};
}
```
(`cfl_ok` unchanged.) Keep the existing damp-then-force comment (matches the Rust reference).
- [ ] **Step 2 — update `tests/sim/test_wave.cpp`:** replace each `ScalarField f = ScalarField::zeros(L.node_order)` with two `Section<f64> u = Section<f64>::zeros(L.node_order), vv = Section<f64>::zeros(L.node_order);`; `f.phi[i]` → `u.data[i]`, `f.phi_dot`/kick → `vv.data` / `(void)vv.add(node, amp)`; `s.step(f, L.matrix, dt)` → `s.step(u, vv, L.matrix, dt)`; the `energy(f, ...)` helper takes `(u,vv)` (KE from `vv.data`, PE from `u.data`). Keep ALL oracle assertions and the determinism `==` check (compare `u.data` across two runs).
- [ ] **Step 3 — build + run** `--test-case="WaveStepper*"` after Task 4 wires the controller (the lib won't link until the controller compiles). If you want to gate Task 2 alone, temporarily it's fine for the full `ctest` to be deferred to Task 4. Commit Tasks 1+2 together: `feat(sim): Section<T> replaces ScalarField; WaveStepper on two Section<f64> (byte-identical)`.

---

## Task 3: `DiffuseStepper` (second field, same Δ) + test

**Files:** new `graph/src/aleph.sim/aleph.sim-diffuse.cppm`; new `tests/sim/test_diffuse.cpp`; CMake + `aleph.sim.cppm` (add `:diffuse`).

- [ ] **Step 1 — `aleph.sim-diffuse.cppm`:**
```cpp
module;
#include <cmath>
#include <cstddef>
#include <expected>
#include <span>
#include <vector>
export module aleph.sim:diffuse;
import aleph.math;
import aleph.linalg.sparse;   // DMatrix
import :section;
import :wave;                 // StepError
export namespace aleph::sim {
using aleph::math::f64;
using aleph::linalg::sparse::DMatrix;
struct DiffuseParams { f64 alpha = 1.0; };
struct DiffuseStepper {
    DiffuseParams params{};
    // Explicit heat step ∂u/∂t = −α·Δu  ⇒  u[i] -= dt·α·(Δu)[i]. matvec-only.
    [[nodiscard]] std::expected<void, StepError>
    step(Section<f64>& u, const DMatrix& delta, f64 dt) const noexcept {
        const std::size_t n = u.size();
        if (n == 0) return std::unexpected(StepError::EmptyField);
        if (delta.rows() != n || delta.cols() != n) return std::unexpected(StepError::DimMismatch);
        const std::vector<f64> lap = delta.matvec(std::span<const f64>(u.data.data(), n));
        for (std::size_t i = 0; i < n; ++i) {
            u.data[i] -= dt * params.alpha * lap[i];
            if (!std::isfinite(u.data[i])) return std::unexpected(StepError::NonFinite);
        }
        return {};
    }
};
}  // namespace aleph::sim
```
(`StepError` is defined in `:wave`; importing `:wave` re-uses it. If that creates a cycle, move `StepError` to `:section` and import it in both — adapt as needed.)
- [ ] **Step 2 — `tests/sim/test_diffuse.cpp`:** on a path graph (reuse the `make_path` helper pattern from `test_wave.cpp`), set a spike `u.data = {1,0,0,0,0}`; record `sum0 = Σu`, `var0`. Step `DiffuseStepper{alpha=0.5}` N times with a small dt (CFL: `dt·α·λmax<2`). Assert (a) sum conserved: `|Σu − sum0| < 1e-9` (Δ's all-ones kernel), (b) variance non-increasing each step (heat smooths), (c) determinism: two runs byte-identical (`==`).
- [ ] **Step 3 — build + run** `--test-case="Diffuse*"`. Commit: `feat(sim): DiffuseStepper (heat on the shared Δ) — multi-field proof on one Section type`.

---

## Task 4: Controller refactor (`u_`/`v_`, `displacement()`) + verify byte-identical

**Files:** `bridge/src/aleph.edit/aleph.edit-controller.cppm`, `tests/edit/test_sim_controller.cpp`.

- [ ] **Step 1 — controller members:** replace `aleph::sim::ScalarField field_{};` with `aleph::sim::Section<double> u_{}, v_{};`. `enable_sim(true)`: `u_ = aleph::sim::Section<double>::zeros(operator_.node_order); v_ = aleph::sim::Section<double>::zeros(operator_.node_order);`. `kick(n, amp)` → `return v_.add(n, amp);` (VELOCITY — matches the old `phi_dot += amp`). `step(dt)` → `auto r = stepper_.step(u_, v_, operator_.matrix, static_cast<double>(dt));` (then re-bake on success). `rebuild_operator_and_reproject()` → `u_.reproject(operator_.node_order); v_.reproject(operator_.node_order);`. Replace the `field()` accessor with `[[nodiscard]] const aleph::sim::Section<double>& displacement() const noexcept { return u_; }`.
- [ ] **Step 2 — φ→vcol projection** in `rebuild_backends_from_prev`: the loop that reads `field_.order`/`field_.phi` → `u_.order`/`u_.data`, and the guard `field_.size()==prev_.entities.size()` → `u_.size()==prev_.entities.size()`. (The φ value rendered is the DISPLACEMENT `u_`, not velocity — do NOT use `v_`.)
- [ ] **Step 3 — `tests/edit/test_sim_controller.cpp`:** `ctl.field().size()` → `ctl.displacement().size()`; `ctl.field().phi` → `ctl.displacement().data`; the `phi_of` helper's `f.order`/`f.phi` → `ctl.displacement().order`/`.data`. Keep both oracles.
- [ ] **Step 4 — build + FULL verify:**
  - `cmake --build build-release && ctest --test-dir build-release` → all pass (Section/wave/diffuse/controller/build_sw). Strict gate → 0.
  - **Byte-identical guard:** `mkdir -p /tmp/after; ./build-release/apps/aleph_edit/aleph_edit --wave /tmp/after; diff -rq /tmp/base /tmp/after` → **NO differences** (the refactor changed nothing observable). If ANY frame differs, STOP — the refactor altered arithmetic.
  - `apps/aleph_edit/main.cpp` should need NO change (only uses enable_sim/kick/step/raster_scene) — confirm it still builds + `--wave-live` launches.
- [ ] **Step 5 — commit** `feat(sim): controller on Section<f64> u/v (displacement/velocity); --wave byte-identical`.

---

## Final verification
- [ ] `ctest` all pass; release-strict 0 warnings.
- [ ] `diff -rq /tmp/base /tmp/after` empty (wave byte-identical vs pre-refactor).
- [ ] `Section<Vec3>` test passes (T-genericity); `DiffuseStepper` smooths + conserves sum (multi-field proof).

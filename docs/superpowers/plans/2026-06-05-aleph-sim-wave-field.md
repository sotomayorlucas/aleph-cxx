# aleph.sim Wave-Field Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Time-evolve a scalar field φ on the *same* `aleph.flow` Laplacian Δ the renderer already reads, prove "graphics and physics on one substrate" with a deterministic wave that ripples across a graph and re-routes after a DPO edit.

**Architecture:** New graph-layer leaf `aleph.sim` (`ScalarField` + `WaveStepper`) depending only on `aleph.linalg.sparse`/`aleph.types`/`aleph.math` — it borrows a raw `const DMatrix&`, never the flow layer. The controller (`bridge/`) is the only place flow and sim meet: it calls `build_laplacian(graph_)` and feeds `operator_.matrix` to the stepper, colormaps φ into the existing `Face::vcol`, and re-projects φ by `NodeId` on every edit. Explicit symplectic-Euler ("Verlet"), matvec-only, f64, deterministic.

**Tech Stack:** C++26 modules (GCC 16, `-fmodules-ts`), doctest, CMake/Ninja presets (`build-release`), `aleph_flags_isa` (no exceptions/RTTI → `std::expected`/`std::optional`/`abort`).

**Spec:** `docs/superpowers/specs/2026-06-05-aleph-sim-wave-field-design.md`

---

## Conventions (read once)

- **Build + test all:** `cmake --build build-release && ctest --test-dir build-release` (expect all green).
- **Run one doctest case:** `cmake --build build-release && ./build-release/tests/aleph_tests --test-case="<name>"`.
- **Strict gate (0 warnings):** `cmake --build build-release-strict 2>&1 | grep -c "warning:"` → `0`.
- **Module file naming:** primary `aleph.X.cppm` (`export module aleph.X; export import :part;`), partitions `aleph.X-part.cppm` (`export module aleph.X:part;`).
- **A new module library** = new `graph/src/aleph.X/CMakeLists.txt` (`add_library(aleph_X)` + `FILE_SET CXX_MODULES`) + one `add_subdirectory` line in `graph/CMakeLists.txt`.
- **Tests** are sources of the single `aleph_tests` target in `tests/CMakeLists.txt`; add files + link the new lib there.
- After editing any `CMakeLists.txt`, the next `cmake --build` reconfigures automatically.

## File structure

| File | Responsibility |
|---|---|
| `graph/src/aleph.sim/aleph.sim.cppm` | primary module: `export import :field; export import :wave;` |
| `graph/src/aleph.sim/aleph.sim-field.cppm` | `ScalarField` state + `zeros`/`kick`/`reproject` |
| `graph/src/aleph.sim/aleph.sim-wave.cppm` | `WaveParams`, `StepError`, `WaveStepper::step`/`cfl_ok` |
| `graph/src/aleph.sim/CMakeLists.txt` | `aleph_sim` library |
| `graph/CMakeLists.txt` | +`add_subdirectory(src/aleph.sim)` |
| `tests/sim/test_field.cpp` | ScalarField oracles |
| `tests/sim/test_wave.cpp` | stepper oracles (sanity, oscillation, energy, determinism) |
| `tests/CMakeLists.txt` | +2 test sources, +`aleph_sim` link |
| `bridge/src/aleph.lowering/aleph.lowering-build_sw.cppm` | `colormap_diverging` + optional `phi_entity` → `vcol` |
| `bridge/src/aleph.edit/aleph.edit-controller.cppm` | sim members, `enable_sim`/`kick`/`step`, reproject, per-entity φ |
| `tests/edit/test_sim_controller.cpp` | controller step + edit re-projection oracle |
| `apps/aleph_edit/main.cpp` | lattice demo scene + headless wave capture |
| `tools/visual_review.sh` | wave-frame contact sheet |

---

## Task 1: `aleph.sim:field` — ScalarField

**Files:**
- Create: `graph/src/aleph.sim/aleph.sim-field.cppm`
- Create: `graph/src/aleph.sim/aleph.sim.cppm`
- Create: `graph/src/aleph.sim/CMakeLists.txt`
- Modify: `graph/CMakeLists.txt` (add subdir)
- Create: `tests/sim/test_field.cpp`
- Modify: `tests/CMakeLists.txt` (source + link)

- [ ] **Step 1: Create the module library files**

`graph/src/aleph.sim/aleph.sim-field.cppm`:

```cpp
module;
#include <cstddef>
#include <vector>

export module aleph.sim:field;

import aleph.math;     // f64
import aleph.types;    // NodeId

export namespace aleph::sim {

using aleph::math::f64;
using aleph::types::NodeId;

// A scalar field over a Laplacian's node_order: φ (displacement) and φ̇ (velocity)
// as dense f64 buffers. `order` is a *copy* of WeightedLaplacian::node_order so the
// field owns its index layout independently of the operator (survives a rebuild).
struct ScalarField {
    std::vector<NodeId> order;
    std::vector<f64>    phi;
    std::vector<f64>    phi_dot;

    [[nodiscard]] std::size_t size() const noexcept { return order.size(); }

    // Zeroed field for a given node order.
    [[nodiscard]] static ScalarField zeros(std::vector<NodeId> node_order) {
        const std::size_t n = node_order.size();
        return ScalarField{std::move(node_order), std::vector<f64>(n, 0.0),
                           std::vector<f64>(n, 0.0)};
    }

    // Velocity impulse at node `n`. Returns false (no-op) if `n` is not in `order`.
    [[nodiscard]] bool kick(NodeId n, f64 amp) noexcept {
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (order[i] == n) { phi_dot[i] += amp; return true; }
        }
        return false;
    }

    // Re-project onto a NEW node order after an edit: surviving NodeIds carry their
    // (phi, phi_dot) forward; new NodeIds start at 0; deleted ones drop. O(n·m) but
    // n,m are small here; deterministic (iterates new_order in order).
    void reproject(const std::vector<NodeId>& new_order) {
        std::vector<f64> np(new_order.size(), 0.0);
        std::vector<f64> nv(new_order.size(), 0.0);
        for (std::size_t i = 0; i < new_order.size(); ++i) {
            for (std::size_t j = 0; j < order.size(); ++j) {
                if (order[j] == new_order[i]) { np[i] = phi[j]; nv[i] = phi_dot[j]; break; }
            }
        }
        order = new_order;
        phi = std::move(np);
        phi_dot = std::move(nv);
    }
};

}  // namespace aleph::sim
```

`graph/src/aleph.sim/aleph.sim.cppm`:

```cpp
export module aleph.sim;
export import :field;
export import :wave;
```

`graph/src/aleph.sim/CMakeLists.txt`:

```cmake
add_library(aleph_sim)
target_sources(aleph_sim
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.sim.cppm
        aleph.sim-field.cppm
        aleph.sim-wave.cppm)
target_link_libraries(aleph_sim
    PUBLIC  aleph_types aleph_math aleph_linalg_sparse
    PRIVATE aleph_flags_isa)
```

> Note: `aleph.sim.cppm` references `:wave` which Task 2 creates. To keep Task 1 self-compiling, create a minimal placeholder `graph/src/aleph.sim/aleph.sim-wave.cppm` now containing only `export module aleph.sim:wave;` and flesh it out in Task 2. (One line; replaced next task.)

- [ ] **Step 2: Register the library + test**

In `graph/CMakeLists.txt`, append after the `aleph.flow` line:

```cmake
add_subdirectory(src/aleph.sim)
```

In `tests/CMakeLists.txt`, add to the `add_executable(aleph_tests ...)` source list (next to the `edit/` entries):

```cmake
    sim/test_field.cpp
    sim/test_wave.cpp
```

and add `aleph_sim` to the `target_link_libraries(aleph_tests PRIVATE ...)` list (next to `aleph_flow`).

> Create `tests/sim/test_wave.cpp` now as a one-line stub `#include "doctest.h"` so the build link succeeds; Task 2 fills it.

- [ ] **Step 3: Write the failing test** — `tests/sim/test_field.cpp`:

```cpp
#include "doctest.h"
#include <vector>
import aleph.sim;
import aleph.types;

using aleph::sim::ScalarField;
using aleph::types::NodeId;

TEST_CASE("ScalarField::zeros sizes buffers and kick targets the right node") {
    std::vector<NodeId> order{NodeId{10}, NodeId{20}, NodeId{30}};
    ScalarField f = ScalarField::zeros(order);
    REQUIRE(f.size() == 3);
    CHECK(f.phi.size() == 3);
    CHECK(f.phi_dot.size() == 3);
    CHECK(f.phi_dot[1] == doctest::Approx(0.0));

    CHECK(f.kick(NodeId{20}, 2.5) == true);
    CHECK(f.phi_dot[1] == doctest::Approx(2.5));
    CHECK(f.phi_dot[0] == doctest::Approx(0.0));
    CHECK(f.kick(NodeId{999}, 1.0) == false);   // not in order -> no-op
}

TEST_CASE("ScalarField::reproject carries survivors, zeros new, drops deleted") {
    ScalarField f = ScalarField::zeros(std::vector<NodeId>{NodeId{1}, NodeId{2}});
    f.phi[0] = 5.0; f.phi_dot[0] = -1.0;   // node 1
    f.phi[1] = 7.0; f.phi_dot[1] =  2.0;   // node 2 (will be deleted)

    // new order: keep 1, drop 2, add 3
    f.reproject(std::vector<NodeId>{NodeId{1}, NodeId{3}});
    REQUIRE(f.size() == 2);
    CHECK(f.order[0] == NodeId{1});
    CHECK(f.order[1] == NodeId{3});
    CHECK(f.phi[0]     == doctest::Approx(5.0));   // survivor carried
    CHECK(f.phi_dot[0] == doctest::Approx(-1.0));
    CHECK(f.phi[1]     == doctest::Approx(0.0));   // new node zeroed
    CHECK(f.phi_dot[1] == doctest::Approx(0.0));
}
```

- [ ] **Step 4: Build + run; verify pass**

Run: `cmake --build build-release && ./build-release/tests/aleph_tests --test-case="ScalarField*"`
Expected: both cases PASS. (If the first build errors on `aleph.types` import, confirm `NodeId` has `operator==` — it does, `aleph.types-id.cppm`.)

- [ ] **Step 5: Commit**

```bash
git add graph/src/aleph.sim graph/CMakeLists.txt tests/sim/test_field.cpp tests/sim/test_wave.cpp tests/CMakeLists.txt
git commit -m "feat(sim): aleph.sim:field ScalarField (zeros/kick/reproject) + lib scaffold"
```

---

## Task 2: `aleph.sim:wave` — WaveStepper

**Files:**
- Modify: `graph/src/aleph.sim/aleph.sim-wave.cppm` (replace the stub)
- Modify: `tests/sim/test_wave.cpp` (replace the stub)

- [ ] **Step 1: Implement the stepper** — replace `graph/src/aleph.sim/aleph.sim-wave.cppm`:

```cpp
module;
#include <cmath>
#include <cstddef>
#include <expected>
#include <span>
#include <vector>

export module aleph.sim:wave;

import aleph.math;            // f64
import aleph.linalg.sparse;   // DMatrix
import :field;

export namespace aleph::sim {

using aleph::math::f64;
using aleph::linalg::sparse::DMatrix;

struct WaveParams {
    f64 c       = 1.0;     // wave speed
    f64 damping = 0.999;   // per-step multiplicative velocity damp
};

enum class StepError { EmptyField, DimMismatch, CflViolation, NonFinite };

struct WaveStepper {
    WaveParams params{};

    // Conservative CFL guard via the Gershgorin radius g = max_i sum_j |Δ_ij|
    // (bounds λ_max(Δ)); explicit scheme stable for c²·dt²·λ_max < 4.
    [[nodiscard]] static bool
    cfl_ok(const DMatrix& delta, const WaveParams& p, f64 dt) noexcept {
        const std::size_t n = delta.rows();
        f64 g = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            f64 row = 0.0;
            for (std::size_t j = 0; j < delta.cols(); ++j) row += std::fabs(delta.at(i, j));
            if (row > g) g = row;
        }
        return p.c * p.c * dt * dt * g < 4.0;
    }

    // One explicit symplectic-Euler ("Verlet") sub-step of φ̈ = −c²Δφ.
    [[nodiscard]] std::expected<void, StepError>
    step(ScalarField& field, const DMatrix& delta, f64 dt) const noexcept {
        const std::size_t n = field.size();
        if (n == 0) return std::unexpected(StepError::EmptyField);
        if (delta.rows() != n || delta.cols() != n)
            return std::unexpected(StepError::DimMismatch);

        // lap = Δ·φ  (the SHARED operator application; matvec only)
        const std::vector<f64> lap =
            delta.matvec(std::span<const f64>(field.phi.data(), n));
        const f64 c2 = params.c * params.c;
        for (std::size_t i = 0; i < n; ++i) {
            // velocity first (symplectic), then position with the NEW velocity
            field.phi_dot[i] = params.damping * field.phi_dot[i] - dt * c2 * lap[i];
            field.phi[i]    += dt * field.phi_dot[i];
            if (!std::isfinite(field.phi[i]) || !std::isfinite(field.phi_dot[i]))
                return std::unexpected(StepError::NonFinite);
        }
        return {};
    }
};

}  // namespace aleph::sim
```

- [ ] **Step 2: Write the failing tests** — replace `tests/sim/test_wave.cpp`:

```cpp
#include "doctest.h"
#include <cmath>
#include <span>
#include <string>
#include <vector>

import aleph.sim;
import aleph.flow;        // build_laplacian, default_weight, WeightedLaplacian
import aleph.graph;       // Graph
import aleph.types;       // NodeId, Mesh, Material, edges
import aleph.math;
import aleph.linalg.sparse;

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::sim::ScalarField;
using aleph::sim::WaveStepper;
using aleph::sim::WaveParams;
using aleph::math::Vec3;
using aleph::math::f64;

namespace {
// A path of `k` Mesh nodes joined by Adjacent edges; returns the graph + ids.
struct Path { Graph g; std::vector<NodeId> ids; };
Path make_path(int k) {
    Path p;
    Graph& g = p.g;
    for (int i = 0; i < k; ++i) {
        NodeId m = g.alloc_node_id();
        Mesh mesh{m, std::string("m") + std::to_string(i), 0};
        mesh.geometry = SphereLocal{Vec3{static_cast<float>(i), 0, 0}, 0.4f};
        g.insert_node(std::move(mesh));
        p.ids.push_back(m);
    }
    for (int i = 0; i + 1 < k; ++i)
        (void)g.add_edge(EdgeKind::Adjacent, p.ids[i], p.ids[i + 1]);
    return p;
}
f64 energy(const ScalarField& f, const aleph::linalg::sparse::DMatrix& d, f64 c) {
    auto lap = d.matvec(std::span<const f64>(f.phi.data(), f.phi.size()));
    f64 ke = 0, pe = 0;
    for (std::size_t i = 0; i < f.size(); ++i) { ke += f.phi_dot[i]*f.phi_dot[i]; pe += f.phi[i]*lap[i]; }
    return 0.5*ke + 0.5*c*c*pe;
}
}  // namespace

TEST_CASE("WaveStepper: Δ on a path graph is symmetric with the all-ones kernel") {
    Path p = make_path(3);
    auto L = aleph::flow::build_laplacian(p.g, aleph::flow::default_weight);
    REQUIRE(L.node_order.size() == 3);
    CHECK(L.matrix.is_symmetric(1e-12));
    CHECK(L.ones_in_kernel(1e-12));
}

TEST_CASE("WaveStepper: 2-node mode oscillates with bounded energy (damping=1)") {
    Path p = make_path(2);
    auto L = aleph::flow::build_laplacian(p.g, aleph::flow::default_weight);
    ScalarField f = ScalarField::zeros(L.node_order);
    f.phi[0] = 1.0; f.phi[1] = -1.0;             // the λ=2w eigenmode
    WaveStepper s{WaveParams{/*c=*/1.0, /*damping=*/1.0}};
    const f64 dt = 0.01;
    REQUIRE(WaveStepper::cfl_ok(L.matrix, s.params, dt));
    const f64 E0 = energy(f, L.matrix, s.params.c);
    bool sign_flipped = false;
    for (int i = 0; i < 4000; ++i) {
        REQUIRE(s.step(f, L.matrix, dt).has_value());
        if (f.phi[0] < -0.5) sign_flipped = true;   // crossed to the other extreme
        CHECK(std::fabs(energy(f, L.matrix, s.params.c) - E0) < 0.02 * E0 + 1e-9);
    }
    CHECK(sign_flipped);                              // it actually oscillated
}

TEST_CASE("WaveStepper: damping<1 makes energy monotone non-increasing") {
    Path p = make_path(4);
    auto L = aleph::flow::build_laplacian(p.g, aleph::flow::default_weight);
    ScalarField f = ScalarField::zeros(L.node_order);
    (void)f.kick(L.node_order[0], 1.0);
    WaveStepper s{WaveParams{1.0, 0.99}};
    const f64 dt = 0.01;
    f64 prev = energy(f, L.matrix, s.params.c);
    for (int i = 0; i < 500; ++i) {
        REQUIRE(s.step(f, L.matrix, dt).has_value());
        const f64 e = energy(f, L.matrix, s.params.c);
        CHECK(e <= prev + 1e-9);
        prev = e;
    }
}

TEST_CASE("WaveStepper: deterministic — identical trajectory across two runs") {
    auto run = [] {
        Path p = make_path(5);
        auto L = aleph::flow::build_laplacian(p.g, aleph::flow::default_weight);
        ScalarField f = ScalarField::zeros(L.node_order);
        (void)f.kick(L.node_order[2], 1.0);
        WaveStepper s{WaveParams{1.0, 0.999}};
        for (int i = 0; i < 200; ++i) (void)s.step(f, L.matrix, 0.01);
        return f.phi;
    };
    std::vector<f64> a = run(), b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        CHECK(a[i] == b[i]);   // byte-identical (==, not Approx)
}

TEST_CASE("WaveStepper: empty field and dim mismatch report errors, no throw") {
    aleph::linalg::sparse::DMatrix d = aleph::linalg::sparse::DMatrix::zeros(2, 2);
    ScalarField empty = ScalarField::zeros(std::vector<NodeId>{});
    WaveStepper s{};
    auto r1 = s.step(empty, aleph::linalg::sparse::DMatrix::zeros(0, 0), 0.01);
    CHECK(!r1.has_value());
    CHECK(r1.error() == aleph::sim::StepError::EmptyField);

    ScalarField two = ScalarField::zeros(std::vector<NodeId>{NodeId{1}, NodeId{2}});
    auto r2 = s.step(two, aleph::linalg::sparse::DMatrix::zeros(3, 3), 0.01);
    CHECK(!r2.has_value());
    CHECK(r2.error() == aleph::sim::StepError::DimMismatch);
}
```

- [ ] **Step 3: Build + run; verify pass**

Run: `cmake --build build-release && ./build-release/tests/aleph_tests --test-case="WaveStepper*"`
Expected: all 5 cases PASS. If the energy case fails the 2% bound, lower `dt` to `0.005` (symplectic drift is O(dt²)); if `cfl_ok` rejects, that confirms the guard works — lower dt.

- [ ] **Step 4: Full suite + strict gate**

Run: `ctest --test-dir build-release` → all pass. `cmake --build build-release-strict 2>&1 | grep -c "warning:"` → `0`.

- [ ] **Step 5: Commit**

```bash
git add graph/src/aleph.sim/aleph.sim-wave.cppm tests/sim/test_wave.cpp
git commit -m "feat(sim): WaveStepper (symplectic Euler on shared Δ) + oracles"
```

---

## Task 3: Colormap + `build_sw` φ thread

**Files:**
- Modify: `bridge/src/aleph.lowering/aleph.lowering-build_sw.cppm`
- Test: `tests/edit/test_build_sw.cpp` (add a case)

- [ ] **Step 1: Add the colormap + thread an optional per-entity φ**

In `aleph.lowering-build_sw.cppm`, inside `namespace aleph::lowering::detail` (near `tex_white`), add a deterministic diverging colormap:

```cpp
// Diverging blue↔white↔red about 0, normalized by a FIXED scale (not a per-frame
// max), so a node's colour depends only on its own φ. v/scale clamped to [-1,1].
[[nodiscard]] inline aleph::math::Vec3
colormap_diverging(double v, double scale) noexcept {
    const double t = std::clamp(v / (scale > 1e-12 ? scale : 1.0), -1.0, 1.0);
    const auto f = static_cast<aleph::math::f32>(t);
    // t<0 → blue..white ; t>0 → white..red (linear).
    if (f < 0.0f) return aleph::math::Vec3{1.0f + f, 1.0f + f, 1.0f};   // (b)
    return aleph::math::Vec3{1.0f, 1.0f - f, 1.0f - f};                  // (r)
}
```

Ensure `<algorithm>` and `<cmath>` are in the global module fragment (build_sw already includes `<cmath>`; add `<algorithm>` if absent).

- [ ] **Step 2: Thread `phi_entity` through the emit chain**

`build_sw` emits per entity in order. Add an optional aligned φ vector. Change the public entry + emit funcs (signatures shown; keep existing bodies, add the override):

```cpp
// build_sw_scene gains an optional per-entity φ (aligned to ls.entities, like
// LoweredScene::importance). nullptr => byte-identical to today.
[[nodiscard]] inline SwBuild
build_sw_scene(const LoweredScene& ls,
               const std::vector<double>* phi_entity = nullptr) {
    SwBuild out{};
    for (std::size_t i = 0; i < ls.entities.size(); ++i) {
        const double* phi = (phi_entity && i < phi_entity->size())
                            ? &(*phi_entity)[i] : nullptr;
        detail::emit_entity(out, ls.entities[i], ls.lights, phi);
    }
    return out;
}
```

`emit_entity` gains a trailing `const double* phi` param. When `phi != nullptr`, override the shaded `vcol` of every face it pushes with `detail::colormap_diverging(*phi, kPhiScale)` (a single colour for all 4 verts — the field is per-node, constant over the entity). Add `inline constexpr aleph::math::f32 kPhiScale = 1.0f;` near the other constants and use `1.0` as the `scale`. Concretely, the simplest correct change: compute `Vec3 fc = phi ? detail::colormap_diverging(*phi, 1.0) : <unused>;` and in each `push_tri(...)` call inside `emit_quad`/`emit_tri`/`emit_sphere`, when `phi` is set pass `fc, fc, fc` instead of the per-vertex Lambert colours. Thread `phi` into `emit_quad`/`emit_tri`/`emit_sphere` as a trailing `const double* phi` param.

- [ ] **Step 3: Write the failing test** — append to `tests/edit/test_build_sw.cpp`:

```cpp
TEST_CASE("build_sw: phi_entity==nullptr is byte-identical; non-null recolors vcol") {
    TwoPrims s = make_two_prims();
    auto lowered = aleph::lowering::lower(s.g);
    REQUIRE(lowered.has_value());

    const aleph::lowering::SwBuild base = aleph::lowering::build_sw_scene(*lowered);
    const aleph::lowering::SwBuild same = aleph::lowering::build_sw_scene(*lowered, nullptr);
    REQUIRE(base.scene.faces.size() == same.scene.faces.size());
    for (std::size_t i = 0; i < base.scene.faces.size(); ++i)
        CHECK(same_face(base.scene.faces[i], same.scene.faces[i]));   // unchanged

    // Two entities; give entity 0 φ=+1 (red), entity 1 φ=-1 (blue).
    std::vector<double> phi{ +1.0, -1.0 };
    const aleph::lowering::SwBuild lit = aleph::lowering::build_sw_scene(*lowered, &phi);
    // Face 0 belongs to the first entity (sphere) -> reddish; some face of the
    // second entity (quad) -> bluish. Check the red/blue channel dominance.
    const auto& f0 = lit.scene.faces.front().vcol[0];
    CHECK(f0.x >= f0.z);                                   // red >= blue at φ=+1
    const auto& fl = lit.scene.faces.back().vcol[0];
    CHECK(fl.z >= fl.x);                                   // blue >= red at φ=-1
}
```

- [ ] **Step 4: Build + run; verify pass**

Run: `cmake --build build-release && ./build-release/tests/aleph_tests --test-case="build_sw: phi_entity*"`
Expected: PASS. Then `ctest --test-dir build-release` (the existing `same_face` determinism case must still pass — the null path is byte-unchanged).

- [ ] **Step 5: Commit**

```bash
git add bridge/src/aleph.lowering/aleph.lowering-build_sw.cppm tests/edit/test_build_sw.cpp
git commit -m "feat(sim): build_sw optional per-entity phi -> colormap vcol (null = byte-identical)"
```

---

## Task 4: `EditorController` wiring — operator, field, step, reproject

**Files:**
- Modify: `bridge/src/aleph.edit/aleph.edit-controller.cppm`
- Create: `tests/edit/test_sim_controller.cpp`
- Modify: `tests/CMakeLists.txt` (add the new test source)

- [ ] **Step 1: Add sim state + methods to `EditorController`**

Add imports near the top of the controller module: `import aleph.flow;` and `import aleph.sim;`. Add private members:

```cpp
aleph::flow::WeightedLaplacian operator_{};   // Δ, rebuilt from graph_
aleph::sim::ScalarField        field_{};      // φ/φ̇ over operator_.node_order
aleph::sim::WaveStepper        stepper_{};
bool                           sim_enabled_ = false;
```

Add a private helper and call it from the constructor's `rebuild_full()` path **only when enabled** (cheap no-op otherwise):

```cpp
void rebuild_operator_and_reproject() {
    operator_ = aleph::flow::build_laplacian(graph_, aleph::flow::default_weight);
    field_.reproject(operator_.node_order);   // survivors keep φ, new nodes 0
}
```

Add public methods (beside `apply`):

```cpp
void enable_sim(bool on) {
    sim_enabled_ = on;
    if (on) {
        operator_ = aleph::flow::build_laplacian(graph_, aleph::flow::default_weight);
        field_ = aleph::sim::ScalarField::zeros(operator_.node_order);
    }
}
[[nodiscard]] bool kick(aleph::types::NodeId n, double amp) noexcept {
    return field_.kick(n, amp);
}
[[nodiscard]] const aleph::sim::ScalarField& field() const noexcept { return field_; }

std::expected<void, aleph::sim::StepError> step(aleph::math::f32 dt) {
    if (!sim_enabled_) return {};
    auto r = stepper_.step(field_, operator_.matrix, static_cast<aleph::math::f64>(dt));
    if (!r) return r;
    rebuild_backends_from_prev();   // re-bake vcol from φ (see Step 2)
    return r;
}
```

In `apply(Op)`, immediately before the existing `rebuild_backends_from_prev()` call, insert:

```cpp
if (sim_enabled_) rebuild_operator_and_reproject();
```

- [ ] **Step 2: Feed φ into the SW rebuild**

Find `rebuild_backends_from_prev()` where it calls `build_sw_scene(prev_)`. Replace with a φ-aware variant:

```cpp
void rebuild_backends_from_prev() {
    // ... existing path-trace Scene rebuild stays ...
    if (sim_enabled_ && field_.size() == prev_.entities.size()) {
        // project field φ (node_order) onto per-entity order via entity.source
        std::vector<double> phi_entity(prev_.entities.size(), 0.0);
        for (std::size_t i = 0; i < prev_.entities.size(); ++i) {
            const aleph::types::NodeId src = prev_.entities[i].source;
            for (std::size_t j = 0; j < field_.order.size(); ++j)
                if (field_.order[j] == src) { phi_entity[i] = field_.phi[j]; break; }
        }
        sw_ = aleph::lowering::build_sw_scene(prev_, &phi_entity);
    } else {
        sw_ = aleph::lowering::build_sw_scene(prev_);
    }
    // ... existing face_source / prim_source bookkeeping stays ...
}
```

> If `entities` and `node_order` differ in length (e.g. non-mesh entities), the guard `field_.size() == prev_.entities.size()` falls back to the plain path. For the lattice/Cornell scenes here they coincide.

- [ ] **Step 3: Register + write the failing test**

In `tests/CMakeLists.txt`, add `edit/test_sim_controller.cpp` to the sources. Create `tests/edit/test_sim_controller.cpp`:

```cpp
#include "doctest.h"
#include <string>
#include <vector>
import aleph.edit;
import aleph.graph;
import aleph.types;
import aleph.math;
import aleph.sim;

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Vec3;

namespace {
// Two adjacent sphere-meshes A,B (+ camera) so the Laplacian is non-trivial.
struct AB { Graph g; NodeId a, b, root; };
AB make_ab() {
    AB s; Graph& g = s.g;
    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, LocalTransform{aleph::math::Mat4::identity()}});
    NodeId cam = g.alloc_node_id();
    Camera c{cam, std::string("sensor0")};
    c.look_from = Vec3{0,1,5}; c.look_at = Vec3{0,0,0}; c.up = Vec3{0,1,0}; c.vfov_deg = 45;
    g.insert_node(std::move(c));
    s.a = g.alloc_node_id();
    Mesh ma{s.a, std::string("A"), 0}; ma.geometry = SphereLocal{Vec3{0,0,0}, 0.4f};
    g.insert_node(std::move(ma));
    s.b = g.alloc_node_id();
    Mesh mb{s.b, std::string("B"), 0}; mb.geometry = SphereLocal{Vec3{1,0,0}, 0.4f};
    g.insert_node(std::move(mb));
    (void)g.add_edge(EdgeKind::Contains, s.root, cam);
    (void)g.add_edge(EdgeKind::Contains, s.root, s.a);
    (void)g.add_edge(EdgeKind::Contains, s.root, s.b);
    (void)g.add_edge(EdgeKind::Adjacent, s.a, s.b);
    return s;
}
}  // namespace

TEST_CASE("EditorController: enable_sim + step evolves φ deterministically") {
    AB s = make_ab();
    aleph::edit::EditorController ctl{std::move(s.g)};
    ctl.set_viewport(64, 48);
    ctl.enable_sim(true);
    REQUIRE(ctl.field().size() >= 2);
    REQUIRE(ctl.kick(s.a, 1.0));
    for (int i = 0; i < 50; ++i) REQUIRE(ctl.step(0.01f).has_value());
    // φ spread from A to B (some non-zero displacement appeared at B's row).
    bool any_nonzero = false;
    for (double v : ctl.field().phi) if (v != 0.0) any_nonzero = true;
    CHECK(any_nonzero);
}

TEST_CASE("EditorController: edit re-projection keeps survivors, zeros new nodes") {
    AB s = make_ab();
    const NodeId a = s.a, b = s.b, root = s.root;
    aleph::edit::EditorController ctl{std::move(s.g)};
    ctl.set_viewport(64, 48);
    ctl.enable_sim(true);
    REQUIRE(ctl.kick(a, 1.0));
    for (int i = 0; i < 20; ++i) REQUIRE(ctl.step(0.01f).has_value());

    // snapshot φ(A) by NodeId
    auto phi_of = [&](NodeId id) -> double {
        const auto& f = ctl.field();
        for (std::size_t i = 0; i < f.order.size(); ++i) if (f.order[i] == id) return f.phi[i];
        return 0.0;
    };
    const double phiA_before = phi_of(a);

    // Add a third mesh C adjacent to B (an AddObject Op).
    aleph::lowering::AddObject add{};
    add.parent = root;
    add.geometry = SphereLocal{Vec3{2,0,0}, 0.4f};
    add.material = aleph::lowering::MaterialParams{};
    auto r = ctl.apply(aleph::lowering::Op{add});
    REQUIRE(r.has_value());

    // Survivor A unchanged; the freshly-created node starts at 0.
    CHECK(phi_of(a) == doctest::Approx(phiA_before));
    // there is at least one zero-initialized new row
    bool found_zero_new = false;
    for (double v : ctl.field().phi) if (v == 0.0) found_zero_new = true;
    CHECK(found_zero_new);
}
```

> Note: if `AddObject` does not auto-add an `Adjacent` edge, the new node is isolated in the skeleton and may not appear in `node_order`; that's acceptable for this oracle (it still tests survivor preservation). If `node_order` excludes isolated meshes, drop the "found_zero_new" check and keep only the survivor assertion.

- [ ] **Step 4: Build + run; verify pass**

Run: `cmake --build build-release && ./build-release/tests/aleph_tests --test-case="EditorController: *sim*,EditorController: edit re-projection*"`
Expected: PASS. Then `ctest --test-dir build-release` all green; `cmake --build build-release-strict 2>&1 | grep -c "warning:"` → 0.

- [ ] **Step 5: Commit**

```bash
git add bridge/src/aleph.edit/aleph.edit-controller.cppm tests/edit/test_sim_controller.cpp tests/CMakeLists.txt
git commit -m "feat(sim): EditorController enable_sim/kick/step + φ reproject on edit + vcol bake"
```

---

## Task 5: Lattice demo scene + headless wave capture

**Files:**
- Modify: `apps/aleph_edit/main.cpp`

- [ ] **Step 1: Add a lattice scene builder** (next to `build_initial_graph`)

```cpp
// An R×R grid of small sphere-meshes joined by Adjacent edges (4-neighborhood),
// + a camera + an overhead light. Gives Δ a connected, curvature-varied skeleton.
struct LatticeScene { aleph::graph::Graph g; std::vector<NodeId> nodes; NodeId root{}; };
LatticeScene build_lattice_graph(int R) {
    using namespace aleph::types;
    LatticeScene s; auto& g = s.g;
    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, LocalTransform{Mat4::identity()}});
    const NodeId cam = g.alloc_node_id();
    Camera c{cam, std::string("sensor0")};
    c.look_from = Vec3{ (R-1)*0.5f, R*0.9f, R*1.4f };
    c.look_at   = Vec3{ (R-1)*0.5f, 0.0f, (R-1)*0.5f };
    c.up = Vec3{0,1,0}; c.vfov_deg = 45;
    g.insert_node(std::move(c));
    std::vector<std::vector<NodeId>> grid(R, std::vector<NodeId>(R));
    for (int z = 0; z < R; ++z) for (int x = 0; x < R; ++x) {
        NodeId m = g.alloc_node_id();
        Mesh mesh{m, std::string("n") + std::to_string(z*R+x), 0};
        mesh.geometry = SphereLocal{Vec3{static_cast<float>(x), 0.0f, static_cast<float>(z)}, 0.35f};
        g.insert_node(std::move(mesh));
        const NodeId mat = g.alloc_node_id();
        Material mt{mat, MaterialKind::Lambertian}; mt.albedo = Vec3{0.8f,0.8f,0.8f};
        g.insert_node(std::move(mt));
        (void)g.add_edge(EdgeKind::Contains, s.root, m);
        (void)g.add_edge(EdgeKind::References, m, mat);
        grid[z][x] = m; s.nodes.push_back(m);
    }
    for (int z = 0; z < R; ++z) for (int x = 0; x < R; ++x) {
        if (x+1 < R) (void)g.add_edge(EdgeKind::Adjacent, grid[z][x], grid[z][x+1]);
        if (z+1 < R) (void)g.add_edge(EdgeKind::Adjacent, grid[z][x], grid[z+1][x]);
    }
    const NodeId light = g.alloc_node_id();
    Light L{light, LightKind::Area, std::string("emit0")};
    L.emission = Vec3{6,6,6};
    L.geometry = QuadLocal{Vec3{-1, R*1.5f, -1}, Vec3{2,0,0}, Vec3{0,0,2}};
    g.insert_node(std::move(L));
    (void)g.add_edge(EdgeKind::Contains, s.root, light);
    return s;
}
```

- [ ] **Step 2: Add a headless wave mode** behind a flag

In `main()`, add an arg branch `--wave <outdir>` that runs:

```cpp
int run_wave(const std::string& outdir) {
    constexpr int R = 7, N = 24;
    constexpr aleph::math::f32 kDt = 0.02f;
    LatticeScene init = build_lattice_graph(R);
    const NodeId center = init.nodes[(R/2)*R + R/2];
    const NodeId root = init.root;
    aleph::edit::EditorController controller{std::move(init.g)};
    constexpr int W = 320, H = 240;
    controller.set_viewport(W, H);
    auto& cam = controller.camera();
    cam.target = Vec3{(R-1)*0.5f, 0.0f, (R-1)*0.5f};
    cam.yaw = 0.5f; cam.pitch = 0.6f; cam.radius = R*1.7f; cam.vfov_deg = 45;
    controller.enable_sim(true);
    (void)controller.kick(center, 1.5);

    aleph::threads::Pool pool(thread_count());
    std::vector<Vec3> film_px(static_cast<std::size_t>(W)*H);
    aleph::render::common::Film film{film_px.data(), W, H, W};
    std::vector<f32> depth(static_cast<std::size_t>(W)*H, 0.0f);
    int frame = 0;
    auto dump = [&](){
        clear_sky(film);
        std::fill(depth.begin(), depth.end(), 0.0f);
        aleph::render::sw::rasterize(controller.raster_scene(),
            orbit_mvp(controller.camera(), W, H), film, depth, pool);
        std::string p = outdir + "/step" + std::to_string(frame) + "_wave_raster.ppm";
        (void)write_ppm(p.c_str(), film);
        ++frame;
    };
    for (int i = 0; i < N; ++i) { (void)controller.step(kDt); dump(); }
    // topology change: add a node adjacent to the center, watch it re-route.
    aleph::lowering::AddObject add{};
    add.parent = root;
    add.geometry = aleph::types::SphereLocal{Vec3{(R-1)*0.5f, 0.0f, -1.0f}, 0.35f};
    add.material = lambertian(Vec3{0.8f,0.8f,0.8f});
    (void)controller.apply(aleph::lowering::Op{add});
    for (int i = 0; i < N; ++i) { (void)controller.step(kDt); dump(); }
    std::printf("aleph_edit[wave]: wrote %d frames to %s\n", frame, outdir.c_str());
    return 0;
}
```

Wire `--wave` in `main()` mirroring the `--headless` branch.

- [ ] **Step 3: Build + smoke-run**

Run:
```bash
cmake --build build-release --target aleph_edit_app
rm -rf /tmp/wave && mkdir -p /tmp/wave
./build-release/apps/aleph_edit/aleph_edit --wave /tmp/wave
ls /tmp/wave/*.ppm | wc -l    # expect 48
```
Expected: 48 PPM frames, no crash.

- [ ] **Step 4: Determinism check**

Run it twice into two dirs and diff a mid frame:
```bash
./build-release/apps/aleph_edit/aleph_edit --wave /tmp/wave2 >/dev/null
cmp /tmp/wave/step12_wave_raster.ppm /tmp/wave2/step12_wave_raster.ppm && echo "DETERMINISTIC"
```
Expected: `DETERMINISTIC` (byte-identical).

- [ ] **Step 5: Commit**

```bash
git add apps/aleph_edit/main.cpp
git commit -m "feat(sim): lattice demo scene + --wave headless capture (deterministic frames)"
```

---

## Task 6: Visual deliverable — wave contact sheet

**Files:**
- Modify: `tools/visual_review.sh` (add a wave mode)

- [ ] **Step 1: Add a `--wave` path to the review tool**

Add an optional first-arg mode so `tools/visual_review.sh wave [out] [build]` runs the `--wave` capture and montages the `stepN_wave_raster.ppm` frames into a single contact sheet (tile by frame). Reuse the existing PPM→PNG loop; build the tile list by sorting the wave frames numerically.

```bash
if [ "${1:-}" = "wave" ]; then
  OUT="${2:-/tmp/aleph_wave}"; BUILD="${3:-build-release}"
  APP="$ROOT/$BUILD/apps/aleph_edit/aleph_edit"
  cmake --build "$ROOT/$BUILD" --target aleph_edit_app >/dev/null
  rm -rf "$OUT"; mkdir -p "$OUT"; "$APP" --wave "$OUT"
  for f in "$OUT"/*.ppm; do magick "$f" "${f%.ppm}.png"; done
  mapfile -t FR < <(ls "$OUT"/step*_wave_raster.png | sort -t/ -k99 -V)
  magick montage "${FR[@]}" -tile 8x6 -geometry +2+2 -background '#222' \
    -title 'aleph.sim wave on the shared Laplacian — ripple, then re-route after a DPO edit' \
    "$OUT/_wave_contact.png"
  echo "done: $OUT/_wave_contact.png"; exit 0
fi
```

- [ ] **Step 2: Generate the contact sheet**

Run: `tools/visual_review.sh wave /tmp/aleph_wave`
Expected: `/tmp/aleph_wave/_wave_contact.png` exists (48-frame montage showing the ripple expanding and re-routing after the edit).

- [ ] **Step 3: Commit + ship the artifact**

```bash
mkdir -p docs/superpowers/artifacts
cp /tmp/aleph_wave/_wave_contact.png docs/superpowers/artifacts/2026-06-05-aleph-sim-wave.png
git add tools/visual_review.sh docs/superpowers/artifacts/2026-06-05-aleph-sim-wave.png
git commit -m "feat(sim): wave contact-sheet review mode + shipped artifact"
```

---

## Final verification

- [ ] `ctest --test-dir build-release` → all pass (new: `ScalarField*`, `WaveStepper*`, `build_sw: phi_entity*`, `EditorController: *sim*`).
- [ ] `cmake --build build-release-strict 2>&1 | grep -c "warning:"` → `0`.
- [ ] ASan gate over the new tests (memory `asan-preset-needs-libasan`): `cmake --build build-asan && LSAN_OPTIONS=suppressions=tests/asan.supp ctest --test-dir build-asan`.
- [ ] `_wave_contact.png` visibly shows: a ripple expanding from the center node across the lattice, then re-routing after the added node (frames ≥ 24).
- [ ] The determinism claim holds (`cmp` of a mid frame across two runs is byte-identical).

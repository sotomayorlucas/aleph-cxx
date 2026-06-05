# Design Spec — `aleph.sim`: scalar-field time-evolution on the shared Laplacian

**Vertical slice 1 of "one substrate for graphics AND physics".**
Date: 2026-06-05 · Status: APPROVED (design) · Branch context: `editor-raster-fixes` (Phase 6 editor shell exists).

---

## 0. Decisions (locked with the user)

- **Goal = research/novelty.** Differentiate through the mathematical foundation, not features any engine has.
- **Axis = unify one substrate.** Graphics and physics ride the *same* operator.
- **Weights = Ollivier-Ricci** (`aleph.flow::default_weight`, `w = 1 + max(κ, −0.95)`). Wave speed varies with the graph's intrinsic curvature — the same geometric Δ the renderer's `importance` channel already reads. This is the thesis made literal.
- **Scene = configurable.** The stepper is **scene-agnostic** (operates on any `WeightedLaplacian` + `ScalarField`), so it runs on the editor's own graph for free; **and** we ship a dedicated **N×N lattice demo scene** (mesh-nodes joined by `Adjacent` edges) as the legible showcase.

---

## 1. Summary & success criterion

Add **time-evolution of a scalar field φ on the graph's Mesh nodes**, where the operator driving the evolution is the *same* `aleph.flow::WeightedLaplacian` (`Δ = Dᵥ − Aᵥ`, Ollivier-Ricci-weighted, `aleph.flow-laplacian.cppm:71`) that the renderer's `importance` channel already derives its curvature from. The field is integrated by explicit symplectic-Euler ("Verlet"), colormapped into the existing raster `Face::vcol` path, and captured headless as a PPM sequence by `tools/visual_review.sh`.

Wave equation: `∂²φ/∂t² = −c²Δφ` (sign: the real Δ is `+D−A`, PSD, so `φ̈ = −c²Δφ` — flip vs. the Rust ring proxy whose `lap` is `−Δ`).

**The one demonstrable result that proves the thesis:**

> Build `Δ = build_laplacian(g, default_weight)` **once** on a connected `Adjacent` 1-skeleton. Kick φ at one node. Step the wave. The renderer shows a colored ripple **propagating across adjacent meshes** following graph adjacency. Then issue **one DPO/Op edit** that adds a node/edge; Δ is rebuilt from the *same* `build_laplacian`, φ is re-projected onto the new node set; the ripple's propagation **re-routes to follow the new topology** — in the same substrate, with no physics-specific mesh/collision structures.

Two artifacts prove it:
1. **Determinism oracle (CI gate):** same `(graph, kick, N, dt)` ⇒ **byte-identical** φ trajectory (SPEC §7), checked against a golden hash. The hard, machine-checkable claim.
2. **Visual artifact:** a `visual_review.sh` contact sheet of `stepN_wave_raster.ppm` frames showing the ripple, before and after the edit.

The thesis holds because the *operator object* and the *node-indexing* (`WeightedLaplacian::node_order`) are literally shared, not parallel copies.

---

## 2. Architecture

### New leaf module: `aleph.sim`

Lives under `graph/src/aleph.sim/`, sibling to `aleph.flow`/`aleph.sheaf`. Depends **only** on `aleph.linalg.sparse` (for `DMatrix` + `matvec`), `aleph.types` (`NodeId`), and `aleph.math` (`f64`). It deliberately does **not** depend on `aleph.flow`: the stepper borrows a raw `const DMatrix&`, never `WeightedLaplacian`, so the field/stepper are reusable against *any* operator and testable without the flow layer. It owns **field state + stepper only**; it must **not** import `aleph.graph`/`aleph.dpo`/`aleph.lowering` (those are upward). The `build_laplacian` call (which *does* use `aleph.flow`) lives in the controller (`bridge/`) and in the sim tests — `bridge` is the already-sanctioned cross-cutter (`aleph.lowering` is the only layer that touches `aleph.flow`/`aleph.sheaf`).

Partitions:
- `aleph.sim:field` — `ScalarField` (state + index).
- `aleph.sim:wave` — `WaveStepper` (Verlet) over a `ScalarField` + a borrowed `const DMatrix&`.

### Dependency graph

```
aleph.math ── aleph.linalg.sparse (DMatrix, LDLT)
      │                  │   │
      │                  │   └────────► aleph.sim   NEW leaf: ScalarField + WaveStepper
      │                  │                  ▲       (borrows a raw DMatrix; owns φ/φ̇;
      │            aleph.flow              │        NO flow/graph deps)
      │      (WeightedLaplacian,           │
      │       build_laplacian)             │
      │            │                       │
      └────────────┴───────────────────────┘
                   │ both feed →  aleph.edit::EditorController   (bridge/)
                   │              build_laplacian(graph_) → operator_.matrix → stepper
                   ▼
            aleph.lowering ──► aleph.render.sw (Face::vcol, rasterizer)
```
The controller is the *only* place `aleph.flow` and `aleph.sim` meet: it calls
`build_laplacian` and hands the resulting `DMatrix` to the stepper.

### Data flow

```
 Graph g
   │  build_laplacian(g, default_weight)          [aleph.flow]
   ▼
 WeightedLaplacian L { node_order, DMatrix matrix }   ── Δ, the SHARED operator
   │                                  │
   │  ScalarField over L.node_order   │  (also feeds :importance curvature today)
   ▼                                  ▼
 ScalarField{ phi, phi_dot, order } ─ WaveStepper::step(field, L.matrix, dt)
   │                                   φ̇ += −dt·c²·(Δφ);  φ += dt·φ̇
   ▼
 φ(t)  (one f64 per node, in node_order)
   │  project NodeId→entity-row via handle_map, colormap → f32 RGB
   ▼
 Face::vcol  ──► rasterize ──► framebuffer ──► dump() PPM ──► visual_review.sh
 ── on Op edit: g'=apply_op(g); L'=build_laplacian(g'); ScalarField::reproject(L→L')
```

---

## 3. Components

### 3.1 `ScalarField` (`aleph.sim:field`)

**Storage = a side table aligned to `WeightedLaplacian::node_order`, NOT a node attribute.** `Node` is a closed variant with no property bag, and the DPO engine value-copies survivors (`aleph.dpo-apply.cppm:138`), so an out-of-band field added as an attribute is clobbered on every structural edit. φ is keyed/ordered by `node_order` so `Δ·φ` (matvec) needs no gather.

```cpp
export module aleph.sim:field;
import aleph.math;            // f64
import aleph.types;           // NodeId
import std;

namespace aleph::sim {
using aleph::math::f64;
using aleph::types::NodeId;

struct ScalarField {
    std::vector<NodeId> order;     // == WeightedLaplacian::node_order at build time
    std::vector<f64>    phi;       // displacement,  size == order.size()
    std::vector<f64>    phi_dot;   // velocity,      size == order.size()

    [[nodiscard]] std::size_t size() const noexcept { return order.size(); }

    [[nodiscard]] static ScalarField zeros(std::vector<NodeId> order);

    // Velocity impulse at a node. Returns false (no-op) if `n` is not in `order`.
    [[nodiscard]] bool kick(NodeId n, f64 amp) noexcept;

    // Re-project onto a NEW node order after an edit: surviving NodeIds keep
    // (phi, phi_dot); new NodeIds start at 0; deleted drop. Deterministic O(n).
    void reproject(const std::vector<NodeId>& new_order);
};
}
```

### 3.2 `WaveStepper` (`aleph.sim:wave`)

Operates on a `ScalarField` + a **borrowed** `const DMatrix&` (the `.matrix` of a `WeightedLaplacian`). The Δ application is exactly **`DMatrix::matvec`** (`aleph.linalg.sparse-dense.cppm:68`). No solve, no factorization — time-stepping is matvec-only.

```cpp
export module aleph.sim:wave;
import aleph.math;
import aleph.linalg.sparse;   // DMatrix
import :field;
import std;

namespace aleph::sim {
using aleph::math::f64;
using aleph::linalg::sparse::DMatrix;

struct WaveParams { f64 c = 1.0; f64 damping = 0.999; };
enum class StepError { EmptyField, DimMismatch, CflViolation, NonFinite };

struct WaveStepper {
    WaveParams params{};

    // One explicit symplectic-Euler ("Verlet") sub-step:
    //   lap = Δ·φ                       // delta.matvec(field.phi)
    //   φ̇ = damping·φ̇ − dt·c²·lap        // velocity first
    //   φ += dt·φ̇                        // position uses NEW velocity (symplectic)
    [[nodiscard]] std::expected<void, StepError>
    step(ScalarField& field, const DMatrix& delta, f64 dt) const noexcept;

    // CFL guard via the cheap Gershgorin radius of Δ (no eigensolve). Conservative.
    [[nodiscard]] static bool
    cfl_ok(const DMatrix& delta, const WaveParams& p, f64 dt) noexcept;
};
}
```

### 3.3 Lattice demo scene builder (`bridge/` or app-side)

A pure factory that builds a `Graph` of an **N×N grid of `Mesh` nodes joined by `Adjacent` edges** (4-neighborhood), each Mesh a small sphere placed on a plane, plus a Camera + a Light so it lowers and renders. This gives Δ a connected skeleton with rich curvature variation (corners/edges/interior differ), so the Ollivier-Ricci-weighted wave visibly varies in speed. Deterministic (fixed positions/ids). Lives next to `build_initial_graph` in `apps/aleph_edit/main.cpp` (or a shared `bridge` helper if reused by tests).

> Decision "configurable": the stepper never sees the scene — it only sees `Δ` + `ScalarField`. So the lattice is purely a *demo input*; the same `step`/`enable_sim` path runs on the editor's default scene too (it just has a sparser/less-connected skeleton).

### 3.4 Edit re-projection — controller side (`aleph.edit`)

`EditorController` gains members and a helper:

```cpp
aleph::flow::WeightedLaplacian operator_;   // Δ, rebuilt from graph_
aleph::sim::ScalarField        field_;      // φ/φ̇ over operator_.node_order
aleph::sim::WaveStepper        stepper_;
bool                           sim_enabled_ = false;

void rebuild_operator_and_reproject() {
    operator_ = aleph::flow::build_laplacian(graph_, aleph::flow::default_weight);
    field_.reproject(operator_.node_order);   // survivors keep φ, new nodes 0
}
```

`apply(Op)` already ends by adopting `prev_` and calling `rebuild_backends_from_prev()`. Insert `rebuild_operator_and_reproject()` immediately before it, gated on `sim_enabled_`.

### 3.5 Stepper hook — `EditorController::step(f32 dt)`

Beside `apply()`. Evolves φ and re-bakes `vcol` **without mutating `graph_`** (preserving the "graph is the one source of truth" determinism contract):

```cpp
std::expected<void, aleph::sim::StepError> step(aleph::math::f32 dt) {
    if (!sim_enabled_) return {};
    auto r = stepper_.step(field_, operator_.matrix, static_cast<f64>(dt));
    if (!r) return r;                    // propagate StepError (no throw)
    rebuild_backends_from_prev();        // re-bake vcol from φ
    return {};
}
```

Also: `enable_sim(bool)` (builds the operator + zeros the field), `kick(NodeId, f64)` (delegates to `field_.kick`).

### 3.6 Render / colormap hook (`aleph.lowering` / `aleph.render.sw`)

`push_tri` already takes per-vertex `ca/cb/cc` and writes `f.vcol`. Thread an **optional per-entity `const std::vector<double>* phi_entity`** (aligned to `entities`, exactly like `LoweredScene::importance`, `aleph.lowering-lowered.cppm:118`) through `build_sw_scene → emit_entity → push_tri`. When present, replace the Lambert color with `colormap_diverging(phi_entity[i], scale)`:

```cpp
// pure-f32 diverging map (blue ↔ white ↔ red about 0), deterministic.
// `scale` is a FIXED constant (e.g. the kick amplitude), NOT a per-frame max|φ|,
// so a pixel's colour depends only on that node's φ — frames stay independent and
// the mapping is stable across the whole sequence. v/scale is clamped to [-1,1].
[[nodiscard]] aleph::math::Vec3 colormap_diverging(double v, double scale) noexcept;
```

**NodeId→entity-row projection** uses the existing `LoweredScene::handle_map` (`OrderedMap<NodeId,u32>`, `lowered.cppm:108`): for each entity row `i` with `source` NodeId `s`, look up φ for `s`. Honest index-domain note: `handle_map` covers only lowered Mesh entities; `node_order` covers all skeleton Mesh vertices — for these one-mesh-per-node scenes they coincide; any node absent from the field maps to φ=0. When `phi_entity == nullptr`, output is **byte-identical** to today (no-physics path).

### 3.7 Frame / headless-capture hook (`apps/aleph_edit/main.cpp`)

- **Live:** call `controller.step(kDt)` once per `run_live` loop iteration, behind a toggle key (so it doesn't fight idle path-trace).
- **Headless (the deterministic artifact):**

```cpp
controller.enable_sim(true);
controller.kick(seed_node, 1.0);
for (int i = 0; i < N; ++i) { controller.step(kDt); dump(std::format("step{}_wave", i)); }
controller.apply(spawn_adjacent_mesh_op);                    // the topology-change money shot
for (int i = N; i < 2*N; ++i) { controller.step(kDt); dump(std::format("step{}_wave", i)); }
```

`visual_review.sh` already globs `$OUT/*.ppm`→PNG and montages by step; naming frames `stepN_wave_raster.ppm` flows them into the contact sheet (extend its `STEPS`).

---

## 4. Data flow & determinism

**Per headless frame:** `step(kDt)` → `lap = operator_.matrix.matvec(field_.phi)` → `φ̇,φ` update → `rebuild_backends_from_prev()` projects φ→per-entity, colormaps to `vcol` → `dump()` writes the PPM.

**Determinism (SPEC §7):** f64 throughout; `DMatrix::matvec` is sequential row-major, fixed summation order, no threads/SIMD reductions — bit-reproducible; the two stepper loops are sequential f64. φ is ordered by `node_order` (stable sorted skeleton vertices); `handle_map` is insertion-ordered and stable across re-lower of the same graph. Fixed `kDt`, no wall-clock in headless. Colormap is pure f32 in `entities` order ⇒ byte-identical PPM.

**CFL:** explicit symplectic Euler on `φ̈=−c²Δφ` is stable for `c²·dt²·λ_max(Δ) < 4`. We avoid a per-frame eigensolve by bounding `λ_max(Δ)` with the **Gershgorin radius** `g = max_i Σ_j |Δ_ij| = max_i 2·Δ_ii`; choose `dt ≤ 0.9·2/(c·√g)`. Defaults `c=1`, `damping=0.999`, `dt` derived from `g` at sim-enable (logged). `jacobi_eigh` (`flow.cppm:167`) is available for the exact `λ_max` if needed.

---

## 5. Edit integration

On a DPO/Op edit, in order: (1) **operator rebuild** `operator_ = build_laplacian(graph_, default_weight)` — the honest correct path: the bare `WeightedLaplacian` is always rebuilt; the rank-k incremental path exists only on `IncrementalLaplacian` and only for the dense node-set-stable case, whereas the demo's edit *adds* a node (node set changes ⇒ full refactor even there). (2) **re-project** `field_.reproject(operator_.node_order)` — survivors carry `(phi,phi_dot)`, new nodes 0; this layer does **not** exist in the Rust reference (it re-zeros) and is exactly what makes the ripple persist-then-re-route across the edit. (3) **re-bake** `rebuild_backends_from_prev()`.

**Mayer-Vietoris localization is explicitly a FOLLOW-UP, not in this slice.** "Recompute only the touched region" is not wired through the sheaf layer — the primitives (`decompose_rewrite`, `mayer_vietoris_certify_with`, `compute_subgraph_h0`, zigzag) exist but are not composed into the live edit path; MV recomputes H⁰(G') wholesale per call. Clean upgrade hook (noted, not built): `RewriteRecord` + the `(U,K,R)` decomposition are the ready inputs; `reproject` is the single function a localized version would replace; `IncrementalLaplacian` rank-k is the operator-side analogue.

---

## 6. Error handling (no exceptions; `aleph_flags_isa`: `std::expected`/`optional`/`abort`)

| Failure | Detection | Report (no throw) |
|---|---|---|
| Empty graph/field (`node_order.size()==0`) | `field.size()==0` | `StepError::EmptyField`; controller no-ops (matches Rust `if n==0 return`). |
| Dim mismatch (φ len ≠ Δ rows) | `phi.size()!=delta.rows()` | `StepError::DimMismatch`; release may `assert`/`abort` (wiring-bug contract). |
| CFL violation | `cfl_ok` (Gershgorin) | `StepError::CflViolation` if guard on; else caught by NonFinite. No silent dt clamp (dt is a deterministic input). |
| Numerical blow-up / NaN | scan φ for non-finite | `StepError::NonFinite`; controller disables sim, keeps last good frame. |
| Singular Δ | — | **Not a failure:** stepping is matvec-only; the all-ones kernel never causes a solve failure (we deliberately avoid `LDLT::solve`). |

All sim entry points return `std::expected<void,StepError>` or `bool`; nothing throws. `abort` reserved for bug-class contract violations.

---

## 7. Testing (`tests/sim/`, mirroring `tests/flow/`)

1. **Operator sanity.** Path graph A–B–C: assert `operator_.is_symmetric(1e-12)`, `ones_in_kernel(1e-12)`, and `node_order == sorted vertices`.
2. **1-D wave vs analytic.** On a path/ring graph, init φ to a Laplacian eigenmode `v` (from `jacobi_eigh(operator_.matrix)`); the continuous solution is `φ(t)=cos(c√λ·t)·v`. Step N and assert match within the `O(dt²)` truncation bound. Cross-check the ring closed-form spectrum `λ_k=2(1−cos(2πk/n))`.
3. **Energy bound.** `E = ½φ̇ᵀφ̇ + ½c²·φᵀΔφ`. `damping=1` ⇒ bounded drift `|E(t)−E(0)| ≤ ε·N·dt²` (no secular growth); `damping<1` ⇒ monotone non-increasing.
4. **Determinism (the gate).** Same `(graph,kick,N,dt)` twice ⇒ byte-identical φ, φ̇; rebuilt operator from same graph ⇒ byte-identical `node_order`, `matrix`.
5. **Edit re-projection.** Δ on A–B, kick A, step; add node C adjacent to B via an Op; assert `φ(A),φ(B)` unchanged and `φ(C)==0`.
6. **Visual artifact (deliverable).** `visual_review.sh` contact sheet of `step0..2N_wave_raster.ppm` (committed PNG).

Run the existing ASan+UBSan gate over `tests/sim`.

---

## 8. Build order (each step compiles + tests green)

1. `aleph.sim:field` — `ScalarField` + `zeros`/`kick`/`reproject` + unit tests (sizes, in/out-of-range kick, reproject survivor/new/deleted).
2. `aleph.sim:wave` — `WaveStepper::step` (via `DMatrix::matvec`) + `cfl_ok` + oracles #1–#4.
3. Colormap + `build_sw` thread — `colormap_diverging` + optional `phi_entity` through `build_sw_scene→emit_entity→push_tri`; golden test: null ⇒ byte-identical, non-null ⇒ vcol == colormap.
4. `EditorController` wiring — members, `enable_sim`, `kick`, `rebuild_operator_and_reproject`, per-entity φ projection in `rebuild_backends_from_prev`, `step(dt)`; tests: trajectory hash + oracle #5 through `apply(Op)`.
5. Lattice demo scene + `apps/aleph_edit/main.cpp` headless capture — fixed-`dt` `step`+`dump` loop, one edit, more frames.
6. `tools/visual_review.sh` — extend `STEPS` to the `2N` wave frames; generate the contact sheet (visual deliverable).

Steps 1–2 are pure `aleph.sim`, gated by unit oracles before any renderer/edit code is touched.

---

## 9. Scope boundary (YAGNI) — out, with the hook kept clean

- **Implicit/unconditionally-stable solvers** — matvec-only; hook: `IncrementalLaplacian::solve` already exists for a future implicit stepper.
- **Mayer-Vietoris localized re-projection** — §5; hook: `RewriteRecord` + `decompose_rewrite` + `compute_subgraph_h0`; `reproject` is the swap point.
- **General `Section`/multi-field** — one scalar per node; hook: `ScalarField` is concrete, a `Section<T>` can wrap `vector<ScalarField>`; colormap takes one `const vector<double>*`.
- **Collisions/contact/rigid bodies** — none; a different consumer of the graph, not of `aleph.sim`.
- **Per-triangle / Hodge (cochain) Laplacian** — we use the 1-skeleton Δ (one DOF per Mesh node); hook: the sheaf δ⁰/δ¹ coboundary operators exist for a future cochain-field slice reusing the `ScalarField`/stepper shape over a different index domain.

---

## 10. New APIs introduced (everything else reused as-is)

1. `aleph.sim` module + `ScalarField` + `WaveStepper` (Δ application reuses `DMatrix::matvec`).
2. `ScalarField::reproject` (no Rust analogue).
3. `colormap_diverging` + optional `phi_entity` param on `build_sw_scene/emit_entity/push_tri` (`Face::vcol` already exists; default-null = byte-unchanged).
4. `EditorController::step/enable_sim/kick` + members `operator_/field_/stepper_` + `rebuild_operator_and_reproject` (reuse `build_laplacian`, `rebuild_backends_from_prev`).
5. Lattice demo scene builder + headless wave loop + `visual_review.sh STEPS` extension.

Everything driving the *physics* (the Laplacian, assembly, matvec, eigensolver, factorization) is **reused as-is** from `aleph.flow`/`aleph.linalg.sparse`. The slice adds state + a stepper + wiring, and reinvents neither the Laplacian nor the solver.

**Files to touch:** new `graph/src/aleph.sim/aleph.sim-field.cppm`, `aleph.sim-wave.cppm`, `aleph.sim.cppm` (primary module interface), `graph/src/aleph.sim/CMakeLists.txt`; `bridge/src/aleph.lowering/aleph.lowering-build_sw.cppm`; `bridge/src/aleph.edit/aleph.edit-controller.cppm`; `apps/aleph_edit/main.cpp`; `tools/visual_review.sh`; new `tests/sim/` + `tests/CMakeLists.txt`. Reused unchanged: `graph/src/aleph.flow/aleph.flow-laplacian.cppm`, `foundation/src/aleph.linalg.sparse/aleph.linalg.sparse-dense.cppm`.

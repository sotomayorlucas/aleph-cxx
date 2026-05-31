# Phase 5 — Graph→Render Lowering (`aleph.lowering`)

**Status:** design for review (rev 2 — incorporates review feedback).
**Date:** 2026-05-31.
**Predecessor:** Phases 1–4 complete on `main`.

## 1. Goal & thesis

The **typed scene graph is the single source of truth**; a renderable scene is its image under a **lowering functor**:

```
GraphScene  ──Lower──▶  LoweredScene  ──build──▶  RenderScene  ──▶  Image
   (truth)            (frozen SEMANTIC IR)        (SoA/BVH)
```

Editing is a **morphism, not a mutation**: an editor gesture → an `Op` → a typed graph mutation or a DPO transaction → re-lower. The renderer holds caches (BVH, buffers) but **no semantic authority**. One truth; the rest is derived. **No double state** — nothing downstream of `Lower` is edited as primary truth.

Out of scope (5.x): sheaf/flow state into render decisions; incremental lowering (dirty-sets); textures; volumes; GPU.

## 2. Architecture

### 2.1 Three levels
- **GraphScene** = `aleph.graph` `Graph` over **enriched** `aleph.types` nodes (§3). The truth.
- **LoweredScene** = a **frozen SEMANTIC IR** (new, in `aleph.lowering`): resolved entities with **world-space geometry payloads**, normalized material params, a light table, the camera, and the deterministic `NodeId → index` handle map. It contains **NO renderer internals** (no BVH, no SoA layout, no tiles) — those belong to `RenderScene`. Lowering tests assert on this IR, never on renderer internals.
- **RenderScene** = the existing `aleph.scene::Scene` (SoA + BVH).

### 2.2 `aleph.lowering` — the sanctioned cross-cutter
New top-level subtree `bridge/src/aleph.lowering/`. The **only** module allowed to link both worlds: `PUBLIC aleph_graph aleph_types aleph_scene aleph_dpo aleph_math`, `PRIVATE aleph_flags_isa`. `graph/` and `render/` stay mutually independent. **Excluded from the `iso_*` set** by design (the one legitimate bridge), with its own integration tests under `tests/lowering/`.

## 3. Part 1 — enrich `aleph.types` (graph gains concrete payloads)

`NodeKind`/`EdgeKind`/`allows()` **UNCHANGED → `tla_cxx_sync` stays green** (it tracks kinds/compat/invariant-names, not struct fields). All new fields default-initialized so existing construction compiles.

```cpp
import aleph.math;  // Vec3, Mat4

// LOWERABLE LOCAL geometry payload — explicitly NOT the final mesh model.
// v1 = analytic primitives; later grows to TriangleMeshRef / ObjMesh / Procedural / Sdf
// without breaking the contract. Local space; world transform applied at lowering.
struct SphereLocal { math::Vec3 center{}; math::f32 radius{1}; };
struct QuadLocal   { math::Vec3 q{}, u{}, v{}; };
struct TriLocal    { math::Vec3 a{}, b{}, c{}; };
using GeometryPayload = std::variant<SphereLocal, QuadLocal, TriLocal>;

// Local-transform abstraction. Storage is Mat4 in v1, but the graph depends on
// LocalTransform (not a raw matrix) so it can grow to TRS / GA rotor / dual-quat /
// constraints later without churning every consumer.
struct LocalTransform { math::Mat4 m{ math::Mat4::identity() }; };

struct Mesh {        // semantic entity + a lowerable geometry payload
    NodeId id{}; std::string geometry_ref; std::uint32_t tris_count{};
    GeometryPayload geometry{ SphereLocal{} };
};
struct Material {    // physical params; `emit` is a RENDERABLE property, not "this is a light"
    NodeId id{}; MaterialKind kind{MaterialKind::Lambertian};
    math::Vec3 albedo{0.8f,0.8f,0.8f}; math::f32 fuzz{0}; math::f32 ior{1.5f};
    math::Vec3 emit{0,0,0};
};
struct Light {       // an EXPLICIT sampling source in its own right (kept as a node)
    NodeId id{}; LightKind kind{LightKind::Point}; std::string emit_ref;
    math::Vec3 emission{1,1,1}; GeometryPayload geometry{ QuadLocal{} };
};
struct Camera {      // concrete pose
    NodeId id{}; std::string sensor_id;
    math::Vec3 look_from{0,0,0}, look_at{0,0,-1}, up{0,1,0};
    math::f32  vfov_deg{40}, aperture{0}, focus_dist{1};
};
struct Transform { NodeId id{}; std::uint32_t pose_slot{}; LocalTransform local{}; };
```

**Light/emission policy (decided):** a `Light` node is an explicit sampling source; `Material.emit > 0` makes an object physically emissive/visible. **The lowering** decides the RenderScene light table = all `Light` nodes ∪ emissive `Mesh`es whose `emit` luminance > 0. Neither "all lights are meshes" nor "all emission is a Light node."

## 4. Part 2 — `aleph.lowering`

```cpp
export module aleph.lowering;
export import :lowered;   // LoweredScene IR + LoweredEntity + MaterialParams (frozen, semantic)
export import :lower;     // lower(Graph) -> expected<LoweredScene, LowerError>
export import :build;     // build_render_scene(LoweredScene) -> aleph.scene::Scene
export import :ops;       // Op vocabulary + apply_op (return path)
```

### 4.1 `LoweredScene` (frozen semantic IR — no renderer internals)
```cpp
struct LoweredEntity { types::NodeId source; types::GeometryPayload world_geometry; MaterialParams material; };
struct LoweredCamera { math::Vec3 look_from, look_at, up; math::f32 vfov_deg, aperture, focus_dist; };
struct LoweredScene {
    std::vector<LoweredEntity>  entities;   // insertion-order stable
    std::vector<LoweredEntity>  lights;     // light table (see policy §3)
    LoweredCamera               camera;
    containers::OrderedMap<types::NodeId, std::uint32_t> handle_map;  // NodeId -> entities index
};
```
Serializable; byte-deterministic for a given graph.

### 4.2 `lower(const Graph&) -> std::expected<LoweredScene, LowerError>`
1. Roots = `Transform` nodes with no incoming `Contains`.
2. DFS `Contains` (insertion order) composing world transform (`world = parent.world * node.local.m`).
3. Each `Mesh`: transform `GeometryPayload` to world; resolve `Material` via `Mesh —References→ Material`. **A missing/dangling reference is a structured error (`DanglingReference`) — never a silent default.** Emit entity + `handle_map`.
4. Light table per the §3 policy.
5. Extract the unique `Camera` (invariant `CameraExclusive`) — none ⇒ `NoCamera`.
`LowerError { NoCamera, DanglingReference, InvalidHierarchy }`. Deterministic throughout.

### 4.3 `build_render_scene(const LoweredScene&) -> aleph.scene::Scene`
Thin translation only: per entity `scene_add_sphere/quad/tri` + material via `scene_add_lambertian/metal/dielectric/emissive`; set camera; `scene_build_bvh`. No decisions.

## 5. Part 3 — return path (edit → op → re-lower)

Two op families, both **all-or-nothing** (return a new valid state or fail with **no partial effects**):
- **Attribute ops** (typed, validated mutations): `SetTransform`, `SetMaterial`.
- **Structural ops** (transactional, via `aleph.dpo` or an equivalent transactional wrapper): `AddObject` (create a Mesh + Material + `References`/`Contains` edges), `AddLight`, `DeleteObject`, `ApplyRule`.

```cpp
using Op = std::variant<SetTransform, SetMaterial, AddObject, AddLight, DeleteObject, ApplyRule>;
std::expected<dpo::RewriteRecord, OpError> apply_op(Graph& g, const Op& op);  // then caller re-lowers
```
`AddObject` is in v1 so the edit loop is symmetric (the editor can add geometry, not only lights). The editor emits `Op`s; it never touches the `RenderScene`.

## 6. v1 scope
**IN:** `Transform/Mesh/Material/Light/Camera` + `Contains`/`References`; enriched payloads (§3); deterministic **full** lowering; `build_render_scene`; `Op → apply → re-lower` for `SetTransform`, `SetMaterial`, `AddObject`, `AddLight`, `DeleteObject`, one `ApplyRule`. **OUT (5.x):** sheaf/flow→render; incremental/dirty-sets; textures; volumes.

## 7. Determinism & authority
Same graph → byte-identical `LoweredScene` + `Scene` (insertion order, `OrderedMap`, f32). Golden snapshots. Renderer caches derived, never authoritative. **v1 = full lowering** (re-lower per edit); incremental is 5.x (avoid debugging cache coherence before the functor is proven).

## 8. Tests (`tests/lowering/`)
1. **lower_minimal:** `{root Transform → Camera + Mesh(SphereLocal, Material Lambertian red) + Light}` → 1 entity, 1 light, camera; `handle_map` stable; **LoweredScene snapshot byte-identical** across two `lower()` calls.
2. **transform_hierarchy:** nested `Transform`s compose a hand-computed world position.
3. **build_render_scene:** the lowered minimal scene → `Scene` with exactly 1 sphere + 1 lambertian + 1 light + camera.
4. **missing_reference_fails_cleanly:** a `Mesh` with no/dangling `References→Material` ⇒ `lower()` returns `LowerError::DanglingReference` (NO silent default).
5. **edit_material:** `apply_op(SetMaterial)` → re-lower → only that entity's `MaterialParams` changed; other handles byte-identical.
6. **add_object / add_light:** `apply_op(AddObject)` and `apply_op(AddLight)` → re-lower → entity/light count grows as expected; handles stable for survivors.
7. **dpo_edit (anti-dangling):** `apply_op(DeleteObject)` / `ApplyRule` → re-lower → valid `Scene`, **no dangling handles**, `validate_all` holds; a rolled-back op leaves graph + lowering unchanged.
8. **determinism:** lower→edit→lower yields a deterministic, consistent `handle_map`.
9. **end-to-end smoke:** `apps/aleph_lower_demo` builds the graph, lowers, renders a PPM (≥10 KB).

## 9. Acceptance criterion (the whole loop)
Phase 5 is accepted when: a minimal typed graph **lowers deterministically** to a `LoweredScene`, **builds** a `RenderScene`, **renders** a PPM; an editor **`Op` modifies the GRAPH** (not the renderer); **re-lower**; and **all handles/maps/invariants remain valid** — and **broken references fail with a structured error, not a silent default**.

## 10. Implementation order (small waves)
- **W1** — enriched payload types (§3) + empty `bridge/aleph.lowering` + CMake/module wiring + iso set updated (green stubs).
- **W2** — `:lowered` (IR) + `:lower` (GraphScene→LoweredScene: hierarchical transforms, materials, geometry payload) + tests 1,2,4.
- **W3** — `:build` (LoweredScene→RenderScene) + `aleph_lower_demo` end-to-end PPM + tests 3,9.
- **W4** — attribute ops `SetTransform`/`SetMaterial` + test 5.
- **W5** — structural ops `AddObject`/`AddLight`/`DeleteObject`/`ApplyRule` (DPO/transactional) + tests 6,7.
- **W6** — hardening: snapshot/golden tests, structured-error coverage, determinism (test 8), docs, drift re-check. Tag `v0.5.0-lowering`.

## 11. Future (5.x+)
Sheaf/flow as render metadata (H⁰ light-grouping, Ricci/heat sampling importance); incremental lowering (dirty-sets keyed off `handle_map` + `RewriteRecord`); textures; volumes; GPU.

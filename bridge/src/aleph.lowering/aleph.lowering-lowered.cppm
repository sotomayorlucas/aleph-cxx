// Part 2 — `LoweredScene`: the frozen SEMANTIC IR. See SPEC §4.1.
//
// This is the image of a GraphScene under the lowering functor: resolved
// entities carrying WORLD-space geometry payloads, normalized material params,
// a light table, the camera pose, and the deterministic `NodeId -> index`
// handle map. It is the contract `lower()` produces and `build_render_scene()`
// consumes.
//
// HARD BOUNDARY — this IR holds **NO renderer internals**: no BVH, no SoA
// layout, no tiles, no `aleph.scene` handles. Those belong to `RenderScene`
// (`aleph.scene::Scene`). Lowering tests assert on THIS IR, never on renderer
// internals (SPEC §2.1). The only things imported here are pure data vocab:
// `aleph.types` (NodeId / GeometryPayload / MaterialKind), `aleph.math`
// (Vec3 / f32) and `aleph.containers` (OrderedMap).
//
// Determinism (SPEC §7): same graph -> byte-identical `LoweredScene`. That is
// why entities/lights are insertion-order `std::vector`s, the handle map is an
// insertion-ordered `OrderedMap`, and every scalar is `f32` (no `double` creep,
// no hash-order iteration). `OrderedMap` is move-only, so `LoweredScene` is a
// move-only value type; copy is deleted to make the "single source of truth,
// no double state" intent explicit (SPEC §1) — a LoweredScene is produced by
// `lower()` and threaded by move, never duplicated.

module;
#include <cstdint>
#include <vector>

export module aleph.lowering:lowered;

import aleph.types;       // NodeId, GeometryPayload, MaterialKind
import aleph.math;        // Vec3, f32
import aleph.containers;  // OrderedMap (move-only, insertion-ordered)

export namespace aleph::lowering {

// Normalized, renderer-independent material parameters — the frozen semantic
// view of a `types::Material`, with the graph `NodeId` dropped (identity lives
// on the owning `LoweredEntity::source`). Carries exactly the params the four
// material families need so `:build` can dispatch on `kind` and forward to
// `scene_add_lambertian/metal/dielectric/emissive` with NO further decisions:
//
//   Lambertian -> albedo
//   Metal      -> albedo, fuzz
//   Dielectric -> ior
//   Emissive   -> emit
//
// `emit` is retained for every kind because emission is a RENDERABLE property,
// not "this is a light" (SPEC §3): the light-table policy keys off `emit`
// luminance regardless of `kind`.
struct MaterialParams {
    types::MaterialKind kind{types::MaterialKind::Lambertian};
    math::Vec3          albedo{0.8f, 0.8f, 0.8f};
    math::f32           fuzz{0.0f};
    math::f32           ior{1.5f};
    math::Vec3          emit{0.0f, 0.0f, 0.0f};
};

// Rec. 709 relative luminance of an emission radiance. Used by the light-table
// policy (SPEC §3): a Mesh joins the light table iff its material's emission
// luminance is > 0. Pure, f32, deterministic.
[[nodiscard]] constexpr math::f32 luminance(math::Vec3 c) noexcept {
    return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
}

// True iff this material physically emits light (any positive emission
// luminance). This — not `kind == Emissive` — is the predicate the light table
// is built from, so an emissive-albedo Lambertian still counts and a
// zero-emit Emissive (degenerate) does not.
[[nodiscard]] constexpr bool is_emissive(const MaterialParams& m) noexcept {
    return luminance(m.emit) > 0.0f;
}

// A resolved, world-space scene entity. Exactly the SPEC §4.1 shape:
//   * `source`         — the originating graph node (a Mesh NodeId); the stable
//                        identity used as the `handle_map` key.
//   * `world_geometry` — the entity's `GeometryPayload` already transformed out
//                        of local space into world space at lowering time.
//   * `material`       — the normalized `MaterialParams` resolved via the
//                        Mesh —References-> Material edge.
struct LoweredEntity {
    types::NodeId          source{};
    types::GeometryPayload world_geometry{types::SphereLocal{}};
    MaterialParams         material{};
};

// The camera pose, flattened from a `types::Camera`. No graph id and no sensor
// reference: the IR is the pose the renderer consumes (SPEC §4.1).
struct LoweredCamera {
    math::Vec3 look_from{0.0f, 0.0f, 0.0f};
    math::Vec3 look_at{0.0f, 0.0f, -1.0f};
    math::Vec3 up{0.0f, 1.0f, 0.0f};
    math::f32  vfov_deg{40.0f};
    math::f32  aperture{0.0f};
    math::f32  focus_dist{1.0f};
};

// The frozen semantic IR (SPEC §4.1). Serializable; byte-deterministic for a
// given graph. Move-only because `handle_map` (an `OrderedMap`) is move-only —
// and deliberately so: this is the one source of truth downstream of `lower()`,
// threaded by move, never copied into a second authoritative state.
struct LoweredScene {
    std::vector<LoweredEntity> entities;  // insertion-order stable
    std::vector<LoweredEntity> lights;    // light table: Light nodes UNION
                                          // emissive Meshes (SPEC §3 policy)
    LoweredCamera              camera{};
    // NodeId -> index into `entities`. Insertion-ordered so the mapping is
    // stable across re-lowering of the same graph (SPEC §7 determinism).
    containers::OrderedMap<types::NodeId, std::uint32_t> handle_map{};

    // Light grouping by VisibilitySheaf H⁰ components (SPEC §4.1). Default
    // empty; populated by the lowering. Empty => every light its own implicit
    // group downstream (degenerate-but-valid).
    std::vector<std::vector<types::NodeId>> light_groups;

    // Per-entity importance (SPEC §4.1, Phase 5.x-b), aligned to `entities`.
    // Default empty ⇒ uniform sampling downstream. Populated by `lower()` /
    // `lower_incremental()` from `aleph.flow` Ricci curvature.
    std::vector<double> importance;

    LoweredScene() = default;

    LoweredScene(const LoweredScene&)            = delete;
    LoweredScene& operator=(const LoweredScene&) = delete;
    LoweredScene(LoweredScene&&) noexcept            = default;
    LoweredScene& operator=(LoweredScene&&) noexcept = default;
};

}  // namespace aleph::lowering

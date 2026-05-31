// aleph.lowering:build — the second arrow of the pipeline (SPEC §4.3):
//
//   GraphScene ──Lower──▶ LoweredScene ──build──▶ RenderScene ──▶ Image
//                          (frozen IR)            (SoA/BVH)
//
// `build_render_scene` is a THIN translation: it walks the already-resolved,
// world-space `LoweredScene` and replays it into an `aleph::scene::Scene`
// (the renderer's SoA + BVH form) with NO decisions of its own. Every choice
// — which entities exist, their world geometry, their material params, the
// light table, the camera pose — was made by `lower()` and frozen into the IR.
// Here we only DISPATCH:
//
//   * each entity's `world_geometry` variant  -> scene_add_sphere/quad/tri
//   * each material's `kind`                   -> scene_add_lambertian /
//                                                 scene_add_metal /
//                                                 scene_add_dielectric /
//                                                 scene_add_emissive
//
// then build the acceleration structure (`scene_build_bvh`). The IR -> Scene
// map is total and order-preserving: entities are emitted in `entities` order
// and standalone light-table entries (Light nodes, which are NOT in `entities`)
// are emitted after, in `lights` order. Same `LoweredScene` ⇒ byte-identical
// `Scene` (SPEC §7 determinism) because we never reorder and the SoA appends
// are insertion-ordered.
//
// HARD BOUNDARY (SPEC §2.1): this is the ONE direction that touches renderer
// internals. `lower()` and the `LoweredScene` IR never see a BVH or a SoA; all
// of that is materialized HERE and only HERE.
//
// Light table (SPEC §3 policy): `lower()` populated `LoweredScene::lights` with
// the union of (a) emissive Meshes — which are ALSO in `entities` — and (b)
// standalone `Light` nodes — which are NOT. `scene_add_*` already auto-registers
// any primitive added with an Emissive material into `Scene::lights`, so the
// emissive Meshes self-register when their entity is added. We therefore only
// replay the standalone Light entries (those whose `source` is not in the
// handle map) so the renderer's light set equals the IR light table with no
// double-counted geometry. This is a translation rule, not a decision.
//
// Camera: `aleph::scene::Scene` has no camera slot — the render-side camera
// (`aleph::render::common::Camera`) is a projection built from a pose PLUS the
// image dimensions, which are a renderer concern not present here. The pose is
// already frozen in `LoweredScene::camera` (a `LoweredCamera`), so "set camera"
// at this layer is structurally a pass-through: the consumer builds its camera
// from `LoweredScene::camera` alongside this `Scene`. `build_render_scene` thus
// returns the geometry/material/BVH `Scene`; the camera flows via the IR.
//
// No exceptions (aleph_flags_isa): the function is total over a valid IR (any
// fallible resolution already happened in `lower()` and surfaced as a
// `LowerError`), so it returns the `Scene` by value rather than `std::expected`.

module;
#include <array>
#include <cstddef>
#include <type_traits>
#include <variant>

export module aleph.lowering:build;

import aleph.scene;   // Scene, scene_add_sphere/quad/tri, scene_add_lambertian/
                      // metal/dielectric/emissive, scene_build_bvh, MaterialHandle
import aleph.types;   // GeometryPayload (SphereLocal/QuadLocal/TriLocal), MaterialKind
import aleph.math;    // Vec3, f32
import aleph.alloc;   // Arena (scratch for scene_build_bvh)
import :lowered;      // LoweredScene, LoweredEntity, MaterialParams, LoweredCamera

namespace aleph::lowering::detail {

// Translate a frozen `MaterialParams` into a renderer material, dispatching on
// `kind` 1:1 onto the four `scene_add_*` material families (SPEC §4.3). No
// decision: `kind` was fixed by `lower()`; we forward exactly the params each
// family consumes (Lambertian: albedo; Metal: albedo+fuzz; Dielectric: ior;
// Emissive: emit).
[[nodiscard]] inline aleph::scene::MaterialHandle
add_material(aleph::scene::Scene& s, const MaterialParams& m) {
    switch (m.kind) {
        case aleph::types::MaterialKind::Lambertian:
            return aleph::scene::scene_add_lambertian(s, m.albedo);
        case aleph::types::MaterialKind::Metal:
            return aleph::scene::scene_add_metal(s, m.albedo, m.fuzz);
        case aleph::types::MaterialKind::Dielectric:
            return aleph::scene::scene_add_dielectric(s, m.ior);
        case aleph::types::MaterialKind::Emissive:
            return aleph::scene::scene_add_emissive(s, m.emit);
    }
    // Unreachable for a valid IR; keep total and deterministic.
    return aleph::scene::scene_add_lambertian(s, m.albedo);
}

// Translate one resolved, world-space entity into the renderer scene: add its
// material, then dispatch its `GeometryPayload` variant onto the matching
// `scene_add_sphere/quad/tri`. Pure translation.
inline void add_entity(aleph::scene::Scene& s, const LoweredEntity& e) {
    const aleph::scene::MaterialHandle mat = add_material(s, e.material);
    std::visit(
        [&](const auto& g) {
            using G = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<G, aleph::types::SphereLocal>) {
                (void)aleph::scene::scene_add_sphere(s, g.center, g.radius, mat);
            } else if constexpr (std::is_same_v<G, aleph::types::QuadLocal>) {
                (void)aleph::scene::scene_add_quad(s, g.q, g.u, g.v, mat);
            } else {  // aleph::types::TriLocal
                (void)aleph::scene::scene_add_tri(s, g.a, g.b, g.c, mat);
            }
        },
        e.world_geometry);
}

}  // namespace aleph::lowering::detail

export namespace aleph::lowering {

// The product of `:build`: the renderer's geometry/material/BVH `Scene` bundled
// with the camera pose it must be viewed through. `aleph::scene::Scene` has no
// camera slot (SPEC §4.3 / §2.1) — the render-side projection is built from a
// pose PLUS image dimensions, which are a renderer concern absent here. The pose
// is frozen in `LoweredScene::camera`, so `build_render_scene` forwards it
// verbatim alongside the `Scene`. This `RenderScene` is the end of the lowering
// pipeline arrow (GraphScene -> LoweredScene -> RenderScene); the consumer turns
// `camera` into a concrete projection with its chosen image size.
struct RenderScene {
    aleph::scene::Scene scene;
    LoweredCamera       camera{};
};

// build_render_scene(const LoweredScene&) -> RenderScene (SPEC §4.3).
// Thin: replay entities + standalone lights into the SoA, build the BVH, and
// forward the camera pose. No logic; the IR already decided everything.
[[nodiscard]] inline RenderScene
build_render_scene(const LoweredScene& ls) {
    aleph::scene::Scene scene{};

    // Entities, in IR insertion order. Emissive Meshes self-register into the
    // renderer's light set via scene_add_* (Emissive material), matching the
    // §3 policy without an explicit re-add.
    for (const LoweredEntity& e : ls.entities) {
        detail::add_entity(scene, e);
    }

    // Standalone light-table entries: a `Light` node is NOT in `entities`
    // (it has no `handle_map` slot), so replay it here so the renderer's light
    // set equals the IR light table. Emissive Meshes ARE in `entities` and were
    // already added above, so skipping handle-mapped sources avoids duplicating
    // their geometry (SPEC §3 light-table policy).
    for (const LoweredEntity& l : ls.lights) {
        if (ls.handle_map.get(l.source) != nullptr) continue;  // emissive Mesh: already added
        detail::add_entity(scene, l);
    }

    // Build the acceleration structure. `scene_build_bvh` takes a scratch arena
    // it does not actually consume in v1; provide a trivial local one so the
    // function stays self-contained and matches the SPEC §4.3 signature
    // (input is only the LoweredScene).
    std::array<std::byte, 64> scratch_buf{};
    aleph::alloc::Arena scratch{scratch_buf.data(), scratch_buf.size()};
    aleph::scene::scene_build_bvh(scene, scratch);

    // "Set camera": the camera pose lives in `ls.camera` (a `LoweredCamera`) and
    // must be consumed alongside this Scene by the renderer, which needs image
    // dimensions to build its projection. `aleph::scene::Scene` has no camera
    // slot, so we forward the frozen pose verbatim in the `RenderScene` bundle.
    return RenderScene{std::move(scene), ls.camera};
}

}  // namespace aleph::lowering

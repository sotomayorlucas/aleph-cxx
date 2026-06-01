// aleph.lowering:build_sw — lowered -> software-rasterizer SceneRT (SPEC §3.1).
//
// `build_sw_scene(const LoweredScene&) -> SwBuild` emits `render::sw` faces per
// lowered primitive and records, per face, the originating graph `NodeId` so the
// (headless) editor can resolve a raster pick back to the entity it edits.
//
// FACE GRANULARITY (SPEC §3.1 / §5.1): the SPEC counts *triangles* — QuadLocal ->
// 2 tris, TriLocal -> 1 tri, SphereLocal -> rings*sectors*2 tris — and the bridge
// emits exactly one `render::sw::Face` per triangle. A `render::sw::Face` is a
// 4-vertex quad rasterized as two triangles {0,1,2} and {0,2,3}; we carry a single
// triangle in it as a *degenerate quad* (verts[3] == verts[2]), so the {0,2,3}
// split collapses to zero area and only {0,1,2} rasterizes. This keeps the
// face count equal to the SPEC triangle count and keeps `face_source` 1:1 with
// `scene.faces`:
//
//   QuadLocal   -> 2 Faces (the quad's two triangles: {p0,p1,p2} and {p0,p2,p3})
//   TriLocal    -> 1 Face  (the triangle {a,b,c})
//   SphereLocal -> a deterministic low-res UV sphere: kSphereRings latitude bands
//                  x kSphereSectors longitude divisions; each grid cell is one
//                  quad split into 2 triangle Faces => kSphereRings*kSphereSectors*2
//                  Faces (cap cells collapse a quad edge onto the pole, which only
//                  makes one of the two cell triangles degenerate; the per-sphere
//                  Face count stays kSphereRings*kSphereSectors*2 regardless).
//
// Lives beside `build_render_scene` (the bridge owns the lowered->render arrow).
// `aleph_lowering` additionally links `aleph_render_sw`; `render.sw`/`render.rt`
// still never import graph (this file is the one place the two meet).
//
// COLOR + SHADING: a `render::sw::Face` carries a flat `albedo` tint that the
// rasterizer modulates onto the per-pixel `TexSampleFn(u,v)` sample (see
// :rast_scan). We paint each face by baking a cheap FLAT LAMBERT shade —
// ambient + Σ_lights albedo·emit·max(0,N·L)·atten + self-emission (see
// `shade_face`) — into `Face::albedo`, paired with a constant-white texture
// `tex_white` so the rendered colour is exactly `white * lit == lit` at every
// covered pixel. Lights come from `LoweredScene::lights`. This makes the fast
// preview read as the same scene as the path tracer (a lit floor, a shaded
// sphere) without shadows/GI (the tracer's job). (An earlier scheme packed the
// albedo into the face UVs as values up to 65535 and decoded per pixel; the
// rasterizer's perspective interpolation of those large UVs lost precision on
// sliver/grazing triangles and flipped high bits -> stray wrong colours. A flat
// colour field is interpolation-proof and trivially deterministic.)
//
// DETERMINISM (SPEC §7): faces are emitted in `entities` order; the sphere
// tessellation walks rings/sectors in fixed order with fixed counts and pure f32
// trig; `face_source` is filled in lockstep with `faces`. Same LoweredScene =>
// byte-identical SwBuild. No exceptions (aleph_flags_isa): the function is total
// over a valid IR and returns by value.

module;
#include <array>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <variant>
#include <vector>

export module aleph.lowering:build_sw;

import aleph.render.sw;  // SceneRT, Face, TexSampleFn
import aleph.types;      // NodeId, GeometryPayload (SphereLocal/QuadLocal/TriLocal)
import aleph.math;       // Vec2, Vec3, f32, u32, u8
import :lowered;         // LoweredScene, LoweredEntity, MaterialParams

export namespace aleph::lowering {

// Result of rasterizing a LoweredScene into the software backend's SceneRT:
// the scene plus a parallel `face_source` map giving, per emitted face, the
// originating graph NodeId (the pick target). `face_source[i]` is the source of
// `scene.faces[i]` — the two vectors are always the same length and aligned 1:1.
struct SwBuild {
    aleph::render::sw::SceneRT          scene;
    std::vector<aleph::types::NodeId>   face_source;
};

// Fixed, deterministic UV-sphere resolution (SPEC §3.1: "low-res, fixed
// rings/sectors for determinism"). Exported so tests can pin the per-sphere face
// count to `kSphereRings * kSphereSectors * 2` against the SPEC formula rather
// than a literal. Kept small for a fluid raster view; bump only with care.
inline constexpr int kSphereRings   = 12;  // latitude bands  (theta: 0..pi)
inline constexpr int kSphereSectors = 16;  // longitude slices (phi:   0..2pi)

namespace detail {

// Internal aliases (the names used throughout the emitters below).
inline constexpr int SPHERE_RINGS   = kSphereRings;
inline constexpr int SPHERE_SECTORS = kSphereSectors;

// --- flat colour ---------------------------------------------------------
//
// COLOUR is carried by `Face::albedo` (a flat per-face tint the rasterizer
// modulates onto the sampled texel). We pair it with a constant-white texture
// `tex_white`, so the rendered colour is exactly `white * albedo == albedo`.
// (The earlier scheme packed the albedo into the face UVs as values up to
// 65535 and decoded them per pixel; the rasterizer's perspective interpolation
// of those large UVs lost enough precision on grazing/sliver triangles to flip
// the high bits -> wrong colour. A flat colour field is interpolation-proof.)

// Captureless white texture: every texel is opaque white (0xFFFFFFFF).
[[nodiscard]] inline aleph::math::u32
tex_white(aleph::math::f32 /*u*/, aleph::math::f32 /*v*/) noexcept {
    return 0xFFFFFFFFu;
}

// --- flat preview shading -------------------------------------------------
//
// The raster view is a FAST PREVIEW, not a physical render — the path tracer
// (idle view) is the ground truth. To make the preview recognizable as the
// same scene (a lit floor, a shaded sphere with a real terminator) we BAKE a
// cheap flat Lambert term into each face's `albedo`: ambient + Σ_lights
// albedo·emit·max(0,N·L)·atten + self-emission. Lights are the LoweredScene's
// `lights` (Light nodes ∪ emissive meshes); a light's position is its geometry
// centre and its radiance is `material.emit`. Pure f32 iterated over lights in
// order => deterministic (SPEC §7). No shadows / GI / bounces — that is the
// path tracer's job; here we only need the scene to read as lit while moving.

// Ambient floor so faces facing away from every light read dim, not pure black
// (stands in for the sky/indirect light the path tracer integrates).
inline constexpr aleph::math::f32 kAmbient = 0.20f;

// `material.emit` is an AREA-LIGHT radiance tuned for the path tracer (which
// integrates it over solid angle + bounces). Driving a flat point-light model
// with it directly over-brightens, so scale it to a sane preview intensity.
inline constexpr aleph::math::f32 kLightScale = 0.28f;

// Smooth inverse-square-ish falloff `1/(1 + kFall·dist²)`: behaves like 1/dist²
// far away but is bounded near a light (never blows up) and, with a small kFall,
// stays gentle across the editor's ~5-unit scene so a far floor is not crushed
// black relative to a near sphere.
inline constexpr aleph::math::f32 kFall = 0.08f;

// Centre of a light's geometry — its effective point-light position for the
// preview. Quad centre = q + (u+v)/2; tri centre = centroid; sphere = centre.
[[nodiscard]] inline aleph::math::Vec3
light_center(const aleph::types::GeometryPayload& g) noexcept {
    return std::visit(
        [](const auto& x) noexcept -> aleph::math::Vec3 {
            using G = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<G, aleph::types::SphereLocal>) {
                return x.center;
            } else if constexpr (std::is_same_v<G, aleph::types::QuadLocal>) {
                return x.q + (x.u + x.v) * 0.5f;
            } else {  // aleph::types::TriLocal
                return (x.a + x.b + x.c) * (1.0f / 3.0f);
            }
        },
        g);
}

// Flat Lambert shade of one triangle face at `centroid` with face `normal`
// (need not be unit). `two_sided` (quads/tris, whose winding-derived normal
// sign is arbitrary) shades by |N·L| so a floor lit from either side reads as
// lit; spheres pass their exact outward normal with two_sided=false for a
// proper light/dark terminator. `self_emit` makes emissive surfaces glow in
// the preview. Returns linear HDR (the Film's gamma tonemap applies, same as
// the path-trace path); only negatives are excluded.
[[nodiscard]] inline aleph::math::Vec3
shade_face(aleph::math::Vec3 centroid, aleph::math::Vec3 normal,
           aleph::math::Vec3 base_albedo, aleph::math::Vec3 self_emit,
           const std::vector<LoweredEntity>& lights, bool two_sided) noexcept {
    const aleph::math::f32 nlen = aleph::math::length(normal);
    const aleph::math::Vec3 N = (nlen > 1e-8f)
        ? normal * (1.0f / nlen)
        : aleph::math::Vec3{0.0f, 1.0f, 0.0f};

    aleph::math::Vec3 lit = base_albedo * kAmbient + self_emit;
    for (const LoweredEntity& L : lights) {
        const aleph::math::Vec3 d = light_center(L.world_geometry) - centroid;
        const aleph::math::f32 dist_sq = aleph::math::dot(d, d);
        if (dist_sq < 1e-6f) continue;
        const aleph::math::f32 ndl0 = aleph::math::dot(N, d) / std::sqrt(dist_sq);
        const aleph::math::f32 ndl = two_sided
            ? std::fabs(ndl0)
            : (ndl0 > 0.0f ? ndl0 : 0.0f);
        if (ndl <= 0.0f) continue;
        const aleph::math::f32 atten = 1.0f / (1.0f + kFall * dist_sq);
        lit = lit + aleph::math::hadamard(base_albedo, L.material.emit)
                        * (ndl * atten * kLightScale);
    }
    return lit;
}

// Append one *triangle* as a render.sw Face (a degenerate quad: verts[3] ==
// verts[2], so rast_scan's {0,2,3} split is zero-area and only the {0,1,2}
// triangle rasterizes). The face is flat-shaded with `albedo` (white texture
// modulated by the tint). `lightmap_id == 0xFFFFFFFF` => no lightmap (build_sw
// emits no placeholder Lightmaps; the raster preview is flat). `face_source`
// grows in lockstep with faces (1 Face == 1 triangle == 1 source).
inline void push_tri(SwBuild& out,
                     aleph::math::Vec3 a, aleph::math::Vec3 b, aleph::math::Vec3 c,
                     aleph::math::Vec3 albedo,
                     aleph::types::NodeId source) {
    aleph::render::sw::Face f{};
    f.verts = {a, b, c, c};
    f.uvs = {aleph::math::Vec2{0.0f, 0.0f}, aleph::math::Vec2{0.0f, 0.0f},
             aleph::math::Vec2{0.0f, 0.0f}, aleph::math::Vec2{0.0f, 0.0f}};
    f.tex = &tex_white;
    f.lightmap_id = 0xFFFFFFFFu;  // no lightmap
    f.albedo = albedo;
    out.scene.faces.push_back(f);
    out.face_source.push_back(source);
}

// QuadLocal -> 2 Faces. The render.rt quad is the parallelogram (q, q+u, q+u+v,
// q+v); we split those four corners into the two triangles {p0,p1,p2} and
// {p0,p2,p3} (matching rast_scan's quad winding) and emit one Face each.
inline void emit_quad(SwBuild& out, const aleph::types::QuadLocal& g,
                      aleph::math::Vec3 albedo, aleph::math::Vec3 emit,
                      const std::vector<LoweredEntity>& lights,
                      aleph::types::NodeId source) {
    const aleph::math::Vec3 p0 = g.q;
    const aleph::math::Vec3 p1 = g.q + g.u;
    const aleph::math::Vec3 p2 = g.q + g.u + g.v;
    const aleph::math::Vec3 p3 = g.q + g.v;
    // Both triangles are coplanar -> one shared (two-sided) normal.
    const aleph::math::Vec3 n = aleph::math::cross(p1 - p0, p2 - p0);
    const aleph::math::Vec3 c0 = (p0 + p1 + p2) * (1.0f / 3.0f);
    const aleph::math::Vec3 c1 = (p0 + p2 + p3) * (1.0f / 3.0f);
    push_tri(out, p0, p1, p2, shade_face(c0, n, albedo, emit, lights, true), source);
    push_tri(out, p0, p2, p3, shade_face(c1, n, albedo, emit, lights, true), source);
}

// TriLocal -> 1 Face (the single triangle {a,b,c}).
inline void emit_tri(SwBuild& out, const aleph::types::TriLocal& g,
                     aleph::math::Vec3 albedo, aleph::math::Vec3 emit,
                     const std::vector<LoweredEntity>& lights,
                     aleph::types::NodeId source) {
    const aleph::math::Vec3 n = aleph::math::cross(g.b - g.a, g.c - g.a);
    const aleph::math::Vec3 c = (g.a + g.b + g.c) * (1.0f / 3.0f);
    push_tri(out, g.a, g.b, g.c, shade_face(c, n, albedo, emit, lights, true), source);
}

// SphereLocal -> a deterministic UV sphere of RINGS x SECTORS quad cells, each
// split into 2 triangle Faces => RINGS*SECTORS*2 Faces.
// Standard parameterization:
//   theta = pi * ring / RINGS        (0 at +Y pole .. pi at -Y pole)
//   phi   = 2pi * sector / SECTORS
//   p(theta,phi) = center + radius * (sin theta cos phi, cos theta, sin theta sin phi)
// Each cell (ring r, sector s) has corners a=(r,s), b=(r+1,s), c=(r+1,s+1),
// d=(r,s+1); we emit the two triangles {a,b,c} and {a,c,d} (matching the quad
// winding used elsewhere). The top/bottom rings have a coincident pole edge, so
// one of a cell's two triangles is degenerate (zero area, rasterizes to nothing)
// — but it is still emitted as a Face, which keeps the per-sphere Face count
// exactly RINGS*SECTORS*2 regardless of caps. All math is pure f32.
inline void emit_sphere(SwBuild& out, const aleph::types::SphereLocal& g,
                        aleph::math::Vec3 albedo, aleph::math::Vec3 emit,
                        const std::vector<LoweredEntity>& lights,
                        aleph::types::NodeId source) {
    constexpr aleph::math::f32 kPi = 3.14159265358979323846f;
    auto on_sphere = [&](int ring, int sector) noexcept -> aleph::math::Vec3 {
        const aleph::math::f32 theta =
            kPi * static_cast<aleph::math::f32>(ring) /
            static_cast<aleph::math::f32>(SPHERE_RINGS);
        const aleph::math::f32 phi =
            2.0f * kPi * static_cast<aleph::math::f32>(sector) /
            static_cast<aleph::math::f32>(SPHERE_SECTORS);
        const aleph::math::f32 st = std::sin(theta);
        const aleph::math::f32 ct = std::cos(theta);
        const aleph::math::f32 sp = std::sin(phi);
        const aleph::math::f32 cp = std::cos(phi);
        return aleph::math::Vec3{
            g.center.x + g.radius * st * cp,
            g.center.y + g.radius * ct,
            g.center.z + g.radius * st * sp,
        };
    };
    for (int ring = 0; ring < SPHERE_RINGS; ++ring) {
        for (int sector = 0; sector < SPHERE_SECTORS; ++sector) {
            const aleph::math::Vec3 a = on_sphere(ring,     sector);
            const aleph::math::Vec3 b = on_sphere(ring + 1, sector);
            const aleph::math::Vec3 c = on_sphere(ring + 1, sector + 1);
            const aleph::math::Vec3 d = on_sphere(ring,     sector + 1);
            // Exact outward normals (centroid - centre) -> a real terminator.
            const aleph::math::Vec3 c0 = (a + b + c) * (1.0f / 3.0f);
            const aleph::math::Vec3 c1 = (a + c + d) * (1.0f / 3.0f);
            push_tri(out, a, b, c,
                     shade_face(c0, c0 - g.center, albedo, emit, lights, false), source);
            push_tri(out, a, c, d,
                     shade_face(c1, c1 - g.center, albedo, emit, lights, false), source);
        }
    }
}

// Dispatch one resolved, world-space entity onto its geometry emitter. Pure
// translation (no decisions): the variant was fixed by `lower()`. Each emitter
// bakes the entity's albedo + emission, lit by `lights`, into the face tints.
inline void emit_entity(SwBuild& out, const LoweredEntity& e,
                        const std::vector<LoweredEntity>& lights) {
    const aleph::math::Vec3 albedo = e.material.albedo;
    const aleph::math::Vec3 emit   = e.material.emit;
    std::visit(
        [&](const auto& g) {
            using G = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<G, aleph::types::SphereLocal>) {
                emit_sphere(out, g, albedo, emit, lights, e.source);
            } else if constexpr (std::is_same_v<G, aleph::types::QuadLocal>) {
                emit_quad(out, g, albedo, emit, lights, e.source);
            } else {  // aleph::types::TriLocal
                emit_tri(out, g, albedo, emit, lights, e.source);
            }
        },
        e.world_geometry);
}

}  // namespace detail

// build_sw_scene(const LoweredScene&) -> SwBuild (SPEC §3.1).
// Walk the lowered entities in IR order, emitting render.sw faces per primitive
// and a parallel `face_source` map (face index -> source graph NodeId). Standalone
// Light nodes (in `lights`, not in `entities`) are NOT rasterized: they carry no
// pickable surface in the raster view; the raster pass only draws scene geometry.
[[nodiscard]] inline SwBuild build_sw_scene(const LoweredScene& ls) {
    SwBuild out{};
    for (const LoweredEntity& e : ls.entities) {
        detail::emit_entity(out, e, ls.lights);
    }
    return out;
}

}  // namespace aleph::lowering

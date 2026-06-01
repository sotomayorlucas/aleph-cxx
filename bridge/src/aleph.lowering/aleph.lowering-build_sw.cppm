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
// COLOR: a `render::sw::Face` carries no color field — it samples a captureless
// `TexSampleFn(u,v)` per pixel (see :rast_scan). To paint a face with the
// entity's runtime `albedo` WITHOUT touching the `Face`/SceneRT layout, we encode
// the albedo into the face's (constant) UVs and decode it back in a single
// captureless sampler `tex_albedo`. All four UVs of a face are identical, so the
// rasterizer's perspective-correct interpolation yields exactly that (u,v) at
// every covered pixel (the gradients are zero) and `tex_albedo` returns a flat
// albedo color. The packing uses exact-integer floats (0..65535, all exactly
// representable as f32), so the round-trip is bit-exact and deterministic.
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

// --- albedo <-> UV codec -------------------------------------------------
//
// We carry the face's flat color through the (otherwise unused) UV channel.
// Quantize each linear albedo channel to 8 bits, pack ARGB into a u32, then
// split that u32 into two halves stored as exact-integer floats:
//   u = high 16 bits (0..65535), v = low 16 bits (0..65535).
// f32 represents every integer up to 2^24 exactly, so the pack/interpolate/
// unpack round-trip is bit-exact and order-independent (the rasterizer
// interpolates a constant -> zero gradient -> returns u,v verbatim).

[[nodiscard]] inline aleph::math::u8 quantize_channel(aleph::math::f32 c) noexcept {
    const aleph::math::f32 clamped = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
    // +0.5 round-to-nearest; deterministic for the clamped [0,1] range.
    return static_cast<aleph::math::u8>(clamped * 255.0f + 0.5f);
}

[[nodiscard]] inline aleph::math::u32 albedo_to_argb(aleph::math::Vec3 albedo) noexcept {
    const aleph::math::u32 r = quantize_channel(albedo.x);
    const aleph::math::u32 g = quantize_channel(albedo.y);
    const aleph::math::u32 b = quantize_channel(albedo.z);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

// The constant UV that encodes `albedo` for a whole face (all four verts share
// it). `argb_to_uv` is the inverse of `tex_albedo`'s decode.
[[nodiscard]] inline aleph::math::Vec2 albedo_to_uv(aleph::math::Vec3 albedo) noexcept {
    const aleph::math::u32 argb = albedo_to_argb(albedo);
    return aleph::math::Vec2{
        static_cast<aleph::math::f32>((argb >> 16) & 0xFFFFu),  // ARGB hi 16 bits
        static_cast<aleph::math::f32>( argb        & 0xFFFFu),  // ARGB lo 16 bits
    };
}

// Captureless sampler: reconstruct the packed ARGB from the (constant) UV.
// `+ 0.5f` then truncation makes the decode robust to any benign f32 wobble
// from the rasterizer's interpolation even though the gradient is zero.
[[nodiscard]] inline aleph::math::u32
tex_albedo(aleph::math::f32 u, aleph::math::f32 v) noexcept {
    const aleph::math::u32 hi = static_cast<aleph::math::u32>(u + 0.5f) & 0xFFFFu;
    const aleph::math::u32 lo = static_cast<aleph::math::u32>(v + 0.5f) & 0xFFFFu;
    return (hi << 16) | lo;
}

// Append one *triangle* as a render.sw Face (a degenerate quad: verts[3] ==
// verts[2], so rast_scan's {0,2,3} split is zero-area and only the {0,1,2}
// triangle rasterizes). All UVs = `uv` so the face is flat-shaded with the
// encoded albedo. Mirrors render.sw add_floor/add_cube: push a Face + a
// (placeholder) Lightmap and keep `lightmap_id` aligned to the lightmaps vector.
// `face_source` grows in lockstep with faces (1 Face == 1 triangle == 1 source).
inline void push_tri(SwBuild& out,
                     aleph::math::Vec3 a, aleph::math::Vec3 b, aleph::math::Vec3 c,
                     aleph::math::Vec2 uv,
                     aleph::types::NodeId source) {
    aleph::render::sw::Face f{};
    f.verts = {a, b, c, c};
    f.uvs = {uv, uv, uv, uv};
    f.tex = &tex_albedo;
    f.lightmap_id = static_cast<aleph::math::u32>(out.scene.lightmaps.size());
    out.scene.faces.push_back(f);
    out.scene.lightmaps.push_back(aleph::render::sw::Lightmap{});
    out.face_source.push_back(source);
}

// QuadLocal -> 2 Faces. The render.rt quad is the parallelogram (q, q+u, q+u+v,
// q+v); we split those four corners into the two triangles {p0,p1,p2} and
// {p0,p2,p3} (matching rast_scan's quad winding) and emit one Face each.
inline void emit_quad(SwBuild& out, const aleph::types::QuadLocal& g,
                      aleph::math::Vec2 uv, aleph::types::NodeId source) {
    const aleph::math::Vec3 p0 = g.q;
    const aleph::math::Vec3 p1 = g.q + g.u;
    const aleph::math::Vec3 p2 = g.q + g.u + g.v;
    const aleph::math::Vec3 p3 = g.q + g.v;
    push_tri(out, p0, p1, p2, uv, source);
    push_tri(out, p0, p2, p3, uv, source);
}

// TriLocal -> 1 Face (the single triangle {a,b,c}).
inline void emit_tri(SwBuild& out, const aleph::types::TriLocal& g,
                     aleph::math::Vec2 uv, aleph::types::NodeId source) {
    push_tri(out, g.a, g.b, g.c, uv, source);
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
                        aleph::math::Vec2 uv, aleph::types::NodeId source) {
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
            push_tri(out, a, b, c, uv, source);
            push_tri(out, a, c, d, uv, source);
        }
    }
}

// Dispatch one resolved, world-space entity onto its geometry emitter. Pure
// translation (no decisions): the variant was fixed by `lower()`. The face color
// is the entity material's albedo, encoded into the face UVs (see codec above).
inline void emit_entity(SwBuild& out, const LoweredEntity& e) {
    const aleph::math::Vec2 uv = albedo_to_uv(e.material.albedo);
    std::visit(
        [&](const auto& g) {
            using G = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<G, aleph::types::SphereLocal>) {
                emit_sphere(out, g, uv, e.source);
            } else if constexpr (std::is_same_v<G, aleph::types::QuadLocal>) {
                emit_quad(out, g, uv, e.source);
            } else {  // aleph::types::TriLocal
                emit_tri(out, g, uv, e.source);
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
        detail::emit_entity(out, e);
    }
    return out;
}

}  // namespace aleph::lowering

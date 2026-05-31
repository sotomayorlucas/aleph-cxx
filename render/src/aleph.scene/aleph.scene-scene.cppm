module;
#include <cstdint>
#include <vector>

export module aleph.scene:scene;

import aleph.math;
import aleph.io;
import :handle32;
import :sphere_soa;
import :quad_soa;
import :tri_soa;
import :material_soa;

export namespace aleph::scene {

// BvhNode lives here (not in :bvh) to avoid circular import:
// :bvh imports :scene for primitive_bbox, so BvhNode must be visible in :scene.
struct BvhNode {
    aleph::math::Aabb bbox;
    Handle32          left;
    Handle32          right;
};

struct BvhNodeArr {
    std::vector<BvhNode> nodes;
};

struct Scene {
    SphereSoA spheres;
    QuadSoA   quads;
    TriSoA    tris;
    LambertianSoA           lamb;
    MetalSoA                metal;
    DielectricSoA           diel;
    EmissiveSoA             emis;
    TexturedLambertianSoA   tex_lamb;
    std::vector<aleph::io::Image> textures;
    std::vector<Handle32>   lights;
    // Groups of emissive-quad handles from `lights`, carried from the lowering
    // (SPEC §4.2). Default empty => integrator treats all lights as one
    // implicit group (= current behavior).
    std::vector<std::vector<Handle32>> light_groups;
    BvhNodeArr              bvh;
};

inline MaterialHandle scene_add_lambertian(Scene& s, aleph::math::Vec3 albedo) {
    return MaterialHandle{MaterialKind::Lambertian, lambertian_append(s.lamb, albedo)};
}

inline MaterialHandle scene_add_metal(Scene& s, aleph::math::Vec3 albedo, aleph::math::f32 fuzz) {
    return MaterialHandle{MaterialKind::Metal, metal_append(s.metal, albedo, fuzz)};
}

inline MaterialHandle scene_add_dielectric(Scene& s, aleph::math::f32 ior) {
    return MaterialHandle{MaterialKind::Dielectric, dielectric_append(s.diel, ior)};
}

inline MaterialHandle scene_add_emissive(Scene& s, aleph::math::Vec3 emit) {
    return MaterialHandle{MaterialKind::Emissive, emissive_append(s.emis, emit)};
}

inline MaterialHandle scene_add_textured_lambertian(Scene& s, std::uint32_t tex_id,
                                                      aleph::math::Vec2 uv_scale) {
    return MaterialHandle{MaterialKind::TexturedLambertian,
                          textured_lambertian_append(s.tex_lamb, tex_id, uv_scale)};
}

inline Handle32 scene_add_sphere(Scene& s, aleph::math::Vec3 center, aleph::math::f32 r,
                                  MaterialHandle m) {
    const std::uint32_t idx = sphere_append(s.spheres, center, r, m);
    const Handle32 h = Handle32::make(HittableKind::Sphere, idx);
    if (m.kind == MaterialKind::Emissive) s.lights.push_back(h);
    return h;
}

inline Handle32 scene_add_quad(Scene& s, aleph::math::Vec3 Q, aleph::math::Vec3 u_edge,
                                aleph::math::Vec3 v_edge, MaterialHandle m) {
    const std::uint32_t idx = quad_append(s.quads, Q, u_edge, v_edge, m);
    const Handle32 h = Handle32::make(HittableKind::Quad, idx);
    if (m.kind == MaterialKind::Emissive) s.lights.push_back(h);
    return h;
}

inline Handle32 scene_add_tri(Scene& s, aleph::math::Vec3 v0, aleph::math::Vec3 v1,
                               aleph::math::Vec3 v2, MaterialHandle m) {
    const std::uint32_t idx = tri_append(s.tris, v0, v1, v2, m);
    const Handle32 h = Handle32::make(HittableKind::Tri, idx);
    if (m.kind == MaterialKind::Emissive) s.lights.push_back(h);
    return h;
}

}  // namespace aleph::scene

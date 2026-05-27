module;
#include <cstdint>
#include <vector>
#include <array>
#include <cmath>

export module aleph.render.sw:scene_rt;

import aleph.math;

export namespace aleph::render::sw {

using TexSampleFn = aleph::math::u32 (*)(aleph::math::f32, aleph::math::f32);

struct Lightmap {
    aleph::math::u32* texels;
    int w, h;
    aleph::math::f32 u_min, u_max, v_min, v_max;
};

struct Face {
    std::array<aleph::math::Vec3, 4> verts;
    std::array<aleph::math::Vec2, 4> uvs;
    TexSampleFn  tex;
    aleph::math::u32 lightmap_id;
};

struct SceneRT {
    std::vector<Face>            faces;
    std::vector<Lightmap>        lightmaps;
    std::vector<aleph::math::u32> lightmap_pool;
};

inline aleph::math::u32 tex_checker(aleph::math::f32 u, aleph::math::f32 v) noexcept {
    const int cu = static_cast<int>(std::floor(u));
    const int cv = static_cast<int>(std::floor(v));
    return ((cu ^ cv) & 1) ? 0xFFE0E0E0u : 0xFF303030u;
}

inline aleph::math::u32 tex_brick(aleph::math::f32 u, aleph::math::f32 v) noexcept {
    const int row = static_cast<int>(std::floor(v));
    const aleph::math::f32 u_off = (row & 1) ? 1.0f : 0.0f;
    const aleph::math::f32 uu = u + u_off;
    const aleph::math::f32 fu = uu - std::floor(uu * 0.5f) * 2.0f;
    const aleph::math::f32 fv = v - std::floor(v);
    if (fu < 0.08f || fu > 1.92f || fv < 0.08f || fv > 0.92f) return 0xFF555555u;
    const int col = static_cast<int>(std::floor(uu * 0.5f));
    const aleph::math::u32 n =
        static_cast<aleph::math::u32>((col * 73856093) ^ (row * 19349663));
    const aleph::math::u8 r = 140u + static_cast<aleph::math::u8>((n >> 16) & 31);
    const aleph::math::u8 g =  70u + static_cast<aleph::math::u8>((n >>  8) & 15);
    const aleph::math::u8 b =  50u + static_cast<aleph::math::u8>( n        & 15);
    return 0xFF000000u | (static_cast<aleph::math::u32>(r) << 16)
         | (static_cast<aleph::math::u32>(g) << 8) | b;
}

inline aleph::math::u32 tex_floor(aleph::math::f32 u, aleph::math::f32 v) noexcept {
    const aleph::math::f32 fu = u * 2.0f;
    const aleph::math::f32 fv = v * 2.0f;
    const int cu = static_cast<int>(std::floor(fu));
    const int cv = static_cast<int>(std::floor(fv));
    const aleph::math::f32 lu = fu - std::floor(fu);
    const aleph::math::f32 lv = fv - std::floor(fv);
    if (lu < 0.04f || lu > 0.96f || lv < 0.04f || lv > 0.96f) return 0xFF202020u;
    return ((cu ^ cv) & 1) ? 0xFFB0B0B0u : 0xFF909090u;
}

inline aleph::math::u32 tex_ceiling(aleph::math::f32 u, aleph::math::f32 v) noexcept {
    const aleph::math::u32 n =
        static_cast<aleph::math::u32>(static_cast<int>(std::floor(u * 4.0f)) * 73856093)
        ^ static_cast<aleph::math::u32>(static_cast<int>(std::floor(v * 4.0f)) * 19349663);
    const aleph::math::u8 r = 200u + static_cast<aleph::math::u8>((n >> 16) & 15);
    const aleph::math::u8 g = 195u + static_cast<aleph::math::u8>((n >>  8) & 15);
    const aleph::math::u8 b = 180u + static_cast<aleph::math::u8>( n        & 15);
    return 0xFF000000u | (static_cast<aleph::math::u32>(r) << 16)
         | (static_cast<aleph::math::u32>(g) << 8) | b;
}

}  // namespace aleph::render::sw

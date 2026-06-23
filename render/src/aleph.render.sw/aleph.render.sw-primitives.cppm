module;
#include <cstdint>
#include <array>

export module aleph.render.sw:primitives;

import aleph.math;
import :scene_rt;

export namespace aleph::render::sw {

inline std::uint32_t add_floor(SceneRT& s, aleph::math::Vec3 c,
                                aleph::math::f32 size, TexSampleFn tex) noexcept {
    const aleph::math::f32 h = size * 0.5f;
    Face f{};
    // Vertices wound CW from above so rast_scan treats above as the FRONT side
    // (rasterize splits as tris {0,1,2} and {0,2,3}; positive screen-area
    //  requires CW order when viewed through the camera). These primitives are
    //  one-sided (two_sided defaults false), so the back face is still culled;
    //  faces flagged two_sided render from both sides instead.
    f.verts = {
        aleph::math::Vec3{c.x - h, c.y, c.z - h},
        aleph::math::Vec3{c.x + h, c.y, c.z - h},
        aleph::math::Vec3{c.x + h, c.y, c.z + h},
        aleph::math::Vec3{c.x - h, c.y, c.z + h},
    };
    f.uvs = {
        aleph::math::Vec2{-h, -h}, aleph::math::Vec2{ h, -h},
        aleph::math::Vec2{ h,  h}, aleph::math::Vec2{-h,  h},
    };
    f.tex = tex;
    f.lightmap_id = static_cast<std::uint32_t>(s.lightmaps.size());
    s.faces.push_back(f);
    s.lightmaps.push_back(Lightmap{});
    return static_cast<std::uint32_t>(s.faces.size() - 1);
}

inline std::uint32_t add_cube(SceneRT& s, aleph::math::Vec3 c,
                               aleph::math::f32 size, TexSampleFn tex) noexcept {
    const aleph::math::f32 h = size * 0.5f;
    const aleph::math::Vec3 mn{c.x - h, c.y - h, c.z - h};
    const aleph::math::Vec3 mx{c.x + h, c.y + h, c.z + h};
    const std::uint32_t first = static_cast<std::uint32_t>(s.faces.size());
    auto add_face = [&](std::array<aleph::math::Vec3, 4> verts,
                          std::array<aleph::math::Vec2, 4> uvs) {
        Face f{};
        f.verts = verts; f.uvs = uvs; f.tex = tex;
        f.lightmap_id = static_cast<std::uint32_t>(s.lightmaps.size());
        s.faces.push_back(f);
        s.lightmaps.push_back(Lightmap{});
    };
    // 6 faces (top/bottom/N/S/E/W)
    add_face({aleph::math::Vec3{mn.x, mx.y, mx.z}, aleph::math::Vec3{mx.x, mx.y, mx.z},
              aleph::math::Vec3{mx.x, mx.y, mn.z}, aleph::math::Vec3{mn.x, mx.y, mn.z}},
             {aleph::math::Vec2{mn.x, mx.z}, aleph::math::Vec2{mx.x, mx.z},
              aleph::math::Vec2{mx.x, mn.z}, aleph::math::Vec2{mn.x, mn.z}});
    add_face({aleph::math::Vec3{mn.x, mn.y, mn.z}, aleph::math::Vec3{mx.x, mn.y, mn.z},
              aleph::math::Vec3{mx.x, mn.y, mx.z}, aleph::math::Vec3{mn.x, mn.y, mx.z}},
             {aleph::math::Vec2{mn.x, mn.z}, aleph::math::Vec2{mx.x, mn.z},
              aleph::math::Vec2{mx.x, mx.z}, aleph::math::Vec2{mn.x, mx.z}});
    add_face({aleph::math::Vec3{mn.x, mn.y, mx.z}, aleph::math::Vec3{mx.x, mn.y, mx.z},
              aleph::math::Vec3{mx.x, mx.y, mx.z}, aleph::math::Vec3{mn.x, mx.y, mx.z}},
             {aleph::math::Vec2{mn.x, mn.y}, aleph::math::Vec2{mx.x, mn.y},
              aleph::math::Vec2{mx.x, mx.y}, aleph::math::Vec2{mn.x, mx.y}});
    add_face({aleph::math::Vec3{mx.x, mn.y, mn.z}, aleph::math::Vec3{mn.x, mn.y, mn.z},
              aleph::math::Vec3{mn.x, mx.y, mn.z}, aleph::math::Vec3{mx.x, mx.y, mn.z}},
             {aleph::math::Vec2{mx.x, mn.y}, aleph::math::Vec2{mn.x, mn.y},
              aleph::math::Vec2{mn.x, mx.y}, aleph::math::Vec2{mx.x, mx.y}});
    add_face({aleph::math::Vec3{mx.x, mn.y, mx.z}, aleph::math::Vec3{mx.x, mn.y, mn.z},
              aleph::math::Vec3{mx.x, mx.y, mn.z}, aleph::math::Vec3{mx.x, mx.y, mx.z}},
             {aleph::math::Vec2{mx.z, mn.y}, aleph::math::Vec2{mn.z, mn.y},
              aleph::math::Vec2{mn.z, mx.y}, aleph::math::Vec2{mx.z, mx.y}});
    add_face({aleph::math::Vec3{mn.x, mn.y, mn.z}, aleph::math::Vec3{mn.x, mn.y, mx.z},
              aleph::math::Vec3{mn.x, mx.y, mx.z}, aleph::math::Vec3{mn.x, mx.y, mn.z}},
             {aleph::math::Vec2{mn.z, mn.y}, aleph::math::Vec2{mx.z, mn.y},
              aleph::math::Vec2{mx.z, mx.y}, aleph::math::Vec2{mn.z, mx.y}});
    return first;
}

inline std::uint32_t add_pillar(SceneRT& s, aleph::math::Vec3 base_c,
                                  aleph::math::f32 width, aleph::math::f32 height,
                                  TexSampleFn tex) noexcept {
    const aleph::math::f32 hw = width * 0.5f;
    const aleph::math::Vec3 mn{base_c.x - hw, base_c.y,          base_c.z - hw};
    const aleph::math::Vec3 mx{base_c.x + hw, base_c.y + height, base_c.z + hw};
    const std::uint32_t first = static_cast<std::uint32_t>(s.faces.size());
    auto add_face = [&](std::array<aleph::math::Vec3, 4> verts,
                          std::array<aleph::math::Vec2, 4> uvs) {
        Face f{};
        f.verts = verts; f.uvs = uvs; f.tex = tex;
        f.lightmap_id = static_cast<std::uint32_t>(s.lightmaps.size());
        s.faces.push_back(f);
        s.lightmaps.push_back(Lightmap{});
    };
    // 5 faces (top + 4 walls, no bottom)
    add_face({aleph::math::Vec3{mn.x, mx.y, mx.z}, aleph::math::Vec3{mx.x, mx.y, mx.z},
              aleph::math::Vec3{mx.x, mx.y, mn.z}, aleph::math::Vec3{mn.x, mx.y, mn.z}},
             {aleph::math::Vec2{mn.x, mx.z}, aleph::math::Vec2{mx.x, mx.z},
              aleph::math::Vec2{mx.x, mn.z}, aleph::math::Vec2{mn.x, mn.z}});
    add_face({aleph::math::Vec3{mn.x, mn.y, mx.z}, aleph::math::Vec3{mx.x, mn.y, mx.z},
              aleph::math::Vec3{mx.x, mx.y, mx.z}, aleph::math::Vec3{mn.x, mx.y, mx.z}},
             {aleph::math::Vec2{mn.x, mn.y}, aleph::math::Vec2{mx.x, mn.y},
              aleph::math::Vec2{mx.x, mx.y}, aleph::math::Vec2{mn.x, mx.y}});
    add_face({aleph::math::Vec3{mx.x, mn.y, mn.z}, aleph::math::Vec3{mn.x, mn.y, mn.z},
              aleph::math::Vec3{mn.x, mx.y, mn.z}, aleph::math::Vec3{mx.x, mx.y, mn.z}},
             {aleph::math::Vec2{mx.x, mn.y}, aleph::math::Vec2{mn.x, mn.y},
              aleph::math::Vec2{mn.x, mx.y}, aleph::math::Vec2{mx.x, mx.y}});
    add_face({aleph::math::Vec3{mx.x, mn.y, mx.z}, aleph::math::Vec3{mx.x, mn.y, mn.z},
              aleph::math::Vec3{mx.x, mx.y, mn.z}, aleph::math::Vec3{mx.x, mx.y, mx.z}},
             {aleph::math::Vec2{mx.z, mn.y}, aleph::math::Vec2{mn.z, mn.y},
              aleph::math::Vec2{mn.z, mx.y}, aleph::math::Vec2{mx.z, mx.y}});
    add_face({aleph::math::Vec3{mn.x, mn.y, mn.z}, aleph::math::Vec3{mn.x, mn.y, mx.z},
              aleph::math::Vec3{mn.x, mx.y, mx.z}, aleph::math::Vec3{mn.x, mx.y, mn.z}},
             {aleph::math::Vec2{mn.z, mn.y}, aleph::math::Vec2{mx.z, mn.y},
              aleph::math::Vec2{mx.z, mx.y}, aleph::math::Vec2{mn.z, mx.y}});
    return first;
}

}  // namespace aleph::render::sw

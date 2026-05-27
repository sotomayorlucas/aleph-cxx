module;
#include <cstdint>
#include <vector>
#include <array>

export module aleph.scene:tri_soa;

import aleph.math;
import :handle32;

export namespace aleph::scene {

struct TriSoA {
    std::vector<aleph::math::f32> v0x, v0y, v0z;
    std::vector<aleph::math::f32> v1x, v1y, v1z;
    std::vector<aleph::math::f32> v2x, v2y, v2z;
    std::vector<MaterialHandle>   mat;
    std::vector<aleph::math::Aabb> bbox;
};

inline std::uint32_t tri_append(TriSoA& t,
                                 aleph::math::Vec3 v0,
                                 aleph::math::Vec3 v1,
                                 aleph::math::Vec3 v2,
                                 MaterialHandle    m) noexcept {
    const std::array<aleph::math::Vec3, 3> corners{ v0, v1, v2 };
    aleph::math::Aabb box = aleph::math::Aabb::from_points(corners);
    constexpr aleph::math::f32 eps = 1e-4f;
    if (box.max.x - box.min.x < eps) { box.min.x -= eps; box.max.x += eps; }
    if (box.max.y - box.min.y < eps) { box.min.y -= eps; box.max.y += eps; }
    if (box.max.z - box.min.z < eps) { box.min.z -= eps; box.max.z += eps; }

    const std::uint32_t idx = static_cast<std::uint32_t>(t.v0x.size());
    t.v0x.push_back(v0.x); t.v0y.push_back(v0.y); t.v0z.push_back(v0.z);
    t.v1x.push_back(v1.x); t.v1y.push_back(v1.y); t.v1z.push_back(v1.z);
    t.v2x.push_back(v2.x); t.v2y.push_back(v2.y); t.v2z.push_back(v2.z);
    t.mat.push_back(m);
    t.bbox.push_back(box);
    return idx;
}

}  // namespace aleph::scene

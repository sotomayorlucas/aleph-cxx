module;
#include <cstdint>
#include <vector>

export module aleph.scene:sphere_soa;

import aleph.math;
import :handle32;

export namespace aleph::scene {

struct SphereSoA {
    std::vector<aleph::math::f32> cx, cy, cz;
    std::vector<aleph::math::f32> r;
    std::vector<MaterialHandle>   mat;
    std::vector<aleph::math::Aabb> bbox;
    // Per-primitive importance (SPEC §4.1, Phase 5.x-b); parallel array.
    // Plain data baked by the lowering; `aleph.scene` knows nothing of flow.
    std::vector<aleph::math::f32> importance;
};

inline std::uint32_t sphere_append(SphereSoA& s,
                                    aleph::math::Vec3 center,
                                    aleph::math::f32  radius,
                                    MaterialHandle    m) noexcept {
    const std::uint32_t idx = static_cast<std::uint32_t>(s.cx.size());
    s.cx.push_back(center.x);
    s.cy.push_back(center.y);
    s.cz.push_back(center.z);
    s.r.push_back(radius);
    s.mat.push_back(m);
    s.bbox.push_back(aleph::math::Aabb{
        aleph::math::Vec3{center.x - radius, center.y - radius, center.z - radius},
        aleph::math::Vec3{center.x + radius, center.y + radius, center.z + radius},
    });
    s.importance.push_back(0.0f);
    return idx;
}

}  // namespace aleph::scene

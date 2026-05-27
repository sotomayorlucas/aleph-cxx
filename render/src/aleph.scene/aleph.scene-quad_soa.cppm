module;
#include <cstdint>
#include <vector>
#include <cmath>
#include <array>

export module aleph.scene:quad_soa;

import aleph.math;
import :handle32;

export namespace aleph::scene {

struct QuadSoA {
    std::vector<aleph::math::f32> Qx, Qy, Qz;
    std::vector<aleph::math::f32> ux, uy, uz;
    std::vector<aleph::math::f32> vx, vy, vz;
    std::vector<aleph::math::f32> nx, ny, nz;
    std::vector<aleph::math::f32> D;
    std::vector<aleph::math::Vec3> w;
    std::vector<MaterialHandle>   mat;
    std::vector<aleph::math::Aabb> bbox;
};

inline std::uint32_t quad_append(QuadSoA& q,
                                  aleph::math::Vec3 Q,
                                  aleph::math::Vec3 u_edge,
                                  aleph::math::Vec3 v_edge,
                                  MaterialHandle    m) noexcept {
    const aleph::math::Vec3 n     = aleph::math::cross(u_edge, v_edge);
    const aleph::math::f32  n_lsq = aleph::math::length_sq(n);
    const aleph::math::f32  inv_n = 1.0f / std::sqrt(n_lsq);
    const aleph::math::Vec3 norm  = n * inv_n;
    const aleph::math::f32  D     = aleph::math::dot(norm, Q);
    const aleph::math::Vec3 w     = n * (1.0f / n_lsq);

    const std::array<aleph::math::Vec3, 4> corners{
        Q, Q + u_edge, Q + v_edge, Q + u_edge + v_edge
    };
    aleph::math::Aabb box = aleph::math::Aabb::from_points(corners);
    constexpr aleph::math::f32 eps = 1e-4f;
    if (box.max.x - box.min.x < eps) { box.min.x -= eps; box.max.x += eps; }
    if (box.max.y - box.min.y < eps) { box.min.y -= eps; box.max.y += eps; }
    if (box.max.z - box.min.z < eps) { box.min.z -= eps; box.max.z += eps; }

    const std::uint32_t idx = static_cast<std::uint32_t>(q.Qx.size());
    q.Qx.push_back(Q.x); q.Qy.push_back(Q.y); q.Qz.push_back(Q.z);
    q.ux.push_back(u_edge.x); q.uy.push_back(u_edge.y); q.uz.push_back(u_edge.z);
    q.vx.push_back(v_edge.x); q.vy.push_back(v_edge.y); q.vz.push_back(v_edge.z);
    q.nx.push_back(norm.x); q.ny.push_back(norm.y); q.nz.push_back(norm.z);
    q.D.push_back(D);
    q.w.push_back(w);
    q.mat.push_back(m);
    q.bbox.push_back(box);
    return idx;
}

}  // namespace aleph::scene

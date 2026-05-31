module;
#include <cstdint>
#include <optional>
#include <cmath>
#include <array>

export module aleph.scene:hit;

import aleph.math;
import :handle32;
import :scene;

export namespace aleph::scene {

struct HitRecord {
    aleph::math::Vec3 p;
    aleph::math::Vec3 normal;
    aleph::math::f32  t;
    aleph::math::f32  u, v;
    MaterialHandle    mat;
    bool              front_face;
    // Per-primitive importance of the winning primitive (SPEC §4.1/§4.2,
    // Phase 5.x-b). hit() sets this from the winning primitive's SoA store.
    aleph::math::f32  importance{0};
};

namespace detail {

inline bool aabb_hit(aleph::math::Aabb box, aleph::math::Ray r,
                      aleph::math::f32 t_min, aleph::math::f32 t_max) noexcept {
    {
        const aleph::math::f32 inv = 1.0f / r.dir.x;
        aleph::math::f32 t0 = (box.min.x - r.origin.x) * inv;
        aleph::math::f32 t1 = (box.max.x - r.origin.x) * inv;
        if (t0 > t1) { const auto t = t0; t0 = t1; t1 = t; }
        if (t0 > t_min) t_min = t0;
        if (t1 < t_max) t_max = t1;
        if (t_max <= t_min) return false;
    }
    {
        const aleph::math::f32 inv = 1.0f / r.dir.y;
        aleph::math::f32 t0 = (box.min.y - r.origin.y) * inv;
        aleph::math::f32 t1 = (box.max.y - r.origin.y) * inv;
        if (t0 > t1) { const auto t = t0; t0 = t1; t1 = t; }
        if (t0 > t_min) t_min = t0;
        if (t1 < t_max) t_max = t1;
        if (t_max <= t_min) return false;
    }
    {
        const aleph::math::f32 inv = 1.0f / r.dir.z;
        aleph::math::f32 t0 = (box.min.z - r.origin.z) * inv;
        aleph::math::f32 t1 = (box.max.z - r.origin.z) * inv;
        if (t0 > t1) { const auto t = t0; t0 = t1; t1 = t; }
        if (t0 > t_min) t_min = t0;
        if (t1 < t_max) t_max = t1;
        if (t_max <= t_min) return false;
    }
    return true;
}

inline std::optional<HitRecord>
hit_sphere(const SphereSoA& s, std::uint32_t idx, aleph::math::Ray r,
            aleph::math::f32 t_min, aleph::math::f32 t_max) noexcept {
    const aleph::math::Vec3 center{s.cx[idx], s.cy[idx], s.cz[idx]};
    const aleph::math::f32  radius = s.r[idx];
    const aleph::math::Vec3 oc = r.origin - center;
    const aleph::math::f32 a  = aleph::math::dot(r.dir, r.dir);
    const aleph::math::f32 hf = aleph::math::dot(oc, r.dir);
    const aleph::math::f32 c  = aleph::math::dot(oc, oc) - radius * radius;
    const aleph::math::f32 disc = hf * hf - a * c;
    if (disc < 0.0f) return std::nullopt;
    const aleph::math::f32 sd = std::sqrt(disc);
    aleph::math::f32 root = (-hf - sd) / a;
    if (root <= t_min || root >= t_max) {
        root = (-hf + sd) / a;
        if (root <= t_min || root >= t_max) return std::nullopt;
    }
    HitRecord rec{};
    rec.t   = root;
    rec.p   = r.at(root);
    rec.mat = s.mat[idx];
    const aleph::math::Vec3 outward = (rec.p - center) * (1.0f / radius);
    rec.front_face = aleph::math::dot(r.dir, outward) < 0.0f;
    rec.normal     = rec.front_face ? outward : -outward;
    const aleph::math::f32 theta = std::acos(-outward.y);
    const aleph::math::f32 phi   = std::atan2(-outward.z, outward.x) + aleph::math::pi_f;
    rec.u = phi   / aleph::math::two_pi_f;
    rec.v = theta / aleph::math::pi_f;
    // SPEC §4.2: carry the primitive's baked importance onto the record.
    // Plain data from the parallel store; `aleph.scene` knows nothing of flow.
    rec.importance = (idx < s.importance.size()) ? s.importance[idx] : 0.0f;
    return rec;
}

inline std::optional<HitRecord>
hit_quad(const QuadSoA& q, std::uint32_t idx, aleph::math::Ray r,
          aleph::math::f32 t_min, aleph::math::f32 t_max) noexcept {
    const aleph::math::Vec3 n{q.nx[idx], q.ny[idx], q.nz[idx]};
    const aleph::math::f32 denom = aleph::math::dot(n, r.dir);
    if (std::abs(denom) < 1e-8f) return std::nullopt;
    const aleph::math::f32 t = (q.D[idx] - aleph::math::dot(n, r.origin)) / denom;
    if (t <= t_min || t >= t_max) return std::nullopt;
    const aleph::math::Vec3 P = r.at(t);
    const aleph::math::Vec3 Q{q.Qx[idx], q.Qy[idx], q.Qz[idx]};
    const aleph::math::Vec3 u_edge{q.ux[idx], q.uy[idx], q.uz[idx]};
    const aleph::math::Vec3 v_edge{q.vx[idx], q.vy[idx], q.vz[idx]};
    const aleph::math::Vec3 hit_vec = P - Q;
    const aleph::math::f32 alpha = aleph::math::dot(q.w[idx], aleph::math::cross(hit_vec, v_edge));
    const aleph::math::f32 beta  = aleph::math::dot(q.w[idx], aleph::math::cross(u_edge, hit_vec));
    if (alpha < 0.0f || alpha > 1.0f || beta < 0.0f || beta > 1.0f) return std::nullopt;
    HitRecord rec{};
    rec.t = t; rec.p = P; rec.mat = q.mat[idx]; rec.u = alpha; rec.v = beta;
    rec.front_face = aleph::math::dot(r.dir, n) < 0.0f;
    rec.normal     = rec.front_face ? n : -n;
    // SPEC §4.2: carry the primitive's baked importance onto the record.
    rec.importance = (idx < q.importance.size()) ? q.importance[idx] : 0.0f;
    return rec;
}

inline std::optional<HitRecord>
hit_tri(const TriSoA& t, std::uint32_t idx, aleph::math::Ray r,
         aleph::math::f32 t_min, aleph::math::f32 t_max) noexcept {
    const aleph::math::Vec3 v0{t.v0x[idx], t.v0y[idx], t.v0z[idx]};
    const aleph::math::Vec3 v1{t.v1x[idx], t.v1y[idx], t.v1z[idx]};
    const aleph::math::Vec3 v2{t.v2x[idx], t.v2y[idx], t.v2z[idx]};
    const aleph::math::Vec3 e1 = v1 - v0;
    const aleph::math::Vec3 e2 = v2 - v0;
    const aleph::math::Vec3 pvec = aleph::math::cross(r.dir, e2);
    const aleph::math::f32 det  = aleph::math::dot(e1, pvec);
    if (std::abs(det) < 1e-8f) return std::nullopt;
    const aleph::math::f32 inv_det = 1.0f / det;
    const aleph::math::Vec3 tvec = r.origin - v0;
    const aleph::math::f32 u = inv_det * aleph::math::dot(tvec, pvec);
    if (u < 0.0f || u > 1.0f) return std::nullopt;
    const aleph::math::Vec3 qvec = aleph::math::cross(tvec, e1);
    const aleph::math::f32 v = inv_det * aleph::math::dot(r.dir, qvec);
    if (v < 0.0f || (u + v) > 1.0f) return std::nullopt;
    const aleph::math::f32 tt = inv_det * aleph::math::dot(e2, qvec);
    if (tt <= t_min || tt >= t_max) return std::nullopt;
    HitRecord rec{};
    rec.t = tt; rec.p = r.at(tt); rec.mat = t.mat[idx];
    const aleph::math::Vec3 outward = aleph::math::normalize(aleph::math::cross(e1, e2));
    rec.front_face = aleph::math::dot(r.dir, outward) < 0.0f;
    rec.normal     = rec.front_face ? outward : -outward;
    // SPEC §4.2: carry the primitive's baked importance onto the record.
    rec.importance = (idx < t.importance.size()) ? t.importance[idx] : 0.0f;
    return rec;
}

}  // namespace detail

// Bench wrappers — internal `detail::hit_sphere/quad` exposed for benchmarking.
[[nodiscard]] inline std::optional<HitRecord>
bench_hit_sphere(const SphereSoA& s, std::uint32_t i, aleph::math::Ray r,
                  aleph::math::f32 tmin, aleph::math::f32 tmax) noexcept {
    return detail::hit_sphere(s, i, r, tmin, tmax);
}

[[nodiscard]] inline std::optional<HitRecord>
bench_hit_quad(const QuadSoA& q, std::uint32_t i, aleph::math::Ray r,
                aleph::math::f32 tmin, aleph::math::f32 tmax) noexcept {
    return detail::hit_quad(q, i, r, tmin, tmax);
}

[[nodiscard]] inline std::optional<HitRecord>
hit(const Scene& s, aleph::math::Ray r,
     aleph::math::f32 t_min, aleph::math::f32 t_max) noexcept {
    if (s.bvh.nodes.empty()) return std::nullopt;

    const std::uint32_t root_idx = static_cast<std::uint32_t>(s.bvh.nodes.size() - 1);
    std::array<std::uint32_t, 64> stack{};
    std::uint32_t sp = 0;
    stack[sp++] = root_idx;

    std::optional<HitRecord> best;
    aleph::math::f32 closest = t_max;

    while (sp > 0) {
        const std::uint32_t ni = stack[--sp];
        const BvhNode& node = s.bvh.nodes[ni];
        if (!detail::aabb_hit(node.bbox, r, t_min, closest)) continue;

        auto process = [&](Handle32 h) {
            switch (h.hittable_kind()) {
                case HittableKind::BvhNode:
                    if (sp < 64u) stack[sp++] = h.index();
                    break;
                case HittableKind::Sphere:
                    if (auto rec = detail::hit_sphere(s.spheres, h.index(), r, t_min, closest); rec) {
                        closest = rec->t; best = rec;
                    }
                    break;
                case HittableKind::Quad:
                    if (auto rec = detail::hit_quad(s.quads, h.index(), r, t_min, closest); rec) {
                        closest = rec->t; best = rec;
                    }
                    break;
                case HittableKind::Tri:
                    if (auto rec = detail::hit_tri(s.tris, h.index(), r, t_min, closest); rec) {
                        closest = rec->t; best = rec;
                    }
                    break;
            }
        };
        process(node.left);
        // Single-primitive leaf-node (Task 7): left == right; skip second process.
        if (node.right.packed != node.left.packed) process(node.right);
    }
    return best;
}

}  // namespace aleph::scene

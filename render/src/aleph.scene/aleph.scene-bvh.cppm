module;
#include <cstdint>
#include <vector>
#include <span>
#include <algorithm>

export module aleph.scene:bvh;

import aleph.math;
import aleph.alloc;
import :handle32;
import :sphere_soa;
import :quad_soa;
import :tri_soa;
import :scene;

// BvhNode and BvhNodeArr are defined in :scene to avoid circular dependency.

export namespace aleph::scene {

namespace detail {

inline aleph::math::Aabb primitive_bbox(const Scene& s, Handle32 h) noexcept {
    switch (h.hittable_kind()) {
        case HittableKind::Sphere:  return s.spheres.bbox[h.index()];
        case HittableKind::Quad:    return s.quads.bbox[h.index()];
        case HittableKind::Tri:     return s.tris.bbox[h.index()];
        case HittableKind::BvhNode: return s.bvh.nodes[h.index()].bbox;
    }
    return aleph::math::Aabb{};
}

inline aleph::math::f32 centroid_axis(const aleph::math::Aabb& b, int axis) noexcept {
    switch (axis) {
        case 0:  return b.min.x + b.max.x;
        case 1:  return b.min.y + b.max.y;
        default: return b.min.z + b.max.z;
    }
}

inline int longest_axis(const aleph::math::Aabb& b) noexcept {
    const aleph::math::f32 dx = b.max.x - b.min.x;
    const aleph::math::f32 dy = b.max.y - b.min.y;
    const aleph::math::f32 dz = b.max.z - b.min.z;
    if (dx >= dy && dx >= dz) return 0;
    if (dy >= dz)              return 1;
    return 2;
}

inline Handle32 build_recursive(Scene& s, std::span<Handle32> items) {
    if (items.size() == 1) return items[0];

    aleph::math::Aabb combined = primitive_bbox(s, items[0]);
    for (std::size_t i = 1; i < items.size(); ++i)
        combined = aleph::math::union_of(combined, primitive_bbox(s, items[i]));

    const int axis = longest_axis(combined);
    std::ranges::sort(items, [&s, axis](Handle32 a, Handle32 b) noexcept {
        return centroid_axis(primitive_bbox(s, a), axis)
             < centroid_axis(primitive_bbox(s, b), axis);
    });

    const std::size_t mid = items.size() / 2;
    const Handle32 left  = build_recursive(s, items.first(mid));
    const Handle32 right = build_recursive(s, items.subspan(mid));

    const std::uint32_t node_idx = static_cast<std::uint32_t>(s.bvh.nodes.size());
    s.bvh.nodes.push_back(BvhNode{
        aleph::math::union_of(primitive_bbox(s, left), primitive_bbox(s, right)),
        left, right,
    });
    return Handle32::make(HittableKind::BvhNode, node_idx);
}

}  // namespace detail

inline void scene_build_bvh(Scene& s, aleph::alloc::Arena& scratch) {
    (void)scratch;
    s.bvh.nodes.clear();

    std::vector<Handle32> items;
    items.reserve(s.spheres.cx.size() + s.quads.Qx.size() + s.tris.v0x.size());
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(s.spheres.cx.size()); ++i)
        items.push_back(Handle32::make(HittableKind::Sphere, i));
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(s.quads.Qx.size()); ++i)
        items.push_back(Handle32::make(HittableKind::Quad, i));
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(s.tris.v0x.size()); ++i)
        items.push_back(Handle32::make(HittableKind::Tri, i));

    if (items.empty()) return;

    if (items.size() == 1) {
        // Single primitive: synthesize a leaf-node so callers walk uniformly.
        s.bvh.nodes.push_back(BvhNode{
            detail::primitive_bbox(s, items[0]),
            items[0], items[0],
        });
        return;
    }

    detail::build_recursive(s, std::span<Handle32>{items});
}

}  // namespace aleph::scene

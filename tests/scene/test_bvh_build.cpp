#include "doctest.h"
import aleph.scene;
import aleph.math;
import aleph.alloc;

using namespace aleph::scene;
using aleph::math::Vec3;

TEST_CASE("scene_build_bvh: 1 sphere -> single leaf root") {
    Scene s;
    const auto m = scene_add_lambertian(s, Vec3{0.5f, 0.5f, 0.5f});
    scene_add_sphere(s, Vec3{0, 0, 0}, 1.0f, m);
    alignas(16) static unsigned char scratch[16384];
    aleph::alloc::Arena arena{scratch, sizeof(scratch)};
    scene_build_bvh(s, arena);
    REQUIRE(s.bvh.nodes.size() == 1);
    const BvhNode& root = s.bvh.nodes[0];
    CHECK(root.left.hittable_kind() == HittableKind::Sphere);
    CHECK(root.left.index() == 0u);
    CHECK(root.right.packed == root.left.packed);   // single-leaf: right=left sentinel
}

TEST_CASE("scene_build_bvh: 3 mixed primitives -> internal node + leaves") {
    Scene s;
    const auto m_lamb = scene_add_lambertian(s, Vec3{0.5f, 0.5f, 0.5f});
    const auto m_met  = scene_add_metal(s, Vec3{0.7f, 0.7f, 0.7f}, 0.0f);
    scene_add_sphere(s, Vec3{-5, 0, 0}, 0.5f, m_lamb);
    scene_add_sphere(s, Vec3{ 0, 0, 0}, 0.5f, m_met);
    scene_add_sphere(s, Vec3{ 5, 0, 0}, 0.5f, m_lamb);
    alignas(16) static unsigned char scratch[16384];
    aleph::alloc::Arena arena{scratch, sizeof(scratch)};
    scene_build_bvh(s, arena);
    REQUIRE(s.bvh.nodes.size() >= 2);   // 3 primitives → 2 internal BvhNodes (N-1 for N leaves)
    const BvhNode& root = s.bvh.nodes.back();
    // With median-split on 3 items: left=Sphere(single), right=BvhNode(pair)
    CHECK(root.right.hittable_kind() == HittableKind::BvhNode);
    CHECK(root.bbox.min.x <= -5.5f);
    CHECK(root.bbox.max.x >=  5.5f);
}

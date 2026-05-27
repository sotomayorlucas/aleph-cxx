#include "doctest.h"
import aleph.scene;

using namespace aleph::scene;

TEST_CASE("Handle32 layout: 4 bytes packed") {
    static_assert(sizeof(Handle32) == 4);
}

TEST_CASE("Handle32 pack/unpack") {
    constexpr Handle32 h = Handle32::make(HittableKind::Sphere, 42);
    CHECK(h.hittable_kind() == HittableKind::Sphere);
    CHECK(h.index() == 42u);
}

TEST_CASE("Handle32 pack/unpack: BvhNode + max index") {
    constexpr Handle32 h = Handle32::make(HittableKind::BvhNode, 0x00FFFFFFu);
    CHECK(h.hittable_kind() == HittableKind::BvhNode);
    CHECK(h.index() == 0x00FFFFFFu);
}

TEST_CASE("Handle32: distinct packed values for different kinds at same idx") {
    constexpr Handle32 a = Handle32::make(HittableKind::Sphere, 7);
    constexpr Handle32 b = Handle32::make(HittableKind::Quad,   7);
    CHECK(a.packed != b.packed);
    CHECK(a.index() == b.index());
    CHECK(a.hittable_kind() != b.hittable_kind());
}

TEST_CASE("MaterialKind enumerators") {
    static_assert(static_cast<int>(MaterialKind::Lambertian) == 0);
    static_assert(static_cast<int>(MaterialKind::TexturedLambertian) == 4);
    CHECK(true);
}

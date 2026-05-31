// Shared, padding-proof byte-image serializers for the Phase-5 lowering tests.
//
// LoweredScene holds a move-only OrderedMap (handle_map) and deletes copy, so it
// is neither copyable nor trivially comparable. The lowering tests freeze each
// lowering into a flat byte image — its scalar LEAVES walked in IR iteration
// order — and compare images with ==. That is the literal "byte-identical
// LoweredScene" the SPEC demands, and it pins insertion order + f32 bit-patterns.
//
// Why LEAF-WISE and never a whole-struct memcpy: the IR value types embed
// `aleph::math::Vec3`, which is `alignas(16)` and carries a 4th (`w`) lane plus
// trailing/inter-field PADDING. So `SphereLocal` / `QuadLocal` / `TriLocal` /
// `MaterialParams` / `LoweredCamera` / `LoweredEntity` all have INDETERMINATE
// padding bytes that `lower()`'s aggregate init does not zero. A raw
// `reinterpret_cast`+memcpy over `sizeof(T)` would capture those bytes, so two
// logically-identical LoweredScenes (from two `lower()` calls) could differ in
// padding alone and fail a "byte-identical" / determinism CHECK nondeterministically.
// `lower()` is deterministic; the bug was in the test serialization. Serializing
// genuinely-scalar leaves (f32 / u32 / u64 / enum-as-u32 / NodeId.value) is exact,
// deterministic, and padding-proof.
//
// Header-only: every helper lives in an anonymous namespace so each translation
// unit that #includes this gets its own internal-linkage copy (no ODR clash, no
// CMake change needed).

#ifndef ALEPH_TESTS_LOWERING_FREEZE_HPP
#define ALEPH_TESTS_LOWERING_FREEZE_HPP

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>
#include <vector>

// Requires (provided by the including TU's module imports): aleph.math (Vec3),
// aleph.types (NodeId / GeometryPayload / Sphere/Quad/TriLocal / MaterialKind),
// aleph.lowering (MaterialParams / LoweredEntity / LoweredScene).

namespace aleph_test_freeze {
namespace {

// Append the raw bytes of a GENUINELY-SCALAR value (integer / enum / f32). These
// have no internal padding, so a raw copy is exact and deterministic. NEVER call
// this on a struct that embeds a Vec3 or otherwise has padding.
template <typename T>
void put(std::vector<std::byte>& out, const T& v) {
    static_assert(std::is_scalar_v<T>,
                  "put() is for scalar leaves only — aggregates may carry "
                  "indeterminate padding; serialize their fields instead");
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    out.insert(out.end(), p, p + sizeof(T));
}

// A Vec3 by its three semantic f32 components (skips the alignas(16) `w` /
// padding lane). NO struct memcpy.
[[maybe_unused]] void put_vec3(std::vector<std::byte>& out, const aleph::math::Vec3& v) {
    put(out, v.x);
    put(out, v.y);
    put(out, v.z);
}

// A geometry primitive, field by field, after the variant tag (so a
// Sphere/Quad/Tri can never collide on payload bytes alone).
[[maybe_unused]] void put_geometry(std::vector<std::byte>& out,
                                   const aleph::types::GeometryPayload& g) {
    put(out, static_cast<std::uint32_t>(g.index()));
    std::visit(
        [&](const auto& prim) {
            using P = std::decay_t<decltype(prim)>;
            if constexpr (std::is_same_v<P, aleph::types::SphereLocal>) {
                put_vec3(out, prim.center);
                put(out, prim.radius);
            } else if constexpr (std::is_same_v<P, aleph::types::QuadLocal>) {
                put_vec3(out, prim.q);
                put_vec3(out, prim.u);
                put_vec3(out, prim.v);
            } else {  // TriLocal
                put_vec3(out, prim.a);
                put_vec3(out, prim.b);
                put_vec3(out, prim.c);
            }
        },
        g);
}

// MaterialParams, field by field (kind + the four physical params).
[[maybe_unused]] void put_material(std::vector<std::byte>& out,
                                   const aleph::lowering::MaterialParams& m) {
    put(out, static_cast<std::uint32_t>(m.kind));
    put_vec3(out, m.albedo);
    put(out, m.fuzz);
    put(out, m.ior);
    put_vec3(out, m.emit);
}

// One entity (or light-table entry): source id, world geometry, MaterialParams —
// all leaf-wise (padding-proof).
[[maybe_unused]] void put_entity(std::vector<std::byte>& out,
                                 const aleph::lowering::LoweredEntity& e) {
    put(out, e.source.value);
    put_geometry(out, e.world_geometry);
    put_material(out, e.material);
}

// One entity (or light-table entry) -> its own flat byte image.
[[maybe_unused]] std::vector<std::byte> freeze_entity(const aleph::lowering::LoweredEntity& e) {
    std::vector<std::byte> out;
    put_entity(out, e);
    return out;
}

// Just the MaterialParams of an entity, as bytes.
[[maybe_unused]] std::vector<std::byte> freeze_material(const aleph::lowering::LoweredEntity& e) {
    std::vector<std::byte> out;
    put_material(out, e.material);
    return out;
}

// The camera pose as bytes, field by field (padding-proof).
[[maybe_unused]] std::vector<std::byte> freeze_camera(const aleph::lowering::LoweredScene& ls) {
    std::vector<std::byte> out;
    put_vec3(out, ls.camera.look_from);
    put_vec3(out, ls.camera.look_at);
    put_vec3(out, ls.camera.up);
    put(out, ls.camera.vfov_deg);
    put(out, ls.camera.aperture);
    put(out, ls.camera.focus_dist);
    return out;
}

// The handle_map as bytes, walked in OrderedMap iteration (insertion) order.
[[maybe_unused]] std::vector<std::byte> freeze_handle_map(const aleph::lowering::LoweredScene& ls) {
    std::vector<std::byte> out;
    put(out, static_cast<std::uint64_t>(ls.handle_map.size()));
    for (auto [nid, idx] : ls.handle_map) {
        put(out, nid.value);
        put(out, idx);
    }
    return out;
}

// Freeze a whole LoweredScene into a flat byte image, walking everything in IR
// iteration order: entities, the light table, the camera pose, then the
// handle_map in OrderedMap iteration (insertion) order.
[[maybe_unused]] std::vector<std::byte> freeze(const aleph::lowering::LoweredScene& ls) {
    std::vector<std::byte> out;

    put(out, static_cast<std::uint64_t>(ls.entities.size()));
    for (const auto& e : ls.entities) put_entity(out, e);

    put(out, static_cast<std::uint64_t>(ls.lights.size()));
    for (const auto& e : ls.lights) put_entity(out, e);

    put_vec3(out, ls.camera.look_from);
    put_vec3(out, ls.camera.look_at);
    put_vec3(out, ls.camera.up);
    put(out, ls.camera.vfov_deg);
    put(out, ls.camera.aperture);
    put(out, ls.camera.focus_dist);

    put(out, static_cast<std::uint64_t>(ls.handle_map.size()));
    for (auto [nid, idx] : ls.handle_map) {
        put(out, nid.value);
        put(out, idx);
    }
    return out;
}

}  // namespace
}  // namespace aleph_test_freeze

#endif  // ALEPH_TESTS_LOWERING_FREEZE_HPP

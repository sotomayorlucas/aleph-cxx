module;
#include <cstdint>

export module aleph.scene:handle32;

export namespace aleph::scene {

enum class HittableKind : std::uint8_t {
    Sphere = 0,
    Quad   = 1,
    Tri    = 2,
    BvhNode = 3,
};

enum class MaterialKind : std::uint8_t {
    Lambertian         = 0,
    Metal              = 1,
    Dielectric         = 2,
    Emissive           = 3,
    TexturedLambertian = 4,
};

struct Handle32 {
    std::uint32_t packed;

    constexpr HittableKind hittable_kind() const noexcept {
        return static_cast<HittableKind>(packed >> 24);
    }
    constexpr std::uint32_t index() const noexcept {
        return packed & 0x00FFFFFFu;
    }
    static constexpr Handle32 make(HittableKind k, std::uint32_t idx) noexcept {
        return Handle32{ (static_cast<std::uint32_t>(k) << 24) | (idx & 0x00FFFFFFu) };
    }
};

struct MaterialHandle {
    MaterialKind kind;
    std::uint32_t idx;
};

}  // namespace aleph::scene

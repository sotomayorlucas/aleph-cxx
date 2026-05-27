module;
#include <cstdint>

export module aleph.types:attribute;

export namespace aleph::types {

enum class MaterialKind : std::uint8_t {
    Lambertian = 0,
    Metal      = 1,
    Dielectric = 2,
    Emissive   = 3,
};

enum class LightKind : std::uint8_t {
    Point       = 0,
    Area        = 1,
    Directional = 2,
};

enum class MediumKind : std::uint8_t {
    Vacuum        = 0,
    Homogeneous   = 1,
    Heterogeneous = 2,
};

enum class TextureFormat : std::uint8_t {
    Rgba8 = 0,
    Rgb8  = 1,
    R32F  = 2,
};

}  // namespace aleph::types

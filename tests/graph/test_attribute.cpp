#include "doctest.h"
import aleph.types;

using namespace aleph::types;

TEST_CASE("MaterialKind values") {
    CHECK(static_cast<int>(MaterialKind::Lambertian) == 0);
    CHECK(static_cast<int>(MaterialKind::Metal)      == 1);
    CHECK(static_cast<int>(MaterialKind::Dielectric) == 2);
    CHECK(static_cast<int>(MaterialKind::Emissive)   == 3);
    CHECK(static_cast<int>(MaterialKind::TexturedLambertian) == 4);
}

TEST_CASE("LightKind values") {
    CHECK(static_cast<int>(LightKind::Point)       == 0);
    CHECK(static_cast<int>(LightKind::Area)        == 1);
    CHECK(static_cast<int>(LightKind::Directional) == 2);
}

TEST_CASE("MediumKind values") {
    CHECK(static_cast<int>(MediumKind::Vacuum)        == 0);
    CHECK(static_cast<int>(MediumKind::Homogeneous)   == 1);
    CHECK(static_cast<int>(MediumKind::Heterogeneous) == 2);
}

TEST_CASE("TextureFormat values") {
    CHECK(static_cast<int>(TextureFormat::Rgba8) == 0);
    CHECK(static_cast<int>(TextureFormat::Rgb8)  == 1);
    CHECK(static_cast<int>(TextureFormat::R32F)  == 2);
}

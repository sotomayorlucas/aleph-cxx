#include "doctest.h"
#include <cstdio>
#include <filesystem>
import aleph.io;

using namespace aleph::io;

TEST_CASE("load_ppm parses a valid 2x1 P6 image") {
    const unsigned char bytes[] = {
        'P','6','\n','2',' ','1','\n','2','5','5','\n',
        255, 0, 0,   0, 255, 0
    };
    auto r = load_ppm(std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(bytes), sizeof(bytes)});
    REQUIRE(r);
    CHECK(r->width  == 2);
    CHECK(r->height == 1);
    CHECK(r->pixels.size() == 6);
    CHECK(r->pixels[0] == std::byte{255});
    CHECK(r->pixels[4] == std::byte{255});
}

TEST_CASE("load_ppm rejects a non-P6 magic") {
    const unsigned char bytes[] = {'P','3','\n','1',' ','1','\n','2','5','5','\n', 0,0,0};
    auto r = load_ppm(std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(bytes), sizeof(bytes)});
    CHECK_FALSE(r);
}

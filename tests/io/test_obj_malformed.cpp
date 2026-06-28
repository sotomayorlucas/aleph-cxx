#include "doctest.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>

import aleph.io;

using namespace aleph::io;

namespace {

std::span<const std::byte> bytes(const char* s) {
    return {reinterpret_cast<const std::byte*>(s), std::strlen(s)};
}

}  // namespace

TEST_CASE("load_obj: rejects face with out-of-range vertex index") {
    const char data[] = "v 0 0 0\nv 1 0 0\nf 1 2 99\n";
    auto r = load_obj(bytes(data));
    CHECK_FALSE(r.has_value());
    CHECK(r.error().find("index") != std::string::npos);
}

TEST_CASE("load_obj: rejects line longer than 255 bytes") {
    std::string data = "v 0 0 0\n";
    data += std::string(260, ' ');
    data += "0 1 0\n";
    auto r = load_obj(std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(data.data()), data.size()});
    CHECK_FALSE(r.has_value());
    CHECK(r.error().find("line") != std::string::npos);
}

TEST_CASE("load_obj: rejects face fan with more than 16 vertices") {
    std::string data;
    for (int i = 0; i < 18; ++i) {
        data += "v " + std::to_string(i) + " 0 0\n";
    }
    data += "f";
    for (int i = 1; i <= 18; ++i) {
        data += " " + std::to_string(i);
    }
    data += "\n";
    auto r = load_obj(std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(data.data()), data.size()});
    CHECK_FALSE(r.has_value());
    CHECK(r.error().find("16") != std::string::npos);
}

TEST_CASE("regression_obj_truncated_line: long face line must not silently drop verts") {
    // 17 indices fit in 255 chars — must error, not produce partial mesh.
    std::string data;
    for (int i = 0; i < 17; ++i) {
        data += "v " + std::to_string(i) + " 0 0\n";
    }
    data += "f";
    for (int i = 1; i <= 17; ++i) {
        data += " " + std::to_string(i);
    }
    data += "\n";
    auto r = load_obj(std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(data.data()), data.size()});
    CHECK_FALSE(r.has_value());
}

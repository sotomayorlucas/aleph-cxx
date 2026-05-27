#include "doctest.h"
#include <cstring>
#include <span>
import aleph.io;

using namespace aleph::io;

TEST_CASE("load_obj: parses verts and faces") {
    const char data[] =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "v 0 0 1\n"
        "f 1 2 3\n"
        "f 1 3 4\n";
    auto r = load_obj(std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(data), sizeof(data) - 1});
    REQUIRE(r);
    CHECK(r->verts.size() == 4);
    CHECK(r->tris.size()  == 2);
    CHECK(r->tris[0][0] == 0);
    CHECK(r->tris[0][1] == 1);
    CHECK(r->tris[0][2] == 2);
}

TEST_CASE("load_obj: ignores comments and unknown directives") {
    const char data[] =
        "# this is a comment\n"
        "vn 0 1 0\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n";
    auto r = load_obj(std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(data), sizeof(data) - 1});
    REQUIRE(r);
    CHECK(r->verts.size() == 3);
    CHECK(r->tris.size()  == 1);
}

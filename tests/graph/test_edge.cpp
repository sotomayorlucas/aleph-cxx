#include "doctest.h"
#include <string_view>
import aleph.types;

using namespace aleph::types;

TEST_CASE("EdgeKind: enum values and as_tla") {
    CHECK(static_cast<int>(EdgeKind::Adjacent)   == 0);
    CHECK(static_cast<int>(EdgeKind::Contains)   == 1);
    CHECK(static_cast<int>(EdgeKind::Influences) == 2);
    CHECK(static_cast<int>(EdgeKind::References) == 3);
    CHECK(as_tla(EdgeKind::Adjacent)   == std::string_view("adjacent"));
    CHECK(as_tla(EdgeKind::Contains)   == std::string_view("contains"));
    CHECK(as_tla(EdgeKind::Influences) == std::string_view("influences"));
    CHECK(as_tla(EdgeKind::References) == std::string_view("references"));
    CHECK(all_edge_kinds().size() == 4);
}

TEST_CASE("EdgeKind::allows — Adjacent only (Mesh, Mesh)") {
    CHECK( allows(EdgeKind::Adjacent, NodeKind::Mesh,     NodeKind::Mesh));
    CHECK(!allows(EdgeKind::Adjacent, NodeKind::Mesh,     NodeKind::Material));
    CHECK(!allows(EdgeKind::Adjacent, NodeKind::Texture,  NodeKind::Texture));
    CHECK(!allows(EdgeKind::Adjacent, NodeKind::Volume,   NodeKind::Volume));
}

TEST_CASE("EdgeKind::allows — Contains: Transform parents subset") {
    CHECK( allows(EdgeKind::Contains, NodeKind::Transform, NodeKind::Transform));
    CHECK( allows(EdgeKind::Contains, NodeKind::Transform, NodeKind::Mesh));
    CHECK( allows(EdgeKind::Contains, NodeKind::Transform, NodeKind::Light));
    CHECK( allows(EdgeKind::Contains, NodeKind::Transform, NodeKind::Camera));
    CHECK( allows(EdgeKind::Contains, NodeKind::Transform, NodeKind::Volume));
    CHECK(!allows(EdgeKind::Contains, NodeKind::Mesh,      NodeKind::Transform));
    CHECK(!allows(EdgeKind::Contains, NodeKind::Transform, NodeKind::Material));
    CHECK(!allows(EdgeKind::Contains, NodeKind::Transform, NodeKind::Texture));
}

TEST_CASE("EdgeKind::allows — Influences: Light/Volume/Material -> Mesh only") {
    CHECK( allows(EdgeKind::Influences, NodeKind::Light,    NodeKind::Mesh));
    CHECK( allows(EdgeKind::Influences, NodeKind::Volume,   NodeKind::Mesh));
    CHECK( allows(EdgeKind::Influences, NodeKind::Material, NodeKind::Mesh));
    CHECK(!allows(EdgeKind::Influences, NodeKind::Mesh,     NodeKind::Light));
    CHECK(!allows(EdgeKind::Influences, NodeKind::Light,    NodeKind::Light));
}

TEST_CASE("EdgeKind::allows — References: Mesh->Material or Material->Texture") {
    CHECK( allows(EdgeKind::References, NodeKind::Mesh,     NodeKind::Material));
    CHECK( allows(EdgeKind::References, NodeKind::Material, NodeKind::Texture));
    CHECK(!allows(EdgeKind::References, NodeKind::Material, NodeKind::Mesh));
    CHECK(!allows(EdgeKind::References, NodeKind::Mesh,     NodeKind::Texture));
}

TEST_CASE("Edge struct: id, kind, src, dst") {
    Edge e{EdgeId{7}, EdgeKind::Influences, NodeId{3}, NodeId{4}};
    CHECK(e.id.value == 7);
    CHECK(e.kind == EdgeKind::Influences);
    CHECK(e.src.value == 3);
    CHECK(e.dst.value == 4);
}

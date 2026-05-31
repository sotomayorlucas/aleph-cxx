#include "doctest.h"

#include <string>

import aleph.math;
import aleph.types;
import aleph.graph;
import aleph.lowering;

// SPEC §8.4 — missing_reference_fails_cleanly.
//
// A Mesh whose required References->Material edge is absent must make lower()
// return LowerError::DanglingReference. The contract is explicit (SPEC §4.2
// step 3 and §9): a missing/dangling material reference is a STRUCTURED error,
// NEVER a silent default. So the oracle is twofold:
//   1. lower() returns an error (NOT a value carrying some fallback material);
//   2. that error is exactly LowerError::DanglingReference.

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Vec3;

namespace {

// root Transform -> Camera + Mesh(SphereLocal). The mesh deliberately has NO
// References->Material edge. Everything else is well-formed so the ONLY reason
// to fail is the dangling material reference.
Graph make_graph_with_unreferenced_mesh() {
    Graph g;

    const NodeId root = g.alloc_node_id();
    g.insert_node(Transform{root, 0});

    const NodeId cam = g.alloc_node_id();
    g.insert_node(Camera{cam, std::string("sensor0")});

    const NodeId mesh = g.alloc_node_id();
    Mesh m{mesh, std::string("sphere"), 0};
    m.geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
    g.insert_node(std::move(m));

    // Note: a Material node may even exist in the graph, but with NO
    // References edge wiring the mesh to it the reference is still dangling.
    const NodeId mat = g.alloc_node_id();
    g.insert_node(Material{mat, MaterialKind::Lambertian});

    (void)g.add_edge(EdgeKind::Contains, root, cam);
    (void)g.add_edge(EdgeKind::Contains, root, mesh);
    // Intentionally NO  References: mesh -> mat.
    return g;
}

}  // namespace

TEST_CASE("lowering: Mesh without References->Material fails with DanglingReference") {
    Graph g = make_graph_with_unreferenced_mesh();

    auto lowered = aleph::lowering::lower(g);

    // 1. Must NOT silently succeed with a default material.
    REQUIRE_FALSE(lowered.has_value());
    // 2. The structured error is precisely DanglingReference.
    CHECK(lowered.error() == aleph::lowering::LowerError::DanglingReference);
}

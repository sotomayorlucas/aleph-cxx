#include "doctest.h"

#include <optional>
#include <string>
#include <utility>

import aleph.math;       // Vec3, Mat4
import aleph.types;      // NodeId, Transform, Mesh, Material, geometry payloads
import aleph.graph;      // Graph
import aleph.edit;       // EditorController

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Vec3;
using aleph::math::Mat4;
using aleph::edit::EditorController;

namespace {

// root Transform ─Contains→ per-object Transform ─Contains→ Mesh ─References→ Material,
// plus a Camera under root so the controller lowers to a non-empty scene.
struct TwoXf { Graph g; NodeId root{}, xf{}, mesh{}; };

TwoXf make_one_object() {
    TwoXf s;
    Graph& g = s.g;
    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, LocalTransform{Mat4::identity()}});

    const NodeId cam = g.alloc_node_id();
    Camera c{cam, std::string("sensor0")};
    c.look_from = Vec3{0.0f, 1.0f, 5.0f};
    c.look_at   = Vec3{0.0f, 0.0f, 0.0f};
    c.up        = Vec3{0.0f, 1.0f, 0.0f};
    c.vfov_deg  = 45.0f;
    g.insert_node(std::move(c));

    s.xf = g.alloc_node_id();
    g.insert_node(Transform{s.xf, 0, LocalTransform{Mat4::identity()}});

    s.mesh = g.alloc_node_id();
    Mesh m{s.mesh, std::string("obj"), 0};
    m.geometry = SphereLocal{Vec3{0.0f, 0.5f, 0.0f}, 0.5f};
    g.insert_node(std::move(m));

    const NodeId mat = g.alloc_node_id();
    Material mm{mat, MaterialKind::Lambertian};
    mm.albedo = Vec3{0.8f, 0.2f, 0.2f};
    g.insert_node(std::move(mm));

    (void)g.add_edge(EdgeKind::Contains,   s.root, cam);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.xf);
    (void)g.add_edge(EdgeKind::Contains,   s.xf,  s.mesh);
    (void)g.add_edge(EdgeKind::References, s.mesh, mat);
    return s;
}

}  // namespace

TEST_CASE("transform_of returns the controlling Transform of a mesh") {
    TwoXf s = make_one_object();
    const NodeId xf = s.xf, mesh = s.mesh, root = s.root;
    EditorController ctl{std::move(s.g)};

    const std::optional<NodeId> got = ctl.transform_of(mesh);
    REQUIRE(got.has_value());
    CHECK(*got == xf);

    // A node with no Transform parent (the root itself) yields nullopt.
    CHECK_FALSE(ctl.transform_of(root).has_value());
}

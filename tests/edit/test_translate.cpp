#include "doctest.h"

#include <optional>
#include <string>
#include <utility>
#include <variant>

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

TEST_CASE("transform_of resolves each mesh to its own controlling Transform") {
    Graph g;
    const NodeId root = g.alloc_node_id();
    g.insert_node(Transform{root, 0, LocalTransform{Mat4::identity()}});

    const NodeId cam = g.alloc_node_id();
    Camera c{cam, std::string("sensor0")};
    c.look_from = Vec3{0.0f, 1.0f, 5.0f};
    c.look_at   = Vec3{0.0f, 0.0f, 0.0f};
    c.up        = Vec3{0.0f, 1.0f, 0.0f};
    c.vfov_deg  = 45.0f;
    g.insert_node(std::move(c));

    // Object A: xfA -> meshA -> matA
    const NodeId xfA = g.alloc_node_id();
    g.insert_node(Transform{xfA, 0, LocalTransform{Mat4::identity()}});
    const NodeId meshA = g.alloc_node_id();
    Mesh mA{meshA, std::string("A"), 0};
    mA.geometry = SphereLocal{Vec3{-1.0f, 0.5f, 0.0f}, 0.5f};
    g.insert_node(std::move(mA));
    const NodeId matA = g.alloc_node_id();
    Material mmA{matA, MaterialKind::Lambertian};
    mmA.albedo = Vec3{0.8f, 0.2f, 0.2f};
    g.insert_node(std::move(mmA));

    // Object B: xfB -> meshB -> matB
    const NodeId xfB = g.alloc_node_id();
    g.insert_node(Transform{xfB, 0, LocalTransform{Mat4::identity()}});
    const NodeId meshB = g.alloc_node_id();
    Mesh mB{meshB, std::string("B"), 0};
    mB.geometry = SphereLocal{Vec3{1.0f, 0.5f, 0.0f}, 0.5f};
    g.insert_node(std::move(mB));
    const NodeId matB = g.alloc_node_id();
    Material mmB{matB, MaterialKind::Lambertian};
    mmB.albedo = Vec3{0.2f, 0.2f, 0.8f};
    g.insert_node(std::move(mmB));

    (void)g.add_edge(EdgeKind::Contains,   root, cam);
    (void)g.add_edge(EdgeKind::Contains,   root, xfA);
    (void)g.add_edge(EdgeKind::Contains,   xfA,  meshA);
    (void)g.add_edge(EdgeKind::References, meshA, matA);
    (void)g.add_edge(EdgeKind::Contains,   root, xfB);
    (void)g.add_edge(EdgeKind::Contains,   xfB,  meshB);
    (void)g.add_edge(EdgeKind::References, meshB, matB);

    EditorController ctl{std::move(g)};

    const auto gotA = ctl.transform_of(meshA);
    const auto gotB = ctl.transform_of(meshB);
    REQUIRE(gotA.has_value());
    REQUIRE(gotB.has_value());
    CHECK(*gotA == xfA);   // each mesh -> ITS OWN transform, not just any
    CHECK(*gotB == xfB);
    CHECK(*gotA != *gotB);
}

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

TEST_CASE("translate_selected moves only the selected object's Transform") {
    TwoXf s = make_one_object();
    const NodeId xf = s.xf, mesh = s.mesh;
    EditorController ctl{std::move(s.g)};

    // No selection -> no-op success (nothing moves).
    {
        auto r = ctl.translate_selected(Vec3{1.0f, 0.0f, 0.0f});
        CHECK(r.has_value());
        const auto* node = ctl.graph().node(xf);
        REQUIRE(node != nullptr);
        const Mat4& m = std::get<Transform>(*node).local.m;
        CHECK(m(0, 3) == doctest::Approx(0.0f));
    }

    // Select the mesh and nudge +X by 1.0, then +Y by 2.0 (accumulates).
    ctl.select(mesh);
    REQUIRE(ctl.translate_selected(Vec3{1.0f, 0.0f, 0.0f}).has_value());
    REQUIRE(ctl.translate_selected(Vec3{0.0f, 2.0f, 0.0f}).has_value());

    const auto* node = ctl.graph().node(xf);
    REQUIRE(node != nullptr);
    const Mat4& m = std::get<Transform>(*node).local.m;
    CHECK(m(0, 3) == doctest::Approx(1.0f));   // translation column (m[12..14])
    CHECK(m(1, 3) == doctest::Approx(2.0f));
    CHECK(m(2, 3) == doctest::Approx(0.0f));
}

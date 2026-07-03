#include "doctest.h"

#include <cstddef>
#include <string>
#include <utility>   // std::move (Graph is move-only)
#include <variant>
#include <vector>

import aleph.edit;     // EditorController (headless core under test)
import aleph.graph;    // Graph
import aleph.types;    // NodeId, Mesh/Material/Camera/Transform, geometry payloads
import aleph.math;     // Vec3, Mat4
import aleph.sim;      // Section<f64>, StepError
import aleph.lowering; // AddObject, Op, MaterialParams

// Phase 6 wave-field — `EditorController` physics seam (this task).
//
// The controller owns the shared graph Laplacian Δ, two `Section<f64>` (u=φ
// displacement, v=φ̇ velocity) over its node_order, and a `WaveStepper`.
// `enable_sim(true)` (re)builds Δ + zeroes u/v;
// `kick` injects a velocity impulse; `step` advances the wave on Δ and re-bakes
// φ→vcol into the SW scene WITHOUT mutating the graph; an edit re-projects φ onto
// the new node_order (survivors keep φ, new nodes start at 0). These two cases
// pin: (1) the field evolves deterministically to a non-zero state after a kick +
// many steps, and (2) an edit preserves a survivor's φ.
//
// The 1-skeleton Δ is over the scene's *Mesh* nodes joined by `Adjacent` edges
// (aleph.sheaf::OneSkeleton), so the two spheres below must carry an Adjacent
// edge to be coupled (and to both appear in node_order). Each Mesh also needs a
// `References→Material` edge or lowering is a DanglingReference (the controller's
// derived state would be empty) — we wire full materials, mirroring
// tests/edit/test_build_sw.cpp.

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Vec3;

namespace {

// Two sphere Meshes (each with a Lambertian Material) under a root Transform with
// a Camera, joined by an Adjacent edge so both are vertices of the wave Laplacian.
struct AB {
    Graph  g;
    NodeId root{}, cam{}, a{}, a_mat{}, b{}, b_mat{};
};

AB make_ab() {
    AB s;
    Graph& g = s.g;

    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, LocalTransform{aleph::math::Mat4::identity()}});

    s.cam = g.alloc_node_id();
    Camera c{s.cam, std::string("sensor0")};
    c.look_from = Vec3{0, 1, 5};
    c.look_at   = Vec3{0, 0, 0};
    c.up        = Vec3{0, 1, 0};
    c.vfov_deg  = 45.0f;
    g.insert_node(std::move(c));

    s.a = g.alloc_node_id();
    Mesh ma{s.a, std::string("A"), 0};
    ma.geometry = SphereLocal{Vec3{0, 0, 0}, 0.4f};
    g.insert_node(std::move(ma));

    s.a_mat = g.alloc_node_id();
    Material amat{s.a_mat, MaterialKind::Lambertian};
    amat.albedo = Vec3{1.0f, 0.0f, 0.0f};
    amat.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(amat));

    s.b = g.alloc_node_id();
    Mesh mb{s.b, std::string("B"), 0};
    mb.geometry = SphereLocal{Vec3{1, 0, 0}, 0.4f};
    g.insert_node(std::move(mb));

    s.b_mat = g.alloc_node_id();
    Material bmat{s.b_mat, MaterialKind::Lambertian};
    bmat.albedo = Vec3{0.0f, 1.0f, 0.0f};
    bmat.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(bmat));

    (void)g.add_edge(EdgeKind::Contains,   s.root, s.cam);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.a);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.b);
    (void)g.add_edge(EdgeKind::References, s.a,    s.a_mat);
    (void)g.add_edge(EdgeKind::References, s.b,    s.b_mat);
    (void)g.add_edge(EdgeKind::Adjacent,   s.a,    s.b);
    return s;
}

}  // namespace

TEST_CASE("EditorController sim: enable + step evolves φ deterministically") {
    AB s = make_ab();
    const NodeId a = s.a;
    aleph::edit::EditorController ctl{std::move(s.g)};
    ctl.set_viewport(64, 48);

    ctl.enable_sim(true);
    REQUIRE(ctl.displacement().size() >= 2);   // both spheres are Δ vertices

    REQUIRE(ctl.kick(a, 1.0));                  // velocity impulse at sphere A
    for (int i = 0; i < 50; ++i)
        REQUIRE(ctl.step(0.01f).has_value());   // the graph is never mutated

    bool any_nonzero = false;
    for (double v : ctl.displacement().data)
        if (v != 0.0) any_nonzero = true;
    CHECK(any_nonzero);                         // the wave propagated into φ
}

TEST_CASE("EditorController sim: edit re-projection keeps survivors, zeros new") {
    AB s = make_ab();
    const NodeId a = s.a, root = s.root;
    aleph::edit::EditorController ctl{std::move(s.g)};
    ctl.set_viewport(64, 48);

    ctl.enable_sim(true);
    REQUIRE(ctl.kick(a, 1.0));
    for (int i = 0; i < 20; ++i)
        REQUIRE(ctl.step(0.01f).has_value());

    auto phi_of = [&](NodeId id) -> double {
        const auto& f = ctl.displacement();
        for (std::size_t i = 0; i < f.order.size(); ++i)
            if (f.order[i] == id) return f.data[i];
        return 0.0;
    };
    const double phiA_before = phi_of(a);

    // An edit (AddObject) re-derives Δ + re-projects φ; survivor `a` keeps its φ.
    aleph::lowering::AddObject add{};
    add.parent   = root;
    add.geometry = SphereLocal{Vec3{2, 0, 0}, 0.4f};
    add.material = aleph::lowering::MaterialParams{};
    auto r = ctl.apply(aleph::lowering::Op{add});
    REQUIRE(r.has_value());

    CHECK(phi_of(a) == doctest::Approx(phiA_before));  // survivor unchanged
}

TEST_CASE("EditorController sim: cascading DeleteObject re-projects φ; survivor keeps it") {
    AB s = make_ab();
    const NodeId a = s.a, b = s.b, root = s.root;
    aleph::edit::EditorController ctl{std::move(s.g)};
    ctl.set_viewport(64, 48);

    ctl.enable_sim(true);
    REQUIRE(ctl.kick(a, 1.0));
    for (int i = 0; i < 20; ++i)
        REQUIRE(ctl.step(0.01f).has_value());

    auto phi_of = [&](NodeId id) -> double {
        const auto& f = ctl.displacement();
        for (std::size_t i = 0; i < f.order.size(); ++i)
            if (f.order[i] == id) return f.data[i];
        return 0.0;
    };
    const double phiA_before = phi_of(a);

    const std::size_t nodes0 = ctl.graph().node_count();
    const std::size_t edges0 = ctl.graph().edge_count();

    // AddObject (mints Transform + Mesh + Material) ...
    aleph::lowering::AddObject add{};
    add.parent   = root;
    add.geometry = SphereLocal{Vec3{2, 0, 0}, 0.4f};
    add.material = aleph::lowering::MaterialParams{};
    REQUIRE(ctl.apply(aleph::lowering::Op{add}).has_value());

    // ... find the minted Mesh (the only Mesh that is neither A nor B) ...
    NodeId minted{};
    bool found = false;
    for (auto [nid, n] : ctl.graph().nodes()) {
        if (kind_of(n) == NodeKind::Mesh && nid != a && nid != b) {
            minted = nid;
            found  = true;
        }
    }
    REQUIRE(found);

    // ... and delete it: the CASCADE reclaims all 3 minted nodes (the record
    // now carries extra deleted Contains/References edges — these must stay
    // inert to the localized wave-operator rebuild and to Section::reproject).
    REQUIRE(ctl.apply(
        aleph::lowering::Op{aleph::lowering::DeleteObject{minted}}).has_value());
    CHECK(ctl.graph().node_count() == nodes0);
    CHECK(ctl.graph().edge_count() == edges0);

    // Survivor A kept its displacement through BOTH re-projections.
    CHECK(phi_of(a) == doctest::Approx(phiA_before));

    // And the sim still steps after the cascading delete.
    REQUIRE(ctl.step(0.01f).has_value());
}

TEST_CASE("EditorController sim: undo/redo history jumps rebuild wave operator") {
    AB s = make_ab();
    const NodeId root = s.root;
    aleph::edit::EditorController ctl{std::move(s.g)};
    ctl.set_viewport(64, 48);

    ctl.enable_sim(true);
    const std::size_t base_nodes = ctl.graph().node_count();
    const std::size_t base_order = ctl.wave_operator().node_order.size();
    REQUIRE(base_order == 2);

    aleph::lowering::AddObject add{};
    add.parent   = root;
    add.geometry = SphereLocal{Vec3{2, 0, 0}, 0.4f};
    add.material = aleph::lowering::MaterialParams{};
    REQUIRE(ctl.apply(aleph::lowering::Op{add}).has_value());
    CHECK(ctl.graph().node_count() > base_nodes);
    CHECK(ctl.can_undo());

    REQUIRE(ctl.undo());
    CHECK(ctl.graph().node_count() == base_nodes);
    CHECK(ctl.wave_operator().node_order.size() == base_order);
    REQUIRE(ctl.step(0.01f).has_value());

    REQUIRE(ctl.redo());
    CHECK(ctl.graph().node_count() > base_nodes);
    CHECK(ctl.wave_operator().node_order.size() > base_order);
    REQUIRE(ctl.step(0.01f).has_value());
}

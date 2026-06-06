#include "doctest.h"

#include <cstddef>
#include <string>
#include <tuple>
#include <utility>   // std::move (Graph / Op are move-only)
#include <vector>

import aleph.edit;       // EditorController (headless core under test)
import aleph.graph;      // Graph
import aleph.types;      // NodeId, EdgeId, Mesh/Material/Camera/Transform, geometry
import aleph.math;       // Vec3, Mat4
import aleph.containers; // FlatSet
import aleph.sheaf;      // decompose_rewrite, mayer_vietoris_certify_with, SheafKind
import aleph.flow;       // build_laplacian_bounded, default_weight, WeightedLaplacian
import aleph.lowering;   // AddObject, DeleteObject, Op, MaterialParams

// Phase 6 / physics slice 3 — `EditorController` on the BOUNDED localized Δ.
//
// Tasks 1+2 made `aleph::flow::build_laplacian_bounded` (bounded-support κ_R) and
// `build_laplacian_local` (byte-EXACT localized recompute). Task 3 wires them into
// the controller: `enable_sim` builds the bounded operator; a structural edit
// (AddObject / DeleteObject) rebuilds it via the localized path (recompute only the
// 2-hop dirty κ_R edges, cache the rest). These cases certify the wiring:
//
//   (1) after a controller AddObject, the controller's wave operator matrix is
//       BYTE-IDENTICAL to a fresh `build_laplacian_bounded(controller.graph())`
//       — the localized rebuild reproduces the full bounded operator bit-for-bit;
//   (2) the Mayer-Vietoris Tier-2 certificate closes (residual == 0) for the same
//       rewrite under the Visibility sheaf;
//   (3) a controller DeleteObject on a lattice INTERIOR node (which deletes its
//       Adjacent edges) ALSO produces the byte-identical bounded operator and
//       reports an O(touched) ≪ |E| recompute count — the localized delete.

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Vec3;
using aleph::math::Mat4;

namespace {

// An R×R lattice of sphere Meshes (each with a Lambertian Material) under a root
// Transform with a Camera, joined by 4-neighbourhood Adjacent edges — the wave
// Laplacian's 1-skeleton. Mirrors apps/aleph_edit's build_lattice_graph but in
// test scope.
struct Lattice {
    Graph               g;
    std::vector<NodeId> nodes;  // row-major Mesh ids: nodes[z*R + x]
    NodeId              root{};
};

Lattice make_lattice(int R) {
    Lattice s;
    Graph& g = s.g;

    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, LocalTransform{Mat4::identity()}});

    const NodeId cam = g.alloc_node_id();
    Camera c{cam, std::string("sensor0")};
    c.look_from = Vec3{0, 1, 5};
    c.look_at   = Vec3{0, 0, 0};
    c.up        = Vec3{0, 1, 0};
    c.vfov_deg  = 45.0f;
    g.insert_node(std::move(c));
    (void)g.add_edge(EdgeKind::Contains, s.root, cam);

    std::vector<std::vector<NodeId>> grid(
        static_cast<std::size_t>(R), std::vector<NodeId>(static_cast<std::size_t>(R)));
    for (int z = 0; z < R; ++z) {
        for (int x = 0; x < R; ++x) {
            const NodeId m = g.alloc_node_id();
            Mesh mesh{m, std::string("n") + std::to_string(z * R + x), 0};
            mesh.geometry = SphereLocal{Vec3{static_cast<float>(x), 0.0f,
                                             static_cast<float>(z)}, 0.35f};
            g.insert_node(std::move(mesh));

            const NodeId mat = g.alloc_node_id();
            Material mt{mat, MaterialKind::Lambertian};
            mt.albedo = Vec3{0.8f, 0.8f, 0.8f};
            mt.emit   = Vec3{0, 0, 0};
            g.insert_node(std::move(mt));

            (void)g.add_edge(EdgeKind::Contains,   s.root, m);
            (void)g.add_edge(EdgeKind::References, m,      mat);
            grid[static_cast<std::size_t>(z)][static_cast<std::size_t>(x)] = m;
            s.nodes.push_back(m);
        }
    }
    for (int z = 0; z < R; ++z) {
        for (int x = 0; x < R; ++x) {
            const NodeId here = grid[static_cast<std::size_t>(z)][static_cast<std::size_t>(x)];
            if (x + 1 < R) {
                (void)g.add_edge(EdgeKind::Adjacent, here,
                                 grid[static_cast<std::size_t>(z)][static_cast<std::size_t>(x + 1)]);
            }
            if (z + 1 < R) {
                (void)g.add_edge(EdgeKind::Adjacent, here,
                                 grid[static_cast<std::size_t>(z + 1)][static_cast<std::size_t>(x)]);
            }
        }
    }
    return s;
}

// `preserved` for decompose_rewrite = after-graph node ids minus the ids created
// by the rewrite (created = after \ before). Mirrors the controller's own
// preserved derivation (current ids minus created).
aleph::containers::FlatSet<NodeId>
preserved_of(const Graph& before, const Graph& after) {
    aleph::containers::FlatSet<NodeId> before_ids;
    for (auto [id, n] : before.nodes()) { (void)n; before_ids.insert(id); }
    aleph::containers::FlatSet<NodeId> preserved;
    for (auto [id, n] : after.nodes()) {
        (void)n;
        if (before_ids.contains(id)) preserved.insert(id);
    }
    return preserved;
}

// Assert the controller's live wave operator matrix equals a fresh full bounded
// rebuild of its current graph, BIT-FOR-BIT (the localized rebuild reproduced the
// full bounded operator exactly).
void check_operator_is_full_bounded(const aleph::edit::EditorController& ctl) {
    const aleph::flow::WeightedLaplacian full =
        aleph::flow::build_laplacian_bounded(ctl.graph(), aleph::flow::default_weight);
    const aleph::flow::WeightedLaplacian& got = ctl.wave_operator();

    REQUIRE(got.node_order == full.node_order);
    REQUIRE(got.matrix.rows() == full.matrix.rows());
    REQUIRE(got.matrix.cols() == full.matrix.cols());
    for (std::size_t i = 0; i < full.matrix.rows(); ++i) {
        for (std::size_t j = 0; j < full.matrix.cols(); ++j) {
            CHECK(got.matrix.at(i, j) == full.matrix.at(i, j));  // == : bit-exact
        }
    }
}

}  // namespace

TEST_CASE("mv-controller: AddObject -> operator == full bounded (byte-exact) + MV closes") {
    Lattice s = make_lattice(4);
    const NodeId root = s.root;
    aleph::edit::EditorController ctl{std::move(s.g)};
    ctl.set_viewport(64, 48);
    ctl.enable_sim(true);

    // Snapshot g_before for the Mayer-Vietoris decomposition.
    const Graph before = ctl.graph().clone();

    aleph::lowering::AddObject add{};
    add.parent   = root;
    add.geometry = SphereLocal{Vec3{2, 0, 2}, 0.35f};
    add.material = aleph::lowering::MaterialParams{};
    auto r = ctl.apply(aleph::lowering::Op{add});
    REQUIRE(r.has_value());

    // (1) The localized rebuild is byte-identical to the full bounded operator.
    check_operator_is_full_bounded(ctl);

    // (2) Mayer-Vietoris Tier-2 certificate closes for this rewrite.
    const aleph::containers::FlatSet<NodeId> preserved =
        preserved_of(before, ctl.graph());
    auto [u, k, rsub] = aleph::sheaf::decompose_rewrite(before, ctl.graph(), preserved);
    const aleph::sheaf::MayerVietorisCertificate cert =
        aleph::sheaf::mayer_vietoris_certify_with(
            ctl.graph(), u, rsub, k, aleph::sheaf::SheafKind::Visibility);
    CHECK(cert.residual == 0);
}

TEST_CASE("mv-controller: DeleteObject (interior lattice node) -> byte-exact + O(touched) recompute") {
    constexpr int R = 12;  // large enough that a local 2-hop ball is ≪ |E|
    Lattice s = make_lattice(R);
    // An interior mesh: full 4-neighbourhood, so its delete removes 4 Adjacent
    // edges and dirties only the local 2-hop ball — the localized delete.
    const NodeId victim = s.nodes[static_cast<std::size_t>((R / 2) * R + (R / 2))];

    aleph::edit::EditorController ctl{std::move(s.g)};
    ctl.set_viewport(64, 48);
    ctl.enable_sim(true);

    const std::size_t edges_before = ctl.wave_operator().curvatures.size();

    const Graph before = ctl.graph().clone();

    auto r = ctl.apply(aleph::lowering::Op{aleph::lowering::DeleteObject{victim}});
    REQUIRE(r.has_value());

    // Byte-exact to the full bounded rebuild of the post-delete graph.
    check_operator_is_full_bounded(ctl);

    // The localized win: recompute count is O(touched) and strictly < |E|.
    const int rc = ctl.last_recompute_count();
    CHECK(rc > 0);                                              // the delete dirtied edges
    CHECK(rc < static_cast<int>(edges_before));                // ≪ |E| — not a full rebuild

    // Mayer-Vietoris Tier-2 still closes for the delete rewrite.
    const aleph::containers::FlatSet<NodeId> preserved =
        preserved_of(before, ctl.graph());
    auto [u, k, rsub] = aleph::sheaf::decompose_rewrite(before, ctl.graph(), preserved);
    const aleph::sheaf::MayerVietorisCertificate cert =
        aleph::sheaf::mayer_vietoris_certify_with(
            ctl.graph(), u, rsub, k, aleph::sheaf::SheafKind::Visibility);
    CHECK(cert.residual == 0);
}

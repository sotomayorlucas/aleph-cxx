// Scaling bench: localized bounded-κ_R Laplacian rebuild vs full rebuild under
// a DPO-style edit trace, plus the global (all-pairs) build and the
// Mayer-Vietoris certificate cost. One CSV row per measurement on stdout;
// human-readable progress on stderr. This produces the paper's evaluation
// curves (O(touched) vs O(|E|) per edit; certificate overhead; gate predictor
// dirty/|E|).
//
//   aleph_bench_scaling [--reps K] [--max-grid R] [--cert-max-grid R]
//                       [--global-max-grid R]
//
// The GLOBAL build_laplacian series (all-pairs build_state; the W_1 support is
// the whole component, so each edge pays a dense n x n transport slice) blows
// up fast — minutes at grid 12. It gets its own cap (--global-max-grid,
// default 12) and a single timed run: a few points suffice to plot the blowup
// the bounded/local operator avoids.
//
// Grids are R x R 4-neighbour lattices of Mesh nodes joined by Adjacent edges
// (the same construction tests/flow/test_mv_localization.cpp gates on). Per
// grid the edit trace mirrors the editor's ops, threading `prev` through every
// step exactly like EditorController::rebuild_operator_localized:
//   add1 (1-edge add, interior) -> add2 (2-edge add) -> add_corner -> delete.
//
// Timings are medians of `reps` runs (steady_clock). The bit-exactness of
// local vs full is asserted on every edit; a mismatch makes the bench exit 1,
// so the numbers can never describe a broken localization.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

import aleph.flow;
import aleph.graph;
import aleph.types;
import aleph.sheaf;
import aleph.containers;
import aleph.math;

using aleph::flow::build_laplacian;
using aleph::flow::build_laplacian_bounded;
using aleph::flow::build_laplacian_local;
using aleph::flow::default_weight;
using aleph::flow::two_hop_touched_edges;
using aleph::flow::WeightedLaplacian;
using aleph::graph::Graph;
using aleph::math::f64;
using aleph::sheaf::OneSkeleton;
using aleph::types::EdgeKind;
using aleph::types::Mesh;
using aleph::types::NodeId;

namespace {

struct Grid {
    Graph                            g;
    std::vector<std::vector<NodeId>> ids;  // ids[r][c]
};

Grid make_grid(std::size_t R) {
    Grid grid;
    grid.ids.assign(R, std::vector<NodeId>(R));
    for (std::size_t i = 0; i < R; ++i) {
        for (std::size_t j = 0; j < R; ++j) {
            const NodeId id = grid.g.alloc_node_id();
            grid.g.insert_node(Mesh{
                id,
                std::string("m") + std::to_string(i) + "_" + std::to_string(j),
                1});
            grid.ids[i][j] = id;
        }
    }
    for (std::size_t i = 0; i < R; ++i) {
        for (std::size_t j = 0; j < R; ++j) {
            if (j + 1 < R
                && !grid.g
                        .add_edge(EdgeKind::Adjacent, grid.ids[i][j],
                                  grid.ids[i][j + 1])
                        .has_value()) {
                std::fprintf(stderr, "add_edge failed\n");
                std::exit(1);
            }
            if (i + 1 < R
                && !grid.g
                        .add_edge(EdgeKind::Adjacent, grid.ids[i][j],
                                  grid.ids[i + 1][j])
                        .has_value()) {
                std::fprintf(stderr, "add_edge failed\n");
                std::exit(1);
            }
        }
    }
    return grid;
}

NodeId add_object(Graph& g, const std::vector<NodeId>& to) {
    const NodeId c = g.alloc_node_id();
    g.insert_node(Mesh{c, std::string("add") + std::to_string(c.value), 1});
    for (const NodeId t : to) {
        if (!g.add_edge(EdgeKind::Adjacent, c, t).has_value()) {
            std::fprintf(stderr, "add_edge failed\n");
            std::exit(1);
        }
    }
    return c;
}

double now_ms() {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Median-of-reps timing of `fn`, with one unmeasured warm-up run. `fn` must
// return something dependent on the computation (escaped into `sink`).
double median_ms(int reps, double& sink, auto&& fn) {
    (void)fn(sink);
    std::vector<double> t;
    t.reserve(static_cast<std::size_t>(reps));
    for (int i = 0; i < reps; ++i) {
        const double t0 = now_ms();
        fn(sink);
        t.push_back(now_ms() - t0);
    }
    std::sort(t.begin(), t.end());
    return t[t.size() / 2];
}

bool bit_exact(const WeightedLaplacian& a, const WeightedLaplacian& b) {
    if (a.node_order != b.node_order) return false;
    if (a.matrix.rows() != b.matrix.rows()
        || a.matrix.cols() != b.matrix.cols()) return false;
    for (std::size_t i = 0; i < a.matrix.rows(); ++i)
        for (std::size_t j = 0; j < a.matrix.cols(); ++j)
            if (a.matrix.at(i, j) != b.matrix.at(i, j)) return false;
    if (a.curvatures.size() != b.curvatures.size()) return false;
    for (const auto& [e, kb] : b.curvatures) {
        const f64* ka = a.curvatures.get(e);
        if (ka == nullptr || *ka != kb) return false;
    }
    return true;
}

struct EditRow {
    const char* name;
    std::vector<NodeId> seed;
    Graph before;  // clone captured before the edit (for decompose_rewrite)
    std::vector<NodeId> created;  // nodes created by the edit (empty for delete)
};

bool g_all_exact = true;

// Measure one edit already applied to `g`: local-vs-full rebuild + optional MV
// certificate. Returns the timed full rebuild to thread as the next `prev`.
WeightedLaplacian measure_edit(std::size_t R, const Grid& /*grid*/, Graph& g,
                               const WeightedLaplacian& prev, const EditRow& e,
                               int reps, bool with_cert) {
    const std::size_t nodes = g.node_count();

    double sink = 0.0;

    // Full bounded rebuild (what the fallback gate pays).
    WeightedLaplacian full = build_laplacian_bounded(g, default_weight);
    const double t_full = median_ms(reps, sink, [&](double& s) {
        const WeightedLaplacian w = build_laplacian_bounded(g, default_weight);
        s += w.matrix.at(0, 0);
        return s;
    });

    // Localized rebuild, END-TO-END: skeleton + 2-hop cover + local build (the
    // same pipeline rebuild_operator_localized runs per edit).
    int  rc    = 0;
    const OneSkeleton skel  = OneSkeleton::from_graph(g);
    const auto        dirty = two_hop_touched_edges(skel, e.seed);
    WeightedLaplacian local =
        build_laplacian_local(g, prev, dirty, default_weight, &rc);
    const double t_local = median_ms(reps, sink, [&](double& s) {
        const OneSkeleton sk = OneSkeleton::from_graph(g);
        const auto        d  = two_hop_touched_edges(sk, e.seed);
        int               r  = 0;
        const WeightedLaplacian w =
            build_laplacian_local(g, prev, d, default_weight, &r);
        s += w.matrix.at(0, 0);
        return s;
    });

    const bool exact = bit_exact(local, full);
    if (!exact) g_all_exact = false;

    // Mayer-Vietoris certificate (Tier-2): decompose the rewrite and certify
    // over the visibility sheaf, as tests/edit/test_mv_controller.cpp does.
    double t_cert    = -1.0;
    int    residual  = 0;
    if (with_cert) {
        aleph::containers::FlatSet<NodeId> preserved;
        for (auto [id, node] : g.nodes()) {
            (void)node;
            bool created = false;
            for (NodeId c : e.created)
                if (c == id) { created = true; break; }
            if (!created) preserved.insert(id);
        }
        auto [u, k, r] = aleph::sheaf::decompose_rewrite(e.before, g, preserved);
        const double t0 = now_ms();
        const auto cert = aleph::sheaf::mayer_vietoris_certify_with(
            g, u, r, k, aleph::sheaf::SheafKind::Visibility);
        t_cert   = now_ms() - t0;
        residual = cert.residual;
    }

    const std::size_t edges = skel.edges.size();
    const double dirty_frac =
        edges == 0 ? 0.0
                   : static_cast<double>(dirty.size()) / static_cast<double>(edges);
    std::printf("%zu,%zu,%zu,%s,%zu,%zu,%d,%.4f,,%.4f,%.4f,%.2f,%d,",
                R, nodes, edges, e.name, e.seed.size(), dirty.size(), rc,
                dirty_frac, t_full, t_local,
                t_local > 0.0 ? t_full / t_local : 0.0, exact ? 1 : 0);
    if (with_cert) std::printf("%d,%.4f\n", residual, t_cert);
    else           std::printf(",\n");
    std::fprintf(stderr,
                 "  grid=%zu %-10s dirty=%zu/%zu rc=%d full=%.2fms local=%.2fms "
                 "x%.1f exact=%d%s\n",
                 R, e.name, dirty.size(), edges, rc, t_full, t_local,
                 t_local > 0.0 ? t_full / t_local : 0.0, exact ? 1 : 0,
                 with_cert ? " +cert" : "");
    (void)sink;
    return full;
}

}  // namespace

int main(int argc, char** argv) {
    int         reps            = 5;
    std::size_t max_grid        = 64;
    std::size_t cert_max_grid   = 24;
    std::size_t global_max_grid = 12;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--reps") == 0 && i + 1 < argc)
            reps = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--max-grid") == 0 && i + 1 < argc)
            max_grid = static_cast<std::size_t>(std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "--cert-max-grid") == 0 && i + 1 < argc)
            cert_max_grid = static_cast<std::size_t>(std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "--global-max-grid") == 0 && i + 1 < argc)
            global_max_grid = static_cast<std::size_t>(std::atoi(argv[++i]));
        else {
            std::fprintf(stderr,
                         "usage: %s [--reps K] [--max-grid R] "
                         "[--cert-max-grid R] [--global-max-grid R]\n", argv[0]);
            return 2;
        }
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);  // row-by-row output even when piped

    std::printf(
        "grid,nodes,edges,edit,seeds,dirty,recomputed,dirty_frac,"
        "t_global_ms,t_full_ms,t_local_ms,speedup,bit_exact,"
        "cert_residual,t_cert_ms\n");

    const std::size_t sizes[] = {8, 12, 16, 24, 32, 48, 64};
    for (const std::size_t R : sizes) {
        if (R > max_grid) break;
        std::fprintf(stderr, "grid %zux%zu...\n", R, R);
        Grid grid = make_grid(R);

        double sink = 0.0;
        // Initial full builds: bounded (the editor/sim operator) and GLOBAL
        // (all-pairs build_state; the lowering::importance path) — the global
        // one is the O(n^2)-BFS curve.
        const double t_bounded0 = median_ms(reps, sink, [&](double& s) {
            const WeightedLaplacian w =
                build_laplacian_bounded(grid.g, default_weight);
            s += w.matrix.at(0, 0);
            return s;
        });
        // Global build: single timed run, no warm-up (it is the slow series).
        double t_global0 = -1.0;
        if (R <= global_max_grid) {
            const double t0 = now_ms();
            const WeightedLaplacian w = build_laplacian(grid.g, default_weight);
            t_global0 = now_ms() - t0;
            sink += w.matrix.at(0, 0);
        }
        WeightedLaplacian prev = build_laplacian_bounded(grid.g, default_weight);
        const OneSkeleton skel0 = OneSkeleton::from_graph(grid.g);
        if (t_global0 >= 0.0)
            std::printf("%zu,%zu,%zu,initial,,,,,%.4f,%.4f,,,,,\n", R,
                        R * R, skel0.edges.size(), t_global0, t_bounded0);
        else
            std::printf("%zu,%zu,%zu,initial,,,,,,%.4f,,,,,\n", R,
                        R * R, skel0.edges.size(), t_bounded0);
        std::fprintf(stderr,
                     "  grid=%zu initial    |E|=%zu global=%.2fms bounded=%.2fms\n",
                     R, skel0.edges.size(), t_global0, t_bounded0);

        const bool with_cert = R <= cert_max_grid;
        const std::size_t mid = R / 2;

        // add1: 1-edge add on an interior node.
        {
            EditRow e{.name = "add1", .seed = {}, .before = grid.g.clone(),
                      .created = {}};
            const NodeId to = grid.ids[mid][mid];
            const NodeId c  = add_object(grid.g, {to});
            e.seed    = {c, to};
            e.created = {c};
            prev = measure_edit(R, grid, grid.g, prev, e, reps, with_cert);
        }
        // add2: 2-edge add spanning two interior nodes.
        NodeId c2{};
        {
            EditRow e{.name = "add2", .seed = {}, .before = grid.g.clone(),
                      .created = {}};
            const NodeId t1 = grid.ids[mid][mid - 1];
            const NodeId t2 = grid.ids[mid - 1][mid];
            c2              = add_object(grid.g, {t1, t2});
            e.seed    = {c2, t1, t2};
            e.created = {c2};
            prev = measure_edit(R, grid, grid.g, prev, e, reps, with_cert);
        }
        // add_corner: 1-edge add at the lattice corner (smallest ball).
        {
            EditRow e{.name = "add_corner", .seed = {}, .before = grid.g.clone(),
                      .created = {}};
            const NodeId to = grid.ids[0][0];
            const NodeId c  = add_object(grid.g, {to});
            e.seed    = {c, to};
            e.created = {c};
            prev = measure_edit(R, grid, grid.g, prev, e, reps, with_cert);
        }
        // delete: cascade-remove the 2-edge object; seed = surviving old
        // neighbours captured BEFORE the delete.
        {
            EditRow e{.name = "delete", .seed = {}, .before = grid.g.clone(),
                      .created = {}};
            const NodeId t1 = grid.ids[mid][mid - 1];
            const NodeId t2 = grid.ids[mid - 1][mid];
            grid.g.remove_node_cascade(c2);
            e.seed = {t1, t2};
            prev = measure_edit(R, grid, grid.g, prev, e, reps, with_cert);
        }
    }

    if (!g_all_exact) {
        std::fprintf(stderr, "FAIL: a localized rebuild was NOT bit-exact\n");
        return 1;
    }
    return 0;
}

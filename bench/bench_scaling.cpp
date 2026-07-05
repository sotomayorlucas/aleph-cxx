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
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <span>
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
import aleph.io;      // load_obj (--family obj)

using aleph::flow::build_laplacian;
using aleph::flow::build_laplacian_bounded;
using aleph::flow::build_laplacian_bounded_sparse;
using aleph::flow::build_laplacian_local;
using aleph::flow::build_laplacian_local_sparse;
using aleph::flow::default_weight;
using aleph::flow::SparseWeightedLaplacian;
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

// ── Irregular family: face-adjacency graphs of triangle meshes ──────────────
// One Mesh node per triangle; an Adjacent edge whenever two triangles share an
// undirected vertex pair. The scaling series is a topological icosphere
// (midpoint subdivision of the icosahedron — vertex coordinates never matter,
// only connectivity) with a deterministic perforation (every 53rd face
// removed) so the graph has REAL valence irregularity: interior faces have
// degree 3, hole-boundary faces degree 1-2, and the 12 icosahedral
// pentagonal clusters give varying ball shapes. --family obj runs the same
// trace on any user OBJ instead.

struct TriMesh {
    int                             n_verts{0};
    std::vector<std::array<int, 3>> tris;
};

TriMesh icosahedron() {
    TriMesh m;
    m.n_verts = 12;
    m.tris = {{{0, 11, 5}},  {{0, 5, 1}},   {{0, 1, 7}},   {{0, 7, 10}},
              {{0, 10, 11}}, {{1, 5, 9}},   {{5, 11, 4}},  {{11, 10, 2}},
              {{10, 7, 6}},  {{7, 1, 8}},   {{3, 9, 4}},   {{3, 4, 2}},
              {{3, 2, 6}},   {{3, 6, 8}},   {{3, 8, 9}},   {{4, 9, 5}},
              {{2, 4, 11}},  {{6, 2, 10}},  {{8, 6, 7}},   {{9, 8, 1}}};
    return m;
}

TriMesh subdivide(const TriMesh& in) {
    TriMesh out;
    out.n_verts = in.n_verts;
    std::map<std::pair<int, int>, int> mid;  // sorted vertex pair -> midpoint id
    const auto midpoint = [&](int a, int b) {
        const std::pair<int, int> k = a < b ? std::pair{a, b} : std::pair{b, a};
        const auto it = mid.find(k);
        if (it != mid.end()) return it->second;
        const int v = out.n_verts++;
        mid.emplace(k, v);
        return v;
    };
    for (const auto& t : in.tris) {
        const int ab = midpoint(t[0], t[1]);
        const int bc = midpoint(t[1], t[2]);
        const int ca = midpoint(t[2], t[0]);
        out.tris.push_back({t[0], ab, ca});
        out.tris.push_back({t[1], bc, ab});
        out.tris.push_back({t[2], ca, bc});
        out.tris.push_back({ab, bc, ca});
    }
    return out;
}

// Remove every `stride`-th face (deterministic perforation -> boundary faces
// with adjacency degree < 3).
TriMesh perforate(const TriMesh& in, std::size_t stride) {
    TriMesh out;
    out.n_verts = in.n_verts;
    for (std::size_t i = 0; i < in.tris.size(); ++i) {
        if (stride == 0 || i % stride != 0) out.tris.push_back(in.tris[i]);
    }
    return out;
}

struct FaceGraph {
    Graph                            g;
    std::vector<NodeId>              ids;   // ids[face]
    std::vector<std::vector<std::size_t>> nbr;  // face -> adjacent faces
};

FaceGraph face_graph(const TriMesh& m) {
    FaceGraph fg;
    for (std::size_t i = 0; i < m.tris.size(); ++i) {
        const NodeId id = fg.g.alloc_node_id();
        fg.g.insert_node(Mesh{id, std::string("f") + std::to_string(i), 1});
        fg.ids.push_back(id);
    }
    // Undirected tri-edge (sorted vertex pair) -> incident faces.
    std::map<std::pair<int, int>, std::vector<std::size_t>> edge_faces;
    for (std::size_t i = 0; i < m.tris.size(); ++i) {
        const auto& t = m.tris[i];
        for (int k = 0; k < 3; ++k) {
            const int a = t[static_cast<std::size_t>(k)];
            const int b = t[static_cast<std::size_t>((k + 1) % 3)];
            edge_faces[a < b ? std::pair{a, b} : std::pair{b, a}].push_back(i);
        }
    }
    fg.nbr.assign(m.tris.size(), {});
    for (const auto& [e, faces] : edge_faces) {
        (void)e;
        if (faces.size() == 2) {
            const std::size_t i = faces[0], j = faces[1];
            fg.nbr[i].push_back(j);
            fg.nbr[j].push_back(i);
        }
    }
    // Graph edges in deterministic face-index order.
    for (std::size_t i = 0; i < fg.nbr.size(); ++i) {
        for (const std::size_t j : fg.nbr[i]) {
            if (j > i
                && !fg.g.add_edge(EdgeKind::Adjacent, fg.ids[i], fg.ids[j])
                        .has_value()) {
                std::fprintf(stderr, "face add_edge failed\n");
                std::exit(1);
            }
        }
    }
    return fg;
}

TriMesh load_obj_mesh(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(1); }
    std::vector<char> raw((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
    const auto mesh = aleph::io::load_obj(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(raw.data()), raw.size()));
    if (!mesh.has_value()) {
        std::fprintf(stderr, "OBJ parse error: %s\n", mesh.error().c_str());
        std::exit(1);
    }
    TriMesh out;
    out.n_verts = static_cast<int>(mesh->verts.size());
    out.tris    = mesh->tris;
    return out;
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

// Sparse-vs-dense value exactness: every stored entry bitwise (spec 2026-07-04).
bool sparse_matches_dense(const SparseWeightedLaplacian& sp,
                          const WeightedLaplacian&       de) {
    if (sp.node_order != de.node_order) return false;
    const std::size_t n = de.node_order.size();
    if (sp.matrix.rows() != n || sp.matrix.cols() != n) return false;
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j)
            if (sp.matrix.get(i, j) != de.matrix.at(i, j)) return false;
    return true;
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

// Measure one edit already applied to `g`: local-vs-full rebuild (dense AND
// sparse carriers) + optional MV certificate. Returns the timed full rebuilds
// to thread as the next `prev`/`prev_sp`.
struct EditResult {
    WeightedLaplacian       full;
    SparseWeightedLaplacian full_sp;
};

EditResult measure_edit(std::size_t R, Graph& g, const WeightedLaplacian& prev,
                        const SparseWeightedLaplacian& prev_sp,
                        const EditRow& e, int reps, bool with_cert) {
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

    // Sparse carriers: full and end-to-end localized (same pipeline), plus
    // per-row value-exactness vs the dense full build.
    SparseWeightedLaplacian full_sp =
        build_laplacian_bounded_sparse(g, default_weight);
    const double t_full_sp = median_ms(reps, sink, [&](double& s) {
        const SparseWeightedLaplacian w =
            build_laplacian_bounded_sparse(g, default_weight);
        s += w.matrix.get(0, 0);
        return s;
    });
    const double t_local_sp = median_ms(reps, sink, [&](double& s) {
        const OneSkeleton sk = OneSkeleton::from_graph(g);
        const auto        d  = two_hop_touched_edges(sk, e.seed);
        int               r  = 0;
        const SparseWeightedLaplacian w =
            build_laplacian_local_sparse(g, prev_sp, d, default_weight, &r);
        s += w.matrix.get(0, 0);
        return s;
    });
    {
        int rs = 0;
        const SparseWeightedLaplacian local_sp = build_laplacian_local_sparse(
            g, prev_sp, dirty, default_weight, &rs);
        if (!sparse_matches_dense(full_sp, full)
            || !sparse_matches_dense(local_sp, full)) {
            g_all_exact = false;
        }
    }

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
    if (with_cert) std::printf("%d,%.4f", residual, t_cert);
    else           std::printf(",");
    std::printf(",%.4f,%.4f\n", t_full_sp, t_local_sp);
    std::fprintf(stderr,
                 "  grid=%zu %-10s dirty=%zu/%zu rc=%d full=%.2fms local=%.2fms "
                 "x%.1f | sp full=%.2fms local=%.2fms | exact=%d%s\n",
                 R, e.name, dirty.size(), edges, rc, t_full, t_local,
                 t_local > 0.0 ? t_full / t_local : 0.0, t_full_sp, t_local_sp,
                 exact ? 1 : 0, with_cert ? " +cert" : "");
    (void)sink;
    return EditResult{std::move(full), std::move(full_sp)};
}

// One full measurement pass over a face-adjacency graph: initial builds +
// the same 4-edit trace as the lattice family (edit names kept identical so
// the CSV tooling is shared; "add_corner" here attaches at a hole-BOUNDARY
// face, the lowest-degree site). `size_param` lands in the CSV `grid` column
// (subdivision level, or 0 for --family obj). Caps are by NODE COUNT, at
// parity with the lattice caps (global: grid 12 = 144 nodes; cert: grid 24 =
// 576 nodes).
void run_mesh_trace(std::size_t size_param, FaceGraph fg, int reps) {
    const std::size_t n_faces = fg.ids.size();
    if (n_faces < 8) {
        std::fprintf(stderr, "mesh too small (%zu faces), skipping\n", n_faces);
        return;
    }
    double sink = 0.0;
    const double t_bounded0 = median_ms(reps, sink, [&](double& s) {
        const WeightedLaplacian w =
            build_laplacian_bounded(fg.g, default_weight);
        s += w.matrix.at(0, 0);
        return s;
    });
    double t_global0 = -1.0;
    if (n_faces <= 150) {
        const double t0 = now_ms();
        const WeightedLaplacian w = build_laplacian(fg.g, default_weight);
        t_global0 = now_ms() - t0;
        sink += w.matrix.at(0, 0);
    }
    const double t_sp0 = median_ms(reps, sink, [&](double& s) {
        const SparseWeightedLaplacian w =
            build_laplacian_bounded_sparse(fg.g, default_weight);
        s += w.matrix.get(0, 0);
        return s;
    });
    WeightedLaplacian prev = build_laplacian_bounded(fg.g, default_weight);
    SparseWeightedLaplacian prev_sp =
        build_laplacian_bounded_sparse(fg.g, default_weight);
    const OneSkeleton skel0 = OneSkeleton::from_graph(fg.g);
    if (t_global0 >= 0.0)
        std::printf("%zu,%zu,%zu,initial,,,,,%.4f,%.4f,,,,,,%.4f,\n", size_param,
                    n_faces, skel0.edges.size(), t_global0, t_bounded0, t_sp0);
    else
        std::printf("%zu,%zu,%zu,initial,,,,,,%.4f,,,,,,%.4f,\n", size_param,
                    n_faces, skel0.edges.size(), t_bounded0, t_sp0);
    std::fprintf(stderr,
                 "  mesh=%zu initial    |V|=%zu |E|=%zu global=%.2fms bounded=%.2fms\n",
                 size_param, n_faces, skel0.edges.size(), t_global0, t_bounded0);

    const bool with_cert = n_faces <= 600;

    // Trace sites. Interior: a degree-3 face near the middle of the index
    // space; its first neighbour for the 2-edge add; boundary: the first
    // face with adjacency degree < 3 (a perforation edge), falling back to
    // face 0 on closed meshes.
    std::size_t interior = n_faces / 2;
    while (interior < n_faces && fg.nbr[interior].size() < 3) ++interior;
    if (interior >= n_faces) interior = n_faces / 2;
    const std::size_t interior2 = fg.nbr[interior].empty()
                                      ? (interior + 1) % n_faces
                                      : fg.nbr[interior][0];
    std::size_t boundary = 0;
    while (boundary < n_faces && fg.nbr[boundary].size() >= 3) ++boundary;
    if (boundary >= n_faces) boundary = 0;

    // add1: attach a new object to the interior face.
    {
        EditRow e{.name = "add1", .seed = {}, .before = fg.g.clone(),
                  .created = {}};
        const NodeId to = fg.ids[interior];
        const NodeId c  = add_object(fg.g, {to});
        e.seed    = {c, to};
        e.created = {c};
        EditResult res =
            measure_edit(size_param, fg.g, prev, prev_sp, e, reps, with_cert);
        prev    = std::move(res.full);
        prev_sp = std::move(res.full_sp);
    }
    // add2: attach spanning two adjacent faces.
    NodeId c2{};
    {
        EditRow e{.name = "add2", .seed = {}, .before = fg.g.clone(),
                  .created = {}};
        const NodeId t1 = fg.ids[interior];
        const NodeId t2 = fg.ids[interior2];
        c2              = add_object(fg.g, {t1, t2});
        e.seed    = {c2, t1, t2};
        e.created = {c2};
        EditResult res =
            measure_edit(size_param, fg.g, prev, prev_sp, e, reps, with_cert);
        prev    = std::move(res.full);
        prev_sp = std::move(res.full_sp);
    }
    // add_corner: attach at a hole-boundary (lowest-degree) face.
    {
        EditRow e{.name = "add_corner", .seed = {}, .before = fg.g.clone(),
                  .created = {}};
        const NodeId to = fg.ids[boundary];
        const NodeId c  = add_object(fg.g, {to});
        e.seed    = {c, to};
        e.created = {c};
        EditResult res =
            measure_edit(size_param, fg.g, prev, prev_sp, e, reps, with_cert);
        prev    = std::move(res.full);
        prev_sp = std::move(res.full_sp);
    }
    // delete: cascade-remove the 2-edge object; seed = its old neighbours.
    {
        EditRow e{.name = "delete", .seed = {}, .before = fg.g.clone(),
                  .created = {}};
        const NodeId t1 = fg.ids[interior];
        const NodeId t2 = fg.ids[interior2];
        fg.g.remove_node_cascade(c2);
        e.seed = {t1, t2};
        EditResult res =
            measure_edit(size_param, fg.g, prev, prev_sp, e, reps, with_cert);
        prev    = std::move(res.full);
        prev_sp = std::move(res.full_sp);
    }
}

}  // namespace

int main(int argc, char** argv) {
    int         reps            = 5;
    std::size_t max_grid        = 64;
    std::size_t cert_max_grid   = 24;
    std::size_t global_max_grid = 12;
    std::size_t max_level       = 4;    // icosphere subdivision cap (5120 faces)
    const char* family          = "lattice";
    const char* obj_path        = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--reps") == 0 && i + 1 < argc)
            reps = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--max-grid") == 0 && i + 1 < argc)
            max_grid = static_cast<std::size_t>(std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "--cert-max-grid") == 0 && i + 1 < argc)
            cert_max_grid = static_cast<std::size_t>(std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "--global-max-grid") == 0 && i + 1 < argc)
            global_max_grid = static_cast<std::size_t>(std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "--max-level") == 0 && i + 1 < argc)
            max_level = static_cast<std::size_t>(std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "--family") == 0 && i + 1 < argc)
            family = argv[++i];
        else if (std::strcmp(argv[i], "--obj") == 0 && i + 1 < argc) {
            family   = "obj";
            obj_path = argv[++i];
        } else {
            std::fprintf(stderr,
                         "usage: %s [--reps K] [--family lattice|mesh] "
                         "[--obj path.obj] [--max-grid R] [--max-level L] "
                         "[--cert-max-grid R] [--global-max-grid R]\n", argv[0]);
            return 2;
        }
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);  // row-by-row output even when piped

    std::printf(
        "grid,nodes,edges,edit,seeds,dirty,recomputed,dirty_frac,"
        "t_global_ms,t_full_ms,t_local_ms,speedup,bit_exact,"
        "cert_residual,t_cert_ms,t_full_sp_ms,t_local_sp_ms\n");

    if (std::strcmp(family, "mesh") == 0) {
        // Icosphere levels 1..max_level, perforated at the final level for
        // valence irregularity (see the family comment above face_graph).
        TriMesh m = icosahedron();
        for (std::size_t level = 1; level <= max_level; ++level) {
            m = subdivide(m);
            std::fprintf(stderr, "icosphere level %zu (%zu faces)...\n", level,
                         m.tris.size());
            run_mesh_trace(level, face_graph(perforate(m, 53)), reps);
        }
        return g_all_exact ? 0 : 1;
    }
    if (std::strcmp(family, "obj") == 0) {
        if (obj_path == nullptr) {
            std::fprintf(stderr, "--family obj requires --obj path\n");
            return 2;
        }
        std::fprintf(stderr, "obj %s...\n", obj_path);
        run_mesh_trace(0, face_graph(load_obj_mesh(obj_path)), reps);
        return g_all_exact ? 0 : 1;
    }

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
        const double t_sp0 = median_ms(reps, sink, [&](double& s2) {
            const SparseWeightedLaplacian w =
                build_laplacian_bounded_sparse(grid.g, default_weight);
            s2 += w.matrix.get(0, 0);
            return s2;
        });
        WeightedLaplacian prev = build_laplacian_bounded(grid.g, default_weight);
        SparseWeightedLaplacian prev_sp =
            build_laplacian_bounded_sparse(grid.g, default_weight);
        const OneSkeleton skel0 = OneSkeleton::from_graph(grid.g);
        if (t_global0 >= 0.0)
            std::printf("%zu,%zu,%zu,initial,,,,,%.4f,%.4f,,,,,,%.4f,\n", R,
                        R * R, skel0.edges.size(), t_global0, t_bounded0, t_sp0);
        else
            std::printf("%zu,%zu,%zu,initial,,,,,,%.4f,,,,,,%.4f,\n", R,
                        R * R, skel0.edges.size(), t_bounded0, t_sp0);
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
            EditResult res =
                measure_edit(R, grid.g, prev, prev_sp, e, reps, with_cert);
            prev    = std::move(res.full);
            prev_sp = std::move(res.full_sp);
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
            EditResult res =
                measure_edit(R, grid.g, prev, prev_sp, e, reps, with_cert);
            prev    = std::move(res.full);
            prev_sp = std::move(res.full_sp);
        }
        // add_corner: 1-edge add at the lattice corner (smallest ball).
        {
            EditRow e{.name = "add_corner", .seed = {}, .before = grid.g.clone(),
                      .created = {}};
            const NodeId to = grid.ids[0][0];
            const NodeId c  = add_object(grid.g, {to});
            e.seed    = {c, to};
            e.created = {c};
            EditResult res =
                measure_edit(R, grid.g, prev, prev_sp, e, reps, with_cert);
            prev    = std::move(res.full);
            prev_sp = std::move(res.full_sp);
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
            EditResult res =
                measure_edit(R, grid.g, prev, prev_sp, e, reps, with_cert);
            prev    = std::move(res.full);
            prev_sp = std::move(res.full_sp);
        }
    }

    if (!g_all_exact) {
        std::fprintf(stderr, "FAIL: a localized rebuild was NOT bit-exact\n");
        return 1;
    }
    return 0;
}

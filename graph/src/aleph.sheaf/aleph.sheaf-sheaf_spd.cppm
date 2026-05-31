// SPD-valued constant sheaf — generalisation of `ConstantZ2Sheaf` from GF(2)
// to R^3 coefficients (the Jakob-Hanika sigmoid coefficient space). Sections
// are `[f32; 3]` triples; the cochain complex over R has H^k computed via a
// dense f64 Householder QR (M18.2.3).
//
// Ported from aleph-engine/aleph-sheaf/src/spd_sheaf.rs.
//
// Adaptation notes (vs. the Rust reference):
//   * `[f32; 3]` -> `std::array<float, 3>` (the partition operates on opaque
//     triples to stay render-free; per the 4c plan we use float[3]/Vec3-style
//     triples rather than pulling in aleph-render). std::array is copyable,
//     unlike the move-only OrderedMap, so generators/sections clone freely.
//   * Rust `IndexMap<NodeId, [f32;3]>` for `per_cell`/`build_per_cell` becomes
//     `aleph::containers::OrderedMap<NodeId, Section>` (move-only but only read
//     via `get` here, so a const& parameter suffices); insertion order is not
//     load-bearing for `per_cell` because each section is fetched by NodeId.
//   * The complex's vertex order is `FlagComplex::simplices[0]` which the C++
//     `:flag_complex` keeps sorted ascending by `SimplexLess` (the Rust used
//     IndexSet insertion order). All oracle values exercised over this port are
//     order-independent (component count, H^1 dim, multiset of sections,
//     pointwise delta, bit-stable determinism), so the result set matches.
//   * Programmer-error guards (edge endpoint missing from the vertex index)
//     use assert, matching the no-exceptions ISA build (aleph_flags_isa).
//   * The Householder QR is ported verbatim: sign choice `alpha = -alpha` when
//     `a(k,k) > 0`, `tau = 2 / vᵀv`, and the `1e-300` degenerate guards on
//     `|alpha|` and `vᵀv`.

module;
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

export module aleph.sheaf:sheaf_spd;

import aleph.graph;
import aleph.types;
import aleph.containers;
import :simplex;
import :skeleton;
import :flag_complex;

export namespace aleph::sheaf {

using aleph::types::NodeId;

// One SPD coefficient section: a `[f32; 3]` triple. Opaque to this partition
// (it carries the Jakob-Hanika sigmoid coefficients in aleph-render's usage).
using Section = std::array<float, 3>;

// SPD-valued constant sheaf. The section over each 0-cell (vertex) is a
// `Section` triple. `sections[i]` corresponds to the i-th vertex in
// `complex.simplices[0]` iteration order (sorted ascending here).
struct SpdSheaf {
    FlagComplex          complex;
    std::vector<Section> sections;  // one per 0-cell, aligned to simplices[0]

    [[nodiscard]] const FlagComplex& get_complex() const noexcept { return complex; }
    [[nodiscard]] const std::vector<Section>& get_sections() const noexcept { return sections; }
};

// Result of computing H^0 / H^1 on an `SpdSheaf`.
//
//   h0_generators[i] — vector indexed by 0-cells; each entry is the `Section`
//                      value of generator i at that 0-cell.
//   h1_generators[i] — vector indexed by 1-cells (edges) in the flag complex's
//                      edge order; each entry is a `Section` triple.
struct SpdCohomology {
    std::size_t                       h0_dim{0};
    std::size_t                       h1_dim{0};
    std::vector<std::vector<Section>> h0_generators;
    std::vector<std::vector<Section>> h1_generators;
};

}  // namespace aleph::sheaf

// ----- Dense f64 QR helper (ported from spd_sheaf.rs's DenseF64/householder_qr) ----
namespace aleph::sheaf::detail {

struct DenseF64 {
    std::size_t       rows{0};
    std::size_t       cols{0};
    std::vector<double> data;

    static DenseF64 zeros(std::size_t rows, std::size_t cols) {
        return DenseF64{rows, cols, std::vector<double>(rows * cols, 0.0)};
    }
    static DenseF64 identity(std::size_t n) {
        DenseF64 m = zeros(n, n);
        for (std::size_t i = 0; i < n; ++i) m.set(i, i, 1.0);
        return m;
    }
    [[nodiscard]] double get(std::size_t r, std::size_t c) const noexcept {
        return data[r * cols + c];
    }
    void set(std::size_t r, std::size_t c, double v) noexcept {
        data[r * cols + c] = v;
    }
    [[nodiscard]] DenseF64 transpose() const {
        DenseF64 out = zeros(cols, rows);
        for (std::size_t i = 0; i < rows; ++i)
            for (std::size_t j = 0; j < cols; ++j)
                out.set(j, i, get(i, j));
        return out;
    }
};

struct QrResult {
    DenseF64    q;
    std::size_t rank{0};
};

// Householder QR. Returns Q (nrows x nrows) and the numerical rank of A (count
// of |R(diag,diag)| > eps). Ported verbatim from the Rust reference: sign
// choice `alpha = -alpha` when `a(k,k) > 0`, `tau = 2 / vᵀv`, and the `1e-300`
// degenerate guards on `|alpha|` and `vᵀv`.
inline QrResult householder_qr(DenseF64 a, double eps) {
    const std::size_t nrows = a.rows;
    const std::size_t ncols = a.cols;
    DenseF64          q     = DenseF64::identity(nrows);
    const std::size_t pivot_count = (nrows < ncols) ? nrows : ncols;
    // Householder reflector vector (length nrows, only indices k..nrows used).
    std::vector<double> hh(nrows, 0.0);
    for (std::size_t k = 0; k < pivot_count; ++k) {
        // Column norm from row k downward.
        double alpha_sq = 0.0;
        for (std::size_t row = k; row < nrows; ++row) {
            const double aik = a.get(row, k);
            alpha_sq += aik * aik;
        }
        double alpha = std::sqrt(alpha_sq);
        if (a.get(k, k) > 0.0) {
            alpha = -alpha;
        }
        if (std::abs(alpha) < 1e-300) {
            continue;
        }
        // Build reflector vector in hh[k..nrows].
        for (std::size_t row = k; row < nrows; ++row) {
            hh[row] = a.get(row, k);
        }
        hh[k] -= alpha;
        double vtv = 0.0;
        for (std::size_t row = k; row < nrows; ++row) {
            vtv += hh[row] * hh[row];
        }
        if (vtv < 1e-300) {
            continue;
        }
        const double tau = 2.0 / vtv;
        // Apply H = I - tau * v * v^T to A from the left.
        for (std::size_t col = k; col < ncols; ++col) {
            double dot = 0.0;
            for (std::size_t row = k; row < nrows; ++row) {
                dot += hh[row] * a.get(row, col);
            }
            dot *= tau;
            for (std::size_t row = k; row < nrows; ++row) {
                const double nv = a.get(row, col) - dot * hh[row];
                a.set(row, col, nv);
            }
        }
        // Apply H to Q from the right: Q <- Q * H^T = Q * H (H is symmetric).
        for (std::size_t qi = 0; qi < nrows; ++qi) {
            double dot = 0.0;
            for (std::size_t qj = k; qj < nrows; ++qj) {
                dot += q.get(qi, qj) * hh[qj];
            }
            dot *= tau;
            for (std::size_t qj = k; qj < nrows; ++qj) {
                const double nv = q.get(qi, qj) - dot * hh[qj];
                q.set(qi, qj, nv);
            }
        }
    }
    std::size_t rank = 0;
    for (std::size_t diag = 0; diag < pivot_count; ++diag) {
        if (std::abs(a.get(diag, diag)) > eps) {
            ++rank;
        }
    }
    return QrResult{std::move(q), rank};
}

}  // namespace aleph::sheaf::detail

export namespace aleph::sheaf {

// Constant sheaf: every 0-cell gets the same coefficient triple. Generalisation
// of `ConstantZ2Sheaf` to R^3.
[[nodiscard]] inline SpdSheaf spd_constant(FlagComplex complex, Section value) {
    // `complex.simplices[0]` is the set of all 0-simplices (vertices); its
    // length gives the vertex count.
    const std::size_t n_vertices =
        complex.simplices.empty() ? std::size_t{0} : complex.simplices.front().size();
    return SpdSheaf{std::move(complex), std::vector<Section>(n_vertices, value)};
}

// Per-cell sheaf: caller provides one coefficient triple per NodeId; nodes
// absent from `node_sections` get `{0,0,0}`. The resulting `sections` slice is
// aligned with `complex.simplices[0]` iteration order so the cohomology builder
// can pair `sections[i]` with the i-th vertex without an extra lookup.
[[nodiscard]] inline SpdSheaf
spd_per_cell(FlagComplex complex,
             const aleph::containers::OrderedMap<NodeId, Section>& node_sections) {
    std::vector<Section> sections;
    if (!complex.simplices.empty()) {
        const auto& verts = complex.simplices.front();
        sections.reserve(verts.size());
        for (const Simplex& simplex : verts) {
            // Each 0-simplex is a Simplex (vector<NodeId>) of length 1.
            const NodeId nid = simplex[0];
            const Section* found = node_sections.get(nid);
            sections.push_back(found ? *found : Section{0.0f, 0.0f, 0.0f});
        }
    }
    return SpdSheaf{std::move(complex), std::move(sections)};
}

// Build a `spd_constant` for the given graph; constructs the flag complex
// internally.
[[nodiscard]] inline SpdSheaf build_constant(const aleph::graph::Graph& graph, Section value) {
    const OneSkeleton skel = OneSkeleton::from_graph(graph);
    FlagComplex       complex = build_flag_complex(skel);
    return spd_constant(std::move(complex), value);
}

// Build a `spd_per_cell` for the given graph + per-node values.
[[nodiscard]] inline SpdSheaf
build_per_cell(const aleph::graph::Graph& graph,
               const aleph::containers::OrderedMap<NodeId, Section>& sections) {
    const OneSkeleton skel = OneSkeleton::from_graph(graph);
    FlagComplex       complex = build_flag_complex(skel);
    return spd_per_cell(std::move(complex), sections);
}

// Compute H^0 and H^1 of an `SpdSheaf` over R.
//
// 1. Build δ₀ : C⁰ → C¹ as a real n_e × n_v matrix where each row is edge
//    (u→v) with −1 at column u and +1 at column v.
// 2. The three coefficient channels decouple, so H^k = 3 copies of the scalar
//    cohomology.
// 3. H⁰ dim = #components (connected components of the 1-skeleton). Generators
//    carry the sheaf's `sections` value at each vertex of the component.
// 4. H¹ generators: closed 1-cochains as columns of Q from the QR of δ₀^T
//    beyond the rank.
[[nodiscard]] inline SpdCohomology compute_spd_cohomology(const SpdSheaf& sheaf) {
    const FlagComplex& complex = sheaf.complex;
    const std::vector<Simplex> empty_level{};
    const std::vector<Simplex>& vertices =
        complex.simplices.empty() ? empty_level : complex.simplices[0];
    const std::vector<Simplex>& edges =
        (complex.simplices.size() > 1) ? complex.simplices[1] : empty_level;
    const std::size_t n_v = vertices.size();
    const std::size_t n_e = edges.size();

    // Vertex NodeId -> column index map (vertices are sorted ascending here).
    aleph::containers::OrderedMap<NodeId, std::size_t> v_idx;
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        v_idx.insert(vertices[i][0], i);  // 0-simplex is a length-1 Simplex
    }
    auto col_of = [&](NodeId nid) -> std::size_t {
        const std::size_t* p = v_idx.get(nid);
        assert(p != nullptr && "edge endpoint missing from vertex index");
        return *p;
    };

    // Build δ₀ as an n_e × n_v real matrix.
    detail::DenseF64 delta0 = detail::DenseF64::zeros(n_e, n_v);
    for (std::size_t ei = 0; ei < edges.size(); ++ei) {
        const Simplex& simp = edges[ei];  // length-2, sorted ascending
        const std::size_t col_u = col_of(simp[0]);
        const std::size_t col_v = col_of(simp[1]);
        delta0.set(ei, col_u, -1.0);
        delta0.set(ei, col_v, 1.0);
    }

    // H⁰: connected components via DFS on the 1-skeleton.
    std::vector<std::vector<std::size_t>> adj(n_v);
    for (const Simplex& simp : edges) {
        const std::size_t col_u = col_of(simp[0]);
        const std::size_t col_v = col_of(simp[1]);
        adj[col_u].push_back(col_v);
        adj[col_v].push_back(col_u);
    }
    constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    std::vector<std::size_t> comp_id(n_v, kNone);
    std::size_t              next_id = 0;
    std::vector<std::size_t> stack;
    for (std::size_t start = 0; start < n_v; ++start) {
        if (comp_id[start] != kNone) {
            continue;
        }
        stack.push_back(start);
        while (!stack.empty()) {
            const std::size_t x = stack.back();
            stack.pop_back();
            if (comp_id[x] != kNone) {
                continue;
            }
            comp_id[x] = next_id;
            for (std::size_t y : adj[x]) {
                if (comp_id[y] == kNone) {
                    stack.push_back(y);
                }
            }
        }
        ++next_id;
    }
    const std::size_t h0_dim = next_id;

    std::vector<std::vector<Section>> h0_gens(
        h0_dim, std::vector<Section>(n_v, Section{0.0f, 0.0f, 0.0f}));
    for (std::size_t cid = 0; cid < h0_dim; ++cid) {
        for (std::size_t vi = 0; vi < n_v; ++vi) {
            if (comp_id[vi] == cid) {
                h0_gens[cid][vi] = sheaf.sections[vi];
            }
        }
    }

    // H¹ generators: nullspace of δ₀^T. dim H¹ = n_e − rank(δ₀).
    const detail::QrResult qr_v = detail::householder_qr(delta0, 1e-9);
    const std::size_t h1_dim = (n_e > qr_v.rank) ? (n_e - qr_v.rank) : std::size_t{0};

    std::vector<std::vector<Section>> h1_gens(
        h1_dim, std::vector<Section>(n_e, Section{0.0f, 0.0f, 0.0f}));
    if (h1_dim > 0) {
        // QR of δ₀^T: the last (n_e − rank) columns of Q span the right
        // nullspace of δ₀^T, i.e. the space of closed 1-cochains.
        detail::DenseF64 delta0_t = delta0.transpose();
        const detail::QrResult qr_e = detail::householder_qr(std::move(delta0_t), 1e-9);
        for (std::size_t gen_i = 0; gen_i < h1_dim; ++gen_i) {
            const std::size_t col = qr_e.rank + gen_i;
            if (col >= qr_e.q.cols) {
                break;
            }
            std::vector<Section>& generator = h1_gens[gen_i];
            for (std::size_t ei = 0; ei < n_e; ++ei) {
                const float coeff = static_cast<float>(qr_e.q.get(ei, col));
                // Carry the SPD payload from the average of edge endpoints.
                const Simplex& simp = edges[ei];
                const std::size_t col_u = col_of(simp[0]);
                const std::size_t col_v = col_of(simp[1]);
                const Section& sec_u = sheaf.sections[col_u];
                const Section& sec_v = sheaf.sections[col_v];
                generator[ei] = Section{
                    coeff * 0.5f * (sec_u[0] + sec_v[0]),
                    coeff * 0.5f * (sec_u[1] + sec_v[1]),
                    coeff * 0.5f * (sec_u[2] + sec_v[2]),
                };
            }
        }
    }

    return SpdCohomology{h0_dim, h1_dim, std::move(h0_gens), std::move(h1_gens)};
}

// Connecting morphism `∂: H⁰(F'') → H¹(F')` for a short-exact sequence of SPD
// sheaves induced by a DPO rewrite. Simplified surface: pointwise difference of
// vertex sections (post − pre). Projection onto an H¹ basis is the caller's
// responsibility.
[[nodiscard]] inline std::vector<Section>
connecting_morphism_spd(const SpdSheaf& pre, const SpdSheaf& post) {
    const std::size_t n = (pre.sections.size() < post.sections.size())
                              ? pre.sections.size()
                              : post.sections.size();
    std::vector<Section> delta(n, Section{0.0f, 0.0f, 0.0f});
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = 0; k < 3; ++k) {
            delta[i][k] = post.sections[i][k] - pre.sections[i][k];
        }
    }
    return delta;
}

}  // namespace aleph::sheaf

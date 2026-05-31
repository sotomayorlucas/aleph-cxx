// Zigzag persistence over DPO rewrite trajectories.
//
// A DPO rewrite G_i → G_{i+1} factors as G_i ⊇ D_i ⊆ G_{i+1} where D_i is the
// pushout complement. Both inclusions are sub-complex inclusions of the flag
// complex and induce GF(2)-linear maps on cohomology. A finite sequence of
// rewrites yields a zigzag of vector spaces; Gabriel's theorem decomposes it
// into interval modules — the barcode (births/deaths).
//
// The trajectory-level Mayer-Vietoris invariant Σ ε_sheaf ≥ #deaths_forward is
// the headline oracle (see aleph-engine/aleph-sheaf/tests/zigzag_mv_witness.rs
// and zigzag_property.rs). The hand-computed barcodes on small trajectories
// are in zigzag_fixtures.rs / zigzag_smoke.rs.
//
// Ported from aleph-engine/aleph-sheaf/src/zigzag.rs.
//
// Adaptation notes (vs. the Rust reference — the Rust does NOT compile
// verbatim):
//   * Graph is move-only in C++ (the backing OrderedMaps delete copy), so the
//     Rust `RewriteTrace.graphs: Vec<Graph>` (which clones each stop) cannot be
//     ported as an owning vector. Per the 4c.1 plan the trajectory is "passed
//     by pointer/descriptor": `RewriteTrace` holds `std::vector<const Graph*>`
//     — non-owning descriptors of the graph at each stop. The caller owns the
//     graphs (one per stop) and keeps them alive for the trace's lifetime, the
//     same pattern the :mayer_vietoris test uses (build `before`/`after` by
//     hand rather than clone).
//   * The Rust `RewriteTrace::apply(rule, m)` clones the last graph, applies the
//     DPO rule in place, and records the step. The C++ aleph.dpo `apply`
//     commits a move-built post-state and is driven by the caller (who owns the
//     graphs), so the trace exposes `record_step(g_before, g_after, preserved)`:
//     it runs `decompose_rewrite` + `mayer_vietoris_certify_with` exactly like
//     the Rust `apply` body and pushes the (pushout_complement, mv, preserved)
//     TraceStep plus the `&g_after` descriptor. There is also an `apply` helper
//     that takes an aleph.dpo Rule + Match + the already-applied after-graph and
//     snapshots the preserved interface, mirroring the Rust signature shape.
//   * `BitMatrix::solve` does not exist in aleph.linalg.gf2. It is ported here
//     (a zigzag-local detail: GF(2) Gaussian elimination on the augmented
//     system, returning *some* solution x with M·x = b or std::nullopt). Used
//     by `inclusion_induced_map_hk` (read off H_k coefficients of an extended
//     sub-cycle) and `cross_backward_arrow` (lift an active class through ψ).
//   * gf2 API drift: Rust `BitMatrix{rows,cols,get,set,zeros,transpose,rank,
//     kernel_basis,image_basis,from_cols,apply}` → C++ `BitMatrix{rows(),cols(),
//     at(),set(),(r,c) ctor),transpose(),rank(),kernel_basis(),image_basis(),
//     from_cols(span,nrows),apply()}`. `from_cols` takes an explicit row count
//     in C++; we pass the layout's total_bits / target dim. `BitVec::zeros(n)`
//     → `BitVec(n)`.
//   * Sheaf surface uses the C++ concept method names (`dim_stalk`,
//     `lift_basis_index`) shared across :sheaf_trait and cluster C.
//   * Homology (NOT cohomology) drives the inclusion maps: chain inclusion
//     D ↪ G commutes with ∂ while cochain extension-by-zero does NOT commute
//     with δ in general. We take the algebraic dual ∂_k = (δ^{k-1})^T (the gf2
//     `transpose`), exactly as the Rust `boundary_matrix`.
//   * No exceptions (aleph_flags_isa): the Rust `.expect(...)` on a solve that
//     must succeed (the chain-inclusion contract) becomes `assert`; recoverable
//     "no preimage" is std::optional/nullopt as in the Rust `match psi.solve`.
//   * `Interval.generator` is always `None` in the Rust reference; kept as
//     `std::optional<BitVec>` (always nullopt) for shape parity.

module;
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

export module aleph.sheaf:zigzag;

import aleph.graph;
import aleph.types;
import aleph.containers;
import aleph.linalg.gf2;

import :simplex;
import :skeleton;
import :flag_complex;
import :subgraph;
import :sheaf_trait;
import :sheaf_constant;
import :sheaf_visibility;
import :cochain;
import :cohomology;
import :mayer_vietoris;

export namespace aleph::sheaf {

using aleph::linalg::gf2::BitMatrix;
using aleph::linalg::gf2::BitVec;

// Direction of the arrow that killed a class.
enum class ArrowDirection {
    // Killed by D_i ↪ G_{i+1} (the forward inclusion).
    Forward,
    // Killed by D_i ↪ G_i (the backward inclusion).
    Backward,
    // The class survives to the end of the trace.
    EndOfTrace,
};

// One indecomposable summand of the zigzag.
struct Interval {
    // Graph index where the feature first appears.
    std::size_t birth{0};
    // Graph index where the feature last appears (birth..=last_graph_index).
    std::size_t death{0};
    // Cohomological dimension this interval lives in (0 or 1).
    std::size_t dim{0};
    // Optional cocycle representative at the birth graph (always nullopt here,
    // matching the Rust reference's `generator: None`).
    std::optional<BitVec> generator{};
    // Direction of the arrow that killed the class.
    std::optional<ArrowDirection> killing_arrow{};

    // Equality ignores `generator` (always nullopt) so the Rust derive(Eq) over
    // the populated fields ports faithfully; BitVec is comparable but never set.
    [[nodiscard]] bool operator==(const Interval& o) const noexcept {
        return birth == o.birth && death == o.death && dim == o.dim &&
               killing_arrow == o.killing_arrow;
    }
};

// The barcode of a zigzag at a fixed cohomological dimension. Intervals are
// sorted lexicographically by (birth, death) for stable golden snapshots.
struct ZigzagBarcode {
    std::size_t           dim{0};
    std::size_t           last_graph_index{0};
    std::vector<Interval> intervals{};

    // Count of intervals that die strictly before the end of the trace. Ported
    // from ZigzagBarcode::finite_intervals.
    [[nodiscard]] std::size_t finite_intervals() const noexcept {
        std::size_t count = 0;
        for (const Interval& iv : intervals) {
            if (iv.death < last_graph_index) ++count;
        }
        return count;
    }
};

// One step of a rewrite trajectory: the pushout complement D_i, the MV
// certificate for the i → i+1 transition, and the preserved runtime node ids.
struct TraceStep {
    // D_i as a Subgraph view of graphs[i+1] (preserved across the rewrite). Same
    // as the U component of the M3a decomposition (decompose_rewrite).
    Subgraph pushout_complement{};
    // MV certificate for the i → i+1 transition.
    MayerVietorisCertificate mv{};
    // Preserved runtime node ids (m(K_pattern)).
    aleph::containers::FlatSet<aleph::types::NodeId> preserved{};
};

}  // namespace aleph::sheaf

namespace aleph::sheaf::zigzag_detail {

using aleph::linalg::gf2::BitMatrix;
using aleph::linalg::gf2::BitVec;

inline constexpr std::size_t kSentinelBirth = std::numeric_limits<std::size_t>::max();

// Find *some* x (cols-wide) with M·x = b (rows-wide) over GF(2), or nullopt if
// the system is inconsistent. GF(2) Gaussian elimination on the augmented
// matrix [M | b]; free variables are pinned to 0 so the returned solution is
// deterministic. Ports the Rust `BitMatrix::solve`
// (aleph-engine/aleph-sheaf/src/linalg_gf2.rs), which aleph.linalg.gf2 does not
// expose.
[[nodiscard]] inline std::optional<BitVec> solve(const BitMatrix& m, const BitVec& b) {
    const std::size_t rows = m.rows();
    const std::size_t cols = m.cols();

    // Augmented rows: bit `cols` holds b's entry. Copy M | b.
    std::vector<BitVec> aug(rows, BitVec(cols + 1));
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            if (m.at(r, c)) aug[r].set(c, true);
        }
        if (b.get(r)) aug[r].set(cols, true);
    }

    // Forward elimination to row echelon form, tracking the pivot column of
    // each pivot row.
    std::vector<std::size_t> pivot_col_of_row(rows, cols + 1);  // sentinel = none
    std::vector<std::size_t> pivot_row_of_col(cols, rows);      // sentinel = none
    std::size_t pivot_row = 0;
    for (std::size_t col = 0; col < cols && pivot_row < rows; ++col) {
        std::size_t sel = rows;
        for (std::size_t r = pivot_row; r < rows; ++r) {
            if (aug[r].get(col)) { sel = r; break; }
        }
        if (sel == rows) continue;  // no pivot in this column
        std::swap(aug[pivot_row], aug[sel]);
        for (std::size_t r = 0; r < rows; ++r) {
            if (r != pivot_row && aug[r].get(col)) aug[r] ^= aug[pivot_row];
        }
        pivot_col_of_row[pivot_row] = col;
        pivot_row_of_col[col] = pivot_row;
        ++pivot_row;
    }

    // Consistency: any row that is all-zero in M but has a 1 in the b column is
    // an inconsistent equation 0 = 1 → no solution.
    for (std::size_t r = 0; r < rows; ++r) {
        if (pivot_col_of_row[r] == cols + 1 && aug[r].get(cols)) {
            return std::nullopt;
        }
    }

    // Back-substitute with free variables pinned to 0: x[pivot_col] = b-entry of
    // its (reduced) row.
    BitVec x(cols);
    for (std::size_t col = 0; col < cols; ++col) {
        const std::size_t pr = pivot_row_of_col[col];
        if (pr != rows && aug[pr].get(cols)) x.set(col, true);
    }
    return x;
}

// A length-`len` one-hot vector with bit `i` set. Ports `one_hot_basis`.
[[nodiscard]] inline BitVec one_hot_basis(std::size_t i, std::size_t len) {
    BitVec v(len);
    v.set(i, true);
    return v;
}

// True iff `v` lies in the GF(2) linear span of `basis`. Uses a rank check
// (does adding v raise the span's rank?) rather than reduce_modulo_image, which
// the Rust reference flags as buggy on non-echelon bases. Ports `is_in_span`.
[[nodiscard]] inline bool is_in_span(const BitVec& v, const std::vector<BitVec>& basis) {
    if (v.is_zero()) return true;
    if (basis.empty()) return false;
    const std::size_t nrows = v.size();
    const std::size_t r_before =
        BitMatrix::from_cols(std::span<const BitVec>(basis), nrows).rank();
    std::vector<BitVec> combined = basis;
    combined.push_back(v);
    const std::size_t r_after =
        BitMatrix::from_cols(std::span<const BitVec>(combined), nrows).rank();
    return r_before == r_after;
}

// Cellular boundary ∂_k: C_k → C_{k-1}. For finite-dim GF(2) (C_*, ∂_*) is the
// linear dual of (C^*, δ^*), so ∂_k = (δ^{k-1})^T. Ports `boundary_matrix`.
template <aleph::sheaf::CellularZ2Sheaf S>
[[nodiscard]] BitMatrix boundary_matrix(const S& sheaf, const aleph::sheaf::FlagComplex& complex,
                                        std::size_t k) {
    if (k == 0) {
        const std::size_t dim_c0 =
            aleph::sheaf::CochainLayout::for_dim(sheaf, complex, 0).total_bits();
        return BitMatrix(0, dim_c0);
    }
    const BitMatrix delta_prev = aleph::sheaf::coboundary_matrix(sheaf, complex, k - 1);
    return delta_prev.transpose();
}

// H_k basis (cycle representatives modulo boundaries): cycles = ker ∂_k, then
// greedily keep cycles whose addition raises the rank of [im ∂_{k+1} | kept].
// Ports `h_k_homology_basis`.
template <aleph::sheaf::CellularZ2Sheaf S>
[[nodiscard]] std::vector<BitVec> h_k_homology_basis(
    const S& sheaf, const aleph::sheaf::FlagComplex& complex, std::size_t k) {
    const BitMatrix bdy_k = boundary_matrix(sheaf, complex, k);
    const std::vector<BitVec> cycles = bdy_k.kernel_basis();
    // Cycles live in C_k; their width is dim C_k = the cochain layout total bits.
    const std::size_t ck_bits =
        aleph::sheaf::CochainLayout::for_dim(sheaf, complex, k).total_bits();

    std::vector<BitVec> image_basis;
    if (k + 1 < complex.simplices.size()) {
        const BitMatrix bdy_kp1 = boundary_matrix(sheaf, complex, k + 1);
        image_basis = bdy_kp1.image_basis();
    }
    const std::size_t image_rank = image_basis.size();

    std::vector<BitVec> h_basis;
    for (const BitVec& v : cycles) {
        const std::size_t current_count = image_rank + h_basis.size();
        std::vector<BitVec> combined = image_basis;
        combined.insert(combined.end(), h_basis.begin(), h_basis.end());
        combined.push_back(v);
        const std::size_t new_rank =
            BitMatrix::from_cols(std::span<const BitVec>(combined), ck_bits).rank();
        if (new_rank > current_count) {
            h_basis.push_back(v);
        }
    }
    return h_basis;
}

// Extend a sub-complex k-chain by zero on simplices not in the sub layout. The
// result has the sup layout. Chain inclusion commutes with ∂_k, so the
// extension of a cycle is a cycle. Ports `extend_chain_by_zero`.
template <aleph::sheaf::CellularZ2Sheaf S>
[[nodiscard]] BitVec extend_chain_by_zero(const BitVec& sub_vec,
                                          const aleph::sheaf::CochainLayout& sub_layout,
                                          const S& sub_sheaf,
                                          const aleph::sheaf::CochainLayout& sup_layout,
                                          const S& sup_sheaf) {
    BitVec out(sup_layout.total_bits());
    for (const auto& [sigma, sub_range] : sub_layout.per_simplex()) {
        const aleph::sheaf::BitRange* sup_range = sup_layout.range_of(sigma);
        if (sup_range == nullptr) continue;
        const std::size_t sub_dim = sub_sheaf.dim_stalk(sigma);
        for (std::size_t i_sub = 0; i_sub < sub_dim; ++i_sub) {
            if (!sub_vec.get(sub_range.start + i_sub)) continue;
            const std::optional<std::size_t> i_sup =
                sub_sheaf.lift_basis_index(sigma, i_sub, sup_sheaf);
            if (i_sup.has_value()) {
                out.set(sup_range->start + *i_sup, true);
            }
        }
    }
    return out;
}

// Matrix of the H_k map induced by a subcomplex inclusion sub ⊆ sup (chain
// inclusion / extend-by-zero). Rows = sup_basis.size(), cols = sub_basis.size().
// Column j expresses extend(sub_basis[j]) (mod im ∂_{k+1}^sup) in sup_basis.
// Ports `inclusion_induced_map_hk`.
template <aleph::sheaf::CellularZ2Sheaf S>
[[nodiscard]] BitMatrix inclusion_induced_map_hk(
    const aleph::sheaf::FlagComplex& sub_complex, const S& sub_sheaf,
    const std::vector<BitVec>& sub_basis, const aleph::sheaf::FlagComplex& sup_complex,
    const S& sup_sheaf, const std::vector<BitVec>& sup_basis, std::size_t k) {
    const aleph::sheaf::CochainLayout sub_layout =
        aleph::sheaf::CochainLayout::for_dim(sub_sheaf, sub_complex, k);
    const aleph::sheaf::CochainLayout sup_layout =
        aleph::sheaf::CochainLayout::for_dim(sup_sheaf, sup_complex, k);

    // Augmented basis [sup_basis | im ∂_{k+1}^sup] — solving against this reads
    // off the H_k coefficients of an extended sub-cycle directly.
    std::vector<BitVec> augmented = sup_basis;
    std::vector<BitVec> image_basis;
    if (k + 1 < sup_complex.simplices.size()) {
        const BitMatrix bdy_kp1 = boundary_matrix(sup_sheaf, sup_complex, k + 1);
        image_basis = bdy_kp1.image_basis();
    }
    augmented.insert(augmented.end(), image_basis.begin(), image_basis.end());
    const std::size_t n_sup = sup_basis.size();
    const std::size_t sup_bits = sup_layout.total_bits();

    const BitMatrix aug_matrix =
        augmented.empty() ? BitMatrix(sup_bits, 0)
                          : BitMatrix::from_cols(std::span<const BitVec>(augmented), sup_bits);

    std::vector<BitVec> cols;
    cols.reserve(sub_basis.size());
    for (const BitVec& c_sub : sub_basis) {
        const BitVec extended =
            extend_chain_by_zero(c_sub, sub_layout, sub_sheaf, sup_layout, sup_sheaf);
        const std::optional<BitVec> x = solve(aug_matrix, extended);
        // The chain inclusion contract guarantees the extended sub-cycle is a
        // sup-cycle expressible in (sup_basis ⊕ im ∂_{k+1}^sup). A failure here
        // means `sub` is not a sub-complex of `sup` (programmer error).
        assert(x.has_value() &&
               "extended sub-cycle must be solvable in (sup_basis ⊕ im ∂_{k+1}^sup)");
        BitVec coeffs(n_sup);
        for (std::size_t i = 0; i < n_sup; ++i) {
            if (x->get(i)) coeffs.set(i, true);
        }
        cols.push_back(std::move(coeffs));
    }

    if (cols.empty()) return BitMatrix(n_sup, 0);
    return BitMatrix::from_cols(std::span<const BitVec>(cols), n_sup);
}

// H_k data at a single stop (a G_i or D_i): the basis and its dimension.
// NOTE on field order: `hk_dim` is declared BEFORE `hk_basis` so that the
// aggregate initializers `StopData{.hk_dim = b.size(), .hk_basis = std::move(b)}`
// list designators in declaration order (required in C++) AND evaluate
// `b.size()` before the move that empties `b`. (Mirrors Rust StopData, where
// literal field order has no bearing on move/evaluation semantics.)
struct StopData {
    std::size_t         hk_dim{0};
    std::vector<BitVec> hk_basis{};
};

// (birth, representative) of an active class threaded through the zigzag.
using Active = std::pair<std::size_t, BitVec>;

// Cross a backward arrow ψ: H_k(D_step) → H_k(G_step). Lift each active class
// through ψ (preimage exists → survives; else dies Backward), then add D-basis
// newborns not covered by the lifted set. Ports `cross_backward_arrow`.
[[nodiscard]] inline std::vector<Active> cross_backward_arrow(
    std::vector<aleph::sheaf::Interval>& intervals, std::vector<Active> active,
    const BitMatrix& psi, std::size_t d_dim, std::size_t step, std::size_t dim_k) {
    std::vector<Active> next;
    std::vector<BitVec> echelon;
    for (auto& [birth, rep_in_g] : active) {
        std::optional<BitVec> preimage = solve(psi, rep_in_g);
        if (preimage.has_value()) {
            echelon.push_back(*preimage);
            next.emplace_back(birth, std::move(*preimage));
        } else {
            intervals.push_back(aleph::sheaf::Interval{
                .birth = birth,
                .death = step,
                .dim = dim_k,
                .generator = std::nullopt,
                .killing_arrow = aleph::sheaf::ArrowDirection::Backward,
            });
        }
    }
    // Newborns at D_step: D-basis elements not in the span of the lifted set.
    for (std::size_t i = 0; i < d_dim; ++i) {
        BitVec e_i = one_hot_basis(i, d_dim);
        if (!is_in_span(e_i, echelon)) {
            echelon.push_back(e_i);
            next.emplace_back(kSentinelBirth, std::move(e_i));
        }
    }
    return next;
}

// Cross a forward arrow φ: H_k(D_step) → H_k(G_{step+1}). Elder rule: oldest
// birth first (sentinel "born at preceding D" ordered as step+1). A class whose
// image is already spanned dies Forward; otherwise it survives with its image.
// Then add G_{step+1}-basis newborns. Ports `cross_forward_arrow`.
[[nodiscard]] inline std::vector<Active> cross_forward_arrow(
    std::vector<aleph::sheaf::Interval>& intervals, std::vector<Active> active,
    const BitMatrix& phi, std::size_t g_next_dim, std::size_t step, std::size_t dim_k) {
    // Sort by birth, sentinel treated as step+1 (stable to preserve discovery
    // order among equal births, matching the Rust sort_by_key).
    std::stable_sort(active.begin(), active.end(), [step](const Active& a, const Active& b) {
        const std::size_t ka = (a.first == kSentinelBirth) ? step + 1 : a.first;
        const std::size_t kb = (b.first == kSentinelBirth) ? step + 1 : b.first;
        return ka < kb;
    });

    std::vector<Active> next;
    std::vector<BitVec> echelon;
    for (auto& [birth, rep_in_d] : active) {
        BitVec img = phi.apply(rep_in_d);
        if (is_in_span(img, echelon)) {
            if (birth != kSentinelBirth) {
                intervals.push_back(aleph::sheaf::Interval{
                    .birth = birth,
                    .death = step,
                    .dim = dim_k,
                    .generator = std::nullopt,
                    .killing_arrow = aleph::sheaf::ArrowDirection::Forward,
                });
            }
        } else {
            echelon.push_back(img);
            const std::size_t real_birth = (birth == kSentinelBirth) ? step + 1 : birth;
            next.emplace_back(real_birth, std::move(img));
        }
    }
    // Newborns at G_{step+1}: complete the basis with coker(φ) ∪ extras.
    for (std::size_t i = 0; i < g_next_dim; ++i) {
        BitVec e_i = one_hot_basis(i, g_next_dim);
        if (!is_in_span(e_i, echelon)) {
            echelon.push_back(e_i);
            next.emplace_back(step + 1, std::move(e_i));
        }
    }
    return next;
}

// Zigzag persistence decomposition. Ports `decompose_zigzag`.
[[nodiscard]] inline aleph::sheaf::ZigzagBarcode decompose_zigzag(
    const std::vector<StopData>& g_stops, const std::vector<StopData>& d_stops,
    const std::vector<BitMatrix>& arrows_back, const std::vector<BitMatrix>& arrows_fwd,
    std::size_t dim_k) {
    assert(g_stops.size() == d_stops.size() + 1);
    assert(arrows_back.size() == d_stops.size());
    assert(arrows_fwd.size() == d_stops.size());

    const std::size_t n = d_stops.size();
    const std::size_t last_graph_index = n;

    std::vector<aleph::sheaf::Interval> intervals;
    std::vector<Active> active;
    for (std::size_t i = 0; i < g_stops[0].hk_dim; ++i) {
        active.emplace_back(std::size_t{0}, one_hot_basis(i, g_stops[0].hk_dim));
    }

    for (std::size_t step = 0; step < n; ++step) {
        active = cross_backward_arrow(intervals, std::move(active), arrows_back[step],
                                      d_stops[step].hk_dim, step, dim_k);
        active = cross_forward_arrow(intervals, std::move(active), arrows_fwd[step],
                                     g_stops[step + 1].hk_dim, step, dim_k);
    }

    for (auto& [birth, rep] : active) {
        (void)rep;
        intervals.push_back(aleph::sheaf::Interval{
            .birth = birth,
            .death = last_graph_index,
            .dim = dim_k,
            .generator = std::nullopt,
            .killing_arrow = aleph::sheaf::ArrowDirection::EndOfTrace,
        });
    }

    std::stable_sort(intervals.begin(), intervals.end(),
                     [](const aleph::sheaf::Interval& a, const aleph::sheaf::Interval& b) {
                         if (a.birth != b.birth) return a.birth < b.birth;
                         return a.death < b.death;
                     });

    return aleph::sheaf::ZigzagBarcode{
        .dim = dim_k, .last_graph_index = last_graph_index, .intervals = std::move(intervals)};
}

}  // namespace aleph::sheaf::zigzag_detail

export namespace aleph::sheaf {

// A finite sequence of DPO rewrites with the data needed to compute the zigzag
// barcode and the trajectory MV invariant.
//
// Graph is move-only, so (unlike the Rust Vec<Graph>) the trace does NOT own the
// graphs: it holds non-owning descriptors (`const Graph*`) to the graph at each
// stop. The caller owns the graphs (one per stop) and must keep them alive for
// the trace's lifetime. `graphs[0]` = initial; `graphs[i+1]` = after the i-th
// rewrite (length n + 1). `steps[i]` records the i-th rewrite (length n).
class RewriteTrace {
public:
    // Start a trace at `initial`. The referenced graph must outlive the trace.
    RewriteTrace(const aleph::graph::Graph& initial, SheafKind sheaf_kind)
        : sheaf_kind_(sheaf_kind) {
        graphs_.push_back(&initial);
    }

    // Number of recorded rewrite steps (Rust `len`).
    [[nodiscard]] std::size_t len() const noexcept { return steps_.size(); }
    // True iff no steps recorded (Rust `is_empty`).
    [[nodiscard]] bool is_empty() const noexcept { return steps_.empty(); }

    [[nodiscard]] SheafKind sheaf_kind() const noexcept { return sheaf_kind_; }
    [[nodiscard]] const std::vector<const aleph::graph::Graph*>& graphs() const noexcept {
        return graphs_;
    }
    [[nodiscard]] const std::vector<TraceStep>& steps() const noexcept { return steps_; }

    // Record a rewrite G_before → G_after with the given preserved interface.
    //
    // `g_before` must be the graph at the current last stop (the caller applied
    // the DPO rewrite to a copy/rebuild to produce `g_after`). Computes the MV
    // cover (U, K, R) via decompose_rewrite and the MV certificate, pushes the
    // TraceStep (pushout_complement = U, the MV cert, the preserved set) and the
    // `&g_after` descriptor. Mirrors the body of the Rust `RewriteTrace::apply`.
    // `g_after` must outlive the trace.
    void record_step(const aleph::graph::Graph& g_before, const aleph::graph::Graph& g_after,
                     const aleph::containers::FlatSet<aleph::types::NodeId>& preserved) {
        auto [u, k, r] = decompose_rewrite(g_before, g_after, preserved);
        const MayerVietorisCertificate mv =
            mayer_vietoris_certify_with(g_after, u, r, k, sheaf_kind_);
        steps_.push_back(TraceStep{
            .pushout_complement = std::move(u),
            .mv = mv,
            .preserved = preserved,
        });
        graphs_.push_back(&g_after);
    }

private:
    std::vector<const aleph::graph::Graph*> graphs_{};
    std::vector<TraceStep>                  steps_{};
    SheafKind                               sheaf_kind_{};
};

}  // namespace aleph::sheaf

namespace aleph::sheaf::zigzag_detail {

using aleph::linalg::gf2::BitMatrix;

// Constant-Z/2 barcode driver. Ports `barcode_constant`.
[[nodiscard]] inline aleph::sheaf::ZigzagBarcode barcode_constant(
    const aleph::sheaf::RewriteTrace& trace, std::size_t dim_k) {
    using aleph::sheaf::ConstantZ2Sheaf;
    using aleph::sheaf::FlagComplex;
    using aleph::sheaf::OneSkeleton;
    using aleph::sheaf::build_flag_complex;

    const std::size_t n = trace.len();
    std::vector<StopData> g_stops;
    std::vector<StopData> d_stops;
    std::vector<BitMatrix> arrows_back;
    std::vector<BitMatrix> arrows_fwd;

    const OneSkeleton g0_skel = OneSkeleton::from_graph(*trace.graphs()[0]);
    FlagComplex prev_g_complex = build_flag_complex(g0_skel);
    {
        const ConstantZ2Sheaf g0_sheaf{prev_g_complex};
        std::vector<BitVec> g0_basis = h_k_homology_basis(g0_sheaf, prev_g_complex, dim_k);
        g_stops.push_back(StopData{.hk_dim = g0_basis.size(), .hk_basis = std::move(g0_basis)});
    }

    for (std::size_t i = 0; i < n; ++i) {
        const aleph::graph::Graph& g_after = *trace.graphs()[i + 1];
        const aleph::sheaf::Subgraph& d_sub = trace.steps()[i].pushout_complement;

        auto [d_skel, d_complex] = d_sub.flag_complex(g_after);
        (void)d_skel;
        const ConstantZ2Sheaf d_sheaf{d_complex};
        std::vector<BitVec> d_basis = h_k_homology_basis(d_sheaf, d_complex, dim_k);

        const OneSkeleton g_next_skel = OneSkeleton::from_graph(g_after);
        FlagComplex g_next_complex = build_flag_complex(g_next_skel);
        const ConstantZ2Sheaf g_next_sheaf{g_next_complex};
        std::vector<BitVec> g_next_basis = h_k_homology_basis(g_next_sheaf, g_next_complex, dim_k);

        // ψ_i: H_k(D_i) → H_k(G_i) (sub = D_i, sup = G_i = prev_g_complex).
        const ConstantZ2Sheaf prev_g_sheaf{prev_g_complex};
        BitMatrix psi = inclusion_induced_map_hk(d_complex, d_sheaf, d_basis, prev_g_complex,
                                                 prev_g_sheaf, g_stops[i].hk_basis, dim_k);
        // φ_i: H_k(D_i) → H_k(G_{i+1}).
        BitMatrix phi = inclusion_induced_map_hk(d_complex, d_sheaf, d_basis, g_next_complex,
                                                 g_next_sheaf, g_next_basis, dim_k);

        d_stops.push_back(StopData{.hk_dim = d_basis.size(), .hk_basis = std::move(d_basis)});
        arrows_back.push_back(std::move(psi));
        arrows_fwd.push_back(std::move(phi));
        g_stops.push_back(
            StopData{.hk_dim = g_next_basis.size(), .hk_basis = std::move(g_next_basis)});
        prev_g_complex = std::move(g_next_complex);
    }

    return decompose_zigzag(g_stops, d_stops, arrows_back, arrows_fwd, dim_k);
}

// Visibility barcode driver. Ports `barcode_visibility`.
[[nodiscard]] inline aleph::sheaf::ZigzagBarcode barcode_visibility(
    const aleph::sheaf::RewriteTrace& trace, std::size_t dim_k) {
    using aleph::sheaf::FlagComplex;
    using aleph::sheaf::OneSkeleton;
    using aleph::sheaf::VisibilitySheaf;
    using aleph::sheaf::build_flag_complex;

    const std::size_t n = trace.len();
    std::vector<StopData> g_stops;
    std::vector<StopData> d_stops;
    std::vector<BitMatrix> arrows_back;
    std::vector<BitMatrix> arrows_fwd;

    const aleph::graph::Graph& g0 = *trace.graphs()[0];
    const OneSkeleton g0_skel = OneSkeleton::from_graph(g0);
    FlagComplex prev_g_complex = build_flag_complex(g0_skel);
    VisibilitySheaf prev_g_sheaf = VisibilitySheaf::build_full(g0, prev_g_complex);
    {
        std::vector<BitVec> g0_basis = h_k_homology_basis(prev_g_sheaf, prev_g_complex, dim_k);
        g_stops.push_back(StopData{.hk_dim = g0_basis.size(), .hk_basis = std::move(g0_basis)});
    }

    for (std::size_t i = 0; i < n; ++i) {
        const aleph::graph::Graph& g_after = *trace.graphs()[i + 1];
        const aleph::sheaf::Subgraph& d_sub = trace.steps()[i].pushout_complement;

        auto [d_skel, d_complex] = d_sub.flag_complex(g_after);
        (void)d_skel;
        const VisibilitySheaf d_sheaf =
            VisibilitySheaf::build_full_from_subgraph(g_after, d_sub, d_complex);
        std::vector<BitVec> d_basis = h_k_homology_basis(d_sheaf, d_complex, dim_k);

        const OneSkeleton g_next_skel = OneSkeleton::from_graph(g_after);
        FlagComplex g_next_complex = build_flag_complex(g_next_skel);
        VisibilitySheaf g_next_sheaf = VisibilitySheaf::build_full(g_after, g_next_complex);
        std::vector<BitVec> g_next_basis = h_k_homology_basis(g_next_sheaf, g_next_complex, dim_k);

        BitMatrix psi = inclusion_induced_map_hk(d_complex, d_sheaf, d_basis, prev_g_complex,
                                                 prev_g_sheaf, g_stops[i].hk_basis, dim_k);
        BitMatrix phi = inclusion_induced_map_hk(d_complex, d_sheaf, d_basis, g_next_complex,
                                                 g_next_sheaf, g_next_basis, dim_k);

        d_stops.push_back(StopData{.hk_dim = d_basis.size(), .hk_basis = std::move(d_basis)});
        arrows_back.push_back(std::move(psi));
        arrows_fwd.push_back(std::move(phi));
        g_stops.push_back(
            StopData{.hk_dim = g_next_basis.size(), .hk_basis = std::move(g_next_basis)});
        prev_g_complex = std::move(g_next_complex);
        prev_g_sheaf = std::move(g_next_sheaf);
    }

    return decompose_zigzag(g_stops, d_stops, arrows_back, arrows_fwd, dim_k);
}

}  // namespace aleph::sheaf::zigzag_detail

export namespace aleph::sheaf {

// Compute the zigzag persistence barcode at homological dimension dim_k (0 or 1)
// for the given trace. Ports `compute_zigzag_barcode`.
[[nodiscard]] inline ZigzagBarcode compute_zigzag_barcode(const RewriteTrace& trace,
                                                          std::size_t dim_k) {
    assert(dim_k <= 1 && "M3c supports k in {0, 1}; higher k is M4+");
    switch (trace.sheaf_kind()) {
        case SheafKind::ConstantZ2:
            return zigzag_detail::barcode_constant(trace, dim_k);
        case SheafKind::Visibility:
            return zigzag_detail::barcode_visibility(trace, dim_k);
    }
    // Unreachable: SheafKind is exhaustively handled above.
    return ZigzagBarcode{};
}

}  // namespace aleph::sheaf

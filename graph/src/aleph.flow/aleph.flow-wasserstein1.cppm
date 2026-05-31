module;
// Wasserstein-1 distance via primal transportation simplex.
//
// For finite probability measures mu, nu on a metric space (V, d):
//   W_1(mu, nu) = min { sum_ij pi_ij d_ij : sum_j pi_ij = mu_i,
//                       sum_i pi_ij = nu_j, pi >= 0 }
//
// Implementation: northwest-corner BFS + stepping-stone simplex with
// cycle-finding via DFS in the bipartite (rows, cols) graph. Suitable for
// small n (<= ~10).
//
// Ported faithfully from aleph-engine/aleph-flow/src/wasserstein.rs. The
// Charnes RHS perturbation (1e-12) and the EPS (1e-10) degeneracy tolerance
// are copied verbatim for LP determinism.
//
// aleph_flags_isa: no exceptions. The Rust reference uses assert_eq! to
// enforce shape preconditions; we instead surface a recoverable
// std::expected<f64, W1Error> so callers do not throw. The numerical core is
// unchanged.
#include <cmath>
#include <cstddef>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

export module aleph.flow:wasserstein1;

import aleph.linalg.sparse;
import aleph.math;

export namespace aleph::flow {

using aleph::math::f64;
using aleph::linalg::sparse::DMatrix;

// Degeneracy tolerance for the transportation simplex (verbatim from
// wasserstein.rs: const EPS: f64 = 1e-10).
inline constexpr f64 WASSERSTEIN1_EPS = 1e-10;

// Shape mismatch between mu, nu, and the cost matrix. The Rust reference
// asserts these equalities; under aleph_flags_isa we report them instead.
enum class W1Error {
    LengthMismatch,  // mu.size() != nu.size()
    ShapeMismatch,   // dist is not n x n
};

}  // namespace aleph::flow

namespace aleph::flow::detail {

using aleph::math::f64;
using aleph::linalg::sparse::DMatrix;

using Cell = std::pair<std::size_t, std::size_t>;

struct BasisEntry {
    std::size_t i;
    std::size_t j;
    f64         flow;
};

// Reduced potentials u (rows) and v (cols), solved from the basis with
// u[0] = 0 and reduced cost zero on basic cells. Mirrors compute_duals.
struct Duals {
    std::vector<f64> u;
    std::vector<f64> v;
};

[[nodiscard]] inline Duals compute_duals(const std::vector<BasisEntry>& basis,
                                         const DMatrix& dist, std::size_t n) {
    std::vector<std::optional<f64>> u(n, std::nullopt);
    std::vector<std::optional<f64>> v(n, std::nullopt);
    u[0] = 0.0;
    for (;;) {
        bool progressed = false;
        for (const auto& e : basis) {
            const std::size_t i = e.i;
            const std::size_t j = e.j;
            if (u[i].has_value() && !v[j].has_value()) {
                v[j] = dist.at(i, j) - *u[i];
                progressed = true;
            } else if (!u[i].has_value() && v[j].has_value()) {
                u[i] = dist.at(i, j) - *v[j];
                progressed = true;
            }
        }
        if (!progressed) {
            break;
        }
    }
    Duals d;
    d.u.resize(n);
    d.v.resize(n);
    for (std::size_t k = 0; k < n; ++k) {
        d.u[k] = u[k].value_or(0.0);
        d.v[k] = v[k].value_or(0.0);
    }
    return d;
}

// Depth-first search for a stepping-stone cycle through the entering cell,
// alternating row/column moves over basic cells. Mirrors find_cycle::dfs.
[[nodiscard]] inline bool cycle_dfs(std::vector<Cell>& path,
                                    const std::vector<Cell>& basis_cells,
                                    Cell target, bool match_column) {
    const Cell last = path.back();
    if (path.size() >= 4) {
        const bool can_close =
            match_column ? (last.second == target.second) : (last.first == target.first);
        if (can_close) {
            return true;
        }
    }
    for (const auto& cell : basis_cells) {
        bool already = false;
        for (const auto& p : path) {
            if (p == cell) {
                already = true;
                break;
            }
        }
        if (already) {
            continue;
        }
        const bool connects =
            match_column ? (cell.second == last.second) : (cell.first == last.first);
        if (!connects) {
            continue;
        }
        path.push_back(cell);
        if (cycle_dfs(path, basis_cells, target, !match_column)) {
            return true;
        }
        path.pop_back();
    }
    return false;
}

[[nodiscard]] inline std::optional<std::vector<Cell>> find_cycle(
    const std::vector<BasisEntry>& basis, Cell entering) {
    std::vector<Cell> basis_cells;
    basis_cells.reserve(basis.size());
    for (const auto& e : basis) {
        basis_cells.emplace_back(e.i, e.j);
    }

    std::vector<Cell> path{entering};
    if (cycle_dfs(path, basis_cells, entering, /*match_column=*/true)) {
        return path;
    }
    path.clear();
    path.push_back(entering);
    if (cycle_dfs(path, basis_cells, entering, /*match_column=*/false)) {
        return path;
    }
    return std::nullopt;
}

[[nodiscard]] inline f64 primal_transportation_simplex(std::span<const f64> mu,
                                                       std::span<const f64> nu,
                                                       const DMatrix& dist) {
    const std::size_t n = mu.size();
    std::vector<BasisEntry> basis;
    {
        // Northwest-corner rule.
        std::vector<f64> mu_left(mu.begin(), mu.end());
        std::vector<f64> nu_left(nu.begin(), nu.end());
        std::size_t i = 0;
        std::size_t j = 0;
        while (i < n && j < n) {
            const f64 flow = mu_left[i] < nu_left[j] ? mu_left[i] : nu_left[j];
            basis.push_back(BasisEntry{i, j, flow});
            mu_left[i] -= flow;
            nu_left[j] -= flow;
            const f64 mu_abs = mu_left[i] < 0.0 ? -mu_left[i] : mu_left[i];
            const f64 nu_abs = nu_left[j] < 0.0 ? -nu_left[j] : nu_left[j];
            if (mu_abs < WASSERSTEIN1_EPS && nu_abs < WASSERSTEIN1_EPS && i + 1 < n &&
                j + 1 < n) {
                // Tie: insert placeholder and advance both.
                i += 1;
                basis.push_back(BasisEntry{i, j, 0.0});
                j += 1;
            } else if (mu_abs < WASSERSTEIN1_EPS) {
                i += 1;
            } else {
                j += 1;
            }
        }
    }

    const std::size_t max_iter = n * n * 10 + 10;
    for (std::size_t iter = 0; iter < max_iter; ++iter) {
        const Duals duals = compute_duals(basis, dist, n);
        std::optional<Cell> entering;
        f64 min_reduced = -WASSERSTEIN1_EPS;
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = 0; j < n; ++j) {
                bool in_basis = false;
                for (const auto& e : basis) {
                    if (e.i == i && e.j == j) {
                        in_basis = true;
                        break;
                    }
                }
                if (in_basis) {
                    continue;
                }
                const f64 reduced = dist.at(i, j) - duals.u[i] - duals.v[j];
                if (reduced < min_reduced) {
                    min_reduced = reduced;
                    entering = Cell{i, j};
                }
            }
        }
        if (!entering.has_value()) {
            break;
        }

        const std::optional<std::vector<Cell>> cycle_opt = find_cycle(basis, *entering);
        if (!cycle_opt.has_value()) {
            break;
        }
        const std::vector<Cell>& cycle = *cycle_opt;

        f64                 leaving_value = std::numeric_limits<f64>::infinity();
        std::optional<Cell> leaving_cell;
        for (std::size_t k = 1; k < cycle.size(); ++k) {
            if (k % 2 == 1) {
                f64 val = 0.0;
                for (const auto& e : basis) {
                    if (e.i == cycle[k].first && e.j == cycle[k].second) {
                        val = e.flow;
                        break;
                    }
                }
                if (val < leaving_value) {
                    leaving_value = val;
                    leaving_cell = cycle[k];
                }
            }
        }
        if (std::isinf(leaving_value)) {
            break;
        }
        const Cell leaving = *leaving_cell;

        for (std::size_t k = 0; k < cycle.size(); ++k) {
            const f64 sign = (k % 2 == 0) ? 1.0 : -1.0;
            bool found = false;
            for (auto& e : basis) {
                if (e.i == cycle[k].first && e.j == cycle[k].second) {
                    e.flow += sign * leaving_value;
                    found = true;
                    break;
                }
            }
            if (!found) {
                basis.push_back(BasisEntry{cycle[k].first, cycle[k].second, sign * leaving_value});
            }
        }
        for (std::size_t idx = 0; idx < basis.size(); ++idx) {
            if (basis[idx].i == leaving.first && basis[idx].j == leaving.second) {
                // swap_remove
                basis[idx] = basis.back();
                basis.pop_back();
                break;
            }
        }
    }

    f64 total = 0.0;
    for (const auto& e : basis) {
        total += e.flow * dist.at(e.i, e.j);
    }
    return total;
}

}  // namespace aleph::flow::detail

export namespace aleph::flow {

// Compute W_1(mu, nu) on a finite metric space.
//
// On success returns the optimal transport cost. Returns W1Error when the
// shapes disagree (mu/nu lengths or dist not n x n) — the Rust reference
// asserts these; we report rather than throw (aleph_flags_isa).
//
// Charnes perturbation (verbatim from wasserstein.rs): break degeneracy of
// the transportation LP by adding tiny per-row offsets to supply/demand.
// For eps = 1e-12 the optimal basis is unchanged for these small
// (n <= ~10) instances and the perturbation contribution to W_1 is well
// below 1e-9.
[[nodiscard]] inline std::expected<f64, W1Error> wasserstein_1(std::span<const f64> mu,
                                                               std::span<const f64> nu,
                                                               const DMatrix& dist) {
    const std::size_t n = mu.size();
    if (nu.size() != n) {
        return std::unexpected(W1Error::LengthMismatch);
    }
    if (dist.rows() != n || dist.cols() != n) {
        return std::unexpected(W1Error::ShapeMismatch);
    }
    if (n == 0) {
        return 0.0;
    }

    const f64        eps = 1e-12;
    std::vector<f64> mu_pert(n);
    f64              mu_sum = 0.0;
    f64              mu_pert_sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        mu_pert[i] = mu[i] + eps * static_cast<f64>(i + 1);
        mu_sum += mu[i];
        mu_pert_sum += mu_pert[i];
    }
    const f64 extra = mu_pert_sum - mu_sum;
    const f64 per_col = extra / static_cast<f64>(n);
    std::vector<f64> nu_pert(n);
    for (std::size_t j = 0; j < n; ++j) {
        nu_pert[j] = nu[j] + per_col;
    }

    return detail::primal_transportation_simplex(mu_pert, nu_pert, dist);
}

}  // namespace aleph::flow

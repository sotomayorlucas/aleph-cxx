// Wasserstein-2 distance via Sinkhorn-Knopp entropic regularisation.
//
// For finite probability measures mu, nu on a metric space (V, d) and
// regularisation strength epsilon > 0, the entropic-regularised problem is
//
//     W2^eps(mu, nu)^2 = min_pi  <pi, M^2> - eps H(pi)
//           pi : sum_j pi_ij = mu_i, sum_i pi_ij = nu_j, pi >= 0
//
// where M_ij = d(i, j) and H(pi) = -sum pi_ij log pi_ij.
//
// The minimiser is unique and computable by Sinkhorn-Knopp fixed-point
// iteration. We implement the iteration in the log-domain (with
// log-sum-exp) so it does not underflow at small epsilon.
//
// Caveat: for eps > 0, W2^eps <= W2 (entropic bias). The bias goes to
// zero as eps -> 0, but small eps needs more iterations. eps = 0.05 is a
// good default for graph-distance cost matrices with d in {0, 1, 2, ...}.
//
// Ported faithfully from aleph-engine/aleph-flow/src/wasserstein2.rs.
// Determinism (spec 7.5): all f64, no parallelism, fixed column-major
// iteration order. Sinkhorn tol = 1e-9 (guaranteed only for eps >= 1e-3).
// Per aleph_flags_isa there are no exceptions; the Rust `assert!`
// preconditions are mirrored with `assert` (a precondition contract, not
// an exception), and degenerate-mass guards return 0.0 as in the Rust.

module;
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

export module aleph.flow:wasserstein2;

import aleph.math;

namespace aleph::flow {

using aleph::math::f64;
using std::size_t;

// log of each entry, with log(0) := -inf (sentinel).
inline std::vector<f64> log_vec(const std::vector<f64>& x) {
    std::vector<f64> out;
    out.reserve(x.size());
    for (const f64 v : x) {
        out.push_back(v > 0.0 ? std::log(v) : -std::numeric_limits<f64>::infinity());
    }
    return out;
}

}  // namespace aleph::flow

export namespace aleph::flow {

// Numerically stable log sum_k exp(x_k) via max-shift.
//
// Returns -inf if all inputs are -inf.
[[nodiscard]] inline f64 log_sum_exp(const std::vector<f64>& xs) {
    if (xs.empty()) {
        return -std::numeric_limits<f64>::infinity();
    }
    f64 m = -std::numeric_limits<f64>::infinity();
    for (const f64 x : xs) {
        if (x > m) m = x;
    }
    if (std::isinf(m) && m < 0.0) {
        return -std::numeric_limits<f64>::infinity();
    }
    f64 s = 0.0;
    for (const f64 x : xs) {
        if (std::isinf(x) && x < 0.0) {
            // exp(-inf) = 0; skip.
            continue;
        }
        s += std::exp(x - m);
    }
    return m + std::log(s);
}

// Compute W2^eps(mu, nu) (the unsquared Wasserstein-2 distance) via
// Sinkhorn-Knopp in log-domain.
//
// - mu, nu: probability vectors (must have equal length, both
//   non-negative; ideally summing to 1).
// - cost_sq: row-major n x n matrix of squared distances M^2_ij = d(i, j)^2.
// - epsilon: regularisation strength. Typical: 0.05.
// - max_iter: cap on Sinkhorn iterations. Typical: 1000.
//
// Returns sqrt(<pi, M^2>) where pi is the optimal transport plan. Returns
// 0.0 for empty support or all-zero measures.
//
// Preconditions (mirroring the Rust `assert!`): epsilon > 0 and the
// dimensions of mu, nu, and cost_sq are consistent (n x n).
[[nodiscard]] inline f64 wasserstein2_sinkhorn(const std::vector<f64>& mu,
                                               const std::vector<f64>& nu,
                                               const std::vector<std::vector<f64>>& cost_sq,
                                               f64 epsilon,
                                               size_t max_iter) {
    if (epsilon <= 0.0) {
        return std::numeric_limits<f64>::quiet_NaN();
    }
    const size_t n = mu.size();
    assert(nu.size() == n && "mu and nu must have the same length");
    if (n == 0) {
        return 0.0;
    }
    assert(cost_sq.size() == n && "cost_sq must have n rows");
    for (const auto& row : cost_sq) {
        assert(row.size() == n && "cost_sq must be n x n");
        (void)row;
    }

    // Degenerate-mass guards.
    f64 mu_sum = 0.0;
    for (const f64 v : mu) mu_sum += v;
    f64 nu_sum = 0.0;
    for (const f64 v : nu) nu_sum += v;
    if (mu_sum <= 0.0 || nu_sum <= 0.0) {
        return 0.0;
    }

    // Log-domain potentials. We work with f_i, g_j such that
    //   pi_ij = exp((f_i + g_j - M^2_ij) / eps)
    // and equivalently u_i = exp(f_i / eps), v_j = exp(g_j / eps).
    //
    // The Sinkhorn update preserves the marginals row-then-column:
    //   f_i = eps * (log mu_i - LSE_j[ (g_j - M^2_ij) / eps ])
    //   g_j = eps * (log nu_j - LSE_i[ (f_i - M^2_ij) / eps ])
    //
    // To avoid log(0) on rows/cols where the input measure has zero
    // mass, we treat -inf as a sentinel and propagate it: if mu_i = 0
    // then row i is empty and we set f_i = -inf (no mass from i).
    const std::vector<f64> log_mu = log_vec(mu);
    const std::vector<f64> log_nu = log_vec(nu);
    std::vector<f64> f(n, 0.0);
    std::vector<f64> g(n, 0.0);

    // Per-iteration scratch buffers.
    std::vector<f64> tmp_row(n, 0.0);
    std::vector<f64> tmp_col(n, 0.0);

    f64 last_obj = std::numeric_limits<f64>::infinity();
    const f64 tol = 1.0e-9;

    for (size_t it = 0; it < max_iter; ++it) {
        // f-update: f_i = eps * log(mu_i) - eps * LSE_j[ (g_j - M^2_ij) / eps ].
        for (size_t i = 0; i < n; ++i) {
            if (std::isinf(log_mu[i])) {
                f[i] = -std::numeric_limits<f64>::infinity();
                continue;
            }
            for (size_t j = 0; j < n; ++j) {
                tmp_row[j] = (g[j] - cost_sq[i][j]) / epsilon;
            }
            const f64 lse = log_sum_exp(tmp_row);
            f[i] = epsilon * (log_mu[i] - lse);
        }
        // g-update: g_j = eps * log(nu_j) - eps * LSE_i[ (f_i - M^2_ij) / eps ].
        for (size_t j = 0; j < n; ++j) {
            if (std::isinf(log_nu[j])) {
                g[j] = -std::numeric_limits<f64>::infinity();
                continue;
            }
            for (size_t i = 0; i < n; ++i) {
                tmp_col[i] = (f[i] - cost_sq[i][j]) / epsilon;
            }
            const f64 lse = log_sum_exp(tmp_col);
            g[j] = epsilon * (log_nu[j] - lse);
        }

        // Objective: <pi, M^2> where pi_ij = exp((f_i + g_j - M^2_ij) / eps).
        f64 obj_sq = 0.0;
        for (size_t i = 0; i < n; ++i) {
            if (std::isinf(f[i])) {
                continue;
            }
            for (size_t j = 0; j < n; ++j) {
                if (std::isinf(g[j])) {
                    continue;
                }
                const f64 arg = (f[i] + g[j] - cost_sq[i][j]) / epsilon;
                const f64 pi_ij = std::exp(arg);
                obj_sq += pi_ij * cost_sq[i][j];
            }
        }

        const f64 denom = std::fmax(std::fabs(last_obj), 1.0e-30);
        const f64 rel = std::fabs(obj_sq - last_obj) / denom;
        if (rel < tol) {
            return std::sqrt(std::fmax(obj_sq, 0.0));
        }
        last_obj = obj_sq;
    }

    return std::sqrt(std::fmax(last_obj, 0.0));
}

}  // namespace aleph::flow

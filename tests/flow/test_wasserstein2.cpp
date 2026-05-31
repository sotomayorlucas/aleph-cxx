#include "doctest.h"

#include <cmath>
#include <limits>
#include <vector>

import aleph.flow;

using aleph::flow::log_sum_exp;
using aleph::flow::wasserstein2_sinkhorn;

namespace {
constexpr double EPS = 0.05;

double sq(double d) { return d * d; }
}  // namespace

// -- log_sum_exp unit oracles (wasserstein2.rs::tests) ----------------------

TEST_CASE("log_sum_exp basic: log(e^0 + e^0) = log 2") {
    std::vector<double> xs{0.0, 0.0};
    const double r = log_sum_exp(xs);
    CHECK(std::fabs(r - std::log(2.0)) < 1e-12);
}

TEST_CASE("log_sum_exp skips -inf entries") {
    std::vector<double> xs{0.0, -std::numeric_limits<double>::infinity(), 0.0};
    const double r = log_sum_exp(xs);
    CHECK(std::fabs(r - std::log(2.0)) < 1e-12);
}

TEST_CASE("log_sum_exp of all -inf is -inf") {
    std::vector<double> xs{-std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity()};
    const double r = log_sum_exp(xs);
    CHECK(std::isinf(r));
    CHECK(r < 0.0);
}

TEST_CASE("log_sum_exp large inputs do not overflow (max-shift)") {
    // Without max-shift this would overflow.
    std::vector<double> xs{1000.0, 1000.0};
    const double r = log_sum_exp(xs);
    CHECK(std::fabs(r - (1000.0 + std::log(2.0))) < 1e-9);
}

// -- wasserstein2_sinkhorn unit oracles (wasserstein2.rs::tests) ------------

TEST_CASE("W2 empty supports is zero") {
    std::vector<std::vector<double>> cost_sq;
    const double w = wasserstein2_sinkhorn({}, {}, cost_sq, 0.05, 100);
    CHECK(std::fabs(w) < 1e-12);
}

TEST_CASE("W2 dirac-self is zero") {
    std::vector<std::vector<double>> cost_sq{{0.0}};
    const double w = wasserstein2_sinkhorn({1.0}, {1.0}, cost_sq, 0.05, 100);
    CHECK(std::fabs(w) < 1e-9);
}

TEST_CASE("W2 same distribution is near zero") {
    // Uniform on 2 points, same on both sides => optimal coupling is the
    // diagonal => W2 = 0 exactly. With entropic regularisation, a tiny
    // off-diagonal flow leaks eps*d^2 in mass.
    std::vector<std::vector<double>> cost_sq{{0.0, sq(1.0)}, {sq(1.0), 0.0}};
    std::vector<double> mu{0.5, 0.5};
    const double w = wasserstein2_sinkhorn(mu, mu, cost_sq, 0.05, 1000);
    CHECK(w < 1e-3);
}

TEST_CASE("W2 dirac at distance recovers the distance") {
    // mu = delta_0, nu = delta_1, d(0,1) = 5. Only feasible plan ships all
    // mass 0->1, cost = 1*25 = 25, W2 = sqrt(25) = 5.
    std::vector<std::vector<double>> cost_sq{{0.0, sq(5.0)}, {sq(5.0), 0.0}};
    const double w = wasserstein2_sinkhorn({1.0, 0.0}, {0.0, 1.0}, cost_sq, 0.05, 1000);
    CHECK(std::fabs(w - 5.0) < 1e-3);
}

TEST_CASE("W2 eps-to-zero limits unbiased") {
    std::vector<std::vector<double>> cost_sq{{0.0, sq(5.0)}, {sq(5.0), 0.0}};
    const double w_loose = wasserstein2_sinkhorn({1.0, 0.0}, {0.0, 1.0}, cost_sq, 0.5, 2000);
    const double w_tight = wasserstein2_sinkhorn({1.0, 0.0}, {0.0, 1.0}, cost_sq, 0.05, 2000);
    CHECK(std::fabs(w_loose - 5.0) < 1e-2);
    CHECK(std::fabs(w_tight - 5.0) < 1e-6);
}

TEST_CASE("W2 4-cycle uniform neighbours") {
    // Uniform measures on opposite vertices' neighbours in a 4-cycle. The
    // true W2^2 is 1*0.5*1^2 + 1*0.5*1^2 = 1 => W2 = 1.0.
    std::vector<std::vector<double>> cost_sq{
        {0.0, sq(1.0), sq(2.0), sq(1.0)},
        {sq(1.0), 0.0, sq(1.0), sq(2.0)},
        {sq(2.0), sq(1.0), 0.0, sq(1.0)},
        {sq(1.0), sq(2.0), sq(1.0), 0.0},
    };
    const double w = wasserstein2_sinkhorn({0.0, 0.5, 0.0, 0.5},
                                           {0.5, 0.0, 0.5, 0.0}, cost_sq, 0.05, 2000);
    // Bias allowance: W2^eps <= W2 = 1. Sinkhorn at eps=0.05 should give
    // w in [0.95, 1.05].
    CHECK(std::fabs(w - 1.0) < 0.05);
}

TEST_CASE("W2 smaller epsilon, smaller bias") {
    // For non-trivial mu != nu the entropic bias is strictly positive;
    // smaller eps => smaller bias. We don't pin exact values -- just
    // monotone-in-eps. True W2 = 2.
    std::vector<std::vector<double>> cost_sq{
        {0.0, sq(1.0), sq(2.0)},
        {sq(1.0), 0.0, sq(1.0)},
        {sq(2.0), sq(1.0), 0.0},
    };
    std::vector<double> mu{1.0, 0.0, 0.0};
    std::vector<double> nu{0.0, 0.0, 1.0};
    const double w_loose = wasserstein2_sinkhorn(mu, nu, cost_sq, 0.5, 2000);
    const double w_med = wasserstein2_sinkhorn(mu, nu, cost_sq, 0.1, 2000);
    const double w_tight = wasserstein2_sinkhorn(mu, nu, cost_sq, 0.02, 2000);
    // All <= 2 + tiny numerical fuzz.
    CHECK(w_loose <= 2.0 + 1e-6);
    CHECK(w_med <= 2.0 + 1e-6);
    CHECK(w_tight <= 2.0 + 1e-6);
    // Monotone in eps.
    CHECK(w_loose <= w_med + 1e-9);
    CHECK(w_med <= w_tight + 1e-9);
    // Tight should be within 1% of the true value.
    CHECK(std::fabs(w_tight - 2.0) < 0.02);
}

// -- integration oracles (wasserstein2_correctness.rs) ----------------------
// Only the direct wasserstein2_sinkhorn cases live here; the
// ricci_curvature_w2 / Laplacian cases belong to other partitions.

TEST_CASE("diagonal W2 small at small epsilon") {
    // For mu = nu the true W2 is 0, but the entropic estimator's bias
    // grows with eps. Verify the bias is small only at small eps.
    std::vector<std::vector<double>> cost_sq{
        {0.0, 1.0, 4.0}, {1.0, 0.0, 1.0}, {4.0, 1.0, 0.0}};
    std::vector<double> mu{0.4, 0.3, 0.3};
    const double w_tight = wasserstein2_sinkhorn(mu, mu, cost_sq, 0.05, 2000);
    CHECK(w_tight < 0.05);
    const double w_loose = wasserstein2_sinkhorn(mu, mu, cost_sq, 0.5, 2000);
    CHECK(w_tight <= w_loose + 1e-9);
}

TEST_CASE("dirac at distance recovers distance (cost_sq = 9)") {
    // mu = delta_0, nu = delta_1, d(0,1) = 3. Squared distance = 9.
    // Only feasible plan ships 1 unit 0->1, cost = 9, W2 = 3.
    std::vector<std::vector<double>> cost_sq{{0.0, 9.0}, {9.0, 0.0}};
    const double w = wasserstein2_sinkhorn({1.0, 0.0}, {0.0, 1.0}, cost_sq, EPS, 1000);
    CHECK(std::fabs(w - 3.0) < 1e-3);
}

TEST_CASE("triangle inequality holds within eps bias") {
    // Three distributions on a 4-node path: 0-1-2-3 with unit edges.
    // mu = delta_0, nu = delta_3, rho = delta_1.
    // True: W2(mu, nu) = 3, W2(mu, rho) = 1, W2(rho, nu) = 2.
    // Triangle: 3 <= 1 + 2 = 3 (saturated).
    std::vector<std::vector<double>> cost_sq{
        {0.0, 1.0, 4.0, 9.0},
        {1.0, 0.0, 1.0, 4.0},
        {4.0, 1.0, 0.0, 1.0},
        {9.0, 4.0, 1.0, 0.0},
    };
    std::vector<double> mu{1.0, 0.0, 0.0, 0.0};
    std::vector<double> rho{0.0, 1.0, 0.0, 0.0};
    std::vector<double> nu{0.0, 0.0, 0.0, 1.0};
    const double dist_mu_nu = wasserstein2_sinkhorn(mu, nu, cost_sq, EPS, 2000);
    const double dist_mu_rho = wasserstein2_sinkhorn(mu, rho, cost_sq, EPS, 2000);
    const double dist_rho_nu = wasserstein2_sinkhorn(rho, nu, cost_sq, EPS, 2000);
    // Allow a generous eps-bias slack on the entropic estimator.
    CHECK(dist_mu_nu <= dist_mu_rho + dist_rho_nu + 0.01);
}

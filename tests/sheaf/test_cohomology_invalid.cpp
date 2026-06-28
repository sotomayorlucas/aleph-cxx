#include "doctest.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

import aleph.math;
import aleph.flow;
import aleph.types;
import aleph.graph;
import aleph.sheaf;

using aleph::types::EdgeKind;
using aleph::types::Mesh;
using aleph::types::NodeId;
using aleph::types::SphereLocal;
using aleph::graph::Graph;
using aleph::math::Vec3;
using aleph::sheaf::OneSkeleton;
using aleph::sheaf::build_flag_complex;

namespace {

Graph make_fully_connected(std::size_t n) {
    Graph g;
    std::vector<NodeId> ids;
    ids.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const NodeId id = g.alloc_node_id();
        Mesh m{id, std::string("m"), 0};
        m.geometry = SphereLocal{Vec3{static_cast<float>(i), 0, 0}, 0.5f};
        g.insert_node(std::move(m));
        ids.push_back(id);
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            (void)g.add_edge(EdgeKind::Adjacent, ids[i], ids[j]);
        }
    }
    return g;
}

}  // namespace

TEST_CASE("regression_flag_complex_32_clique: oversized clique does not abort") {
    const Graph g = make_fully_connected(32);
    const auto fc = build_flag_complex(OneSkeleton::from_graph(g));
    CHECK(fc.max_dim >= 1);
    CHECK(fc.simplices.size() > 1);
    CHECK(!fc.simplices[1].empty());
}

TEST_CASE("regression_wasserstein2_nonpositive_epsilon: returns NaN not UB") {
    std::vector<std::vector<double>> cost_sq{{0.0, 1.0}, {1.0, 0.0}};
    const double w =
        aleph::flow::wasserstein2_sinkhorn({0.5, 0.5}, {0.5, 0.5}, cost_sq, 0.0, 100);
    CHECK(std::isnan(w));
    const double w_neg =
        aleph::flow::wasserstein2_sinkhorn({0.5, 0.5}, {0.5, 0.5}, cost_sq, -1.0, 100);
    CHECK(std::isnan(w_neg));
}

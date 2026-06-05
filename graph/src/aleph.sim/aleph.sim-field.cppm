module;
#include <cstddef>
#include <vector>

export module aleph.sim:field;

import aleph.math;     // f64
import aleph.types;    // NodeId

export namespace aleph::sim {

using aleph::math::f64;
using aleph::types::NodeId;

// A scalar field over a Laplacian's node_order: φ (displacement) and φ̇ (velocity)
// as dense f64 buffers. `order` is a *copy* of WeightedLaplacian::node_order so the
// field owns its index layout independently of the operator (survives a rebuild).
struct ScalarField {
    std::vector<NodeId> order;
    std::vector<f64>    phi;
    std::vector<f64>    phi_dot;

    [[nodiscard]] std::size_t size() const noexcept { return order.size(); }

    // Zeroed field for a given node order.
    [[nodiscard]] static ScalarField zeros(std::vector<NodeId> node_order) {
        const std::size_t n = node_order.size();
        return ScalarField{std::move(node_order), std::vector<f64>(n, 0.0),
                           std::vector<f64>(n, 0.0)};
    }

    // Velocity impulse at node `n`. Returns false (no-op) if `n` is not in `order`.
    [[nodiscard]] bool kick(NodeId n, f64 amp) noexcept {
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (order[i] == n) { phi_dot[i] += amp; return true; }
        }
        return false;
    }

    // Re-project onto a NEW node order after an edit: surviving NodeIds carry their
    // (phi, phi_dot) forward; new NodeIds start at 0; deleted ones drop. O(n·m) but
    // n,m are small here; deterministic (iterates new_order in order).
    void reproject(const std::vector<NodeId>& new_order) {
        std::vector<f64> np(new_order.size(), 0.0);
        std::vector<f64> nv(new_order.size(), 0.0);
        for (std::size_t i = 0; i < new_order.size(); ++i) {
            for (std::size_t j = 0; j < order.size(); ++j) {
                if (order[j] == new_order[i]) { np[i] = phi[j]; nv[i] = phi_dot[j]; break; }
            }
        }
        order = new_order;
        phi = std::move(np);
        phi_dot = std::move(nv);
    }
};

}  // namespace aleph::sim

module;
#include <cstddef>
#include <vector>

export module aleph.sim:section;

import aleph.math;     // f64, Vec3
import aleph.types;    // NodeId

export namespace aleph::sim {

using aleph::math::f64;
using aleph::types::NodeId;

// A section of the cell complex: one value of type T per node, dense + ordered to
// match a WeightedLaplacian::node_order. `order` is a *copy* of the operator's
// node_order so the section owns its index layout independently of the operator
// (survives a rebuild). T must be zero-initializable (T{} == 0) and support
// `T operator+(T)` — that is ALL of Section's own four methods use. Scalar scaling
// lives only in the steppers (instantiated for Section<f64> this slice;
// aleph::math::Vec3 scales by f32, not f64 — a future Vec3 stepper handles that).
// BOTH f64 and Vec3 satisfy the Section contract.
//
// ALL members are defined INLINE-IN-CLASS (this codebase exports class templates —
// SmallVector, dense_index — with every member inline; out-of-line class-template
// member definitions across C++26 modules are an under-tested toolchain path).
template<class T>
struct Section {
    std::vector<NodeId> order;
    std::vector<T>      data;

    [[nodiscard]] std::size_t size() const noexcept { return order.size(); }

    // Zeroed section for a given node order.
    [[nodiscard]] static Section zeros(std::vector<NodeId> node_order) {
        const std::size_t n = node_order.size();
        return Section{std::move(node_order), std::vector<T>(n, T{})};
    }

    // Accumulate `v` at node `n`; false (no-op) if `n` is not in `order`.
    // (Generalizes ScalarField::kick.)
    [[nodiscard]] bool add(NodeId n, const T& v) noexcept {
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (order[i] == n) { data[i] = data[i] + v; return true; }
        }
        return false;
    }

    // Value at node `n`, or nullptr if absent.
    [[nodiscard]] const T* at(NodeId n) const noexcept {
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (order[i] == n) return &data[i];
        }
        return nullptr;
    }

    // Re-project onto a NEW node order after an edit: surviving NodeIds carry their
    // value forward; new NodeIds start at T{}; deleted ones drop. O(n·m) but n,m are
    // small here; deterministic (iterates new_order in order).
    // Precondition: new_order contains no duplicate NodeIds (guaranteed by
    // WeightedLaplacian::node_order; duplicates would produce aliased state).
    void reproject(const std::vector<NodeId>& new_order) {
        std::vector<T> nd(new_order.size(), T{});
        for (std::size_t i = 0; i < new_order.size(); ++i) {
            for (std::size_t j = 0; j < order.size(); ++j) {
                if (order[j] == new_order[i]) { nd[i] = data[j]; break; }
            }
        }
        order = new_order;
        data = std::move(nd);
    }
};

}  // namespace aleph::sim

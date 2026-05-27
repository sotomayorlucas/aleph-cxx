module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>

export module aleph.graph:invariants;

import aleph.containers;
import aleph.types;
import :graph;

export namespace aleph::graph {

enum class InvariantError : std::uint8_t {
    TypedNodes              = 0,
    TypedEdges              = 1,
    EdgeEndpointsExist      = 2,
    EdgeTypeCompat          = 3,
    TransformAcyclic        = 4,
    CameraExclusive         = 5,
    MaterialReferenced      = 6,
    UniqueIds               = 7,
    ContainsAntireflexive   = 8,
    BoundedDegree           = 9,
};

inline constexpr std::array<std::string_view, 10> INVARIANT_NAMES = {
    "TypedNodes",
    "TypedEdges",
    "EdgeEndpointsExist",
    "EdgeTypeCompat",
    "TransformAcyclic",
    "CameraExclusive",
    "MaterialReferenced",
    "UniqueIds",
    "ContainsAntireflexive",
    "BoundedDegree",
};

// ── Invariant 1: TypedNodes ─────────────────────────────────────────
inline std::expected<void, InvariantError>
check_typed_nodes(const Graph&) noexcept { return {}; }

// ── Invariant 2: TypedEdges ─────────────────────────────────────────
inline std::expected<void, InvariantError>
check_typed_edges(const Graph&) noexcept { return {}; }

// ── Invariant 3: EdgeEndpointsExist ─────────────────────────────────
inline std::expected<void, InvariantError>
check_edge_endpoints_exist(const Graph& g) noexcept {
    for (auto [eid, e] : g.edges()) {
        if (!g.node(e.src) || !g.node(e.dst)) {
            return std::unexpected(InvariantError::EdgeEndpointsExist);
        }
    }
    return {};
}

// ── Invariant 4: EdgeTypeCompat ─────────────────────────────────────
inline std::expected<void, InvariantError>
check_edge_type_compat(const Graph& g) noexcept {
    for (auto [eid, e] : g.edges()) {
        const aleph::types::Node* sn = g.node(e.src);
        const aleph::types::Node* dn = g.node(e.dst);
        if (!sn || !dn) {
            return std::unexpected(InvariantError::EdgeTypeCompat);
        }
        if (!aleph::types::allows(e.kind, aleph::types::kind_of(*sn), aleph::types::kind_of(*dn))) {
            return std::unexpected(InvariantError::EdgeTypeCompat);
        }
    }
    return {};
}

// ── Invariant 5: TransformAcyclic ───────────────────────────────────
// DFS over Contains edges restricted to Transform nodes.
// Bounded: SmallVector with N=128 covers reasonable scenes; spills to
// heap above that (SmallVector behavior).
inline std::expected<void, InvariantError>
check_transform_acyclic(const Graph& g) noexcept {
    enum : std::uint8_t { WHITE = 0, GRAY = 1, BLACK = 2 };
    aleph::containers::SmallVector<aleph::types::NodeId, 128> transforms;
    for (auto [nid, n] : g.nodes()) {
        if (aleph::types::kind_of(n) == aleph::types::NodeKind::Transform) {
            transforms.push_back(nid);
        }
    }
    const std::size_t N = transforms.size();
    if (N == 0) return {};

    // Find index of nid in transforms[]; returns SIZE_MAX if not Transform.
    auto find_idx = [&](aleph::types::NodeId nid) -> std::size_t {
        for (std::size_t i = 0; i < N; ++i) {
            if (transforms[i] == nid) return i;
        }
        return SIZE_MAX;
    };

    aleph::containers::SmallVector<std::uint8_t, 128> color;
    for (std::size_t i = 0; i < N; ++i) color.push_back(WHITE);

    // Iterative DFS. Stack: vector of (node-idx, child-iter-position).
    struct Frame { std::size_t u; std::size_t child_seen; };
    aleph::containers::SmallVector<Frame, 128> stack;

    for (std::size_t root = 0; root < N; ++root) {
        if (color[root] != WHITE) continue;
        stack.push_back({root, 0});
        color[root] = GRAY;
        while (stack.size() > 0) {
            std::size_t sp = stack.size() - 1;
            Frame& top = stack[sp];
            // Walk Contains edges to find children of transforms[top.u].
            // Re-iterate to find the (top.child_seen)-th child (0-indexed next child).
            std::size_t seen = 0;
            std::size_t next_child = SIZE_MAX;
            for (auto [eid, e] : g.edges()) {
                if (e.kind != aleph::types::EdgeKind::Contains) continue;
                if (e.src != transforms[top.u]) continue;
                const std::size_t v = find_idx(e.dst);
                if (v == SIZE_MAX) continue;
                if (seen == top.child_seen) {
                    next_child = v;
                    break;
                }
                ++seen;
            }
            if (next_child != SIZE_MAX) {
                stack[sp].child_seen++;
                if (color[next_child] == GRAY) return std::unexpected(InvariantError::TransformAcyclic);
                if (color[next_child] == WHITE) {
                    color[next_child] = GRAY;
                    stack.push_back({next_child, 0});
                }
            } else {
                color[top.u] = BLACK;
                // Manually shrink stack by decrementing capacity-like management.
                // Since SmallVector doesn't expose pop_back, we'll rebuild it.
                aleph::containers::SmallVector<Frame, 128> new_stack;
                for (std::size_t i = 0; i < sp; ++i) {
                    new_stack.push_back(stack[i]);
                }
                stack = std::move(new_stack);
            }
        }
    }
    return {};
}

}  // namespace aleph::graph

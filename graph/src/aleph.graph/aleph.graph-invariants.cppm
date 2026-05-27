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

// ── Invariant 6: CameraExclusive ────────────────────────────────────
inline std::expected<void, InvariantError>
check_camera_exclusive(const Graph& g) noexcept {
    std::size_t n = 0;
    for (auto [nid, node] : g.nodes()) {
        if (aleph::types::kind_of(node) == aleph::types::NodeKind::Camera) ++n;
    }
    return (n == 1) ? std::expected<void, InvariantError>{}
                    : std::unexpected(InvariantError::CameraExclusive);
}

// ── Invariant 7: MaterialReferenced ─────────────────────────────────
inline std::expected<void, InvariantError>
check_material_referenced(const Graph& g) noexcept {
    for (auto [mid, mnode] : g.nodes()) {
        if (aleph::types::kind_of(mnode) != aleph::types::NodeKind::Mesh) continue;
        std::size_t mat_refs = 0;
        for (auto [eid, e] : g.edges()) {
            if (e.kind != aleph::types::EdgeKind::References) continue;
            if (e.src != mid) continue;
            const aleph::types::Node* dn = g.node(e.dst);
            if (dn && aleph::types::kind_of(*dn) == aleph::types::NodeKind::Material) ++mat_refs;
        }
        if (mat_refs != 1) return std::unexpected(InvariantError::MaterialReferenced);
    }
    return {};
}

// ── Invariant 8: UniqueIds ──────────────────────────────────────────
// OrderedMap key uniqueness already enforces this. Vacuous PASS.
inline std::expected<void, InvariantError>
check_unique_ids(const Graph&) noexcept { return {}; }

// ── Invariant 9: ContainsAntireflexive ──────────────────────────────
// No pair (a, b) has BOTH Contains(a, b) AND Contains(b, a). Use
// SmallVector to collect pairs (avoids std::vector in module body).
inline std::expected<void, InvariantError>
check_contains_antireflexive(const Graph& g) noexcept {
    struct Pair { aleph::types::NodeId a; aleph::types::NodeId b; };
    aleph::containers::SmallVector<Pair, 128> pairs;
    for (auto [eid, e] : g.edges()) {
        if (e.kind == aleph::types::EdgeKind::Contains) {
            pairs.push_back(Pair{e.src, e.dst});
        }
    }
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        const auto p = pairs[i];
        for (std::size_t j = 0; j < pairs.size(); ++j) {
            const auto q = pairs[j];
            if (p.a == q.b && p.b == q.a) {
                return std::unexpected(InvariantError::ContainsAntireflexive);
            }
        }
    }
    return {};
}

// ── Invariant 10: BoundedDegree ─────────────────────────────────────
inline std::expected<void, InvariantError>
check_bounded_degree(const Graph& g, std::size_t limit) noexcept {
    for (auto [nid, n] : g.nodes()) {
        if (g.in_degree(nid) > limit) {
            return std::unexpected(InvariantError::BoundedDegree);
        }
    }
    return {};
}

// ── validate_all: runs every check in canonical order ───────────────
inline std::expected<void, InvariantError>
validate_all(const Graph& g, std::size_t max_in_degree) noexcept {
    if (auto r = check_typed_nodes(g);            !r.has_value()) return r;
    if (auto r = check_typed_edges(g);            !r.has_value()) return r;
    if (auto r = check_edge_endpoints_exist(g);   !r.has_value()) return r;
    if (auto r = check_edge_type_compat(g);       !r.has_value()) return r;
    if (auto r = check_transform_acyclic(g);      !r.has_value()) return r;
    if (auto r = check_camera_exclusive(g);       !r.has_value()) return r;
    if (auto r = check_material_referenced(g);    !r.has_value()) return r;
    if (auto r = check_unique_ids(g);             !r.has_value()) return r;
    if (auto r = check_contains_antireflexive(g); !r.has_value()) return r;
    if (auto r = check_bounded_degree(g, max_in_degree); !r.has_value()) return r;
    return {};
}

}  // namespace aleph::graph

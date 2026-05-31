module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <tuple>

export module aleph.dpo:pattern;

import aleph.containers;
import aleph.types;

export namespace aleph::dpo {

// ── Pattern-local identifiers ───────────────────────────────────────
// Strong typedefs over u32, distinct from aleph::types::NodeId/EdgeId.
// A Pattern is a graph in its own little universe; these ids index the
// pattern's nodes/edges and are mapped to host-graph ids by the matcher.

struct PatternNodeId {
    std::uint32_t v{};
    constexpr explicit PatternNodeId(std::uint32_t value = 0) noexcept : v{value} {}
    constexpr bool operator==(const PatternNodeId& o) const noexcept { return v == o.v; }
    constexpr bool operator!=(const PatternNodeId& o) const noexcept { return v != o.v; }
    constexpr bool operator< (const PatternNodeId& o) const noexcept { return v <  o.v; }
};

struct PatternEdgeId {
    std::uint32_t v{};
    constexpr explicit PatternEdgeId(std::uint32_t value = 0) noexcept : v{value} {}
    constexpr bool operator==(const PatternEdgeId& o) const noexcept { return v == o.v; }
    constexpr bool operator!=(const PatternEdgeId& o) const noexcept { return v != o.v; }
    constexpr bool operator< (const PatternEdgeId& o) const noexcept { return v <  o.v; }
};

// ── Attribute predicate ─────────────────────────────────────────────
// Optional refinement on a node constraint beyond its NodeKind. The
// predicate inspects the candidate host Node; `description` is for debug
// printing / drift diagnostics.
struct AttrPredicate {
    std::function<bool(const aleph::types::Node&)> pred;
    std::string                                    description;
};

// ── Node constraint ─────────────────────────────────────────────────
// A pattern node matches a host node iff kinds agree and (if present)
// the attribute predicate holds.
struct NodeConstraint {
    aleph::types::NodeKind        kind{};
    std::optional<AttrPredicate>  attrs{};
};

// ── Pattern ─────────────────────────────────────────────────────────
// nodes: pattern-local id -> constraint, in insertion order.
// edges: pattern-local id -> (src pattern node, dst pattern node, kind).
struct Pattern {
    aleph::containers::OrderedMap<PatternNodeId, NodeConstraint> nodes;
    aleph::containers::OrderedMap<
        PatternEdgeId,
        std::tuple<PatternNodeId, PatternNodeId, aleph::types::EdgeKind>> edges;
};

}  // namespace aleph::dpo

// Hash specializations so OrderedMap<PatternNodeId, V> /
// OrderedMap<PatternEdgeId, V> can key on the strong typedefs.
template <> struct std::hash<aleph::dpo::PatternNodeId> {
    std::size_t operator()(aleph::dpo::PatternNodeId k) const noexcept {
        return std::hash<std::uint32_t>{}(k.v);
    }
};

template <> struct std::hash<aleph::dpo::PatternEdgeId> {
    std::size_t operator()(aleph::dpo::PatternEdgeId k) const noexcept {
        return std::hash<std::uint32_t>{}(k.v);
    }
};

module;
#include <cstddef>
#include <tuple>
#include <vector>

export module aleph.dpo:matcher;

import aleph.containers;
import aleph.types;
import aleph.graph;
import :pattern;
import :rule;

export namespace aleph::dpo {

// ── Match ───────────────────────────────────────────────────────────
// An embedding m: L -> G. node_map maps every lhs pattern node to a host
// node; edge_map maps every lhs pattern edge to the host edge realising
// it. Move-only (OrderedMap is move-only).
struct Match {
    aleph::containers::OrderedMap<PatternNodeId, aleph::types::NodeId> node_map;
    aleph::containers::OrderedMap<PatternEdgeId, aleph::types::EdgeId> edge_map;
};

}  // namespace aleph::dpo

namespace aleph::dpo::detail {

// Flattened, copyable scratch views of a Pattern (OrderedMap is move-only
// and not random-access, so we snapshot into vectors once up front and
// iterate those during backtracking — preserving insertion order).
struct PatNode { PatternNodeId id; NodeConstraint const* c; };
struct PatEdge {
    PatternEdgeId          id;
    PatternNodeId          src;
    PatternNodeId          dst;
    aleph::types::EdgeKind kind;
};

inline std::vector<PatNode> flatten_nodes(const Pattern& p) {
    std::vector<PatNode> out;
    for (auto [pid, c] : p.nodes) out.push_back(PatNode{pid, &c});
    return out;
}

inline std::vector<PatEdge> flatten_edges(const Pattern& p) {
    std::vector<PatEdge> out;
    for (auto [eid, t] : p.edges) {
        out.push_back(PatEdge{eid, std::get<0>(t), std::get<1>(t), std::get<2>(t)});
    }
    return out;
}

// Does host node `h` satisfy pattern constraint `c`?
inline bool node_ok(const NodeConstraint& c,
                    const aleph::types::Node& h) {
    if (aleph::types::kind_of(h) != c.kind) return false;
    if (c.attrs.has_value()) {
        if (!c.attrs->pred) return false;          // ill-formed predicate
        if (!c.attrs->pred(h)) return false;
    }
    return true;
}

// Find the host edge (insertion order) of kind `k` from host src->dst.
// Returns true + writes the EdgeId; false if none exists.
inline bool find_host_edge(const aleph::graph::Graph& g,
                           aleph::types::NodeId src,
                           aleph::types::NodeId dst,
                           aleph::types::EdgeKind k,
                           aleph::types::EdgeId& out) {
    for (auto [eid, e] : g.edges()) {
        if (e.kind == k && e.src == src && e.dst == dst) {
            out = eid;
            return true;
        }
    }
    return false;
}

// Backtracking assignment. assign[i] holds the host NodeId chosen for
// pat_nodes[i]; valid[i] flags whether assigned. Edges are checked
// incrementally: an edge is validated as soon as both its endpoints are
// assigned.
struct State {
    const aleph::graph::Graph&     g;
    const std::vector<PatNode>&    pat_nodes;
    const std::vector<PatEdge>&    pat_edges;
    std::vector<aleph::types::NodeId> assign;
    std::vector<bool>                 valid;
    // Host node ids in insertion order, for deterministic candidate scan.
    std::vector<aleph::types::NodeId> host_order;
};

inline std::size_t index_of(const std::vector<PatNode>& nodes, PatternNodeId id) {
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].id == id) return i;
    }
    return static_cast<std::size_t>(-1);
}

// Verify every pattern edge whose endpoints are both bound and that
// touches the slot just bound (index `just`). An edge is satisfied iff a
// host edge of the same kind connects the mapped endpoints. Returns false
// on the first unsatisfiable edge.
inline bool edges_consistent(State& s, std::size_t just) {
    for (std::size_t ei = 0; ei < s.pat_edges.size(); ++ei) {
        const PatEdge& pe = s.pat_edges[ei];
        const std::size_t si = index_of(s.pat_nodes, pe.src);
        const std::size_t di = index_of(s.pat_nodes, pe.dst);
        if (si == static_cast<std::size_t>(-1) ||
            di == static_cast<std::size_t>(-1)) return false;
        if (!s.valid[si] || !s.valid[di]) continue;   // not fully bound yet
        if (si != just && di != just) continue;        // already validated
        aleph::types::EdgeId hit{};
        if (!find_host_edge(s.g, s.assign[si], s.assign[di], pe.kind, hit)) {
            return false;
        }
    }
    return true;
}

// Recursive DFS over pattern-node slots in pattern (insertion) order.
inline void recurse(State& s, std::size_t slot,
                    std::vector<Match>& out) {
    if (slot == s.pat_nodes.size()) {
        // Complete embedding: resolve every pattern edge to its host edge
        // (all are satisfiable — validated incrementally), then emit.
        Match m;
        for (std::size_t i = 0; i < s.pat_nodes.size(); ++i) {
            m.node_map.insert(s.pat_nodes[i].id, s.assign[i]);
        }
        for (std::size_t ei = 0; ei < s.pat_edges.size(); ++ei) {
            const PatEdge& pe = s.pat_edges[ei];
            const std::size_t si = index_of(s.pat_nodes, pe.src);
            const std::size_t di = index_of(s.pat_nodes, pe.dst);
            aleph::types::EdgeId hit{};
            if (!find_host_edge(s.g, s.assign[si], s.assign[di], pe.kind, hit)) {
                return;  // should not happen — edges validated incrementally
            }
            m.edge_map.insert(pe.id, hit);
        }
        out.push_back(std::move(m));
        return;
    }

    const NodeConstraint& c = *s.pat_nodes[slot].c;
    for (aleph::types::NodeId cand : s.host_order) {
        const aleph::types::Node* hn = s.g.node(cand);
        if (!hn) continue;
        if (!node_ok(c, *hn)) continue;
        // Injectivity: cand must not already be used by an earlier slot.
        bool used = false;
        for (std::size_t i = 0; i < slot; ++i) {
            if (s.valid[i] && s.assign[i] == cand) { used = true; break; }
        }
        if (used) continue;

        s.assign[slot] = cand;
        s.valid[slot]  = true;
        if (edges_consistent(s, slot)) {
            recurse(s, slot + 1, out);
        }
        s.valid[slot] = false;
    }
}

}  // namespace aleph::dpo::detail

export namespace aleph::dpo {

// ── find_matches ────────────────────────────────────────────────────
// VF2-style backtracking subgraph isomorphism of rule.lhs into g.
//
// Determinism contract: candidates are scanned in host insertion order
// (g.nodes()); pattern slots in pattern insertion order; matches are
// appended in discovery order. Re-running on an unchanged graph yields an
// identical sequence.
inline std::vector<Match> find_matches(const Rule& r,
                                       const aleph::graph::Graph& g) {
    std::vector<Match> out;

    const std::vector<detail::PatNode> pat_nodes = detail::flatten_nodes(r.lhs);
    const std::vector<detail::PatEdge> pat_edges = detail::flatten_edges(r.lhs);

    // Empty LHS matches once (the empty embedding) — but a rule with no
    // pattern nodes has nothing to anchor deletions/creations to, so we
    // treat it as a single trivial match only if it also has no edges.
    if (pat_nodes.empty()) {
        if (pat_edges.empty()) out.push_back(Match{});
        return out;
    }

    detail::State s{
        .g          = g,
        .pat_nodes  = pat_nodes,
        .pat_edges  = pat_edges,
        .assign     = std::vector<aleph::types::NodeId>(pat_nodes.size()),
        .valid      = std::vector<bool>(pat_nodes.size(), false),
        .host_order = {},
    };
    for (auto [nid, node] : g.nodes()) {
        (void)node;
        s.host_order.push_back(nid);
    }

    detail::recurse(s, 0, out);
    return out;
}

}  // namespace aleph::dpo

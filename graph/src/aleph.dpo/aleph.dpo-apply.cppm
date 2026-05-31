module;
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

export module aleph.dpo:apply;

import aleph.containers;
import aleph.types;
import aleph.graph;
import :pattern;
import :rule;
import :matcher;

export namespace aleph::dpo {

// ── RewriteRecord ───────────────────────────────────────────────────
// What an apply() did, in host-graph ids. created_* are fresh ids minted
// during apply; deleted_* are ids that existed in the pre-state but not
// the post-state.
struct RewriteRecord {
    std::vector<aleph::types::NodeId> created_nodes;
    std::vector<aleph::types::NodeId> deleted_nodes;
    std::vector<aleph::types::EdgeId> created_edges;
    std::vector<aleph::types::EdgeId> deleted_edges;
};

// ── ApplyError ──────────────────────────────────────────────────────
enum class ApplyError {
    DanglingEdgeAfterDelete,  // a CreateEdge endpoint was deleted / missing
    InvariantViolation,       // validate_all failed post-rewrite -> not committed
    AttrSetMismatch,          // ModifyAttr target had the wrong NodeKind
};

}  // namespace aleph::dpo

namespace aleph::dpo::detail {

inline bool contains_id(const std::vector<aleph::types::NodeId>& v,
                        aleph::types::NodeId id) {
    for (auto x : v) if (x == id) return true;
    return false;
}
inline bool contains_eid(const std::vector<aleph::types::EdgeId>& v,
                         aleph::types::EdgeId id) {
    for (auto x : v) if (x == id) return true;
    return false;
}

// Resolve a pattern edge spec (src,dst,kind) from a Pattern.
inline bool pattern_edge_spec(const Pattern& p, PatternEdgeId eid,
                              PatternNodeId& src, PatternNodeId& dst,
                              aleph::types::EdgeKind& kind) {
    const auto* t = p.edges.get(eid);
    if (!t) return false;
    src  = std::get<0>(*t);
    dst  = std::get<1>(*t);
    kind = std::get<2>(*t);
    return true;
}

// Advance `out`'s node allocator past every preserved node id so later
// allocations never collide with ids carried over from the pre-state.
inline void fast_forward_node_alloc(aleph::graph::Graph& out) {
    std::uint32_t max_id = 0;
    bool any = false;
    for (auto [nid, node] : out.nodes()) {
        (void)node; any = true;
        if (nid.value > max_id) max_id = nid.value;
    }
    if (any) while (out.alloc_node_id().value <= max_id) { /* drain */ }
}

}  // namespace aleph::dpo::detail

export namespace aleph::dpo {

// ── apply (transactional) ───────────────────────────────────────────
// Builds the post-state in a *snapshot* graph `post`, runs validate_all on
// it, and only on success commits it back (g = std::move(post)). The live
// graph `g` is read-only until that commit, so on any failure path g —
// including its node_count()/edge_count() — is left exactly as it was
// (transactional rollback by construction).
inline std::expected<RewriteRecord, ApplyError>
apply(const Rule& rule, const Match& m, aleph::graph::Graph& g) {
    using namespace aleph::types;
    RewriteRecord rec;

    // ── Pass 1 (read-only on g): resolve deletions + attr mods ──────────
    std::vector<NodeId> delete_nodes;
    std::vector<EdgeId> delete_edges;
    struct AttrMod { NodeId id; std::function<void(Node&)> mutate; };
    std::vector<AttrMod> attr_mods;

    for (const RuleAction& action : rule.side_effects) {
        if (const auto* de = std::get_if<DeleteEdge>(&action)) {
            const EdgeId* hid = m.edge_map.get(de->edge);
            if (!hid || !g.edge(*hid)) {
                return std::unexpected(ApplyError::DanglingEdgeAfterDelete);
            }
            if (!detail::contains_eid(delete_edges, *hid)) delete_edges.push_back(*hid);

        } else if (const auto* dn = std::get_if<DeleteNode>(&action)) {
            const NodeId* hid = m.node_map.get(dn->local);
            if (!hid || !g.node(*hid)) {
                return std::unexpected(ApplyError::DanglingEdgeAfterDelete);
            }
            if (!detail::contains_id(delete_nodes, *hid)) delete_nodes.push_back(*hid);
            for (auto [eid, e] : g.edges()) {            // cascade incident edges
                if (e.src == *hid || e.dst == *hid) {
                    if (!detail::contains_eid(delete_edges, eid)) delete_edges.push_back(eid);
                }
            }

        } else if (const auto* ma = std::get_if<ModifyAttr>(&action)) {
            const NodeId* hid = m.node_map.get(ma->node);
            if (!hid) return std::unexpected(ApplyError::AttrSetMismatch);
            const Node* live = g.node(*hid);
            if (!live || kind_of(*live) != ma->set.expect_kind) {
                return std::unexpected(ApplyError::AttrSetMismatch);
            }
            attr_mods.push_back({*hid, ma->set.mutate});
        }
        // CreateNode / CreateEdge handled in Pass 2 (they need `post`).
    }

    // ── Pass 2: build the post-state snapshot ───────────────────────────
    aleph::graph::Graph post;

    // 2a. Surviving nodes (preserve ids; apply ModifyAttr to the copy).
    for (auto [nid, node] : g.nodes()) {
        if (detail::contains_id(delete_nodes, nid)) continue;
        Node copy = node;
        for (const auto& mod : attr_mods) {
            if (mod.id == nid && mod.mutate) mod.mutate(copy);
        }
        post.insert_node(copy);
    }
    // Advance the node allocator past all preserved ids before minting new.
    detail::fast_forward_node_alloc(post);

    // resolve: pattern node id -> host node id. Seed with the embedding,
    // extend as CreateNode mints fresh ids off `post`.
    aleph::containers::OrderedMap<PatternNodeId, NodeId> resolve;
    for (auto [pid, nid] : m.node_map) resolve.insert(pid, nid);

    // 2b. Replay creations in side_effect order (a CreateNode always
    //     precedes the CreateEdge that uses it).
    for (const RuleAction& action : rule.side_effects) {
        if (const auto* cn = std::get_if<CreateNode>(&action)) {
            if (!cn->init.build) return std::unexpected(ApplyError::AttrSetMismatch);
            const NodeId nid = post.alloc_node_id();
            post.insert_node(cn->init.build(nid));
            resolve.insert(cn->local, nid);
            rec.created_nodes.push_back(nid);
        }
    }

    // 2c. Surviving edges (not deleted). Reconstructed via add_edge, which
    //     reassigns edge ids; the contract stabilises node ids + counts.
    for (auto [eid, e] : g.edges()) {
        if (detail::contains_eid(delete_edges, eid)) continue;
        auto r = post.add_edge(e.kind, e.src, e.dst);
        if (!r.has_value()) {
            return std::unexpected(ApplyError::DanglingEdgeAfterDelete);
        }
    }

    // 2d. Created edges (record their freshly minted ids).
    for (const RuleAction& action : rule.side_effects) {
        const auto* ce = std::get_if<CreateEdge>(&action);
        if (!ce) continue;
        PatternNodeId psrc{}, pdst{};
        EdgeKind kind{};
        if (!detail::pattern_edge_spec(rule.rhs, ce->edge, psrc, pdst, kind)) {
            return std::unexpected(ApplyError::DanglingEdgeAfterDelete);
        }
        const NodeId* hsrc = resolve.get(psrc);
        const NodeId* hdst = resolve.get(pdst);
        if (!hsrc || !hdst) return std::unexpected(ApplyError::DanglingEdgeAfterDelete);
        auto r = post.add_edge(kind, *hsrc, *hdst);
        if (!r.has_value()) {           // endpoint missing / type-incompatible
            return std::unexpected(ApplyError::DanglingEdgeAfterDelete);
        }
        rec.created_edges.push_back(*r);
    }

    // Record deletions for the report.
    for (auto id : delete_nodes) rec.deleted_nodes.push_back(id);
    for (auto id : delete_edges) rec.deleted_edges.push_back(id);

    // ── Pass 3: post-condition. Commit only if every invariant holds. ───
    // (max_in_degree = SIZE_MAX: DPO rewrites are not gated on a degree cap;
    //  callers that want one re-validate with their own bound afterwards.)
    if (auto ok = aleph::graph::validate_all(post, SIZE_MAX); !ok.has_value()) {
        return std::unexpected(ApplyError::InvariantViolation);  // g untouched
    }

    g = std::move(post);  // commit
    return rec;
}

}  // namespace aleph::dpo

module;
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <utility>

export module aleph.graph:graph;

import aleph.containers;
import aleph.types;

export namespace aleph::graph {

enum class GraphError {
    NodeNotFound,
    EdgeNotFound,
    EdgeTypeMismatch,
};

class Graph {
public:
    using NodeMap = aleph::containers::OrderedMap<aleph::types::NodeId,  aleph::types::Node>;
    using EdgeMap = aleph::containers::OrderedMap<aleph::types::EdgeId,  aleph::types::Edge>;

    Graph() = default;

    aleph::types::NodeId alloc_node_id() noexcept { return ids_.alloc_node(); }
    aleph::types::EdgeId alloc_edge_id() noexcept { return ids_.alloc_edge(); }

    void insert_node(aleph::types::Node n) {
        const aleph::types::NodeId id = aleph::types::id_of(n);
        const bool fresh = nodes_.insert(id, std::move(n));
        if (!fresh) std::abort();
    }

    const aleph::types::Node* node(aleph::types::NodeId id) const noexcept {
        return nodes_.get(id);
    }

    std::size_t node_count() const noexcept { return nodes_.size(); }
    std::size_t edge_count() const noexcept { return edges_.size(); }

    struct NodeRange {
        const NodeMap* m;
        NodeMap::const_iterator begin() const noexcept { return m->cbegin(); }
        NodeMap::const_iterator end()   const noexcept { return m->cend(); }
    };
    NodeRange nodes() const noexcept { return {&nodes_}; }

    // ── Edge ops ──────────────────────────────────────────────────
    std::expected<aleph::types::EdgeId, GraphError>
    add_edge(aleph::types::EdgeKind kind,
             aleph::types::NodeId   src,
             aleph::types::NodeId   dst) {
        const aleph::types::Node* sn = nodes_.get(src);
        if (!sn) return std::unexpected(GraphError::NodeNotFound);
        const aleph::types::Node* dn = nodes_.get(dst);
        if (!dn) return std::unexpected(GraphError::NodeNotFound);
        if (!aleph::types::allows(kind, aleph::types::kind_of(*sn), aleph::types::kind_of(*dn))) {
            return std::unexpected(GraphError::EdgeTypeMismatch);
        }
        const aleph::types::EdgeId id = ids_.alloc_edge();
        edges_.insert(id, aleph::types::Edge{id, kind, src, dst});
        return id;
    }

    const aleph::types::Edge* edge(aleph::types::EdgeId id) const noexcept {
        return edges_.get(id);
    }

    struct EdgeRange {
        const EdgeMap* m;
        EdgeMap::const_iterator begin() const noexcept { return m->cbegin(); }
        EdgeMap::const_iterator end()   const noexcept { return m->cend(); }
    };
    EdgeRange edges() const noexcept { return {&edges_}; }

    std::size_t in_degree(aleph::types::NodeId id) const noexcept {
        std::size_t n = 0;
        for (auto [eid, e] : edges_) if (e.dst == id) ++n;
        return n;
    }

    void remove_node_cascade(aleph::types::NodeId id) {
        aleph::containers::SmallVector<aleph::types::EdgeId, 16> incident;
        for (auto [eid, e] : edges_) {
            if (e.src == id || e.dst == id) incident.push_back(eid);
        }
        for (std::size_t i = 0; i < incident.size(); ++i) {
            (void)edges_.remove(incident[i]);
        }
        (void)nodes_.remove(id);
    }

    // An EXACT structural copy of this graph: same node ids, same edge ids
    // (src/dst/kind), and the same IdAllocator watermarks, so a clone is
    // indistinguishable from the original for every subsequent operation
    // (fresh ids continue from the same point). The node/edge maps are move-only
    // (OrderedMap), so this is the sanctioned deep copy — it rebuilds the maps by
    // copying each value. Used to snapshot g_before for a localized DPO rebuild
    // (decompose_rewrite + deleted-edge endpoint lookup), where Graph's
    // move-only-ness otherwise forces a rebuild-from-scratch.
    [[nodiscard]] Graph clone() const {
        Graph out;
        for (auto [id, node] : nodes_) {
            (void)id;
            out.nodes_.insert(aleph::types::id_of(node), node);
        }
        for (auto [eid, e] : edges_) {
            out.edges_.insert(eid, e);
        }
        out.ids_ = ids_;  // preserve watermarks so future ids don't collide
        return out;
    }

protected:
    NodeMap                    nodes_{};
    EdgeMap                    edges_{};
    aleph::types::IdAllocator  ids_{};
};

}  // namespace aleph::graph

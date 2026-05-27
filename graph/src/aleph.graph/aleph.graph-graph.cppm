module;
#include <cstddef>
#include <cstdlib>
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

    // T10 adds edge-side ops + true cascade. For T9, just remove the node.
    void remove_node_cascade(aleph::types::NodeId id) {
        (void)nodes_.remove(id);
    }

protected:
    NodeMap                    nodes_{};
    EdgeMap                    edges_{};
    aleph::types::IdAllocator  ids_{};
};

}  // namespace aleph::graph

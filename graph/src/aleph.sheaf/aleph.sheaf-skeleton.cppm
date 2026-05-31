module;
#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

export module aleph.sheaf:skeleton;

import aleph.graph;
import aleph.types;

export namespace aleph::sheaf {

// The 1-skeleton |G| used by M2: Mesh vertices + Adjacent edges.
//
// Ported from aleph-engine/aleph-sheaf/src/skeleton.rs. The Rust reference
// backs `vertices`/`edges` with `IndexSet`; per the C++ container rules there
// is no OrderedSet, so we use sorted-and-deduped std::vector — this preserves
// the set semantics (membership + counts) used by the Rust API and oracles
// while giving deterministic (sorted-ascending) iteration order.
struct OneSkeleton {
    // Mesh vertices of the scene graph.
    std::vector<aleph::types::NodeId> vertices;
    // Canonical (min, max) edges to avoid double-counting Adjacent pairs
    // (Adjacent is symmetric — add_edge may have inserted in either
    // direction).
    std::vector<std::pair<aleph::types::NodeId, aleph::types::NodeId>> edges;

    [[nodiscard]] bool contains_vertex(aleph::types::NodeId id) const noexcept {
        return std::binary_search(vertices.begin(), vertices.end(), id);
    }

    [[nodiscard]] bool contains_edge(aleph::types::NodeId a,
                                     aleph::types::NodeId b) const noexcept {
        const std::pair<aleph::types::NodeId, aleph::types::NodeId> key{a, b};
        return std::binary_search(edges.begin(), edges.end(), key,
                                  edge_less);
    }

    [[nodiscard]] bool operator==(const OneSkeleton& o) const noexcept {
        return vertices == o.vertices && edges == o.edges;
    }

    static OneSkeleton from_graph(const aleph::graph::Graph& g) {
        OneSkeleton skel;

        // Collect Mesh vertices.
        for (auto [id, node] : g.nodes()) {
            if (aleph::types::kind_of(node) == aleph::types::NodeKind::Mesh) {
                skel.vertices.push_back(id);
            }
        }
        std::sort(skel.vertices.begin(), skel.vertices.end());
        skel.vertices.erase(
            std::unique(skel.vertices.begin(), skel.vertices.end()),
            skel.vertices.end());

        // Collect canonical Adjacent edges between two mesh vertices.
        for (auto [eid, e] : g.edges()) {
            (void)eid;
            if (e.kind == aleph::types::EdgeKind::Adjacent &&
                skel.contains_vertex(e.src) && skel.contains_vertex(e.dst)) {
                const auto [a, b] = (e.src < e.dst)
                                        ? std::pair{e.src, e.dst}
                                        : std::pair{e.dst, e.src};
                skel.edges.emplace_back(a, b);
            }
        }
        std::sort(skel.edges.begin(), skel.edges.end(), edge_less);
        skel.edges.erase(std::unique(skel.edges.begin(), skel.edges.end()),
                         skel.edges.end());

        return skel;
    }

private:
    static bool edge_less(
        const std::pair<aleph::types::NodeId, aleph::types::NodeId>& lhs,
        const std::pair<aleph::types::NodeId, aleph::types::NodeId>& rhs)
        noexcept {
        if (lhs.first != rhs.first) return lhs.first < rhs.first;
        return lhs.second < rhs.second;
    }
};

}  // namespace aleph::sheaf

#include <string>

import aleph.graph;
import aleph.types;

int main() {
    aleph::graph::Graph g;
    auto a = g.alloc_node_id();
    g.insert_node(aleph::types::Mesh{a, std::string("x"), 1});
    auto b = g.alloc_node_id();
    g.insert_node(aleph::types::Mesh{b, std::string("y"), 1});
    auto e = g.add_edge(aleph::types::EdgeKind::Adjacent, a, b);
    if (!e.has_value()) return 1;
    return g.edge_count() == 1 ? 0 : 2;
}

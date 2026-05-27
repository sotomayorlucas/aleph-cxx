#include <cstdio>
#include <cstdlib>
#include <string>

import aleph.graph;
import aleph.types;

using namespace aleph::graph;
using namespace aleph::types;

int main() {
    Graph g;
    const auto root  = g.alloc_node_id();
    g.insert_node(Transform{root, 0});
    const auto child = g.alloc_node_id();
    g.insert_node(Transform{child, 1});
    const auto cam   = g.alloc_node_id();
    g.insert_node(Camera{cam, std::string("default")});
    const auto light = g.alloc_node_id();
    g.insert_node(Light{light, LightKind::Point, std::string("ies/std")});
    const auto a     = g.alloc_node_id();
    g.insert_node(Mesh{a, std::string("cube"), 12});
    const auto b     = g.alloc_node_id();
    g.insert_node(Mesh{b, std::string("sphere"), 320});
    const auto mat   = g.alloc_node_id();
    g.insert_node(Material{mat, MaterialKind::Lambertian});
    const auto tex   = g.alloc_node_id();
    g.insert_node(Texture{tex, 256, 256, TextureFormat::Rgb8});

    auto check = [&](auto r, const char* what) {
        if (!r.has_value()) {
            std::fprintf(stderr, "fixture: add_edge %s failed\n", what);
            std::exit(1);
        }
    };
    check(g.add_edge(EdgeKind::Contains,   root,  child),  "root -> child");
    check(g.add_edge(EdgeKind::Contains,   child, a),       "child -> mesh A");
    check(g.add_edge(EdgeKind::Contains,   child, b),       "child -> mesh B");
    check(g.add_edge(EdgeKind::Contains,   root,  cam),     "root -> camera");
    check(g.add_edge(EdgeKind::References, a,     mat),     "mesh A -> material");
    check(g.add_edge(EdgeKind::References, b,     mat),     "mesh B -> material");
    check(g.add_edge(EdgeKind::References, mat,   tex),     "material -> texture");
    check(g.add_edge(EdgeKind::Influences, light, a),       "light -> mesh A");

    std::printf("fixture: %zu nodes, %zu edges\n", g.node_count(), g.edge_count());

    const auto r = validate_all(g, /*max_in_degree*/ 8);
    if (!r.has_value()) {
        static const char* names[] = {
            "TypedNodes", "TypedEdges", "EdgeEndpointsExist", "EdgeTypeCompat",
            "TransformAcyclic", "CameraExclusive", "MaterialReferenced",
            "UniqueIds", "ContainsAntireflexive", "BoundedDegree",
        };
        const auto idx = static_cast<std::size_t>(r.error());
        std::fprintf(stderr, "fixture: FAILED invariant %zu (%s)\n", idx, names[idx]);
        return 2;
    }
    std::printf("fixture: all 10 invariants pass\n");
    return 0;
}

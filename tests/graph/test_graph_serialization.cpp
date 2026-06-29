#include "doctest.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

import aleph.graph;
import aleph.types;
import aleph.math;

using aleph::graph::Graph;
using aleph::types::NodeId;
using namespace aleph::types;

namespace {

struct EdgeKey {
    EdgeKind kind{};
    NodeId   src{};
    NodeId   dst{};
    bool operator==(const EdgeKey& o) const {
        return kind == o.kind && src.value == o.src.value && dst.value == o.dst.value;
    }
    bool operator<(const EdgeKey& o) const {
        if (kind != o.kind) return kind < o.kind;
        if (src.value != o.src.value) return src.value < o.src.value;
        return dst.value < o.dst.value;
    }
};

std::set<EdgeKey> edge_set(const Graph& g) {
    std::set<EdgeKey> out;
    for (auto [eid, e] : g.edges()) {
        (void)eid;
        out.insert(EdgeKey{e.kind, e.src, e.dst});
    }
    return out;
}

Graph make_sample() {
    Graph g;
    const NodeId root = g.alloc_node_id();
    g.insert_node(Transform{root, 0, LocalTransform{aleph::math::Mat4::identity()}});

    const NodeId cam = g.alloc_node_id();
    Camera camera{cam, std::string("sensor0")};
    camera.look_from = aleph::math::Vec3{0.0f, 1.0f, 5.0f};
    camera.look_at   = aleph::math::Vec3{0.0f, 0.0f, 0.0f};
    g.insert_node(std::move(camera));

    const NodeId mesh = g.alloc_node_id();
    Mesh m{mesh, std::string("sphere"), 0};
    m.geometry = SphereLocal{aleph::math::Vec3{0.0f, 0.5f, 0.5f}, 0.5f};
    g.insert_node(std::move(m));

    const NodeId mat = g.alloc_node_id();
    Material material{mat, MaterialKind::Lambertian};
    material.albedo = aleph::math::Vec3{0.8f, 0.2f, 0.2f};
    g.insert_node(std::move(material));

    (void)g.add_edge(EdgeKind::Contains, root, cam);
    (void)g.add_edge(EdgeKind::Contains, root, mesh);
    (void)g.add_edge(EdgeKind::References, mesh, mat);
    return g;
}

bool same_topology(const Graph& a, const Graph& b) {
    if (a.node_count() != b.node_count()) return false;
    if (edge_set(a) != edge_set(b)) return false;
    for (auto [id, node_a] : a.nodes()) {
        const Node* node_b = b.node(id);
        if (node_b == nullptr) return false;
        if (kind_of(node_a) != kind_of(*node_b)) return false;
        if (id_of(node_a) != id_of(*node_b)) return false;
    }
    return true;
}

constexpr const char* kIdentity =
    "1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1";
constexpr std::size_t kStringNpos = static_cast<std::size_t>(-1);

std::string minimal_graph_text() {
    return std::string{"aleph-graph/1\n"}
        + "root 0\n"
        + "node 0 transform 0 " + kIdentity + "\n"
        + "node 1 camera sensor0 0 1 5 0 0 0 0 1 0 45 0 1\n"
        + "edge contains 0 1\n";
}

}  // namespace

TEST_CASE("graph serialization: round-trip preserves topology") {
    Graph original = make_sample();
    const NodeId root{0};

    const std::string saved = aleph::graph::save_graph_string(original, root);
    CHECK(saved.starts_with("aleph-graph/1"));

    auto loaded = aleph::graph::load_graph_string(saved);
    REQUIRE(loaded.has_value());
    CHECK(loaded->root == root);
    CHECK(same_topology(original, loaded->graph));
}

TEST_CASE("graph serialization: file round-trip") {
    Graph original = make_sample();
    const NodeId root{0};
    const std::string path = "/tmp/aleph_graph_roundtrip.aleph";

    REQUIRE(aleph::graph::save_graph_file(original, root, path).has_value());
    auto loaded = aleph::graph::load_graph_file(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->root == root);
    CHECK(same_topology(original, loaded->graph));
}

TEST_CASE("graph serialization: rejects missing header") {
    std::string text = minimal_graph_text();
    text.erase(0, std::string{"aleph-graph/1\n"}.size());

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == aleph::graph::SerializationError::InvalidHeader);
}

TEST_CASE("graph serialization: rejects duplicate node id without abort") {
    const std::string text = std::string{"aleph-graph/1\n"}
        + "root 0\n"
        + "node 0 transform 0 " + kIdentity + "\n"
        + "node 0 camera sensor0 0 1 5 0 0 0 0 1 0 45 0 1\n";

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == aleph::graph::SerializationError::InvalidNode);
}

TEST_CASE("graph serialization: rejects numeric token with trailing garbage") {
    std::string text = minimal_graph_text();
    const std::string bad = "node 1 camera sensor0 0 1 5x 0 0 0 0 1 0 45 0 1";
    const std::string good = "node 1 camera sensor0 0 1 5 0 0 0 0 1 0 45 0 1";
    const std::size_t pos = text.find(good);
    REQUIRE(pos != kStringNpos);
    text.replace(pos, good.size(), bad);

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == aleph::graph::SerializationError::ParseError);
}

TEST_CASE("graph serialization: rejects trailing fields on a valid line") {
    std::string text = minimal_graph_text();
    const std::string old_root = "root 0\n";
    const std::size_t pos = text.find(old_root);
    REQUIRE(pos != kStringNpos);
    text.replace(pos, old_root.size(), "root 0 extra\n");

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == aleph::graph::SerializationError::ParseError);
}

TEST_CASE("graph serialization: rejects trailing fields on a valid node line") {
    std::string text = minimal_graph_text();
    const std::string good = "node 1 camera sensor0 0 1 5 0 0 0 0 1 0 45 0 1";
    const std::string bad = good + " extra";
    const std::size_t pos = text.find(good);
    REQUIRE(pos != kStringNpos);
    text.replace(pos, good.size(), bad);

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == aleph::graph::SerializationError::ParseError);
}

TEST_CASE("graph serialization: rejects missing root node") {
    std::string text = minimal_graph_text();
    const std::size_t pos = text.find("root 0\n");
    REQUIRE(pos != kStringNpos);
    text.replace(pos, std::string{"root 0\n"}.size(), "root 99\n");

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == aleph::graph::SerializationError::ParseError);
}

TEST_CASE("graph serialization: rejects invalid edge endpoint") {
    std::string text = minimal_graph_text();
    text += "edge contains 0 99\n";

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == aleph::graph::SerializationError::InvalidEdge);
}

TEST_CASE("graph serialization: rejects incompatible edge kind") {
    std::string text = minimal_graph_text();
    text += "edge references 0 1\n";

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == aleph::graph::SerializationError::InvalidEdge);
}

TEST_CASE("graph serialization: allocator advances past largest loaded node id") {
    const std::string text = std::string{"aleph-graph/1\n"}
        + "root 99\n"
        + "node 99 transform 0 " + kIdentity + "\n"
        + "node 100 camera sensor0 0 1 5 0 0 0 0 1 0 45 0 1\n"
        + "edge contains 99 100\n";

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE(loaded.has_value());
    CHECK(loaded->graph.alloc_node_id().value == 101);
}

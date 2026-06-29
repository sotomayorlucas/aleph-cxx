#include "doctest.h"

#include <cstdint>
#include <set>
#include <string>
#include <variant>
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

const Node& require_node(const Graph& g, NodeId id) {
    const Node* n = g.node(id);
    REQUIRE(n != nullptr);
    return *n;
}

void check_vec3(aleph::math::Vec3 actual, aleph::math::Vec3 expected, const char* label = nullptr) {
    if (label != nullptr) {
        INFO(label);
        CHECK(actual.x == doctest::Approx(expected.x));
        CHECK(actual.y == doctest::Approx(expected.y));
        CHECK(actual.z == doctest::Approx(expected.z));
        return;
    }
    CHECK(actual.x == doctest::Approx(expected.x));
    CHECK(actual.y == doctest::Approx(expected.y));
    CHECK(actual.z == doctest::Approx(expected.z));
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

TEST_CASE("graph serialization: round-trip preserves node payloads") {
    Graph original;
    const NodeId root = original.alloc_node_id();
    aleph::math::Mat4 root_xf = aleph::math::Mat4::identity();
    root_xf.m[12] = 2.0f;
    root_xf.m[13] = 3.0f;
    root_xf.m[14] = 4.0f;
    original.insert_node(Transform{root, 7, LocalTransform{root_xf}});

    const NodeId cam = original.alloc_node_id();
    Camera camera{cam, std::string("sensor wide\tmain")};
    camera.look_from  = aleph::math::Vec3{1.0f, 2.0f, 3.0f};
    camera.look_at    = aleph::math::Vec3{4.0f, 5.0f, 6.0f};
    camera.up         = aleph::math::Vec3{0.0f, 1.0f, 0.0f};
    camera.vfov_deg   = 55.0f;
    camera.aperture   = 0.25f;
    camera.focus_dist = 9.5f;
    original.insert_node(std::move(camera));

    const NodeId mesh_id = original.alloc_node_id();
    Mesh mesh{mesh_id, std::string("tri mesh\tasset"), 1};
    TriLocal tri{};
    tri.a = aleph::math::Vec3{0.0f, 0.0f, 0.0f};
    tri.b = aleph::math::Vec3{1.0f, 0.0f, 0.0f};
    tri.c = aleph::math::Vec3{0.0f, 1.0f, 0.0f};
    mesh.geometry = tri;
    original.insert_node(std::move(mesh));

    const NodeId sphere_mesh_id = original.alloc_node_id();
    Mesh sphere_mesh{sphere_mesh_id, std::string("sphere mesh\tasset"), 0};
    sphere_mesh.geometry = SphereLocal{aleph::math::Vec3{-2.0f, 3.5f, 4.25f}, 6.75f};
    original.insert_node(std::move(sphere_mesh));

    const NodeId mat_id = original.alloc_node_id();
    Material material{mat_id, MaterialKind::TexturedLambertian};
    material.albedo   = aleph::math::Vec3{0.1f, 0.2f, 0.3f};
    material.fuzz     = 0.4f;
    material.ior      = 1.45f;
    material.emit     = aleph::math::Vec3{0.5f, 0.6f, 0.7f};
    material.uv_scale = 12.0f;
    original.insert_node(material);

    const NodeId sphere_mat_id = original.alloc_node_id();
    original.insert_node(Material{sphere_mat_id, MaterialKind::Metal});

    const NodeId tex_id = original.alloc_node_id();
    original.insert_node(Texture{tex_id, 320, 200, TextureFormat::Rgb8});

    const NodeId light_id = original.alloc_node_id();
    Light light{light_id, LightKind::Directional, std::string("emit \"key\" \\bank")};
    light.emission = aleph::math::Vec3{10.0f, 11.0f, 12.0f};
    QuadLocal quad{};
    quad.q = aleph::math::Vec3{-1.0f, 0.0f, 0.0f};
    quad.u = aleph::math::Vec3{2.0f, 0.0f, 0.0f};
    quad.v = aleph::math::Vec3{0.0f, 2.0f, 0.0f};
    light.geometry = quad;
    original.insert_node(std::move(light));

    const NodeId volume_id = original.alloc_node_id();
    original.insert_node(Volume{volume_id, MediumKind::Heterogeneous});

    REQUIRE(original.add_edge(EdgeKind::Contains, root, cam).has_value());
    REQUIRE(original.add_edge(EdgeKind::Contains, root, mesh_id).has_value());
    REQUIRE(original.add_edge(EdgeKind::Contains, root, sphere_mesh_id).has_value());
    REQUIRE(original.add_edge(EdgeKind::Contains, root, light_id).has_value());
    REQUIRE(original.add_edge(EdgeKind::Contains, root, volume_id).has_value());
    REQUIRE(original.add_edge(EdgeKind::References, mesh_id, mat_id).has_value());
    REQUIRE(original.add_edge(EdgeKind::References, sphere_mesh_id, sphere_mat_id).has_value());
    REQUIRE(original.add_edge(EdgeKind::References, mat_id, tex_id).has_value());

    const std::string saved = aleph::graph::save_graph_string(original, root);
    auto loaded = aleph::graph::load_graph_string(saved);
    REQUIRE(loaded.has_value());
    CHECK(loaded->root == root);
    CHECK(loaded->graph.node_count() == 9);
    CHECK(loaded->graph.edge_count() == 8);
    CHECK(edge_set(loaded->graph) == edge_set(original));

    const auto* loaded_root = std::get_if<Transform>(&require_node(loaded->graph, root));
    REQUIRE(loaded_root != nullptr);
    CHECK(loaded_root->pose_slot == 7);
    CHECK(loaded_root->local.m.m[12] == doctest::Approx(2.0f));
    CHECK(loaded_root->local.m.m[13] == doctest::Approx(3.0f));
    CHECK(loaded_root->local.m.m[14] == doctest::Approx(4.0f));

    const auto* loaded_camera = std::get_if<Camera>(&require_node(loaded->graph, cam));
    REQUIRE(loaded_camera != nullptr);
    CHECK(loaded_camera->sensor_id == "sensor wide\tmain");
    check_vec3(loaded_camera->look_from, aleph::math::Vec3{1.0f, 2.0f, 3.0f}, "camera look_from");
    check_vec3(loaded_camera->look_at, aleph::math::Vec3{4.0f, 5.0f, 6.0f}, "camera look_at");
    check_vec3(loaded_camera->up, aleph::math::Vec3{0.0f, 1.0f, 0.0f}, "camera up");
    CHECK(loaded_camera->vfov_deg == doctest::Approx(55.0f));
    CHECK(loaded_camera->aperture == doctest::Approx(0.25f));
    CHECK(loaded_camera->focus_dist == doctest::Approx(9.5f));

    const auto* loaded_mesh = std::get_if<Mesh>(&require_node(loaded->graph, mesh_id));
    REQUIRE(loaded_mesh != nullptr);
    CHECK(loaded_mesh->geometry_ref == "tri mesh\tasset");
    CHECK(loaded_mesh->tris_count == 1);
    const auto* loaded_tri = std::get_if<TriLocal>(&loaded_mesh->geometry);
    REQUIRE(loaded_tri != nullptr);
    check_vec3(loaded_tri->a, aleph::math::Vec3{0.0f, 0.0f, 0.0f}, "tri a");
    check_vec3(loaded_tri->b, aleph::math::Vec3{1.0f, 0.0f, 0.0f}, "tri b");
    check_vec3(loaded_tri->c, aleph::math::Vec3{0.0f, 1.0f, 0.0f}, "tri c");

    const auto* loaded_sphere_mesh = std::get_if<Mesh>(&require_node(loaded->graph, sphere_mesh_id));
    REQUIRE(loaded_sphere_mesh != nullptr);
    CHECK(loaded_sphere_mesh->geometry_ref == "sphere mesh\tasset");
    CHECK(loaded_sphere_mesh->tris_count == 0);
    const auto* loaded_sphere = std::get_if<SphereLocal>(&loaded_sphere_mesh->geometry);
    REQUIRE(loaded_sphere != nullptr);
    check_vec3(loaded_sphere->center, aleph::math::Vec3{-2.0f, 3.5f, 4.25f}, "sphere center");
    CHECK(loaded_sphere->radius == doctest::Approx(6.75f));

    const auto* loaded_material = std::get_if<Material>(&require_node(loaded->graph, mat_id));
    REQUIRE(loaded_material != nullptr);
    CHECK(loaded_material->kind == MaterialKind::TexturedLambertian);
    check_vec3(loaded_material->albedo, aleph::math::Vec3{0.1f, 0.2f, 0.3f}, "material albedo");
    CHECK(loaded_material->fuzz == doctest::Approx(0.4f));
    CHECK(loaded_material->ior == doctest::Approx(1.45f));
    check_vec3(loaded_material->emit, aleph::math::Vec3{0.5f, 0.6f, 0.7f}, "material emit");
    CHECK(loaded_material->uv_scale == doctest::Approx(12.0f));

    const auto* loaded_sphere_material = std::get_if<Material>(&require_node(loaded->graph, sphere_mat_id));
    REQUIRE(loaded_sphere_material != nullptr);
    CHECK(loaded_sphere_material->kind == MaterialKind::Metal);

    const auto* loaded_texture = std::get_if<Texture>(&require_node(loaded->graph, tex_id));
    REQUIRE(loaded_texture != nullptr);
    CHECK(loaded_texture->width == 320);
    CHECK(loaded_texture->height == 200);
    CHECK(loaded_texture->format == TextureFormat::Rgb8);

    const auto* loaded_light = std::get_if<Light>(&require_node(loaded->graph, light_id));
    REQUIRE(loaded_light != nullptr);
    CHECK(loaded_light->kind == LightKind::Directional);
    CHECK(loaded_light->emit_ref == "emit \"key\" \\bank");
    check_vec3(loaded_light->emission, aleph::math::Vec3{10.0f, 11.0f, 12.0f}, "light emission");
    const auto* loaded_quad = std::get_if<QuadLocal>(&loaded_light->geometry);
    REQUIRE(loaded_quad != nullptr);
    check_vec3(loaded_quad->q, aleph::math::Vec3{-1.0f, 0.0f, 0.0f}, "quad q");
    check_vec3(loaded_quad->u, aleph::math::Vec3{2.0f, 0.0f, 0.0f}, "quad u");
    check_vec3(loaded_quad->v, aleph::math::Vec3{0.0f, 2.0f, 0.0f}, "quad v");

    const auto* loaded_volume = std::get_if<Volume>(&require_node(loaded->graph, volume_id));
    REQUIRE(loaded_volume != nullptr);
    CHECK(loaded_volume->medium == MediumKind::Heterogeneous);
}

TEST_CASE("graph serialization: round-trip preserves empty strings") {
    Graph original;
    const NodeId root = original.alloc_node_id();
    original.insert_node(Transform{root, 0, LocalTransform{aleph::math::Mat4::identity()}});

    const NodeId cam = original.alloc_node_id();
    Camera camera{cam, std::string{}};
    original.insert_node(std::move(camera));

    const NodeId light_id = original.alloc_node_id();
    Light light{light_id, LightKind::Directional, std::string{}};
    original.insert_node(std::move(light));

    REQUIRE(original.add_edge(EdgeKind::Contains, root, cam).has_value());
    REQUIRE(original.add_edge(EdgeKind::Contains, root, light_id).has_value());

    auto loaded = aleph::graph::load_graph_string(aleph::graph::save_graph_string(original, root));
    REQUIRE(loaded.has_value());

    const auto* loaded_camera = std::get_if<Camera>(&require_node(loaded->graph, cam));
    REQUIRE(loaded_camera != nullptr);
    CHECK(loaded_camera->sensor_id.empty());

    const auto* loaded_light = std::get_if<Light>(&require_node(loaded->graph, light_id));
    REQUIRE(loaded_light != nullptr);
    CHECK(loaded_light->emit_ref.empty());
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

TEST_CASE("graph serialization: rejects repeated header") {
    std::string text = minimal_graph_text();
    const std::size_t pos = text.find("node 0 transform");
    REQUIRE(pos != kStringNpos);
    text.insert(pos, "aleph-graph/1\n");

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == aleph::graph::SerializationError::InvalidHeader);
}

TEST_CASE("graph serialization: rejects missing root declaration") {
    std::string text = minimal_graph_text();
    const std::size_t pos = text.find("root 0\n");
    REQUIRE(pos != kStringNpos);
    text.erase(pos, std::string{"root 0\n"}.size());

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

TEST_CASE("graph serialization: rejects max node id that cannot advance allocator") {
    const std::string text = std::string{"aleph-graph/1\n"}
        + "root 0\n"
        + "node 0 transform 0 " + kIdentity + "\n"
        + "node 4294967295 camera sensor0 0 1 5 0 0 0 0 1 0 45 0 1\n"
        + "edge contains 0 4294967295\n";

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

TEST_CASE("graph serialization: rejects invalid numeric tokens") {
    struct BadNumericCase {
        const char* name;
        std::string text;
    };

    std::vector<BadNumericCase> cases;
    {
        std::string text = minimal_graph_text();
        const std::string good = "node 0 transform 0 ";
        const std::size_t pos = text.find(good);
        REQUIRE(pos != kStringNpos);
        text.replace(pos, good.size(), "node 0 transform -1 ");
        cases.push_back({"negative u32", text});
    }
    {
        std::string text = minimal_graph_text();
        const std::string good = "node 0 transform 0 ";
        const std::size_t pos = text.find(good);
        REQUIRE(pos != kStringNpos);
        text.replace(pos, good.size(), "node 0 transform 4294967296 ");
        cases.push_back({"overflowing u32", text});
    }
    {
        std::string text = minimal_graph_text();
        const std::string good = "node 1 camera sensor0 0 1 5 0 0 0 0 1 0 45 0 1";
        const std::string bad = "node 1 camera sensor0 0 1 nan 0 0 0 0 1 0 45 0 1";
        const std::size_t pos = text.find(good);
        REQUIRE(pos != kStringNpos);
        text.replace(pos, good.size(), bad);
        cases.push_back({"nan float", text});
    }
    {
        std::string text = minimal_graph_text();
        const std::string good = "node 1 camera sensor0 0 1 5 0 0 0 0 1 0 45 0 1";
        const std::string bad = "node 1 camera sensor0 0 1 inf 0 0 0 0 1 0 45 0 1";
        const std::size_t pos = text.find(good);
        REQUIRE(pos != kStringNpos);
        text.replace(pos, good.size(), bad);
        cases.push_back({"inf float", text});
    }

    for (const BadNumericCase& c : cases) {
        INFO(c.name);
        auto loaded = aleph::graph::load_graph_string(c.text);
        REQUIRE_FALSE(loaded.has_value());
        CHECK(loaded.error() == aleph::graph::SerializationError::ParseError);
    }
}

TEST_CASE("graph serialization: rejects quoted token without delimiter") {
    const std::string text = std::string{"aleph-graph/1\n"}
        + "root 0\n"
        + "node 0 transform 0 " + kIdentity + "\n"
        + "node 1 camera \"sensor0\"0 0 1 5 0 0 0 0 1 0 45 0 1\n"
        + "edge contains 0 1\n";

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == aleph::graph::SerializationError::ParseError);
}

TEST_CASE("graph serialization: rejects quoted token delimiter before following field") {
    const std::string text = std::string{"aleph-graph/1\n"}
        + "root 0\n"
        + "node 0 transform 0 " + kIdentity + "\n"
        + "node 1 mesh \"mesh-ref\"0 sphere 0 0 0 1\n"
        + "edge contains 0 1\n";

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == aleph::graph::SerializationError::ParseError);
}

TEST_CASE("graph serialization: rejects unterminated quoted string") {
    const std::string text = std::string{"aleph-graph/1\n"}
        + "root 0\n"
        + "node 0 transform 0 " + kIdentity + "\n"
        + "node 1 camera \" 0 1 5 0 0 0 0 1 0 45 0 1\n"
        + "edge contains 0 1\n";

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

TEST_CASE("graph serialization: rejects trailing fields on edge line") {
    std::string text = minimal_graph_text();
    const std::string good = "edge contains 0 1\n";
    const std::size_t pos = text.find(good);
    REQUIRE(pos != kStringNpos);
    text.replace(pos, good.size(), "edge contains 0 1 extra\n");

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == aleph::graph::SerializationError::ParseError);
}

TEST_CASE("graph serialization: rejects duplicate root declaration") {
    std::string text = minimal_graph_text();
    const std::size_t pos = text.find("node 0 transform");
    REQUIRE(pos != kStringNpos);
    text.insert(pos, "root 0\n");

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

TEST_CASE("graph serialization: accepts tab-separated fields") {
    std::string identity = kIdentity;
    for (char& c : identity) {
        if (c == ' ') c = '\t';
    }

    const std::string text = std::string{"aleph-graph/1\n"}
        + "root\t0\n"
        + "node\t0\ttransform\t0\t" + identity + "\n"
        + "node\t1\tcamera\tsensor0\t0\t1\t5\t0\t0\t0\t0\t1\t0\t45\t0\t1\n"
        + "edge\tcontains\t0\t1\n";

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE(loaded.has_value());
    CHECK(loaded->root == NodeId{0});
    CHECK(loaded->graph.node_count() == 2);
    CHECK(edge_set(loaded->graph).count(EdgeKey{EdgeKind::Contains, NodeId{0}, NodeId{1}}) == 1);
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

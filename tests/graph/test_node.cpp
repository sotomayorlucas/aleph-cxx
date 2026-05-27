#include "doctest.h"
#include <string>
#include <string_view>

import aleph.types;

using namespace aleph::types;

TEST_CASE("NodeKind: enum values and ALL array") {
    CHECK(static_cast<int>(NodeKind::Mesh)      == 0);
    CHECK(static_cast<int>(NodeKind::Material)  == 1);
    CHECK(static_cast<int>(NodeKind::Light)     == 2);
    CHECK(static_cast<int>(NodeKind::Volume)    == 3);
    CHECK(static_cast<int>(NodeKind::Camera)    == 4);
    CHECK(static_cast<int>(NodeKind::Texture)   == 5);
    CHECK(static_cast<int>(NodeKind::Transform) == 6);
    CHECK(all_node_kinds().size() == 7);
    CHECK(all_node_kinds()[0] == NodeKind::Mesh);
}

TEST_CASE("NodeKind::as_tla: lowercase strings matching scene_graph.tla") {
    CHECK(as_tla(NodeKind::Mesh)      == "mesh");
    CHECK(as_tla(NodeKind::Material)  == "material");
    CHECK(as_tla(NodeKind::Light)     == "light");
    CHECK(as_tla(NodeKind::Volume)    == "volume");
    CHECK(as_tla(NodeKind::Camera)    == "camera");
    CHECK(as_tla(NodeKind::Texture)   == "texture");
    CHECK(as_tla(NodeKind::Transform) == "transform");
}

TEST_CASE("Mesh node carries geometry ref + tri count") {
    Mesh m{NodeId{4}, std::string("cube"), 12};
    CHECK(m.id.value == 4);
    CHECK(m.geometry_ref == "cube");
    CHECK(m.tris_count == 12);
}

TEST_CASE("Material node carries kind tag only") {
    Material mat{NodeId{6}, MaterialKind::Lambertian};
    CHECK(mat.id.value == 6);
    CHECK(mat.kind == MaterialKind::Lambertian);
}

TEST_CASE("Light node carries kind + emit ref") {
    Light l{NodeId{3}, LightKind::Point, std::string("ies/std")};
    CHECK(l.id.value == 3);
    CHECK(l.kind == LightKind::Point);
    CHECK(l.emit_ref == "ies/std");
}

TEST_CASE("Volume node carries medium tag") {
    Volume v{NodeId{9}, MediumKind::Homogeneous};
    CHECK(v.id.value == 9);
    CHECK(v.medium == MediumKind::Homogeneous);
}

TEST_CASE("Camera node carries sensor ref") {
    Camera c{NodeId{2}, std::string("default")};
    CHECK(c.id.value == 2);
    CHECK(c.sensor_id == "default");
}

TEST_CASE("Texture node carries dims + format") {
    Texture t{NodeId{7}, 256, 256, TextureFormat::Rgb8};
    CHECK(t.width == 256);
    CHECK(t.format == TextureFormat::Rgb8);
}

TEST_CASE("Transform node carries pose slot") {
    Transform tr{NodeId{1}, 5};
    CHECK(tr.pose_slot == 5);
}

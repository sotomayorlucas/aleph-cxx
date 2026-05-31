module;
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

export module aleph.types:node;

import :id;
import :attribute;
import aleph.math;  // Vec3, Mat4

export namespace aleph::types {

// LOWERABLE LOCAL geometry payload — explicitly NOT the final mesh model.
// v1 = analytic primitives; later grows to TriangleMeshRef / ObjMesh / Procedural / Sdf
// without breaking the contract. Local space; world transform applied at lowering.
struct SphereLocal { math::Vec3 center{}; math::f32 radius{1}; };
struct QuadLocal   { math::Vec3 q{}, u{}, v{}; };
struct TriLocal    { math::Vec3 a{}, b{}, c{}; };
using GeometryPayload = std::variant<SphereLocal, QuadLocal, TriLocal>;

// Local-transform abstraction. Storage is Mat4 in v1, but the graph depends on
// LocalTransform (not a raw matrix) so it can grow to TRS / GA rotor / dual-quat /
// constraints later without churning every consumer.
struct LocalTransform { math::Mat4 m{ math::Mat4::identity() }; };

struct Mesh {        // semantic entity + a lowerable geometry payload
    NodeId        id{};
    std::string   geometry_ref;
    std::uint32_t tris_count{};
    GeometryPayload geometry{ SphereLocal{} };
};
struct Material {    // physical params; `emit` is a RENDERABLE property, not "this is a light"
    NodeId       id{};
    MaterialKind kind{MaterialKind::Lambertian};
    math::Vec3   albedo{0.8f, 0.8f, 0.8f};
    math::f32    fuzz{0};
    math::f32    ior{1.5f};
    math::Vec3   emit{0, 0, 0};
};
struct Light {       // an EXPLICIT sampling source in its own right (kept as a node)
    NodeId      id{};
    LightKind   kind{LightKind::Point};
    std::string emit_ref;
    math::Vec3      emission{1, 1, 1};
    GeometryPayload geometry{ QuadLocal{} };
};
struct Volume {
    NodeId     id{};
    MediumKind medium{MediumKind::Vacuum};
};
struct Camera {      // concrete pose
    NodeId      id{};
    std::string sensor_id;
    math::Vec3  look_from{0, 0, 0}, look_at{0, 0, -1}, up{0, 1, 0};
    math::f32   vfov_deg{40}, aperture{0}, focus_dist{1};
};
struct Texture {
    NodeId        id{};
    std::uint32_t width{};
    std::uint32_t height{};
    TextureFormat format{TextureFormat::Rgba8};
};
struct Transform {
    NodeId        id{};
    std::uint32_t pose_slot{};
    LocalTransform local{};
};

enum class NodeKind : std::uint8_t {
    Mesh      = 0,
    Material  = 1,
    Light     = 2,
    Volume    = 3,
    Camera    = 4,
    Texture   = 5,
    Transform = 6,
};

inline constexpr std::array<NodeKind, 7> all_node_kinds() noexcept {
    return {
        NodeKind::Mesh,      NodeKind::Material,
        NodeKind::Light,     NodeKind::Volume,
        NodeKind::Camera,    NodeKind::Texture,
        NodeKind::Transform,
    };
}

constexpr std::string_view as_tla(NodeKind k) noexcept {
    switch (k) {
        case NodeKind::Mesh:      return "mesh";
        case NodeKind::Material:  return "material";
        case NodeKind::Light:     return "light";
        case NodeKind::Volume:    return "volume";
        case NodeKind::Camera:    return "camera";
        case NodeKind::Texture:   return "texture";
        case NodeKind::Transform: return "transform";
    }
    return "";
}

using Node = std::variant<
    Mesh, Material, Light, Volume, Camera, Texture, Transform
>;

constexpr NodeKind kind_of(const Node& n) noexcept {
    return std::visit([](auto const& x) constexpr -> NodeKind {
        using T = std::decay_t<decltype(x)>;
        if constexpr      (std::is_same_v<T, Mesh>)      return NodeKind::Mesh;
        else if constexpr (std::is_same_v<T, Material>)  return NodeKind::Material;
        else if constexpr (std::is_same_v<T, Light>)     return NodeKind::Light;
        else if constexpr (std::is_same_v<T, Volume>)    return NodeKind::Volume;
        else if constexpr (std::is_same_v<T, Camera>)    return NodeKind::Camera;
        else if constexpr (std::is_same_v<T, Texture>)   return NodeKind::Texture;
        else                                              return NodeKind::Transform;
    }, n);
}

constexpr NodeId id_of(const Node& n) noexcept {
    return std::visit([](auto const& x) constexpr { return x.id; }, n);
}

}  // namespace aleph::types

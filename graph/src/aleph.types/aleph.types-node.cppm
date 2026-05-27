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

export namespace aleph::types {

struct Mesh {
    NodeId        id{};
    std::string   geometry_ref;
    std::uint32_t tris_count{};
};
struct Material {
    NodeId       id{};
    MaterialKind kind{MaterialKind::Lambertian};
};
struct Light {
    NodeId      id{};
    LightKind   kind{LightKind::Point};
    std::string emit_ref;
};
struct Volume {
    NodeId     id{};
    MediumKind medium{MediumKind::Vacuum};
};
struct Camera {
    NodeId      id{};
    std::string sensor_id;
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

module;
#include <array>
#include <cstdint>
#include <string_view>

export module aleph.types:edge;

import :id;
import :node;

export namespace aleph::types {

enum class EdgeKind : std::uint8_t {
    Adjacent   = 0,
    Contains   = 1,
    Influences = 2,
    References = 3,
};

inline constexpr std::array<EdgeKind, 4> all_edge_kinds() noexcept {
    return {EdgeKind::Adjacent, EdgeKind::Contains,
            EdgeKind::Influences, EdgeKind::References};
}

constexpr std::string_view as_tla(EdgeKind k) noexcept {
    switch (k) {
        case EdgeKind::Adjacent:   return "adjacent";
        case EdgeKind::Contains:   return "contains";
        case EdgeKind::Influences: return "influences";
        case EdgeKind::References: return "references";
    }
    return "";
}

constexpr bool allows(EdgeKind kind, NodeKind src, NodeKind dst) noexcept {
    switch (kind) {
        case EdgeKind::Adjacent:
            return src == NodeKind::Mesh && dst == NodeKind::Mesh;
        case EdgeKind::Contains:
            if (src != NodeKind::Transform) return false;
            return dst == NodeKind::Transform
                || dst == NodeKind::Mesh
                || dst == NodeKind::Light
                || dst == NodeKind::Camera
                || dst == NodeKind::Volume;
        case EdgeKind::Influences:
            if (dst != NodeKind::Mesh) return false;
            return src == NodeKind::Light
                || src == NodeKind::Volume
                || src == NodeKind::Material;
        case EdgeKind::References:
            return (src == NodeKind::Mesh     && dst == NodeKind::Material)
                || (src == NodeKind::Material && dst == NodeKind::Texture);
    }
    return false;
}

struct Edge {
    EdgeId   id{};
    EdgeKind kind{EdgeKind::Adjacent};
    NodeId   src{};
    NodeId   dst{};
};

}  // namespace aleph::types

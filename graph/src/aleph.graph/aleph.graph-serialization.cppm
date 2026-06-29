module;
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module aleph.graph:serialization;

import :graph;
import :invariants;
import aleph.types;
import aleph.math;

export namespace aleph::graph {

enum class SerializationError {
    InvalidHeader,
    ParseError,
    InvalidNode,
    InvalidEdge,
    IoError,
    InvariantViolation,
};

struct LoadedGraph {
    Graph            graph;
    aleph::types::NodeId root{};
};

namespace detail {

[[nodiscard]] inline bool rest_is_blank(std::string_view line) noexcept {
    for (char c : line) {
        if (c != ' ' && c != '\t') return false;
    }
    return true;
}

inline void append_f32(std::string& out, aleph::math::f32 v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
    out += buf;
}

inline void append_vec3(std::string& out, aleph::math::Vec3 v) {
    append_f32(out, v.x); out += ' ';
    append_f32(out, v.y); out += ' ';
    append_f32(out, v.z);
}

inline void append_mat4(std::string& out, const aleph::math::Mat4& m) {
    for (int i = 0; i < 16; ++i) {
        if (i) out += ' ';
        append_f32(out, m.m[static_cast<std::size_t>(i)]);
    }
}

inline void append_string(std::string& out, std::string_view s) {
    if (!s.empty()
        && s.find(' ') == std::string_view::npos
        && s.find('\t') == std::string_view::npos
        && s.find('"') == std::string_view::npos
        && s.find('\\') == std::string_view::npos) {
        out += s;
        return;
    }
    out += '"';
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    out += '"';
}

[[nodiscard]] inline bool parse_f32(std::string_view tok, aleph::math::f32& out) {
    if (tok.empty()) return false;
    char buf[64];
    if (tok.size() >= sizeof(buf)) return false;
    std::memcpy(buf, tok.data(), tok.size());
    buf[tok.size()] = '\0';
    errno = 0;
    char* end = nullptr;
    const double v = std::strtod(buf, &end);
    if (end != buf + tok.size()) return false;
    if (errno == ERANGE || !std::isfinite(v)) return false;
    out = static_cast<aleph::math::f32>(v);
    return std::isfinite(out);
}

[[nodiscard]] inline bool parse_u32(std::string_view tok, std::uint32_t& out) {
    if (tok.empty()) return false;
    if (tok.front() == '-') return false;
    char buf[32];
    if (tok.size() >= sizeof(buf)) return false;
    std::memcpy(buf, tok.data(), tok.size());
    buf[tok.size()] = '\0';
    errno = 0;
    char* end = nullptr;
    const unsigned long v = std::strtoul(buf, &end, 10);
    if (end != buf + tok.size()) return false;
    if (errno == ERANGE || v > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    out = static_cast<std::uint32_t>(v);
    return true;
}

[[nodiscard]] inline bool parse_node_id(std::string_view tok, aleph::types::NodeId& out) {
    std::uint32_t v = 0;
    if (!parse_u32(tok, v)) return false;
    out = aleph::types::NodeId{v};
    return true;
}

[[nodiscard]] inline std::string_view next_token(std::string_view& line) {
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
        line.remove_prefix(1);
    }
    if (line.empty()) return {};
    if (line.front() == '"') {
        line.remove_prefix(1);
        std::string_view rest = line;
        std::size_t i = 0;
        while (i < rest.size()) {
            if (rest[i] == '\\' && i + 1 < rest.size()) { i += 2; continue; }
            if (rest[i] == '"') break;
            ++i;
        }
        if (i >= rest.size()) return {};
        const std::string_view tok = rest.substr(0, i);
        line = rest.substr(i + 1);
        return tok;
    }
    std::size_t sp = 0;
    while (sp < line.size() && line[sp] != ' ' && line[sp] != '\t') ++sp;
    const std::string_view tok = line.substr(0, sp);
    line = (sp == line.size()) ? std::string_view{} : line.substr(sp + 1);
    return tok;
}

[[nodiscard]] inline std::string unescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            out += s[i + 1];
            ++i;
        } else {
            out += s[i];
        }
    }
    return out;
}

[[nodiscard]] inline bool parse_vec3(std::string_view& line, aleph::math::Vec3& v) {
    std::string_view tx = next_token(line);
    std::string_view ty = next_token(line);
    std::string_view tz = next_token(line);
    return parse_f32(tx, v.x) && parse_f32(ty, v.y) && parse_f32(tz, v.z);
}

[[nodiscard]] inline bool parse_mat4(std::string_view& line, aleph::math::Mat4& m) {
    for (int i = 0; i < 16; ++i) {
        std::string_view tok = next_token(line);
        if (!parse_f32(tok, m.m[static_cast<std::size_t>(i)])) return false;
    }
    return true;
}

[[nodiscard]] inline std::optional<aleph::types::MaterialKind>
parse_material_kind(std::string_view tok) {
    if (tok == "lambertian")          return aleph::types::MaterialKind::Lambertian;
    if (tok == "metal")               return aleph::types::MaterialKind::Metal;
    if (tok == "dielectric")          return aleph::types::MaterialKind::Dielectric;
    if (tok == "emissive")            return aleph::types::MaterialKind::Emissive;
    if (tok == "textured_lambertian") return aleph::types::MaterialKind::TexturedLambertian;
    return std::nullopt;
}

[[nodiscard]] inline std::string_view material_kind_name(aleph::types::MaterialKind k) {
    switch (k) {
        case aleph::types::MaterialKind::Lambertian:         return "lambertian";
        case aleph::types::MaterialKind::Metal:              return "metal";
        case aleph::types::MaterialKind::Dielectric:         return "dielectric";
        case aleph::types::MaterialKind::Emissive:           return "emissive";
        case aleph::types::MaterialKind::TexturedLambertian: return "textured_lambertian";
    }
    return "lambertian";
}

[[nodiscard]] inline std::optional<aleph::types::LightKind>
parse_light_kind(std::string_view tok) {
    if (tok == "point")       return aleph::types::LightKind::Point;
    if (tok == "area")        return aleph::types::LightKind::Area;
    if (tok == "directional") return aleph::types::LightKind::Directional;
    return std::nullopt;
}

[[nodiscard]] inline std::string_view light_kind_name(aleph::types::LightKind k) {
    switch (k) {
        case aleph::types::LightKind::Point:       return "point";
        case aleph::types::LightKind::Area:        return "area";
        case aleph::types::LightKind::Directional: return "directional";
    }
    return "point";
}

[[nodiscard]] inline std::optional<aleph::types::MediumKind>
parse_medium_kind(std::string_view tok) {
    if (tok == "vacuum")        return aleph::types::MediumKind::Vacuum;
    if (tok == "homogeneous")   return aleph::types::MediumKind::Homogeneous;
    if (tok == "heterogeneous") return aleph::types::MediumKind::Heterogeneous;
    return std::nullopt;
}

[[nodiscard]] inline std::optional<aleph::types::TextureFormat>
parse_texture_format(std::string_view tok) {
    if (tok == "rgba8") return aleph::types::TextureFormat::Rgba8;
    if (tok == "rgb8")  return aleph::types::TextureFormat::Rgb8;
    if (tok == "r32f")  return aleph::types::TextureFormat::R32F;
    return std::nullopt;
}

[[nodiscard]] inline std::optional<aleph::types::EdgeKind>
parse_edge_kind(std::string_view tok) {
    if (tok == "adjacent")   return aleph::types::EdgeKind::Adjacent;
    if (tok == "contains")   return aleph::types::EdgeKind::Contains;
    if (tok == "influences") return aleph::types::EdgeKind::Influences;
    if (tok == "references") return aleph::types::EdgeKind::References;
    return std::nullopt;
}

[[nodiscard]] inline bool parse_geometry(std::string_view& line,
                                         aleph::types::GeometryPayload& geom) {
    const std::string_view tag = next_token(line);
    if (tag == "sphere") {
        aleph::types::SphereLocal s{};
        if (!parse_vec3(line, s.center)) return false;
        std::string_view r = next_token(line);
        return parse_f32(r, s.radius) && (geom = s, true);
    }
    if (tag == "quad") {
        aleph::types::QuadLocal q{};
        if (!parse_vec3(line, q.q)) return false;
        if (!parse_vec3(line, q.u)) return false;
        if (!parse_vec3(line, q.v)) return false;
        geom = q;
        return true;
    }
    if (tag == "tri") {
        aleph::types::TriLocal t{};
        if (!parse_vec3(line, t.a)) return false;
        if (!parse_vec3(line, t.b)) return false;
        if (!parse_vec3(line, t.c)) return false;
        geom = t;
        return true;
    }
    return false;
}

inline void append_geometry(std::string& out, const aleph::types::GeometryPayload& geom) {
    std::visit([&](const auto& g) {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, aleph::types::SphereLocal>) {
            out += " sphere ";
            append_vec3(out, g.center); out += ' ';
            append_f32(out, g.radius);
        } else if constexpr (std::is_same_v<T, aleph::types::QuadLocal>) {
            out += " quad ";
            append_vec3(out, g.q); out += ' ';
            append_vec3(out, g.u); out += ' ';
            append_vec3(out, g.v);
        } else if constexpr (std::is_same_v<T, aleph::types::TriLocal>) {
            out += " tri ";
            append_vec3(out, g.a); out += ' ';
            append_vec3(out, g.b); out += ' ';
            append_vec3(out, g.c);
        }
    }, geom);
}

inline void append_node_line(std::string& out, aleph::types::NodeId id,
                             const aleph::types::Node& node) {
    out += "node ";
    out += std::to_string(id.value);
    out += ' ';
    out += aleph::types::as_tla(aleph::types::kind_of(node));
    std::visit([&](const auto& n) {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, aleph::types::Mesh>) {
            out += ' '; append_string(out, n.geometry_ref);
            out += ' '; out += std::to_string(n.tris_count);
            append_geometry(out, n.geometry);
        } else if constexpr (std::is_same_v<T, aleph::types::Material>) {
            out += ' '; out += material_kind_name(n.kind);
            out += ' '; append_vec3(out, n.albedo);
            out += ' '; append_f32(out, n.fuzz);
            out += ' '; append_f32(out, n.ior);
            out += ' '; append_vec3(out, n.emit);
            out += ' '; append_f32(out, n.uv_scale);
        } else if constexpr (std::is_same_v<T, aleph::types::Light>) {
            out += ' '; out += light_kind_name(n.kind);
            out += ' '; append_string(out, n.emit_ref);
            out += ' '; append_vec3(out, n.emission);
            append_geometry(out, n.geometry);
        } else if constexpr (std::is_same_v<T, aleph::types::Volume>) {
            out += ' ';
            switch (n.medium) {
                case aleph::types::MediumKind::Vacuum:        out += "vacuum"; break;
                case aleph::types::MediumKind::Homogeneous:   out += "homogeneous"; break;
                case aleph::types::MediumKind::Heterogeneous: out += "heterogeneous"; break;
            }
        } else if constexpr (std::is_same_v<T, aleph::types::Camera>) {
            out += ' '; append_string(out, n.sensor_id);
            out += ' '; append_vec3(out, n.look_from);
            out += ' '; append_vec3(out, n.look_at);
            out += ' '; append_vec3(out, n.up);
            out += ' '; append_f32(out, n.vfov_deg);
            out += ' '; append_f32(out, n.aperture);
            out += ' '; append_f32(out, n.focus_dist);
        } else if constexpr (std::is_same_v<T, aleph::types::Texture>) {
            out += ' '; out += std::to_string(n.width);
            out += ' '; out += std::to_string(n.height);
            out += ' ';
            switch (n.format) {
                case aleph::types::TextureFormat::Rgba8: out += "rgba8"; break;
                case aleph::types::TextureFormat::Rgb8:  out += "rgb8"; break;
                case aleph::types::TextureFormat::R32F:  out += "r32f"; break;
            }
        } else if constexpr (std::is_same_v<T, aleph::types::Transform>) {
            out += ' '; out += std::to_string(n.pose_slot);
            out += ' '; append_mat4(out, n.local.m);
        }
    }, node);
    out += '\n';
}

struct PendingEdge {
    aleph::types::EdgeKind kind{};
    aleph::types::NodeId   src{};
    aleph::types::NodeId   dst{};
};

[[nodiscard]] inline std::expected<aleph::types::Node, SerializationError>
parse_node_line(std::string_view line) {
    std::string_view rest = line;
    if (next_token(rest) != "node") return std::unexpected(SerializationError::ParseError);
    aleph::types::NodeId id{};
    if (!parse_node_id(next_token(rest), id)) {
        return std::unexpected(SerializationError::ParseError);
    }
    const std::string_view kind_tok = next_token(rest);
    if (kind_tok == "mesh") {
        aleph::types::Mesh m{};
        m.id = id;
        m.geometry_ref = unescape(next_token(rest));
        if (!parse_u32(next_token(rest), m.tris_count)) {
            return std::unexpected(SerializationError::ParseError);
        }
        if (!parse_geometry(rest, m.geometry)) {
            return std::unexpected(SerializationError::ParseError);
        }
        if (!rest_is_blank(rest)) return std::unexpected(SerializationError::ParseError);
        return aleph::types::Node{std::move(m)};
    }
    if (kind_tok == "material") {
        aleph::types::Material mat{};
        mat.id = id;
        const auto kind = parse_material_kind(next_token(rest));
        if (!kind.has_value()) return std::unexpected(SerializationError::ParseError);
        mat.kind = *kind;
        if (!parse_vec3(rest, mat.albedo)) return std::unexpected(SerializationError::ParseError);
        if (!parse_f32(next_token(rest), mat.fuzz)) return std::unexpected(SerializationError::ParseError);
        if (!parse_f32(next_token(rest), mat.ior)) return std::unexpected(SerializationError::ParseError);
        if (!parse_vec3(rest, mat.emit)) return std::unexpected(SerializationError::ParseError);
        if (!parse_f32(next_token(rest), mat.uv_scale)) return std::unexpected(SerializationError::ParseError);
        if (!rest_is_blank(rest)) return std::unexpected(SerializationError::ParseError);
        return aleph::types::Node{std::move(mat)};
    }
    if (kind_tok == "light") {
        aleph::types::Light l{};
        l.id = id;
        const auto kind = parse_light_kind(next_token(rest));
        if (!kind.has_value()) return std::unexpected(SerializationError::ParseError);
        l.kind = *kind;
        l.emit_ref = unescape(next_token(rest));
        if (!parse_vec3(rest, l.emission)) return std::unexpected(SerializationError::ParseError);
        if (!parse_geometry(rest, l.geometry)) return std::unexpected(SerializationError::ParseError);
        if (!rest_is_blank(rest)) return std::unexpected(SerializationError::ParseError);
        return aleph::types::Node{std::move(l)};
    }
    if (kind_tok == "volume") {
        aleph::types::Volume v{};
        v.id = id;
        const auto kind = parse_medium_kind(next_token(rest));
        if (!kind.has_value()) return std::unexpected(SerializationError::ParseError);
        v.medium = *kind;
        if (!rest_is_blank(rest)) return std::unexpected(SerializationError::ParseError);
        return aleph::types::Node{std::move(v)};
    }
    if (kind_tok == "camera") {
        aleph::types::Camera c{};
        c.id = id;
        c.sensor_id = unescape(next_token(rest));
        if (!parse_vec3(rest, c.look_from)) return std::unexpected(SerializationError::ParseError);
        if (!parse_vec3(rest, c.look_at)) return std::unexpected(SerializationError::ParseError);
        if (!parse_vec3(rest, c.up)) return std::unexpected(SerializationError::ParseError);
        if (!parse_f32(next_token(rest), c.vfov_deg)) return std::unexpected(SerializationError::ParseError);
        if (!parse_f32(next_token(rest), c.aperture)) return std::unexpected(SerializationError::ParseError);
        if (!parse_f32(next_token(rest), c.focus_dist)) return std::unexpected(SerializationError::ParseError);
        if (!rest_is_blank(rest)) return std::unexpected(SerializationError::ParseError);
        return aleph::types::Node{std::move(c)};
    }
    if (kind_tok == "texture") {
        aleph::types::Texture t{};
        t.id = id;
        if (!parse_u32(next_token(rest), t.width)) return std::unexpected(SerializationError::ParseError);
        if (!parse_u32(next_token(rest), t.height)) return std::unexpected(SerializationError::ParseError);
        const auto fmt = parse_texture_format(next_token(rest));
        if (!fmt.has_value()) return std::unexpected(SerializationError::ParseError);
        t.format = *fmt;
        if (!rest_is_blank(rest)) return std::unexpected(SerializationError::ParseError);
        return aleph::types::Node{std::move(t)};
    }
    if (kind_tok == "transform") {
        aleph::types::Transform xf{};
        xf.id = id;
        if (!parse_u32(next_token(rest), xf.pose_slot)) {
            return std::unexpected(SerializationError::ParseError);
        }
        if (!parse_mat4(rest, xf.local.m)) return std::unexpected(SerializationError::ParseError);
        if (!rest_is_blank(rest)) return std::unexpected(SerializationError::ParseError);
        return aleph::types::Node{std::move(xf)};
    }
    return std::unexpected(SerializationError::InvalidNode);
}

}  // namespace detail

[[nodiscard]] inline std::string
save_graph_string(const Graph& g, aleph::types::NodeId root) {
    std::string out = "aleph-graph/1\n";
    out += "root ";
    out += std::to_string(root.value);
    out += '\n';
    for (auto [id, node] : g.nodes()) {
        detail::append_node_line(out, id, node);
    }
    for (auto [eid, e] : g.edges()) {
        (void)eid;
        out += "edge ";
        out += aleph::types::as_tla(e.kind);
        out += ' ';
        out += std::to_string(e.src.value);
        out += ' ';
        out += std::to_string(e.dst.value);
        out += '\n';
    }
    return out;
}

[[nodiscard]] inline std::expected<LoadedGraph, SerializationError>
load_graph_string(std::string_view text) {
    LoadedGraph loaded{};
    aleph::types::NodeId root{};
    bool have_header = false;
    bool have_root = false;
    bool have_nodes = false;
    std::uint32_t max_node_id = 0;
    std::vector<detail::PendingEdge> pending_edges;

    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = (end < text.size()) ? end + 1 : end;

        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            line.remove_prefix(1);
        }
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') continue;

        if (!have_header) {
            if (line != "aleph-graph/1") {
                return std::unexpected(SerializationError::InvalidHeader);
            }
            have_header = true;
            continue;
        }

        if (line == "aleph-graph/1") {
            return std::unexpected(SerializationError::InvalidHeader);
        }

        std::string_view rest = line;
        const std::string_view tag = detail::next_token(rest);
        if (tag == "root") {
            if (!detail::parse_node_id(detail::next_token(rest), root)) {
                return std::unexpected(SerializationError::ParseError);
            }
            if (!detail::rest_is_blank(rest)) return std::unexpected(SerializationError::ParseError);
            have_root = true;
            continue;
        }
        if (tag == "node") {
            auto node = detail::parse_node_line(line);
            if (!node.has_value()) return std::unexpected(node.error());
            const aleph::types::NodeId id = aleph::types::id_of(*node);
            auto inserted = loaded.graph.try_insert_node(std::move(*node));
            if (!inserted.has_value()) {
                return std::unexpected(SerializationError::InvalidNode);
            }
            if (!have_nodes || id.value > max_node_id) {
                max_node_id = id.value;
                have_nodes = true;
            }
            continue;
        }
        if (tag == "edge") {
            detail::PendingEdge pe{};
            const auto kind = detail::parse_edge_kind(detail::next_token(rest));
            if (!kind.has_value()) return std::unexpected(SerializationError::InvalidEdge);
            pe.kind = *kind;
            if (!detail::parse_node_id(detail::next_token(rest), pe.src)) {
                return std::unexpected(SerializationError::InvalidEdge);
            }
            if (!detail::parse_node_id(detail::next_token(rest), pe.dst)) {
                return std::unexpected(SerializationError::InvalidEdge);
            }
            if (!detail::rest_is_blank(rest)) return std::unexpected(SerializationError::ParseError);
            pending_edges.push_back(pe);
            continue;
        }
        return std::unexpected(SerializationError::ParseError);
    }

    if (!have_header) return std::unexpected(SerializationError::InvalidHeader);
    if (!have_root) return std::unexpected(SerializationError::InvalidHeader);
    if (loaded.graph.node(root) == nullptr) {
        return std::unexpected(SerializationError::ParseError);
    }

    if (have_nodes && max_node_id != std::numeric_limits<std::uint32_t>::max()) {
        loaded.graph.sync_node_allocator_to_at_least(max_node_id + 1);
    }

    for (const detail::PendingEdge& pe : pending_edges) {
        auto eid = loaded.graph.add_edge(pe.kind, pe.src, pe.dst);
        if (!eid.has_value()) return std::unexpected(SerializationError::InvalidEdge);
    }

    if (!validate_all(loaded.graph, static_cast<std::size_t>(-1)).has_value()) {
        return std::unexpected(SerializationError::InvariantViolation);
    }

    loaded.root = root;
    return loaded;
}

[[nodiscard]] inline std::expected<void, SerializationError>
save_graph_file(const Graph& g, aleph::types::NodeId root, std::string_view path) {
    std::ofstream f{std::string{path}, std::ios::binary | std::ios::trunc};
    if (!f) return std::unexpected(SerializationError::IoError);
    const std::string data = save_graph_string(g, root);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!f) return std::unexpected(SerializationError::IoError);
    return {};
}

[[nodiscard]] inline std::expected<LoadedGraph, SerializationError>
load_graph_file(std::string_view path) {
    std::ifstream f{std::string{path}, std::ios::binary};
    if (!f) return std::unexpected(SerializationError::IoError);
    std::ostringstream ss;
    ss << f.rdbuf();
    if (!f && !f.eof()) return std::unexpected(SerializationError::IoError);
    return load_graph_string(ss.view());
}

}  // namespace aleph::graph

module;
#include <cstddef>
#include <cstdint>
#include <expected>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module aleph.lowering:lower;

import aleph.containers;   // OrderedMap (move-only, insertion-ordered)
import aleph.graph;       // Graph: nodes()/edges()/node()/edge()
import aleph.types;       // enriched Node/Edge payloads (§3)
import aleph.math;        // Vec3/Vec4/Mat4 + operator*

// ---------------------------------------------------------------------------
// aleph.lowering:lower — the lowering functor (SPEC §4.2).
//
//   GraphScene ──Lower──▶ LoweredScene   (frozen SEMANTIC IR, no renderer guts)
//
// The typed scene graph is the single source of truth; `lower()` is its image
// under a deterministic functor. This is a NEW design (rev 2), not a port:
//
//   1. Roots = `Transform` nodes with NO incoming `Contains`.
//   2. DFS over `Contains` (insertion order) composing the world transform as
//      `world = parent.world * node.local.m`.
//   3. Each `Mesh`: transform its `GeometryPayload` to world space and resolve
//      its `Material` via the `Mesh —References→ Material` edge. A missing or
//      dangling reference is a STRUCTURED error (`LowerError::DanglingReference`)
//      — never a silent default. Emit one `LoweredEntity` + a `handle_map` slot.
//   4. Build the light table per the §3 policy: every `Light` node UNION every
//      emissive `Mesh` (its resolved `Material.emit` luminance > 0). NOT
//      "all lights are meshes."
//   5. Extract the unique `Camera` (invariant `CameraExclusive`); none ⇒
//      `LowerError::NoCamera`.
//
// Determinism: traversal follows insertion order (`Graph` stores nodes/edges in
// an insertion-ordered `OrderedMap`), all math is f32, and the `handle_map` is a
// stable `NodeId → entities-index`. Same graph ⇒ byte-identical `LoweredScene`.
//
// No exceptions (aleph_flags_isa): all fallible paths return `std::expected`.
//
// NOTE: SPEC §4 partitions the IR types into `aleph.lowering:lowered`. That
// partition is still an empty W1 stub, so the IR is defined here in `:lower`
// (which the umbrella re-exports) to keep this translation unit self-contained
// and compilable on its own. When `:lowered` is populated these definitions
// move there and `:lower` imports them — the public names are unchanged.
// ---------------------------------------------------------------------------

export namespace aleph::lowering {

// ── Frozen semantic IR (SPEC §4.1) ─────────────────────────────────────────

// Normalized, renderer-agnostic material parameters. Carries exactly what
// `build_render_scene` needs to pick a `scene_add_lambertian/metal/dielectric/
// emissive` and nothing renderer-internal.
struct MaterialParams {
    aleph::types::MaterialKind kind{aleph::types::MaterialKind::Lambertian};
    aleph::math::Vec3          albedo{0.8f, 0.8f, 0.8f};
    aleph::math::f32           fuzz{0};
    aleph::math::f32           ior{1.5f};
    aleph::math::Vec3          emit{0, 0, 0};
};

// A resolved drawable: its source node, world-space geometry, and the material
// resolved through `References` (for lights, the synthesized emissive material).
struct LoweredEntity {
    aleph::types::NodeId          source{};
    aleph::types::GeometryPayload world_geometry{aleph::types::SphereLocal{}};
    MaterialParams                material{};
};

// The unique camera, pose-only (no projection matrix — that is a renderer concern).
struct LoweredCamera {
    aleph::math::Vec3 look_from{0, 0, 0};
    aleph::math::Vec3 look_at{0, 0, -1};
    aleph::math::Vec3 up{0, 1, 0};
    aleph::math::f32  vfov_deg{40};
    aleph::math::f32  aperture{0};
    aleph::math::f32  focus_dist{1};
};

// Frozen, byte-deterministic IR. Contains NO renderer internals (no BVH, no SoA).
struct LoweredScene {
    std::vector<LoweredEntity> entities;   // insertion-order stable
    std::vector<LoweredEntity> lights;     // light table (policy §3)
    LoweredCamera              camera{};
    // NodeId -> index into `entities`. Stable, move-only (OrderedMap).
    aleph::containers::OrderedMap<aleph::types::NodeId, std::uint32_t> handle_map{};

    LoweredScene() = default;
    LoweredScene(LoweredScene&&) = default;
    LoweredScene& operator=(LoweredScene&&) = default;
    LoweredScene(const LoweredScene&) = delete;             // OrderedMap is move-only
    LoweredScene& operator=(const LoweredScene&) = delete;
};

// Structured lowering failures (SPEC §4.2). Broken references fail HERE, loudly.
enum class LowerError {
    NoCamera,            // no Camera node (violates CameraExclusive)
    DanglingReference,   // a Mesh has no / a broken References→Material edge
    InvalidHierarchy,    // the Contains hierarchy is malformed (e.g. a cycle)
};

}  // namespace aleph::lowering

// ── Implementation helpers (non-exported) ──────────────────────────────────
namespace aleph::lowering::detail {

using aleph::math::Mat4;
using aleph::math::Vec3;
using aleph::math::Vec4;

// Transform a position (affine point, w = 1) by a column-major Mat4.
[[nodiscard]] inline Vec3 xform_point(const Mat4& world, Vec3 p) noexcept {
    const Vec4 r = world * Vec4{p.x, p.y, p.z, 1.0f};
    return Vec3{r.x, r.y, r.z};
}

// Transform a direction (w = 0) by a column-major Mat4: ignores translation.
[[nodiscard]] inline Vec3 xform_dir(const Mat4& world, Vec3 d) noexcept {
    const Vec4 r = world * Vec4{d.x, d.y, d.z, 0.0f};
    return Vec3{r.x, r.y, r.z};
}

// Map a LOCAL geometry payload into world space under `world`.
// Sphere center is a point (radius kept; v1 transforms are rigid). Quad anchor
// `q` is a point, spanning vectors `u`/`v` are directions. Tri verts are points.
[[nodiscard]] inline aleph::types::GeometryPayload
to_world(const aleph::types::GeometryPayload& local, const Mat4& world) {
    return std::visit(
        [&](const auto& g) -> aleph::types::GeometryPayload {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, aleph::types::SphereLocal>) {
                return aleph::types::SphereLocal{xform_point(world, g.center), g.radius};
            } else if constexpr (std::is_same_v<T, aleph::types::QuadLocal>) {
                return aleph::types::QuadLocal{
                    xform_point(world, g.q),
                    xform_dir(world, g.u),
                    xform_dir(world, g.v)};
            } else {  // TriLocal
                return aleph::types::TriLocal{
                    xform_point(world, g.a),
                    xform_point(world, g.b),
                    xform_point(world, g.c)};
            }
        },
        local);
}

// Project a graph Material onto the normalized IR params.
[[nodiscard]] inline MaterialParams to_params(const aleph::types::Material& m) noexcept {
    return MaterialParams{m.kind, m.albedo, m.fuzz, m.ior, m.emit};
}

// Photometric luminance (Rec. 709) of an emission color. > 0 ⇒ physically emissive.
[[nodiscard]] inline aleph::math::f32 luminance(Vec3 c) noexcept {
    return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
}

}  // namespace aleph::lowering::detail

export namespace aleph::lowering {

// ── lower(const Graph&) -> expected<LoweredScene, LowerError> (SPEC §4.2) ───
[[nodiscard]] inline std::expected<LoweredScene, LowerError>
lower(const aleph::graph::Graph& g) {
    using aleph::graph::Graph;
    using aleph::math::Mat4;
    using aleph::types::EdgeKind;
    using aleph::types::NodeId;
    using aleph::types::NodeKind;
    namespace t = aleph::types;

    LoweredScene out{};

    // ── Step 5 (eager): extract the unique Camera (CameraExclusive). ────────
    // Done first so a camera-less graph fails fast and deterministically before
    // any traversal work. Insertion order makes "first" deterministic; the graph
    // invariant guarantees at most one, so this is unambiguous.
    const t::Camera* camera = nullptr;
    for (auto [id, node] : g.nodes()) {
        if (t::kind_of(node) == NodeKind::Camera) {
            camera = &std::get<t::Camera>(node);
            break;
        }
    }
    if (camera == nullptr) {
        return std::unexpected(LowerError::NoCamera);
    }
    out.camera = LoweredCamera{
        camera->look_from, camera->look_at, camera->up,
        camera->vfov_deg, camera->aperture, camera->focus_dist};

    // ── Pre-pass: resolve every Mesh's Material via Mesh —References→ Material.
    // A Mesh with no such edge, or whose edge points at a missing/non-Material
    // node, is a DanglingReference — never silently defaulted. Insertion order
    // over edges makes "the" reference deterministic (the graph guarantees one).
    aleph::containers::OrderedMap<NodeId, MaterialParams> mesh_material{};
    {
        // Collect each Mesh's referenced Material id (first References edge, by
        // insertion order) and validate it resolves to a real Material node.
        for (auto [id, node] : g.nodes()) {
            if (t::kind_of(node) != NodeKind::Mesh) continue;
            const NodeId mesh_id = id;

            bool found = false;
            NodeId mat_id{};
            for (auto [eid, e] : g.edges()) {
                if (e.kind == EdgeKind::References && e.src == mesh_id) {
                    mat_id = e.dst;
                    found = true;
                    break;
                }
            }
            if (!found) {
                return std::unexpected(LowerError::DanglingReference);
            }
            const t::Node* mat_node = g.node(mat_id);
            if (mat_node == nullptr
                || t::kind_of(*mat_node) != NodeKind::Material) {
                return std::unexpected(LowerError::DanglingReference);
            }
            (void)mesh_material.insert(
                mesh_id, detail::to_params(std::get<t::Material>(*mat_node)));
        }
    }

    // ── Steps 1+2: roots are Transform nodes with no incoming Contains; DFS
    // Contains (insertion order) composing world = parent.world * node.local.m.
    //
    // The Contains hierarchy is over Transform→{Transform,Mesh,Light,Camera,...}.
    // We DFS from each root Transform, carrying the accumulated world matrix, and
    // emit entities/lights for the Mesh/Light nodes we reach. A cycle (a node
    // re-entered while still on the active stack) is InvalidHierarchy.

    // incoming-Contains predicate over nodes.
    auto has_incoming_contains = [&](NodeId id) noexcept -> bool {
        for (auto [eid, e] : g.edges()) {
            if (e.kind == EdgeKind::Contains && e.dst == id) return true;
        }
        return false;
    };

    // visited (closed) set + active-path (open) stack for deterministic cycle
    // detection. `closed` prevents re-processing a node reached via two paths;
    // `path` is the set of ancestors currently on the DFS spine — a node seen
    // while still on `path` is a genuine Contains cycle (InvalidHierarchy).
    aleph::containers::FlatSet<NodeId> closed{};
    std::vector<NodeId>                path{};
    auto path_contains = [&](NodeId id) noexcept -> bool {
        for (NodeId p : path) if (p == id) return true;
        return false;
    };
    auto path_pop = [&](NodeId id) noexcept {
        for (std::size_t i = path.size(); i-- > 0;) {
            if (path[i] == id) { path.erase(path.begin() + static_cast<std::ptrdiff_t>(i)); return; }
        }
    };

    // Explicit work stack of (node, world). We expand a node's Contains children
    // in insertion order; to honor DFS *insertion order* we push children in
    // reverse so the first child is processed first.
    struct Frame { NodeId id; Mat4 world; bool entering; };
    std::vector<Frame> stack{};

    // Visit one node: if Mesh -> emit entity (+ handle_map, + light if emissive);
    // if Light -> emit light. Returns false on InvalidHierarchy.
    auto emit_for_node = [&](NodeId id, const Mat4& world) -> bool {
        const t::Node* np = g.node(id);
        if (np == nullptr) return true;  // dangling Contains child: skip, not fatal
        switch (t::kind_of(*np)) {
            case NodeKind::Mesh: {
                const auto& mesh = std::get<t::Mesh>(*np);
                const MaterialParams* mp = mesh_material.get(id);
                if (mp == nullptr) {
                    // Should be unreachable: every Mesh was resolved in the
                    // pre-pass. Treat a miss as a dangling reference, defensively.
                    return false;
                }
                LoweredEntity ent{};
                ent.source         = id;
                ent.world_geometry = detail::to_world(mesh.geometry, world);
                ent.material       = *mp;
                const auto index   = static_cast<std::uint32_t>(out.entities.size());
                out.entities.push_back(ent);
                (void)out.handle_map.insert(id, index);
                // Light-table policy (§3): an emissive Mesh joins the light table.
                if (detail::luminance(mp->emit) > 0.0f) {
                    out.lights.push_back(ent);
                }
                break;
            }
            case NodeKind::Light: {
                const auto& light = std::get<t::Light>(*np);
                LoweredEntity lent{};
                lent.source         = id;
                lent.world_geometry = detail::to_world(light.geometry, world);
                // A Light node carries its emission directly; surface it as an
                // emissive material so the light table is uniform LoweredEntities.
                lent.material = MaterialParams{
                    aleph::types::MaterialKind::Emissive,
                    light.emission,  // albedo unused for emissive; keep emission
                    0.0f, 1.5f,
                    light.emission};
                out.lights.push_back(lent);
                break;
            }
            default:
                break;  // Camera/Volume/etc. carry no drawable payload here.
        }
        return true;
    };

    // Iterate roots in node insertion order.
    for (auto [rid, rnode] : g.nodes()) {
        if (t::kind_of(rnode) != NodeKind::Transform) continue;
        if (has_incoming_contains(rid)) continue;  // not a root

        stack.clear();
        const auto& root_xf = std::get<t::Transform>(rnode);
        stack.push_back(Frame{rid, root_xf.local.m, true});

        while (!stack.empty()) {
            Frame fr = stack.back();
            stack.pop_back();

            if (!fr.entering) {
                // Leaving frame: pop from the active path.
                path_pop(fr.id);
                continue;
            }

            if (path_contains(fr.id)) {
                return std::unexpected(LowerError::InvalidHierarchy);  // cycle
            }
            if (closed.contains(fr.id)) {
                continue;  // already fully processed via another path: skip
            }
            path.push_back(fr.id);
            closed.insert(fr.id);

            // Emit a drawable for this node (Mesh/Light) under the world matrix.
            if (!emit_for_node(fr.id, fr.world)) {
                return std::unexpected(LowerError::DanglingReference);
            }

            // Schedule the "leave" marker, then push Contains children so the
            // first (insertion-order) child is processed first.
            stack.push_back(Frame{fr.id, fr.world, false});

            // Gather this node's Contains children in insertion order.
            aleph::containers::SmallVector<NodeId, 16> children{};
            for (auto [eid, e] : g.edges()) {
                if (e.kind == EdgeKind::Contains && e.src == fr.id) {
                    children.push_back(e.dst);
                }
            }
            // Push reversed so processing order == insertion order.
            for (std::size_t i = children.size(); i-- > 0;) {
                const NodeId cid = children[i];
                const t::Node* cn = g.node(cid);
                Mat4 child_world = fr.world;
                if (cn != nullptr
                    && t::kind_of(*cn) == NodeKind::Transform) {
                    // world = parent.world * node.local.m
                    child_world = fr.world * std::get<t::Transform>(*cn).local.m;
                }
                stack.push_back(Frame{cid, child_world, true});
            }
        }
    }

    return out;
}

}  // namespace aleph::lowering

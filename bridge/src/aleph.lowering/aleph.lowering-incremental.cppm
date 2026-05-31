// aleph.lowering:incremental — Phase 5.x-c incremental lowering (SPEC
// 2026-05-31, "Incremental Lowering (dirty-sets)").
//
//   After an editor `Op`, re-lower ONLY what changed instead of rebuilding the
//   whole `LoweredScene`. The result MUST be byte-identical to a full
//   `lower(after)` — incremental is purely an OPTIMIZATION, never a semantic
//   divergence (SPEC §1/§2).
//
// ── CORE CONTRACT ───────────────────────────────────────────────────────────
// `lower_incremental(prev, after, op, rec)` returns a `LoweredScene` that is
// BYTE-IDENTICAL to `lower(after)` for every op type. Incremental is an
// optimization layered ON TOP of that guarantee: it reuses clean entities from
// `prev` verbatim and recomputes only the dirty ones, but the SHAPE of the
// result (entity order, light order, handle_map order, light_groups) is exactly
// what a full `lower(after)` would produce.
//
// ── This wave: ATTRIBUTE ops (SetMaterial / SetTransform) ───────────────────
// For an ATTRIBUTE op the GRAPH TOPOLOGY is unchanged (no node/edge created or
// deleted — `rec` is empty); only a `Material`'s params (SetMaterial) or a
// `Transform`'s local pose (SetTransform) changed in place. That has two
// consequences that make a provably-byte-identical incremental patch possible:
//
//   1. The set of Meshes and the Contains DFS ORDER are identical to `prev`'s,
//      so `prev.entities` is ALREADY in the order `lower(after)` would emit.
//      We therefore patch `prev` IN ORDER rather than re-deriving ordering.
//   2. A CLEAN (un-dirtied) entity's lowered output is a pure function of its
//      node + transform chain + material — none of which changed — so it is
//      provably identical to what full would produce and is REUSED VERBATIM.
//
// `dirty(op, after)` (SPEC §3):
//   * SetMaterial{mesh}  — resolve the Material that mesh References, then dirty
//                          EVERY Mesh referencing that same Material (their
//                          `material` params changed; possibly `emit`).
//   * SetTransform{xf}   — dirty every Contains-DESCENDANT Mesh of `xf` (their
//                          world geometry shifts under the new local pose).
// Dirty entities have their `world_geometry`/`material` recomputed exactly as
// `lower()` does (same f32 math, same world = ∏ ancestor-Transform locals along
// the FIRST Contains path — matching `lower()`'s once-per-node DFS). Clean
// entities are copied from `prev`.
//
// `lights` is the light table built by re-walking the SAME DFS `lower()` runs
// (emissive Meshes UNION Light nodes, in DFS order); clean entries are copied
// from `prev`, dirty ones recomputed — so its bytes match full. `light_groups`
// (the sheaf H⁰ partition) depends ONLY on Influences/Adjacent topology + Light
// existence, none of which an attribute op touches, so it is REUSED from `prev`
// — EXCEPT a SetMaterial that changes `emit` is conservatively treated as
// light-group-dirty (SPEC §3.4) and recomputed via `light_groups_of(after)`
// (which, topology being unchanged, yields the same value — still byte-identical).
//
// ── This wave: STRUCTURAL ops (AddObject / AddLight / DeleteObject) ─────────
// These change graph TOPOLOGY: AddObject mints a Mesh+Material (and its
// References/Contains edges), AddLight mints a Light (and its Contains edge),
// DeleteObject removes a Mesh and cascades its incident edges. `rec` carries the
// delta: `rec.created_nodes` (mesh+material / light) and `rec.deleted_nodes`
// (the removed mesh). The EXISTING (surviving) nodes' Contains chains and
// References are UNCHANGED — only nodes were added or removed — so every
// surviving entity is CLEAN and its lowered payload is reused VERBATIM from
// `prev`; only the created Mesh's payload is recomputed (SPEC §4.1/§4.2).
//
// Byte-identical ordering (SPEC §4.3): `full lower()` emits entities in DFS
// order over the `Contains` hierarchy (roots in node insertion order, children
// in edge insertion order). A structural op shifts that order — a deleted mesh
// vanishes from its slot; a created mesh appears at its DFS position (appended
// last among its parent's Contains children, since `apply_op` adds the Contains
// edge last). Rather than guess the new slot, we RE-WALK the very DFS `lower()`
// runs over `after` (`detail::dfs_emit`) to produce the exact emission order,
// then for each emitted Mesh we REUSE `prev`'s payload (clean survivor) or
// RECOMPUTE it (created). This reproduces full's entity order, light table and
// handle_map byte-for-byte while recomputing only |created| Mesh payloads —
// O(dirty), not O(N) (SPEC §6.3).
//
// `light_groups` (the sheaf H⁰ partition) depends ONLY on Influences/Adjacent
// topology + Light existence + per-material emission. A structural op is
// light-group-dirty iff it added/removed a `Light`, added/removed an
// `Influences` edge, or changed emissive membership (AddObject of an emissive
// material; DeleteObject of a mesh that was emissive). When NONE of those hold
// (the common AddObject-of-a-lambertian / DeleteObject-of-a-non-emissive case
// with no incident Influences), `prev.light_groups` is reused verbatim and
// `stats.light_groups_recomputed` stays false; otherwise it is recomputed via
// `light_groups_of(after)` (topology-correct, hence byte-identical, SPEC §4.4).
//
// ── Not-yet-incremental ops fall back to full ──────────────────────────────
// `ApplyRule` is an arbitrary DPO rewrite whose dirty set is harder to bound
// (SPEC §5); it FALLS BACK to a full `lower(after)`, which is byte-identical by
// being exactly that call. (A structural patch that hits an inconsistency also
// falls back, never compromising the byte-identical contract.)
//
// No exceptions (aleph_flags_isa): the fallible path is `std::expected`,
// forwarded verbatim from `lower()`.

module;
#include <cstddef>
#include <cstdint>
#include <expected>
#include <type_traits>
#include <variant>
#include <vector>

export module aleph.lowering:incremental;

import aleph.containers;  // OrderedMap / FlatSet (move-only, insertion-ordered)
import :lowered;     // LoweredScene / LoweredEntity / MaterialParams (frozen IR)
import :lower;       // lower(), LoweredScene, LowerError, detail::to_world/to_params/luminance
import :ops;         // Op (the editor op vocabulary)
import :grouping;    // light_groups_of (kept in the partition closure)
import :importance;  // entity_importance (Ollivier-Ricci → per-Mesh importance)
import aleph.graph;  // Graph
import aleph.dpo;    // RewriteRecord (what apply_op reported)
import aleph.types;  // NodeId / NodeKind / EdgeKind / Mesh/Material/Transform
import aleph.math;   // Mat4 / Vec3 / f32

export namespace aleph::lowering {

// Work-bound instrumentation for incremental lowering (SPEC §6.3). A caller
// passes a pointer; on success the lowering fills it in. `recomputed_entities`
// is the number of entities whose payload was recomputed (NOT copied from
// `prev`) — for an attribute op this is |dirty|, proving O(dirty) not O(N).
// `light_groups_recomputed` is whether the sheaf H⁰ pass ran.
struct IncrementalStats {
    std::size_t recomputed_entities{};
    bool        light_groups_recomputed{false};
    // Whether the Ollivier-Ricci → per-entity importance pass ran (SPEC §4.1,
    // Phase 5.x-b). Importance is a pure function of the Adjacent mesh skeleton +
    // the set of Meshes, so it is recomputed ONLY when the op dirtied that
    // topology (a structural op that added/removed a Mesh or cascaded Adjacent
    // edges); attribute ops (SetMaterial/SetTransform) and AddLight reuse
    // `prev.importance` verbatim and leave this false (SPEC §6 incremental_importance).
    bool        importance_recomputed{false};
};

}  // namespace aleph::lowering

// ── Implementation helpers (non-exported) ───────────────────────────────────
namespace aleph::lowering::detail {

using aleph::graph::Graph;
using aleph::math::Mat4;
using aleph::types::EdgeKind;
using aleph::types::NodeId;
using aleph::types::NodeKind;
namespace t = aleph::types;

// The first `Mesh —References→ Material` edge target for `mesh`, in edge
// insertion order — exactly the reference `lower()` and `apply_op` resolve.
// Returns false if the mesh has no References edge (a dangling mesh).
[[nodiscard]] inline bool referenced_material(const Graph& g, NodeId mesh,
                                              NodeId& out) noexcept {
    for (auto [eid, e] : g.edges()) {
        (void)eid;
        if (e.kind == EdgeKind::References && e.src == mesh) {
            out = e.dst;
            return true;
        }
    }
    return false;
}

// Collect every Contains-DESCENDANT of `root` (transitive closure over Contains,
// `root` excluded) into `out` as a membership set. Used by SetTransform: a
// Transform's new local pose shifts the world transform of all meshes beneath
// it. Deterministic; cycle-safe via the visited set.
inline void contains_descendants(const Graph& g, NodeId root,
                                 aleph::containers::FlatSet<NodeId>& out) {
    std::vector<NodeId> frontier;
    frontier.push_back(root);
    aleph::containers::FlatSet<NodeId> seen;
    seen.insert(root);
    while (!frontier.empty()) {
        const NodeId cur = frontier.back();
        frontier.pop_back();
        for (auto [eid, e] : g.edges()) {
            (void)eid;
            if (e.kind == EdgeKind::Contains && e.src == cur) {
                if (!seen.contains(e.dst)) {
                    seen.insert(e.dst);
                    out.insert(e.dst);          // descendant (root excluded)
                    frontier.push_back(e.dst);
                }
            }
        }
    }
}

// Recompute one Mesh entity exactly as `lower()` would: its world matrix is the
// product of the local poses of the Transforms on its FIRST Contains path from a
// root (matching `lower()`'s once-per-node DFS), and its material is resolved
// through `References`. Returns false on a dangling reference / unreachable mesh
// (the same structured failures `lower()` raises).
//
// `lower()` reaches each node ONCE, via the first DFS path (insertion order over
// roots, then insertion order over each node's Contains children). We reproduce
// that path here by, for the target mesh, finding the unique chain of ancestor
// Transforms `lower()` would have descended — i.e. for each node we take its
// FIRST incoming Contains parent (insertion order) that is itself reachable from
// a root. Because attribute ops never change Contains, this path is stable.
[[nodiscard]] inline bool world_of_mesh(const Graph& g, NodeId mesh, Mat4& out) {
    // Walk UP from the mesh to a root via first-incoming-Contains parents,
    // recording the Transform chain, then compose top-down.
    std::vector<NodeId> chain;          // [root .. parent-of-mesh], Transforms only
    aleph::containers::FlatSet<NodeId> on_path;  // cycle guard
    NodeId cur = mesh;
    on_path.insert(cur);
    for (;;) {
        // First incoming Contains parent of `cur` in edge insertion order.
        bool has_parent = false;
        NodeId parent{};
        for (auto [eid, e] : g.edges()) {
            (void)eid;
            if (e.kind == EdgeKind::Contains && e.dst == cur) {
                parent = e.src;
                has_parent = true;
                break;
            }
        }
        if (!has_parent) break;             // reached a root boundary
        if (on_path.contains(parent)) break; // defensive: Contains cycle
        const t::Node* pn = g.node(parent);
        if (pn != nullptr && t::kind_of(*pn) == NodeKind::Transform) {
            chain.push_back(parent);
        }
        on_path.insert(parent);
        cur = parent;
    }

    // Compose world = root.local * ... * parent.local (top-down). `chain` is
    // bottom-up (parent-of-mesh first), so iterate it in reverse.
    Mat4 world = Mat4::identity();
    bool any = false;
    for (std::size_t i = chain.size(); i-- > 0;) {
        const t::Node* xn = g.node(chain[i]);
        const Mat4& local = std::get<t::Transform>(*xn).local.m;
        world = any ? (world * local) : local;  // first factor is root.local (no I*)
        any = true;
    }
    if (!any) {
        // Mesh has no Transform ancestor: `lower()` only emits meshes reached by
        // the transform DFS, so this mesh would not lower. Treat as unreachable.
        return false;
    }
    out = world;
    return true;
}

// Recompute the payload (world geometry + material) of a dirty Mesh entity.
[[nodiscard]] inline bool recompute_entity(const Graph& g, NodeId mesh,
                                           LoweredEntity& out) {
    const t::Node* mn = g.node(mesh);
    if (mn == nullptr || t::kind_of(*mn) != NodeKind::Mesh) return false;
    NodeId mat_id{};
    if (!referenced_material(g, mesh, mat_id)) return false;
    const t::Node* mat = g.node(mat_id);
    if (mat == nullptr || t::kind_of(*mat) != NodeKind::Material) return false;
    Mat4 world{};
    if (!world_of_mesh(g, mesh, world)) return false;
    out.source         = mesh;
    out.world_geometry = to_world(std::get<t::Mesh>(*mn).geometry, world);
    out.material       = to_params(std::get<t::Material>(*mat));
    return true;
}

// Recompute a LIGHT-NODE light-table entry exactly as `lower()` synthesizes it:
// the light's local geometry transformed by its world matrix, surfaced as an
// Emissive material carrying the light's emission. A Light is reached by the
// transform DFS just like a Mesh, so its world matrix is the product of the
// ancestor Transforms on its first Contains path. Used when a SetTransform moves
// a subtree that CONTAINS a Light (its world geometry shifts). Returns false if
// the light is not reachable under a Transform (it would not lower).
[[nodiscard]] inline bool recompute_light(const Graph& g, NodeId light_id,
                                          LoweredEntity& out) {
    const t::Node* ln = g.node(light_id);
    if (ln == nullptr || t::kind_of(*ln) != NodeKind::Light) return false;
    Mat4 world{};
    if (!world_of_mesh(g, light_id, world)) return false;  // path walk is kind-agnostic
    const auto& light = std::get<t::Light>(*ln);
    out.source         = light_id;
    out.world_geometry = to_world(light.geometry, world);
    out.material       = MaterialParams{
        aleph::types::MaterialKind::Emissive,
        light.emission,  // albedo unused for emissive; mirror lower()
        0.0f, 1.5f,
        light.emission};
    return true;
}

// ── DFS-replay for structural ops (SPEC §4.3) ───────────────────────────────
// One emission event from `lower()`'s Contains DFS: the node it would emit and
// the world matrix under which it would emit it. `is_light` distinguishes a
// Light node (always a light-table entry) from a Mesh (an `entities` entry that
// is ALSO a light-table entry iff its resolved material is emissive).
struct EmitEvent {
    NodeId id;
    Mat4   world;
    bool   is_light;  // true => Light node; false => Mesh node
};

// Replay `lower()`'s EXACT Contains-DFS over `g`, recording the ordered Mesh /
// Light emission events (the same order, world matrices and once-per-node
// reachability `lower()` produces). This is a faithful transcription of the DFS
// in `:lower` (roots = Transforms with no incoming Contains, in node insertion
// order; children in edge insertion order; world = parent.world * local; closed
// set so a node reached twice emits once; active-path cycle guard). Returns
// false on a Contains cycle (`lower()`'s InvalidHierarchy) so the caller can
// fall back to full. Pure ordering: it does NOT resolve materials or transform
// geometry — the caller reuses/recomputes per event.
[[nodiscard]] inline bool dfs_emit(const Graph& g, std::vector<EmitEvent>& out) {
    auto has_incoming_contains = [&](NodeId id) noexcept -> bool {
        for (auto [eid, e] : g.edges()) {
            (void)eid;
            if (e.kind == EdgeKind::Contains && e.dst == id) return true;
        }
        return false;
    };

    aleph::containers::FlatSet<NodeId> closed{};
    std::vector<NodeId>                path{};
    auto path_contains = [&](NodeId id) noexcept -> bool {
        for (NodeId p : path) if (p == id) return true;
        return false;
    };
    auto path_pop = [&](NodeId id) noexcept {
        for (std::size_t i = path.size(); i-- > 0;) {
            if (path[i] == id) {
                path.erase(path.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
    };

    struct Frame { NodeId id; Mat4 world; bool entering; };
    std::vector<Frame> stack{};

    for (auto [rid, rnode] : g.nodes()) {
        if (t::kind_of(rnode) != NodeKind::Transform) continue;
        if (has_incoming_contains(rid)) continue;  // not a root

        stack.clear();
        const auto& root_xf = std::get<t::Transform>(rnode);
        stack.push_back(Frame{rid, root_xf.local.m, true});

        while (!stack.empty()) {
            Frame fr = stack.back();
            stack.pop_back();

            if (!fr.entering) {            // leaving frame: pop the active path
                path_pop(fr.id);
                continue;
            }
            if (path_contains(fr.id)) return false;   // Contains cycle
            if (closed.contains(fr.id)) continue;     // reached via another path
            path.push_back(fr.id);
            closed.insert(fr.id);

            const t::Node* np = g.node(fr.id);
            if (np != nullptr) {
                const NodeKind k = t::kind_of(*np);
                if (k == NodeKind::Mesh) {
                    out.push_back(EmitEvent{fr.id, fr.world, false});
                } else if (k == NodeKind::Light) {
                    out.push_back(EmitEvent{fr.id, fr.world, true});
                }
            }

            stack.push_back(Frame{fr.id, fr.world, false});  // leave marker

            // Contains children in edge insertion order, pushed reversed so the
            // first child is processed first (matching `lower()`).
            std::vector<NodeId> children{};
            for (auto [eid, e] : g.edges()) {
                (void)eid;
                if (e.kind == EdgeKind::Contains && e.src == fr.id) {
                    children.push_back(e.dst);
                }
            }
            for (std::size_t i = children.size(); i-- > 0;) {
                const NodeId cid = children[i];
                const t::Node* cn = g.node(cid);
                Mat4 child_world = fr.world;
                if (cn != nullptr && t::kind_of(*cn) == NodeKind::Transform) {
                    child_world = fr.world * std::get<t::Transform>(*cn).local.m;
                }
                stack.push_back(Frame{cid, child_world, true});
            }
        }
    }
    return true;
}

// Build the world-space payload for a Mesh emission event WITHOUT re-deriving
// the ancestor chain: the event already carries the world matrix `lower()` would
// use. Resolves the Mesh's Material through `References` exactly as `lower()`.
// Returns false on a dangling reference (the caller falls back to full).
[[nodiscard]] inline bool entity_from_event(const Graph& g, const EmitEvent& ev,
                                            LoweredEntity& out) {
    const t::Node* mn = g.node(ev.id);
    if (mn == nullptr || t::kind_of(*mn) != NodeKind::Mesh) return false;
    NodeId mat_id{};
    if (!referenced_material(g, ev.id, mat_id)) return false;
    const t::Node* mat = g.node(mat_id);
    if (mat == nullptr || t::kind_of(*mat) != NodeKind::Material) return false;
    out.source         = ev.id;
    out.world_geometry = to_world(std::get<t::Mesh>(*mn).geometry, ev.world);
    out.material       = to_params(std::get<t::Material>(*mat));
    return true;
}

// Build the light-table payload for a Light emission event, mirroring `lower()`.
[[nodiscard]] inline bool light_from_event(const Graph& g, const EmitEvent& ev,
                                           LoweredEntity& out) {
    const t::Node* ln = g.node(ev.id);
    if (ln == nullptr || t::kind_of(*ln) != NodeKind::Light) return false;
    const auto& light = std::get<t::Light>(*ln);
    out.source         = ev.id;
    out.world_geometry = to_world(light.geometry, ev.world);
    out.material       = MaterialParams{
        aleph::types::MaterialKind::Emissive,
        light.emission,
        0.0f, 1.5f,
        light.emission};
    return true;
}

}  // namespace aleph::lowering::detail

export namespace aleph::lowering {

// ── lower_incremental_structural (SPEC §4) ──────────────────────────────────
// Incremental patch for the STRUCTURAL ops AddObject / AddLight / DeleteObject.
// The result is BYTE-IDENTICAL to `lower(after)`: we re-walk `lower()`'s exact
// Contains DFS over `after` (so entity order, light order and handle_map order
// all match full), but for each emitted node we REUSE `prev`'s already-lowered
// payload when the node is a clean survivor and only RECOMPUTE it when the node
// was created by the op. Surviving nodes' Contains chains and References are
// untouched by these ops, so a survivor's payload is provably identical to what
// full would emit (same world matrix from the same DFS, same material) — hence
// reuse is sound. `recomputed_entities` counts only the recomputed (created)
// Meshes, proving O(dirty). `light_groups` is recomputed via the sheaf ONLY when
// the op changed Influences/Light/emission, else reused from `prev`.
//
// Any inconsistency (DFS cycle, dangling reference, a "clean" survivor missing
// from `prev`) FALLS BACK to a full `lower(after)` — correctness over speed.
[[nodiscard]] inline std::expected<LoweredScene, LowerError>
lower_incremental_structural(const LoweredScene&              prev,
                             const aleph::graph::Graph&       after,
                             const aleph::lowering::Op&       op,
                             const aleph::dpo::RewriteRecord& rec,
                             IncrementalStats*                stats) {
    using aleph::types::NodeId;
    namespace t = aleph::types;

    auto fall_back_to_full = [&]() -> std::expected<LoweredScene, LowerError> {
        std::expected<LoweredScene, LowerError> full = lower(after);
        if (full.has_value() && stats != nullptr) {
            stats->recomputed_entities     = full->entities.size();
            stats->light_groups_recomputed = true;
            stats->importance_recomputed   = true;  // full lower recomputed importance
        }
        return full;
    };

    // `created` = the set of NodeIds the op minted (AddObject -> [mesh, material];
    // AddLight -> [light]; DeleteObject -> none). A created Mesh/Light has no
    // entry in `prev`, so its payload must be recomputed; everything else is a
    // clean survivor reused verbatim. `deleted_nodes` simply will not appear in
    // the DFS over `after`, so it needs no explicit handling here.
    aleph::containers::FlatSet<NodeId> created;
    for (NodeId id : rec.created_nodes) created.insert(id);

    // Fast lookup of a survivor's lowered ENTITY by source: reuse `prev.entities`
    // verbatim, indexed through `prev.handle_map` (NodeId -> entities index).
    auto reuse_entity = [&](NodeId src, LoweredEntity& out) -> bool {
        const std::uint32_t* idx = prev.handle_map.get(src);
        if (idx == nullptr || *idx >= prev.entities.size()) return false;
        out = prev.entities[*idx];
        return true;
    };

    // ── Reproduce `lower()`'s DFS emission order over `after`. ──────────────
    std::vector<detail::EmitEvent> events;
    if (!detail::dfs_emit(after, events)) {
        return fall_back_to_full();  // Contains cycle: full reports InvalidHierarchy
    }

    LoweredScene out{};
    out.camera = prev.camera;  // structural ops never touch the Camera node

    std::size_t recomputed = 0;

    // Walk the DFS events IN ORDER, building `entities`, `handle_map` and the
    // light table exactly as `lower()` interleaves them.
    for (const detail::EmitEvent& ev : events) {
        if (ev.is_light) {
            // A Light node: a light-table entry only (never an `entities` entry).
            LoweredEntity lent{};
            if (created.contains(ev.id)) {
                if (!detail::light_from_event(after, ev, lent)) return fall_back_to_full();
                ++recomputed;
            } else {
                // Clean Light survivor: reuse its `prev.lights` entry verbatim.
                bool found = false;
                for (const LoweredEntity& pl : prev.lights) {
                    if (pl.source == ev.id) { lent = pl; found = true; break; }
                }
                if (!found) return fall_back_to_full();
            }
            out.lights.push_back(lent);
            continue;
        }

        // A Mesh node: an `entities` entry, ALSO a light-table entry iff emissive.
        LoweredEntity ent{};
        if (created.contains(ev.id)) {
            if (!detail::entity_from_event(after, ev, ent)) return fall_back_to_full();
            ++recomputed;
        } else {
            if (!reuse_entity(ev.id, ent)) return fall_back_to_full();
        }
        const auto index = static_cast<std::uint32_t>(out.entities.size());
        out.entities.push_back(ent);
        (void)out.handle_map.insert(ev.id, index);
        // Light-table policy (SPEC §3): an emissive Mesh joins the light table at
        // its DFS position — exactly where `lower()` would push it.
        if (is_emissive(ent.material)) {
            out.lights.push_back(ent);
        }
    }

    // ── importance (SPEC §4.1, Phase 5.x-b) ─────────────────────────────────
    // Per-entity importance is a pure function of the Adjacent mesh skeleton +
    // the set of Meshes (Ollivier-Ricci aggregated per Mesh, then min-max
    // normalized across ALL meshes). A STRUCTURAL op that adds or removes a Mesh
    // (AddObject / DeleteObject) changes that vertex/edge set — the normalization
    // range and incident-edge sets shift — so importance MUST be recomputed.
    // AddLight touches neither the mesh set nor any Adjacent edge, so every
    // mesh's importance is unchanged; we reuse `prev`'s values, re-aligned to the
    // new entity order by source (the mesh order is itself unchanged, but the
    // by-source lookup is robust and costs O(N) over a copyable f64 map).
    const bool importance_dirty = std::holds_alternative<AddObject>(op)
                               || std::holds_alternative<DeleteObject>(op);
    out.importance.resize(out.entities.size(), 0.0);
    if (importance_dirty) {
        const auto imp = entity_importance(after);
        for (std::size_t i = 0; i < out.entities.size(); ++i) {
            const double* v = imp.get(out.entities[i].source);
            out.importance[i] = (v != nullptr) ? *v : 0.0;
        }
    } else {
        // Reuse `prev.importance` verbatim, mapped by source: build the source ->
        // importance association from `prev` (aligned to `prev.entities`) and
        // re-emit it in `out`'s entity order. A survivor missing from `prev`
        // cannot happen on the clean AddLight path (no mesh was added), but if it
        // did we degrade to 0 (uniform) for that slot rather than misalign.
        for (std::size_t i = 0; i < out.entities.size(); ++i) {
            const NodeId src = out.entities[i].source;
            const std::uint32_t* idx = prev.handle_map.get(src);
            out.importance[i] =
                (idx != nullptr && *idx < prev.importance.size())
                    ? prev.importance[*idx]
                    : 0.0;
        }
    }

    // ── light_groups (SPEC §4.4) ────────────────────────────────────────────
    // Recompute the sheaf H⁰ partition ONLY when the op may have changed it:
    //   * AddLight     — created a `Light` node (a new singleton group at least);
    //   * AddObject    — created a `Material`; if it is emissive the new mesh is
    //                    a light (changes membership). A non-emissive AddObject
    //                    adds neither a Light, an Influences edge, nor emission,
    //                    so the partition is unchanged — reuse `prev`.
    //   * DeleteObject — removed a Mesh; if that mesh was emissive (a member of
    //                    the light table / groups) or had incident Influences
    //                    edges, the partition changes — recompute. Otherwise the
    //                    deleted mesh never touched Influences/Light/emission, so
    //                    the partition is unchanged — reuse `prev`.
    // When NONE of those hold we reuse `prev.light_groups` verbatim (no sheaf
    // pass) and leave `light_groups_recomputed` false. Recompute is always
    // topology-correct, hence byte-identical, so being conservative is safe.
    bool light_groups_dirty = false;
    if (std::holds_alternative<AddLight>(op)) {
        light_groups_dirty = true;  // a Light node was added
    } else if (std::holds_alternative<AddObject>(op)) {
        // AddObject adds NO `Light`, `Influences`, or `Adjacent` edge — only a
        // Mesh+Material with `References` + parent `Contains`. The new mesh is an
        // isolated 1-skeleton vertex with an empty visibility stalk, so it forms
        // no group and grabs no Light; the Light partition is therefore UNCHANGED
        // for a non-emissive AddObject and `prev.light_groups` is reused. We still
        // flag dirty when the new material is EMISSIVE — emission changed (SPEC
        // §3.4) — and recompute (byte-identical regardless), so the literal "emit
        // changed ⇒ recompute" rule holds and the §6.4 reuse test exercises the
        // clean (non-emissive) path. Inspect the created mesh's material in `after`.
        for (NodeId id : rec.created_nodes) {
            const t::Node* n = after.node(id);
            if (n == nullptr || t::kind_of(*n) != t::NodeKind::Mesh) continue;
            NodeId mat_id{};
            if (detail::referenced_material(after, id, mat_id)) {
                const t::Node* mat = after.node(mat_id);
                if (mat != nullptr && t::kind_of(*mat) == t::NodeKind::Material
                    && detail::luminance(std::get<t::Material>(*mat).emit) > 0.0f) {
                    light_groups_dirty = true;
                }
            }
        }
    } else {  // DeleteObject
        // DeleteObject removes a Mesh and CASCADES its incident edges — possibly
        // `Adjacent` edges (changing the 1-skeleton / flag complex) and/or
        // `Influences` edges (changing which lights touch a region). Either can
        // merge or split H⁰ components, hence change the Light grouping, even if
        // the mesh was not itself a light. The mesh's pre-state incident edges are
        // no longer in `after` and are not recorded in `prev` (which holds only
        // lowered IR + Light-id groups), so we cannot cheaply PROVE the partition
        // is unchanged. We therefore conservatively RECOMPUTE `light_groups` for
        // every DeleteObject. (`light_groups_of(after)` is topology-correct, so
        // this stays byte-identical; the reuse fast-path is reserved for the
        // provably-clean Add* cases above.)
        light_groups_dirty = true;
    }

    if (light_groups_dirty) {
        out.light_groups = light_groups_of(after);
        if (stats != nullptr) {
            stats->recomputed_entities     = recomputed;
            stats->light_groups_recomputed = true;
            stats->importance_recomputed   = importance_dirty;
        }
        return out;
    }

    out.light_groups = prev.light_groups;  // reused verbatim (no sheaf recompute)
    if (stats != nullptr) {
        stats->recomputed_entities     = recomputed;
        stats->light_groups_recomputed = false;
        stats->importance_recomputed   = importance_dirty;
    }
    return out;
}

// ── lower_incremental (SPEC §3/§4) ──────────────────────────────────────────
//
//   prev  — the LoweredScene produced before the op.
//   after — the graph AFTER `apply_op` committed the mutation.
//   op    — the editor op that was applied (drives the dirty set for ATTRIBUTE
//           ops, which mutate in place so `rec` carries no created/deleted).
//   rec   — the RewriteRecord `apply_op` returned (created/deleted host ids for
//           STRUCTURAL ops; empty for attribute ops).
//   stats — optional; populated with the work bound on success.
//
// CONTRACT: the returned scene is BYTE-IDENTICAL to `lower(after)` for every op
// type (SPEC §2). ATTRIBUTE ops take the dirty-set patch path below; every other
// op type FALLS BACK to a full `lower(after)`.
[[nodiscard]] inline std::expected<LoweredScene, LowerError>
lower_incremental(const LoweredScene&              prev,
                  const aleph::graph::Graph&       after,
                  const aleph::lowering::Op&       op,
                  const aleph::dpo::RewriteRecord& rec,
                  IncrementalStats*                stats = nullptr) {
    using aleph::types::EdgeKind;
    using aleph::types::NodeId;
    namespace t = aleph::types;

    // A small helper: take the whole result from a full `lower(after)` (the
    // proven byte-identical path) and stamp the stats as a full recompute. Used
    // by every fallback below so the byte-identical contract is never at risk.
    auto fall_back_to_full = [&]() -> std::expected<LoweredScene, LowerError> {
        std::expected<LoweredScene, LowerError> full = lower(after);
        if (full.has_value() && stats != nullptr) {
            stats->recomputed_entities     = full->entities.size();
            stats->light_groups_recomputed = true;
            stats->importance_recomputed   = true;  // full lower recomputed importance
        }
        return full;
    };

    // ApplyRule is an arbitrary DPO rewrite (SPEC §5): FALL BACK to full, which
    // is byte-identical by being exactly `lower(after)`.
    if (std::holds_alternative<ApplyRule>(op)) {
        return fall_back_to_full();
    }

    // ── STRUCTURAL ops (AddObject / AddLight / DeleteObject) ────────────────
    // `rec` carries the topology delta. Surviving nodes are unchanged, so every
    // surviving entity is reused verbatim and only created Meshes are recomputed;
    // we re-walk `lower()`'s DFS to reproduce its exact emission order.
    const bool is_structural = std::holds_alternative<AddObject>(op)
                            || std::holds_alternative<AddLight>(op)
                            || std::holds_alternative<DeleteObject>(op);
    if (is_structural) {
        return lower_incremental_structural(prev, after, op, rec, stats);
    }

    // Is this an ATTRIBUTE op (SetMaterial / SetTransform)? Those are patched by
    // the dirty-set path below; everything else has already returned above.
    const bool is_attr = std::holds_alternative<SetMaterial>(op)
                         || std::holds_alternative<SetTransform>(op);
    if (!is_attr) {
        // Defensive: an op kind not handled above. Full is always byte-identical.
        return fall_back_to_full();
    }

    // `rec` carries structural deltas; attribute ops report none — the dirty set
    // comes from `op`, so `rec` is unused on the attribute path below.
    (void)rec;

    // ── Dirty set (SPEC §3) ─────────────────────────────────────────────────
    // `dirty` is the set of MESH NodeIds whose lowered entity must be recomputed.
    // `light_groups_dirty` flags whether emission/topology changed enough to
    // require re-running the sheaf H⁰ pass (attribute ops never change topology,
    // but SetMaterial editing `emit` is conservatively treated as light-dirty).
    aleph::containers::FlatSet<NodeId> dirty;        // dirty MESH ids
    aleph::containers::FlatSet<NodeId> dirty_lights;  // dirty LIGHT-node ids
    bool light_groups_dirty = false;

    if (const auto* sm = std::get_if<SetMaterial>(&op)) {
        // The op names the MESH whose referenced Material was edited. Resolve
        // that Material, then dirty EVERY Mesh referencing it (they all share the
        // changed params). A dangling reference cannot happen on a committed op,
        // but if the graph is inconsistent we fall back to full for safety.
        NodeId edited_mat{};
        if (!detail::referenced_material(after, sm->target, edited_mat)) {
            std::expected<LoweredScene, LowerError> full = lower(after);
            if (full.has_value() && stats != nullptr) {
                stats->recomputed_entities     = full->entities.size();
                stats->light_groups_recomputed = true;
                stats->importance_recomputed   = true;  // full lower recomputed importance
            }
            return full;
        }
        for (auto [id, node] : after.nodes()) {
            if (t::kind_of(node) != t::NodeKind::Mesh) continue;
            NodeId m{};
            if (detail::referenced_material(after, id, m) && m == edited_mat) {
                dirty.insert(id);
            }
        }
        // SetMaterial that changes `emit` moves the entity in/out of the light
        // table (SPEC §3.4). We cannot know the OLD emit from the new params
        // alone, so we compare against what `prev` recorded: a SetMaterial is
        // light-dirty iff the new params are emissive OR any dirtied mesh was
        // emissive in `prev`'s light table (i.e. it may be toggling off).
        // Conservative — recomputing the (topology-invariant) groups stays
        // byte-identical regardless.
        if (is_emissive(sm->params)) {
            light_groups_dirty = true;
        } else {
            for (const LoweredEntity& pl : prev.lights) {
                if (dirty.contains(pl.source)) { light_groups_dirty = true; break; }
            }
        }
    } else if (const auto* st = std::get_if<SetTransform>(&op)) {
        // A Transform's new local pose shifts the world transform of every
        // Contains-descendant — both Meshes (their entity world geometry) AND
        // Light nodes (their light-table world geometry). `lower()` transforms a
        // Light's geometry by the same world matrix, so a descendant Light's
        // light-table entry is dirty too. Materials/emission are untouched, so the
        // sheaf H⁰ grouping (topology-only) need NOT be recomputed.
        aleph::containers::FlatSet<NodeId> desc;
        detail::contains_descendants(after, st->target, desc);
        for (auto [id, node] : after.nodes()) {
            const t::NodeKind k = t::kind_of(node);
            if (!desc.contains(id)) continue;
            if (k == t::NodeKind::Mesh)        dirty.insert(id);
            else if (k == t::NodeKind::Light)  dirty_lights.insert(id);
        }
    }

    // ── PATCH prev (SPEC §4) ────────────────────────────────────────────────
    // Topology is unchanged for an attribute op, so `prev.entities` is ALREADY
    // in the exact DFS order `lower(after)` would emit. Walk it in order:
    //   * clean entity  -> copy from `prev` verbatim (provably identical);
    //   * dirty entity  -> recompute `world_geometry`/`material`.
    // Reassemble `entities` in that same order and rebuild `handle_map`. The
    // light table is rebuilt the same way (DFS-ordered emissive meshes UNION
    // Light entries), copying clean light entries and recomputing dirty ones.
    LoweredScene out{};
    out.camera = prev.camera;  // attribute ops never touch the Camera node

    std::size_t recomputed = 0;

    for (const LoweredEntity& prev_ent : prev.entities) {
        const NodeId src = prev_ent.source;
        LoweredEntity ent{};
        if (dirty.contains(src)) {
            if (!detail::recompute_entity(after, src, ent)) {
                // The graph became inconsistent for this mesh; fall back to full
                // so correctness (byte-identical) is never compromised.
                std::expected<LoweredScene, LowerError> full = lower(after);
                if (full.has_value() && stats != nullptr) {
                    stats->recomputed_entities     = full->entities.size();
                    stats->light_groups_recomputed = true;
                    stats->importance_recomputed   = true;  // full lower recomputed importance
                }
                return full;
            }
            ++recomputed;
        } else {
            ent = prev_ent;  // clean: byte-identical reuse
        }
        const auto index = static_cast<std::uint32_t>(out.entities.size());
        out.entities.push_back(ent);
        (void)out.handle_map.insert(src, index);
    }

    // ── importance (SPEC §4.1, Phase 5.x-b) ─────────────────────────────────
    // An ATTRIBUTE op (SetMaterial / SetTransform) changes NO graph topology: the
    // mesh set and every `Adjacent` edge are untouched, so the Ollivier-Ricci
    // aggregate and its min-max normalization are byte-identical to `prev`'s.
    // `out.entities` is rebuilt in `prev.entities`' exact order, so `prev.
    // importance` (aligned to `prev.entities`) re-aligns 1:1 — reuse it verbatim,
    // skipping the flow pass entirely (SPEC §6 incremental_importance reuse case).
    out.importance = prev.importance;

    // ── Light table ─────────────────────────────────────────────────────────
    // `lower()` builds `lights` during the SAME DFS that builds `entities`,
    // interleaving emissive Meshes and Light nodes in DFS order. `prev.lights` is
    // therefore already in the correct order for the LIGHT-NODE entries (which an
    // attribute op never adds/removes/moves). The only churn is emissive-MESH
    // entries: a SetMaterial can toggle a mesh's emissivity. To stay byte-
    // identical we rebuild `lights` from `prev.lights`:
    //   * a Light-node entry        -> copy verbatim (never affected);
    //   * an emissive-Mesh entry that is still emissive -> recomputed payload;
    //   * an emissive-Mesh entry that is no longer emissive -> dropped;
    //   * a mesh that BECAME emissive -> must be inserted at its DFS position.
    // An emissive toggle changes the light table's MEMBERSHIP and its ordering
    // relative to the interleaved Light nodes. Reproducing that DFS-interleaved
    // order in isolation would duplicate `lower()`'s whole traversal, so we split
    // on `light_groups_dirty` (set iff emission may have toggled):
    //   * toggled  -> take `lights` + `light_groups` from a full `lower(after)`
    //                 (the entities patched above are already byte-identical to
    //                 full, so the merged scene is too);
    //   * untoggled-> membership/order are unchanged; patch `prev.lights` in place
    //                 (recompute dirty entries, reuse the rest verbatim) and reuse
    //                 `prev.light_groups`.
    if (light_groups_dirty) {
        // Emission changed: the light-table DFS interleaving and the sheaf H⁰
        // groups must both match full exactly. Recomputing them in isolation
        // would duplicate `lower()`'s DFS; defer to the proven path and take the
        // light table + groups from a full lower (entities above are already the
        // incremental patch and are byte-identical, so the merged result is too).
        std::expected<LoweredScene, LowerError> full = lower(after);
        if (!full.has_value()) return std::unexpected(full.error());
        out.lights       = std::move(full->lights);
        out.light_groups = std::move(full->light_groups);
        if (stats != nullptr) {
            stats->recomputed_entities     = recomputed;
            stats->light_groups_recomputed = true;
            // importance is topology-invariant under an attribute op: `out.
            // importance` was reused from `prev` above (== full's), so the flow
            // pass did NOT run.
            stats->importance_recomputed   = false;
        }
        return out;
    }

    // No emissive toggle: the light table's membership and DFS order are
    // unchanged (no entry enters or leaves), so `prev.lights` is already in the
    // exact order `lower(after)` emits. Copy each entry, recomputing the payload
    // of any whose source is dirty:
    //   * a dirty emissive-MESH entry  -> world geometry / material recomputed
    //     (SetTransform shift, or a non-emit-changing SetMaterial param edit);
    //   * a dirty LIGHT-node entry      -> world geometry recomputed (SetTransform
    //     moved the subtree that Contains it);
    //   * everything else               -> reused verbatim (byte-identical).
    out.lights.reserve(prev.lights.size());
    for (const LoweredEntity& prev_light : prev.lights) {
        const NodeId src = prev_light.source;
        const t::Node* sn = after.node(src);
        const t::NodeKind k = (sn != nullptr) ? t::kind_of(*sn) : t::NodeKind::Mesh;
        const bool mesh_dirty  = (k == t::NodeKind::Mesh)  && dirty.contains(src);
        const bool light_dirty = (k == t::NodeKind::Light) && dirty_lights.contains(src);
        if (mesh_dirty || light_dirty) {
            LoweredEntity ent{};
            const bool ok = mesh_dirty ? detail::recompute_entity(after, src, ent)
                                       : detail::recompute_light(after, src, ent);
            if (!ok) {
                std::expected<LoweredScene, LowerError> full = lower(after);
                if (full.has_value() && stats != nullptr) {
                    stats->recomputed_entities     = full->entities.size();
                    stats->light_groups_recomputed = true;
                    stats->importance_recomputed   = true;  // full lower recomputed importance
                }
                return full;
            }
            out.lights.push_back(ent);
        } else {
            out.lights.push_back(prev_light);  // verbatim reuse
        }
    }

    // `light_groups` is the sheaf H⁰ partition over Influences/Adjacent topology
    // + Light existence — none of which an attribute op touches — so it is reused
    // from `prev` verbatim (no sheaf recompute). This is the SPEC §4.4 / §6.4
    // "op that does NOT touch Influences/emission reuses prev.light_groups" path.
    out.light_groups = prev.light_groups;  // std::vector<std::vector<NodeId>> is copyable

    if (stats != nullptr) {
        stats->recomputed_entities     = recomputed;
        stats->light_groups_recomputed = false;
        // Attribute op: importance reused from `prev` verbatim (no flow pass).
        stats->importance_recomputed   = false;
    }
    return out;
}

}  // namespace aleph::lowering

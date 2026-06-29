// aleph.lowering:ops — the RETURN path of the pipeline (SPEC §5):
//
//   editor gesture ──▶ Op ──▶ apply_op ──▶ typed GRAPH mutation ──▶ re-lower
//
// Editing is a *morphism, not a mutation of the render product* (SPEC §1). The
// editor never touches the `RenderScene`; it emits an `Op` that mutates the
// GraphScene (the single source of truth), after which the caller re-lowers.
// One truth; the rest is derived.
//
// This file carries BOTH op families (SPEC §5):
//   * ATTRIBUTE ops (W4) — typed, validated, in-place attribute mutations that
//     never change a node's NodeKind:
//       - SetTransform — replace a `Transform` node's `LocalTransform`.
//       - SetMaterial  — retarget the `Material` a `Mesh` References, replacing
//                        that Material node's normalized params.
//   * STRUCTURAL ops (W5) — transactional create/delete of nodes+edges:
//       - AddObject   — create a per-object Transform + Mesh + Material, wired
//                       parent—Contains→Transform—Contains→Mesh and
//                       Mesh—References→Material.
//       - AddLight    — create a Light node, Contained by a parent Transform.
//       - DeleteObject{NodeId} — delete a Mesh and cascade its incident edges
//                       plus its exclusive Material(s) and its now-childless
//                       per-object Transform (never a root — one level only).
//       - ApplyRule{const dpo::Rule*, dpo::Match} — replay a DPO rewrite.
//
// ── ALL-OR-NOTHING (SPEC §5) ────────────────────────────────────────────────
// Every op returns a NEW VALID state or fails with NO PARTIAL EFFECT. We obtain
// this exactly the way `aleph::dpo::apply` does: we never mutate the live graph
// in place. Instead we (1) VALIDATE the request against the live graph read-only
// (targets exist and have the expected kinds), then (2) build the post-state in
// a SNAPSHOT `Graph` — copying every surviving node (preserving its NodeId so
// `lower()`'s `handle_map` stays stable for survivors), minting fresh ids for
// created nodes, and reconstructing every surviving edge — (3) run `validate_all`
// on the snapshot, and only (4) on success commit it back via `g = std::move`.
// On ANY failure path the live `g` is left byte-for-byte as it was.
//
// Edge ids are reassigned by reconstruction, exactly as `dpo::apply` does — the
// structure (kind/src/dst of every edge) is identical, which is what `lower()`
// reads. The success report is an `aleph::dpo::RewriteRecord` so the whole
// return path (attribute AND structural ops) speaks ONE vocabulary to the
// caller: an attribute op creates/deletes nothing (all four record vectors
// empty, mirroring a DPO `ModifyAttr`); a structural op fills created_*/
// deleted_* with host-graph ids; `ApplyRule` forwards `dpo::apply`'s record
// verbatim.
//
// No exceptions (aleph_flags_isa): every fallible path is `std::expected`.
// Determinism (SPEC §7): node insertion order and edge insertion order are
// preserved by the snapshot rebuild; created node ids are minted off a node
// allocator fast-forwarded past every preserved id (the `dpo::apply` recipe), so
// a re-lower of the committed graph is deterministic and byte-stable.

module;
#include <cstddef>
#include <cstdint>
#include <expected>
#include <utility>
#include <variant>
#include <vector>

export module aleph.lowering:ops;

import aleph.graph;   // Graph: nodes()/edges()/node()/insert_node()/add_edge()/alloc_node_id()/validate_all
import aleph.types;   // Node/Edge, NodeId, EdgeId, NodeKind, EdgeKind, LocalTransform, Mesh/Material/Light, GeometryPayload, kind_of
import aleph.dpo;     // Rule, Match, find_matches, apply, RewriteRecord, ApplyError (the shared return-path engine + report)
import aleph.math;    // Vec3, f32 (carried by MaterialParams)
import :lowered;      // MaterialParams (the normalized, renderer-independent material view)

export namespace aleph::lowering {

// ── Attribute ops (SPEC §5) ─────────────────────────────────────────────────
// Each names a target graph `NodeId` and the new attribute value. Applying an
// op replaces ONLY the named attribute on the target; the node's identity and
// kind are invariant.

// Replace the `LocalTransform` of a `Transform` node. The transform hierarchy
// (the `Contains` edges) is untouched — only this node's local pose changes —
// so re-lowering recomposes every descendant's world transform off the new
// local. `LocalTransform` (not a raw Mat4) is the contract so it can grow to
// TRS / GA rotor / dual-quat later without churning this op (SPEC §3).
struct SetTransform {
    types::NodeId        target{};
    types::LocalTransform local{};
};

// Retarget the material on ONE object: `target` names a `Mesh`, and the op
// replaces the normalized parameters of the `Material` that mesh References.
// (The editor gesture is "change the material on THIS object", so the op speaks
// in mesh ids — `LoweredEntity::source` is a Mesh id, matching SPEC §8.5's "only
// that entity's MaterialParams changed".) `params` is the SAME frozen, renderer-
// independent material view the IR uses (`:lowered`), so the editor speaks one
// material vocabulary on both the down (lower) and up (op) paths. The new
// `kind`/albedo/fuzz/ior/emit are projected back onto the referenced graph
// `Material`; because the light table keys off `emit` luminance (SPEC §3),
// editing `emit` here can add or drop the entity from the light table on the
// next `lower()` — no separate "make it a light" op. A mesh with no/dangling
// `References→Material` is a STRUCTURED error (`DanglingReference`, SPEC §5),
// never a silent default — mirroring `lower()`.
struct SetMaterial {
    types::NodeId  target{};   // the MESH whose referenced Material to edit
    MaterialParams params{};
};

// ── Structural ops (SPEC §5) ────────────────────────────────────────────────
// These CREATE and/or DELETE nodes and edges. They are transactional via the
// same snapshot/rollback wrapper `dpo::apply` uses, and (for `ApplyRule`)
// directly via `dpo::apply` itself. Each is all-or-nothing: a post-state that
// fails `validate_all` is never committed, so a graph invariant can never be
// transiently broken (SPEC §5: no partial effects).

// Add a renderable object: create a per-object `Transform` (identity pose) + a
// `Mesh` carrying `geometry` (LOCAL space — lowering composes world from the
// hierarchy) + a `Material` carrying `params`, then wire the three edges that
// make the object whole and invariant-valid: `Mesh —References→ Material` (so
// `MaterialReferenced` holds — the very anti-dangling property `lower()` relies
// on), `parent —Contains→ Transform` (so the per-object transform is reachable
// by the DFS), and `Transform —Contains→ Mesh` (so the object is independently
// posable via SetTransform). `parent` MUST name an existing `Transform`;
// otherwise the op fails with a structured error and the graph is unchanged.
// New node ids are minted deterministically in the post-state; the
// `RewriteRecord` reports them (created_nodes = [transform, mesh, material])
// plus the three created edge ids.
struct AddObject {
    types::NodeId          parent{};                         // an existing Transform to Contain the new per-object Transform (and transitively the mesh)
    types::GeometryPayload geometry{types::SphereLocal{}};   // LOCAL geometry payload
    MaterialParams         material{};                       // normalized material for the new Material node
};

// Add an explicit sampling light: create a `Light` node (its own kind — NOT a
// mesh; SPEC §3) carrying `emission` + a LOCAL `geometry`, Contained by an
// existing `parent` Transform so it lowers into the light table. `parent` MUST
// be a `Transform`. The `RewriteRecord` reports created_nodes = [light] and the
// created Contains edge.
struct AddLight {
    types::NodeId          parent{};                         // an existing Transform to Contain the light
    types::LightKind       kind{types::LightKind::Point};
    math::Vec3             emission{1.0f, 1.0f, 1.0f};
    types::GeometryPayload geometry{types::QuadLocal{}};     // LOCAL geometry payload
};

// Delete an object: remove the named `Mesh` and CASCADE the orphaned remains of
// its AddObject footprint, in ONE transaction. The DELETE-SET, computed over the
// pre-state graph, is:
//   * the target Mesh itself;
//   * every Material ONLY this mesh References (no other node has a References
//     edge to it) — a Material SHARED with another mesh survives untouched;
//   * the mesh's controlling per-object Transform, iff the mesh was its ONLY
//     Contains child AND that Transform itself has an incoming Contains edge
//     (the NON-ROOT guard: a root or shared/structural Transform is never
//     deleted).
// ONE level only — no recursion, no upward cascade beyond that Transform (e.g.
// a Texture referenced by a cascaded Material is left in place). Every edge
// incident to ANY delete-set node is cascaded, exactly as the `remove_object`
// DPO rule does for the mesh's own edges. `target` MUST name an existing
// `Mesh`. The `RewriteRecord` reports deleted_nodes = the whole delete-set and
// the pre-state ids of every cascaded edge. After re-lowering there are NO
// dangling handles (SPEC §8.7): the mesh's entity simply vanishes from
// `entities` / `handle_map`; survivors keep their ids.
struct DeleteObject {
    types::NodeId target{};   // the MESH to remove (with its incident edges)
};

// Replay a DPO rewrite. `rule` is a borrowed pointer to a `dpo::Rule` (typically
// one of `dpo::rules::*`); `match` is an OPTIONAL embedding produced by
// `dpo::find_matches` (move-only — it owns `OrderedMap`s — so `ApplyRule`, and
// therefore `Op`, are move-only too).
//
// The editor gesture is "apply THIS rule" — it usually does not pre-compute the
// embedding. So when `match` is left empty (its `node_map` has no bindings),
// `apply_op` DISCOVERS the embedding itself, deterministically: it runs
// `dpo::find_matches(rule, g)` (VF2 backtracking in host insertion order, SPEC
// §7) and applies the FIRST match. A rule with no embedding in the live graph is
// a STRUCTURED error (`NoMatch`) — never a silent no-op — and leaves the graph
// unchanged. A caller that DOES pre-compute a specific embedding may pass it in
// `match` (non-empty `node_map`); `apply_op` then uses exactly that one.
//
// Either way the transaction is `dpo::apply`, the canonical transactional engine:
// build the post-state, `validate_all`, commit or roll back. The `RewriteRecord`
// is returned VERBATIM (created/deleted node + edge ids in host-graph ids). A
// null `rule` is a structured error, never a crash. A rolled-back rewrite
// (post-state fails an invariant) leaves the graph and thus the next `lower()`
// unchanged (SPEC §8.7).
struct ApplyRule {
    const dpo::Rule* rule{nullptr};
    dpo::Match       match{};
};

// The editor's op vocabulary: the ATTRIBUTE family ∪ the STRUCTURAL family
// (SPEC §5). `ApplyRule` embeds a move-only `dpo::Match`, so `Op` is a move-only
// value type — it is constructed in place and passed to `apply_op` by const
// reference; it is never copied.
using Op = std::variant<SetTransform, SetMaterial,
                        AddObject, AddLight, DeleteObject, ApplyRule>;

// Structured failure of an op (SPEC §5: no silent defaults). Every failure
// leaves the graph unchanged.
enum class OpError {
    NodeNotFound,        // a target/parent NodeId is not in the graph
    KindMismatch,        // the target exists but is the wrong NodeKind for the op
    DanglingReference,   // SetMaterial's mesh has no / a broken References→Material
    InvariantViolation,  // the post-state failed validate_all -> NOT committed
    EdgeTypeMismatch,    // a created edge was rejected by the typed-edge rules
    NullRule,            // ApplyRule was handed a null Rule pointer
    NoMatch,             // ApplyRule's rule has no embedding in the live graph
};

}  // namespace aleph::lowering

// ── Implementation (non-exported) ───────────────────────────────────────────
namespace aleph::lowering::detail {

// Translate a `dpo::ApplyError` into the op vocabulary so `ApplyRule` speaks one
// `OpError` enum to the caller while delegating the transaction to `dpo::apply`.
[[nodiscard]] inline OpError from_apply_error(aleph::dpo::ApplyError e) noexcept {
    switch (e) {
        case aleph::dpo::ApplyError::InvariantViolation:
            return OpError::InvariantViolation;
        case aleph::dpo::ApplyError::AttrSetMismatch:
            return OpError::KindMismatch;
        case aleph::dpo::ApplyError::DanglingEdgeAfterDelete:
            return OpError::InvariantViolation;
    }
    return OpError::InvariantViolation;
}

// Project a normalized `MaterialParams` onto a fresh graph `Material` node body.
[[nodiscard]] inline aleph::types::Material
material_from(aleph::types::NodeId id, const MaterialParams& p) noexcept {
    aleph::types::Material m{};
    m.id     = id;
    m.kind   = p.kind;
    m.albedo = p.albedo;
    m.fuzz   = p.fuzz;
    m.ior    = p.ior;
    m.emit   = p.emit;
    m.uv_scale = p.uv_scale;
    return m;
}

// Advance `out`'s node allocator past every preserved node id so freshly minted
// ids never collide with ids carried over from the pre-state. Mirrors
// `dpo::apply`'s `fast_forward_node_alloc` (we cannot call that private detail,
// so we replicate it on the public Graph API).
inline void fast_forward_node_alloc(aleph::graph::Graph& out) {
    std::uint32_t max_id = 0;
    bool any = false;
    for (auto [nid, node] : out.nodes()) {
        (void)node; any = true;
        if (nid.value > max_id) max_id = nid.value;
    }
    if (any) while (out.alloc_node_id().value <= max_id) { /* drain */ }
}

// Commit an ATTRIBUTE mutation transactionally (the `dpo::apply` pattern).
//
// `expect` is the NodeKind the target must have; `mutate(Node&)` edits the
// target's COPY in the snapshot (it must not change the node's kind). The live
// graph `g` is read-only until the final commit, so every early return leaves it
// exactly as it was — all-or-nothing by construction.
template <typename Mutate>
[[nodiscard]] std::expected<aleph::dpo::RewriteRecord, OpError>
commit_attr(aleph::graph::Graph& g,
            aleph::types::NodeId target,
            aleph::types::NodeKind expect,
            Mutate&& mutate) {
    using aleph::types::Node;

    // (1) Validate the request against the LIVE graph, read-only.
    const Node* live = g.node(target);
    if (live == nullptr) {
        return std::unexpected(OpError::NodeNotFound);
    }
    if (aleph::types::kind_of(*live) != expect) {
        return std::unexpected(OpError::KindMismatch);
    }

    // (2) Build the post-state in a snapshot graph: copy every node in insertion
    //     order, applying the attribute change to the target's copy only.
    aleph::graph::Graph post;
    for (auto [nid, node] : g.nodes()) {
        Node copy = node;
        if (nid == target) {
            mutate(copy);  // typed in-place attribute edit on the copy
        }
        post.insert_node(copy);
    }
    // Reconstruct every edge in insertion order (kind/src/dst preserved). All
    // endpoints still exist (no node was removed), so add_edge cannot dangle;
    // a type-mismatch here would mean the source graph was already invalid.
    for (auto [eid, e] : g.edges()) {
        (void)eid;
        auto r = post.add_edge(e.kind, e.src, e.dst);
        if (!r.has_value()) {
            return std::unexpected(OpError::InvariantViolation);  // g untouched
        }
    }

    // (3) Post-condition: commit only if every invariant still holds.
    //     (max_in_degree = SIZE_MAX: attribute edits don't change degree; a
    //      caller wanting a cap re-validates with its own bound, as dpo::apply.)
    if (auto ok = aleph::graph::validate_all(post, static_cast<std::size_t>(-1));
        !ok.has_value()) {
        return std::unexpected(OpError::InvariantViolation);  // g untouched
    }

    // (4) Commit. The graph now reflects the edit; the caller re-lowers.
    g = std::move(post);
    // An attribute edit creates/deletes nothing (node ids preserved, edge
    // structure identical) — the RewriteRecord is empty, like a DPO ModifyAttr.
    return aleph::dpo::RewriteRecord{};
}

// Commit a STRUCTURAL mutation transactionally (the `dpo::apply` pattern, for
// create/delete). `build(post, rec)` populates the post-state snapshot — it
// inserts surviving + created nodes (preserving survivor ids, minting created
// ids off the fast-forwarded allocator), reconstructs surviving edges, adds
// created edges, and records every created/deleted host id into `rec`. On
// success the snapshot is committed and `rec` returned; on any failure the live
// graph is untouched.
//
// The builder owns the post-state shape entirely; this wrapper only validates
// (`validate_all`) and commits-or-rolls-back, so all four structural ops share
// one transactional spine and one all-or-nothing guarantee (SPEC §5).
template <typename Build>
[[nodiscard]] std::expected<aleph::dpo::RewriteRecord, OpError>
commit_structural(aleph::graph::Graph& g, Build&& build) {
    aleph::graph::Graph        post;
    aleph::dpo::RewriteRecord  rec;

    // The builder reports its own structured failure (e.g. a bad endpoint kind).
    if (auto e = build(post, rec); !e.has_value()) {
        return std::unexpected(e.error());  // g untouched
    }

    // Post-condition: commit only if every invariant still holds.
    if (auto ok = aleph::graph::validate_all(post, static_cast<std::size_t>(-1));
        !ok.has_value()) {
        return std::unexpected(OpError::InvariantViolation);  // g untouched
    }

    g = std::move(post);  // commit
    return rec;
}

// Copy every node of `src` into `dst` preserving ids, then fast-forward `dst`'s
// node allocator past them so created ids are collision-free. Edges are NOT
// copied here — callers add surviving/created edges themselves (so a delete can
// skip cascaded edges).
inline void clone_nodes(const aleph::graph::Graph& src, aleph::graph::Graph& dst) {
    for (auto [nid, node] : src.nodes()) {
        (void)nid;
        aleph::types::Node copy = node;
        dst.insert_node(copy);
    }
    fast_forward_node_alloc(dst);
}

// The first root Transform (a Transform with no incoming Contains), in node
// insertion order — the same "root" notion `lower()` uses (SPEC §4.2 step 1).
// Returns false if the graph has no root Transform.
[[nodiscard]] inline bool first_root_transform(const aleph::graph::Graph& g,
                                               aleph::types::NodeId& out) noexcept {
    for (auto [nid, node] : g.nodes()) {
        if (aleph::types::kind_of(node) != aleph::types::NodeKind::Transform) continue;
        bool incoming = false;
        for (auto [eid, e] : g.edges()) {
            (void)eid;
            if (e.kind == aleph::types::EdgeKind::Contains && e.dst == nid) {
                incoming = true;
                break;
            }
        }
        if (!incoming) { out = nid; return true; }
    }
    return false;
}

// Does `id` have any incoming `Contains` edge (i.e. is it reachable by the
// transform DFS `lower()` runs)?
[[nodiscard]] inline bool has_incoming_contains(const aleph::graph::Graph& g,
                                                aleph::types::NodeId id) noexcept {
    for (auto [eid, e] : g.edges()) {
        (void)eid;
        if (e.kind == aleph::types::EdgeKind::Contains && e.dst == id) return true;
    }
    return false;
}

// After a DPO rewrite that MINTED new Mesh nodes (e.g. a monotone "refine"
// split), those meshes carry their References→Material edge but are NOT yet
// wired into the Contains hierarchy — so `lower()`'s transform DFS would never
// reach them and they would silently fail to materialize as entities. A
// structural op must produce a state that re-lowers to a VALID Scene with the
// added geometry visible (SPEC §5/§8.7), so we wire each freshly-created Mesh
// that has NO incoming Contains under the FIRST root Transform (insertion order
// — the same root `lower()` uses), in a SEPARATE transactional step (snapshot →
// validate_all → commit-or-rollback). This adds ONLY Contains edges (no nodes),
// so the rewrite's RewriteRecord — its created_nodes/deleted_nodes — is
// unchanged, and survivors keep their ids so `lower()`'s handle_map stays stable
// for them. If there is no root Transform, or no created Mesh needs reattaching,
// the graph is left exactly as it was.
[[nodiscard]] inline std::expected<void, OpError>
attach_created_meshes(aleph::graph::Graph& g,
                      const std::vector<aleph::types::NodeId>& created) {
    // Collect the created Mesh ids that still lack an incoming Contains.
    std::vector<aleph::types::NodeId> orphans;
    for (aleph::types::NodeId id : created) {
        const aleph::types::Node* n = g.node(id);
        if (n == nullptr) continue;
        if (aleph::types::kind_of(*n) != aleph::types::NodeKind::Mesh) continue;
        if (has_incoming_contains(g, id)) continue;
        orphans.push_back(id);
    }
    if (orphans.empty()) return {};  // nothing to wire — g untouched

    aleph::types::NodeId root{};
    if (!first_root_transform(g, root)) {
        // No hierarchy root to attach under: leave the graph as the rewrite left
        // it (the caller can still inspect the RewriteRecord).
        return {};
    }

    // Transactional rebuild: copy survivors (ids preserved), rebuild every edge,
    // then add one Contains edge per orphan, validate, commit-or-rollback.
    auto r = commit_structural(
        g,
        [&](aleph::graph::Graph& post, aleph::dpo::RewriteRecord& rec)
            -> std::expected<void, OpError> {
            clone_nodes(g, post);
            for (auto [eid, e] : g.edges()) {
                (void)eid;
                auto rr = post.add_edge(e.kind, e.src, e.dst);
                if (!rr.has_value()) {
                    return std::unexpected(OpError::InvariantViolation);
                }
            }
            for (aleph::types::NodeId id : orphans) {
                auto con = post.add_edge(aleph::types::EdgeKind::Contains, root, id);
                if (!con.has_value()) {
                    return std::unexpected(OpError::EdgeTypeMismatch);
                }
                rec.created_edges.push_back(*con);  // local record, discarded
            }
            return {};
        });
    if (!r.has_value()) return std::unexpected(r.error());
    return {};
}

}  // namespace aleph::lowering::detail

export namespace aleph::lowering {

// ── apply_op(Graph&, const Op&) -> expected<RewriteRecord, OpError> (SPEC §5) ─
// Dispatch the op onto its typed, all-or-nothing mutation. On success the GRAPH
// (the single source of truth) reflects the edit and the caller re-lowers; on
// failure the graph is unchanged and a structured `OpError` is returned.
[[nodiscard]] inline std::expected<aleph::dpo::RewriteRecord, OpError>
apply_op(aleph::graph::Graph& g, const Op& op) {
    return std::visit(
        [&](const auto& o) -> std::expected<aleph::dpo::RewriteRecord, OpError> {
            using T = std::decay_t<decltype(o)>;

            if constexpr (std::is_same_v<T, SetTransform>) {
                // Replace the Transform node's local pose, in place.
                return detail::commit_attr(
                    g, o.target, aleph::types::NodeKind::Transform,
                    [&](aleph::types::Node& n) {
                        std::get<aleph::types::Transform>(n).local = o.local;
                    });

            } else if constexpr (std::is_same_v<T, SetMaterial>) {
                // The op names the MESH; resolve the Material it References and
                // edit THAT node. Validate the mesh (exists + is a Mesh) against
                // the LIVE graph read-only, then find its References→Material edge
                // exactly as `lower()` does (first such edge, insertion order). A
                // missing/dangling reference is a STRUCTURED error — never a
                // silent default (SPEC §5) — and leaves the graph unchanged.
                const aleph::types::Node* mesh = g.node(o.target);
                if (mesh == nullptr) {
                    return std::unexpected(OpError::NodeNotFound);
                }
                if (aleph::types::kind_of(*mesh) != aleph::types::NodeKind::Mesh) {
                    return std::unexpected(OpError::KindMismatch);
                }
                aleph::types::NodeId mat_id{};
                bool found = false;
                for (auto [eid, e] : g.edges()) {
                    if (e.kind == aleph::types::EdgeKind::References
                        && e.src == o.target) {
                        mat_id = e.dst;
                        found  = true;
                        break;
                    }
                }
                if (!found) {
                    return std::unexpected(OpError::DanglingReference);
                }
                const aleph::types::Node* mat = g.node(mat_id);
                if (mat == nullptr
                    || aleph::types::kind_of(*mat) != aleph::types::NodeKind::Material) {
                    return std::unexpected(OpError::DanglingReference);
                }
                // Project the normalized MaterialParams back onto the referenced
                // Material node, replacing kind + all physical params (SPEC §3).
                // `commit_attr` re-validates `mat_id` is a Material and rebuilds
                // the snapshot transactionally (all-or-nothing).
                return detail::commit_attr(
                    g, mat_id, aleph::types::NodeKind::Material,
                    [&](aleph::types::Node& n) {
                        auto& m  = std::get<aleph::types::Material>(n);
                        m.kind   = o.params.kind;
                        m.albedo = o.params.albedo;
                        m.fuzz   = o.params.fuzz;
                        m.ior    = o.params.ior;
                        m.emit   = o.params.emit;
                    });

            } else if constexpr (std::is_same_v<T, AddObject>) {
                // Create a per-object Transform + Mesh + Material and the edges
                // that make the object whole and invariant-valid (parent→
                // Transform→Mesh, Mesh→Material).
                // Validate the parent (exists + is a Transform) up front so a bad
                // parent fails BEFORE any snapshot work; the snapshot then mints
                // the new ids, copies survivors, rebuilds surviving edges, and
                // adds the new edges — all-or-nothing.
                const aleph::types::Node* parent = g.node(o.parent);
                if (parent == nullptr) {
                    return std::unexpected(OpError::NodeNotFound);
                }
                if (aleph::types::kind_of(*parent) != aleph::types::NodeKind::Transform) {
                    return std::unexpected(OpError::KindMismatch);
                }
                return detail::commit_structural(
                    g,
                    [&](aleph::graph::Graph& post, aleph::dpo::RewriteRecord& rec)
                        -> std::expected<void, OpError> {
                        // Survivors first (ids preserved), then fast-forward the
                        // allocator so new ids are collision-free.
                        detail::clone_nodes(g, post);

                        // Per-object Transform (identity) so the object is
                        // independently posable: parent ─Contains→ Transform
                        // ─Contains→ Mesh. (Transform→Transform→Mesh is
                        // invariant-legal; lowering composes nested Transforms.)
                        const aleph::types::NodeId xf_id = post.alloc_node_id();
                        post.insert_node(aleph::types::Node{aleph::types::Transform{
                            xf_id, 0,
                            aleph::types::LocalTransform{aleph::math::Mat4::identity()}}});
                        rec.created_nodes.push_back(xf_id);

                        const aleph::types::NodeId mesh_id = post.alloc_node_id();
                        aleph::types::Mesh mesh{};
                        mesh.id       = mesh_id;
                        mesh.geometry = o.geometry;
                        post.insert_node(aleph::types::Node{std::move(mesh)});
                        rec.created_nodes.push_back(mesh_id);

                        const aleph::types::NodeId mat_id = post.alloc_node_id();
                        post.insert_node(aleph::types::Node{
                            detail::material_from(mat_id, o.material)});
                        rec.created_nodes.push_back(mat_id);

                        // Reconstruct every surviving edge (insertion order).
                        for (auto [eid, e] : g.edges()) {
                            (void)eid;
                            auto r = post.add_edge(e.kind, e.src, e.dst);
                            if (!r.has_value()) {
                                return std::unexpected(OpError::InvariantViolation);
                            }
                        }

                        // Mesh —References→ Material (satisfies MaterialReferenced).
                        auto ref = post.add_edge(aleph::types::EdgeKind::References,
                                                 mesh_id, mat_id);
                        if (!ref.has_value()) {
                            return std::unexpected(OpError::EdgeTypeMismatch);
                        }
                        rec.created_edges.push_back(*ref);

                        // parent —Contains→ Transform —Contains→ Mesh.
                        auto pcon = post.add_edge(aleph::types::EdgeKind::Contains,
                                                  o.parent, xf_id);
                        if (!pcon.has_value()) {
                            return std::unexpected(OpError::EdgeTypeMismatch);
                        }
                        rec.created_edges.push_back(*pcon);
                        auto con = post.add_edge(aleph::types::EdgeKind::Contains,
                                                 xf_id, mesh_id);
                        if (!con.has_value()) {
                            return std::unexpected(OpError::EdgeTypeMismatch);
                        }
                        rec.created_edges.push_back(*con);
                        return {};
                    });

            } else if constexpr (std::is_same_v<T, AddLight>) {
                // Create a Light node Contained by an existing Transform. A Light
                // is its OWN node (SPEC §3) — not a mesh — so no Material/
                // References edge is involved; only the parent Contains edge.
                const aleph::types::Node* parent = g.node(o.parent);
                if (parent == nullptr) {
                    return std::unexpected(OpError::NodeNotFound);
                }
                if (aleph::types::kind_of(*parent) != aleph::types::NodeKind::Transform) {
                    return std::unexpected(OpError::KindMismatch);
                }
                return detail::commit_structural(
                    g,
                    [&](aleph::graph::Graph& post, aleph::dpo::RewriteRecord& rec)
                        -> std::expected<void, OpError> {
                        detail::clone_nodes(g, post);

                        const aleph::types::NodeId light_id = post.alloc_node_id();
                        aleph::types::Light light{};
                        light.id       = light_id;
                        light.kind     = o.kind;
                        light.emission = o.emission;
                        light.geometry = o.geometry;
                        post.insert_node(aleph::types::Node{std::move(light)});
                        rec.created_nodes.push_back(light_id);

                        for (auto [eid, e] : g.edges()) {
                            (void)eid;
                            auto r = post.add_edge(e.kind, e.src, e.dst);
                            if (!r.has_value()) {
                                return std::unexpected(OpError::InvariantViolation);
                            }
                        }

                        auto con = post.add_edge(aleph::types::EdgeKind::Contains,
                                                 o.parent, light_id);
                        if (!con.has_value()) {
                            return std::unexpected(OpError::EdgeTypeMismatch);
                        }
                        rec.created_edges.push_back(*con);
                        return {};
                    });

            } else if constexpr (std::is_same_v<T, DeleteObject>) {
                // Remove a Mesh and CASCADE the orphaned remains of its
                // AddObject footprint: every incident edge, every Material ONLY
                // this mesh References (a shared Material survives), and its
                // controlling per-object Transform iff the mesh was its only
                // Contains child AND the Transform has an incoming Contains
                // edge (the NON-ROOT guard) — one level only, no recursion.
                // Validate target (exists + is a Mesh) up front.
                const aleph::types::Node* mesh = g.node(o.target);
                if (mesh == nullptr) {
                    return std::unexpected(OpError::NodeNotFound);
                }
                if (aleph::types::kind_of(*mesh) != aleph::types::NodeKind::Mesh) {
                    return std::unexpected(OpError::KindMismatch);
                }

                // ── the DELETE-SET, computed over the PRE-state graph ────────
                // [mesh] ∪ [exclusive Materials] ∪ [childless controlling
                // Transform]. It stays tiny (a handful of ids), so a vector +
                // linear membership test is the right size of machinery.
                std::vector<aleph::types::NodeId> doomed;
                doomed.push_back(o.target);
                const auto is_doomed = [&doomed](aleph::types::NodeId id) noexcept {
                    for (const aleph::types::NodeId d : doomed) {
                        if (d == id) return true;
                    }
                    return false;
                };

                // Exclusive Material(s): a Material the target References that
                // NO OTHER node References. A shared Material survives.
                for (auto [eid, e] : g.edges()) {
                    (void)eid;
                    if (e.kind != aleph::types::EdgeKind::References
                        || e.src != o.target) {
                        continue;
                    }
                    const aleph::types::Node* m = g.node(e.dst);
                    if (m == nullptr
                        || aleph::types::kind_of(*m)
                               != aleph::types::NodeKind::Material) {
                        continue;
                    }
                    bool shared = false;
                    for (auto [eid2, e2] : g.edges()) {
                        (void)eid2;
                        if (e2.kind == aleph::types::EdgeKind::References
                            && e2.dst == e.dst && e2.src != o.target) {
                            shared = true;
                            break;
                        }
                    }
                    if (!shared && !is_doomed(e.dst)) doomed.push_back(e.dst);
                }

                // Controlling Transform: a Contains-parent of the mesh that is
                // a Transform, has NO other Contains child (the mesh was its
                // only one), and has an incoming Contains edge itself — the
                // NON-ROOT guard (a root or shared/structural Transform is
                // never cascaded).
                for (auto [eid, e] : g.edges()) {
                    (void)eid;
                    if (e.kind != aleph::types::EdgeKind::Contains
                        || e.dst != o.target) {
                        continue;
                    }
                    const aleph::types::Node* p = g.node(e.src);
                    if (p == nullptr
                        || aleph::types::kind_of(*p)
                               != aleph::types::NodeKind::Transform) {
                        continue;
                    }
                    bool other_child = false;
                    for (auto [eid2, e2] : g.edges()) {
                        (void)eid2;
                        if (e2.kind == aleph::types::EdgeKind::Contains
                            && e2.src == e.src && e2.dst != o.target) {
                            other_child = true;
                            break;
                        }
                    }
                    if (other_child) continue;
                    if (!detail::has_incoming_contains(g, e.src)) continue;
                    if (!is_doomed(e.src)) doomed.push_back(e.src);
                }

                return detail::commit_structural(
                    g,
                    [&](aleph::graph::Graph& post, aleph::dpo::RewriteRecord& rec)
                        -> std::expected<void, OpError> {
                        // Copy every node NOT in the delete-set (preserving
                        // survivor ids), then fast-forward (no new ids minted,
                        // but keeps the allocator consistent with the
                        // dpo::apply recipe).
                        for (auto [nid, node] : g.nodes()) {
                            if (is_doomed(nid)) continue;
                            aleph::types::Node copy = node;
                            post.insert_node(copy);
                        }
                        detail::fast_forward_node_alloc(post);
                        for (const aleph::types::NodeId d : doomed) {
                            rec.deleted_nodes.push_back(d);
                        }

                        // Reconstruct edges, skipping (cascading away) every edge
                        // incident to ANY delete-set node; report their PRE-state
                        // ids (matching dpo::apply's deletion-id convention).
                        for (auto [eid, e] : g.edges()) {
                            if (is_doomed(e.src) || is_doomed(e.dst)) {
                                rec.deleted_edges.push_back(eid);
                                continue;
                            }
                            auto r = post.add_edge(e.kind, e.src, e.dst);
                            if (!r.has_value()) {
                                return std::unexpected(OpError::InvariantViolation);
                            }
                        }
                        return {};
                    });

            } else if constexpr (std::is_same_v<T, ApplyRule>) {
                // Forward to the canonical DPO engine. A null rule is a structured
                // error (never a deref).
                if (o.rule == nullptr) {
                    return std::unexpected(OpError::NullRule);
                }
                // The editor gesture is "apply THIS rule" — it usually does not
                // pre-compute the embedding. If the supplied match carries no
                // bindings, DISCOVER one deterministically (VF2 in host insertion
                // order, SPEC §7) and apply the FIRST embedding. A rule with no
                // embedding in the live graph is a STRUCTURED error (NoMatch),
                // never a silent no-op — the graph is left unchanged. A caller
                // that pre-computed a specific embedding passes it in `o.match`
                // (non-empty node_map) and that exact one is used.
                aleph::dpo::RewriteRecord rec;
                if (o.match.node_map.empty()) {
                    std::vector<aleph::dpo::Match> matches =
                        aleph::dpo::find_matches(*o.rule, g);
                    if (matches.empty()) {
                        return std::unexpected(OpError::NoMatch);  // g untouched
                    }
                    auto r = aleph::dpo::apply(*o.rule, matches.front(), g);
                    if (!r.has_value()) {
                        return std::unexpected(detail::from_apply_error(r.error()));
                    }
                    rec = std::move(*r);
                } else {
                    // Caller-supplied embedding: apply exactly that one.
                    auto r = aleph::dpo::apply(*o.rule, o.match, g);
                    if (!r.has_value()) {
                        return std::unexpected(detail::from_apply_error(r.error()));
                    }
                    rec = std::move(*r);
                }
                // A monotone "add geometry" rule mints Mesh nodes carrying a
                // References→Material edge but NO Contains edge, so `lower()`'s
                // transform DFS would not reach them. Wire every newly-created
                // orphan Mesh into the hierarchy (transactional, all-or-nothing)
                // so the post-state re-lowers to a VALID Scene with the added
                // geometry visible (SPEC §5/§8.7). Adds only Contains edges, so
                // `rec`'s created/deleted node ids are unchanged.
                if (auto w = detail::attach_created_meshes(g, rec.created_nodes);
                    !w.has_value()) {
                    return std::unexpected(w.error());
                }
                return rec;

            } else {
                // Keep the visitor total: every Op alternative is handled above.
                static_assert(std::is_same_v<T, SetTransform>
                                  || std::is_same_v<T, SetMaterial>
                                  || std::is_same_v<T, AddObject>
                                  || std::is_same_v<T, AddLight>
                                  || std::is_same_v<T, DeleteObject>
                                  || std::is_same_v<T, ApplyRule>,
                              "apply_op: unhandled Op alternative");
                return std::unexpected(OpError::KindMismatch);
            }
        },
        op);
}

}  // namespace aleph::lowering

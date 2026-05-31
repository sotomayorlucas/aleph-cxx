// aleph.lowering:ops — the RETURN path of the pipeline (SPEC §5):
//
//   editor gesture ──▶ Op ──▶ apply_op ──▶ typed GRAPH mutation ──▶ re-lower
//
// Editing is a *morphism, not a mutation of the render product* (SPEC §1). The
// editor never touches the `RenderScene`; it emits an `Op` that mutates the
// GraphScene (the single source of truth), after which the caller re-lowers.
// One truth; the rest is derived.
//
// This wave (W4) implements the ATTRIBUTE op family ONLY:
//   * SetTransform — replace a `Transform` node's `LocalTransform`.
//   * SetMaterial  — retarget the `Material` a `Mesh` References, replacing that
//                    Material node's normalized params (the op names the MESH).
// Both are TYPED, VALIDATED, in-place attribute mutations (no node's NodeKind
// ever changes — only attributes). The STRUCTURAL op family
// (AddObject / AddLight / DeleteObject / ApplyRule) is transactional via
// `aleph.dpo` and arrives in W5 (SPEC §5 / §6 / §10). The `Op` variant and the
// `apply_op` switch below leave a clearly marked extension point for them.
//
// ── ALL-OR-NOTHING (SPEC §5) ────────────────────────────────────────────────
// Every op returns a NEW VALID state or fails with NO PARTIAL EFFECT. We obtain
// this the same way `aleph::dpo::apply` does: we never mutate the live graph in
// place. Instead we (1) VALIDATE the request against the live graph read-only
// (the target exists and has the expected kind), then (2) build the post-state
// in a SNAPSHOT `Graph` — copying every node, applying the attribute change to
// the target's copy, and reconstructing every edge — (3) run `validate_all` on
// the snapshot, and only (4) on success commit it back via `g = std::move(post)`.
// On ANY failure path the live `g` is left byte-for-byte as it was. Node ids are
// preserved (so `lower()`'s `handle_map` stays stable for survivors); edge ids
// are reassigned by reconstruction, exactly as `dpo::apply` does — the structure
// (kind/src/dst of every edge) is identical, which is what `lower()` reads.
//
// The success report is an `aleph::dpo::RewriteRecord` so the whole return path
// (attribute AND structural ops) speaks ONE vocabulary to the caller. An
// attribute op creates and deletes nothing, so all four record vectors are empty
// — the mutation is an in-place attribute edit, mirroring a DPO `ModifyAttr`.
//
// No exceptions (aleph_flags_isa): every fallible path is `std::expected`.
// Determinism (SPEC §7): node insertion order and edge insertion order are
// preserved by the snapshot rebuild, so a re-lower of the committed graph is
// byte-identical to lowering any graph with the same structure + attributes.

module;
#include <expected>
#include <utility>
#include <variant>

export module aleph.lowering:ops;

import aleph.graph;   // Graph: nodes()/edges()/node()/insert_node()/add_edge()/validate_all
import aleph.types;   // Node/Edge, NodeId, NodeKind, EdgeKind, LocalTransform, Material, kind_of
import aleph.dpo;     // RewriteRecord (the shared return-path report)
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

// The editor's op vocabulary. W4 = the ATTRIBUTE family only.
//
// EXTENSION POINT (W5, SPEC §5): the STRUCTURAL family is added here as
//   std::variant<SetTransform, SetMaterial,
//                AddObject, AddLight, DeleteObject, ApplyRule>
// Those ops are transactional via `aleph.dpo` (they create/delete nodes+edges),
// so they slot into `apply_op`'s switch alongside the attribute ops below and
// reuse the very same `RewriteRecord` return type — no signature change.
using Op = std::variant<SetTransform, SetMaterial>;

// Structured failure of an op (SPEC §5: no silent defaults). Every failure
// leaves the graph unchanged.
enum class OpError {
    NodeNotFound,        // the target NodeId is not in the graph
    KindMismatch,        // the target exists but is the wrong NodeKind for the op
    DanglingReference,   // SetMaterial's mesh has no / a broken References→Material
    InvariantViolation,  // the post-state failed validate_all -> NOT committed
};

}  // namespace aleph::lowering

// ── Implementation (non-exported) ───────────────────────────────────────────
namespace aleph::lowering::detail {

// Commit an attribute mutation transactionally (the `dpo::apply` pattern).
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
            }
            // EXTENSION POINT (W5): structural ops (AddObject / AddLight /
            // DeleteObject / ApplyRule) add their own branches here, returning
            // their `dpo::apply` RewriteRecord. No signature change.
            else {
                // Unreachable for the current `Op` variant; keeps the visitor
                // total so no path falls off the end of a non-void lambda.
                static_assert(std::is_same_v<T, SetTransform>
                                  || std::is_same_v<T, SetMaterial>,
                              "apply_op: unhandled Op alternative");
                return std::unexpected(OpError::KindMismatch);
            }
        },
        op);
}

}  // namespace aleph::lowering

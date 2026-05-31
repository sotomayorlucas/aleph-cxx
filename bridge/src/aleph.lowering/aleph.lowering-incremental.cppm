// aleph.lowering:incremental — Phase 5.x-c incremental lowering (SPEC
// 2026-05-31, "Incremental Lowering (dirty-sets)").
//
//   After an editor `Op`, re-lower ONLY what changed instead of rebuilding the
//   whole `LoweredScene`. The result MUST be byte-identical to a full
//   `lower(after)` — incremental is purely an OPTIMIZATION, never a semantic
//   divergence (SPEC §1/§2).
//
// ── This file is a CORRECT-BUT-TRIVIAL SCAFFOLD (Phase 5.x-c, wave 0) ────────
// `lower_incremental` is wired with its final SIGNATURE and `IncrementalStats`
// contract, but its BODY is a full re-lower fallback: it ignores `prev`, `op`
// and `rec` and simply returns `lower(after)`. By construction this is
// byte-identical to full (it *is* full), so it is already CORRECT — it is just
// not yet incremental. Later waves (SPEC §8) compute the dirty set from `rec`
// and recompute only the affected entities + light groups, at which point the
// body changes but this signature does not.
//
// `IncrementalStats` is the work-bound instrumentation the SPEC's
// "actually_incremental" test (SPEC §6.3) keys off: how many entities were
// recomputed, and whether the light-group table (the sheaf H⁰ pass) was
// recomputed. In this stub the full re-lower recomputes EVERY entity and ALWAYS
// rebuilds the light groups, so `recomputed_entities == after's entity count`
// and `light_groups_recomputed == true` — an honest report of the fallback's
// cost, not a fabricated O(dirty) figure.
//
// No exceptions (aleph_flags_isa): the fallible path is `std::expected`,
// forwarded verbatim from `lower()`.

module;
#include <cstddef>
#include <expected>

export module aleph.lowering:incremental;

import :lowered;     // LoweredScene / LoweredEntity / MaterialParams (frozen IR)
import :lower;       // lower(), LoweredScene, LowerError
import :ops;         // Op (the editor op vocabulary)
import :grouping;    // light_groups_of (kept in the partition closure)
import aleph.graph;  // Graph
import aleph.dpo;    // RewriteRecord (what apply_op reported)
import aleph.types;  // NodeId etc. (vocab carried by the IR)

export namespace aleph::lowering {

// Work-bound instrumentation for incremental lowering (SPEC §6.3). A caller
// passes a pointer; on success the lowering fills it in. In the wave-0 stub the
// full re-lower recomputes every entity and always rebuilds the light groups.
struct IncrementalStats {
    std::size_t recomputed_entities{};
    bool        light_groups_recomputed{false};
};

// Re-lower after an editor `Op`, reusing `prev` where the change did not reach.
//
//   prev  — the LoweredScene produced before the op.
//   after — the graph AFTER `apply_op` committed the mutation.
//   op    — the editor op that was applied (drives the future dirty set).
//   rec   — the RewriteRecord `apply_op` returned (created/deleted host ids).
//   stats — optional; populated with the work bound on success.
//
// CONTRACT: the returned scene is byte-identical to `lower(after)` for every op
// type (SPEC §2). WAVE-0 STUB: this is achieved trivially by *being* the full
// re-lower — `prev`, `op` and `rec` are ignored. Later waves compute the dirty
// set from `rec` and recompute O(dirty) entities; the signature is unchanged.
[[nodiscard]] inline std::expected<LoweredScene, LowerError>
lower_incremental(const LoweredScene&             prev,
                  const aleph::graph::Graph&      after,
                  const aleph::lowering::Op&      op,
                  const aleph::dpo::RewriteRecord& rec,
                  IncrementalStats*               stats = nullptr) {
    // Wave-0 scaffold: ignore prev/op/rec and full re-lower. Byte-identical to
    // `lower(after)` by construction (it is exactly that call).
    (void)prev;
    (void)op;
    (void)rec;

    std::expected<LoweredScene, LowerError> result = lower(after);
    if (result.has_value() && stats != nullptr) {
        // The full re-lower recomputed every entity and rebuilt the light
        // groups. Report that honestly (no fabricated O(dirty) figure).
        stats->recomputed_entities      = result->entities.size();
        stats->light_groups_recomputed  = true;
    }
    return result;
}

}  // namespace aleph::lowering

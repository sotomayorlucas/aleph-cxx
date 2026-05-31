# Phase 5.x-c — Incremental Lowering (dirty-sets)

**Status:** design for review.
**Date:** 2026-05-31.
**Predecessor:** Phase 5 v1 + 5.x-a on `main`.

## 1. Goal

After an editor `Op`, re-lower **only what changed** instead of rebuilding the whole `LoweredScene`. The result must be **byte-identical to a full `lower(graph_after)`** — so incremental is purely an optimization, never a semantic divergence. Touches `aleph.lowering` only; no renderer change.

```cpp
// In aleph.lowering (new partition :incremental):
std::expected<LoweredScene, LowerError>
lower_incremental(const LoweredScene& prev,
                  const Graph& after,
                  const dpo::RewriteRecord& rec);   // rec = what apply_op changed
```

## 2. Why byte-identical-to-full is achievable

`full lower()` emits entities in **graph insertion order** with an insertion-ordered `handle_map`. A clean (un-dirtied) entity's lowered output is a pure function of its node + its transform chain + its material — none of which changed — so it is **provably identical** to what full would produce. Reassembling clean + recomputed entities in insertion order reproduces full's ordering exactly. Hence incremental == full, byte-for-byte.

## 3. Dirty-set computation (from `RewriteRecord` + dependency rules)

A node's lowered entity changes iff one of:
- **its own payload changed** (`SetMaterial` on the Material a Mesh references; `SetTransform`/geometry on the Mesh);
- **an ancestor `Transform` changed** (world transform shifts → all `Contains`-descendants dirty);
- **it was created** (new entity) or **deleted** (removed entity);
- for the **light table**: dirty iff any `Influences` edge or `Light`/emissive-`Material` changed (recompute `light_groups` via the sheaf only then).

`dirty(rec, after)`:
1. seed = `rec.created_nodes ∪ rec.deleted_nodes ∪` (nodes whose attributes the op touched).
2. transform-closure: if any seed is a `Transform`, add all its `Contains`-descendants.
3. material-closure: if a `Material` is dirty, add the `Mesh`es referencing it.
4. `light_groups_dirty` = seed touched any `Influences` edge, `Light` node, or `Material.emit`.

## 4. Algorithm
1. Start from `prev`. Drop entities sourced from deleted/dirty nodes; keep clean entities (their `LoweredEntity` is reused verbatim).
2. Recompute the dirty + created entities by lowering just those nodes (world transform via the (possibly cached) ancestor chain).
3. Reassemble `entities` in **graph insertion order** (iterate `after`'s meshes in order, taking the reused or recomputed entity) → rebuild `handle_map`.
4. `lights`: if `light_groups_dirty`, recompute the light table + `light_groups` (sheaf H⁰); else reuse `prev`'s.
5. Return the patched `LoweredScene`.

## 5. v1 scope
**Incremental (dirty-set recompute):** `SetMaterial`, `SetTransform`, `AddObject`, `AddLight`, `DeleteObject`. **Fallback to full `lower(after)`:** `ApplyRule` (arbitrary DPO rewrite — its dirty set is harder; correct via full, optimize later). Both paths return a result byte-identical to full.

## 6. Tests (`tests/lowering/`)
1. **incremental_equals_full (the oracle):** for each op type, on a non-trivial graph, `lower_incremental(prev, after, rec)` is **byte-identical** (shared `lowering_freeze.hpp`) to `lower(after)`.
2. **property_256:** 256 pseudo-random graphs × a random `Op` each → incremental == full byte-identical, every time (seeded, deterministic).
3. **actually_incremental (work bound):** instrument a recompute counter; a `SetMaterial` on one of N meshes recomputes **exactly 1** entity (not N); `SetTransform` on a leaf recomputes only that subtree. Proves it isn't secretly full.
4. **light_groups_incremental:** an op that does NOT touch `Influences`/emission reuses `prev.light_groups` (no sheaf recompute); one that does recomputes and matches full.
5. **applyrule_fallback:** an `ApplyRule` incremental == full (via fallback), no dangling handles.

## 7. Success criteria
- `lower_incremental` == `lower(after)` byte-identical across all op types + 256 random cases.
- Work-bound test proves attribute ops recompute O(dirty), not O(N).
- `release-strict` 0 warnings; full `ctest` green; determinism robust (3× guard).

## 8. Waves
- **W1** — `dirty()` + `lower_incremental` for attribute ops (`SetMaterial`/`SetTransform`) + tests 1(partial),3.
- **W2** — structural ops (`AddObject`/`AddLight`/`DeleteObject`) + `ApplyRule` fallback + light-group reuse + tests 1(full),2,4,5. Tag `v0.5.2-incremental`.

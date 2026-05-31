# Phase 5.x-b — Flow (Ollivier-Ricci) → Adaptive Sampling Importance

**Status:** design for review.
**Date:** 2026-05-31.
**Predecessor:** Phase 5 v1 + 5.x-a (light-grouping) + 5.x-c (incremental) on `main`.

## 1. Goal

Second proof that the math layer informs render: `aleph.flow`'s **Ollivier-Ricci curvature** over the scene's mesh adjacency yields a **per-entity importance**; the lowering bakes it into the `Scene` as **per-primitive importance**; and the path tracer gains an **opt-in adaptive-spp** path that spends more samples where high-importance entities are visible.

**Architectural constraint:** `render.rt` and `aleph.scene` NEVER import `aleph.flow`. The importance is a plain `f32` array baked onto the `Scene` by the lowering. Only `aleph.lowering` (the sanctioned cross-cutter) touches `aleph.flow`.

## 2. Honest scoping (the heuristic part)

- **Exactly verifiable:** importance == a deterministic function of `aleph.flow::ollivier_ricci` on a known graph (cross-checked against the flow module directly); flows through lowering into `Scene`; the adaptive-spp mechanism **demonstrably allocates samples ∝ importance** (sample-count buffer, histogram within tolerance).
- **NOT asserted (heuristic):** that Ricci is a *good* importance heuristic / that it reduces noise on real scenes. We assert the mechanism is correct + deterministic + opt-in, not that it's optimal.
- **Default unchanged:** `adaptive_spp=false` ⇒ the current uniform-spp loop, byte-for-byte.

## 3. Data flow
```
Graph (Adjacent edges, mesh skeleton)
   │ lower(): flow::ollivier_ricci -> per-edge kappa -> aggregate per Mesh -> importance in [0,1]
   ▼
LoweredScene.importance : vector<f64> aligned to entities
   │ build_render_scene(): push each entity's importance into the matching SoA store
   ▼
Scene.{sphere,quad,tri}_importance : parallel f32 arrays; hit() returns HitRecord.importance
   │ render.rt
   ▼
render_tile(adaptive_spp): per pixel a cheap primary-ray probe -> hit.importance ->
   spp_local = clamp(round(base_spp * (1 + (max_scale-1)*importance)), 1, base_spp*max_scale);
   accumulate spp_local samples, normalize by spp_local; record sample count.
```

## 4. Components

### 4.1 `aleph.lowering` — `:importance` (new partition) + bake
- Link `aleph_flow` into `aleph_lowering`.
- `entity_importance(const Graph&) -> containers::OrderedMap<types::NodeId, double>`: run `flow::ollivier_ricci` over the graph's `Adjacent` mesh skeleton; for each Mesh, aggregate the curvatures of its incident edges (mean), then **normalize across meshes to [0,1]** deterministically (min-max; if all equal or no edges, importance = 0 for all → uniform). A Mesh with no Adjacent edges → 0.
- `LoweredScene` gains `std::vector<double> importance;` (aligned to `entities`). `lower()` populates it (and `lower_incremental` recomputes it only when `Adjacent`/mesh topology is dirty — extend the 5.x-c dirty rules; else reuse).

### 4.2 `aleph.scene` — carry per-primitive importance (no flow dependency)
- Each SoA store (spheres/quads/tris) gains a parallel `std::vector<f32> importance;`. `HitRecord` gains `f32 importance{0};`.
- `aleph.scene::hit(...)` sets `HitRecord.importance` from the winning primitive's store. (Plain data; `aleph.scene` does not know about flow.)
- `build_render_scene` (lowering) pushes each entity's importance into the store it appends the primitive to.

### 4.3 `aleph.render.rt` — opt-in adaptive spp
- `RenderOpts` gains `bool adaptive_spp = false; int max_spp_scale = 4;`.
- `render_tile`, when `adaptive_spp`: per pixel, trace ONE cheap primary ray to read `hit.importance` (miss ⇒ importance 0), compute `spp_local` per the §3 formula, run that many samples, normalize by `spp_local`. Record per-pixel sample count into an optional `std::vector<std::uint32_t>* sample_counts` (for the test/instrumentation).
- Default (`adaptive_spp=false`): the existing uniform loop, **byte-identical**. `render.rt` does NOT import `aleph.flow`.

## 5. Tests
1. **importance_matches_ricci (exact):** on a known mesh-adjacency graph, `entity_importance` equals the deterministic aggregate of `aleph::flow::ollivier_ricci` (cross-check directly). Deterministic across two calls.
2. **importance_flows_to_scene:** `lower` → `build_render_scene` → the per-primitive `Scene` importance matches `LoweredScene.importance` for each entity's primitive.
3. **adaptive_alloc (mechanism):** render a scene with two regions of importance ~0 and ~1; the per-pixel `sample_counts` over the high-importance region average ≈ `max_spp_scale ×` the low region (within tolerance). Proves allocation ∝ importance.
4. **default_unchanged:** `adaptive_spp=false` renders byte-identical to the pre-feature path (single-light fixed scene, fixed seed).
5. **purity:** `render.rt`/`aleph.scene` contain no `aleph.flow` / `aleph::flow` reference (grep-gated in-test or by `iso_render_rt`/`iso_scene` staying green).
6. **incremental_importance:** an `Op` that does not change `Adjacent` topology reuses `prev.importance` (5.x-c stat); one that does recomputes and matches full.
7. **smoke:** `aleph_lower_demo --adaptive` renders a multi-mesh scene with adaptive spp → PPM (≥10 KB).

## 6. Scope
**IN:** Ricci→importance in lowering; per-primitive importance on Scene + HitRecord; opt-in adaptive spp; incremental importance reuse; the 7 tests. **OUT:** heat-field importance; per-pixel importance maps from secondary bounces; MIS; the "is it better" question.

## 7. Success criteria
- `entity_importance` == flow Ricci aggregate (exact); deterministic; flows to `Scene`.
- adaptive-spp sample allocation ∝ importance (mechanism test); `adaptive_spp=false` byte-unchanged.
- `render.rt` + `aleph.scene` free of any `aleph.flow` dependency (`iso_render_rt`, `iso_scene` green).
- `release-strict` 0 warnings; full `ctest` green (3× determinism guard).

## 8. Waves
- **W1** — `aleph.lowering:importance` (Ricci aggregate) + `LoweredScene.importance` + `lower`/`lower_incremental` bake + tests 1,6.
- **W2** — `Scene` per-primitive importance + `HitRecord` + `hit()` + `build_render_scene` bake + test 2.
- **W3** — `RenderOpts.adaptive_spp` + `render_tile` adaptive loop + sample_counts + demo `--adaptive` + tests 3,4,5,7. Tag `v0.5.3-adaptive-spp`.

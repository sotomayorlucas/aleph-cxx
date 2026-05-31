# Phase 5.x-a — Sheaf H⁰ Light-Grouping as Render Metadata

**Status:** design for review.
**Date:** 2026-05-31.
**Predecessor:** Phase 5 v1 (lowering bridge) merged on `main`.

## 1. Goal

First proof that **the mathematical layer informs how the scene is rendered** (not just what it is): the `VisibilitySheaf`'s **H⁰ connected components** partition the scene's lights into **groups**; the lowering bakes the grouping into the `LoweredScene`/`RenderScene` as metadata; and the path tracer gains an **opt-in group-stratified NEE** path that samples one light per group instead of summing all.

**Architectural constraint (unchanged):** sheaf state reaches the renderer ONLY through the lowering. `aleph.render.rt` never imports `aleph.sheaf`; it consumes a plain `light_groups` table on the `Scene`.

## 2. Honest scoping (the all-sum nuance)

Today NEE sums **all** lights exactly (`for Lh in scene.lights: direct += direct_light_quad`). So:
- **Verifiable core (this spec's center):** compute the grouping from the sheaf and bake it as metadata. Oracle is exact and image-independent: `groups == compute_h0(VisibilitySheaf) components` on known scenes; deterministic; flows through lowering.
- **Renderer payoff (opt-in):** a group-stratified NEE path (sample one light per group, weight by group size) gated by a `RenderOpts` flag, leaving the existing all-sum path the default. Its variance benefit appears with many lights; for a single group it must remain unbiased (matches all-sum in expectation).

## 3. Data flow

```
Graph (Influences: Light->Mesh edges)
   │  lower():  build VisibilitySheaf -> compute_h0 -> Components -> light groups
   ▼
LoweredScene.light_groups : vector<vector<NodeId>>   (+ per-light group id)
   │  build_render_scene()
   ▼
Scene.light_groups : vector<vector<Handle32>>        (groups of emissive-quad handles)
   │  render.rt
   ▼
ray_color(..., RenderOpts{grouped_nee}) : per shading point, if grouped_nee, sample one
   light per group (stratified) else sum all (default, unchanged)
```

## 4. Components

### 4.1 `aleph.lowering` — derive groups (new partition `:grouping`, or extend `:lower`)
- `light_groups_of(const Graph&) -> std::vector<std::vector<types::NodeId>>`: build the `VisibilitySheaf` from the graph (it already reads `Influences` edges), run `compute_h0`, and map the resulting `Component`s to **groups of Light nodes**: lights that co-influence a connected mesh region land in the same group; lights influencing disjoint regions are separate groups; a light with no `Influences` edge is its own singleton group. Deterministic (insertion order).
- `LoweredScene` gains `std::vector<std::vector<types::NodeId>> light_groups;` and the lowering populates it. When the graph has no `Influences` edges, every light is its own group (degenerate-but-valid).

### 4.2 `aleph.scene` — carry the grouping (thin extension)
- `Scene` gains `std::vector<std::vector<Handle32>> light_groups;` (groups of the emissive handles already in `s.lights`). `build_render_scene` fills it by mapping each grouped `NodeId` to its `Handle32` via the lowering's `handle_map` + the light table. If empty, the integrator treats all lights as one implicit group (= current behavior).

### 4.3 `aleph.render.rt` — opt-in group-stratified NEE
- `RenderOpts` gains `bool grouped_nee = false;`.
- In `ray_color`, when `grouped_nee && !scene.light_groups.empty()`: for each group pick one light (rng), evaluate `direct_light_quad`, weight by group size; sum across groups. Else: the existing all-lights sum (default, byte-unchanged).
- `render.rt` does NOT import `aleph.sheaf` — it only reads `scene.light_groups`.

## 5. Tests (`tests/lowering/` + `tests/render/`)
1. **grouping_matches_h0:** a graph with two disjoint Light↔Mesh `Influences` clusters → `light_groups_of` returns exactly 2 groups whose membership equals the `compute_h0` `Component` partition; a shared mesh merges two lights into one group; an unconnected light is a singleton. (Oracle: cross-check against `aleph::sheaf::compute_h0` directly.)
2. **grouping_deterministic:** two calls byte-identical; group order = insertion order.
3. **lowered_carries_groups:** `lower(graph).light_groups` matches `light_groups_of(graph)`; survives `build_render_scene` into `Scene.light_groups` with correct handle mapping.
4. **no_influences_degenerate:** graph without `Influences` → each light its own group; lowering still succeeds.
5. **grouped_nee_unbiased (render):** a scene whose lights form a single group renders (grouped_nee=true) to an image whose mean luminance matches the all-sum path within MC tolerance (unbiasedness sanity).
6. **smoke:** `aleph_lower_demo --grouped` (or a flag) renders a multi-light grouped scene to a PPM (≥10 KB).

## 6. Scope
**IN:** sheaf→groups in lowering; `light_groups` on LoweredScene + Scene; opt-in `grouped_nee`; the 6 tests. **OUT:** Ricci/heat importance (5.x-b); incremental lowering (5.x-c); multiple-importance sampling / power-weighted group selection (later).

## 7. Success criteria
- `light_groups_of` == `compute_h0` components on known scenes; deterministic; flows through lowering into `Scene`.
- `render.rt` stays free of any `aleph.sheaf` dependency (verified: it only reads `scene.light_groups`); `iso_render_rt` still green.
- Default render path byte-unchanged (grouped_nee=false); grouped path unbiased on a single-group scene.
- `release-strict` 0 warnings; full `ctest` green.

## 8. Waves
- **W1** — `light_groups_of` + `LoweredScene.light_groups` (lowering) + tests 1,2,4.
- **W2** — `Scene.light_groups` + `build_render_scene` mapping + test 3.
- **W3** — `RenderOpts.grouped_nee` + group-stratified NEE + tests 5,6 + demo flag. Tag `v0.5.1-light-grouping`.

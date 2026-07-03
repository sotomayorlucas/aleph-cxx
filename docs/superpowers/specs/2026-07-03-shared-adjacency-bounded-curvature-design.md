# Design Spec — Shared skeleton adjacency for bounded κ_R (kill the per-edge O(V+E))

**Goal:** make the bounded-curvature builds truly O(touched). Today
`detail::build_local_state` (aleph.flow-ollivier_ricci.cppm:186) rebuilds the
GLOBAL `g_index` (OrderedMap over all vertices) and `g_adj` (all skeleton
edges) on EVERY per-edge call — O(V+E) per edge — so:

- `build_laplacian_bounded` costs O(|E|·(V+E)) per full build;
- `build_laplacian_local` costs O(dirty·(V+E)) per edit — the measured
  t_local grows with |V| at constant dirty (bench_scaling grid 8→16:
  17.4→21.0 ms for dirty=37), leaking the global factor the localization
  is supposed to avoid.

Date 2026-07-03 · Status: approved (part of the paper-evaluation push).
Evidence: bench/bench_scaling.cpp smoke (grids 8–16).

## 1. Approach

Hoist the global index+adjacency into a shared, build-once structure and
thread it through the per-edge bounded primitives. **Byte-exactness is
preserved by construction**: the hoisted structures are built by the SAME
loops in the SAME order (g_index over skel.vertices; g_adj over skel.edges)
as the current per-call code; everything after them (ball BFS, canonical
sort, induced neighbours via neighbour_insert, local all-pairs BFS,
wasserstein_1) is untouched. Same inputs, same op order ⇒ bit-identical κ_R.

## 2. Components (all in aleph.flow:ollivier_ricci unless noted)

### 2.1 `SkeletonAdjacency` (EXPORTED)
```cpp
struct SkeletonAdjacency {
    aleph::containers::OrderedMap<NodeId, std::size_t> index;  // vertex -> dense idx
    std::vector<std::vector<std::size_t>>              adj;    // symmetric, dense idx
};
[[nodiscard]] SkeletonAdjacency build_adjacency(const OneSkeleton& skel);
```
Built once per skeleton: `index` inserted in `skel.vertices` order, `adj`
filled in `skel.edges` order — token-identical to the loops currently at the
top of `build_local_state` (and of `two_hop_touched_edges`).

### 2.2 `build_local_state(skel, shared, a, b, radius)` (detail overload)
Identical body to the current `build_local_state` from the ball-BFS on:
reads `shared.index` / `shared.adj` instead of building them. The existing
4-arg signature becomes a thin wrapper (`build_local_state(skel, build_adjacency(skel), …)`)
so any external behavior is unchanged.

### 2.3 `ricci_curvature_edge_bounded(skel, shared, a, b, radius)` (EXPORTED overload)
`ricci_curvature_edge(build_local_state(skel, shared, a, b, radius), a, b)`.
The existing detail 4-arg form stays (delegates via the wrapper).

### 2.4 Callers updated (aleph.flow:laplacian)
- `build_laplacian_bounded`: `const auto shared = build_adjacency(skel);`
  once, then the per-edge loop uses the overload.
- `build_laplacian_local`: same — one `build_adjacency`, shared by every
  dirty-edge recompute.
- `two_hop_touched_edges`: unchanged (it already builds its adjacency once
  per call; folding it into SkeletonAdjacency is possible but not load-bearing
  — out of scope).

## 3. Complexity
Full bounded build: O(V+E + |E|·ball_work). Localized edit:
O(V+E + dirty·ball_work + |E|) (the O(|E|) is the fresh-map assembly walk,
already there). The remaining O(V+E) terms are the unavoidable per-edit
skeleton/adjacency scans — linear, not quadratic.

## 4. Tests (tests/flow/test_shared_adjacency.cpp)
1. **Primitive ≡ builder:** on a 6×6 grid + an attached object, for EVERY
   skeleton edge: exported `ricci_curvature_edge_bounded(skel, shared, a, b)`
   == the κ stored in `build_laplacian_bounded(g).curvatures` — bitwise.
2. **Wrapper ≡ overload:** 4-arg `ricci_curvature_edge_bounded(skel, a, b)`
   (fresh adjacency each call) == shared-adjacency overload, bitwise, every edge.
3. **Tier-1 unchanged:** the whole existing mv_localization suite still passes
   (byte-exact local==full on the edit trace) — the real gate.

## 5. Non-goals
No change to the GLOBAL `ricci_curvature`/`build_state` path (its all-pairs
BFS + whole-component W₁ support is the *documented contrast case* in the
bench; localizing lowering::importance is a separate, later slice).

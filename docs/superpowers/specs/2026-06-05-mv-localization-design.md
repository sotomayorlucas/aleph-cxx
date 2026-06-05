# Design Spec — Mayer-Vietoris localization of Δ (physics slice 3)

**Goal:** after a DPO edit, recompute only the **touched region** of the shared Laplacian's Ollivier-Ricci curvature (instead of the unconditional full `build_laplacian(g_after)`), **certified byte-identical** to the full rebuild, and certified topologically by a Mayer-Vietoris cohomology check. The manifesto thesis: *DPO rewrites invalidate only the touched part — topologically-targeted, not brute-forced.* Date 2026-06-05 · Status: DRAFT (from the mapping+design workflow). **APIs that don't exist yet are `[NEW]`.**

## 0. The corrected premise (adversarial-map finding)

Ollivier-Ricci κ(a,b) is **NOT** a pure function of the 1-hop neighbourhoods of a,b. Its mass lives on the 1-hop stars, but its transport **cost** is the graph hop-distance among `N(a)∪N(b)` (`aleph.flow-ollivier_ricci.cppm:181-209`, sliced from the all-pairs BFS `st.dist`). That is a **2-hop** quantity (and global-geodesic in degenerate short-cycle topologies). So the sound invalidation rule is **2-hop**: recompute κ for every skeleton edge incident to a vertex within 2 hops of an inserted/removed Adjacent edge. The Tier-1 oracle (§3) catches any residual (degenerate-topology) case by diffing against the full rebuild.

## 1. Scope of THIS slice (and the deferred bottleneck)

**In:** localized **curvature recompute** — reuse cached κ for non-dirty edges, recompute the 2-hop-dirty edges, reassemble Δ in canonical order (byte-exact vs full). Wired into the controller's edit path with `g_before` capture + a 2-hop dirty derivation. Tier-1 + Tier-2 oracles. A `recompute_count` win counter.

**Deferred (honest, the real asymptotic win) — `[FOLLOW-UP]`:** the all-pairs BFS in `build_state` (`ollivier_ricci.cppm:121-141`) is O(n²) and rebuilt every call; this slice REUSES it (so total work stays O(n²), but the per-edge W₁ transport solves drop from O(N) to O(touched) — the demonstrable curvature-recompute win). The full asymptotic win needs a **bounded-radius BFS** for the 2-hop `sub_dist` instead of the global `build_state` — a subtle follow-up slice. This slice proves *correctness + topological targeting of the curvature*; the next proves *asymptotic cost*.

## 2. Architecture

### 2.1 `[NEW] detail::ricci_curvature_edge(st, a, b) -> f64` (`aleph.flow-ollivier_ricci.cppm`)
Factor the single-edge body out of the existing loop (`:168-211`): given the prebuilt `st` (from `build_state` over `g_after`'s skeleton), compute κ(a,b) via the IDENTICAL `uniform_on_neighbors` + `sub_dist` slice + `wasserstein_1` path. Reusing the identical code ⇒ recomputed κ is **bit-exact** to the full build's κ for that edge.

### 2.2 `[NEW] build_laplacian_local(g_after, prev, dirty_edges, weight_fn) -> WeightedLaplacian` (`aleph.flow-laplacian.cppm`)
```cpp
[[nodiscard]] WeightedLaplacian build_laplacian_local(
    const aleph::graph::Graph& g_after,
    const WeightedLaplacian&   prev,                                   // cached node_order/matrix/curvatures
    const std::vector<std::pair<NodeId,NodeId>>& dirty_edges,          // 2-hop touched (canonical-keyed)
    WeightFn weight_fn,
    int* recompute_count = nullptr);                                   // §5 win counter
```
Body: `skel = OneSkeleton::from_graph(g_after)` (deterministic sorted vertices/edges); `st = build_state(skel)` (REUSED full BFS this slice). Then **iterate `skel.edges` in canonical sorted order** and for each edge `e`: `κ[e] = dirty_set.contains(e) ? ricci_curvature_edge(st, e.a, e.b) (++*recompute_count) : *prev.curvatures.get(e)` (reuse cached; if absent — a never-before-seen survivor edge — recompute as a safe fallback). Assemble Δ via the existing `detail::assemble` over that SAME canonical order ⇒ identical fp summation ⇒ `local.matrix == full.matrix` **bit-for-bit**.

### 2.3 `[NEW] two_hop_touched_edges(skel, seed) -> vector<pair<NodeId,NodeId>>` (`aleph.flow`)
`ball2 = BFS(skel, seed, radius=2)`; `dirty = { e ∈ skel.edges : e.a ∈ ball2 ∨ e.b ∈ ball2 }`. `seed` = mesh nodes whose incident structure changed (R's nodes ∪ endpoints of created/deleted Adjacent edges). Deterministic.

### 2.4 Controller wiring (`aleph.edit-controller.cppm`)
- `[NEW]` member `aleph::graph::Graph prev_graph_;`; capture `prev_graph_ = graph_;` immediately BEFORE `apply_op(graph_, op)` (needed: deleted-edge endpoints are gone from `graph_`; `decompose_rewrite` needs both graphs).
- In `rebuild_operator_and_reproject` (gated by the existing `topo_changed`): derive `preserved = nodes(graph_) \ rec.created_nodes`; `(U,K,R) = decompose_rewrite(prev_graph_, graph_, preserved)`; `seed` from R + created/deleted-edge endpoints; `dirty = two_hop_touched_edges(skel_after, seed)`; `operator_ = build_laplacian_local(graph_, operator_, dirty, default_weight)`. **Fallback to full `build_laplacian`** when `dirty.size() > kLocalFraction * skel.edges.size()` (large/global edits) or `ApplyRule` (arbitrary rewrites) — gate it.
- `u_`/`v_` reproject unchanged (already correct) — or R-scoped as a minor optimization (optional this slice).

## 3. Correctness oracle (LOAD-BEARING — the adversarial gate)
**Tier-1 (every test, the hard gate):** for an arbitrary rewrite trace, `build_laplacian_local` MUST equal `build_laplacian(g_after)`:
```cpp
WeightedLaplacian full  = build_laplacian(g_after, default_weight);
WeightedLaplacian local = build_laplacian_local(g_after, prev, dirty, default_weight);
REQUIRE(local.node_order == full.node_order);                  // pure fn of id set
CHECK(local.matrix.approx_eq(full.matrix, 1e-12));             // (assemble in canonical order ⇒ exact)
for (auto& [e, kf] : full.curvatures) { auto* kl = local.curvatures.get(e); REQUIRE(kl); CHECK(std::abs(*kl-kf) <= 1e-12); }
CHECK(local.curvatures.size() == full.curvatures.size());
```
Run this over a SEQUENCE of edits (add several nodes, delete some) on a lattice — if the 2-hop rule ever under-approximates, Tier-1 FAILS (and the implementer widens the radius or falls back; do NOT loosen the tolerance to hide it).
**Tier-2 (tests/CI only, O(N)):** `auto [u,k,r] = decompose_rewrite(prev_graph_, g_after, preserved); CHECK(mayer_vietoris_certify_with(g_after, u, r, k, SheafKind::Visibility).residual == 0);` — certifies the cover is faithful (the dirty seed didn't drop a connectivity-changing node).

## 4. Determinism
`OneSkeleton::from_graph` sorts vertices/edges; the dirty set is a deterministic BFS; cached-vs-recompute selection iterates the sorted edge list; assembly is canonical-order. Same `(g_after, prev, dirty)` ⇒ bit-identical Δ. The `--wave` byte-identity contract is unaffected (the wave's Δ comes from this path; localized==full means identical Δ ⇒ identical wave).

## 5. Demonstrable win
On an M×M mesh lattice, a single `AddObject` (1 node + 2 Adjacent edges to an interior node): log `recompute_count` — full = all `|E|=O(M²)` edges; local = the 2-hop ball ≈ O(1) edges (degree-bounded). Assert `recompute_count == dirty.size()` and `recompute_count < full_edge_count`. The log line is the win; the Tier-1 `approx_eq` + Tier-2 `residual==0` ride alongside as the correctness gate. (Honest: wall-clock still O(n²) until the `[FOLLOW-UP]` bounded-BFS; the *curvature* recompute is already O(touched).)

## 6. Risks
1. **2-hop under-approximation in degenerate topologies (correctness).** Caught by Tier-1 (diffs vs full). Sound for locally-Euclidean meshes; falls-loud (test fails) elsewhere → widen radius or fall back. Document it.
2. **fp assembly order (byte-equality).** Mitigated by assembling in canonical sorted `skel.edges` order (§2.2) so summation order == full ⇒ exact.
3. **Deleted-edge endpoints need `g_before`** — the `prev_graph_` capture is mandatory.
4. **`RewriteRecord` misses survivor interface nodes that only gained an edge** — seed from `decompose_rewrite`'s R (includes `preserved`) + created/deleted-edge endpoints, not just `created_nodes`.
5. **Cache key validity:** κ keyed by `(NodeId,NodeId)`; survivor node-ids are stable across `apply_op` (assert in Tier-1). A cache miss → safe recompute.

## 7. Scope boundary (YAGNI)
**In:** localized curvature recompute (reuse + 2-hop recompute), canonical-order byte-exact assembly, controller wiring (g_before, dirty, fallback gate), Tier-1 + Tier-2 oracles, win counter. **Out (hooks kept):** the bounded-radius BFS for the asymptotic win (`[FOLLOW-UP]` — the headline O(touched) wall-clock); incremental MV certificate (currently O(N) — CI/audit only); localizing `IncrementalLaplacian::apply_rewrite` (test-only, not the editor path); R-scoped `reproject` (minor opt). `kLocalFraction`/radius are `constexpr`.

# Bounded-support curvature + exact MV localization — Plan (physics slice 3, revised)

> TDD. **Spec:** `docs/superpowers/specs/2026-06-05-mv-localization-design.md`. The HARD gate is **Tier-1 byte-EXACT**: localized Δ == full `build_laplacian_bounded` Δ bit-for-bit — now ACHIEVABLE because κ_R is local. Builds on the committed Task-1 primitive (`1a59ae0`: `ricci_curvature_edge`, `build_laplacian_local`, `two_hop_touched_edges`). Branch `mv-localization` (checked out).

**Conventions:** `cmake --build build-release && ctest --test-dir build-release`; one case `--test-case`; strict `grep -c "warning:"` → 0. No exceptions/RTTI.

**Key existing code (read):** `aleph.flow-ollivier_ricci.cppm` — `detail::SkeletonState` (fields: `neighbors`, `dist` all-pairs DMatrix, node index map), `detail::build_state(skel)` (global all-pairs BFS), `detail::ricci_curvature_edge(st, a, b)` (per-edge κ from a SkeletonState — committed Task-1), `detail::node_index_of`, `uniform_on_neighbors`, `wasserstein_1`, `support` slice. `aleph.flow-laplacian.cppm` — `detail::assemble(skel, RicciMap, wf)` (iterates RicciMap insertion order), `build_laplacian`, `build_laplacian_local` (committed Task-1). `OneSkeleton::from_graph` (sorted).

**RADIUS:** `constexpr int kCurvRadius = 2`. B₂(a,b) provably captures all support geodesics (i∈N(a),j∈N(b) ⇒ d(i,j)≤3 with intermediates ≤2 hops of {a,b}), so κ_{R=2} matches the global support geometry exactly (only the local `n`=ball size differs from global).

---

## Task 1: bounded-support curvature (the new core)

**Files:** `graph/src/aleph.flow/aleph.flow-ollivier_ricci.cppm`, `graph/src/aleph.flow/aleph.flow-laplacian.cppm`, `tests/flow/test_mv_localization.cpp` (extend).

- [ ] **Step 1 — `detail::build_local_state(skel, a, b, radius) -> SkeletonState`:** BFS from `{a,b}` over `skel`'s adjacency to `radius` hops → the local ball node set, SORTED (canonical, deterministic). Build a `SkeletonState` scoped to the ball: local `neighbors` (each ball node's neighbors that are in the ball — note: a ball node at the boundary may have neighbors outside; INCLUDE them in the support only if within `radius`+? — to keep κ well-defined use the induced subgraph on the ball, distances via BFS WITHIN the ball). `dist` = all-pairs BFS within the induced ball subgraph. `n` = ball size. This must produce a `SkeletonState` that `ricci_curvature_edge(local_st, a, b)` consumes identically.
  - Subtlety: `ricci_curvature_edge` uses `uniform_on_neighbors(ia, st.neighbors, n)` (μ over N(a)) and the `support` (finite-distance nodes). In the local state, N(a) must be a's TRUE 1-hop neighbours (all of them — they're within radius 2 ≥ 1), and the support is the ball. Ensure a's full neighbour set is captured (radius≥1 guarantees it). Distances among support nodes (≤3 hops) are exact within B₂.
- [ ] **Step 2 — `detail::ricci_curvature_edge_bounded(skel, a, b, radius)`** = `ricci_curvature_edge(build_local_state(skel, a, b, radius), a, b)`. (Reuses the committed per-edge fn with a LOCAL state ⇒ κ_R.)
- [ ] **Step 3 — `build_laplacian_bounded(g, weight_fn, radius=kCurvRadius)`** (exported, laplacian.cppm): `skel = from_graph(g)`; fresh `RicciMap` iterating `skel.edges` in order, `κ[e] = ricci_curvature_edge_bounded(skel, e.a, e.b, radius)`; `assemble(skel, curv, wf)`. (The global `build_laplacian`/`ricci_curvature` stay UNCHANGED for `lowering::importance`.)
- [ ] **Step 4 — tests** (extend `test_mv_localization.cpp`): (a) **determinism** — `build_laplacian_bounded(g)` twice → byte-identical matrix; (b) **sanity** — `is_symmetric(1e-12)`, `ones_in_kernel(1e-12)` hold (Δ is still a valid graph Laplacian); (c) **locality fidelity** — on a large grid, an interior edge's `ricci_curvature_edge_bounded(skel,a,b,2)` ≈ the global `ricci_curvature_edge` within ~1e-6 (R=2 captures the local geometry; only the perturbation-`n` differs). 
- [ ] **Step 5 — build + run** `--test-case="mv-local*"` + full ctest + strict 0. **Commit** `feat(flow): bounded-support Ollivier-Ricci curvature (build_laplacian_bounded) — local operator`.

---

## Task 2: exact localization on the bounded operator (Tier-1 byte-exact)

**Files:** `graph/src/aleph.flow/aleph.flow-laplacian.cppm` (rework `build_laplacian_local`), `tests/flow/test_mv_localization.cpp`.

- [ ] **Step 1 — rework `build_laplacian_local`** to use BOUNDED κ: per edge, `κ[e] = dirty.contains(e) ? ricci_curvature_edge_bounded(skel_after, e.a, e.b, kCurvRadius) (++count) : prev.curvatures.get(e)` (cached). Fresh RicciMap in `skel.edges` order → `assemble`. (The cached κ are bounded κ_R from the previous `build_laplacian_bounded`/`build_laplacian_local`.) NO global `build_state` — each recompute builds its own local state (the asymptotic win).
- [ ] **Step 2 — dirty derivation:** `dirty = two_hop_touched_edges(skel_after, seed)` with the BFS radius = `kCurvRadius` (an edit changes κ_R(e) iff within R hops of e). `seed` = changed mesh nodes ∪ endpoints of created/deleted edges.
- [ ] **Step 3 — Tier-1 byte-EXACT test:** g0=grid; `prev = build_laplacian_bounded(g0)`. Edit→g1 (add node + edges). `full = build_laplacian_bounded(g1)`; `dirty = two_hop_touched_edges(skel(g1), seed)`; `int rc=0; local = build_laplacian_local(g1, prev, dirty, default_weight, &rc)`. Assert:
  - `local.node_order == full.node_order`;
  - `local.matrix.at(i,j) == full.matrix.at(i,j)` for ALL i,j — **`==`, bit-exact** (now holds: κ_R is local, non-dirty edges' cached κ_R == full κ_R exactly, same local `n`);
  - every curvature matches exactly; `rc == dirty.size() < |E|`.
  - **Multi-edit trace** (several adds + a delete) threading `prev` → exact at every step.
- [ ] **Step 4 — build + run.** If byte-exact STILL fails: the local-state node ordering or `n` differs between cached and recomputed for a non-dirty edge — make `build_local_state` fully deterministic (sorted ball, induced-subgraph distances independent of the rest of the graph). This MUST pass now; if not, report BLOCKED with the exact mismatching edge. **Commit** `feat(flow): exact MV localization on bounded curvature — Tier-1 byte-identical`.

---

## Task 3: controller wiring + new --wave baseline + win

**Files:** `bridge/src/aleph.edit/aleph.edit-controller.cppm`, tests, `apps/aleph_edit/main.cpp` (no change expected).

- [ ] **Step 1 — operator_ → bounded:** `enable_sim` and the first build use `aleph::flow::build_laplacian_bounded(graph_, default_weight)` (NOT the global `build_laplacian`). This changes the wave's Δ → a NEW deterministic `--wave` baseline (expected; recapture it).
- [ ] **Step 2 — `prev_graph_` capture** (member; set before `apply_op`). In `rebuild_operator_and_reproject` (gated by `topo_changed`): derive `preserved`; `decompose_rewrite(prev_graph_, graph_, preserved)` → seed from R + created/deleted-edge endpoints; `dirty = two_hop_touched_edges(skel_after, seed)`; if `dirty.size() <= kLocalFraction*|E|` and the op is localizable → `operator_ = build_laplacian_local(graph_, operator_, dirty, default_weight, &recompute_count_)`, else `build_laplacian_bounded(graph_, …)` (fallback). `u_/v_` reproject as today.
- [ ] **Step 3 — tests** (`test_mv_controller.cpp`): after an `AddObject`, controller `operator_.matrix` == `build_laplacian_bounded(controller graph)` byte-exact (localized correctly); MV Tier-2 `mayer_vietoris_certify_with(graph, u, r, k, Visibility).residual == 0`.
- [ ] **Step 4 — --wave determinism + win:** capture the NEW `--wave` baseline (`/tmp/base`), run again (`/tmp/after`), `diff -rq` EMPTY (run-to-run determinism on the localized path, incl. the mid-run DeleteObject). Log `recompute_count` per edit showing O(touched) ≪ |E|. ctest + strict 0. **Commit** `feat(sim): controller on bounded localized Δ — wave deterministic, MV cert closes, O(touched) recompute`.

---

## Final verification
- [ ] Tier-1 byte-EXACT (localized == full bounded) on single + multi-edit traces.
- [ ] Tier-2 MV `residual==0`; ctest all pass; strict 0.
- [ ] `--wave` run-to-run byte-identical (new bounded baseline); `recompute_count` O(touched) ≪ |E|.

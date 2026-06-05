# Mayer-Vietoris Localization — Implementation Plan (physics slice 3)

> TDD. **Spec:** `docs/superpowers/specs/2026-06-05-mv-localization-design.md`. The HARD gate is **Tier-1**: the localized Δ must EQUAL the full `build_laplacian(g_after)` Δ bit-for-bit on a multi-edit trace. If you can't make Tier-1 pass, report BLOCKED — do NOT loosen the tolerance.

**Conventions:** `cmake --build build-release && ctest --test-dir build-release`; one case `--test-case="<name>"`; strict `grep -c "warning:"` → 0. No exceptions/RTTI.

**Read first:** `graph/src/aleph.flow/aleph.flow-ollivier_ricci.cppm` (esp. `build_state` ~:100-141, the per-edge loop `ricci_curvature_from_skeleton` :159-214 — `detail::SkeletonState st`, `uniform_on_neighbors`, the `sub_dist` slice, `wasserstein_1`), and `aleph.flow-laplacian.cppm` (`WeightedLaplacian` :71 with `node_order`/`matrix`/`curvatures`; `assemble(skel, RicciMap, weight_fn)` :114 which **iterates `curvatures` in insertion order**; `build_laplacian` :149; `default_weight`). **Byte-equality hinges on building the localized `curvatures` map by inserting in the SAME `skel.edges` order the full build uses, so `assemble`'s iteration order — hence the diagonal `+=` summation order — is identical.** `OneSkeleton::from_graph` sorts vertices+edges (deterministic).

---

## Task 1: localized curvature primitive + Tier-1 (the core + hard gate)

**Files:** `graph/src/aleph.flow/aleph.flow-ollivier_ricci.cppm` (factor `ricci_curvature_edge`), `graph/src/aleph.flow/aleph.flow-laplacian.cppm` (`build_laplacian_local`, `two_hop_touched_edges`), `tests/flow/test_mv_localization.cpp` (new; add to `tests/CMakeLists.txt`).

- [ ] **Step 1 — factor `detail::ricci_curvature_edge(const SkeletonState& st, NodeId a, NodeId b) -> f64`** out of the `ricci_curvature_from_skeleton` loop body (lines ~168-211): everything inside `for (const auto& [a,b] : skel.edges) { ... }` that computes one edge's κ (resolve ia/ib indices from `st`, build `mu`, slice `sub_dist`, call `wasserstein_1`, return κ). Then rewrite the loop to `out.insert({a,b}, ricci_curvature_edge(st, a, b));` — proving the factor is behavior-preserving (existing `test_ollivier_ricci` + `test_laplacian` must still pass byte-identically).

- [ ] **Step 2 — `two_hop_touched_edges`** (free fn in `aleph.flow`, e.g. in laplacian.cppm or ollivier_ricci.cppm):
```cpp
// 2-hop closure: every skeleton edge incident to a vertex within 2 hops of `seed`.
[[nodiscard]] inline std::vector<std::pair<NodeId,NodeId>>
two_hop_touched_edges(const OneSkeleton& skel, const std::vector<NodeId>& seed) {
    // build adjacency from skel.edges; BFS radius 2 from each seed node into a set ball2;
    // return all skel.edges with either endpoint in ball2, in skel.edges order.
}
```
(Deterministic; use the skeleton's sorted edges. Edge keys are canonical `(min,max)` as `from_graph` produces.)

- [ ] **Step 3 — `build_laplacian_local`** (laplacian.cppm):
```cpp
[[nodiscard]] inline WeightedLaplacian build_laplacian_local(
    const aleph::graph::Graph& g_after, const WeightedLaplacian& prev,
    const std::vector<std::pair<NodeId,NodeId>>& dirty_edges,
    WeightFn weight_fn, int* recompute_count = nullptr) {
    const OneSkeleton skel = OneSkeleton::from_graph(g_after);
    const detail::SkeletonState st = detail::build_state(skel);   // full BFS reused this slice
    // dirty as a set for O(1) lookup (canonical edge key)
    RicciMap curv;                                                // FRESH, inserted in skel.edges order
    for (const auto& [a, b] : skel.edges) {
        f64 k;
        if (/* (a,b) in dirty_edges */) { k = detail::ricci_curvature_edge(st, a, b); if (recompute_count) ++*recompute_count; }
        else { const f64* cached = prev.curvatures.get({a, b}); k = cached ? *cached : detail::ricci_curvature_edge(st, a, b); }
        curv.insert({a, b}, k);
    }
    return detail::assemble(skel, std::move(curv), weight_fn);    // canonical order ⇒ byte-equal to full
}
```
(Confirm `RicciMap::insert`/`get`, `OrderedMap` API, and `assemble`'s signature against the real code; adapt names. The cached-miss → recompute is the safe fallback.)

- [ ] **Step 4 — Tier-1 test** `tests/flow/test_mv_localization.cpp`:
```cpp
#include "doctest.h"
#include <string>
#include <vector>
import aleph.flow; import aleph.graph; import aleph.types; import aleph.math;
using namespace aleph::types; using aleph::graph::Graph;
namespace { /* make_grid(R) -> Graph of R×R Mesh nodes + 4-neighbour Adjacent edges (mirror tests/flow patterns); return ids */ }

TEST_CASE("mv-local: localized Δ == full build_laplacian, bit-for-bit, over an edit") {
    // g0 = R×R grid; full0 = build_laplacian(g0).
    // Edit: add 1 Mesh node C adjacent to an interior node + its Adjacent edges -> g1.
    // full1 = build_laplacian(g1, default_weight);
    // seed = {C} ∪ endpoints of the new Adjacent edges; dirty = two_hop_touched_edges(skel(g1), seed);
    // int rc=0; local1 = build_laplacian_local(g1, full0, dirty, default_weight, &rc);
    REQUIRE(local1.node_order == full1.node_order);
    CHECK(local1.matrix.approx_eq(full1.matrix, 1e-12));               // exact in practice
    for (auto& [e, kf] : full1.curvatures) { auto* kl=local1.curvatures.get(e); REQUIRE(kl); CHECK(std::abs(*kl-kf)<=1e-12); }
    CHECK(local1.curvatures.size()==full1.curvatures.size());
    CHECK(rc == (int)dirty.size());
    CHECK(rc < (int)full1.curvatures.size());                          // the win: O(touched) < O(N)
}
TEST_CASE("mv-local: byte-EXACT (not just approx) on the grid") {
    // same setup; assert local1.matrix.at(i,j) == full1.matrix.at(i,j) exactly for all i,j
    // (the canonical-order assembly makes the fp summation identical). If this fails, the
    // curvatures map insertion order differs — FIX the order, don't loosen.
}
TEST_CASE("mv-local: multi-edit trace stays exact") {
    // apply several adds (and a delete if feasible at flow level) in sequence, threading prev
    // through each; assert localized == full at every step. This stress-tests the 2-hop rule.
}
```
- [ ] **Step 5 — build + run** `--test-case="mv-local*"` → pass (esp. the byte-EXACT one). Existing `test_ollivier_ricci`/`test_laplacian` still pass. Full `ctest`; strict 0. **Commit** `feat(flow): localized curvature recompute (build_laplacian_local) — byte-identical to full, certified Tier-1`.

> If the byte-EXACT test fails but approx passes: the localized `curvatures` insertion order differs from full — ensure you iterate `skel.edges` (the SAME order `ricci_curvature_from_skeleton` uses) when building `curv`. If the multi-edit trace ever fails approx: the 2-hop rule under-approximated on that topology — report it (widen radius or note the degenerate case); do NOT loosen tolerance.

---

## Task 2: controller wiring + MV certificate + --wave guard

**Files:** `bridge/src/aleph.edit/aleph.edit-controller.cppm`, `tests/edit/test_sim_controller.cpp` (or a new `test_mv_controller.cpp`).

- [ ] **Step 1 — `g_before` capture:** add member `aleph::graph::Graph prev_graph_{};`; in `apply(Op)`, capture `prev_graph_ = graph_;` IMMEDIATELY before `apply_op(graph_, op)` (so it holds g_before incl. soon-to-be-deleted edges).
- [ ] **Step 2 — localized rebuild** in `rebuild_operator_and_reproject` (still gated by the existing `topo_changed`): import `aleph.sheaf` (for `decompose_rewrite`). Derive `preserved` (= current graph node ids minus the just-created ones — pass the RewriteRecord in, or recompute survivors). `auto [u,k,r] = decompose_rewrite(prev_graph_, graph_, preserved);` seed = R's mesh nodes ∪ endpoints of created/deleted Adjacent edges; `dirty = two_hop_touched_edges(OneSkeleton::from_graph(graph_), seed)`. Then:
```cpp
constexpr double kLocalFraction = 0.5;
const OneSkeleton skel = ...from_graph(graph_);
if (/* localizable op */ && dirty.size() <= kLocalFraction * skel.edges.size())
    operator_ = aleph::flow::build_laplacian_local(graph_, operator_, dirty, aleph::flow::default_weight);
else
    operator_ = aleph::flow::build_laplacian(graph_, aleph::flow::default_weight);   // fallback
u_.reproject(operator_.node_order); v_.reproject(operator_.node_order);
```
(rebuild_operator_and_reproject is also called from `enable_sim` indirectly? No — enable_sim builds fresh. Only the apply() path has a prev. On the FIRST build / enable_sim, use the full `build_laplacian`. Ensure `operator_` is valid before the first localized call.)
- [ ] **Step 2b — `rebuild_operator_and_reproject` needs the RewriteRecord/preserved** — thread it from `apply()` (which has `rec`). Adjust the helper signature to take the touched info, or inline the localized logic in `apply()`.
- [ ] **Step 3 — tests** (`tests/edit/test_mv_controller.cpp`, add to CMake): after an `AddObject` via the controller, the controller's `operator_.matrix` equals a fresh `build_laplacian(controller graph)` (the controller localized correctly); AND the MV Tier-2 certificate closes: `CHECK(mayer_vietoris_certify_with(graph, u, r, k, SheafKind::Visibility).residual == 0);`. (Expose a const accessor to `operator_` for the test if needed, or test via the public surface.)
- [ ] **Step 4 — --wave byte-identical guard:** the wave's Δ now comes (on edits) from the localized path. `mkdir -p /tmp/base /tmp/after`; capture `--wave /tmp/base` BEFORE wiring (or from `main`), then after; `diff -rq` must be EMPTY (localized==full ⇒ identical Δ ⇒ identical wave). The `--wave` demo does a DeleteObject mid-run — this exercises the localized DELETE path. If frames differ, the localized delete is wrong — STOP/report.
- [ ] **Step 5 — full `ctest` + strict 0 + the win:** add a `--wave`/headless log of `recompute_count` showing O(touched) ≪ |E| on the lattice (a printf in the localized path, or a controller accessor). **Commit** `feat(sim): controller localizes Δ on edit (MV cover + 2-hop) — --wave byte-identical, MV cert closes`.

---

## Final verification
- [ ] Tier-1 byte-EXACT (localized==full) on single + multi-edit traces; existing flow tests unchanged.
- [ ] Tier-2 MV `residual==0`. `ctest` all pass; strict 0.
- [ ] `--wave` byte-identical vs pre-wiring (localized delete correct).
- [ ] `recompute_count` log shows O(touched) ≪ |E| (the demonstrable targeting; wall-clock win deferred to the bounded-BFS follow-up).

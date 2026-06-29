# Design Spec — Localized Delta Hardening

**Date:** 2026-06-29  
**Status:** approved design, written for review  
**Scope:** C slice after serialization hardening

## 1. Goal

Close the remaining engine gap in the graph-derived simulation path: the
localized rebuild of the editor wave operator must be byte-exact, bounded in
work for local edits, and covered by verification that does not require
excluding the long `mv-controller: DeleteObject` test.

The engine thesis stays unchanged:

```text
Graph (truth) -> Op/apply_op -> derived LoweredScene/Delta -> pixels/sim state
```

No renderer or simulator state becomes authoritative. This slice only hardens
how `EditorController` derives its bounded-support Laplacian `Delta` after a
structural graph edit.

## 2. Trigger

Serialization hardening verification found no assertion failure in the C area,
but the full test suite had to exclude:

```text
mv-controller: DeleteObject (interior lattice node) -> byte-exact + O(touched) recompute
```

That case exercises exactly the intended C surface: a graph edit deletes an
interior lattice node, the controller uses a Mayer-Vietoris-localized dirty
cover, and `aleph.flow` rebuilds the bounded-support curvature Laplacian from
cached and recomputed edge curvatures.

A long-running test here means the claimed local recompute is not yet a reliable
engine contract, even if the math is correct.

## 3. Non-Goals

- No new visual editor workflow. That is B.
- No new renderer feature or shader.
- No semantic graph state in `render.sw`, `render.rt`, or `aleph.scene`.
- No loosening byte-exact checks to hide drift.
- No replacing the bounded-support curvature model with global Ricci curvature.

## 4. Architecture

### 4.1 Current data flow

`EditorController::apply` commits an `Op` to the graph, captures the rewrite
record, and for structural localizable edits calls
`rebuild_operator_localized`.

The localized path:

1. Builds the preserved interface from `prev_graph_` and `graph_`.
2. Uses `decompose_rewrite` for the Mayer-Vietoris cover.
3. Seeds the dirty region from newly-created mesh vertices and created/deleted
   `Adjacent` endpoints.
4. Expands that seed with `two_hop_touched_edges`.
5. Calls `build_laplacian_local`.
6. Reprojects `u` and `v` onto the new `node_order`.

The intended correctness oracle remains:

```text
build_laplacian_local(graph_after, previous_delta, dirty)
== build_laplacian_bounded(graph_after)
```

with identical node order, curvature map keys, and matrix entries.

### 4.2 Investigation targets

The implementation plan must first measure where the long case spends time,
without changing semantics:

- dirty seed size
- two-hop dirty edge count
- fallback/full-rebuild decision
- bounded curvature recompute count
- time in `decompose_rewrite`
- time in `two_hop_touched_edges`
- time in `build_laplacian_local`
- time in the final Mayer-Vietoris certificate used by the test

The result decides the smallest fix. Expected possibilities:

- The production localized rebuild is fine, but the test's full oracle or MV
  certificate dominates. In that case, keep the strong oracle on a smaller graph
  and add a separate larger cost-shape test that does not run the expensive
  certificate.
- The dirty set is too large for an interior delete. In that case, fix the seed
  or radius usage so local edits remain local, then preserve the byte-exact
  oracle.
- The local path recomputes too many curvatures because of cache misses or key
  drift. In that case, repair cache-key continuity and add a regression test for
  survivor edge cache reuse.
- The fallback threshold chooses the wrong path. In that case, make the fallback
  policy explicit and tested.

## 5. Behavioral Contract

For a local structural edit over a lattice-like graph:

- `last_recompute_count() > 0`.
- `last_recompute_count() < edge_count_before`.
- Localized `Delta` is byte-exact to a fresh full bounded rebuild.
- The field sections preserve survivor values through `Section::reproject`.
- The graph remains the single truth; all derived state is disposable.

For non-local or too-large edits:

- The controller may fall back to a full bounded rebuild.
- The fallback must be observable as `last_recompute_count() == 0`.
- The resulting `Delta` must still match the full bounded rebuild.

## 6. Tests

### 6.1 Existing test repair

Keep the existing delete-object test meaningful, but make it suitable for the
normal suite. It must still prove:

- interior delete succeeds
- localized result is byte-exact to full bounded rebuild
- recompute count is non-zero and less than the prior edge count
- Mayer-Vietoris residual is zero, if the chosen graph size keeps that check
  practical

If the MV certificate is the only expensive component, split it:

- a small byte-exact + MV test
- a larger cost-shape test without the full MV certificate

### 6.2 New regression coverage

Add focused tests only where investigation proves a gap:

- dirty seed regression for deleted `Adjacent` endpoints
- cache-hit regression for survivor edges
- fallback-threshold regression for broad edits
- timing-independent guard that recompute count scales with touched edges, not
  total edges

The tests should not depend on wall-clock thresholds. They should assert counts,
paths, and byte-exact output.

## 7. Verification

Acceptance requires:

- targeted `mv-controller` tests pass without manual interruption
- `graph serialization*` still passes
- `lowering*` still passes
- `regression_*` still passes
- full `aleph_tests` passes without excluding the localized delete case, or the
  only remaining exclusion is documented as unrelated to this slice
- non-`aleph_tests` CTest isolation tests pass

## 8. Risks

- The long runtime may live entirely in a test-only oracle. The fix should avoid
  weakening production semantics just to speed up a certificate.
- Changing the dirty set can silently break byte-exactness. Every semantic change
  must be paired with a full bounded comparison.
- Existing dirty worktree changes touch editor/lowering files. Implementation
  must stage only C-slice hunks and leave unrelated user changes intact.

## 9. Success Criterion

C-1 is complete when the localized wave-operator path is diagnosable, bounded for
local edits by structural counts, byte-exact against the full bounded operator,
and no longer blocks the full test suite.

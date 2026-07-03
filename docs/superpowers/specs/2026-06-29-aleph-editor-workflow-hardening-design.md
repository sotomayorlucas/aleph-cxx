# Design Spec - Editor Workflow Hardening

**Date:** 2026-06-29
**Status:** approved design, written for review
**Scope:** B slice after serialization hardening and localized Delta hardening

## 1. Goal

Turn the current editor workflow draft into a tested graph-first workflow:

```text
CLI/key gesture -> Op or graph I/O -> Graph truth -> lower_incremental/full lower -> render/sim products
```

This slice makes project I/O, OBJ import, undo/redo, and the first DPO editor
gesture practical without letting the renderer, software rasterizer, or simulator
own semantic state.

## 2. Existing Draft To Adopt

The worktree already contains an uncommitted B draft in:

- `apps/aleph_edit/main.cpp`
- `apps/aleph_edit/CMakeLists.txt`
- `bridge/src/aleph.edit/aleph.edit-controller.cppm`
- `bridge/src/aleph.lowering/aleph.lowering-ops.cppm`
- `bridge/src/aleph.lowering/CMakeLists.txt`

The implementation phase should preserve useful pieces of that draft, but it
must not assume the draft is correct. Tests define the accepted behavior. Each
commit must stage only B-slice hunks and must not revert unrelated worktree
changes.

## 3. Scope

In scope:

- `ImportObj` as a transactional `aleph.lowering::Op`.
- CLI `--load <file.aleph>`, `--save <file.aleph>`, and `--import <file.obj>`.
- Live editor key bindings:
  - `u` undo
  - `y` redo
  - `r` apply `refine_cell`
  - `s` save to the configured `--save` path
- Headless import/save smoke coverage.
- Controller-level undo/redo tests.
- Save/load round-trip through the `aleph-graph/1` format hardened in A.

Out of scope:

- Multi-select, outliner, gizmos, asset browser, material inspector overhaul.
- A new UI layout.
- Renderer-side OBJ ownership.
- Optimizing global Ricci/importance cost.
- Making `ApplyRule` localized in lowering or wave Delta; it remains a full
  fallback where existing contracts require it.

## 4. Architecture

### 4.1 ImportObj op

`ImportObj` lives in `aleph.lowering:ops` beside `AddObject`, `AddLight`,
`DeleteObject`, and `ApplyRule`.

Input:

- `parent`: existing `Transform` node.
- `obj_bytes`: raw OBJ bytes.
- `material`: normalized `MaterialParams`.

Behavior:

1. Validate `parent` exists and is a `Transform`.
2. Parse OBJ through `aleph.io::load_obj`.
3. Reject invalid OBJ, empty triangle lists, and meshes above a fixed triangle
   cap.
4. Commit atomically through the existing structural transaction pattern:
   - clone existing graph into the post-state;
   - create a group `Transform` under `parent`;
   - create one shared `Material`;
   - create one `Mesh` per OBJ triangle using `TriLocal`;
   - add `Contains` and `References` edges;
   - return a complete `RewriteRecord`.

Failure returns a structured `OpError` and leaves the input graph unchanged.

### 4.2 Controller undo/redo

`EditorController` owns undo/redo because it already owns the graph truth and
all derived state.

Rules:

- A successful `apply()` pushes the previous graph snapshot onto undo.
- A failed `apply()` changes neither graph nor history.
- `undo()` restores the prior graph, pushes the current graph onto redo, clears
  selection, and fully rederives lowering, render backends, and sim Delta/fields
  if sim is enabled.
- `redo()` mirrors undo.
- Any new successful `apply()` clears redo.
- History is capped at 64 graph snapshots.

Undo/redo restores graph truth, not renderer caches. Caches are rebuilt from the
restored graph.

### 4.3 CLI and live shell

`apps/aleph_edit` remains a thin shell:

- `--load` loads a serialized graph before constructing the controller.
- `--save` writes the current graph on `s` and on clean live exit; headless saves
  when requested.
- `--import` starts from a minimal graph when no `--load` path is provided, then
  applies `ImportObj` under the scene root.
- Import framing computes a bounding box from the parsed OBJ and adjusts the
  orbit camera; this is UI convenience only, not graph truth.
- `--wave` keeps its deterministic lattice path unchanged.

Invalid arguments and failed I/O should fail loudly with stderr messages and
non-zero exit codes where the mode cannot proceed.

## 5. Tests

### 5.1 Lowering op tests

Add focused tests for `ImportObj`:

- valid one-triangle OBJ creates a group transform, shared material, mesh, and
  valid lowered `TriLocal` entity;
- invalid OBJ returns `OpError::InvariantViolation` and leaves graph unchanged;
- missing or non-transform parent returns the existing structured parent errors;
- imported material preserves `MaterialParams`, including `uv_scale`.

### 5.2 Controller tests

Add controller-level tests:

- successful `apply(AddObject)` can be undone and redone, with lowered state
  matching fresh `lower(graph)` after each step;
- redo is cleared after undo followed by a new successful `apply`;
- failed `apply` does not push undo history;
- undo/redo with sim enabled reprojects/rebuilds the wave operator coherently.

### 5.3 App/headless tests or smoke

Prefer automated coverage where the current test harness allows it:

- save a graph, load it, and verify entity counts / lowered identity;
- import a tiny OBJ in headless mode and verify output artifacts are written;
- save after import and load the saved graph.

If direct app execution is too slow or requires SDL in a way CI cannot support,
keep the shell changes thin and cover behavior through `ImportObj`, graph
serialization, and `EditorController` tests.

## 6. Verification

Acceptance requires:

- `cmake --build build --target aleph_tests aleph_edit -j$(nproc)`
- targeted ImportObj/lowering tests pass
- targeted controller undo/redo tests pass
- `graph serialization*` passes
- `lowering*` passes
- `edit:*` and `mv-controller:*` pass
- full `aleph_tests` passes
- `ctest --test-dir build -E '^aleph_tests$' --output-on-failure` passes

Manual smoke, when SDL is available:

```bash
./build/apps/aleph_edit/aleph_edit --import tiny.obj --save /tmp/tiny.aleph
./build/apps/aleph_edit/aleph_edit --load /tmp/tiny.aleph
```

## 7. Risks

- The draft may already include behavior without tests. Implementation must
  convert it into small test-backed commits instead of committing it wholesale.
- OBJ import can create many graph nodes. Keep the triangle cap explicit and
  tested.
- Undo/redo stores full graph snapshots. That is acceptable for this B slice, but
  future large-scene work may need compact history records.
- `ApplyRule` from the live editor can alter topology broadly. It should rely on
  existing full fallback contracts, not pretend to be localized.
- Save/load must use the hardened A parser; no alternate project format.

## 8. Success Criterion

B is complete when users can start the editor from a saved graph or OBJ, make
graph edits, undo/redo them, save the result, and reload it, with automated tests
showing that the graph remains the only semantic truth and all render/sim state
is rederived from it.

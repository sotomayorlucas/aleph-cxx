# Aleph Serialization Hardening

**Date:** 2026-06-29
**Status:** approved for planning
**Scope order:** A first, then C, then B

## Context

Aleph's core contract is that the typed graph is the single source of truth and
everything downstream is derived. The current persistence work moves in the
right direction: saved projects and OBJ import enter the graph instead of
becoming renderer-owned state. The weak point is the external-input boundary.
`load_graph_string()` currently calls the fatal `Graph::insert_node()` path, so
a malformed `.aleph` file can abort the process instead of returning a
structured `SerializationError`.

This phase hardens graph persistence before further simulation/render work or
editor workflow polish. The aim is not a new file format; it is to make the
existing `aleph-graph/1` format strict, deterministic, and safe to load.

## Goals

- Loading a `.aleph` string or file must return `std::expected`, never abort,
  for malformed user input.
- Parser behavior must be strict enough that invalid tokens, missing headers,
  duplicate node IDs, invalid edges, missing roots, and trailing garbage are
  rejected deterministically.
- Graph allocator watermarks must advance from loaded IDs without O(max_id)
  draining loops.
- Material semantics must survive op projection: `MaterialParams::uv_scale`
  must be copied into fresh `Material` nodes.
- Tests must cover invalid-input behavior and the allocator after load, not only
  happy-path round trips.

## Non-Goals

- No redesign of the `aleph-graph/1` format.
- No binary persistence format.
- No broad `Graph` refactor beyond the minimal safe-insert and allocator support
  needed by loading and structural op snapshots.
- No changes to renderer or editor behavior except where they consume the
  hardened graph APIs.

## Design

### Graph API

Add a non-fatal node insertion surface:

```cpp
std::expected<void, GraphError> try_insert_node(types::Node node);
```

`try_insert_node()` returns an error on duplicate IDs. Existing trusted
construction code can keep using `insert_node()`, which remains fatal on
duplicates and delegates to the checked path internally.

Add an explicit allocator synchronization API:

```cpp
void sync_node_allocator_to_at_least(std::uint32_t next) noexcept;
void sync_edge_allocator_to_at_least(std::uint32_t next) noexcept;
```

The loaded graph scans max node and edge IDs once, then sets watermarks directly
to `max + 1` with overflow protection. This replaces loops that repeatedly call
`alloc_node_id()`.

### Serialization Parser

`load_graph_string()` must require the first non-empty, non-comment line to be
exactly `aleph-graph/1`. A missing or later header is `InvalidHeader`.

Token parsing must consume the full token. Values such as `12abc`, `1.0x`, and
empty quoted strings where a number is expected are rejected. Each parsed line
must be fully consumed after its expected fields, ignoring only whitespace.

Node loading uses `try_insert_node()`. Duplicate IDs become a structured
`SerializationError::InvalidNode` or `ParseError`; they must not reach
`std::abort()`.

Pending edges are still added after nodes are loaded. Invalid endpoints or edge
kind compatibility return `InvalidEdge`. The root must exist. Final
`validate_all()` remains the postcondition.

### Material Projection

`detail::material_from()` in `aleph.lowering:ops` must copy `uv_scale` from
`MaterialParams`. This preserves the graph-level material truth for fresh
materials produced by `AddObject` and `ImportObj`.

### Tests

Extend `tests/graph/test_graph_serialization.cpp` with:

- full attribute round trip for materials, lights, camera pose, transform matrix,
  and geometry payloads;
- duplicate node ID rejected without abort;
- missing header rejected;
- numeric trailing garbage rejected;
- line trailing garbage rejected;
- root pointing at a missing node rejected;
- invalid edge endpoint or incompatible edge rejected;
- allocator after load returns an ID greater than the largest loaded node ID.

Add a focused lowering/op regression for `uv_scale` projection if no existing
test observes it through `AddObject`.

## Error Handling

The public persistence boundary reports `SerializationError`. It does not print,
exit, throw, or abort. The CLI/editor shell remains responsible for turning that
error into user-facing stderr text and a non-zero exit code.

## Verification

Minimum verification for A:

```bash
cmake --build build --target aleph_tests -j$(nproc)
./build/tests/aleph_tests -tc="graph serialization*"
./build/tests/aleph_tests -tc="lowering*"
./build/tests/aleph_tests -tc="regression_*"
```

If the touched code changes CMake module dependencies, also run full
`./build/tests/aleph_tests`.

## Follow-On Order

After A is implemented and verified, continue with C: simulation/render
improvements that deepen the graph-derived mathematical engine. Then continue
with B: editor save/load/import/undo workflow polish on top of the hardened
persistence layer.

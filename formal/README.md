# TLA+ specs (aleph-cxx)

These are byte-identical copies of `aleph-engine/formal/{scene_graph,dpo_rules}.{tla,cfg}` and `check.sh`.
The aleph-cxx C++26 enums + invariant names + rule names must mirror them
exactly — verified by `tests/tla_cxx_sync.cpp`.

To run TLC against the specs:

```sh
TLA_TOOLS=/tmp/tla2tools.jar ./formal/check.sh
```

If `tla2tools.jar` is missing, `check.sh` prints a download hint and exits 1.
TLC is **not** required for the C++ build — it's a separate model-checking
step. The C++ side has its own `tla_cxx_sync` regression test that doesn't
need a JVM.

## Files

| File              | What it specifies                                       |
|-------------------|----------------------------------------------------------|
| `scene_graph.tla` | Typed graph G=(V,E,τ,α) + 10 well-formedness invariants |
| `scene_graph.cfg` | TLC config: `NodesMax=8`, `EdgesMax=12`, `MaxDegree=8`  |
| `dpo_rules.tla`   | 4 DPO rules (spawn_light, remove_object, replace_material, refine_cell); parsed by tla_cxx_sync in Sub-phase 4b |
| `dpo_rules.cfg`   | TLC config for DPO rule preservation checking            |
| `check.sh`        | One-liner that runs TLC on both .tla files               |

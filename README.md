# aleph-cxx

Cycle-tight C++26 engine: typed scene graph, DPO edits, deterministic lowering, software raster + path tracing.

## Build

Requirements: CMake 3.28+, Ninja (recommended), g++ with C++26 modules, SDL2 (optional, for GUI).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target aleph_tests aleph_edit -j$(nproc)
ctest --test-dir build --output-on-failure
```

AddressSanitizer preset (Ninja only):

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DALEPH_SANITIZE=address
cmake --build build-asan --target aleph_tests -j$(nproc)
./build-asan/tests/aleph_tests
```

## Run

| Target | Command |
|--------|---------|
| Full test suite | `./build/tests/aleph_tests` |
| Structural editor (GUI) | `./build/apps/aleph_edit/aleph_edit` |
| Software raster demo | `./build/apps/aleph_sw/aleph_sw` |
| Headless edit script | `./build/apps/aleph_edit/aleph_edit --headless /tmp/out` |
| Scaling bench (paper CSV) | `./build/bench/aleph_bench_scaling --reps 5 > docs/paper/data/scaling.csv` |

### Editor project I/O

```bash
# Save/load typed graph projects (aleph-graph/1 text format)
./build/apps/aleph_edit/aleph_edit --load scene.aleph --save scene.aleph

# Import Wavefront OBJ at startup (replaces the demo scene; frames the camera)
./build/apps/aleph_edit/aleph_edit --import model.obj
```

### Editor keys (live mode)

| Key | Action |
|-----|--------|
| `a` / `l` / `x` | Add object / light / delete selection |
| `u` / `y` | Undo / redo (64-step stack) |
| `r` | DPO `refine_cell` on first matching mesh |
| `s` | Save project (requires `--save path.aleph`) |
| drag / wheel | Orbit / zoom camera |

## CI

GitHub Actions (`.github/workflows/ci.yml`) on `main`: Ubuntu, Ninja, g++, SDL2, full `ctest`, plus `regression_*` subset.

## Architecture (short)

```
Graph (truth) → Op / apply_op → lower_incremental → {SceneRT, Scene} → pixels
```

The graph is the single source of truth; render products are derived and rebuilt after every edit.

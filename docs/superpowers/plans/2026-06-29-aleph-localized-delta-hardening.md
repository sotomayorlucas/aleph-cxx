# Aleph Localized Delta Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the controller's localized bounded-Laplacian delete path verifiable in the normal suite without excluding the long `mv-controller` case.

**Architecture:** Keep production semantics centered on `Graph -> Op -> derived Delta`. First diagnose the long test with temporary timing; then split controller tests into byte-exact/MV coverage, explicit fallback coverage, and a large localized recompute-count coverage that avoids the expensive Mayer-Vietoris certificate.

**Tech Stack:** C++26 modules, doctest, CMake/Ninja, `aleph.edit`, `aleph.flow`, `aleph.sheaf`, existing bounded-support Laplacian APIs.

---

## File Structure

- `tests/edit/test_mv_controller.cpp`
  - Owns controller-level coverage for bounded localized Delta after structural edits. Modify this file to split the long delete test into smaller, structural tests.
- `bridge/src/aleph.edit/aleph.edit-controller.cppm`
  - Owns the production localized rebuild path. Read and instrument temporarily only if the test split shows production work is still unexpectedly large.
- `graph/src/aleph.flow/aleph.flow-laplacian.cppm`
  - Owns dirty-edge expansion and local bounded Laplacian assembly. Do not change unless diagnostics prove the dirty set or recompute count is wrong.
- `docs/superpowers/specs/2026-06-29-aleph-localized-delta-hardening-design.md`
  - Approved design for this plan.

## Task 1: Reproduce And Locate The Long Delete Cost

**Files:**
- Inspect: `tests/edit/test_mv_controller.cpp`
- Inspect: `bridge/src/aleph.edit/aleph.edit-controller.cppm`
- Inspect: `graph/src/aleph.flow/aleph.flow-laplacian.cppm`

- [ ] **Step 1: Use the debugging skill before touching code**

Activate `superpowers:systematic-debugging` for this task because the symptom is a long-running test/performance bug. Follow its evidence-first loop.

- [ ] **Step 2: Build the current test binary**

Run:

```bash
cmake --build build --target aleph_tests -j$(nproc)
```

Expected: build succeeds.

- [ ] **Step 3: Reproduce the current long case**

Run:

```bash
timeout 120 ./build/tests/aleph_tests \
  -tc="mv-controller: DeleteObject (interior lattice node) -> byte-exact + O(touched) recompute" \
  --duration=true
```

Expected before this slice: either exit code `124` from `timeout`, or a runtime that is far larger than nearby controller tests. Record whether the test reaches assertion output or stalls silently.

- [ ] **Step 4: Add temporary phase timing in the delete test**

Temporarily add these includes near the top of `tests/edit/test_mv_controller.cpp`:

```cpp
#include <chrono>
#include <iostream>
```

Then add this helper inside the anonymous namespace:

```cpp
using Clock = std::chrono::steady_clock;

long long elapsed_ms(Clock::time_point start, Clock::time_point stop) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
}
```

Temporarily replace the body of the existing delete test with this instrumented body:

```cpp
    constexpr int R = 12;  // large enough that a local 2-hop ball is << |E|
    Lattice s = make_lattice(R);
    const NodeId victim = s.nodes[static_cast<std::size_t>((R / 2) * R + (R / 2))];

    aleph::edit::EditorController ctl{std::move(s.g)};
    ctl.set_viewport(64, 48);
    ctl.enable_sim(true);

    const std::size_t edges_before = ctl.wave_operator().curvatures.size();
    const Graph before = ctl.graph().clone();

    const auto t0 = Clock::now();
    auto r = ctl.apply(aleph::lowering::Op{aleph::lowering::DeleteObject{victim}});
    const auto t1 = Clock::now();
    REQUIRE(r.has_value());

    check_operator_is_full_bounded(ctl);
    const auto t2 = Clock::now();

    const int rc = ctl.last_recompute_count();
    CHECK(rc > 0);
    CHECK(rc < static_cast<int>(edges_before));
    const auto t3 = Clock::now();

    const aleph::containers::FlatSet<NodeId> preserved =
        preserved_of(before, ctl.graph());
    auto [u, k, rsub] = aleph::sheaf::decompose_rewrite(before, ctl.graph(), preserved);
    const auto t4 = Clock::now();
    const aleph::sheaf::MayerVietorisCertificate cert =
        aleph::sheaf::mayer_vietoris_certify_with(
            ctl.graph(), u, rsub, k, aleph::sheaf::SheafKind::Visibility);
    const auto t5 = Clock::now();
    CHECK(cert.residual == 0);

    std::cerr << "mv-delete timing:"
              << " apply=" << elapsed_ms(t0, t1)
              << " full_check=" << elapsed_ms(t1, t2)
              << " count_assert=" << elapsed_ms(t2, t3)
              << " decompose=" << elapsed_ms(t3, t4)
              << " mv_cert=" << elapsed_ms(t4, t5)
              << " rc=" << rc
              << " edges_before=" << edges_before
              << "\n";
```

- [ ] **Step 5: Run the instrumented case once**

Run:

```bash
timeout 120 ./build/tests/aleph_tests \
  -tc="mv-controller: DeleteObject (interior lattice node) -> byte-exact + O(touched) recompute" \
  --duration=true
```

Expected: timing output identifies the dominant phase. If `mv_cert` dominates, proceed with the test split in Task 2. If `apply` dominates, inspect `rebuild_operator_localized` and `build_laplacian_local` before changing assertions. If `full_check` dominates, keep full byte-exact checks on smaller graphs and leave large tests to count/path assertions.

- [ ] **Step 6: Remove all temporary timing code**

Before committing anything, remove:

```cpp
#include <chrono>
#include <iostream>
using Clock = std::chrono::steady_clock;
long long elapsed_ms(Clock::time_point start, Clock::time_point stop) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
}
```

Restore the delete test to a clean non-instrumented state before starting Task 2.

## Task 2: Split Delete Coverage Into MV, Fallback, And Localized Count Tests

**Files:**
- Modify: `tests/edit/test_mv_controller.cpp`

- [ ] **Step 1: Update the test file comment**

In `tests/edit/test_mv_controller.cpp`, replace the final delete bullet in the header comment:

```cpp
//   (3) a controller DeleteObject on a lattice INTERIOR node (which deletes its
//       Adjacent edges) ALSO produces the byte-identical bounded operator and
//       reports an O(touched) << |E| recompute count - the localized delete.
```

with:

```cpp
//   (3) controller DeleteObject is covered in three pieces:
//       small graph: byte-identical bounded operator + MV closes;
//       R=8 graph: dirty cover is too broad, so the full fallback is observable;
//       R=9 graph: dirty cover is local, recompute_count is the 60 touched edges.
```

- [ ] **Step 2: Replace the existing long delete test with three focused tests**

Delete the current `TEST_CASE("mv-controller: DeleteObject (interior lattice node) -> byte-exact + O(touched) recompute")` and replace it with:

```cpp
TEST_CASE("mv-controller: DeleteObject (small lattice) -> byte-exact + MV closes") {
    constexpr int R = 6;
    Lattice s = make_lattice(R);
    const NodeId victim = s.nodes[static_cast<std::size_t>((R / 2) * R + (R / 2))];

    aleph::edit::EditorController ctl{std::move(s.g)};
    ctl.set_viewport(64, 48);
    ctl.enable_sim(true);

    const Graph before = ctl.graph().clone();

    auto r = ctl.apply(aleph::lowering::Op{aleph::lowering::DeleteObject{victim}});
    REQUIRE(r.has_value());

    check_operator_is_full_bounded(ctl);

    const aleph::containers::FlatSet<NodeId> preserved =
        preserved_of(before, ctl.graph());
    auto [u, k, rsub] = aleph::sheaf::decompose_rewrite(before, ctl.graph(), preserved);
    const aleph::sheaf::MayerVietorisCertificate cert =
        aleph::sheaf::mayer_vietoris_certify_with(
            ctl.graph(), u, rsub, k, aleph::sheaf::SheafKind::Visibility);
    CHECK(cert.residual == 0);
}

TEST_CASE("mv-controller: DeleteObject (R=8 lattice) -> broad dirty cover uses full fallback") {
    constexpr int R = 8;
    Lattice s = make_lattice(R);
    const NodeId victim = s.nodes[static_cast<std::size_t>((R / 2) * R + (R / 2))];

    aleph::edit::EditorController ctl{std::move(s.g)};
    ctl.set_viewport(64, 48);
    ctl.enable_sim(true);

    auto r = ctl.apply(aleph::lowering::Op{aleph::lowering::DeleteObject{victim}});
    REQUIRE(r.has_value());

    check_operator_is_full_bounded(ctl);
    CHECK(ctl.last_recompute_count() == 0);
}

TEST_CASE("mv-controller: DeleteObject (R=9 lattice) -> byte-exact + 60-edge localized recompute") {
    constexpr int R = 9;
    Lattice s = make_lattice(R);
    const NodeId victim = s.nodes[static_cast<std::size_t>((R / 2) * R + (R / 2))];

    aleph::edit::EditorController ctl{std::move(s.g)};
    ctl.set_viewport(64, 48);
    ctl.enable_sim(true);

    const std::size_t edges_before = ctl.wave_operator().curvatures.size();
    CHECK(edges_before == 144);

    auto r = ctl.apply(aleph::lowering::Op{aleph::lowering::DeleteObject{victim}});
    REQUIRE(r.has_value());

    check_operator_is_full_bounded(ctl);

    const int rc = ctl.last_recompute_count();
    CHECK(rc == 60);
    CHECK(rc < static_cast<int>(edges_before / 2));
}
```

- [ ] **Step 3: Run only the edited controller tests**

Run:

```bash
cmake --build build --target aleph_tests -j$(nproc)
timeout 120 ./build/tests/aleph_tests -tc="mv-controller:*" --duration=true
```

Expected: all `mv-controller` tests pass within the timeout. The `R=8` case reports fallback with `last_recompute_count() == 0`; the `R=9` case reports localized recompute count `60`.

- [ ] **Step 4: Commit the test split**

Run:

```bash
git add tests/edit/test_mv_controller.cpp
git commit -m "test(edit): split localized delete delta coverage"
```

Expected: commit contains only `tests/edit/test_mv_controller.cpp`.

## Task 3: Repair Production Localized Rebuild Only If Counts Fail

**Files:**
- Modify if needed: `bridge/src/aleph.edit/aleph.edit-controller.cppm`
- Modify if needed: `graph/src/aleph.flow/aleph.flow-laplacian.cppm`
- Test: `tests/edit/test_mv_controller.cpp`

- [ ] **Step 1: Check whether Task 2 exposed a production bug**

If Task 2 Step 3 passes with `R=8 -> 0` and `R=9 -> 60`, skip this task. If either count fails, keep using `superpowers:systematic-debugging` and continue with the next steps.

- [ ] **Step 2: If R=8 does not fall back, inspect the threshold**

In `bridge/src/aleph.edit/aleph.edit-controller.cppm`, the fallback gate must stay:

```cpp
        constexpr double kLocalFraction = 0.5;
        recompute_count_ = 0;
        if (static_cast<double>(dirty.size())
            <= kLocalFraction * static_cast<double>(skel.edges.size())) {
            operator_ = aleph::flow::build_laplacian_local(
                graph_, operator_, dirty, aleph::flow::default_weight,
                &recompute_count_);
        } else {
            operator_ = aleph::flow::build_laplacian_bounded(
                graph_, aleph::flow::default_weight);
        }
```

If the code differs, restore this exact gate. Then run:

```bash
cmake --build build --target aleph_tests -j$(nproc)
./build/tests/aleph_tests -tc="mv-controller: DeleteObject (R=8 lattice)*"
```

Expected: pass, with `last_recompute_count() == 0`.

- [ ] **Step 3: If R=9 recompute count is not 60, inspect dirty seed formation**

In `bridge/src/aleph.edit/aleph.edit-controller.cppm`, the delete seed must include endpoints of deleted `Adjacent` edges from `prev_graph_`:

```cpp
        for (aleph::types::EdgeId eid : rec.deleted_edges) {
            if (const aleph::types::Edge* e = prev_graph_.edge(eid);
                e != nullptr && e->kind == aleph::types::EdgeKind::Adjacent) {
                seed.push_back(e->src);
                seed.push_back(e->dst);
            }
        }
```

If this block is missing or reads deleted edges from `graph_`, fix it to the code above. Then run:

```bash
cmake --build build --target aleph_tests -j$(nproc)
./build/tests/aleph_tests -tc="mv-controller: DeleteObject (R=9 lattice)*"
```

Expected: pass, with `last_recompute_count() == 60`.

- [ ] **Step 4: If R=9 still does not count 60, inspect dirty expansion**

In `graph/src/aleph.flow/aleph.flow-laplacian.cppm`, `two_hop_touched_edges` must return edges whose endpoint is in the radius-2 seed ball:

```cpp
    std::vector<std::pair<NodeId, NodeId>> dirty;
    for (const auto& [a, b] : skel.edges) {
        const std::size_t* ia = node_to_idx.get(a);
        const std::size_t* ib = node_to_idx.get(b);
        const bool ina = (ia != nullptr) && ball2[*ia];
        const bool inb = (ib != nullptr) && ball2[*ib];
        if (ina || inb) {
            dirty.emplace_back(a, b);
        }
    }
    return dirty;
```

If the code differs, restore this exact logic. Then run:

```bash
cmake --build build --target aleph_tests -j$(nproc)
./build/tests/aleph_tests -tc="mv-controller: DeleteObject (R=9 lattice)*"
```

Expected: pass, with `last_recompute_count() == 60`.

- [ ] **Step 5: Commit any production fix**

Only if Steps 2-4 changed production code, run:

```bash
git add bridge/src/aleph.edit/aleph.edit-controller.cppm \
        graph/src/aleph.flow/aleph.flow-laplacian.cppm \
        tests/edit/test_mv_controller.cpp
git commit -m "fix(edit): keep localized delta dirty cover bounded"
```

Expected: commit contains the minimal production fix plus the test that failed before it.

## Task 4: Verification Sweep

**Files:**
- No source changes expected.

- [ ] **Step 1: Build once**

Run:

```bash
cmake --build build --target aleph_tests -j$(nproc)
```

Expected: build succeeds.

- [ ] **Step 2: Run targeted C tests**

Run:

```bash
timeout 180 ./build/tests/aleph_tests -tc="mv-controller:*" --duration=true
```

Expected: all `mv-controller` tests pass without timeout.

- [ ] **Step 3: Re-run A safety tests**

Run:

```bash
./build/tests/aleph_tests -tc="graph serialization*"
./build/tests/aleph_tests -tc="lowering*"
./build/tests/aleph_tests -tc="regression_*"
```

Expected:

```text
all tests passed
```

for each command.

- [ ] **Step 4: Run full `aleph_tests` without excluding localized delete**

Run:

```bash
timeout 300 ./build/tests/aleph_tests --duration=true
```

Expected: full suite passes within timeout. If a different unrelated case times out, document the exact case name and run the suite with only that unrelated case excluded.

- [ ] **Step 5: Run non-aleph CTest isolation tests**

Run:

```bash
ctest --test-dir build -E '^aleph_tests$' --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 6: Commit verification notes if test-only docs changed**

If no files changed during verification, do not commit. If a verification note is added to docs, commit only that doc:

```bash
git add docs/superpowers/plans/2026-06-29-aleph-localized-delta-hardening.md
git commit -m "docs: record localized delta verification"
```

Expected: no source files are staged by this step.

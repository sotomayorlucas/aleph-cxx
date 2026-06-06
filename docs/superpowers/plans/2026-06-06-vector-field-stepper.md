# Vector-Field (Section<Vec3>) Diffusion Stepper — Implementation Plan (physics slice)

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use `- [ ]`.

**Goal:** a `VectorDiffuseStepper` (`aleph.sim:vector_field`) evolving a `Section<Vec3>` by the scalar Δ applied component-wise — the vector analog of `DiffuseStepper`, on the shared operator.

**Spec:** `docs/superpowers/specs/2026-06-06-vector-field-stepper-design.md` (REVISED — read §2/§4/§5). **Model it on `aleph.sim-diffuse.cppm` (the scalar `DiffuseStepper`) + `tests/sim/test_diffuse.cpp` (the harness).** **Conventions:** `cmake --build build-release && ctest --test-dir build-release`; `--test-case="*ector*"`/`"*diffuse*"`; strict `cmake --build build-release-strict 2>&1 | grep "warning:" | grep -v '^ninja:' | wc -l` → 0.

---

## Task 1: VectorDiffuseStepper + tests

**Files:** `graph/src/aleph.sim/aleph.sim-vector_field.cppm` (new), `…/aleph.sim.cppm` + `…/CMakeLists.txt`, `tests/sim/test_vector_field.cpp` (new) + `tests/CMakeLists.txt`.

- [ ] **Step 1 — partition skeleton.** New `aleph.sim-vector_field.cppm`: `module;` + `#include <cstddef>/<expected>/<span>/<vector>/<cmath>/<limits>`; `export module aleph.sim:vector_field;` `import aleph.math; import aleph.linalg.sparse; import :section;` `export namespace aleph::sim {` with the using-block `using aleph::math::f64; using aleph::math::f32; using aleph::math::Vec3; using aleph::linalg::sparse::DMatrix;` (mirror `diffuse.cppm`). Register the file in `graph/src/aleph.sim/CMakeLists.txt` `FILE_SET CXX_MODULES`; add `export import :vector_field;` to `aleph.sim.cppm`. Build (empty) to confirm wiring (`aleph_sim` already links what's needed; `:section` is a sibling partition).

- [ ] **Step 2 — `VectorDiffuseParams` + `VectorDiffuseStepper::step`** (model on `DiffuseStepper`; 3 EXPLICIT component blocks):
```cpp
struct VectorDiffuseParams { f64 alpha = 1.0; };
struct VectorDiffuseStepper {
    VectorDiffuseParams params{};
    [[nodiscard]] std::expected<void, StepError>
    step(Section<Vec3>& u, const DMatrix& delta, f64 dt) const noexcept {
        const std::size_t n = u.size();
        if (n == 0) return std::unexpected(StepError::EmptyField);
        if (delta.rows() != n || delta.cols() != n) return std::unexpected(StepError::DimMismatch);
        std::vector<f64> comp(n);
        // --- X ---
        for (std::size_t i = 0; i < n; ++i) comp[i] = static_cast<f64>(u.data[i].x);
        { const std::vector<f64> lap = delta.matvec(std::span<const f64>(comp.data(), n));
          for (std::size_t i = 0; i < n; ++i)
              u.data[i].x = static_cast<f32>(static_cast<f64>(u.data[i].x) - dt * params.alpha * lap[i]); }
        // --- Y --- (identical with .y)
        for (std::size_t i = 0; i < n; ++i) comp[i] = static_cast<f64>(u.data[i].y);
        { const std::vector<f64> lap = delta.matvec(std::span<const f64>(comp.data(), n));
          for (std::size_t i = 0; i < n; ++i)
              u.data[i].y = static_cast<f32>(static_cast<f64>(u.data[i].y) - dt * params.alpha * lap[i]); }
        // --- Z --- (identical with .z)
        for (std::size_t i = 0; i < n; ++i) comp[i] = static_cast<f64>(u.data[i].z);
        { const std::vector<f64> lap = delta.matvec(std::span<const f64>(comp.data(), n));
          for (std::size_t i = 0; i < n; ++i)
              u.data[i].z = static_cast<f32>(static_cast<f64>(u.data[i].z) - dt * params.alpha * lap[i]); }
        for (std::size_t i = 0; i < n; ++i)
            if (!std::isfinite(u.data[i].x) || !std::isfinite(u.data[i].y) || !std::isfinite(u.data[i].z))
                return std::unexpected(StepError::NonFinite);
        return {};
    }
};
```
(`comp` scratch reused across the 3 blocks; each `lap` is block-scoped. The `{ }` blocks avoid redeclaring `lap` — `-Wshadow` clean. Verify `DMatrix::matvec` signature + `Vec3` member names against the source before pasting.)

- [ ] **Step 3 — build + run existing.** `cmake --build build-release`; confirm the sim suite (wave/diffuse/section) is unaffected. Strict 0.

- [ ] **Step 4 — tests** (`tests/sim/test_vector_field.cpp`; register in `tests/CMakeLists.txt`). Mirror `test_diffuse.cpp`'s graph/Δ/CFL helpers (`make_path`, `build_laplacian(g, default_weight)`, the Gershgorin `dt`):
  - **Component independence (ONE step):** x-spike `Section<Vec3>` + same-spike `Section<f64>`, same Δ/dt/α; one `VectorDiffuseStepper.step` + one `DiffuseStepper.step`; assert x-components ≈ scalar `data` (tol 1e-5) AND y==z==0.0 exactly.
  - **Per-component sum conservation (ONE step):** arbitrary x/y/z field; assert each component's Σ unchanged after one step (tol ~1e-5·n).
  - **Smoothing:** single-node spike on a path → a neighbour's component grew from 0, the spike shrank.
  - **Guards:** `EmptyField` (empty `Section<Vec3>`); `DimMismatch` (size ≠ Δ rows); `NonFinite` — seed `u.data[k].x = std::numeric_limits<float>::infinity()` then step → `StepError::NonFinite` (do NOT rely on a past-CFL dt — it stays finite in one step).
  - **Determinism:** two steps on identical inputs → equal element-wise (`Vec3::operator==`).

- [ ] **Step 5 — build + run + strict.** `--test-case="*ector*"` (your case names) all pass; full `ctest`; strict 0. Report the component-vs-scalar max diff + the sum-conservation residuals. **Commit** `feat(sim): VectorDiffuseStepper — Section<Vec3> diffusion on the shared Δ (component-wise)`.

---

## Final verification
- [ ] Component-independence (x≈scalar, y=z=0), per-component sum conservation, smoothing, the 3 guards (incl. seeded-inf NonFinite), determinism all pass.
- [ ] `ctest` all pass (wave/diffuse/section/helmholtz unaffected); release-strict 0.
- [ ] One-substrate: the stepper consumes the same `WeightedLaplacian.matrix` Δ; vector physics on the shared operator.

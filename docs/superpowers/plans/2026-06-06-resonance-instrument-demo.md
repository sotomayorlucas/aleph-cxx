# "Geometry Instrument" — Mesh Resonance Spectrum Demo (plan)

> A DEMO app gluing already-tested pieces (Helmholtz residual <1e-9). Lighter flow than an engine slice: implement → code review + gates → merge + artifact. No new core numerics.

**Goal:** compute & export a mesh's **acoustic resonance spectrum** on the shared Δ — anchor an `AudioSource` at a node, sweep frequency, sample the response at a `Microphone`, and plot |response| vs Hz. The peaks are the mesh's resonant modes (where `Δ − k²I` is near-singular, i.e. k² near an eigenvalue of Δ). Demonstrates the one-substrate thesis tangibly: the SAME Δ that renders the mesh gives its sound.

**Confirmed APIs:** `aleph::flow::IncrementalLaplacian::from_graph(g)` → `{node_order, laplacian, …}` (Δ = `build_laplacian(g, default_weight)`). `aleph::sim::HelmholtzOperator::make(const IncrementalLaplacian&, f64 k²)` → `std::expected<…, HelmholtzError>` (FactorFailed when `Δ−k²I` is singular = a resonance); `.solve(source, flow)` → `std::optional<std::vector<f64>>`. `AudioSource{mesh_anchor, frequency_hz, amplitude}` → `.k_squared()` = `(2πf/340)²`, `.source_vector(order)` one-hot. `Microphone{mesh_anchor}.sample(phi, order)`. Grid builder pattern: `tests/flow/test_mv_localization.cpp:62` (`make_grid` — Mesh nodes + 4-neighbour Adjacent edges).

---

## Task 1: the app + a testable sweep + the spectrum artifact

**Files:** `apps/aleph_resonance/{main.cpp, resonance.hpp, CMakeLists.txt}` (new), `apps/CMakeLists.txt` (register), `tests/sim/test_resonance.cpp` (new) + `tests/CMakeLists.txt`.

- [ ] **Step 1 — `resonance.hpp` (the testable core).** A self-contained header (`#pragma once`; imports `aleph.flow`, `aleph.sim`, `aleph.types`):
```cpp
struct ResonancePoint { aleph::math::f64 freq_hz; aleph::math::f64 k_squared; aleph::math::f64 mic_abs; bool resonant; };
// Sweep frequency_hz ∈ [0, f_max] in `steps`; at each: k²=AudioSource.k_squared(); op=make(flow,k²);
// FactorFailed (or solve()==nullopt) → resonant=true, mic_abs=NaN; else mic_abs=|Microphone.sample(solve(source))|.
[[nodiscard]] std::vector<ResonancePoint>
resonance_sweep(const aleph::flow::IncrementalLaplacian& flow,
                aleph::types::NodeId src, aleph::types::NodeId mic,
                aleph::math::f64 f_max_hz, int steps);
// f_max from the Δ spectrum: k²_max ≈ Gershgorin bound = 2·max_i Δ(i,i) (Laplacian: diag = off-diag-sum,
// so radius ≈ 2·max diagonal); f_max = 340·sqrt(k²_max)/(2π). A helper computes it from flow.laplacian.
```
Each step: `AudioSource a{src, freq, 1.0}; const f64 k2 = a.k_squared(); auto op = HelmholtzOperator::make(flow, k2);` if `!op` → `{freq,k2,NaN,true}`; else `auto phi = op->solve(a.source_vector(flow.node_order), flow);` if `!phi` → `{freq,k2,NaN,true}`; else `{freq, k2, std::abs(Microphone{mic}.sample(*phi, flow.node_order)), false}`.

- [ ] **Step 2 — `main.cpp`.** Build a grid (`make_grid(R)` inline, R≈6, mirroring the mv-test pattern: Mesh nodes + 4-neighbour Adjacent edges); `IncrementalLaplacian::from_graph(g)`; pick `src` = a corner, `mic` = the opposite corner; `resonance_sweep(flow, src, mic, f_max, ~400)`. Write (a) a **CSV** `freq_hz,k_squared,mic_abs,resonant` to an out path (argv[1] or a default), and (b) a **PPM line-plot** of `mic_abs` vs `freq` (log-scale the y so the peaks read; mark `resonant` columns; W≈640 H≈240) using the `aleph_rt` `P6` write idiom. Print the top few peak frequencies to stdout ("resonant modes at ~X Hz"). Keep it CLI-simple: `aleph_resonance <out_prefix>` → `<prefix>.csv` + `<prefix>.ppm`.

- [ ] **Step 3 — CMake.** `apps/aleph_resonance/CMakeLists.txt`: `add_executable(aleph_resonance main.cpp)` + `target_link_libraries(aleph_resonance PRIVATE aleph_graph aleph_types aleph_flow aleph_sim aleph_math aleph_alloc aleph_flags_test)` (add libs until it links — flow/sim pull their deps). Register in `apps/CMakeLists.txt`.

- [ ] **Step 4 — test** (`tests/sim/test_resonance.cpp`, include `resonance.hpp`; register in `tests/CMakeLists.txt`). On a small grid Δ:
  - The k²≈0 (low-freq) `mic_abs` is FINITE (the PSD/Green's response).
  - The spectrum is **non-trivial**: `max(mic_abs over non-resonant) > 5× median(mic_abs)` (a clear resonance peak exists) — the demo actually shows modes, not a flat line.
  - Every non-resonant `mic_abs` is finite (`std::isfinite`); `resonant` points cluster near peaks (a `resonant` point's neighbours have large `mic_abs`).
  - Determinism: two sweeps byte-identical.

- [ ] **Step 5 — build + run + gates.** `cmake --build build-release` (+ the app); run `./build-release/apps/aleph_resonance/aleph_resonance /tmp/reson` → confirm the CSV has peaks + stdout lists modes; convert `/tmp/reson.ppm` → `docs/superpowers/artifacts/2026-06-06-resonance-spectrum.png` (ImageMagick, as `visual_review.sh` does). `--test-case="*esonance*"` pass; full `ctest`; strict 0. **Commit** `feat(demo): aleph_resonance — mesh acoustic resonance spectrum on the shared Δ`.

---

## Final verification
- [ ] The app runs, prints the resonant-mode frequencies, writes CSV + PPM; the plot shows distinct peaks (modes).
- [ ] The sweep test passes (finite baseline, a clear peak, determinism); `ctest` all pass; release-strict 0.
- [ ] Artifact `docs/superpowers/artifacts/2026-06-06-resonance-spectrum.png` shows the spectrum.

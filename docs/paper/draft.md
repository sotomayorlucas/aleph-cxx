# One Substrate: Byte-Exact Local Maintenance of a Curvature-Weighted Graph Laplacian for Interactive Rendering and Physics

**Working draft — 2026-07-03.**
Target: SGP/CGF (fallback SCA). Related-work basis: `related-work-survey.md`.
Evaluation data: `data/scaling.csv` (produced by `bench/aleph_bench_scaling`).

---

## Abstract (draft)

Interactive engines keep rendering and physics in separate data structures;
geometry processing reuses prefactored Laplacians but assumes fixed topology.
We present a system in which a single Ollivier-Ricci-curvature-weighted graph
Laplacian Δ is the shared substrate for rendering signals and four physics
solvers (wave, heat, frequency-domain Helmholtz acoustics, vector diffusion),
maintained *incrementally and byte-identically* under structural double-pushout
(DPO) graph rewrites. We observe that the standard globally-formulated
Ollivier-Ricci computation is not byte-reproducible under edits — the W₁
transport LP's floating-point output depends on global support size — and show
that a bounded-support curvature κ_R (R=2) restores *exact* locality: after an
edit, recomputing only a 2-hop dirty cover derived from the DPO interface
yields an operator bit-identical to a full rebuild, by construction. A
Mayer-Vietoris residual over the graph's flag complex certifies each localized
rebuild at runtime. On lattice benchmarks the localized rebuild recomputes a
constant number of edges as the mesh grows (dirty/|E| → 0), with certificate
overhead of ~1 ms, and the whole engine — editor with undo/redo included — is
deterministic to the bit.

**Positioning (one line):** we do not introduce a new curvature, a new
Laplacian, or a new incremental factorization; we show that an ORC-weighted
graph Laplacian can serve as a single, dynamically-maintained substrate for
rendering and physics, and that under DPO edits this substrate can be rebuilt
locally, byte-identically, and self-certified at runtime — turning determinism
into a correctness proof for a live, editable engine.

## 1. Introduction

- Gap: physics engines are representation-agnostic w.r.t. rendering (cite the
  2026 Gaussian-splat-physics framing); DEC / Heat Method / Projective
  Dynamics reuse ONE prefactored operator but assume FIXED topology; editors
  need structural edits.
- Thesis: make the operator the single source of derived truth and maintain it
  *exactly* under edits. Exactness (bit-identity) is what turns "incremental"
  from an optimization into a *verifiable invariant* — the incremental path
  provably cannot diverge from the from-scratch path.
- Contributions:
  1. **Observation** (motivation, not a theorem about ORC): under the global
     W₁ LP formulation (Charnes perturbation over the full component), the
     *computed* curvature of far-away edges drifts (~1e-10) when a node is
     added anywhere — the mathematical locality of ORC [Jost–Liu] does not
     transfer to the computed value. A byte-exact engine cannot use the global
     formulation.
  2. **Bounded-support κ_R with exact locality** (Prop. 1): κ_R(e) is a pure
     function of the induced radius-R ball B_R(e); with R=2 the in-ball
     geodesics among the W₁ support coincide with the global ones (Lemma 1),
     and the localized rebuild after a DPO rewrite is bit-identical to a full
     rebuild by construction.
  3. **Runtime cohomological certificate**: a Mayer-Vietoris residual (== 0)
     over the rewrite's U/K/R cover of the flag complex, certifying the
     decomposition each localized rebuild relies on — per-edit, in a live
     engine. **Contrast with care** (see contrast-notes.md, RISK ESCALATION):
     Volk 2026 incrementally maintains sheaf cohomology *through the sheaf
     0-Laplacian* with a PROVEN zero-drift / bit-exact guarantee (Thm 3.10)
     and a §8.4 self-consistency check that re-verifies its own maintained
     cohomology; Young 2026 already frames H¹ as an obstruction certificate
     and uses MV for incrementality in program analysis. We claim NO primacy
     on incremental cohomology, bit-exact maintenance, or MV-as-certificate.
     The defensible claim (no prior work has all four qualifiers): the first
     cohomological certificate that is an EXTERNAL witness guarding a
     DIFFERENT object — a *curvature-weighted rendering/physics operator's*
     localized rebuild — under *DPO scene-graph edits* inside a *live,
     deterministic, interactive engine*.
  4. **The one-substrate system**: the same cached, kernel-aware LDLᵀ of Δ
     drives light/importance, wave, heat, Helmholtz (indefinite, via
     Bunch-Kaufman on the k²-shifted copy), vector diffusion, and implicit
     (unconditionally stable) stepping — in a deterministic interactive editor
     with undo/redo, gated by TLA+-mirrored invariants and byte-exact CI
     oracles.

## 2. Related work (summary; full survey in related-work-survey.md)

Concede and cite: ORC locality [Jost & Liu 2014; arXiv:2208.09535]; local
factor reuse [Herholz & Alexa 2018; Herholz & Sorkine-Hornung 2020]; one
prefactored operator, many tasks [DEC 2005/2013; Crane et al. 2013; Sharp et
al. 2019; Bouaziz et al. 2014]; curvature-weighted Laplacians [LLY
reweighting, arXiv:2603.11060]; sheaf spectra [Hansen & Ghrist 2019];
incremental sheaf cohomology *via the sheaf Laplacian, bit-exact* [Volk 2026 —
the closest prior art; camera-ready contrast paragraphs in contrast-notes.md];
H¹-as-certificate + MV incrementality [Young 2026 — full title and author
correction in contrast-notes.md]; DPO in geometric modeling [Jerboa];
deterministic lockstep [Fiedler; ReproBLAS]. Our delta on each axis: dynamic
topology, bit-identity as invariant, an EXTERNAL certificate guarding a
*different, curvature-weighted numerical operator* (not re-verifying the
maintained object itself), and the rendering+physics union on ONE graph
operator.

## 3. System overview

Typed scene graph G = (V, E, τ, α) (10 well-formedness invariants,
TLA+-specified, mirrored in C++ and regression-checked). Edits are DPO
rewrites (spawn_light, remove_object, replace_material, refine_cell — rule
preservation model-checked) plus editor ops lowered to rewrites
(AddObject/AddLight/DeleteObject with cascade semantics, transactional OBJ
import). Derived products: lowered IR → software raster + path tracer; the
flow layer builds Δ = D_w − A_w with w(e) = 1 + max(κ(e), −0.95) over the
Adjacent 1-skeleton; `aleph.sim` evolves Sections (scalar/vector fields) on Δ;
undo/redo replays the graph, and every derived product is a pure function of
it.

## 4. Exact locality of bounded-support curvature

**Definition (κ_R).** For an edge e=(a,b), build the induced subgraph on the
ball B_R(a,b) (BFS from {a,b}, R hops, canonically sorted); μ_a, μ_b are
uniform on in-ball 1-hop neighbourhoods; d is the in-ball all-pairs hop
metric; κ_R(e) = 1 − W₁(μ_a, μ_b)/d(a,b), with W₁ solved by the
transportation simplex with Charnes perturbation over the *ball-restricted*
support.

**Lemma 1 (R=2 suffices for metric fidelity).** For any support pair
x ∈ N(a), y ∈ N(b): d_G(x,y) ≤ 3 (via the edge ab), and every intermediate
vertex of a ≤3-hop x→y geodesic lies within 2 hops of {a,b}; hence the
in-ball metric restricted to the support equals the global metric. (The only
remaining difference vs the global computation is the *support/perturbation
size* — ball vs component — which is exactly the non-reproducible part.)

**Proposition 1 (byte-exact locality).** Fix the deterministic evaluation
order (sorted ball, fixed simplex pivoting, f64). If G and G′ induce the same
subgraph on B_R(e), then κ_R(e;G) == κ_R(e;G′) bitwise. *Proof:* the entire
computation is a deterministic function of the induced ball; equal inputs,
equal bits. ∎

**Corollary (localized rebuild).** For a DPO rewrite with seed set S (created
mesh vertices ∪ endpoints of created/deleted Adjacent edges), every edge
outside the 2-hop cover of S has an unchanged B_2, so its cached κ_R is the
full rebuild's value bitwise; dirty edges are recomputed by the identical
primitive; assembly iterates canonical skeleton-edge order. Therefore
`build_laplacian_local(G′, prev, dirty)` == `build_laplacian_bounded(G′)`
bitwise. (Gated in CI on multi-edit traces, Tier-1 `==` on every matrix entry
and every curvature.)

**The observation that motivates all this** (phrase carefully, see survey
Risk 2): with the global formulation the LP's perturbation depends on the
component size n, so *far-field computed values* move ~1e-10 under any edit —
not a contradiction of mathematical locality; a property of the computed
artifact. Byte-exact caching is impossible under that formulation.

## 5. Runtime Mayer-Vietoris certificate

decompose_rewrite(G, G′, preserved) → (U, K, R) subgraph cover;
`mayer_vietoris_certify_with` builds the flag complexes of G′, U, R, K, the
visibility sheaf on each, and checks the signed residual
h⁰(G′) − (h⁰(U) + h⁰(R) − h⁰(K) + ε_sheaf) == 0 (ε_sheaf = rank of the
connecting map). Residual 0 certifies the cover the locality argument
decomposes over. Cost: ~1 ms at |V| ≈ 64 (Tier-2; measured in §7 as
cert_max grids). [TODO: exact cost curve from data/scaling.csv.]

## 6. One substrate, five consumers

- **Rendering**: lowering::importance reads the global curvature channel; the
  raster/PT pipelines read Δ-derived vcol fields (wave demo).
- **Wave / heat / vector diffusion**: explicit matvec steppers over Sections
  on Δ (component-wise scalar Δ for Vec3).
- **Helmholtz acoustics**: (Δ − k²I)φ = s; PSD path reuses the CACHED
  kernel-aware LDLᵀ (mean-projected); indefinite path Bunch-Kaufman-factors
  the shifted copy. AudioSource/Microphone bridge to frequency domain.
- **Implicit stepping** (new): ShiftedLaplacian factors (I + βΔ) (SPD ∀β≥0);
  backward-Euler heat (β=dt·α) and wave (β=dt²c²) are unconditionally stable
  (tests drive 100× past the explicit CFL and assert the explicit stepper
  diverges at the same dt); one factorization amortized over steps ≫ edits.
- Determinism: pure f64, fixed orders, no RNG in any of the above; identical
  trajectories bit-for-bit run-to-run (CI-gated).

## 7. Evaluation (numbers from data/scaling.csv — regenerate with
`./build/bench/aleph_bench_scaling --reps 5 > docs/paper/data/scaling.csv`)

RxR 4-neighbour lattices, grids 8–64 (|V| 64–4096, |E| 112–8064); per-grid
edit trace add1/add2/add_corner/delete threading prev (mirrors the editor
path). 5-rep medians; full sweep in data/scaling.csv; figures figs/fig_[a-d].

- **O(touched)**: the dirty cover is CONSTANT in mesh size (37/51/13/49 per
  edit kind, grid 8 through 64); dirty/|E| falls from 0.33–0.44 (grid 8) to
  0.0016–0.0063 (grid 64) — the fallback gate (0.5) is never approached
  beyond toy sizes (fig_b).
- **Local vs full bounded rebuild** (fig_a): full grows ∝|E| (30 ms → 4.8 s,
  grid 8 → 64); local stays ~flat (17–30 ms) until dense assembly dominates
  (knee at |E|≈2k), settling at 36–45× speedup at grid 64 (108–133 ms vs
  4.8 s). The knee is the §8 dense-assembly limitation, visible and honest:
  the κ_R recompute is O(touched); the O(n²) assembly bounds the end-to-end
  win at large n until the sparse-operator slice lands.
- **Global formulation blowup (the contrast case, fig_c)**: 6.1 s at 64 nodes
  and 337 s at 144 nodes, vs 20 ms / 84 ms bounded — ×309 and ×4005; the
  global-support series is unmeasurable past toy sizes, which is precisely
  why a byte-exact engine needs κ_R.
- **Bit-exactness**: asserted on every bench row (the bench exits nonzero on
  any mismatch — the whole sweep ran green); Tier-1 CI equality on multi-edit
  traces.
- **MV certificate** (fig_d): residual 0 on every edit; ~1 ms at 65 nodes,
  ~7.7 ms at 257, ~53 ms at 577 (superlinear — the visibility-sheaf H⁰
  computation over the full flag complex dominates; localizing the
  certificate itself is future work, but at ~53 ms it is already viable
  per-edit at editor scale).
- **Qualitative artifacts**: deterministic wave-ripple contact sheets
  (`docs/superpowers/artifacts/2026-06-06-wave-field.{gif,mp4}`), resonance
  spectrum (`2026-06-06-resonance-spectrum.png`), raster/PT parity sheets.
- **Verification stack**: 20 ctest suites (unit → Tier-1 byte-exact → Tier-2
  MV → regression PPM baselines), ASan+UBSan gate, TLC model checking of the
  graph/DPO specs, TLA↔C++ sync test.

## 8. Limitations / future work

- Dense operator storage: assembly and LDLᵀ are dense (O(n²) memory; assembly
  O(n²) per rebuild) — the curvature recompute is O(touched) but the end-to-end
  edit cost is eventually assembly-bound; sparse assembly + factor update
  (CHOLMOD-style, or Herholz-style factor reuse — now with a bit-exactness
  question attached) is the natural next slice.
- The rendering-importance channel still uses the global-support curvature
  (deliberately, as the contrast case); moving it to κ_R changes its values
  within ~1e-6.
- Implicit steppers refactor (I+βΔ) from scratch per (Δ, dt); factor *updates*
  under edits are future work.
- Scalar Δ applied component-wise to vector fields does not couple components;
  a connection/Hodge Laplacian (curvature-coupled) is the follow-up numerics.
- Grid-lattice evaluation only; irregular meshes (OBJ imports) exercise the
  same code paths but scaling curves on them are TODO.

## 9. Reproducibility

Single deterministic C++26 codebase; every figure regenerable:
`aleph_bench_scaling` (CSV), `visual_review.sh wave` (contact sheets),
`aleph_edit --wave` (live). CI: gcc-16 container, full ctest + regression
subset.

---

### TODO list for submission readiness
1. [ ] Final scaling sweep numbers (grids 24–64) into §7 + two plots
       (t_local vs |E| at constant dirty; dirty/|E| decay; cert cost).
2. [ ] Irregular-mesh trace (OBJ import + edits) as a second benchmark family.
3. [ ] Write Lemma 1 / Prop. 1 proofs in full (currently sketches).
4. [ ] Verify paywalled citations against PDFs (survey caveat). Volk/Young
       full texts READ and contrast paragraphs WRITTEN (contrast-notes.md) —
       re-confirm quotes/section numbers against official PDFs at camera-ready
       (Volk v2 numbering may shift); use Young's corrected title/author.
5. [ ] Decide headline: SGP framing (operator maintenance + certificate) vs
       SCA framing (physics family on one substrate). Current draft: SGP.
6. [ ] LaTeX port (CGF template) once §7 data is final.

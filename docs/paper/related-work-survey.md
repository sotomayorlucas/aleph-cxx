# Related Work & Novelty Survey — "aleph" (single ORC-weighted Laplacian substrate)

**Prepared:** 2026-07-03
**Scope:** Novelty sweep for the paper claiming a C++ engine in which a single Ollivier–Ricci-curvature-weighted graph Laplacian Δ is the shared substrate for rendering *and* physics (wave / heat / Helmholtz / vector diffusion), maintained incrementally and byte-identically under DPO graph rewrites, certified at runtime by a Mayer–Vietoris residual.

> **UPDATE 2026-07-05:** bibliographic data verified — see `bib-verification.md` (4 corrections: 2208.09535 authors are DasGupta/Grigorescu/Mukherjee, NOT Ni-Wang; the sparsifiers paper is arXiv:1906.10530 STOC'19; LLY-reweighting is Kotharkar 2026; Jerboa is LNCS 8571). `tex/refs.bib` is the authoritative record.

> **Method note / caveat.** Findings below come from web search + targeted full-text fetches. For the two 2026 papers that are the nearest prior art (Volk; Young) I fetched the arXiv abstract pages directly and they are real. Where a specific number or claim comes only from a search-summary model I have flagged it. Several cited items are behind paywalls (ACM TOG, ScienceDirect) and were read only at abstract level. Treat exact quantitative claims from those as "to be verified against the PDF before citing."

---

## Executive summary / novelty verdict (read this first)

**The core contribution is defensible, but only if positioned as a *systems + determinism* result, not as a new theorem about curvature locality or a new incremental-factorization algorithm.** Each *individual* ingredient has close prior art:

- Bounded-support / local Ollivier–Ricci curvature (ORC) is **known to be mathematically local** (depends only on short cycles ≤ 5 / the 2–3-hop support). The paper's κ_R is a *principled use* of that known locality, not a discovery that ORC is local.
- Incremental / local reuse of a prefactored sparse Laplacian factorization under local mesh edits is a **well-established line in geometry processing** (Herholz–Alexa "Factor Once"; Herholz–Davis–Alexa "Localized solutions"; Herholz–Sorkine-Hornung sparse Cholesky updates).
- Reusing **one** prefactored Laplacian across many geometry-processing tasks is the explicit philosophy of **Discrete Exterior Calculus** and the **(vector) heat method**.
- Bit-exact `f64` determinism is standard practice in **deterministic-lockstep** game engines.
- **Most dangerously:** a June 2026 paper (Volk) already does *incremental sheaf cohomology maintenance under a "bounded local geometry" assumption with O(1)-in-n edits and "zero measured drift"* — overlapping the paper's contributions #2 (byte-identical bounded-support incremental rebuild) and #3 (cohomological runtime certificate) at the abstract-mathematical level. It is pure computational topology, **not** graphics/physics/ORC/DPO/systems — so it is differentiable, but it *must* be cited and distinguished.

**What is genuinely novel is the *combination and the guarantee*:** an ORC-weighted graph Laplacian used as a *single shared operator* for both rendering signals and several physics solvers, whose incremental rebuild under **DPO** rewrites is **byte-identical to a full rebuild by construction** (not merely low-error), self-certified at runtime by a **Mayer–Vietoris residual == 0**, inside a **deterministic interactive editor with undo/redo**. No prior work found unifies all of: ORC edge weights + DPO structural edits + byte-exact incremental Laplacian + cohomological runtime certificate + one-operator rendering-and-physics. That intersection is the paper.

---

## Area 1 — Local / bounded / approximate Ollivier–Ricci curvature and its behavior under edits

**Key references**

1. Ollivier (2009), *Ricci curvature of Markov chains on metric spaces*, J. Funct. Anal. — foundational definition (W₁ transport between lazy random-walk measures).
2. Jost & Liu (2014), *Ollivier's Ricci curvature, local clustering and curvature-dimension inequalities on graphs*, Discrete Comput. Geom. (arXiv:1103.4037) — **the** locality result: analytic upper/lower bounds in terms of node degrees and triangle/short-cycle counts; ORC of an edge depends only on its short-cycle support.
3. Ni, Lin, Luo, Gao (2019), *Community Detection on Networks with Ricci Flow*, Sci. Rep. 9:9984 — canonical large-graph ORC pipeline; iterative *recomputation of ORC only for affected nodes/edges* after edge removal (an informal locality-of-update already in use).
4. Ni, Lin, Gao, Gu (2018), *Network Alignment by Discrete Ollivier-Ricci Flow*, Graph Drawing — ORC as adaptive edge weights.
5. Sia, Jonckheere, Bogdan (2019), *Ollivier-Ricci Curvature-Based Method to Community Detection*, Sci. Rep. 9 — ORC as bottleneck/bridge detector.
6. **Ni & Wang / "On computing Discretized Ricci curvatures of graphs: local algorithms and (localized) fine-grained reductions"** (arXiv:2208.09535, Theoret. Comput. Sci. 2023) — **highly relevant to contribution #1**: exact ORC ≡ min-cost perfect matching on a local bipartite instance; proves Ω(n) queries needed in the worst case to compute curvature *exactly*, and gives constant-query approximate schemes under structural constraints.
7. Jost–Liu-style linear-time lower-bound approximations; Jaccard/gJC proxies (CurvGAD, arXiv:2502.08605) — scalable *approximate* ORC.
8. Loisel & Romon (2014), *Ricci curvature on polyhedral surfaces via optimal transportation* (arXiv:1402.0644) — ORC/OT curvature on surface meshes (graphics-adjacent).
9. saibalmars/GraphRicciCurvature — reference implementation (documents the standard global normalization / lazy parameter conventions).
10. Salez (2022) and Münch–Wojciechowski, *Ollivier–Ricci curvature for general/weighted graph Laplacians* — curvature expressed *through* the graph Laplacian on weighted graphs.

**Novelty verdict.** The *locality* of ORC is prior art and well-cited (Jost–Liu; the fine-grained-reduction paper explicitly formalizes "local neighborhood suffices"). Therefore the paper should **not** claim to have discovered bounded-support locality; it should cite Jost–Liu and frame κ_R (ball B_R, R=2 capturing geodesics ≤ 3) as the *engineering realization* of that known locality with an exactness guarantee. 

The **non-localizability finding (contribution #1) is the subtle one.** It appears to *contradict* the known locality unless stated precisely. The known result says the *mathematical* value depends only on local support; the paper's finding is about the *computed* value: a global W₁ LP (Charnes-style perturbation over the full support / global normalization) yields a solver solution whose floating-point value / degenerate-optimum pivoting depends on global support size, hence ~1e-10 drift on far edges. **No prior work found states this byte-level non-localizability of a globally-formulated ORC LP** — this looks novel *as an observation*, but it is an artifact of the formulation, not a property of ORC. Phrase it as such (see Risks). The fine-grained-reduction paper (2208.09535) is the closest and should be cited as the theoretical backdrop.

---

## Area 2 — Incremental / dynamic graph-Laplacian maintenance and local factorization reuse

**Key references**

1. **Herholz & Alexa (2018), *Factor Once: Reusing Cholesky Factorizations on Sub-Meshes*, ACM TOG (SIGGRAPH Asia)** — nearest prior art: update a sparse Cholesky factor to get the factorization of the operator restricted to a sub-mesh, up to 10× faster than refactorization. Directly analogous to "incremental operator update touches only a dirty cover."
2. **Herholz, Davis, Alexa (2017), *Localized Solutions of Sparse Linear Systems for Geometry Processing*, ACM TOG** — exploit sparsity in RHS *and* desired-solution set for local solves on a prefactored system.
3. **Herholz & Sorkine-Hornung (2020), *Sparse Cholesky Updates for Interactive Mesh Parameterization*, ACM TOG** — incremental factor updates driven by local mesh edits; the closest "interactive + local edit + factor maintenance" analogue.
4. Hecht, Bickel et al. / Herholz, *Updated Sparse Cholesky Factors for Corotational Elastodynamics* — factor update/downdate under changing systems.
5. Davis & Hager, *Dynamic supernodes in sparse Cholesky update/downdate*, ACM TOMS — the low-level update/downdate machinery (CHOLMOD).
6. Chen, Liu et al. (2021), *Multiscale Cholesky Preconditioning for Ill-Conditioned Problems*, ACM TOG — hierarchical/local factorization.
7. Durfee, Gao, Peng, et al., dynamic Laplacian solvers / *Dynamic Graph Algorithms and Graph Sparsification* (arXiv:1909.06413) — theory: maintain Laplacian-system solutions under edge updates in O(m^{3/4}) amortized; dynamic spectral sparsifiers.
8. *Incremental Eigenpair Computation for Graph Laplacian Matrices* (arXiv:1801.08196) — incremental spectral maintenance.
9. Radial-basis / recurrence-Cholesky incremental mesh deformation (ScienceDirect S002199911830696X) — incremental LDLT reuse in CFD mesh deformation.

**Novelty verdict.** Incremental/local reuse of a prefactored sparse Laplacian under local edits is **an established sub-field** (the Herholz line is squarely this). The paper is *not* the first to update a Laplacian factorization locally. **Two things separate it:** (a) the update is provably **byte-identical to a full rebuild by construction** — geometry-processing prior art measures *speed and residual error*, not bit-exact equality to a from-scratch rebuild; and (b) the trigger is a **DPO rewrite** with a formally-defined 2-hop dirty cover, not an ad-hoc edit. Position contribution #2 as "byte-identical local rebuild with a formal dirty-cover derived from the rewrite's DPO interface," explicitly contrasting with Herholz et al. (fast but not bit-identical). Do **not** claim novelty for "local Laplacian updates" per se.

---

## Area 3 — Sheaf Laplacians, cellular sheaves, and Mayer–Vietoris as a computational/runtime certificate

**Key references**

1. **Hansen & Ghrist (2019), *Toward a Spectral Theory of Cellular Sheaves*, J. Appl. Comput. Topology (arXiv:1808.01513)** — foundational: Hodge/sheaf Laplacian, spectrum ↔ sheaf cohomology; eigenvalue interlacing, sparsification, effective resistance. Cite as the substrate for any "cohomology from a Laplacian" claim.
2. Hansen, *A Gentle Introduction to Sheaves on Graphs* — accessible reference.
3. **Bodnar, Di Giovanni, Chamberlain, Liò, Bronstein (2022), *Neural Sheaf Diffusion*, NeurIPS** — sheaf Laplacian as a *diffusion* operator on graphs (heterophily/oversmoothing). Nearest "sheaf Laplacian used dynamically" reference, but ML, not certification.
4. Barbero et al. (2022), *Sheaf Neural Networks with Connection Laplacians* — connection-Laplacian sheaf diffusion.
5. **Volk (2026), *Incremental Sheaf Cohomology on Cellular Complexes: O(1)-in-n Lazy Edit Processing under Bounded Local Geometry* (arXiv:2606.04227)** — **NEAREST PRIOR ART to contributions #2 & #3.** Maintains H¹ of a cellular sheaf on a *dynamically evolving* complex; under bounded cell size / stalk dim / nerve degree achieves O(1) amortized per-edit; reports "zero measured computational drift." Pure computational topology / TDA (knowledge-graph consistency example), **not** graphics/physics/ORC/DPO/system.
6. **Young (2026), *Sheaf-Cohomological Program Analysis … via Čech Cohomology* (arXiv:2603.27015)** — **uses the Mayer–Vietoris sequence for *incremental* obstruction counting**; H¹ classifies "gluing obstructions." Program analysis, not geometry, but it is a direct precedent for *"MV enables compositional/incremental correctness reasoning."*
7. Curry, Ghrist, Nanda, *Discrete Morse theory for computing cellular sheaf cohomology* (arXiv:1312.6454) — practical sheaf-cohomology computation.
8. Ó Conghaile, *Cohomological k-consistency* — cohomology as a *consistency certificate* in constraint satisfaction (conceptual cousin of "MV residual certifies consistency").
9. Robinson, *Topological Signal Processing* / sheaves for data consistency — sheaf cohomology as a data-agreement obstruction.

**Novelty verdict.** Using sheaf cohomology / an H¹ obstruction as a *consistency certificate* is **already an idea in the air** (Ó Conghaile in CSP; Young 2026 in program analysis; Robinson in signal processing), and **Volk 2026 already maintains sheaf cohomology incrementally under a bounded-local-geometry assumption with a no-drift claim.** This is the single biggest overlap in the whole survey. The paper's differentiators, which must be made explicit: (i) the certificate is a **runtime, per-edit MV *residual* over the flag complex of an ORC-weighted graph inside a live engine**, used to guard a rendering/physics operator rebuild — not an offline TDA/program-analysis computation; (ii) it is coupled to a **DPO** rewrite and a **numerical** Laplacian factorization, certifying that the *local operator rebuild equals the global one*, which is a different assertion from "H¹ detects a contradiction in a knowledge graph." Cite Hansen–Ghrist, Volk 2026, and Young 2026, and state plainly what MV is doing that they do not.

---

## Area 4 — DPO / algebraic graph rewriting applied to scene graphs, geometry, engines

**Key references**

1. Ehrig, Pfender, Schneider (1973), *Graph-grammars: an algebraic approach* — origin of DPO.
2. Ehrig, Ehrig, Prange, Taentzer, *Fundamentals of Algebraic Graph Transformation* (2006) — the standard monograph; DPO with negative application conditions, gluing conditions.
3. Corradini et al., *Algebraic Approaches to Graph Transformation, Part I (DPO)* — canonical reference.
4. **Jerboa** (Belhaouari, Arnould, Le Gall, Bellet), *Jerboa: A Graph Transformation Library for Topology-Based Geometric Modeling* — **DPO-style rewriting for geometric modelers** on generalized/oriented maps (the closest "DPO for geometry" work).
5. Poudret et al., *Topology-based geometric modelling with graph transformations* — G-maps + rule-based geometric operations.
6. PBPO+ (arXiv:2203.01032) — modern generalization of DPO (relabeling, quasitoposes).
7. AGG / GROOVE / Henshin — DPO tool ecosystems (typically for software models, not scene graphs).

**Novelty verdict.** DPO is mature theory; its use for **geometric modeling exists (Jerboa, G-maps)** but there it rewrites the *topological map* (combinatorial cells), **not** a scene graph coupled to a numerical operator. **No prior work found couples DPO rewrites to an incremental *Laplacian / simulation operator* rebuild**, and none uses DPO as the structural-edit layer of a rendering+physics engine with a bit-exact operator invariant. This is a clean novelty pocket. Position: "DPO gives the structural edits a formal interface (glue graph L, interface morphisms) from which the 2-hop dirty cover — and hence the byte-identical rebuild region — is *derived*, not guessed." Cite Ehrig et al. and Jerboa; note Jerboa as the nearest "DPO in geometry" but emphasize it has no operator/physics/rendering coupling.

---

## Area 5 — "One substrate": prefactored-Laplacian reuse and unified graphics+physics

**Key references**

1. **de Goes, Crane, Desbrun, Schröder, *Digital Geometry Processing with Discrete Exterior Calculus* (SIGGRAPH 2013 course); Desbrun, Hirani, Leok, Marsden, *DEC* (2005).** — the canonical "**one framework / one set of operators (d, ⋆, Δ) → many tasks**." This is the philosophical predecessor to the "one substrate" claim.
2. **Elcott, Tong, Kanso, Schröder, Desbrun (2007), *Stable, Circulation-Preserving, Simplicial Fluids*, ACM TOG** — DEC operators driving fluid physics; unified operator reuse.
3. **Crane, Weischedel, Wardetzky (2013), *Geodesics in Heat / The Heat Method*, ACM TOG** — one prefactored Δ solves many boundary conditions (amortized reuse).
4. **Sharp, Soliman, Crane (2019), *The Vector Heat Method*, ACM TOG** — connection-Laplacian **vector-field diffusion / parallel transport** via three prefactored Poisson-like solves. **Directly overlaps the paper's "vector field diffusion" physics channel.**
5. **Sorkine et al. (2004) / Lipman, Sorkine-Hornung, *Laplacian Framework for Interactive Mesh Editing*** — prefactored Laplacian for interactive differential-coordinate editing (the "factor once, edit interactively" template).
6. **Bouaziz, Martin, Liu, Kavan, Pauly (2014), *Projective Dynamics*, ACM TOG; Narain et al., *ADMM ⊇ Projective Dynamics*** — a **constant, prefactored system matrix reused every timestep** for physics; the strongest "prefactored operator reused across a whole simulation" precedent.
7. Brandt, Eisemann, Hildebrandt, *Hyper-Reduced Projective Dynamics* — subspace reuse of the prefactored operator.
8. Müller et al., *Position-Based Dynamics* (PBD) — unified constraint-projection physics (no shared Laplacian, contrast point).
9. Recent 3D-Gaussian-splat physics (arXiv:2606.21753, 2606.00444) — explicitly note that **"current physics engines are representation-agnostic; there is no native shared representation for rendering and physics"** — useful framing quote supporting the gap the paper fills.
10. Maggiorini et al., *Sulfur* (2014) — unified rigid/soft body representation (systems precedent, no Laplacian).

**Novelty verdict.** The idea of **reusing one prefactored Laplacian across many operations is established** (Heat Method, Vector Heat Method, Laplacian editing, Projective Dynamics all "factor once, reuse many"), and **DEC is an explicit "one operator family, many physics" program.** So "reuse a prefactored Laplacian" and "unify tasks under one operator" are **not novel in isolation** — this is the most crowded prior-art area and the reviewers most likely to push back are SGP/SCA people who know DEC cold. **The defensible novelty:** (a) the shared operator is an **ORC-weighted *graph* Laplacian** (not the geometric/cotangent/Hodge Laplacian of a fixed mesh) — an operator choice **nobody uses** for rendering+physics; (b) the *same cached LDLT* drives **both rendering importance/light and four distinct physics** (wave, heat, indefinite-Helmholtz via Bunch–Kaufman, vector diffusion) in **one interactive deterministic editor**; (c) the operator is **maintained under structural (DPO) topology change**, whereas DEC/Heat-Method/Projective-Dynamics assume a *fixed* mesh/topology and refactor from scratch on remesh. Frame the contribution as "one *dynamically-maintained* substrate under topology edits," which is the axis DEC does not address.

---

## Area 6 — ORC specifically as *edge weights for a Laplacian / diffusion / importance*

**Key references**

1. Münch & Wojciechowski, *Ollivier–Ricci curvature for general graph Laplacians* — curvature ↔ Laplacian on weighted graphs.
2. Ni et al. (2018/2019, above) — ORC/Ricci-flow as **adaptive edge weights** (the operational precedent for "curvature → weight").
3. *Lin–Lu–Yau Ricci Reweighting in the SBM* (arXiv:2603.11060) — assign each edge a weight from its empirical LLY curvature, then use the **normalized Laplacian of the reweighted graph** for spectral clustering. **Closest "curvature-weighted Laplacian" instance found.**
4. Topping et al. (2022), *Understanding over-squashing … curvature*; graph-rewiring-by-curvature literature — curvature drives edge modification for GNN diffusion.
5. *Neural Feature Geometry Evolves as Discrete Ricci Flow* (arXiv:2509.22362); DYMAG (arXiv:2309.09924) — curvature/diffusion coupling in learning.

**Novelty verdict.** "Curvature-weighted Laplacian" **exists** (LLY-reweighted normalized Laplacian for clustering; Ricci-flow edge weights), so the *concept* of ORC edge weights feeding a Laplacian is not new. But those uses are **spectral-clustering / GNN / network-analysis**; **no prior work uses ORC edge weights to build the operator that drives rendering *and* time-domain/frequency-domain physics.** The novelty is the *application and the shared-operator role*, not the weighting idea. Cite the LLY-reweighting and Ricci-flow-weight papers so reviewers see you know the weighting is not claimed as new.

---

## Area 7 — Deterministic bit-exact simulation (positioning)

**Key references**

1. Fiedler, *Floating Point Determinism* & *Deterministic Lockstep*, Gaffer On Games — canonical account: same inputs → bit-identical state, checksummable per frame; determinism achievable within one compiler + one ISA.
2. *Reproducible Floating-Point Aggregation in RDBMSs* (arXiv:1802.09883) — order-independent reproducible FP summation (relevant to making the Laplacian assembly order-independent → byte-identical).
3. Demmel & Nguyen, *reproducible BLAS / ReproBLAS* — reproducible summation primitives (worth citing for the assembly-determinism mechanism).
4. Lockstep RTS engines (mrdav30/LockstepRTSEngine); fixed-point physics essays — engineering practice context.

**Novelty verdict.** Bit-exact determinism is **standard engineering** in lockstep games and there is a reproducibility literature (ReproBLAS). Do **not** claim determinism itself as novel. The novel move is **using determinism as a *proof technique*:** byte-identical incremental-vs-full rebuild is a *correctness* guarantee for the incremental operator, cross-checked by the MV residual — determinism in service of a verifiable operator invariant, not just netcode sync. Cite Gaffer + ReproBLAS to show the machinery is understood and to preempt "determinism is trivial" reviews.

---

## Overall novelty assessment

**Is the core contribution defensible? Yes — as a systems/geometry-processing paper whose novelty is an *integration with a hard guarantee*, not any single new theorem.** Restated bluntly:

- **Already done (cite, don't claim):** ORC is local (Jost–Liu); ORC exact computation is a local matching problem with Ω(n) worst-case query bound (2208.09535); local/incremental Cholesky factor reuse under mesh edits (Herholz et al.); one prefactored Laplacian reused for many tasks (DEC, Heat Method, Vector Heat Method, Laplacian editing, Projective Dynamics); ORC as edge weights for a Laplacian (LLY reweighting, Ricci-flow weights); sheaf Laplacian ↔ cohomology (Hansen–Ghrist); MV for incremental obstruction reasoning (Young 2026); **incremental sheaf-cohomology maintenance under bounded local geometry with no drift (Volk 2026)**; bit-exact determinism (lockstep, ReproBLAS).
- **Not found in prior work (the paper's real contribution):** the *conjunction* — an **ORC-weighted graph Laplacian** as a **single shared substrate** for **rendering + multiple physics**, whose **DPO-triggered incremental rebuild is byte-identical to a full rebuild by construction**, **self-certified per edit by an MV residual == 0**, in a **deterministic interactive editor**. No single prior work spans even three of these axes together; none spans ORC + DPO + byte-exact-incremental + rendering-and-physics.

**Nearest prior art, ranked:** (1) **Volk 2026** (bounded-local-geometry incremental sheaf cohomology, no drift) — overlaps #2/#3 mathematically, differs in domain; **must cite**. (2) **Herholz–Alexa "Factor Once" / Herholz–Sorkine-Hornung** — overlaps #2 (local factor reuse), differs on byte-exactness + DPO. (3) **DEC / Vector Heat Method** — overlaps #4/#5 (one-operator-many-tasks, vector diffusion), differs on ORC-weighting + dynamic topology + rendering-physics union.

---

## Recommended venue fit

- **Primary: SGP (Symposium on Geometry Processing) → CGF.** Best match for the discrete-Laplacian machinery, the byte-identical-factorization guarantee, the cohomological certificate, and the mathematical rigor SGP rewards. The Herholz/DEC/Crane audience is exactly who should evaluate the incremental-operator and one-substrate claims. Risk: this audience knows DEC and prefactored reuse intimately, so the "one substrate is new" framing must be replaced by "one *dynamically-maintained* substrate under DPO topology change, byte-exact + certified."
- **Secondary: SCA (Symposium on Computer Animation).** Strong fit if the headline is the *physics* side (wave/heat/Helmholtz/vector diffusion sharing a solver) + interactive editor + determinism. SCA is more forgiving on the topology/rendering-unification framing.
- **Alternative: i3D.** Fits if the headline is the *real-time interactive engine* with undo/redo and deterministic reproducibility; weaker fit for the topology/cohomology depth.
- **JACT (J. Applied & Computational Topology):** only if the MV-residual certificate and the ORC-locality/non-localizability theory become the paper's center of gravity — but then Volk 2026 becomes a very direct competitor and the systems contribution is undersold. Not recommended as primary.

**Recommendation:** target **SGP/CGF** with a strong systems + determinism spine, SCA as fallback.

---

## The 3 biggest novelty risks and how to phrase claims

**Risk 1 — Volk 2026 (incremental sheaf cohomology under bounded local geometry, "zero drift") pre-empts contributions #2 & #3.**
This is the sharpest risk: an existing paper *already* does bounded-local-geometry incremental cohomology maintenance with a no-drift claim. Reviewers who know it will say "the incremental + certificate idea is done."
- *Phrasing fix:* Cite Volk 2026 (and Young 2026) explicitly and state the delta: "Prior work maintains abstract sheaf cohomology on evolving complexes for TDA/knowledge-graph consistency [Volk 2026] or program analysis [Young 2026]. We instead use the MV *residual* as a **runtime certificate that a locally-rebuilt numerical rendering/physics operator equals its global rebuild**, on the flag complex of an **ORC-weighted** graph, under **DPO** edits, inside a live deterministic engine." Do not claim "first incremental sheaf cohomology" or "first no-drift cohomology" — claim "first cohomological certificate guarding an operator rebuild in a graphics/physics system."

**Risk 2 — "Non-localizability of global ORC" (contribution #1) reads as contradicting the well-known locality of ORC, or as a mere LP-formulation artifact.**
Jost–Liu locality says the *value* depends only on local support, so a naive reader hears a contradiction; a sharp reader says "your drift is just a degenerate-LP / normalization artifact, not a finding."
- *Phrasing fix:* State it as a property of the *computed* quantity under a specific global formulation: "Under a W₁ LP posed over the global support with global normalization (Charnes-style perturbation), the optimal transport plan is non-unique and the solver's floating-point selection depends on global problem size; hence far-field edge curvatures are **not byte-reproducible** under edits, drifting ~1e-10. This is not a contradiction of the known *mathematical* locality [Jost–Liu]; it is why a byte-exact engine cannot use the global formulation. κ_R restricts the transport to B_R, recovering byte-identical values *by construction*." Frame #1 as the *motivation* for #2, not a standalone theorem. Cite 2208.09535 (local matching / query lower bounds) as backdrop.

**Risk 3 — "One prefactored Laplacian for many tasks" and "local factorization updates" both have heavy prior art (DEC, Heat/Vector-Heat Method, Projective Dynamics; Herholz "Factor Once").**
The SGP audience will feel the one-substrate and incremental-factor ideas are old.
- *Phrasing fix:* Concede both lineages up front and pivot to the two axes they don't cover: **(a) dynamic topology** — "DEC/Heat-Method/Projective-Dynamics assume fixed mesh/topology and refactor from scratch on structural change; we maintain the operator *across* DPO topology edits"; and **(b) byte-identical guarantee** — "Herholz et al. optimize local-refactor *speed with bounded residual*; we guarantee the incremental factor is **bit-identical** to a full rebuild and *prove it per-edit* via the MV residual." Never write "first to reuse a prefactored Laplacian" or "first local Laplacian update."

**One-line positioning to use in the intro:** *"We do not introduce a new curvature, a new Laplacian, or a new incremental factorization; we show that an ORC-weighted graph Laplacian can serve as a single, dynamically-maintained substrate for both rendering and several physics solvers, and that under DPO structural edits this substrate can be rebuilt locally, byte-identically, and self-certified at runtime by a Mayer–Vietoris residual — turning determinism into a correctness proof for a live, editable engine."*

---

## Consolidated key citations (starter .bib targets)

- Ollivier 2009 (J. Funct. Anal.); Jost & Liu 2014 (arXiv:1103.4037); "Local algorithms / fine-grained reductions for discretized Ricci curvature" (arXiv:2208.09535); Ni–Lin–Luo–Gao 2019 (Sci. Rep. 9:9984); LLY Ricci reweighting (arXiv:2603.11060); Loisel–Romon 2014 (arXiv:1402.0644).
- Herholz & Alexa 2018 *Factor Once* (ACM TOG); Herholz–Davis–Alexa 2017 *Localized Solutions* (ACM TOG); Herholz & Sorkine-Hornung 2020 *Sparse Cholesky Updates* (ACM TOG); dynamic Laplacian solvers (arXiv:1909.06413).
- Hansen & Ghrist 2019 (arXiv:1808.01513); Bodnar et al. 2022 *Neural Sheaf Diffusion* (NeurIPS); **Volk 2026 (arXiv:2606.04227)**; **Young 2026 (arXiv:2603.27015)**; Ó Conghaile *Cohomological k-consistency*.
- Ehrig et al. *Fundamentals of Algebraic Graph Transformation* 2006; Jerboa (Belhaouari et al.); PBPO+ (arXiv:2203.01032).
- de Goes–Crane–Desbrun–Schröder 2013 (DEC course); Elcott et al. 2007 (simplicial fluids); Crane et al. 2013 (Heat Method); Sharp–Soliman–Crane 2019 (Vector Heat Method); Sorkine/Lipman Laplacian editing; Bouaziz et al. 2014 (Projective Dynamics).
- Fiedler, *Floating Point Determinism / Deterministic Lockstep* (Gaffer On Games); Demmel–Nguyen ReproBLAS; reproducible FP aggregation (arXiv:1802.09883).

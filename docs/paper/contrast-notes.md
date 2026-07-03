# Contrast notes: Volk 2026 & Young 2026 vs. our submission

Prepared for the related-work section of *One Substrate: Byte-Exact Local
Maintenance of a Curvature-Weighted Graph Laplacian for Interactive Rendering
and Physics* (target SGP/CGF). Our contributions, for reference: (a) byte-exact
localized rebuild of an Ollivier-Ricci-weighted graph Laplacian Δ under DPO
scene-graph rewrites (bounded-support κ_R over 2-hop balls), and (b) a runtime
Mayer-Vietoris residual certificate over the visibility sheaf on the graph's
flag complex, guarding each localized rebuild inside a live, deterministic,
interactive editor that also runs wave/heat/Helmholtz/vector-diffusion physics
on the same operator.

---

## ⚠ RISK ESCALATION (read first)

**Volk 2026 overlaps us more than "incremental sheaf cohomology for TDA"
implies.** Three specific findings from the full text change our exposure:

1. **Volk maintains a numerical Laplacian-derived quantity, not just an
   abstract Betti number.** The method explicitly builds the sheaf 0-Laplacian
   "L_ℱ = (δ⁰)ᵀδ⁰" and reads H⁰/H¹ dimensions from the multiplicity of its zero
   eigenvalue. So the naked claim "no prior work incrementally maintains a
   Laplacian-derived quantity with exactness" is **false** — Volk does exactly
   that. *However:* Volk's Laplacian is the **sheaf** Laplacian used as an
   internal cohomology-computation device; it is **not** curvature-weighted
   (no Ollivier-Ricci / metric weighting — the "weights" are sheaf restriction
   maps), and it is never used to render or to run physics. Our "rendering/
   physics operator" qualifier survives, but the generic "Laplacian" claim does
   not.

2. **Volk already claims bit-exactness / zero-drift for the maintained
   cohomology.** Theorem 3.10: "the drift Δ(t) = h¹_inc(t) − h¹_batch(t) is
   exactly zero for all t"; the abstract says the state "agrees exactly with
   batch recomputation" with "zero measured drift," and Remark 3.11 calls it
   "bit-exact in the algebraic RAM model." **Consequence: we must NOT claim
   "first bit-exact / no-drift incremental cohomology (or Laplacian)
   maintenance."** That flag is planted. Our exactness novelty is narrower and
   must always be qualified: byte-exactness of a *curvature-weighted rendering/
   physics operator rebuild*, under *DPO structural rewrites*, in a *live
   engine*. (This is exactly why the team lead's phrasing rule exists — the
   escalation confirms it is mandatory, not stylistic.)

3. **Volk has a runtime consistency check that is functionally certificate-
   like.** Section 8.4 (Contradiction Localization): an injected "structural
   contradiction ... at V=5×10⁶ is detected by recomputing one cell out of
   25,473 (0.0039% of the complex)." This is the closest prior mechanism to our
   MV residual. *The role differs and this is our real line of defense:* Volk's
   check **re-verifies the maintained cohomology against its own batch value**
   (same object, cheap local recomputation); our MV residual is an **external
   cohomological witness certifying a *different* numerical operator** (the ORC
   Laplacian rebuild), never recomputed from batch. We must state this contrast
   explicitly rather than lean on "nobody certifies at runtime."

**Net effect on our claims.** Retire any "first incremental sheaf cohomology,"
"first no-drift cohomology," or "first bit-exact incremental Laplacian" phrasing
(Volk). Retire "first to use cohomology / Mayer-Vietoris as an obstruction
certificate" (Young — see below; Young already frames H¹ and MV that way). The
**defensible** claim is the conjunction, none of whose four qualifiers appears
in either paper: *first cohomological (Mayer-Vietoris) certificate guarding a
**curvature-weighted rendering/physics** operator's **localized rebuild** under
**DPO scene-graph edits** inside a **live, deterministic, interactive engine**.*
The draft's current one-liner (draft.md §1, contribution 3: "Volk 2026 maintains
abstract sheaf cohomology for TDA") **understates Volk and should be rewritten**
to acknowledge the sheaf Laplacian, the exactness claim, and the contradiction-
localization check.

**Young 2026: no escalation, but one correction.** Young is clean prior art
(symbolic program analysis, no numerical operator, no geometry). But note Young
*already* (i) treats H¹ as an obstruction certificate and (ii) uses Mayer-
Vietoris for incremental/compositional analysis. So we cannot claim either idea
as new in the abstract sense — only its application target and setting.

---

## 1. Paper summaries

### 1.1 Volk 2026

**Bibliographic (as visible on arXiv).**
Jason L. Volk. "Incremental Sheaf Cohomology on Cellular Complexes: O(1)-in-n
Lazy Edit Processing under Bounded Local Geometry." arXiv:2606.04227
[cs.DS] (cross-list cs.AI). Submitted 2 June 2026 (v1); revised 6 June 2026
(v2). https://arxiv.org/abs/2606.04227 . Code reported available on GitHub.

**Technical summary.** Volk gives an algorithm for the *incremental maintenance*
of first sheaf cohomology "H¹(X;ℱ)" on a dynamically evolving 1-dimensional
cellular complex (a graph) carrying a finite-dimensional cellular sheaf (stalks
are real vector spaces of dimension ≤ d, restriction maps are linear). Classical
batch computation via factoring the coboundary costs O(n³), and re-running it per
edit over a stream of m edits costs O(mn³). Under a "bounded local geometry"
assumption — Definition 2.9, parameters (v_max, d, D): each cell has ≤ v_max
vertices, every stalk has dimension ≤ d, and each cell is adjacent to ≤ D others
in the nerve — each edit "affects only a bounded set of local coboundary
blocks," and Theorem 3.3 processes an edit in "O(v_max³·d³) time ... independent
of the total complex size n" (the lazy per-edit update itself is O(d²), i.e. the
title's "O(1)-in-n"). The supported edits are streaming vertex insertion, intra-
and cross-cell edge insertion, and restriction-map update. The maintained value
is derived from the sheaf 0-Laplacian "L_ℱ = (δ⁰)ᵀδ⁰" via the multiplicity of
its zero eigenvalue, with a "Purity Gate" (Def 5.1) bounding restriction-map
spectral norm, "σ_max(ρ) ≤ ρ_max." Crucially, the guarantee is exactness, not
just amortized speed: Theorem 3.10 states the drift "h¹_inc(t) − h¹_batch(t) is
exactly zero for all t," reported as "zero measured drift" up to V = 5×10⁶
vertices / 1.7×10⁷ edits at "35 μs median" update latency, and Remark 3.11 calls
it "bit-exact in the algebraic RAM model." Section 8.4 adds a runtime
consistency check: an injected structural contradiction is localized by
"recomputing one cell out of 25,473." There is **no** rendering, physics,
curvature weighting, or interactive-editor context — it is an offline streaming-
algorithms result (cs.DS).

### 1.2 Young 2026

**Bibliographic (as visible on arXiv).**
Halley Young (Microsoft Research). "Sheaf-Cohomological Program Analysis:
Unifying Bug Finding, Equivalence, and Verification via Čech Cohomology."
arXiv:2603.27015 [cs.PL]. Submitted 27 March 2026.
https://arxiv.org/abs/2603.27015 .
*Note:* the team-lead brief's title ("Sheaf-Cohomological Program Analysis via
Čech Cohomology") is a paraphrase; cite the full title and author "Halley Young"
above.

**Technical summary.** Young recasts three program-analysis tasks — type
checking, bug finding, and equivalence verification — as "computing the Čech
cohomology of a semantic presheaf over a program's site category." The site
category 𝐒_P has five site kinds (ArgBoundary, BranchGuard, CallResult,
OutBoundary, ErrorSite); the presheaf assigns refinement-type predicates
(complete lattices, computations over 𝔽₂) to sites and restricts along data-flow
morphisms. In this framing "H⁰" is "the space of globally consistent typings"
and "H¹ classifies gluing obstructions — bugs, type errors, and equivalence
failures — each localized to a specific pair of disagreeing sites." The headline
algebraic results are: "rk Ȟ¹ over 𝔽₂ equals the minimum number of independent
fixes needed to resolve all errors"; a sound-and-complete cohomological
criterion for behavioral equivalence; and a Mayer-Vietoris sequence giving
"compositional obstruction counting, enabling incremental analysis" — e.g.
"rk Ȟ¹(𝒰) = rk Ȟ¹(𝒜) + rk Ȟ¹(ℬ) − rk Ȟ¹(𝒜∩ℬ) + rk im(δ)," supporting an "exact
incremental update" when one sub-cover is modified. The tool **Deppy** is
evaluated on 375 Python benchmarks (133 bug-detection, 134 equivalence pairs,
108 spec checks): 100% bug-detection recall at 69% precision (F1 = 81%), 99%
equivalence accuracy with zero false equivalences, and 98% specification
accuracy, "outperforming" mypy and pyright, "which report zero findings on
unannotated code." It is a **symbolic, primarily static/offline** analysis with
**no** numerical operator, Laplacian, curvature, rendering, physics, or geometry;
its "incremental" mode operates on *code edits between analysis runs*, not at
engine runtime.

---

## 2. Delta table (honest, axis-by-axis)

| Axis | Volk 2026 | Young 2026 | This work |
|---|---|---|---|
| **Domain** | Streaming/dynamic algorithms; sheaf-theoretic TDA (cs.DS/cs.AI) | Programming-language analysis via Čech cohomology (cs.PL) | Interactive graphics + geometry processing + physics (SGP/CGF) |
| **Object maintained / computed** | First sheaf cohomology H¹(X;ℱ) of a cellular sheaf on a 1-D complex; read off the **sheaf** Laplacian L_ℱ=(δ⁰)ᵀδ⁰ kernel | Čech Ȟ⁰/Ȟ¹ of a semantic presheaf over a program site category (over 𝔽₂) | An **Ollivier-Ricci-curvature-weighted graph Laplacian Δ** (rendering + 4 physics solvers); cohomology is *not* the maintained object |
| **Edit model** | Streaming vertex/edge insertion & restriction-map update; "lazy" per-edit processing | Code edits **between** analysis runs; MV re-uses one modified sub-cover | **DPO scene-graph rewrites** (structural add/remove) at interactive runtime, with undo/redo |
| **Guarantee type** | **Bit-exactness** (Thm 3.10, "drift ... exactly zero"; "bit-exact in the algebraic RAM model") **and** O(1)-in-n amortized cost | Soundness/completeness of the algebraic characterization; "exact incremental update" of a rank; empirical accuracy | **Byte-exactness** of the Δ rebuild (local rebuild ≡ full rebuild, by construction) **plus** a per-edit certificate; constant dirty-set as mesh grows |
| **Role of the cohomology** | Cohomology **is the deliverable** being maintained; a cheap single-cell recomputation *re-verifies that same deliverable* (§8.4) | Cohomology **is the deliverable** (H¹ = obstructions; rk H¹ = min fixes); MV = compositional/incremental accounting of it | Cohomology is a **certificate guarding a separate numerical operator** — an MV residual (== 0) over the visibility sheaf's U/K/R cover witnesses the Δ decomposition; Δ, not cohomology, is the deliverable |
| **Numerical operator?** | Yes, but the **sheaf** Laplacian, **uncurved**, used only to extract Betti dimensions; no rendering/physics | **None** | Yes — a **curvature-weighted** Laplacian driving light/importance + wave/heat/Helmholtz/vector-diffusion |
| **Curvature weighting?** | No | No | Yes (bounded-support κ_R, R=2, over 2-hop balls) |
| **Runtime certification?** | Contradiction localization by single-cell **batch recomputation** of the *same* object (§8.4) | No runtime notion; H¹ *is* the static obstruction report | **Mayer-Vietoris residual** as an online witness for *each* localized rebuild of a *different* operator, ~1 ms overhead |
| **System context** | Offline streaming algorithm + benchmark harness | Static analyzer (Deppy) run on Python source | **Live, deterministic, bit-reproducible interactive editor** (undo/redo, TLA+-mirrored invariants, byte-exact CI oracles) |

---

## 3. Camera-ready contrast paragraphs

*(LaTeX-free; ~120 words each; phrased to claim only the defensible conjunction.)*

**On Volk 2026.** Volk [2026] is the closest algorithmic precedent for
incremental cohomology maintenance, and we do not claim priority over it. His
method maintains first sheaf cohomology on a streaming one-dimensional complex in
time independent of complex size under bounded local geometry, and — beyond an
amortized-cost result — proves the maintained state "agrees exactly with batch
recomputation," reporting "zero measured drift" through five million vertices; it
even assembles the sheaf Laplacian (δ⁰)ᵀδ⁰ and localizes injected contradictions
by recomputing a single cell. Our contribution is orthogonal in what the
cohomology is *for*. Volk re-verifies a maintained cohomological quantity against
its own batch value in an offline stream. Our Mayer-Vietoris residual is instead
an external witness certifying the byte-exact rebuild of a *curvature-weighted
rendering-and-physics* Laplacian under DPO scene-graph edits inside a live,
deterministic editor — a different object, a different guarantee, a live setting.

**On Young 2026.** Young [2026] recasts program analysis as Čech cohomology of a
semantic presheaf over a program's site category, where H⁰ is "the space of
globally consistent typings" and H¹ "classifies gluing obstructions ... each
localized to a specific pair of disagreeing sites," with rk H¹ over 𝔽₂ counting
the "minimum number of independent fixes," and a Mayer-Vietoris sequence
supplying "compositional obstruction counting, enabling incremental analysis."
Because that work already treats cohomology as an obstruction certificate and
already uses Mayer-Vietoris for incrementality, we claim neither idea as novel in
the abstract. What is new here is the target and the setting: an MV residual over
a visibility sheaf on a flag complex that guards a numerical rendering-and-
physics operator's localized rebuild at interactive rates, rather than
statically counting bugs in unannotated Python between analysis runs.

---

## 4. Provenance / confidence

Summaries and every quoted phrase are drawn from the arXiv abstract pages and the
rendered HTML full text (arxiv.org/html/2606.04227v2 and /2603.27015),
cross-checked against web search and, for Young, the Microsoft Research
publication listing. Theorem/definition/section numbers (Def 2.9, Thm 3.3, Thm
3.10, Rem 3.11, §8.4 for Volk; the site kinds and MV rank formula for Young) are
as they appear in the HTML render. **Before camera-ready, re-confirm the exact
quotes and numbers against the official PDFs** — HTML renders can differ slightly
in symbol formatting (e.g., subscripts, 𝔽₂, δ⁰), and Volk's v2 numbering could
shift in a later revision.

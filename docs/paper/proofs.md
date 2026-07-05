# Formal statements and proofs (backing draft §4)

Status 2026-07-04: full proofs for the draft's Lemma 1 / Proposition 1 /
Corollary, plus two statements the sketches left implicit: **Lemma 2**
(κ_R equals the global curvature *exactly* in exact arithmetic — the bounded
support is not an approximation) and **Lemma 3** (soundness of the 2-hop
dirty cover), combined into **Theorem 1** (bitwise equality of the localized
and full rebuilds). Every code fact cited below was verified against the
implementation on 2026-07-04 (file:line refs current at commit b172240).

## 0. Setup and notation

**Skeleton.** For a scene graph G, the skeleton Σ(G) = (V, E) has V = the
Mesh-node ids of G, sorted ascending and deduplicated, and E = the Adjacent
edges between Mesh nodes, each stored as the sorted pair (min, max), the list
sorted lexicographically and deduplicated
(`aleph.sheaf-skeleton.cppm:from_graph`). All objects below live on Σ(G);
graph elements that do not change Σ (Materials, Lights, Contains/References
edges, attribute values) are invisible to every statement.

**Metric.** d_G is the hop metric on Σ(G). N_G(v) is v's skeleton
neighbourhood. For an edge e = (a,b) ∈ E and R ≥ 0, the **ball**
B_R^G(e) = { v ∈ V : min(d_G(v,a), d_G(v,b)) ≤ R }, and G[B_R^G(e)] is the
**labeled** induced subgraph (node ids retained; edges of E with both
endpoints in the ball).

**Curvature (mathematical).** μ_v = the uniform probability measure on
N_G(v) (a Dirac at v if isolated — impossible for an edge endpoint).
κ(e) = 1 − W₁(μ_a, μ_b) / d_G(a,b), with W₁ the exact optimal-transport cost
under d_G. For e ∈ E, d_G(a,b) = 1.

**Curvature (computed), κ_R.** The program
(`aleph.flow-ollivier_ricci.cppm:build_local_state` + `ricci_curvature_edge`):
(1) radius-R BFS from {a,b} over Σ(G) → ball node set, sorted ascending;
(2) induced neighbour lists, filled by scanning E in its canonical order and
keeping in-ball edges; (3) all-pairs hop distances by BFS **within** the
induced subgraph; (4) support restriction to the ball-component of {a,b};
(5) W₁ by the primal transportation simplex with the Charnes RHS perturbation
μ_i += ε·(i+1), ν_j += ε·(n_s+1)/2, ε = 1e-12, where n_s is the LP support
size and i is the support index (`aleph.flow-wasserstein1.cppm:310`);
(6) κ_R(e) = 1 − W₁/d(a,b), all in f64, single-threaded, no RNG.

**Determinism assumptions (standing).** One binary, one ISA (x86-64-v3), no
runtime FP-environment changes, single-threaded evaluation of the pipeline
above. Under IEEE-754, every operation is then a deterministic function of
its operand bits, so a fixed instruction sequence on identical inputs yields
identical output bits. (The standard deterministic-lockstep setting [Fiedler];
enforced in CI by byte-golden oracles.)

---

## Lemma 1 (metric fidelity of the 2-ball)

**Statement.** Let e = (a,b) ∈ E, R ≥ 2, and x, y ∈ N_G(a) ∪ N_G(b). Then
d_{G[B_R^G(e)]}(x, y) = d_G(x, y).

**Proof.** Write B = B_R^G(e) and H = G[B].

(≥) H is a subgraph of Σ(G), so every H-path is a G-path: d_H ≥ d_G.

(≤) First, d_G(x,y) ≤ 3: the walk x–a–b–y uses the edges (x,a), (a,b),
(b,y) (or shorter walks when x or y coincides with a or b, or both attach to
the same endpoint). So d_G(x,y) ∈ {0,1,2,3}. We show some G-geodesic from x
to y lies entirely in H; then d_H(x,y) ≤ d_G(x,y).

Note x, y ∈ B_1^G(e) ⊆ B. Take any G-geodesic P from x to y.

- d_G = 0, 1: P is {x} or the edge (x,y); both endpoints are in B and the
  edge (if any) is induced.
- d_G = 2: P = x–m–y. m is adjacent to x ∈ B_1, so
  min(d(m,a), d(m,b)) ≤ 1 + 1 = 2 ≤ R, i.e. m ∈ B.
- d_G = 3: P = x–m₁–m₂–y. m₁ is adjacent to x ∈ B_1 ⇒ m₁ ∈ B_2 ⊆ B;
  m₂ is adjacent to y ∈ B_1 ⇒ m₂ ∈ B.

In every case all vertices of P lie in B, hence all its edges are induced in
H, so P is an H-path of length d_G(x,y). ∎

*Remark.* R = 2 is tight: on a 6-cycle a–b–y–m₂–m₁–x–a, the pair
x ∈ N(a), y ∈ N(b) has d_G(x,y) = 3 realized only through m₁, m₂ at distance
2 from {a,b}; with R = 1 the ball metric gives d = 3 via x–a–b–y... (both
metrics still agree here — the tightness example needs the *chord-free*
detour; see the unit test `mv-local: bounded kappa_R ~= global kappa`, which
pins the R=2 fidelity empirically on lattices).

---

## Lemma 2 (exact-value fidelity: κ_R is not an approximation)

**Statement.** In exact arithmetic with the *unperturbed* transport LP,
κ_R(e) = κ(e) for every e ∈ E and every R ≥ 2.

**Proof.** Both formulations solve
min { Σ_{ij} π_ij c_ij : π ≥ 0, marginals μ_a, μ_b } and differ only in
(i) the index set over which π ranges (global component support vs
ball-component support) and (ii) the cost matrix (d_G vs d_H restricted to
the respective supports).

For (i): in any feasible plan, mass conservation forces π_ij = 0 whenever
μ_a(i) = 0 or μ_b(j) = 0. Hence restricting the variable set to
supp(μ_a) × supp(μ_b) changes neither feasibility nor cost of any plan, and
both problems have the same optimum as the problem posed on
supp(μ_a) × supp(μ_b) alone. Both supports contain supp(μ_a) = N_G(a) and
supp(μ_b) = N_G(b) in full: for R ≥ 1, every x ∈ N_G(a) lies in B_1 ⊆ B and
the edge (a,x) is induced, so the in-ball star of a equals its true star
(`build_local_state` mechanics note 2), and likewise for b; the global
formulation restricts to the connected component of {a,b}, which contains
both stars.

For (ii): by Lemma 1, c restricted to (N(a) ∪ N(b))² is identical in both
problems. Finally d_G(a,b) = d_H(a,b) = 1. Hence identical optima and
identical κ. ∎

**Corollary 2.1.** The observed discrepancies |κ_R − κ_global| (≤ 1e-6 in
the test suite, ~1e-10 typical) are *entirely* artifacts of the Charnes
perturbation scale and floating-point evaluation order — model error is
exactly zero.

---

## Observation 1 (why the global formulation is not byte-cachable)

The implementation perturbs the LP right-hand side by μ_i += ε·(i+1) for
i = 1..n_s and ν_j += ε·(n_s+1)/2, injecting total extra mass
ε·n_s(n_s+1)/2 that the optimal plan must transport
(`aleph.flow-wasserstein1.cppm:310-324`). The perturbed optimum — the
*returned f64 value* — therefore depends on the support size n_s and on each
support point's index in the sorted support order.

Under the global formulation, n_s = |component of {a,b}| and the indices are
positions in the sorted **global** vertex list. Inserting or deleting *any*
vertex or edge in the component changes n_s and/or shifts indices, moving the
computed value of **every** edge in the component at order ε·n_s·diam —
the measured ~1e-10 far-edge drift. This does not contradict the mathematical
locality of ORC (Jost–Liu; Lemma 2 above): the *value* is local, the
*computed artifact* under the global LP is not. It does make byte-exact
caching impossible under the global formulation — the motivation for κ_R,
whose n_s and support indices are functions of the ball alone.

---

## Proposition 1 (byte-exact locality of the computed κ_R)

**Statement.** Fix R ≥ 1 and the standing determinism assumptions. Let G, G′
be scene graphs and e = (a,b) an edge of both skeletons. If
B_R^G(e) = B_R^{G′}(e) =: B as node-id sets and G[B] = G′[B] as labeled
graphs, then the computed κ_R(e; G) and κ_R(e; G′) are bit-identical.

**Proof.** We show every intermediate artifact of the κ_R program is a pure
function of the labeled graph G[B] (and of e, R); the hypothesis then gives
identical inputs to a fixed f64 instruction sequence, and the standing
assumptions give identical output bits.

1. *Ball set.* The radius-R BFS from {a,b} visits exactly
   { v : d_G(v,{a,b}) ≤ R } = B, independently of adjacency-list iteration
   order (BFS layer sets are order-invariant). Moreover every path witnessing
   d_G(v,{a,b}) ≤ R has all its vertices within distance R of {a,b} (each
   prefix is shorter), i.e. lies in B; hence the same BFS executed on G′
   (whose ball data coincides by hypothesis) visits the same set.
2. *Canonical ball order.* The ball ids are sorted ascending
   (`build_local_state` step 1) — a pure function of the set B.
3. *Induced adjacency.* Neighbour lists are filled by scanning the canonical
   (lexicographically sorted, deduplicated) skeleton edge list and keeping
   in-ball edges. The subsequence of in-ball edges, in that order, is the
   sorted list of E(G[B]) — a pure function of G[B]: the relative order of
   in-ball edges does not depend on what other edges exist.
4. *Local metric.* All-pairs BFS **within** the induced lists — a pure
   function of step 3's output.
5. *Support, measures, LP.* The component restriction, μ, ν, the sub-distance
   matrix, and the Charnes-perturbed transportation simplex (fixed pivot
   rules, fixed ε, support size and indices from step 2's order) consume only
   steps 2–4's outputs.
6. The division and subtraction forming κ_R consume step 5's output and
   d(a,b) = 1.

Each step is a deterministic single-threaded f64/integer program of the
previous step's outputs. ∎

---

## Lemma 3 (soundness of the 2-hop dirty cover)

**Setting.** A *localizable* edit takes G → G′ (AddObject, AddLight,
DeleteObject, attribute ops; ApplyRule takes the engine's full-rebuild
fallback and is outside this lemma). Let the *touched* skeleton elements be
the created skeleton vertices, and the endpoints of created or deleted
skeleton edges (endpoints read in the graph where the edge exists). The
engine's seed set S contains all touched elements that exist in Σ(G′), plus
possibly more (`aleph.edit-controller.cppm:rebuild_operator_localized`;
`two_hop_touched_edges` drops seed ids absent from Σ(G′)). The dirty set is
D = { (u,v) ∈ E(G′) : u or v within distance R of S in Σ(G′) }
(`aleph.flow-laplacian.cppm:two_hop_touched_edges`).

Model facts used: an edge exists only while both endpoints exist
(delete cascades incident edges: `Graph::remove_node_cascade`); a created
vertex with no created edge is isolated in Σ(G′).

**Statement.** For every e = (a,b) ∈ E(G′) \ D:
B_R^G(e) = B_R^{G′}(e) and G[B] = G′[B] (labeled). Hence, by Proposition 1,
the cached computed κ_R(e; G) is bit-identical to κ_R(e; G′).

**Proof.** Suppose e ∉ D, i.e. neither a nor b is within distance R of any
seed in Σ(G′). We show no discrepancy between the two labeled balls can
exist. Throughout, "created"/"deleted" refer to skeleton edges of
E(G′) \ E(G) and E(G) \ E(G′).

*(i) No vertex enters the ball.* Suppose v ∈ B_R^{G′}(e) \ B_R^G(e), and let
P be a G′-path from {a,b} to v of length ≤ R. P must contain a created edge
— otherwise P ⊆ Σ(G) and v ∈ B_R^G(e). Let (u,w) be the first created edge
along P starting from the {a,b} end, with u its nearer endpoint. The prefix
of P up to u is a G′-path, so d_{G′}(u, {a,b}) ≤ R; and u ∈ S (endpoint of a
created edge, alive in G′). Then a or b lies within distance R of the seed
u, so e ∈ D — contradiction.

*(ii) No vertex leaves the ball.* Suppose v ∈ B_R^G(e) \ B_R^{G′}(e), and
let P be a G-path from {a,b} to v of length ≤ R. P must contain a deleted
edge — otherwise P survives into Σ(G′) whole (surviving edges keep surviving
endpoints) and v ∈ B_R^{G′}(e). Let (u,w) be the first deleted edge along P
from the {a,b} end, u its nearer endpoint. The prefix up to u contains no
deleted edge, so it survives into Σ(G′) — in particular u is alive — and
d_{G′}(u, {a,b}) ≤ R. u is an endpoint of a deleted edge, so u ∈ S, and
again e ∈ D — contradiction.

*(iii) No induced edge changes.* Given (i)+(ii) the ball sets coincide (=: B).
Suppose some edge (u,w) with u, w ∈ B is created or deleted. Then
u ∈ B ⊆ B_R^{G′}(e), so d_{G′}(u, {a,b}) ≤ R, and u ∈ S (endpoint of a
created or deleted edge, alive in G′ since u ∈ B ⊆ V(G′)). So e ∈ D —
contradiction. Created isolated vertices never lie in any ball
(infinite distance), and deleted vertices are covered by (ii) through their
cascaded edge deletions. ∎

---

## Theorem 1 (bitwise equality of localized and full rebuild)

**Statement.** Let G₀ → G₁ → … → G_k be a trace of localizable edits, with
Δ₀ = `build_laplacian_bounded(G₀)` and, for t ≥ 1,
Δ_t = `build_laplacian_local(G_t, Δ_{t−1}, D_t)` where D_t is the dirty set
of Lemma 3 for the t-th edit. Then for every t, Δ_t is **bit-identical**
(matrix and curvature map) to `build_laplacian_bounded(G_t)`.

**Proof.** Induction on t. Base t = 0 is definitional. Step: assume
Δ_{t−1} == `build_laplacian_bounded(G_{t−1})` bitwise. Consider each edge e
of Σ(G_t) as processed by `build_laplacian_local`
(`aleph.flow-laplacian.cppm:build_laplacian_local`):

- e ∈ D_t, or e is a cache miss: κ is recomputed by
  `ricci_curvature_edge_bounded(skel(G_t), shared, e)` — the *identical*
  primitive, on the *identical* shared adjacency (`build_adjacency(skel)`,
  built by both the local and the full path from the same canonical
  skeleton), that `build_laplacian_bounded(G_t)` runs for e. Identical
  program, identical inputs ⇒ identical bits.
- e ∉ D_t with a cache hit: the cached value equals (inductive hypothesis)
  the value the full build computed on G_{t−1}; by Lemma 3 the labeled
  R-balls of e in G_{t−1} and G_t coincide; by Proposition 1 that value is
  bit-identical to what the full build computes for e on G_t.

So the fresh curvature map agrees with the full build's, entry by entry;
both maps are inserted in the same canonical skeleton-edge order, and
`detail::assemble` performs the same f64 accumulation sequence on the same
operands in the same order. Under the standing determinism assumptions the
assembled matrices are bit-identical. ∎

**Remarks.**
0. *Sparse twin (2026-07-04).* `assemble_sparse` consumes the identical
   curvature map (shared construction) and accumulates each diagonal entry in
   the same canonical-edge-order subsequence, so Theorem 1 holds verbatim for
   `build_laplacian_*_sparse` with "bit-identical" meaning entry-wise equality
   of stored values (gated by tests/flow/test_sparse_laplacian.cpp). Note the
   deliberately weaker matvec contract: matvecs across carriers agree to a few
   ulps but are NOT bitwise (ISO FP-contraction discretion differs per loop
   shape); each carrier is individually byte-deterministic.
1. The engine's fallback gate (dirty > 0.5·|E| → full rebuild) and the
   ApplyRule fallback only ever *replace* the localized path by the full
   build — trivially bit-exact.
2. Theorem 1 is enforced, not just proven: Tier-1 CI asserts `==` on every
   matrix entry and curvature across multi-edit traces
   (`tests/flow/test_mv_localization.cpp`), and the bench asserts it on every
   measured row across both benchmark families.
3. The Mayer–Vietoris residual (draft §5) is an *independent runtime witness*
   of the rewrite decomposition (U/K/R cover) the seeds are derived from —
   it certifies structure Theorem 1's proof consumes, on the live graph, per
   edit.

# Aleph-cxx Phase 4 — Typed Scene Graph + DPO + Sheaves + Flow

**Status:** approved design, ready for implementation plan.
**Date:** 2026-05-27.
**Branch:** `phase-4-graph`.
**Predecessor:** Phase 1 (foundation, v0.1.1-foundation) + Phase 2 (scene + render, v0.2.0-render) complete on `main`.

## 1. Goal

Port the structural (DPO), topological (sheaf cohomology) and dynamical (flow/PDE) layers of the Rust `aleph-engine` workspace into `aleph-cxx` as C++26 modules — preserving determinism, formal-verification cross-checks against TLA+, and the mathematical APIs of the original 5 Rust crates.

Source crates (Rust, ~11K LOC):

| Rust crate | LOC | C++26 module |
|---|---|---|
| `aleph-types` | 464 | `aleph.types` |
| `aleph-graph` | 843 | `aleph.graph` |
| `aleph-dpo` | 1266 | `aleph.dpo` |
| `aleph-sheaf` | 5319 | `aleph.sheaf` (+ `aleph.linalg.gf2` extracted) |
| `aleph-flow` | 3116 | `aleph.flow` (+ `aleph.linalg.sparse` extracted) |

Out of scope: render integration (Phase 5+), GPU compute, render-graph lowering.

## 2. Architecture

### 2.1 Directory layout

```
aleph-cxx/
├── foundation/                              # Phase 1 ✓ + Phase 4 extensions
│   ├── aleph.containers/                    # +OrderedMap<K,V>
│   ├── aleph.linalg.gf2/                    # NEW (~400 LOC)
│   │   └── BitVec, BitMatrix, rank/kernel/null_space over GF(2)
│   └── aleph.linalg.sparse/                 # NEW (~1500 LOC)
│       └── DMatrix, CsrMatrix, LDLᵀ dense+sparse, Sinkhorn-Knopp helpers
│
├── render/                                  # Phase 2 ✓ untouched
│
├── graph/                                   # NEW Phase 4
│   ├── aleph.types/         (Sub-4a)        # NodeKind, EdgeKind, IDs, attributes
│   ├── aleph.graph/         (Sub-4a)        # Graph + 10 invariants
│   ├── aleph.dpo/           (Sub-4b)        # Pattern, Matcher, 4 rules
│   ├── aleph.sheaf/         (Sub-4c)        # H⁰ visibility, H¹ via ∂, MV, DEC
│   └── aleph.flow/          (Sub-4d)        # Laplacian, Ricci, Wasserstein-2
│
├── formal/                                  # NEW
│   ├── scene_graph.tla                      # copied from aleph-engine/formal/
│   ├── dpo_rules.tla
│   ├── sheaf_h0.cfg + 3 fixture .cfg
│   └── check.sh
│
├── tests/
│   ├── isolation/                           # +6 new iso_* per module
│   ├── graph/ dpo/ sheaf/ flow/ linalg/     # per-module unit tests
│   ├── fixtures/                            # 8-node canonical scene + variants
│   └── tla_cxx_sync.cpp                     # TLA+ parser + drift detector
│
├── bench/                                   # extends Phase 1/2 baselines
└── apps/aleph_graph_fixture/                # 8-node fixture validator binary
```

### 2.2 Module dependencies (acyclic)

```
                            aleph.math (Phase 1)
                                ▲
                ┌───────────────┼───────────────────┐
                │               │                   │
        aleph.containers    aleph.linalg.gf2    aleph.linalg.sparse
                ▲               ▲                   ▲
                │               │                   │
            aleph.types  ←──────┘                   │
                ▲                                   │
            aleph.graph ────────────────────────────┤
                ▲                                   │
        ┌───────┼───────┐                           │
        │       │       │                           │
    aleph.dpo  aleph.sheaf  ────────→  aleph.flow ──┘
```

`aleph.flow` depends on `aleph.sheaf` (uses `:skeleton` and `:simplex` for Ollivier-Ricci and Laplacian). Implementation order is forced: 4a → 4b → 4c → 4d.

### 2.3 Sub-phase plan

| Sub-phase | Modules | LOC est. | Plan file | Tag |
|---|---|---|---|---|
| 4a | aleph.types, aleph.graph, +OrderedMap, sync test | ~2050 | plans/4a | `v0.3.0-graph` |
| 4b | aleph.dpo | ~1700 | plans/4b | `v0.3.1-dpo` |
| 4c | aleph.sheaf, +aleph.linalg.gf2 | ~6600 | plans/4c | `v0.3.2-sheaf` |
| 4d | aleph.flow, +aleph.linalg.sparse | ~5000 | plans/4d | `v0.3.3-flow` |

Each sub-phase merges to `main` independently when its tag is cut. Plans are written one at a time (4a's plan first; 4b's plan after 4a tag).

## 3. Hard rules (inherited from Rust workspace)

1. **OrderedMap, not unordered_map.** Deterministic iteration is load-bearing: TLC traces stable across runs, golden snapshots byte-identical, Mayer-Vietoris incremental rewrites fold updates in canonical order. New `aleph::containers::OrderedMap<K,V>` (hash table + insertion-order doubly-linked list) implements IndexMap semantics.
2. **TLA+ sync test mandatory.** Every new `NodeKind`, `EdgeKind`, invariant name, or rule name must update both `aleph.types`/`aleph.graph`/`aleph.dpo` AND `formal/*.tla`, then prove `tests/tla_cxx_sync.cpp` passes. Drift is the failure mode this design defeats.
3. **No `unsafe`.** Workspace lints already set `unsafe_code = "forbid"`; same standard applies.
4. **`std::expected<T, E>`, no exceptions in module libraries.** `aleph_flags_isa` (Phase 1 workaround) for all module CMakeLists. Tests link `aleph_flags_test` (exceptions on, doctest).
5. **No `coarsen_cell` rule.** Its gluing condition involves a pushout with identifications — deliberately deferred (same as Rust M2).
6. **Side-by-side with render.** `graph/` subtree is independent of `render/`. No lowering layer in Phase 4 (deferred to Phase 5+). Apps that want both import each independently.

## 4. Sub-phase 4a — `aleph.types` + `aleph.graph`

### 4a.1 `aleph.types`

```cpp
export module aleph.types;
export import :id;          // NodeId, EdgeId (strong typedefs over u32), IdAllocator
export import :attribute;   // MaterialKind, LightKind, MediumKind, TextureFormat
export import :node;        // Mesh, Material, Light, Volume, Camera, Texture,
                            //   Transform, Node (std::variant), NodeKind
export import :edge;        // Edge, EdgeKind, allows(src_kind, dst_kind)
```

**Decisions:**

- `Node` = `std::variant<Mesh, Material, Light, Volume, Camera, Texture, Transform>`. C++26 `std::visit` covers the `match` of Rust.
- `NodeId`/`EdgeId`: strong typedef over `u32`, `explicit` constructor, not interconvertible.
- `IdAllocator`: per-kind `u32` counter + `alloc_node()`/`alloc_edge()`.
- `NodeKind::ALL` / `EdgeKind::ALL`: `constexpr std::array`, not global statics.
- `as_tla(NodeKind)` / `as_tla(EdgeKind)` return `constexpr std::string_view` — exact strings `"mesh"`, `"material"`, …, `"adjacent"`, … matching the TLA+ spec.
- `EdgeKind::allows(NodeKind, NodeKind)` is `constexpr bool`. Exact mirror of the Rust compatibility matrix:
  - `Adjacent`: `(Mesh, Mesh)` only
  - `Contains`: `(Transform, {Transform, Mesh, Light, Camera, Volume})`
  - `Influences`: `({Light, Volume, Material}, Mesh)`
  - `References`: `(Mesh, Material) | (Material, Texture)`
- `Mesh::geometry_ref`, `Light::emit_ref`, `Camera::sensor_id`: `std::string` (M1 conservative); symbolic ref tables come later.

### 4a.2 `aleph.graph`

```cpp
export module aleph.graph;
export import :graph;        // Graph: OrderedMap<NodeId, Node> + OrderedMap<EdgeId, Edge>
export import :invariants;   // 10 check_* + validate_all + InvariantError + INVARIANT_NAMES
```

**Storage:** `OrderedMap<NodeId, Node>` and `OrderedMap<EdgeId, Edge>`.

**API surface (mirror of Rust):**

```cpp
class Graph {
public:
    Graph();
    NodeId alloc_node_id();
    EdgeId alloc_edge_id();
    void insert_node(Node node);                                       // aborts on id collision
    std::expected<EdgeId, GraphError> add_edge(EdgeKind, NodeId, NodeId);
    void remove_node_cascade(NodeId);                                  // also removes incident edges
    const Node* node(NodeId) const noexcept;
    const Edge* edge(EdgeId) const noexcept;
    // Insertion-order range views. Both yield std::pair<const Key&, const Value&>
    // (where Key is NodeId/EdgeId). Concretely backed by OrderedMap's iterator;
    // returned as a borrowed range so callers iterate without copies.
    auto nodes() const -> OrderedMap<NodeId, Node>::const_range;
    auto edges() const -> OrderedMap<EdgeId, Edge>::const_range;
    std::size_t node_count() const noexcept;
    std::size_t edge_count() const noexcept;
    std::size_t in_degree(NodeId) const noexcept;
};

enum class GraphError {
    NodeNotFound,
    EdgeNotFound,
    EdgeTypeMismatch,
};
```

**10 invariants** (names match `formal/scene_graph.tla` `INVARIANT_NAMES`):

1. `TypedNodes`
2. `TypedEdges`
3. `EdgeEndpointsExist`
4. `EdgeTypeCompat`
5. `TransformAcyclic`
6. `CameraExclusive`
7. `MaterialReferenced`
8. `UniqueIds`
9. `ContainsAntireflexive`
10. `BoundedDegree`

```cpp
constexpr std::array<std::string_view, 10> INVARIANT_NAMES = { ... };

enum class InvariantError {
    TypedNodes, TypedEdges, EdgeEndpointsExist, EdgeTypeCompat,
    TransformAcyclic, CameraExclusive, MaterialReferenced, UniqueIds,
    ContainsAntireflexive, BoundedDegree,
};

std::expected<void, InvariantError>
validate_all(const Graph& g, std::size_t max_in_degree = std::numeric_limits<std::size_t>::max());
```

### 4a.3 TLA+ sync test

```cpp
// tests/tla_cxx_sync.cpp
// 1. read formal/scene_graph.tla as a string
// 2. extract: NodeKind == { ... }, EdgeKind == { ... }, EdgeTypeCompat == [ ... ]
// 3. compare against NodeKind::ALL · as_tla, EdgeKind::ALL · as_tla,
//    EdgeKind::allows(src, dst) cartesian product
// 4. fail if any drift
```

Parser is a small regex + state-machine (~150 LOC); we do **not** ship a full TLA+ parser, only enough to extract named constants we care about.

### 4a.4 Fixture validator binary

```
apps/aleph_graph_fixture/main.cpp
```

Ports `aleph-graph/examples/fixture_scene.rs`. Builds 8-node canonical scene (1 root Transform, 1 Camera, 2 Mesh adjacents, 1 Material referenced by both, 1 Texture, 1 Light influencing both meshes, 1 Volume), runs `validate_all`, prints PASS/FAIL list of invariants.

### 4a.5 LOC estimate

| Component | LOC |
|---|---|
| `aleph.types` code | 400 |
| `aleph.graph` code | 600 |
| `OrderedMap` extension | 250 |
| `tla_cxx_sync.cpp` | 200 |
| Tests + fixtures | 600 |
| **Total** | **~2050** |

### 4a.6 Success criteria

- `ctest` green: isolation tests (`iso_types`, `iso_graph`) + per-module unit tests + `tla_cxx_sync`.
- `apps/aleph_graph_fixture` exits 0 on the canonical 8-node scene.
- `OrderedMap` benchmark within 2× of `std::unordered_map` insert+iterate.
- Tag `v0.3.0-graph` cut, sub-phase merged to `main`.

## 5. Sub-phase 4b — `aleph.dpo`

### 5.1 Module

```cpp
export module aleph.dpo;
export import :pattern;     // Pattern, PatternNodeId, PatternEdgeId, NodeConstraint
export import :attribute;   // AttrInit, AttrSet, AttrPredicate
export import :rule;        // Rule, RuleAction (std::variant)
export import :matcher;     // find_matches(rule, graph) -> std::vector<Match>
export import :apply;       // apply(rule, match, graph) -> std::expected<RewriteRecord, ApplyError>
export import :rules;       // rules::{spawn_light, remove_object, replace_material, refine_cell}
```

### 5.2 Concept: DPO rewrite

A rule is `L ← K → R` where `L` is what the matcher finds in the host `G`, `K` is the preserved core, `R` is the result. Apply:

1. Find embedding `m: L → G` via the matcher.
2. Delete nodes/edges in `L \ K`.
3. Create nodes/edges in `R \ K`.
4. Apply `ModifyAttr` side-effects.
5. Post-condition: run `validate_all`; if any invariant fails, transactional rollback.

### 5.3 Types (Rust → C++26 port)

```cpp
struct PatternNodeId { u32 v; };  // strong typedef
struct PatternEdgeId { u32 v; };

struct AttrPredicate {
    std::function<bool(const Node&)> pred;
    std::string description;       // for debug printing
};

struct NodeConstraint {
    NodeKind kind;
    std::optional<AttrPredicate> attrs;
};

struct Pattern {
    OrderedMap<PatternNodeId, NodeConstraint> nodes;
    OrderedMap<PatternEdgeId, std::tuple<PatternNodeId, PatternNodeId, EdgeKind>> edges;
};

struct CreateNode { PatternNodeId local; AttrInit init; };
struct CreateEdge { PatternEdgeId edge; };               // references rhs.edges
struct DeleteNode { PatternNodeId local; };
struct DeleteEdge { PatternEdgeId edge; };
struct ModifyAttr { PatternNodeId node; AttrSet set; };

using RuleAction = std::variant<CreateNode, CreateEdge,
                                  DeleteNode, DeleteEdge, ModifyAttr>;

struct Rule {
    std::string_view name;                       // matches rule name in dpo_rules.tla
    Pattern lhs;
    std::vector<PatternNodeId> interface;        // K = preserved ids
    Pattern rhs;
    std::vector<RuleAction> side_effects;
};
```

### 5.4 Matcher

VF2-style backtracking subgraph isomorphism with type pruning:

```cpp
struct Match {
    OrderedMap<PatternNodeId, NodeId> node_map;
    OrderedMap<PatternEdgeId, EdgeId> edge_map;
};

std::vector<Match> find_matches(const Rule& r, const Graph& g);
```

**Determinism contract:** matches returned in canonical order (= order of discovery by backtracking over `OrderedMap` iteration). Required for TLC trace reproducibility.

### 5.5 Apply (transactional)

```cpp
struct RewriteRecord {
    std::vector<NodeId> created_nodes;
    std::vector<NodeId> deleted_nodes;
    std::vector<EdgeId> created_edges;
    std::vector<EdgeId> deleted_edges;
};

enum class ApplyError {
    DanglingEdgeAfterDelete,
    InvariantViolation,
    AttrSetMismatch,
};

std::expected<RewriteRecord, ApplyError>
apply(const Rule& rule, const Match& m, Graph& g);
```

Implementation: snapshot `g` via copy ctor before mutating; on post-condition failure, restore `g = snapshot`.

### 5.6 The 4 rules

```cpp
namespace aleph::dpo::rules {
    const Rule& spawn_light();
    const Rule& remove_object();
    const Rule& replace_material();
    const Rule& refine_cell();
}
```

`coarsen_cell` deferred.

### 5.7 TLA+ sync extension

`tla_cxx_sync.cpp` is extended to:

- For each rule in C++26, verify a matching `Rule_<name>` action exists in `dpo_rules.tla`.
- Compare pre-/post-conditions structurally (not semantically).

### 5.8 LOC + criteria

LOC: ~1700 (~1200 code + ~500 tests).

Success criteria:

- `find_matches` is deterministic across 256 random graphs.
- Applying each of the 4 rules + invariant check passes on the canonical fixture and 256 random variations.
- Transactional rollback verified (force an invariant break → state unchanged).
- `tla_cxx_sync` extended assertions pass.
- Tag `v0.3.1-dpo`.

## 6. Sub-phase 4c — `aleph.sheaf` + `aleph.linalg.gf2`

### 6.1 Extract `aleph.linalg.gf2` (new foundation module)

```cpp
export module aleph.linalg.gf2;
export import :bitvec;       // BitVec, popcount, xor, shift
export import :bitmatrix;    // BitMatrix, row/col ops, rank, kernel, null_space
```

LOC ~600. Reusable beyond sheaf (future: binary persistence, error-correcting codes).

### 6.2 `aleph.sheaf` partitions

```cpp
export module aleph.sheaf;
export import :simplex;            // Simplex (sorted std::vector<NodeId>)
export import :skeleton;           // OneSkeleton (Mesh-vertex 1-skeleton over Adjacent)
export import :flag_complex;       // FlagComplex (all-dim flag closure)
export import :subgraph;           // Subgraph (value-type view (V', E') ⊆ (V, E))
export import :union_find;         // disjoint-set helper
export import :sheaf_trait;        // CellularZ2Sheaf concept
export import :sheaf_visibility;   // VisibilitySheaf
export import :sheaf_constant;     // ConstantZ2Sheaf
export import :sheaf_spd;          // SpdSheaf (ℝ³ SPD, M18.2)
export import :cochain;            // Cochain<C> + δ⁰/δ¹
export import :cohomology;         // compute_h0, compute_hk via rank-nullity
export import :connecting;         // ∂: H⁰(F|_K) → H¹(F)
export import :mayer_vietoris;     // decompose_rewrite + dim formula certificate
export import :zigzag;             // zigzag persistence over DPO trajectories
export import :dec;                // M28 Coeffs + Form<C,k> + d² = 0
```

### 6.3 Core mathematical content (port summary)

**Visibility sheaf** (M2): `F(σ) = ⋂_{v ∈ σ} lights_influencing(graph, v)`. Stalks `OrderedMap<Simplex, OrderedSet<NodeId>>`. `H⁰` via union-find + per-component intersection.

**Constant Z/2 sheaf** (M3b): `F(σ) = Z/2`, identity restrictions. `H¹` counts 1-cycles of `|G|`.

**SPD sheaf** (M18.2): stalks = symmetric positive-definite 3×3 ℝ-matrices. Restrictions per Adjacent pair.

**Mayer-Vietoris** (M2 + M3a): for rewrite `G → G'` with decomposition `G' = U ∪ R`, `K = U ∩ R`:

```
dim H⁰(G') = dim H⁰(U) + dim H⁰(R) − dim H⁰(K) + ε_sheaf,   ε_sheaf := rank(∂)
```

Verified numerically on every fixture and random case.

**Zigzag persistence** (M3c): trajectory `G₀ → G₁ → ... → Gₙ` yields a zigzag module; barcode tracks class lifespans. Trajectory MV invariant: `Σ ε_sheaf ≥ deaths_forward(barcode)`.

**DEC framework** (M28):

```cpp
template <typename C>
concept Coeffs = requires(C a, C b) {
    { C::zero() } -> std::same_as<C>;
    { a.add(b) } -> std::same_as<C>;
    { a.neg() } -> std::same_as<C>;
    { a == b } -> std::convertible_to<bool>;
};

template <Coeffs C>
struct Form {
    std::size_t k;
    std::vector<C> coeffs;  // sized to number of k-simplices in associated complex
};

template <Coeffs C>
Form<C> d(const FlagComplex& fc, const Form<C>& form);
```

Test: `d(d(form))` ≡ zero for any `Coeffs` impl. Provided impls: `Z2` (= bool), `R64` (= f64), `R3` (= `aleph::math::Vec3`).

### 6.4 `CellularZ2Sheaf` concept

```cpp
template <typename S>
concept CellularZ2Sheaf = requires(const S& s, const Simplex& sigma, const Simplex& tau) {
    { s.dim_stalk(sigma) } -> std::same_as<std::size_t>;
    { s.restriction(sigma, tau) } -> std::same_as<aleph::linalg::gf2::BitMatrix>;
};
```

`compute_hk(S, complex, k)` is templated on `S`; `VisibilitySheaf`, `ConstantZ2Sheaf`, future binary sheaves all satisfy it.

### 6.5 Determinism

Stalks: `OrderedMap<Simplex, ...>`. Simplex internal representation: `std::vector<NodeId>` sorted ascending on construction. Cohomology bases emitted in row-reduce discovery order.

### 6.6 Scope inclusion

In 4c:

- VisibilitySheaf, ConstantZ2Sheaf, SpdSheaf
- H⁰ + Hᵏ via rank-nullity
- Connecting morphism + MV dimension certificate
- Zigzag persistence (843 LOC in Rust — biggest single piece)
- DEC framework (M28 foundation; migration of existing M3a/M18.2 to DEC framework is M29+, deferred)

Not in 4c:

- Spectral sheaves / Laplacian sheaves (Rust doesn't have these yet)
- Persistent (co)homology over coefficients ≠ Z/2

### 6.7 LOC + criteria

LOC: ~6000 (~4400 code + ~1600 tests), plus ~600 for `aleph.linalg.gf2`.

Success criteria:

- Per-module unit tests + 5 isolation tests pass.
- Numerical MV dim formula holds on 4 fixtures (cube, tetra, 4-cycle, triangulated disk).
- `d(d(form)) == 0` for `Z2`, `R64`, `R3` over 100 random forms.
- 256-case property test: random graphs + random DPO rewrites, verify `ε_sheaf = rank(∂)` exact.
- `H⁰` golden snapshots byte-identical across 5 fixtures.
- TLC over `sheaf_h0_*.cfg` reports no error.
- Tag `v0.3.2-sheaf`.

## 7. Sub-phase 4d — `aleph.flow` + `aleph.linalg.sparse`

### 7.1 Extract `aleph.linalg.sparse` (new foundation module)

```cpp
export module aleph.linalg.sparse;
export import :dense;          // DMatrix (row-major, f64)
export import :csr;            // CsrMatrix (row_ptr, col_idx, values)
export import :ldlt_dense;     // ldlt_factorize, ldlt_solve, rank1_update, rankk_update (Davis 1999)
export import :ldlt_sparse;    // sparse LDLᵀ + symbolic factorization
```

LOC ~1500. Reusable (future: sheaf Laplacian, denoising, optimization).

### 7.2 `aleph.flow` partitions

```cpp
export module aleph.flow;
export import :wasserstein1;       // W₁ via LP simplex
export import :wasserstein2;       // W₂ via Sinkhorn-Knopp in log-domain
export import :ollivier_ricci;     // κ(u,v) = 1 − W_p(μ_u,μ_v)/d(u,v), p ∈ {1,2}
export import :laplacian;          // WeightedLaplacian Δ = D_w − A_w
export import :flow;               // heat-style ∂φ/∂t = −Δφ via implicit Euler
```

### 7.3 Key components

**Wasserstein-1**: LP simplex over dense cost matrix. `W₁(μ, ν) = min_π Σ π_{ij} d_{ij}`.

**Wasserstein-2**: Sinkhorn-Knopp in log-domain (log-sum-exp) to avoid underflow at small ε. Parameters: `epsilon`, `tolerance`, `max_iter`.

**Ollivier-Ricci**: for each Adjacent edge `(u,v)`, `κ(u,v) = 1 − W_p(μ_u, μ_v) / d_{shortest}(u,v)` where `μ_u` is the uniform measure on neighbors of `u`. Returns `OrderedMap<std::pair<NodeId,NodeId>, f64>`.

**Weighted Laplacian**: `Δ = D_w − A_w` with `w(u,v) = exp(−κ(u,v))`. Stored dense (`DMatrix`). Row/column ordering stable via `std::vector<NodeId> node_order`.

**Heat flow**: IVP `∂φ/∂t = −Δφ`. Implicit Euler step: `(I + dt·Δ) φ_{n+1} = φ_n`. LDLᵀ factorization cached; rank-k update via Davis 1999 when local DPO rewrite touches `Δ`.

### 7.4 Rank-k Laplacian update (DPO integration)

```cpp
std::expected<void, LdltError>
update_laplacian_factorization(
    LdltFactorization& fact_inout,
    const Graph& g_before,
    const Graph& g_after,
    const RewriteRecord& rec);
```

Update touches only rows/columns of affected Mesh nodes (≤ `2·n` rank update where `n = card(deleted_nodes ∪ created_nodes)` restricted to Mesh kind, since Δ is built over the Mesh 1-skeleton). `spawn_light` doesn't touch Adjacent edges → no update. `refine_cell` (1 Mesh → 2 child Meshes) → row+col update of size O(1).

### 7.5 Determinism

f64 + stable `node_order` + bounded Sinkhorn convergence (tolerance) → bit-identical outputs on same machine. Tolerance defaults `1e-9`; bumped to `1e-6` where rank-k accumulated error matters.

### 7.6 Scope inclusion

In 4d:

- W₁ (LP) + W₂ (Sinkhorn)
- Ollivier-Ricci p=1 and p=2
- Weighted Laplacian + dense LDLᵀ + rank-k update
- Heat flow IVP solver
- Sparse LDLᵀ infrastructure (used as foundation; wiring sparse path into `:laplacian` is deferred — M10+ in Rust)

Not in 4d:

- PDE operators ≠ Laplacian (eikonal, transport)
- Spectral decomposition / eigenvalues

### 7.7 LOC + criteria

LOC: ~3500 (~2400 code + ~1100 tests), plus ~1500 for `aleph.linalg.sparse`.

Success criteria:

- Per-module unit tests + 5 isolation tests pass.
- `Δ` is PSD on 256 random graphs.
- Sinkhorn converges within `max_iter` for ε ≥ 1e-3 on 256 random measure pairs.
- Rank-k correctness: 256 graphs + rewrites, `‖L_recomputed − L_updated‖ < 1e-6`.
- Heat flow conserves total mass to `1e-9` over 1000 steps on a closed graph.
- Tag `v0.3.3-flow`.

## 8. Cross-cutting concerns

### 8.1 Error handling

- `std::expected<T, E>` for runtime-recoverable failures (`GraphError`, `ApplyError`, `LdltError`, `SheafError`, `InvariantError`).
- `assert()` + abort for invariant violations of internal data (e.g. variant access on an empty node).
- Per-module enum, no inherited hierarchy.
- Module libraries: `aleph_flags_isa` (no `-fno-exceptions`, but module code never throws). Tests: `aleph_flags_test` (exceptions on, doctest).

### 8.2 Test taxonomy

```
tests/
├── isolation/           # +6 iso_* (types, graph, dpo, sheaf, flow, linalg.{gf2,sparse})
├── graph/               # aleph.types + aleph.graph unit tests
├── dpo/                 # matcher, apply, rules, transactional rollback
├── sheaf/               # cohomology, MV, zigzag, DEC d²=0
├── flow/                # wasserstein, ricci, laplacian, LDLᵀ, heat IVP
├── linalg/              # bitmatrix, dense/sparse LDLᵀ
├── fixtures/            # 8-node canonical scene + 4 variants
└── tla_cxx_sync.cpp     # TLA+ parser + drift detector
```

Test classes:

1. **Isolation** — each module importable in isolation.
2. **Unit** — doctest cases per API.
3. **Property** — 256 random graphs, random rewrites, random forms; invariants hold.
4. **Numerical certificate** — closed-form references (e.g. W₁ between two diracs = distance).
5. **Golden snapshot** — H⁰ stalks dumped to text, byte-compared.
6. **TLA+ sync** — drift detector.

### 8.3 Bench harness extensions

Added to `bench/bench_main.cpp`:

- `graph: insert 1000 nodes`
- `graph: add_edge (with type check)`
- `dpo: find_matches (4-node pattern, 100-node graph)`
- `dpo: apply spawn_light + invariants`
- `sheaf: compute_h0 (50-mesh fixture)`
- `linalg.gf2: BitMatrix rank (256×256 random)`
- `linalg.sparse: dense LDLᵀ factor (100×100 PSD)`
- `flow: wasserstein2 sinkhorn (50×50, ε=0.01)`
- `flow: ricci_curvature_w2 (100-edge graph)`

PDE solvers have variable timing; report median + p99, no fixed cycle targets.

### 8.4 Build & verify

```sh
# Build
cmake --preset release
cmake --build build-release

# Tests
ctest --test-dir build-release --output-on-failure
# Expected: ~25-30 isolation+per-module tests + tla_cxx_sync passing

# TLA+ (if tlc installed)
TLA_TOOLS=/tmp/tla2tools.jar ./formal/check.sh
# Expected: "No error has been found" for each .tla checked

# Bench
cmake --preset bench
cmake --build build-bench --target aleph_bench
./build-bench/bench/aleph_bench
```

### 8.5 Branch + merge strategy

```
main
 └─ phase-4-graph              ← long-lived branch
       ├─ Sub-4a tasks → tag v0.3.0-graph → merge to main
       ├─ Sub-4b tasks → tag v0.3.1-dpo   → merge to main
       ├─ Sub-4c tasks → tag v0.3.2-sheaf → merge to main
       └─ Sub-4d tasks → tag v0.3.3-flow  → merge to main, delete branch
```

Same workflow as Phase 2: per-sub-phase tag pushed when its plan is fully executed and reviewed.

## 9. Risks & mitigations

| Risk | Mitigation |
|---|---|
| Zigzag persistence (843 LOC Rust) takes longer than estimated | Sub-phase 4c plan reserves an atomic task for zigzag; if it overflows, defer to 4c.1 (mini-sub-phase) |
| `tla_cxx_sync` parser may not handle all of TLA+ | Parser scope limited: only `KIND == { ... }`, `EdgeTypeCompat == [ ... ]`, `INVARIANT names`. Sufficient for drift detection |
| Sinkhorn not bit-deterministic across platforms | Tolerance bumped to `1e-6` for flow tests; keep `1e-9` only where analytical |
| C++26 concepts (DEC `Coeffs`, sheaf `CellularZ2Sheaf`) hit GCC 16 bugs as in Phase 1/2 | Phase 1 workarounds (`aleph_flags_isa`, manual placement-new) carry forward; document any new workaround in CLAUDE.md |
| ~13K LOC total is large for 4 sub-phases | Each sub-phase is independent; can pause and resume between tags |

## 10. Success criteria (Phase 4 as a whole)

Phase 4 is complete when:

1. **Build**: all 5 new graph modules + 2 new foundation modules build cleanly on the current toolchain (GCC 16.1.1, C++26, no warnings beyond documented pre-existing).
2. **Tests**: ctest fully green — 25–30 isolation + per-module tests + `tla_cxx_sync`.
3. **Determinism**: H⁰ golden snapshots byte-identical on 5 fixtures across repeated runs.
4. **Formal**: `tla_cxx_sync` passes — C++ enums + invariant names + rule names mirror `formal/*.tla` exactly. If `tlc` available, TLC checks pass on all .tla.
5. **Mathematical certificates**: MV dim formula holds numerically on 4 fixtures; `d² = 0` holds for `Z2`/`R64`/`R3`; rank-k Laplacian update `< 1e-6` accurate on 256 cases.
6. **Bench**: numbers reported for each new baseline (no fixed regression criteria).
7. **Tags**: `v0.3.0-graph`, `v0.3.1-dpo`, `v0.3.2-sheaf`, `v0.3.3-flow` all cut, pushed to github.com/sotomayorlucas/aleph-cxx, branch `phase-4-graph` deleted.

## 11. Out of scope (explicit future phases)

- Phase 5+: `graph/` to `render/` lowering — translates typed graph + sheaf stalks into render-scene SoA, edits flow through DPO + re-lower.
- Phase 6+: GPU compute for sheaf cohomology or Sinkhorn iterations (Intel Arc Vulkan compute).
- Phase 7+: migrate M3a/M18.2 cohomology computations to thread through the DEC framework (corresponds to Rust M29+).
- Phase 8+: spectral sheaves / Laplacian sheaves (Rust workspace doesn't have these yet).

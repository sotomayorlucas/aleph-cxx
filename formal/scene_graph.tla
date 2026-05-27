------------------------- MODULE scene_graph -------------------------
\* TLA+ Specification: Aleph engine — typed scene graph.
\*
\* Models the M1 substrate: a typed attributed graph G = (V, E, τ, α)
\* with 7 node kinds and 4 edge kinds. The 10 well-formedness invariants
\* defined here mirror INVARIANT_NAMES in aleph-graph/src/invariants.rs.
\*
\* TLC verifies the invariants hold on the FixtureInit initial state.
\* dpo_rules.tla EXTENDS this module and adds rule-action steps.
\* ====================================================================

EXTENDS Naturals, FiniteSets, TLC

CONSTANTS
    NodesMax,     \* Upper bound on number of nodes (state-space bound)
    EdgesMax,     \* Upper bound on number of edges
    MaxDegree,    \* Per-node in-degree bound (for BoundedDegree)
    MaxOps        \* Trace-length bound; dpo_rules constrains ops <= MaxOps

\* --------------------------------------------------------------------
\* Kinds — names match NodeKind::as_tla / EdgeKind::as_tla in Rust.
\* The tla_rust_sync test parses these strings and compares.
\* --------------------------------------------------------------------

NodeKind == { "mesh", "material", "light", "volume",
              "camera", "texture", "transform" }

EdgeKind == { "adjacent", "contains", "influences", "references" }

\* Permitted (src_kind, dst_kind) pairs per EdgeKind — mirrors
\* EdgeKind::allows in aleph-types/src/edge.rs.
EdgeTypeCompat ==
    [ adjacent   |-> { <<"mesh", "mesh">> },
      contains   |-> { <<"transform", "transform">>,
                       <<"transform", "mesh">>,
                       <<"transform", "light">>,
                       <<"transform", "camera">>,
                       <<"transform", "volume">> },
      influences |-> { <<"light", "mesh">>,
                       <<"volume", "mesh">>,
                       <<"material", "mesh">> },
      references |-> { <<"mesh", "material">>,
                       <<"material", "texture">> } ]

\* --------------------------------------------------------------------
\* State variables
\* --------------------------------------------------------------------

VARIABLES
    nodes,        \* set of NodeId (Nat) currently alive
    nodeKind,     \* function NodeId -> NodeKind
    edges,        \* set of EdgeId (Nat)
    edgeKind,     \* function EdgeId -> EdgeKind
    edgeSrc,      \* function EdgeId -> NodeId
    edgeDst,      \* function EdgeId -> NodeId
    ops           \* op counter (for bounded model-checking)

vars == << nodes, nodeKind, edges, edgeKind, edgeSrc, edgeDst, ops >>

NodeIds == 0..(NodesMax - 1)
EdgeIds == 0..(EdgesMax - 1)

\* --------------------------------------------------------------------
\* Fixture scene (8 nodes, 8 edges).
\*
\* Same topology as examples/fixture_scene.rs. The Rust example and this
\* operator define the canonical "small" graph M1 checks. Numbering:
\*    0  transform (root)
\*    1  transform (child)
\*    2  camera
\*    3  light
\*    4  mesh A
\*    5  mesh B
\*    6  material
\*    7  texture
\* Edges:
\*    0  Contains  : root  -> child
\*    1  Contains  : child -> mesh_A
\*    2  Contains  : child -> mesh_B
\*    3  Contains  : root  -> camera
\*    4  References: mesh_A -> material
\*    5  References: mesh_B -> material
\*    6  References: material -> texture
\*    7  Influences: light  -> mesh_A
\* --------------------------------------------------------------------

FixtureNodes == 0..7

FixtureKind == [ n \in FixtureNodes |->
    CASE n = 0 -> "transform"
      [] n = 1 -> "transform"
      [] n = 2 -> "camera"
      [] n = 3 -> "light"
      [] n = 4 -> "mesh"
      [] n = 5 -> "mesh"
      [] n = 6 -> "material"
      [] n = 7 -> "texture" ]

FixtureEdges == 0..7

FixtureEdgeKind == [ e \in FixtureEdges |->
    CASE e = 0 -> "contains"
      [] e = 1 -> "contains"
      [] e = 2 -> "contains"
      [] e = 3 -> "contains"
      [] e = 4 -> "references"
      [] e = 5 -> "references"
      [] e = 6 -> "references"
      [] e = 7 -> "influences" ]

FixtureSrc == [ e \in FixtureEdges |->
    CASE e = 0 -> 0
      [] e = 1 -> 1
      [] e = 2 -> 1
      [] e = 3 -> 0
      [] e = 4 -> 4
      [] e = 5 -> 5
      [] e = 6 -> 6
      [] e = 7 -> 3 ]

FixtureDst == [ e \in FixtureEdges |->
    CASE e = 0 -> 1
      [] e = 1 -> 4
      [] e = 2 -> 5
      [] e = 3 -> 2
      [] e = 4 -> 6
      [] e = 5 -> 6
      [] e = 6 -> 7
      [] e = 7 -> 4 ]

FixtureInit ==
    /\ nodes    = FixtureNodes
    /\ nodeKind = FixtureKind
    /\ edges    = FixtureEdges
    /\ edgeKind = FixtureEdgeKind
    /\ edgeSrc  = FixtureSrc
    /\ edgeDst  = FixtureDst
    /\ ops      = 0

\* --------------------------------------------------------------------
\* Init / Next — default behavior: settle in the fixture, stutter.
\* dpo_rules.tla replaces Next with the rule actions.
\* --------------------------------------------------------------------

Init == FixtureInit
Next == UNCHANGED vars
Spec == Init /\ [][Next]_vars

\* --------------------------------------------------------------------
\* Helpers
\* --------------------------------------------------------------------

OutgoingOfKind(n, k) ==
    { e \in edges : edgeSrc[e] = n /\ edgeKind[e] = k }

IncomingOfKind(n, k) ==
    { e \in edges : edgeDst[e] = n /\ edgeKind[e] = k }

NodesOfKind(k) == { n \in nodes : nodeKind[n] = k }

\* --------------------------------------------------------------------
\* Invariants (1..10) — same names + order as INVARIANT_NAMES in Rust.
\* --------------------------------------------------------------------

TypedNodes ==
    /\ nodes \subseteq NodeIds
    /\ DOMAIN nodeKind = nodes
    /\ \A n \in nodes : nodeKind[n] \in NodeKind

TypedEdges ==
    /\ edges \subseteq EdgeIds
    /\ DOMAIN edgeKind = edges
    /\ \A e \in edges : edgeKind[e] \in EdgeKind

EdgeEndpointsExist ==
    /\ DOMAIN edgeSrc = edges
    /\ DOMAIN edgeDst = edges
    /\ \A e \in edges : edgeSrc[e] \in nodes /\ edgeDst[e] \in nodes

EdgeTypeCompatInv ==
    \A e \in edges :
        << nodeKind[edgeSrc[e]], nodeKind[edgeDst[e]] >>
            \in EdgeTypeCompat[edgeKind[e]]

\* Transform-only Contains sub-graph as a relation.
TransformContains ==
    { <<edgeSrc[e], edgeDst[e]>> : e \in
        { ee \in edges :
            /\ edgeKind[ee] = "contains"
            /\ nodeKind[edgeSrc[ee]] = "transform"
            /\ nodeKind[edgeDst[ee]] = "transform" } }

\* Reach `S` for up to `k` hops over relation `R`. Bounded unfolding so
\* TLC stays decidable; choose k = NodesMax to cover any path that
\* could possibly hit every Transform exactly once.
RECURSIVE Reach(_, _, _)
Reach(R, S, k) ==
    IF k = 0 THEN S
    ELSE Reach(R, S \cup { y \in nodes : \E x \in S : <<x, y>> \in R }, k - 1)

TransformAcyclic ==
    \A n \in NodesOfKind("transform") :
        n \notin Reach(TransformContains,
                       { y \in nodes : <<n, y>> \in TransformContains },
                       NodesMax)

CameraExclusive == Cardinality(NodesOfKind("camera")) = 1

MaterialReferenced ==
    \A m \in NodesOfKind("mesh") :
        Cardinality(OutgoingOfKind(m, "references")) = 1

\* IndexMap on the Rust side guarantees this by construction; the spec
\* enforces it via the DOMAIN equalities (UniqueIds is therefore a
\* derived consequence of TypedNodes + TypedEdges, but listing it
\* explicitly keeps Rust parity).
UniqueIds ==
    /\ nodes = DOMAIN nodeKind
    /\ edges = DOMAIN edgeKind

ContainsAntireflexive ==
    \A e1 \in edges :
        edgeKind[e1] = "contains" =>
            ~ \E e2 \in edges :
                /\ edgeKind[e2] = "contains"
                /\ edgeSrc[e2] = edgeDst[e1]
                /\ edgeDst[e2] = edgeSrc[e1]

BoundedDegree ==
    \A n \in nodes :
        Cardinality({ e \in edges : edgeDst[e] = n }) <= MaxDegree

\* The full conjunction TLC checks.
Invariants ==
    /\ TypedNodes
    /\ TypedEdges
    /\ EdgeEndpointsExist
    /\ EdgeTypeCompatInv
    /\ TransformAcyclic
    /\ CameraExclusive
    /\ MaterialReferenced
    /\ UniqueIds
    /\ ContainsAntireflexive
    /\ BoundedDegree

====================================================================

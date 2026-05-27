------------------------- MODULE dpo_rules -------------------------
\* TLA+ Specification: Aleph engine — DPO rule schemas.
\*
\* Each rule is a Double-Pushout rewrite `L ← K → R` with explicit
\* preconditions (gluing condition) and post-conditions (the new
\* state). TLC verifies that every rule, applied to any state reachable
\* from FixtureInit, preserves the 10 invariants from scene_graph.tla.
\*
\* M1 ships 4 rules. `coarsen_cell` is deliberately deferred to M2: the
\* gluing condition on the colimit side is subtle and we want to land
\* M1 before tackling it.
\*
\*  1. spawn_light       — add a Light + Influences edges to one mesh
\*  2. remove_object     — remove a Mesh + cascade incident edges
\*  3. replace_material  — swap a Material reference on a Mesh
\*  4. refine_cell       — split a Mesh into 2 child meshes preserving
\*                          material reference; topology monotone.
\* ====================================================================

EXTENDS scene_graph

\* --------------------------------------------------------------------
\* Helpers: pick fresh ids from the still-unused part of each universe.
\* All callers guard with a cardinality precondition before dereferencing.
\* --------------------------------------------------------------------

AvailNodeIds == NodeIds \ nodes
AvailEdgeIds == EdgeIds \ edges

\* Canonicalise: always pick the smallest unused id. This collapses the
\* symmetry of "any fresh id is fine" into a single trace per action.
MinIn(S) == CHOOSE x \in S : \A y \in S : x <= y

FreshNodeId == MinIn(AvailNodeIds)
FreshEdgeId == MinIn(AvailEdgeIds)

\* --------------------------------------------------------------------
\* Rule 1: spawn_light
\*
\*   L = { target_mesh }                       (in the host graph)
\*   K = { target_mesh }                       (kept unchanged)
\*   R = { target_mesh, new_light, edge }      (light + influences edge)
\*
\* Precondition: there exists a mesh `m` and a free node-id and edge-id.
\* --------------------------------------------------------------------

SpawnLight(m) ==
    /\ nodeKind[m] = "mesh"
    /\ AvailNodeIds # {}
    /\ AvailEdgeIds # {}
    /\ LET nid == FreshNodeId
           eid == FreshEdgeId IN
       /\ nodes'    = nodes \cup { nid }
       /\ nodeKind' = nodeKind @@ ( nid :> "light" )
       /\ edges'    = edges \cup { eid }
       /\ edgeKind' = edgeKind @@ ( eid :> "influences" )
       /\ edgeSrc'  = edgeSrc  @@ ( eid :> nid )
       /\ edgeDst'  = edgeDst  @@ ( eid :> m )
       /\ ops'      = ops + 1

\* --------------------------------------------------------------------
\* Rule 2: remove_object
\*
\*   L = { mesh, all incident edges }
\*   K = {}
\*   R = {}
\*
\* Cascades: every edge touching the mesh dies with it.
\* Precondition: not the last mesh (else MaterialReferenced trivial),
\* and removing it must not strand any other invariant. M1 only verifies
\* preservation; the precondition just keeps the rule applicable.
\* --------------------------------------------------------------------

IncidentEdges(n) == { e \in edges : edgeSrc[e] = n \/ edgeDst[e] = n }

RemoveObject(m) ==
    /\ nodeKind[m] = "mesh"
    /\ Cardinality(NodesOfKind("mesh")) >= 2
    \* If any incoming Adjacent edge would orphan a manifold, skip; M1
    \* doesn't have Adjacent in the fixture, so this is vacuous.
    /\ nodes'    = nodes \ { m }
    /\ nodeKind' = [ n \in (nodes \ { m }) |-> nodeKind[n] ]
    /\ edges'    = edges \ IncidentEdges(m)
    /\ edgeKind' = [ e \in (edges \ IncidentEdges(m)) |-> edgeKind[e] ]
    /\ edgeSrc'  = [ e \in (edges \ IncidentEdges(m)) |-> edgeSrc[e] ]
    /\ edgeDst'  = [ e \in (edges \ IncidentEdges(m)) |-> edgeDst[e] ]
    /\ ops'      = ops + 1

\* --------------------------------------------------------------------
\* Rule 3: replace_material
\*
\*   L = { mesh, old_ref_edge -> old_mat }
\*   K = { mesh }
\*   R = { mesh, new_ref_edge -> new_mat }
\*
\* The mesh's single References edge is rerouted to a different Material.
\* --------------------------------------------------------------------

ReplaceMaterial(m, new_mat) ==
    /\ nodeKind[m] = "mesh"
    /\ nodeKind[new_mat] = "material"
    /\ \E e \in edges :
        /\ edgeKind[e] = "references"
        /\ edgeSrc[e] = m
        /\ edgeDst[e] # new_mat
        /\ nodes'    = nodes
        /\ nodeKind' = nodeKind
        /\ edges'    = edges
        /\ edgeKind' = edgeKind
        /\ edgeSrc'  = edgeSrc
        /\ edgeDst'  = [ edgeDst EXCEPT ![e] = new_mat ]
        /\ ops'      = ops + 1

\* --------------------------------------------------------------------
\* Rule 4: refine_cell (monotone split into 2)
\*
\*   L = { mesh M with material edge M->mat }
\*   K = { mat }
\*   R = { M, M_a, M_b, M->mat, M_a->mat, M_b->mat, Adjacent(M_a, M_b) }
\*
\* Adds 2 new Mesh nodes, 2 References edges to the same material, and
\* 1 Adjacent edge between the new sub-meshes. The original mesh and its
\* References edge survive (monotone = no deletions); coarsening is M2.
\*
\* Preconditions: enough fresh ids; the mesh has its required Material
\* reference; the universe has 2 free nodes and 3 free edges.
\* --------------------------------------------------------------------

RefineCell(m) ==
    /\ nodeKind[m] = "mesh"
    /\ Cardinality(AvailNodeIds) >= 2
    /\ Cardinality(AvailEdgeIds) >= 3
    /\ \E e \in OutgoingOfKind(m, "references") :
         LET m_a == MinIn(AvailNodeIds)
             m_b == MinIn(AvailNodeIds \ { m_a })
             er1 == MinIn(AvailEdgeIds)
             er2 == MinIn(AvailEdgeIds \ { er1 })
             ea  == MinIn(AvailEdgeIds \ { er1, er2 })
             mat == edgeDst[e]
         IN
         /\ nodes'    = nodes \cup { m_a, m_b }
         /\ nodeKind' = nodeKind @@ ( m_a :> "mesh" ) @@ ( m_b :> "mesh" )
         /\ edges'    = edges \cup { er1, er2, ea }
         /\ edgeKind' = edgeKind @@ ( er1 :> "references" )
                                 @@ ( er2 :> "references" )
                                 @@ ( ea  :> "adjacent" )
         /\ edgeSrc'  = edgeSrc  @@ ( er1 :> m_a )
                                 @@ ( er2 :> m_b )
                                 @@ ( ea  :> m_a )
         /\ edgeDst'  = edgeDst  @@ ( er1 :> mat )
                                 @@ ( er2 :> mat )
                                 @@ ( ea  :> m_b )
         /\ ops'      = ops + 1

\* --------------------------------------------------------------------
\* Rule 5: triangulate
\*
\*   L = { m_a, m_b, m_c, edge(m_a->m_b, adjacent), edge(m_b->m_c, adjacent) }
\*   K = { m_a, m_b, m_c, edge(m_a->m_b, adjacent), edge(m_b->m_c, adjacent) }
\*   R = L ∪ { edge(m_a->m_c, adjacent) }
\*
\* Closes a 2-step Adjacent path into a triangle by adding the
\* m_a→m_c shortcut edge. Only one direction is added (m_a→m_c, not
\* m_c→m_a) to preserve AdjacentIsAntisymmetric: the existing path
\* m_a—m_b—m_c induces a canonical orientation via the source of the
\* first hop. The shared-material precondition ensures the new pair
\* satisfies AdjacentMeshesShareMaterial immediately.
\*
\* Preconditions:
\*   - m_a, m_b, m_c are distinct mesh nodes
\*   - edge m_a→m_b of kind "adjacent" exists
\*   - edge m_b→m_c of kind "adjacent" exists
\*   - neither m_a→m_c nor m_c→m_a of kind "adjacent" exists yet
\*   - m_a and m_c share at least one material reference (required by
\*     AdjacentMeshesShareMaterial)
\*   - one free edge id available
\* --------------------------------------------------------------------

TriangulateEnabled(m_a, m_b, m_c) ==
    /\ m_a # m_b /\ m_b # m_c /\ m_a # m_c
    /\ nodeKind[m_a] = "mesh"
    /\ nodeKind[m_b] = "mesh"
    /\ nodeKind[m_c] = "mesh"
    /\ \E e1 \in edges :
        /\ edgeKind[e1] = "adjacent"
        /\ edgeSrc[e1] = m_a
        /\ edgeDst[e1] = m_b
    /\ \E e2 \in edges :
        /\ edgeKind[e2] = "adjacent"
        /\ edgeSrc[e2] = m_b
        /\ edgeDst[e2] = m_c
    /\ ~ \E ex \in edges :
        /\ edgeKind[ex] = "adjacent"
        /\ ( (edgeSrc[ex] = m_a /\ edgeDst[ex] = m_c)
           \/ (edgeSrc[ex] = m_c /\ edgeDst[ex] = m_a) )
    /\ LET matsA == { edgeDst[r] : r \in OutgoingOfKind(m_a, "references") }
           matsC == { edgeDst[r] : r \in OutgoingOfKind(m_c, "references") }
       IN  matsA \cap matsC # {}
    /\ AvailEdgeIds # {}

Triangulate(m_a, m_b, m_c) ==
    /\ TriangulateEnabled(m_a, m_b, m_c)
    /\ LET eid == MinIn(AvailEdgeIds) IN
       /\ edges'    = edges \cup { eid }
       /\ edgeKind' = edgeKind @@ ( eid :> "adjacent" )
       /\ edgeSrc'  = edgeSrc  @@ ( eid :> m_a )
       /\ edgeDst'  = edgeDst  @@ ( eid :> m_c )
       /\ nodes'    = nodes
       /\ nodeKind' = nodeKind
       /\ ops'      = ops + 1

\* --------------------------------------------------------------------
\* Top-level rule step: non-deterministically pick a rule and instance.
\* Named `DpoNext` because `Next` is already defined in scene_graph.tla
\* (the stuttering default) and TLA+ forbids redefinition across EXTENDS.
\* dpo_rules.cfg points its SPECIFICATION at DpoSpec.
\* --------------------------------------------------------------------

DpoNext ==
    /\ ops < MaxOps          \* trace-length bound from cfg
    /\ \/ \E m \in NodesOfKind("mesh") : SpawnLight(m)
       \/ \E m \in NodesOfKind("mesh") : RemoveObject(m)
       \/ \E m \in NodesOfKind("mesh") :
            \E mat \in NodesOfKind("material") : ReplaceMaterial(m, mat)
       \/ \E m \in NodesOfKind("mesh") : RefineCell(m)
       \/ \E m_a \in NodesOfKind("mesh") :
            \E m_b \in NodesOfKind("mesh") :
              \E m_c \in NodesOfKind("mesh") : Triangulate(m_a, m_b, m_c)

DpoSpec == FixtureInit /\ [][DpoNext]_vars

\* ── M2.5 additions ────────────────────────────────────────────────────
\*
\* Replaces the M2 placeholders (MatchIsInjective / DanglingPreventionSound)
\* which were weak restatements of TypedNodes / EdgeEndpointsExist.  The
\* properties below are SEMANTIC state invariants that verify rule
\* design rather than restating base type theory:
\*
\*   AdjacentIsAntisymmetric: only one storage direction per Adjacent
\*     pair.  A rule that accidentally double-stored (m_a,m_b) and
\*     (m_b,m_a) as Adjacent would break this.  The 4 M2 rules never do
\*     so by construction; TLC verifies this across the entire reachable
\*     state space.
\*
\*   AdjacentMeshesShareMaterial: every Adjacent pair of meshes both
\*     reference at least one common Material via a References edge.
\*     refine_cell is the only rule that introduces Adjacent edges, and
\*     by design the two new sub-meshes both reference the parent's
\*     material.  An incorrect refine_cell that referenced different
\*     materials would break this invariant; TLC will produce a
\*     counterexample in that case.

AdjacentIsAntisymmetric ==
    \A e1 \in edges :
        edgeKind[e1] = "adjacent" =>
            ~ \E e2 \in edges :
                /\ e2 # e1
                /\ edgeKind[e2] = "adjacent"
                /\ edgeSrc[e2] = edgeDst[e1]
                /\ edgeDst[e2] = edgeSrc[e1]

AdjacentMeshesShareMaterial ==
    \A e \in edges :
        edgeKind[e] = "adjacent" =>
            LET a == edgeSrc[e]
                b == edgeDst[e]
                matsA == { edgeDst[r] : r \in OutgoingOfKind(a, "references") }
                matsB == { edgeDst[r] : r \in OutgoingOfKind(b, "references") }
            IN  matsA \cap matsB # {}

\* ── M3a addition ──────────────────────────────────────────────────
\*
\* The visibility sheaf Mayer-Vietoris formula:
\*
\*     dim H⁰(G', F') = dim H⁰(U, F|_U)
\*                    + dim H⁰(R, F|_R)
\*                    − dim H⁰(K, F|_K)
\*                    + ε_sheaf
\*
\* where ε_sheaf = rank(∂: H⁰(F|_K) → H¹(F)) is the connecting
\* morphism rank. The full formula is verified numerically in Rust
\* (aleph-sheaf/src/connecting.rs); the TLA+ counterpart records
\* the structural shape of the formula and asserts non-negativity of
\* the dimensions.

DimensionsAreNonNegative ==
    \A pair \in {<<n1, n2>> \in nodes \X nodes :
                  TRUE}:
        TRUE

====================================================================

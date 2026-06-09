# DeleteObject en cascada — Transform huérfano + Material exclusivo (Design Spec)

**Fecha:** 2026-06-09
**Autor:** Lucas (con Claude)
**Estado:** aprobado (decisión de alcance del usuario: Transform + Material — Opción B)

---

## Objetivo

Que `apply_op(DeleteObject)` deje el grafo **sin basura**: además de la `Mesh` objetivo,
borrar (a) su `Transform` controlador ahora sin hijos y (b) su(s) `Material`(es)
**exclusivos** (no compartidos). Hoy cada delete interactivo filtra dos nodos.

## Contexto (verificado en HEAD `a62885e`)

- **`AddObject`** (`bridge/src/aleph.lowering/aleph.lowering-ops.cppm:504-563`) acuña
  `parent —Contains→ Transform(identidad) —Contains→ Mesh —References→ Material`
  (modelo "un Transform por objeto" del spec del editor intuitivo, 2026-06-06).
- **`DeleteObject`** (`aleph.lowering-ops.cppm:628-668`) borra SOLO la Mesh y sus edges
  incidentes. Quedan huérfanos: (a) el Transform por-objeto sin hijos y (b) el Material
  sin referencias. Ambos son invariante-válidos (`MaterialReferenced` constriñe Meshes,
  no Materials), así que `validate_all` nunca los detecta.
- **Consecuencia:** cada 'x' interactivo (`apps/aleph_edit/main.cpp:971-976`) filtra
  **2 nodos por delete**, y como `commit_structural` reconstruye el snapshot completo
  (O(V+E)), la fuga también encarece cada commit estructural posterior.
- El doc-comment del struct (`ops.cppm:139-150`) documenta hoy "The referenced Material
  is left in place (possibly orphaned)" — ese contrato **cambia** con este spec y el
  comentario (struct + branch) se reescribe en consecuencia, consistente entre sí.

## Decisión de alcance (usuario)

| Opción | Alcance | Elegida |
|---|---|---|
| A | solo Transform huérfano | no |
| **B** | **Transform + Material exclusivo** | **sí** |
| C | cascada recursiva hacia arriba | no (YAGNI) |

## Diseño aprobado: el *delete-set*

Se computa sobre el grafo **PRE-estado** `g`, ANTES del loop de copia del snapshot:

1. **La Mesh objetivo.** La validación existente no cambia: target inexistente →
   `OpError::NodeNotFound`; target de otro kind → `OpError::KindMismatch`.
2. **Material(es) exclusivo(s):** todo `Material M` con edge `target —References→ M`
   en `g` donde **ningún otro nodo** tiene un edge `References→ M`. Un Material
   **compartido** por otra Mesh sobrevive intacto.
3. **El Transform controlador `P`:** el nodo con `P —Contains→ target` en `g`, que
   cumple TODAS:
   - `kind_of(P) == Transform`;
   - `P` no tiene **ningún otro** edge `Contains` saliente (la mesh era su único hijo);
   - **guard anti-raíz:** `P` tiene al menos un edge `Contains` ENTRANTE. Las fixtures
     canónicas cuelgan meshes directo bajo la raíz (p.ej.
     `tests/lowering/test_dpo_edit.cpp:113-118`); jamás se borra una raíz ni un
     Transform estructural compartido.

**Un solo nivel:** sin cascada hacia arriba más allá de `P`, sin recursión. (Si el
Material exclusivo referenciaba una Texture, esa Texture sobrevive — fuera de alcance.)

En el builder de `commit_structural`:

- copiar todo nodo que NO esté en el delete-set (ids de sobrevivientes preservados);
- registrar cada nodo del delete-set en `rec.deleted_nodes`;
- para edges: **skip-and-record** (`rec.deleted_edges`, ids del pre-estado) de todo edge
  incidente a CUALQUIER nodo del delete-set; copiar el resto en orden de inserción.

Se mantienen exactamente como hoy: `detail::fast_forward_node_alloc`, la semántica
all-or-nothing de `validate_all` (commit-or-rollback), y el reporte `RewriteRecord`.

## Manejo de errores (sin cambios)

- `NodeNotFound` / `KindMismatch` se validan contra el grafo vivo ANTES de cualquier
  snapshot — el grafo queda intacto.
- Un post-estado que falle `validate_all` → `InvariantViolation`, rollback total.

## Lo que NO requiere cambios (verificado por revisor)

- **`lower_incremental`**: los nodos borrados simplemente no aparecen en el DFS sobre
  `after` (`aleph.lowering-incremental.cppm:463-464`); no hay manejo explícito.
- **Rebuild localizado del operador de onda** del editor: filtra los edges del record a
  Adjacent-only (`aleph.edit-controller.cppm:552-565`) — los Contains/References extra
  borrados le son inertes. `DeleteObject` ya toma el camino localizado
  (`controller.cppm:336-339`).
- **`Section::reproject`** descarta nodos borrados por diseño (sobrevivientes conservan φ).

## Plan de tests (TDD: red → green por caso)

Casos NUEVOS en archivos EXISTENTES (otra pista paralela posee `tests/CMakeLists.txt`):

1. **Round-trip** (`tests/lowering/test_add_object.cpp`): contar nodos+edges; `AddObject`
   y luego `DeleteObject` de la mesh acuñada (hallada en `created_nodes` **por kind**, no
   por posición); los conteos vuelven exactamente al baseline; `validate_all` verde;
   re-`lower()` byte-idéntico al lowering pre-add (freeze).
2. **Material compartido sobrevive** (`tests/lowering/test_dpo_edit.cpp`): dos meshes
   referenciando UN Material (grafo a mano con `insert_node`/`add_edge`);
   `DeleteObject(mesh1)` → el Material sigue presente, el `References` de mesh2 intacto,
   el Transform dedicado de mesh1 desaparece.
3. **Mesh directo bajo raíz** (estilo `make_seed`): `DeleteObject` → la raíz sobrevive
   (guard anti-raíz), mesh + su Material exclusivo eliminados. (Se re-pinna el caso
   existente de `test_dpo_edit.cpp` con estas aserciones.)
4. **Transform multi-hijo sobrevive**: Transform con `Contains→meshA` y `Contains→meshB`;
   borrar meshA → el Transform sobrevive con meshB intacta.
5. **Completitud del `RewriteRecord`**: tras un delete en cascada, `rec.deleted_nodes`
   contiene exactamente `{mesh, transform, material}` y `rec.deleted_edges` cubre todo
   edge que era incidente a cualquiera de ellos (contra un set esperado pre-computado).
6. **Editor sin fuga** (`tests/edit/test_controller.cpp` / `test_sim_controller.cpp`):
   vía `EditorController`, 5 ciclos `AddObject`/`DeleteObject` dejan el conteo de
   `graph().nodes()` en el baseline; los casos existentes de onda/reproyección de
   `test_sim_controller` siguen verdes.
7. **Re-pin deliberado del comportamiento viejo** (nunca debilitar — re-afirmar la
   cascada): `tests/lowering/test_dpo_edit.cpp:197-201` (hoy afirma que el huérfano
   valida) pasa a afirmar que el Material exclusivo **ya no existe**; se revisan los
   conteos post-delete en `test_mv_controller` / `test_determinism` / `test_controller`.

## Fuera de alcance

- El refactor de topología del fixture `make_seed()` en `tests/lowering/test_add_object.cpp`
  (meshes directo bajo raíz, sin Transform por objeto) **queda como está**.
- Cascada recursiva (Texture de un Material borrado, Transforms ancestros vacíos).
- `tests/CMakeLists.txt` (pista paralela): cero archivos de test nuevos.

## Gates (obligatorios antes de cerrar)

- `ctest --test-dir build-release` → **20/20**.
- Strict: `cmake --build build-release-strict` con `grep "warning:" | wc -l` → **0**.
- Doc-comments de `ops.cppm` (struct `DeleteObject` + branch) reescritos al nuevo
  contrato, token-consistentes entre sí.

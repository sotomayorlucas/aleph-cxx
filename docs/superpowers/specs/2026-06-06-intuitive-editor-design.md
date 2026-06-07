# Editor de manipulación directa — *seleccionar, ver, mover* (Design Spec)

**Fecha:** 2026-06-06
**Autor:** Lucas (con Claude)
**Estado:** aprobado en brainstorming, pendiente de plan

---

## Objetivo

Hacer el editor interactivo (`aleph_edit`, sin args) **intuitivo para colocar objetos**: el
usuario hace click sobre un objeto, lo **ve resaltado con un contorno (silueta)** en el
viewport, y lo **mueve con las flechas del teclado** en pasos fijos. Cada objeto pasa a
tener su **propio nodo Transform**, de modo que mover uno no mueve la escena entera y queda
la base lista para rotar/escalar a futuro.

## Alcance (lo que NO incluye — YAGNI)

- **No** gizmo de arrastre con mouse (se eligió nudge por teclas).
- **No** rotación ni escala todavía (pero el modelo de Transform-por-objeto las habilita
  sin re-arquitectura).
- **No** undo/redo, **no** outliner/jerarquía, **no** pan de cámara, **no** multi-selección.
  (Quedan como follow-ups posibles, fuera de este spec.)

## Decisiones de diseño (tomadas en brainstorming)

| Decisión | Elección |
|---|---|
| Foco | Ver y mover objetos (manipulación directa) |
| Interacción de movimiento | Nudge por teclas (flechas), paso fijo |
| Marco de las flechas | **Relativo a la cámara**, proyectado al piso; Q/E para Y |
| Resaltado de selección | **Contorno (silueta)** anti-aliased |
| Arquitectura de movimiento | **Un Transform por objeto** (reusa `SetTransform`; toca `AddObject`) |

---

## Arquitectura

Cuatro piezas con interfaces limpias entre capas:

1. **Modelo "un Transform por objeto"** — capa grafo/lowering.
2. **Mover con teclado** — `EditorController` + app shell.
3. **Contorno de selección** — `render.sw` (buffer de id) + app shell.
4. **Discoverability mínima** — app shell (hints + posición en el panel).

### Contexto actual relevante (anclas de código)

- **Topología actual:** un único Transform raíz (identidad) Contiene directamente todas las
  mallas; la posición vive horneada en la geometría local
  (`SphereLocal.center` / `QuadLocal.q`). `apps/aleph_edit/main.cpp:96-181`.
- **`AddObject`** acuña solo `Mesh + Material` bajo `parent`, sin Transform.
  `bridge/src/aleph.lowering/aleph.lowering-ops.cppm:118-122, 504-563`.
- **`SetTransform{target, local}`**: `target` debe ser un nodo `Transform` (si no,
  `OpError::KindMismatch`); reemplaza su `local` y re-lowering recompone todos los
  descendientes. `aleph.lowering-ops.cppm:78-81, 449-455, 258-260`.
- **`LocalTransform` es un `Mat4`**. `graph/src/aleph.types/aleph.types-node.cppm:28`.
- **Lowering compone** `child_world = parent.world * child.local.m` y solo los hijos
  `Transform` multiplican al mundo; un `Mesh` hereda el `world` del padre y luego
  `to_world(geometry, world)` mapea su centro local a mundo.
  `bridge/src/aleph.lowering/aleph.lowering-lower.cppm:146-166, 300, 333-388`.
- **`Transform` puede Contener `Transform`** (legal por reglas de edge-type).
  `graph/src/aleph.types/aleph.types-edge.cppm:39-45`.
- **`EditorController`**: `apply(Op)→expected<void,OpError>` (re-lowering + rebuild de ambos
  backends, incremental), `selected()`, `select()`, `pick(px,py)→optional<NodeId>` (devuelve
  el NodeId de la **Mesh**), `graph()`, `lowered()` (con `handle_map: NodeId→entity index`),
  `face_source()` (face→NodeId). `bridge/src/aleph.edit/aleph.edit-controller.cppm`.
- **`Graph` no expone padre/incoming-edges**: hay que escanear `g.edges()` por
  `kind==Contains && dst==id`. Precedente: `detail::has_incoming_contains` /
  `first_root_transform` en `aleph.lowering-ops.cppm:346-372`.
- **Rasterizer SW** corre a `kSSAA=2×` en `ss_film` y luego box-downsamplea a `film`; el
  overlay de UI se dibuja a 1× después. `apps/aleph_edit/main.cpp:~1010-1018`.
- **Toolkit de UI inmediata**: `ui_panel/ui_label/ui_button/ui_slider_f`,
  `draw_rect/draw_text/draw_text_shadowed`, font bitmap 8×8.
  `render/src/aleph.editor/aleph.editor-ui.cppm`, `…-font.cppm`.
- **`window::Event`** lleva `key` (SDL keysym), `button`, `x/y`, `dx/dy`, `wheel`, y campos
  `shift/ctrl/alt` que **existen pero NO se rellenan** hoy.
  `render/src/aleph.window/aleph.window-event.cppm`, `…-window.cppm`.

---

## Pieza 1 — Modelo "un Transform por objeto"

**Qué hace:** garantiza que cada malla movible tenga su propio Transform controlador, para
que `SetTransform` sobre él mueva **solo** ese objeto.

**Cambios:**

- **`build_initial_graph`** (`apps/aleph_edit/main.cpp`): reestructurar a
  `raíz → Transform_i (identidad) → Mesh_i` para cada malla (esfera, metal, vidrio, piso).
  El piso también recibe Transform (uniformidad; simplemente no se suele empujar).
  La geometría de cada malla sigue en local; con Transform identidad, world == local
  (sin regresión visual).
- **`AddObject`** (`aleph.lowering-ops.cppm`): acuñar `Transform (identidad) + Mesh + Material`
  y cablear `parent —Contains→ Transform —Contains→ Mesh`, `Mesh —References→ Material`.
  Actualizar el doc-comment. Los ids se acuñan determinísticamente; se actualizan los tests
  de lowering/golden que dependan de la secuencia de ids o de la estructura.

**Por qué así:** mover reusa el `SetTransform` existente (no hace falta Op estructural nuevo);
`Transform→Transform→Mesh` es legal y el lowering ya compone Transforms anidados.

**Interfaz/invariantes:** post-condición de `AddObject` y de la escena inicial: **toda Mesh
tiene exactamente un Transform padre vía Contains**. Esto es lo que la Pieza 2 asume.

## Pieza 2 — Mover con teclado

**Qué hace:** traduce teclas en traslaciones del objeto seleccionado.

**Controller (`aleph.edit-controller.cppm`):**

- `std::optional<NodeId> transform_of(NodeId mesh) const;`
  Escanea Contains entrantes a `mesh`; devuelve su nodo Transform padre (o `nullopt` si la
  malla no tiene uno — no debería pasar tras la Pieza 1, pero se maneja).
- `std::expected<void, OpError> translate_selected(Vec3 world_delta);`
  Resuelve `transform_of(*selected())`, lee el `local.m` actual
  (`std::get<types::Transform>(*graph().node(tid)).local.m`), compone
  `Mat4::translate(world_delta) * local.m`, y emite `apply(SetTransform{tid, {nuevo_m}})`.
  Si no hay selección → no-op exitoso (sin tocar nada); si la malla no tiene Transform →
  `OpError::KindMismatch` (propagado).
  *Nota:* `Mat4::translate(Vec3)` ya existe en `foundation/src/aleph.math/aleph.math-mat.cppm:46`.

**Window (`aleph.window-window.cppm` / `-event.cppm`):**

- Rellenar `Event.shift/ctrl/alt` desde `SDL_GetModState()` en `poll_events` (hoy quedan en
  `false`). Necesario para el paso grueso con Shift.
- Exponer las flechas sin filtrar SDL al app: agregar constantes nombradas en el módulo
  window, p.ej. `namespace aleph::window::key { constexpr int Left, Right, Up, Down, ...; }`
  mapeadas desde `SDLK_*` en la impl. El app compara `e.key == aleph::window::key::Left`.

**App shell (`run_live`, `apps/aleph_edit/main.cpp`):**

- Con selección activa, en el bloque de gestos→Ops:
  - Calcular ejes de movimiento **relativos a la cámara, proyectados al piso (XZ)**:
    `fwd = normalize(proj_XZ(target - eye))`, `right = normalize(cross(fwd, up))`.
    `eye = camera().look_from()`, `up = {0,1,0}`.
  - Mapear teclas → `world_delta`:
    `←/→ = ∓right·step`, `↑/↓ = ±fwd·step` (↑ = lejos de la cámara),
    `Q/E = ±{0,1,0}·step` (Y mundo).
  - `step = 0.1f`; si `e.shift` → `step *= 5` (paso grueso).
  - Cada tecla relevante → una llamada `controller.translate_selected(world_delta)`.
  - Marcar `invalidate()` (ya existe) para volver a raster y refrescar; el `apply` ya
    reconstruye ambos backends de forma incremental.

**Flujo de datos:** tecla → `world_delta` (app) → `translate_selected` (controller) →
`SetTransform` → re-lowering → ambos backends → siguiente frame muestra el objeto movido.

## Pieza 3 — Contorno de selección

**Qué hace:** dibuja una silueta anti-aliased alrededor del objeto seleccionado.

> **NOTA as-built (2026-06-07):** el mecanismo que se implementó NO es el `id_out` de
> abajo, sino un **segundo pase de rasterizado** de solo las caras del objeto
> seleccionado en un depth scratch limpio (cobertura = `depth>0`) + una función pura
> `aleph::render::sw::draw_selection_outline`. Esto evita tocar el rasterizer caliente
> compartido (`rast_scan_textured` + sus tests) — riesgo mucho menor, mismo resultado
> (silueta anti-aliased, x-ray). Ver el plan (`2026-06-06-intuitive-editor.md`, Tareas
> 7-8) y `apps/aleph_edit/main.cpp` (`run_live`). El diseño `id_out` de abajo queda como
> registro histórico.

**Render (`render/src/aleph.render.sw/`):**

- `rasterize(...)` (público en `aleph.render.sw-rasterize.cppm`) gana un parámetro
  **opcional** `std::uint32_t* id_out = nullptr`, threadeado hasta el scan-converter
  (`aleph.render.sw-rast_scan.cppm`, donde vive el depth-test por fragmento).
  En el mismo pase con depth-test, cuando un fragmento gana el test, escribe en `id_out[px]`
  el **índice de entidad** de esa cara (el mismo índice que usa `lowered().handle_map`).
  Fragmentos sin cobertura → centinela `0xFFFFFFFFu`. Parámetro defaulteado ⇒ los llamadores
  y tests actuales no cambian.
  *Detalle:* `id_out` está a resolución **SSAA** (igual que `ss_film`/`ss_depth`).

**App shell (`run_live`):**

- Asignar `std::vector<u32> ss_id(kSSAA*kSSAA*W*H)`; pasar `ss_id.data()` a `rasterize`.
- Si hay selección: `sel_idx = lowered().handle_map.get(*selected())` (entity index).
  Computar el contorno **a resolución SSAA, antes del downsample**: un píxel es "borde" si
  `id != sel_idx` pero algún vecino en un radio `k` (p.ej. `k = kSSAA`, ~1px a 1×) tiene
  `id == sel_idx`. Pintar esos píxeles con el color de contorno (p.ej. naranja
  `{1.0, 0.6, 0.1}`) en `ss_px`.
- El box-downsample existente promedia ⇒ contorno anti-aliased gratis.
- Solo corre cuando hay selección (sin costo en el caso común).

**Por qué así:** reutiliza el SSAA + downsample que ya existen; el único cambio en el core es
un buffer de id opcional (aditivo, sin tocar la firma para llamadores existentes).

## Pieza 4 — Discoverability mínima

**Qué hace:** que las teclas nuevas sean visibles y dé feedback de posición.

**App shell (`run_live`, panel de UI existente):**

- Actualizar los hints (`ui_label`) para incluir: `ARROWS MOVE  Q/E UP-DN  SHIFT FAST`.
- Cuando hay selección, mostrar la **posición mundo** del objeto en el panel (leída del
  Transform controlador o del centroide lowered), p.ej. `POS  x.xx  y.yy  z.zz`.
- Reusa `ui_label`/`draw_text_shadowed`; sin primitivas nuevas.

---

## Manejo de errores

- `translate_selected` sin selección: no-op exitoso (no es error).
- Malla sin Transform padre (no debería ocurrir tras Pieza 1): `OpError::KindMismatch`,
  propagado y **ignorado en silencio** por el shell (el objeto simplemente no se mueve;
  no se rompe el loop).
- `handle_map.get(selected)` nulo (selección obsoleta tras un delete): se trata como "sin
  selección" para el contorno y el movimiento.
- Todos los `apply()` son all-or-nothing (ya garantizado por el controller): un Op fallido
  deja el grafo intacto.

## Testing

**Unit (capa lowering/controller):**

- `transform_of` devuelve el Transform controlador para (a) una malla de la escena inicial y
  (b) una malla creada con `AddObject`.
- `translate_selected(delta)` mueve **solo** el target: la world-pos del objeto seleccionado
  cambia exactamente `delta`; las world-pos de las demás entidades quedan idénticas.
- `AddObject` produce la estructura `parent → Transform → Mesh → Material` (assert sobre los
  edges Contains/References y los NodeKind).

**Unit (render):**

- `rasterize` con `id_out`: un píxel cubierto reporta el índice de entidad correcto; un píxel
  de fondo reporta el centinela. Escena mínima determinista (1–2 triángulos, MVP fijo).

**Manual / visual (no bloqueante):**

- Correr `aleph_edit`, seleccionar la esfera, moverla con flechas, confirmar contorno + que
  solo se mueve ella. (No automatizable sin display; se documenta el procedimiento.)

**Gates (obligatorios antes de merge):**

- `cmake --build build-release && ctest --test-dir build-release` → **20/20** (o +N nuevos).
- Strict: `cmake --build build-release-strict` con `grep warning: | wc -l` → **0**.
- ASan/UBSan: `LSAN_OPTIONS=suppressions=tests/asan.supp ASAN_OPTIONS=detect_leaks=1
  ./build-asan/tests/aleph_tests` desde la raíz → verde.

## Orden de construcción sugerido

1. **Pieza 1** (modelo Transform-por-objeto) + sus tests — base de todo lo demás.
2. **Pieza 2** (mover con teclado) + tests — depende de 1.
3. **Pieza 3** (contorno) — independiente de 1/2, puede ir en paralelo.
4. **Pieza 4** (hints/posición) — al final, depende de 2 para los textos.

## Riesgos

- **Tocar `AddObject` rompe tests de lowering** que asumen ids/estructura: esperado; se
  actualizan como parte de la Pieza 1 (aceptado por el usuario al elegir Transform-por-objeto).
- **`gcc-16` "Bad file data"** si se editan structs IR dual-definidos (`MaterialParams`/
  `LoweredEntity`/`Camera`/`Scene` en `:lower` y `:lowered`): no se prevé tocarlos aquí, pero
  si surge, editar ambas copias token-idénticas.
- **Índice de entidad del rasterizer vs `handle_map`**: hay que verificar que el índice que
  escribe `id_out` coincide con el de `lowered().handle_map`; si difieren, mapear vía
  `face_source` en el shell.

# HUD de tiempos por fase + dirty-flag/pacing de frames en el editor (Design Spec)

**Fecha:** 2026-06-09
**Autor:** Lucas (con Claude)
**Estado:** aprobado, en implementación

---

## Objetivo

Darle al editor interactivo (`aleph_edit`) **visibilidad de rendimiento** (una línea de HUD
con el tiempo medio de cada fase del frame) y **dejar de quemar CPU cuando no hay nada que
redibujar** (dirty-flag por frame + re-present del frame cacheado + sleep corto).

## Problema verificado

- **Cero visibilidad de rendimiento:** el HUD actual (`apps/aleph_edit/main.cpp:1182-1189`)
  muestra solo `ENTITIES/LIGHTS/FACES`. `Window::perf_counter`/`perf_frequency`
  (`render/src/aleph.window/aleph.window-window.cppm:118-119`) están exportados pero
  **nadie los llama** en todo el repo.
- **`run_live` hace busy-spin:** el `while` de `main.cpp:898` no duerme nunca (no hay
  `sleep` en todo `main.cpp`); `present()` es un `SDL_BlitSurface` +
  `SDL_UpdateWindowSurface` sin sincronía (`window.cppm:102-108`). Con cero input:
  - durante la ventana pre-idle de 250 ms (`kIdleMs`, `main.cpp:846`) re-rasteriza
    **completa** la escena supersampleada a `kSSAA=2` (1600×1200; `main.cpp:71,779`)
    cada frame, y
  - tras converger el path-trace (`pt_samples >= kMaxSpp` salta nuevos batches en
    `:1040`) sigue corriendo el loop W×H de crossfade/tonemap + present cada frame
    (`main.cpp:1206-1220`).

## Diseño

Dos rebanadas, en orden. Toda la instrumentación vive en `run_live` (shell); el
`EditorController` **no se toca**.

### Rebanada 1 — HUD de tiempos por fase

Nueva partición **`aleph.editor:perf`** (`render/src/aleph.editor/aleph.editor-perf.cppm`,
registrada en el CMakeLists y el umbrella igual que `:ui`/`:picking`). Pura, testeable,
sin ventana:

- `export struct RollingMean` — ring buffer de capacidad fija **30** (constante nombrada),
  `void push(float ms) noexcept`, `float mean() const noexcept` (media sobre las muestras
  presentes; `0.0f` si está vacío). Sin asignación dinámica.
- `export struct PhaseTimes` — los tiempos (ms) de un frame:
  `events_ms, step_ms, raster_ms, outline_ms, downsample_ms, ui_ms, present_ms, frame_ms`
  (las fases reales de `run_live`).

**Cableado en `run_live`:** cronometrar cada fase con deltas de
`win.perf_counter()/perf_frequency()` → push en un `RollingMean` por fase; dibujar UNA
línea extra de HUD bajo el readout `ENTITIES/LIGHTS/FACES` existente (mismo patrón
`snprintf` + `draw_rect`/`draw_text_shadowed`), p.ej.:

```
FRM 12.3 EVT 0.1 STP 0.0 RAS 8.1 OUT 0.4 DSP 1.2 UI 0.1 PRS 0.9 | 81 FPS
```

Fases: manejo de eventos+Ops, paso de simulación (wave), rasterizado, contorno de
selección, downsample, dibujo de UI, tonemap/present, y el frame completo (FPS = 1000 /
media de `frame_ms`). El costo es 2 lecturas de `perf_counter` por fase — despreciable.

### Rebanada 2 — dirty-flag + pacing

Cuando **nada cambió** en modo raster, saltar el trabajo de render y re-presentar el frame
cacheado (la superficie trasera de SDL persiste los píxeles tonemapeados) + dormir ~4 ms
(constante nombrada, `std::this_thread::sleep_for`).

En la misma partición `:perf`, el decisor **PURO** (eso lo hace testeable):

```c++
export struct FrameSignals {
    bool had_input; bool op_applied; bool sim_stepping; bool crossfade_active;
    bool pt_accumulating; bool view_rebake_pending; bool selection_changed;
    bool first_frame;
};
export constexpr bool frame_dirty(FrameSignals s) noexcept;  // true si CUALQUIER señal
```

**Cableado en `run_live`:** juntar las señales cada iteración. Condiciones de corrección
críticas — el frame DEBE seguir sucio:

- mientras el alpha del crossfade raster↔PT sea < 1 (`main.cpp:1194-1220`);
- mientras el PT acumule y no haya convergido aún;
- en modo wave/sim (hace `step()` cada frame, `main.cpp:1079`);
- mientras haya un re-bake view-dependent throttled pendiente (`view_dirty`,
  `main.cpp:880-882,1066-1076`);
- ante cualquier evento de input; al aplicar un Op o cambiar la selección; en el primer
  frame.

Cuando está limpio (modo raster): saltar `clear_sky` + `rasterize` + contorno +
`downsample` + redibujo de UI; `win.present()` re-presenta la superficie sin cambios +
sleep. **Hold de PT convergido:** con el PT convergido y la imagen convergida ya
presentada, saltar también el loop W×H de crossfade/tonemap (present cacheado + sleep).

**HUD:** anexar la etiqueta `IDLE` a la línea de tiempos mientras se saltan frames (el
frame de transición sucio→limpio se dibuja una vez con la etiqueta; los siguientes frames
limpios re-presentan ese frame cacheado).

## Testing

**Unit (doctest, `tests/render/test_editor_perf.cpp`, partición pura — sin SDL en runtime):**

- `RollingMean`: vacío → `0.0f`; llenado parcial → media de las presentes; media exacta
  con ventana llena; wraparound tras >30 pushes (solo sobreviven las 30 más nuevas).
- `frame_dirty`: tabla de verdad — todo-false → `false`; cada flag individual → `true`
  (loop sobre los 8 flags).

**Gates (obligatorios antes de merge):**

- `ctest --test-dir build-release` → **20/20**.
- Strict: `cmake --build build-release-strict` con `grep "warning:" | wc -l` → **0**.
- `aleph_edit --headless` sigue saliendo 0 con su artefacto idéntico (el modo headless no
  se toca).

**Manual / visual (no bloqueante):** correr `aleph_edit`, ver la línea de tiempos; dejar
de tocar el mouse y confirmar que aparece `IDLE` y la CPU baja.

## Alcance (lo que NO incluye — YAGNI)

- **No** instrumentación CSV ni headless (la telemetría es solo la línea de HUD en vivo).
- **No** tiempos internos del controller (lower/rebuild): esta rebanada cronometra solo a
  nivel `main.cpp`; si una fase del shell aparece cara, abrir el controller es el
  follow-up natural.
- **No** vsync ni reloj de frame-rate fijo: el pacing es solo el sleep de ~4 ms en frames
  limpios.
- **No** dirty-rects parciales: el dirty-flag es por frame completo.

## Riesgos

- **Frame que se queda "pegado":** si una señal de suciedad falta (p.ej. el re-bake
  pendiente o el crossfade), la pantalla se congela con contenido viejo. Mitigación: el
  decisor es puro y la tabla de verdad está testeada; el cableado enumera explícitamente
  las condiciones críticas de arriba.
- **`aleph_editor` está gateado por SDL2** (`render/CMakeLists.txt:6-9`): el test nuevo se
  registra en el bloque `if(ALEPH_HAVE_SDL2)` de `tests/CMakeLists.txt` (igual que
  `editor/test_orbit.cpp`), aunque la partición en sí no use SDL en runtime.

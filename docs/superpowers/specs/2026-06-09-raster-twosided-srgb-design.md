# Spec de diseño — Rasterización de dos caras + transferencia sRGB real (dos slices de corrección)

**Objetivo:** cerrar dos defectos de corrección verificados del rasterizador por software, en dos slices independientes: (A) el sombreado es de dos caras pero la rasterización descarta la cara trasera — orbita bajo el suelo o detrás de un muro y el quad DESAPARECE aunque el ray de picking del editor sí lo encuentra; (B) la transferencia de salida es gamma-2.0 (`sqrt`) y la decodificación de texels es `byte/255` plano — admitido en el propio código ("NO sRGB") y en el spec 2026-06-06 — en vez del par OETF/EOTF sRGB real. Fecha 2026-06-09 · Estado: DISEÑO. Determinismo (bytes idénticos entre ejecuciones) es OBLIGATORIO en ambos slices.

Contexto (verificado en `a62885e`): el shading de quads/tris usa `|N·L|` de dos caras (`build_sw.cppm:583`, aplicado en quads `:665-668` y tris `:686-688`; las esferas usan el clamp de una cara `:741-744`) y el picking del editor (`aleph.edit-controller.cppm:632-658`) no descarta por signo del determinante — pero `rast_scan_textured` hace cull incondicional del área con signo negativa (`if (signed_area < 0.0f) return;`, `rast_scan.cppm:36-38`; winding CW-frontal documentado en `primitives.cppm:16-18`). En transferencia: `byte_from_linear` es `sqrt` (`tonemap.cppm:12-16`), `argb_to_linear` es `byte/255` plano (`rast_scan.cppm:20-25`, usado en `:156,:165`), el PT replica el checker linealmente (`kCheckerHi=1.0`/`kCheckerLo=128/255`, `rt-material.cppm:17-18`) y el bake de lightmaps codifica `lit*255` lineal (`lightmap.cppm:121-126`) decodificado por el mismo `argb_to_linear` — si el decode pasa a sRGB, el encode del bake DEBE pasar a sRGB o el camino de lightmaps de `aleph_sw` se desplaza.

## 1. Slice A — rasterización de dos caras (quads/tris)

### Diseño
- **`Face`** (`scene_rt.cppm`): nuevo campo `bool two_sided = false;` — contrato: si es `true`, `rast_scan` re-enrolla el triángulo de cara trasera en vez de descartarlo. El default `false` deja TODO caller existente byte-idéntico.
- **`build_sw`**: `push_tri` gana un parámetro `bool two_sided` que copia al Face. Call sites: `emit_quad` ×2 y `emit_tri` ×1 → `true` (su shading ya es de dos caras, `|N·L|`); `emit_sphere` ×2 → `false` (superficie cerrada, su shading es de una cara — el cull sigue siendo correcto Y un ahorro real: ~la mitad de los triángulos de cada esfera se descartan).
- **`rasterize`**: pasa `face.two_sided` a `rast_scan_textured` como parámetro EXPLÍCITO (no default; `Face` no es visible dentro de `rast_scan`).
- **`rast_scan_textured`**: cuando `signed_area < 0` — si `!two_sided`, cull como hoy; si `two_sided`, `std::swap(v1, v2)` ANTES de cualquier setup de gradientes/aristas (los atributos uv/col/inv_w viajan dentro de `ScreenVert`, así que el swap los lleva consigo). El área con signo del triángulo re-enrollado es exactamente `-signed_area > 0`; nada aguas abajo lee `signed_area`, así que el resto del scan (orden-y, gradientes, spans) queda INTACTO.
- El outline de selección (`outline.cppm`) reutiliza `rasterize`, así que pasa a ser correcto desde la cara trasera GRATIS (sin cambio de firma).
- Comentario de winding en `primitives.cppm:16-18` actualizado (el cull ya no es incondicional).

### Oráculos (tests, `tests/render/test_sw_rasterize.cpp` — TDD: test rojo primero)
1. Un quad `Face` con `two_sided=true` visto desde su cara trasera produce cobertura no nula Y escribe depth.
2. La misma geometría con `two_sided=false` produce cobertura cero (comportamiento de hoy).
3. De frente, `two_sided=true` y `two_sided=false` son BYTE-IDÉNTICOS (misma escena rasterizada dos veces) — guarda contra cambios accidentales en el setup de gradientes.

## 2. Slice B — par de transferencia sRGB real

### Diseño
- **OETF de salida** (`tonemap.cppm`): `byte_from_linear` pasa de `sqrt` a la OETF sRGB por tramos (`x<=0.0031308 ? 12.92x : 1.055·x^(1/2.4)−0.055`), cuantizada a byte con redondeo al más cercano.
- **EOTF de texels** (`rast_scan.cppm`): `argb_to_linear` decodifica cada canal con la EOTF sRGB (`c<=0.04045 ? c/12.92 : ((c+0.055)/1.055)^2.4`).
- **RENDIMIENTO + DETERMINISMO** (ambas funciones están en bucles calientes; `rasterize` es multihilo):
  - *Decode*: LUT de 256 entradas f32 — EXACTA por construcción (el dominio es un byte) y trivial.
  - *Encode*: una `pow` de libm por canal por píxel serían ~1.4M llamadas por present a 800×600 — coste innecesario en el bucle de present. En su lugar: tabla de 255 UMBRALES lineales `t[i] = srgb_eotf((i+0.5)/255)` (las fronteras de decisión exactas del cuantizador `round(srgb_oetf(x)·255)`) + búsqueda binaria (8 pasos); el resultado coincide con el cuantizador ideal para todo f32 salvo a <1 ulp de una frontera, donde el umbral f32-redondeado decide de forma determinista. Error ≤ 0 LSB respecto a la definición tabulada.
  - Ambas tablas son `static` locales de función (magic statics de C++: init única thread-safe) construidas en `double` con la fórmula exacta → bytes idénticos entre ejecuciones.
- **Paridad raster↔PT preservada vía la constante**: `kCheckerLo` (`rt-material.cppm`) pasa de `128/255≈0.502` al valor sRGB-decodificado del byte `0x80`: **`0.215860501f`** (= `((128/255+0.055)/1.055)^2.4` calculado en double y redondeado a f32 — el MISMO valor que produce la LUT de decode del raster, bit a bit). `kCheckerHi` sigue siendo `1.0` (el byte `0xFF` decodifica a 1.0 exacto). Los tests de scatter referencian las constantes simbólicamente y se auto-actualizan.
- **Consistencia de lightmaps**: el encode del bake (`lightmap.cppm:121-126`) pasa de `lit*255` lineal a `byte_from_linear(lit)` (la misma OETF sRGB), de modo que encode/decode hacen round-trip en el camino de `aleph_sw`.
- Comentarios "NO sRGB" reescritos (`scene_rt.cppm:49-54`, `rt-material.cppm:15-16`); nota as-built fechada 2026-06-09 en el spec 2026-06-06; `tonemap_argb8888_gamma2` renombrado a `tonemap_argb8888_srgb` (el nombre viejo describía la transferencia que se elimina).

### Oráculos (tests — TDD: el round-trip es EL test y va primero)
1. **Round-trip (clave):** para todo byte `b∈0..255`, `byte_from_linear(srgb_decode_byte(b)) == b`.
2. Los pins existentes de `test_tonemap` sobreviven (`0→0`, `1→255`, rango en 0.25); nuevo pin en la frontera del tramo lineal: `byte_from_linear(0.0031308f) == 10` (= `round(0.04045·255)`).
3. Checker: `kCheckerLo == 0.215860501f` y paridad con el decode del raster del byte `0x80`.
4. Lightmap: el bake con la nueva OETF sigue produciendo texels en rango (test existente) y encode/decode redondean al mismo byte.

### Efectos esperados sobre goldens
- El golden byte-exacto de `test_build_sw.cpp:709-743` compara `vcol` LINEAL (pre-tonemap) — NO afectado (verificar).
- Ningún test pinea bytes post-tonemap salvo `test_tonemap` (verificado por grep); cualquier re-pin se hace DELIBERADAMENTE y se lista en el reporte. Nunca debilitar una aserción.

## 3. Determinismo (obligatorio, ambos slices)
Slice A es una transformación pura de control de flujo (swap + cull condicional); cero RNG, cero dependencia de orden de hilos nueva (el orden painter + z-test por píxel no cambian). Slice B: LUTs construidas una vez (magic statics) con `std::pow` de libm — bit-estables por ejecución y entre ejecuciones en la misma máquina; las consultas son lecturas puras. Oráculo operativo: `./build-release/tests/aleph_tests` dos veces, ambas verdes.

## 4. Frontera de alcance (YAGNI)
**Dentro:** el flag `two_sided` Face→build_sw→rast_scan; el par OETF/EOTF sRGB + LUTs; `kCheckerLo` re-anclado; encode sRGB del bake de lightmaps; comentarios/docs. **Fuera:** esferas de dos caras (cerradas — el cull es correcto y más barato); filtrado de lightmaps en espacio lineal (el bilinear mezcla bytes en espacio gamma — aproximación preexistente, sin cambio); texturas imagen; tonemapping HDR (la salida sigue siendo clamp [0,1] + OETF).

# Design Spec — Raster SSAA (visual-alignment slice 1 of 3)

**Goal:** kill the jagged/faceted silhouettes in the raster *preview* by 2×2 supersampling, so the fast nav view reads much closer to the path-traced truth. Date 2026-06-05 · Status: DRAFT.

Context: this is slice 1 of the "align raster preview with path-trace" roadmap (then: contact shadows/AO, then exposure+diff). The path tracer already antialiases (per-pixel jitter across spp); only the **rasterizer** preview is aliased.

## 1. Approach (chosen)

**SSAA (supersampled antialiasing):** render the raster scene at `kSSAA×` resolution into a scratch linear-`Vec3` film, then **box-downsample** (average each `kSSAA×kSSAA` block) into the real `W×H` film. Averaging happens in **linear space** (the film is linear `Vec3`; tonemap/gamma is applied *after*, at present/`write_ppm`), so the AA is energy-correct. `kSSAA = 2` (4 samples/px).

Why SSAA over MSAA/coverage: the rasterizer has no separate coverage/edge pass, and SSAA is trivially correct (just more pixels), deterministic, and reuses the *existing* `rasterize` verbatim. We **reuse the existing 1× MVP unchanged**: resolution enters *only* at the viewport mapping (`detail::to_screen` reads `fb.width`/`fb.height`), and a **uniform** `kSSAA×` scale preserves aspect (`(kSSAA·W)/(kSSAA·H) == W/H`, the ratio `orbit_mvp` bakes into the projection), so rendering into a bigger film just supersamples. `kSSAA` must be uniform in x and y. No rasterizer-core changes.

## 2. Components

### 2.1 New primitive — `downsample_box` (`aleph.render.sw` or a small util)
```cpp
// Average each `ss×ss` block of `src` (ss*dst.width × ss*dst.height, linear Vec3)
// into `dst` (dst.width × dst.height). Pure, deterministic (fixed summation order).
void downsample_box(const aleph::render::common::Film& src,
                    aleph::render::common::Film& dst, int ss) noexcept;
```
Contract: `src.width == ss*dst.width && src.height == ss*dst.height`. `dst[y][x] = (1/ss²)·Σ_{i,j<ss} src[ss·y+i][ss·x+j]`, indexing rows by each film's own `stride_pixels`. `ss==1` → straight copy.

### 2.2 Wiring (the only other change) — at each raster call site
The pattern at every site that rasterizes the 3D scene becomes:
```cpp
clear_sky(ss_film);                      // clear_sky already works at any resolution
std::fill(ss_depth.begin(), ss_depth.end(), 0.0f);
aleph::render::sw::rasterize(scene, mvp, ss_film, ss_depth, pool);   // renders at ss·W × ss·H
aleph::render::sw::downsample_box(ss_film, film, kSSAA);             // -> the 1× film
// ... any UI overlay / HUD / sliders are drawn on `film` at 1× AFTER this ...
```
Scratch buffers (`ss_film` = `Vec3[kSSAA²·W·H]`, `ss_depth` = `f32[kSSAA²·W·H]`) are allocated **once** outside the frame loop, next to the existing `film`/`depth`, as heap `std::vector`s (the `aleph_edit` sites already heap-allocate `film_px`/`depth`, so 4× is just a larger vector).

**Scratch-`Film` construction invariant (REQUIRED — `rasterize` asserts it):** the scratch `Film` MUST have `stride_pixels == kSSAA·W` and `ss_depth.size() == (kSSAA·W)·(kSSAA·H)`. `rasterize` hard-asserts `fb.stride_pixels == fb.width` and `depth.size() >= fb.width*fb.height` (`aleph.render.sw-rasterize.cppm:57-59`), and color vs depth indices coincide only when `stride == width`. Build it as `Film{ss_px.data(), kSSAA*W, kSSAA*H, kSSAA*W}` (stride == width), NOT with a stale 1× stride.

Sites (all in `apps/aleph_edit/main.cpp`):
- `run_headless` `dump` (raster PPM).
- `run_wave` `dump`.
- `run_live` raster branch — **UI overlay must stay at 1×**: supersample only the 3D scene, downsample to `film`, THEN draw `ui_*`/HUD/sliders on `film` (the existing overlay block is already after the rasterize call, so no reordering — just insert the downsample between rasterize and the UI). The UI is text/rects; supersampling it is pointless and would blur it.

`kSSAA` is a single `constexpr int kSSAA = 2;`.

**NOT wired this slice — `apps/aleph_sw/main.cpp` (deferred):** its `film` comes from a fixed static arena sized exactly 1× (`alignas(64) static unsigned char film_scratch[W*H*sizeof(Vec3)]`, `aleph_sw/main.cpp:47`). A 2× scratch overflows that arena; `Arena::allocate` returns `nullptr` *without aborting* (`aleph.alloc-arena.cppm:22`) → a silent null write in `rast_scan`. Wiring `aleph_sw` would require resizing that static buffer by `kSSAA²` (and an inline 2× sky clear, since it doesn't use `clear_sky`). Out of scope here; `aleph_sw` is a secondary standalone sw-demo, not the editor. Follow-up if desired.

## 3. Determinism
`rasterize` is already deterministic; `downsample_box` is a fixed-order linear average → deterministic. The headless `--wave`/`--headless` byte-identical-frame contract still holds (the whole path stays a pure function of inputs). The existing wave determinism check (`cmp` across runs) must still pass.

## 4. Error handling (`aleph_flags_isa`)
`downsample_box` asserts the size contract (`src.width==ss*dst.width`, etc.) in debug and is a no-op-safe copy at `ss==1`. No allocation inside it (caller owns buffers). No exceptions.

## 5. Testing
- **Unit (`tests/render/`):** a `4×4` linear `Vec3` src with known per-block values → `downsample_box(ss=2)` → `2×2` dst equals the exact per-block averages; `ss==1` → identity copy; a non-uniform block averages correctly.
- **Determinism:** unchanged headless `--wave` frames remain byte-identical across two runs.
- **Visual:** a before/after of a raster frame (jagged vs smooth silhouette) via `visual_review.sh`.

## 6. Build order
1. `downsample_box` + unit test (no call-site changes yet).
2. Wire `run_headless` + `run_wave` (headless, easy to verify via PPM + determinism).
3. Wire `run_live` raster branch (downsample between rasterize and the UI overlay; UI stays at 1×).
4. Regenerate a before/after artifact.

## 7. Scope boundary (YAGNI)
**In:** 2×2 SSAA for the raster preview, linear-space box downsample, UI at 1×. **Out (hooks kept):** MSAA/coverage AA (not needed — SSAA is correct); temporal AA (no history buffer); antialiasing the path trace (already jittered); making `kSSAA` runtime-configurable (constexpr is enough; a later slice can expose it). Perf: 2× raster = 4× raster pixels, acceptable for the fast preview (raster is the cheap path); if it ever bites, `kSSAA` is the one knob.

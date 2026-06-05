# Design Spec — Raster exposure alignment + diff view (visual-alignment slice 3 of 3)

**Goal:** (A) close the residual brightness gap between the raster preview and the path-trace truth with a single exposure constant, and (B) turn the raster↔truth comparison from "by eye" into a quantitative **|raster − path_trace| heatmap** tool. Date 2026-06-05 · Status: DRAFT. *(Lightest slice — one engine constant + a tooling pass; empirically tuned + self-reviewed rather than the heavy multi-agent workflow.)*

Context: final slice of "align raster with path-trace" (after SSAA + contact-shadows/tessellation). Measured *now* (post slices 1–2) on the headless scene: raster mean luminance ≈ 232 vs PT ≈ 207 (raster ~12% too bright); raster floor ≈ 248 vs PT ≈ 205 (the earlier `kAmbient`/`kLightScale` over-tune). The preview is **too bright**, so exposure *darkens* it toward the truth.

## Part A — Exposure alignment

### A.1 Approach
A single `constexpr f32 kRasterExposure` multiplied into the **shade output** in `build_sw`'s `shade_face` (scales baked geometry colour only — NOT the shared sky gradient, which already matches; and NOT the wave φ-colormap path, which is overridden before shading). Tuned so the raster mean luminance ≈ the PT mean on the reference headless scene (target `kRasterExposure ≈ 0.88`, refined empirically to bring the PT/raster mean ratio to ≈ 1.0).

### A.2 Change
In `aleph.lowering-build_sw.cppm`, add `inline constexpr aleph::math::f32 kRasterExposure = <tuned>;` near `kAmbient`, and scale `shade_face`'s returned `lit` by it: `return lit * kRasterExposure;`. Everything upstream (ambient, diffuse, shadows, self-emit) is computed as before, then uniformly exposure-scaled. Determinism unchanged (one pure-f32 multiply). The wave demo is unaffected (φ path skips `shade_face`).

### A.3 Tuning procedure (in the implementation)
Render `--headless`, measure raster vs PT mean luminance (script), set `kRasterExposure = pt_mean/raster_mean` (≈0.88), rebuild, re-measure → confirm ratio ∈ [0.97, 1.03]. The constant is fixed (deterministic), not runtime.

## Part B — Diff view (tooling only; no engine change)

### B.1 Approach
The headless dump already writes `stepN_<label>_raster.ppm` and `stepN_<label>_pt.ppm` per step. A new `tools/visual_review.sh diff [out]` mode renders headless, then for each step computes a per-pixel difference heatmap and tiles **raster | path-trace | diff** per step into one contact sheet.

### B.2 Diff metric + heatmap
Per pixel, `d = mean(|raster_rgb − pt_rgb|)` (0..255 in gamma/PNG space — comparing the displayed images is what "looks different" means). Map `d` through a perceptual hot colormap (black=match → blue → red → white=large) so disagreement regions pop. Implemented in a small Python/PIL pass inside the script (PIL is available; deterministic). Also print the **mean diff** per step (a single number — the residual the exposure tune leaves).

### B.3 Output
`<out>/_diff_contact.png`: rows = steps, columns = [raster, path-trace, diff-heatmap]. The per-step mean-diff numbers are printed to stdout and titled on the sheet.

## Determinism
Part A is one constexpr multiply (deterministic; the `same_face`/`vcol` oracle and `--wave` byte-identity hold — wave skips shade). Part B is post-processing of deterministic PPMs.

## Testing
- **Exposure:** after tuning, the measurement script shows raster mean ≈ PT mean (ratio ∈ [0.97,1.03]) on the headless scene; `ctest` stays green (no test asserts absolute raster brightness; `test_build_sw` pins `vcol` *equality across two builds*, not a value, so a global scale is fine — but VERIFY the determinism oracle still passes, and that no test hard-codes a pre-exposure `vcol` value).
- **Diff tool:** `visual_review.sh diff` produces `_diff_contact.png` and prints decreasing/expected per-step mean-diff numbers; the diff heatmap is darkest where raster≈truth (lit floor) and hottest at the known divergences (the path-trace's soft shadows/noise the cheap raster lacks).
- **Visual:** the diff contact sheet, shipped as the artifact.

## Scope boundary (YAGNI)
**In:** one exposure constant (mean-matched), a diff-heatmap review mode, mean-diff readout. **Out (hooks kept):** per-channel/white-balance correction (single scalar is enough — the gap is brightness, not tint); auto-exposure at runtime (fixed constant is deterministic + sufficient); SSIM/perceptual metrics (mean-abs is enough to see *where* they differ); matching the *path-trace's* exposure to the raster (the tracer is the reference, not the other way). `kRasterExposure` is `constexpr`.

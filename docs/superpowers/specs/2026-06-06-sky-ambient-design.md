# Design Spec — Directional Sky Ambient + Sun Tint (visual slice 4b)

**Goal:** replace the raster preview's flat grey ambient (`kAmbient = 0.45`, direction-independent) with a **directional ambient** = a hemispheric **sky** term (cool from above, neutral at the horizon, sampled by the surface normal) **plus** a soft warm **sun tint** wrapped from the dominant light's direction. The ambient stops being uniform grey and reads like environment light — up-faces pick up the sky, the lit side gets a warm fill near the terminator — so the preview looks "rendered". Date 2026-06-06 · Status: REVISED (adversarial review applied). Follows visual slice 4a (per-vertex AO).

Context: the **visible background sky already matches** between path-trace (`render.common::sky_sample` on ray-miss, by `dir.y`) and raster (`clear_sky` screen-`y` gradient, same `kSky`). That gap is closed. The remaining gap is the **ambient term** in the bake: `bridge/.../aleph.lowering-build_sw.cppm`'s `shade_face` seeds `lit = base_albedo*(kAmbient*ao) + self_emit` with a single grey scalar. This slice makes that seed directional + colored. Stays entirely in `build_sw` (the bridge); `render.sw` stays graph-free; the path tracer is untouched.

## 1. Approach

Per shaded vertex with unit normal `N` (and the face's `two_sided` flag), the ambient color becomes:

```
amb = sky_ambient(N) + sun_tint(point, N, lights, two_sided)
lit = hadamard(base_albedo, amb) * ao + self_emit
```

- **`sky_ambient(N)`** — a hemispheric gradient sampled by the **world-up-reoriented** normal (the floor normal points DOWN — `cross(u,v) = (0,-64,0)` — and a quad/tri normal's sign is arbitrary winding, so reuse the AO world-up flip `N_up = dot(N,{0,1,0})<0 ? -N : N`). `a = clamp(N_up.y, 0, 1)`; `sky_ambient = lerp(kSkyHorizon, kSkyZenith, a)`. A floor (N_up.y≈1) gets the bright cool zenith; a vertical face (N_up.y≈0) gets the dimmer horizon. **Known preview simplification:** because the flip is unconditional, a *closed convex* surface (a sphere) has its BOTTOM read the same bright/cool zenith as its TOP (darkest band at the equator) — physically a sphere underside should see the dimmer ground. We accept this: the flip is *required* for the floor (the dominant ground surface, whose winding sign is arbitrary), and sphere undersides rest on the floor and are rarely seen. (The principled alternative — raw `N` in `sky_ambient` and reorienting the floor's normal at the `emit_quad` site — is a noted §7 hook, not built: `emit_quad` is generic and does not know a quad is "the floor".)
- **`sun_tint(point, N, lights, two_sided)`** — a soft warm fill from the dominant light, a cheap half-bounce fake. Pick the primary light, form `L` toward it, wrap `w = max(0, 0.5*nd + 0.5)` where `nd = two_sided ? |dot(N,L)| : dot(N,L)`; return `kSunStrength * w * kSunColor`. The wrap is **0 at `nd=-1`** (a sphere face fully away from the light → *zero* fill), **0.5 at the terminator** (`nd=0`), **1 facing the light** (`nd=+1`) — it tints the near-terminator band and decays to zero on the fully-shadowed back; it does NOT blanket the dark hemisphere (measured dark-side mean fill ≈ 0.012 lum, global max ≈ 0.05). On **two-sided** geometry (quads/tris, whose normal sign is arbitrary) it uses `|dot(N,L)|` so the warm fill depends on *lighting, not winding* — consistent with the direct-light path's `|N·L|` (build_sw.cppm:444-445) and AO's world-up flip. On a **sphere** (`two_sided=false`, meaningful outward normal) it uses the signed `dot`, so the lit hemisphere warms and the far side does not. No primary light ⇒ `{0,0,0}`.
- Both terms live INSIDE the **AO-multiplied** ambient (so AO occludes the fill too — the pooling under the sphere darkens the whole ambient, not just the sky half). The **direct** light path (`hadamard(base_albedo, emit) * (ndl·atten·kLightScale·vis)`) and the final `* kRasterExposure` are **UNCHANGED** — this slice only restructures the ambient seed.

**Deterministic** (normal + light geometry + fixed `constexpr` constants; no camera, no random). **φ-skip preserved** (ambient lives in `shade_face`, which the `phi ? fc : shade_face(...)` fast path bypasses → wave mode unaffected). **No new params / no plumbing:** `shade_face` already receives `point`, `lights`, and `two_sided`; the sky constants are local to `build_sw` `detail` exactly like `kAmbient`/`kRasterExposure`/`kLightScale`.

## 2. Components (all in `build_sw` `detail`)

### 2.1 Constants (near `kAmbient`)
```cpp
// Hemispheric sky ambient. Channel-sum (≈ luminance proxy) zenith=1.47, horizon=1.19;
// the UNIFORM-hemisphere mean (a=0.5) is ~0.443, matching the prior flat kAmbient (0.45)
// in aggregate. The redistribution is the FEATURE: up-faces (floor, a=1) read ~9% brighter
// and cooler at the zenith, horizon-facing ~12% dimmer — not a global brightness jump, a
// directional one. sun_tint adds ~0.02–0.03 to the LIT-scene mean by design (warm fill =
// added light), so the 0.45 anchor is for the sky term alone.
inline constexpr aleph::math::Vec3 kSkyZenith  = {0.43f, 0.48f, 0.56f}; // up-facing (cool, bright)
inline constexpr aleph::math::Vec3 kSkyHorizon = {0.38f, 0.39f, 0.42f}; // side-facing (neutral, dimmer)
// Soft warm half-bounce fill from the dominant light (fake sun GI). Small — a fill, not a key.
inline constexpr aleph::math::Vec3 kSunColor    = {0.55f, 0.42f, 0.28f}; // warm tint (scaled by w·strength)
inline constexpr aleph::math::f32  kSunStrength = 0.12f;                 // fill weight (max lum add ≈ 0.05)
```
**`kAmbient` becomes dead code** after §2.4 + the §5 test re-expression remove its last reference. It is a namespace-scope `inline constexpr` (not function-local, not anonymous-ns), so leaving it does NOT trip `-Wunused` — deletion is for cleanliness, not to silence a warning. Delete it once `grep -n 'kAmbient' build_sw.cppm test_build_sw.cpp` shows zero *symbol* references (comments/bare-literal `0.45f` don't count).

### 2.2 `sky_ambient(N) -> Vec3`
```cpp
[[nodiscard]] inline aleph::math::Vec3 sky_ambient(aleph::math::Vec3 N) noexcept;
```
`N_up = (dot(N,{0,1,0}) < 0) ? N*-1 : N`; `const aleph::math::f32 a = std::clamp(N_up.y, 0.0f, 1.0f);` `return aleph::math::lerp(kSkyHorizon, kSkyZenith, a);`. **Use `std::clamp` with FLOAT literals** (`0.0f, 1.0f`): `aleph.math` has no `clamp`, and `std::clamp(x, 0, 1)` fails deduction (f32 vs int) and trips `-Wconversion` — match build_sw.cppm:121/350. `<algorithm>` is already included (build_sw.cppm:56). Reuses `aleph::math::lerp` (vec.cppm:79).

### 2.3 `sun_tint(point, N, lights, two_sided) -> Vec3`
```cpp
[[nodiscard]] inline aleph::math::Vec3
sun_tint(aleph::math::Vec3 point, aleph::math::Vec3 N,
         const std::vector<LoweredEntity>& lights, bool two_sided) noexcept;
```
1. **Primary light** = the light in `lights` with the greatest emissive luminance (`emit.x+emit.y+emit.z`). Pin the operator: `best = -inf; pick = npos; for i in order: if (s > best) { best = s; pick = i; }` — **STRICT `>`** so the lowest-index maximum wins on ties (a `>=` accumulator would pick the highest index, breaking the tie-break). If `lights` empty / `pick==npos` ⇒ return `{0,0,0}`.
2. **Guard before normalize:** `d = light_center(primary.world_geometry) - point; dist2 = dot(d,d); if (dist2 < 1e-6f) return {0,0,0};` (`aleph::math::normalize` has no zero-guard → NaN on a zero vector; mirror the direct-light loop's `dist_sq < 1e-6f` early-out, build_sw.cppm:441-442). Then `L = d * (1.0f / std::sqrt(dist2))`.
3. `nd = two_sided ? std::fabs(dot(N, L)) : dot(N, L);` `w = std::max(0.0f, 0.5f*nd + 0.5f);` `return kSunColor * (kSunStrength * w);` (`Vec3 * f32` scale — `operator*(f32)`, no hadamard). Reuses the existing `light_center` (build_sw.cppm) and `dot`/`normalize`.

### 2.4 `shade_face` ambient seed
Change the seed line from
```cpp
aleph::math::Vec3 lit = base_albedo * (kAmbient * ao) + self_emit;
```
to
```cpp
const aleph::math::Vec3 amb = sky_ambient(N) + sun_tint(point, N, lights, two_sided);
aleph::math::Vec3 lit = aleph::math::hadamard(base_albedo, amb) * ao + self_emit;
```
**CRITICAL — use `hadamard`, not `*`.** In `aleph.math`, `operator*(Vec3,Vec3)` is the **geometric product** (→ `Multivector`, in `:multivector`), NOT component-wise. Per-channel albedo×color MUST use `aleph::math::hadamard(a, b)` — the exact idiom the existing direct-light line uses (`lit + hadamard(base_albedo, L.material.emit) * (...)`, build_sw.cppm:450). `* ao` is then the f32 scale. `ao` (slice 4a), unit `N`, `point`, `lights`, and `two_sided` already exist in `shade_face`. Everything downstream (direct light × `vis`, atten, `* kRasterExposure`) unchanged.

## 3. Determinism
Fixed `constexpr` constants; `sky_ambient`/`sun_tint` are pure-f32 of `N`/light geometry; primary-light pick is strict-`>` max-luminance with lowest-index tie-break; `lights` iterated in order. Same `LoweredScene` ⇒ byte-identical `vcol`. `test_build_sw`'s `same_face` byte-equality oracle holds. `--wave`/`--headless` byte-identity unaffected (wave skips shade). NOTE: this changes the deterministic raster baseline vs slice 4a (the ambient model changed); no committed raster PPM gates today (only the before/after PNG artifact, not a test).

## 4. Error handling (`aleph_flags_isa`)
No allocation/exceptions. Degenerate `N` (`|N|≈0`) → `shade_face`'s existing `N=(0,1,0)` fallback → `sky_ambient` = zenith, `sun_tint` finite. Zero-length `L` (light at the point) → guarded by the `dist2 < 1e-6f` early-return in §2.3 step 2, **before** any `normalize` — no NaN. No-lights → `sun_tint = {0,0,0}`, ambient = sky only.

## 5. Testing (`tests/edit/test_build_sw.cpp`)
Oracles assert **relationships**, not magic magnitudes, so constant tuning doesn't break them. **Scene constraints below are load-bearing** (each fixes a way the naive oracle fails to discriminate — see the review):

- **Hemispheric (sky).** Scene: a **neutral-albedo** sphere (e.g. `{0.7,0.7,0.7}` — albedo.z>0 is REQUIRED; a red `{1,0,0}` sphere collapses the blue oracle to `0>0`), **NO lights** (isolates sky; `sun_tint=0`). Compare the **top** vertex (N≈+Y → zenith) vs an **equator** vertex (N≈horizontal → horizon). Assert both: `lum(top) > lum(side)` (albedo-agnostic, safe) AND blue-fraction `vcol(top).z/lum(top) > vcol(side).z/lum(side)` (needs albedo.z>0). Compare top vs **equator**, NOT top vs bottom (the world-up flip makes bottom==top).
- **Sun tint (warmth + direction).** Scene: a **sphere** entity (`emit_sphere` → `two_sided=false`, so a shadowed-hemisphere vertex gets `ndl0≤0 → ndl=0 →` direct light skipped, leaving ambient-only vcol; a quad/tri would be `two_sided=true → ndl=|ndl0|` and leak ~2.6× the ambient as neutral direct light, destroying the signal). One light off to **+X**. Pick two vertices **both on the equator ring** (`N.y≈0`, so `sky_ambient` is identical between them and the vertical gradient can't confound/invert the red-fraction): one azimuth **grazing toward** the light (`dot(N,L)` near `0⁻` → `w≈0.5`), one **fully away** (`dot(N,L)` near `−1` → `w=0`). Assert the toward vertex is **warmer**: `vcol.x/lum` (red fraction) higher. Add an assertion that both chosen vertices have `dot(N,L) ≤ 0` (truly shadowed → direct light skipped). Companion: with NO light, the two have equal warmth (asymmetry vanishes).
- **AO oracle still holds (regression canary, unchanged).** Tessellated floor + sphere, no lights → `lum(near) < lum(far)`. The two floor faces share the **same normal** ⇒ identical `sky_ambient` ⇒ only AO differs ⇒ near still darker. Should pass with NO edit — verify it does (guards the AO×ambient composition).
- **Update the existing baselines (mandatory — they hold stale `0.45`):**
  - *lone-sphere no-self-black* (sphere centred at ORIGIN so `n0 = normalize(v0)` exactly equals `shade_face`'s `N = normalize(v0 - centre)`; AO=1, lights empty → `sun_tint=0`): each face's ambient now depends on its own `N.y`, so the expected value is NOT a single scalar — compute `lum( hadamard(albedo, sky_ambient(n0)) * kRasterExposure )` at the SAME `n0` the checked face uses (the first emitted face is the +Y pole → zenith), via the `detail` helpers.
  - *sphere-lit* (relational): assert `top_lum >` the top vertex's **own** ambient seed computed WITH `* kRasterExposure` and the real lights (the old line-354 baseline omitted exposure — don't mix exposed vs un-exposed quantities). Use `hadamard(base_albedo, sky_ambient(N) + sun_tint(point, N, lights, two_sided)) * kRasterExposure`, NOT bare `*` (geometric product). Remove the last `detail::kAmbient` symbol reference.
- **Determinism:** `same_face`/`vcol` byte-equality across two builds still holds. (Optional: a two-equal-emit-light scene asserting the lower-index light is picked as primary.)
- **φ-skip:** the existing φ test (vcol == pure colormap) still passes (ambient bypassed in wave mode).
- **Visual:** before/after (flat-grey ambient vs sky+sun) of headless `step1_add_object` raster → `docs/superpowers/artifacts/2026-06-06-sky-ambient-before-after.png`. Expect cool sky tint on up-faces, a warm fill near the light-side terminator, grey flatness gone.

## 6. Cost / when it runs
Per vertex inside `shade_face`: one `sky_ambient` (a flip + `lerp`) + one `sun_tint` (a light scan for the max + one `normalize`/`dot`). `O(verts + verts·lights)`, dwarfed by AO's `O(verts × kAoRays × entities)` ray casts right beside it. `build_sw` runs on `apply(Op)`/`enable_sim`; NOT per-frame in non-wave live (cached `sw_` re-rasterized). In `--wave` the φ-override short-circuits before `shade_face`, so the sky ambient costs nothing there. No new allocation.

## 7. Scope boundary (YAGNI)
**In:** hemispheric sky ambient by world-up normal + soft directional sun tint (half-Lambert wrap, sign-invariant on two-sided geometry), fixed `constexpr` sky/sun constants, AO-occluded, calibrated to the prior ambient mean. **Out (hooks kept):**
- *Physical sphere undersides* — raw-`N` `sky_ambient` + reorienting the floor normal at the `emit_quad` site (needs the emit site to know a quad is the floor / its up-ness; not worth it for a preview while spheres rest on the floor).
- *Multi-light fill stability* — the discrete primary-light pick is deterministic per scene but can **flip across an edit** if two lights are near-equal luminance, popping the warm-fill direction (the editor is where scenes mutate). Acceptable: the fill is weak (≤0.05 lum). A luminance-weighted `L_sun` over all lights would remove the discontinuity later — noted, not built.
- *True sky IBL / cosine-weighted hemisphere irradiance* (the path tracer is the GI truth; this is a cheap analytic preview); *per-scene configurable sky in the bake* (uses fixed constants like every other shading constant; plumb a `Sky` into `LoweredScene` if the app's `kSky` should drive it later); *view-dependent ambient* (no camera in the bake, and ambient is view-independent).

All four constants (`kSkyZenith`, `kSkyHorizon`, `kSunColor`, `kSunStrength`) are `constexpr` tunables.

# Procedural Checker Texture (Raster↔PT Parity) — Implementation Plan (visual slice 4c-ii)

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use `- [ ]`.

**Goal:** a deterministic procedural checker on the **floor quad** that aligns between the software rasterizer and the path tracer. Wire `TexturedLambertian` graph→lowering→scene→PT→raster + a textured floor.

**Spec:** `docs/superpowers/specs/2026-06-06-procedural-texture-parity-design.md` (REVISED — read §1/§2/§3/§5). **Scope:** floor QUAD only (sphere/tri stay `tex_white` — §1/§7).

**Conventions:** `cmake --build build-release && ctest --test-dir build-release`; strict `cmake --build build-release-strict 2>&1 | grep "warning:" | grep -v '^ninja:' | wc -l` → 0. **GOTCHA:** the lowering IR structs (`MaterialParams` etc.) are DUAL-DEFINED in `:lower` + `:lowered` — edit BOTH copies token-identically or gcc-16 fails with "Bad file data".

---

## Task 1: TexturedLambertian through graph → lowering → scene → PT

**Files:** `graph/src/aleph.types/{aleph.types-attribute.cppm, aleph.types-node.cppm}`, `bridge/src/aleph.lowering/{aleph.lowering-lower.cppm, aleph.lowering-lowered.cppm, aleph.lowering-build.cppm}`, `apps/aleph_lower_demo/main.cpp`, `render/src/aleph.scene/{aleph.scene-material_soa.cppm, aleph.scene-scene.cppm}`, `render/src/aleph.render.rt/aleph.render.rt-material.cppm`, tests.

- [ ] **Step 1 — graph enum + Material field.** `aleph.types-attribute.cppm`: add `TexturedLambertian = 4,` to `enum class MaterialKind`. `aleph.types-node.cppm`: add `math::f32 uv_scale{4.0f};` as the trailing field of `struct Material`.

- [ ] **Step 2 — MaterialParams (BOTH copies) + to_params.** Add `math::f32 uv_scale{4.0f};` as the trailing field to `MaterialParams` in **`aleph.lowering-lower.cppm`:~60 AND `aleph.lowering-lowered.cppm`:~50** (token-identical). In `aleph.lowering-lower.cppm` `to_params` (~166), append `, m.uv_scale` (6th positional arg): `return MaterialParams{m.kind, m.albedo, m.fuzz, m.ior, m.emit, m.uv_scale};`. (The positional init at ~314 stays valid — uv_scale defaults.)

- [ ] **Step 3 — scene SoA + scene_add (BEFORE the lowering arm).** `aleph.scene-material_soa.cppm`: add `std::vector<aleph::math::Vec3> albedo;` to `TexturedLambertianSoA`; change `textured_lambertian_append` to `(TexturedLambertianSoA& s, aleph::math::Vec3 albedo, std::uint32_t tex_id, aleph::math::Vec2 uv_scale)` pushing `albedo` too. `aleph.scene-scene.cppm`: change `scene_add_textured_lambertian` to `(Scene& s, aleph::math::Vec3 albedo, aleph::math::Vec2 uv_scale)` → `textured_lambertian_append(s.tex_lamb, albedo, 0u, uv_scale)` (dummy tex_id 0).

- [ ] **Step 4 — PT sampler.** `aleph.render.rt-material.cppm`: add `inline constexpr aleph::math::f32 kCheckerHi = 1.0f; inline constexpr aleph::math::f32 kCheckerLo = 128.0f / 255.0f;` near the top. Replace `sample_textured_albedo`'s grey-stub body with:
```cpp
const aleph::math::Vec2 sc = s.tex_lamb.uv_scale[mat_idx];
const aleph::math::Vec3 a  = s.tex_lamb.albedo[mat_idx];
const int cu = static_cast<int>(std::floor(u * sc.x));
const int cv = static_cast<int>(std::floor(v * sc.y));
return a * (((cu ^ cv) & 1) ? kCheckerHi : kCheckerLo);
```
(Drop the `(void)` suppressions. `<cmath>` for `std::floor` — confirm it's included.)

- [ ] **Step 5 — the two exhaustive switches.** `aleph.lowering-build.cppm` `add_material` (~80): add `case aleph::types::MaterialKind::TexturedLambertian: return aleph::scene::scene_add_textured_lambertian(s, m.albedo, {m.uv_scale, m.uv_scale});`. `apps/aleph_lower_demo/main.cpp` (~318): add the same `case` (or a `default:`) to its `add_material` copy.

- [ ] **Step 6 — tests.** (a) `tests/graph/test_attribute.cpp`: `CHECK(static_cast<int>(aleph::types::MaterialKind::TexturedLambertian) == 4);`. (b) MIGRATE `tests/scene/test_material_soa.cpp` TexturedLambertian case to the new `textured_lambertian_append(t, Vec3{...}, 7u, Vec2{2,1})` + assert `albedo[0]` alongside `tex_id[0]`/`uv_scale[0]`. (c) `tests/render/test_material_scatter.cpp`: add a `sample_textured_albedo` test (build a `scene_add_textured_lambertian(albedo, {sx,sy})`; assert HI-cell `== albedo*kCheckerHi`, LO-cell `== albedo*kCheckerLo`, adjacent tiles differ) AND a `scatter()` TexturedLambertian-arm test (`scatter()->attenuation == sample_textured_albedo(...)`, mirroring the Lambertian scatter test).

- [ ] **Step 7 — build + run + strict.** `ctest` all pass (graph/scene/rt suites). Strict 0 (BOTH new switch arms silence `-Wswitch`; no IR-struct divergence). **Commit** `feat(tex): TexturedLambertian graph->lowering->scene->PT (procedural checker)`.

---

## Task 2: Raster — real UVs + checker on the floor quad

**Files:** `render/src/aleph.render.sw/aleph.render.sw-scene_rt.cppm`, `bridge/src/aleph.lowering/aleph.lowering-build_sw.cppm`, `tests/edit/test_build_sw.cpp`.

- [ ] **Step 1 — `tex_checker_uv`.** In `render.sw-scene_rt.cppm` beside `tex_checker`:
```cpp
inline aleph::math::u32 tex_checker_uv(aleph::math::f32 u, aleph::math::f32 v) noexcept {
    const int cu = static_cast<int>(std::floor(u));
    const int cv = static_cast<int>(std::floor(v));
    return ((cu ^ cv) & 1) ? 0xFFFFFFFFu : 0xFF808080u;   // HI=1.0, LO=128/255 via argb_to_linear (/255)
}
```

- [ ] **Step 2 — `push_tri` gains UVs + a tex fn.** Change to `push_tri(out, a,b,c, ca,cb,cc, Vec2 uva, Vec2 uvb, Vec2 uvc, TexSampleFn fn, source)`; set `f.uvs = {uva, uvb, uvc, uvc}` and `f.tex = fn` (remove the hardcoded zeroed uvs + `&tex_white`).

- [ ] **Step 3 — `emit_quad` UVs + checker + φ guard.** In `emit_quad`, add a UV lambda mirroring `P(i,j)`: `auto UV = [&](int i, int j){ return Vec2{ (f32(i)/f32(Nu)) * mat.uv_scale, (f32(j)/f32(Nv)) * mat.uv_scale }; };`. Select the fn ONCE: `const TexSampleFn fn = (mat.kind == aleph::types::MaterialKind::TexturedLambertian && phi == nullptr) ? &tex_checker_uv : &tex_white;`. The two `push_tri` calls pass the 3 corner UVs each: `push_tri(out, c00,c10,c11, s00,s10,s11, UV(i,j),UV(i+1,j),UV(i+1,j+1), fn, source)` and `push_tri(out, c00,c11,c01, s00,s11,s01, UV(i,j),UV(i+1,j+1),UV(i,j+1), fn, source)`.

- [ ] **Step 4 — `emit_sphere`/`emit_tri` pass-through.** Update their `push_tri` calls to pass `Vec2{0,0}` for every uv + `&tex_white` (spheres/tris untextured this slice).

- [ ] **Step 5 — raster tests.** In `test_build_sw.cpp`: (a) a `TexturedLambertian` floor quad → some face has a non-zero `f.uvs` AND `f.tex == &tex_checker_uv`; a Lambertian quad → `f.tex == &tex_white`. (b) `tex_checker_uv(0.5,0.5) != tex_checker_uv(1.5,0.5)` and `== tex_checker_uv(1.5,1.5)`; pin levels `0xFFFFFFFF`/`0xFF808080`. (c) the 4c-i Lambert golden + `same_face` still pass (Lambertian byte-identical). (d) φ guard: a textured quad built with a φ value → `f.tex == &tex_white` and the existing φ vcol test passes.

- [ ] **Step 6 — parity oracle.** In `test_build_sw.cpp` (or a scene/edit test with PT access): for a unit floor quad, at a tessellation vertex assert the PT `hit_quad`'s `(α,β) == ` the raster baked `(i/Nu, j/Nv)` (`Vec2 operator==`, bit-exact) AND `cell(α·sc) == cell(baked·sc)`. (Use the existing PT hit harness; do NOT sample mid-cell points.) If a direct `hit_quad` call is awkward from the edit test, place this in the scene/rt test area where `hit_quad` is reachable.

- [ ] **Step 7 — build + run + strict.** `--test-case="build_sw*"` all pass; full `ctest`; strict 0. **Commit** `feat(tex): raster checker on the floor quad (real UVs + tex_checker_uv + φ guard)`.

---

## Task 3: Textured floor subject + parity artifacts

**Files:** `apps/aleph_edit/main.cpp`, artifacts.

- [ ] **Step 1 — textured floor.** In `build_initial_graph`, change the floor Material to `MaterialKind::TexturedLambertian` (a neutral tan/grey albedo, `uv_scale` ~4–8 so the 8×8 floor reads as a clear board). Keep the matte/metal/glass spheres.

- [ ] **Step 2 — build + smoke.** `--headless` renders without crash; full `ctest` (no controller/headless test pins the floor material — update if one does). Strict 0.

- [ ] **Step 3 — artifacts.** (a) before/after of the editor floor flat (`main`) vs checkered (this branch) raster → `docs/superpowers/artifacts/2026-06-06-texture-checker-before-after.png` (temp `git worktree` for `main`). (b) a raster-vs-PT contact sheet of the textured floor via `tools/visual_review.sh` → confirm the checker tiles ALIGN between backends (the parity payoff). **Commit** `feat(tex): textured floor subject + raster<->PT parity artifacts`.

---

## Final verification
- [ ] Graph enum=4, scene SoA albedo, PT analytic checker, both switches handled; `test_material_soa`/`test_material_scatter`/`test_attribute` pass.
- [ ] Raster: textured floor → checker `f.tex` + real UVs; Lambert golden + `same_face` + φ unchanged; parity oracle (vertex `(α,β)==(i/Nu,j/Nv)`) passes.
- [ ] `ctest` all pass; release-strict 0. Headless renders the checkered floor.
- [ ] Artifacts: raster before/after + raster↔PT tiles align.

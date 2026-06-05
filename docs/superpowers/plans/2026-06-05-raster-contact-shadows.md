# Raster Contact Shadows + Quad Tessellation — Plan (slice 2)

> TDD, frequent commits. **Spec:** `docs/superpowers/specs/2026-06-05-raster-contact-shadows-design.md`. All work is in `bridge/src/aleph.lowering/aleph.lowering-build_sw.cppm` + `tests/edit/test_build_sw.cpp`. `render.sw` is NOT touched.

**Conventions:** `cmake --build build-release && ctest --test-dir build-release`; one case `./build-release/tests/aleph_tests --test-case="<name>"`; strict `cmake --build build-release-strict 2>&1 | grep -c "warning:"` → 0. No exceptions/RTTI.

**Key existing shapes in build_sw.cppm (read first):** `shade_face(Vec3 point, Vec3 normal, Vec3 albedo, Vec3 emit, const std::vector<LoweredEntity>& lights, bool two_sided)`; `light_center(const GeometryPayload&)`; `emit_quad/tri/sphere(out, geom, albedo, emit, lights, source, phi)` (each calls `shade_face` per vertex as `phi ? fc : shade_face(...)`); `emit_entity(out, e, lights, phi)`; `build_sw_scene(ls, phi_entity=nullptr)`. `LoweredEntity{ NodeId source; types::GeometryPayload world_geometry; MaterialParams material; }`. `GeometryPayload = std::variant<SphereLocal{Vec3 center; f32 radius}, QuadLocal{Vec3 q,u,v}, TriLocal{Vec3 a,b,c}>`. Constants live near `kAmbient`/`kLightScale`.

---

## Task 1: Tessellate quads + update the face-count oracle

**Files:** `bridge/src/aleph.lowering/aleph.lowering-build_sw.cppm`, `tests/edit/test_build_sw.cpp`.

- [ ] **Step 1 — constants** near `kAmbient`:
```cpp
inline constexpr aleph::math::f32 kCell     = 0.5f;  // target quad cell size (world units)
inline constexpr int              kMaxCells = 24;    // per-axis tessellation cap
```
- [ ] **Step 2 — rewrite `emit_quad`** to tessellate. Keep the `phi ? fc : shade_face(...)` pattern and the `{c00,c10,c11}`/`{c00,c11,c01}` winding:
```cpp
inline void emit_quad(SwBuild& out, const aleph::types::QuadLocal& g,
                      aleph::math::Vec3 albedo, aleph::math::Vec3 emit,
                      const std::vector<LoweredEntity>& lights,
                      aleph::types::NodeId source, const double* phi) {
    using aleph::math::Vec3; using aleph::math::f32;
    const Vec3 fc = phi ? detail::colormap_diverging(*phi, kPhiScale) : Vec3{};
    const Vec3 n  = aleph::math::cross(g.u, g.v);
    auto clampc = [](int v){ return v < 1 ? 1 : (v > kMaxCells ? kMaxCells : v); };
    const int Nu = clampc(static_cast<int>(std::ceil(aleph::math::length(g.u) / kCell)));
    const int Nv = clampc(static_cast<int>(std::ceil(aleph::math::length(g.v) / kCell)));
    auto P = [&](int i, int j) {
        return g.q + g.u * (static_cast<f32>(i) / static_cast<f32>(Nu))
                   + g.v * (static_cast<f32>(j) / static_cast<f32>(Nv));
    };
    for (int j = 0; j < Nv; ++j) {
        for (int i = 0; i < Nu; ++i) {
            const Vec3 c00 = P(i,j),   c10 = P(i+1,j);
            const Vec3 c11 = P(i+1,j+1), c01 = P(i,j+1);
            const Vec3 s00 = phi ? fc : shade_face(c00, n, albedo, emit, lights, true);
            const Vec3 s10 = phi ? fc : shade_face(c10, n, albedo, emit, lights, true);
            const Vec3 s11 = phi ? fc : shade_face(c11, n, albedo, emit, lights, true);
            const Vec3 s01 = phi ? fc : shade_face(c01, n, albedo, emit, lights, true);
            push_tri(out, c00, c10, c11, s00, s10, s11, source);
            push_tri(out, c00, c11, c01, s00, s11, s01, source);
        }
    }
}
```
(Ensure `<cmath>` is in the module's global fragment for `std::ceil`.) Task 2 adds the `occluders`/`self` params; for now this compiles against the existing `shade_face`.
- [ ] **Step 3 — update the oracle** in `tests/edit/test_build_sw.cpp`. The quad mesh there is `QuadLocal{Vec3{-1,-1,-2}, Vec3{2,0,0}, Vec3{0,2,0}}` (|u|=|v|=2 → Nu=Nv=`clamp(ceil(2/0.5))`=4 → `2·4·4 = 32` faces). Replace `const std::size_t quad_faces = 2u;` with the computed value mirroring the spec formula:
```cpp
const int qNu = 4, qNv = 4;   // ceil(2/0.5)=4, within kMaxCells; matches emit_quad
const std::size_t quad_faces = static_cast<std::size_t>(2 * qNu * qNv);  // = 32
```
(If your `kCell`/`kMaxCells` differ, recompute. Keep `faces_from(sw, s.quad) == quad_faces` and the `sphere_faces + quad_faces` total assertion — they now use the new `quad_faces`.)
- [ ] **Step 4 — build + run** `./build-release/tests/aleph_tests --test-case="build_sw*"` → pass (the determinism `same_face` case still passes — tessellation is deterministic). Full `ctest`; strict 0.
- [ ] **Step 5 — visual sanity (optional):** `build-release/apps/aleph_edit/aleph_edit --headless /tmp/t` then view `step0_init_raster` — the floor should now show a subtle lighting gradient (no shadows yet). **Commit** `feat(sw): tessellate QuadLocal into an NxN grid (floor gradient + shadow substrate)`.

---

## Task 2: Per-vertex contact shadows

**Files:** `bridge/src/aleph.lowering/aleph.lowering-build_sw.cppm`, `tests/edit/test_build_sw.cpp`.

- [ ] **Step 1 — shadow constants** near the others:
```cpp
inline constexpr aleph::math::f32 kShadowEps    = 1.0e-3f;
inline constexpr aleph::math::f32 kShadowBias   = 2.0e-3f;
inline constexpr int              kShadowSamples = 2;   // 2x2 on area lights
```
- [ ] **Step 2 — occlusion primitives** (in `namespace detail`, before `shade_face`). `dir` is unit; hit window `(kShadowEps, seg_len − kShadowEps)`:
```cpp
[[nodiscard]] inline bool seg_hits_sphere(aleph::math::Vec3 p, aleph::math::Vec3 dir,
        aleph::math::f32 len, const aleph::types::SphereLocal& s) noexcept {
    const aleph::math::Vec3 oc = p - s.center;
    const aleph::math::f32 b = aleph::math::dot(oc, dir);
    const aleph::math::f32 c = aleph::math::dot(oc, oc) - s.radius * s.radius;
    const aleph::math::f32 disc = b*b - c;
    if (disc < 0.0f) return false;
    const aleph::math::f32 sq = std::sqrt(disc);
    const aleph::math::f32 t1 = -b - sq, t2 = -b + sq;
    return (t1 > kShadowEps && t1 < len - kShadowEps)
        || (t2 > kShadowEps && t2 < len - kShadowEps);
}
[[nodiscard]] inline bool seg_hits_quad(aleph::math::Vec3 p, aleph::math::Vec3 dir,
        aleph::math::f32 len, const aleph::types::QuadLocal& g) noexcept {
    const aleph::math::Vec3 n = aleph::math::cross(g.u, g.v);
    const aleph::math::f32 dn = aleph::math::dot(dir, n);
    if (std::fabs(dn) < 1.0e-8f) return false;
    const aleph::math::f32 t = aleph::math::dot(g.q - p, n) / dn;
    if (!(t > kShadowEps && t < len - kShadowEps)) return false;
    const aleph::math::Vec3 w = (p + dir * t) - g.q;
    const aleph::math::f32 uu = aleph::math::dot(g.u,g.u), vv = aleph::math::dot(g.v,g.v),
                           uv = aleph::math::dot(g.u,g.v),
                           wu = aleph::math::dot(w,g.u),   wv = aleph::math::dot(w,g.v);
    const aleph::math::f32 den = uu*vv - uv*uv;
    if (std::fabs(den) < 1.0e-12f) return false;
    const aleph::math::f32 s = (wu*vv - wv*uv) / den;
    const aleph::math::f32 r = (wv*uu - wu*uv) / den;
    return s >= 0.0f && s <= 1.0f && r >= 0.0f && r <= 1.0f;
}
[[nodiscard]] inline bool seg_hits_tri(aleph::math::Vec3 p, aleph::math::Vec3 dir,
        aleph::math::f32 len, const aleph::types::TriLocal& g) noexcept {
    const aleph::math::Vec3 e1 = g.b - g.a, e2 = g.c - g.a;
    const aleph::math::Vec3 pv = aleph::math::cross(dir, e2);
    const aleph::math::f32 det = aleph::math::dot(e1, pv);
    if (std::fabs(det) < 1.0e-8f) return false;
    const aleph::math::f32 inv = 1.0f / det;
    const aleph::math::Vec3 tv = p - g.a;
    const aleph::math::f32 u = aleph::math::dot(tv, pv) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    const aleph::math::Vec3 qv = aleph::math::cross(tv, e1);
    const aleph::math::f32 w = aleph::math::dot(dir, qv) * inv;
    if (w < 0.0f || u + w > 1.0f) return false;
    const aleph::math::f32 t = aleph::math::dot(e2, qv) * inv;
    return t > kShadowEps && t < len - kShadowEps;
}
```
- [ ] **Step 3 — `light_visibility`** (in `detail`, before `shade_face`):
```cpp
[[nodiscard]] inline aleph::math::f32
light_visibility(aleph::math::Vec3 point, aleph::math::Vec3 n_unit,
                 const LoweredEntity& light,
                 const std::vector<LoweredEntity>& occluders,
                 aleph::types::NodeId self) noexcept {
    const aleph::math::Vec3 p0 = point + n_unit * kShadowBias;
    // gather light samples
    std::array<aleph::math::Vec3, kShadowSamples * kShadowSamples> samp{};
    int ns = 0;
    if (const auto* q = std::get_if<aleph::types::QuadLocal>(&light.world_geometry)) {
        for (int j = 0; j < kShadowSamples; ++j)
            for (int i = 0; i < kShadowSamples; ++i) {
                const aleph::math::f32 su = (static_cast<aleph::math::f32>(i) + 0.5f)
                                            / static_cast<aleph::math::f32>(kShadowSamples);
                const aleph::math::f32 sv = (static_cast<aleph::math::f32>(j) + 0.5f)
                                            / static_cast<aleph::math::f32>(kShadowSamples);
                samp[static_cast<std::size_t>(ns++)] = q->q + q->u * su + q->v * sv;
            }
    } else {
        samp[static_cast<std::size_t>(ns++)] = light_center(light.world_geometry);
    }
    int vis = 0;
    for (int k = 0; k < ns; ++k) {
        const aleph::math::Vec3 d = samp[static_cast<std::size_t>(k)] - p0;
        const aleph::math::f32 len = aleph::math::length(d);
        if (len < 2.0f * kShadowEps) { ++vis; continue; }
        const aleph::math::Vec3 dir = d * (1.0f / len);
        bool occ = false;
        for (const LoweredEntity& o : occluders) {
            if (o.source == self || o.source == light.source) continue;
            if (std::visit([&](const auto& gg){
                    using G = std::decay_t<decltype(gg)>;
                    if constexpr (std::is_same_v<G, aleph::types::SphereLocal>) return seg_hits_sphere(p0, dir, len, gg);
                    else if constexpr (std::is_same_v<G, aleph::types::QuadLocal>) return seg_hits_quad(p0, dir, len, gg);
                    else return seg_hits_tri(p0, dir, len, gg);
                }, o.world_geometry)) { occ = true; break; }
        }
        if (!occ) ++vis;
    }
    return static_cast<aleph::math::f32>(vis) / static_cast<aleph::math::f32>(ns);
}
```
(Add `<array>`, `<variant>`, `<type_traits>` to the global fragment if not present — `<variant>`/`<type_traits>` already are, used by `emit_entity`.)
- [ ] **Step 4 — `shade_face` gains `(occluders, self)`** and multiplies the diffuse term by visibility. Locate the light loop; where it currently adds the diffuse contribution, capture it into a local and scale:
```cpp
// signature:
shade_face(Vec3 point, Vec3 normal, Vec3 base_albedo, Vec3 self_emit,
           const std::vector<LoweredEntity>& lights, bool two_sided,
           const std::vector<LoweredEntity>& occluders, NodeId self)
// inside the per-light loop, after computing the light's diffuse Vec3 `contrib`
// and BEFORE adding it to the accumulator, scale by visibility using the unit normal:
const f32 V = light_visibility(point, N_unit, L, occluders, self);
acc = acc + contrib * V;   // (contrib was the albedo⊙emit·ndl·atten·kLightScale term)
```
Use the already-normalized normal the function computes (the existing `shade_face` normalizes `normal` into a unit vector — reuse that as `N_unit`). Ambient + self-emit unchanged.
- [ ] **Step 5 — thread `(occluders, self)`** through `emit_quad/tri/sphere` (add `const std::vector<LoweredEntity>& occluders` param; pass `occluders, source` into every `shade_face` call), `emit_entity` (add `occluders`, forward), and `build_sw_scene` (pass `ls.entities` as `occluders` to `emit_entity`). The `phi ? fc : shade_face(...)` short-circuit stays (shadows skipped in wave mode).
- [ ] **Step 6 — tests** in `tests/edit/test_build_sw.cpp`:
```cpp
TEST_CASE("build_sw: contact shadow darkens the floor under a sphere") {
    // root + camera + overhead area-light quad at y=3 + sphere at origin + floor at y=0.
    // (mirror make_two_prims's construction; add a Light Area quad + a floor quad + a sphere.)
    // Lower -> build_sw_scene. Find a floor face whose 4 verts' centroid.x,z ≈ 0 (under the
    // sphere) and one with centroid far (|x|>2). Assert luminance(under) < luminance(far).
    // luminance = vcol[0].x+vcol[0].y+vcol[0].z (any vert; flat-ish per face is fine).
}
TEST_CASE("build_sw: phi override still bypasses shadows/shade") {
    // build_sw_scene(*lowered, &phi) with phi={...}: a face's vcol == colormap_diverging(phi,kPhiScale)
    // exactly (no shadow/Lambert mixed) -> proves the phi path skips shade_face/light_visibility.
}
```
Write them concretely against the real `aleph.types`/`aleph.lowering` API (mirror `make_two_prims`/the existing `build_sw phi` test). Keep the determinism `same_face` test passing.
- [ ] **Step 7 — build + run** the new cases + full `ctest` (20/20+) + strict 0. **Determinism:** `--wave` frames still byte-identical across two runs (wave skips shade, so unaffected — verify with `cmp`).
- [ ] **Step 8 — before/after artifact:** headless `step1_add_object_raster` on this branch (shadows) vs `main` (no shadows); montage to `docs/superpowers/artifacts/2026-06-05-contact-shadows-before-after.png`. **Commit** `feat(sw): per-vertex contact shadows (analytic light occlusion) baked into vcol`.

---

## Final verification
- [ ] `ctest --test-dir build-release` all pass (updated quad oracle, shadow oracle, φ-skip, determinism). release-strict 0 warnings.
- [ ] `--wave` byte-identical across runs (shadows don't run in wave mode).
- [ ] Visual: the floor shows a soft shadow under the sphere + a lighting gradient (before/after artifact).

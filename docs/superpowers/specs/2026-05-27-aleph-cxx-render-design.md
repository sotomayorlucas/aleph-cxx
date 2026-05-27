---
name: aleph-cxx-render-design
date: 2026-05-27
status: approved (sections 1-4 confirmed in brainstorming)
phase: 2 of 5
builds-on: docs/superpowers/specs/2026-05-26-aleph-cxx-foundation-design.md
---

# aleph-cxx — Scene & Render (Phase 2)

## Context

Phase 1 (foundation) is complete and merged: 6 C++26 modules (`aleph.cpu`,
`aleph.math`, `aleph.alloc`, `aleph.containers`, `aleph.threads`, `aleph.io`)
totalling ~2 900 LOC at `v0.1.1-foundation`. All 6 ctest cases pass.

Phase 2 absorbs the renderers from **Sotark cxx26** (`/home/lkz/Sotark/`,
branch `cxx26`, ~3 000 LOC) onto the foundation, replacing `std::variant`-
based hittables and materials with **SoA storage per kind** + a **unified
BVH** that uses packed `Handle32{kind:8, idx:24}` in its leaves. The CPU
raytracer + sw_renderer + SDL2 editor are all targeted in this phase.

**Scope:** both `aleph_rt` (CLI path tracer) and `aleph_sw` (interactive
SDL editor) ship together. Editor parity with Sotark cxx26: orbit camera,
ray-pick, primitive add/remove, light sliders, immediate-mode UI panels.

## Constraints (decided in brainstorming)

| Axis              | Decision                                                              |
| ---               | ---                                                                   |
| Repo / branch     | `/home/lkz/aleph-cxx/`, work on `phase-2-render` branch from `main`   |
| Toolchain         | GCC 16.1.1, `-std=c++26`, C++20+ modules                              |
| Build             | CMake 4.3 + Ninja, presets unchanged from Phase 1                     |
| Test              | doctest (already vendored)                                            |
| Storage           | SoA per kind + typed `Handle32{kind:8, idx:24}`                       |
| BVH               | Single unified, leaves carry `Handle32` (any kind)                    |
| `sw_renderer` UI  | SDL2 window + interactive editor (parity Sotark cxx26)                |
| Determinism       | No hash maps in hot paths; `DenseIndex` and `FlatSet` from Phase 1    |
| Exception policy  | Module libraries link `aleph_flags_isa` (same workaround as Phase 1)  |

## Decomposition (5 phases — recap)

| Phase | Topic                | LOC target | Status                  |
| ---   | ---                  | ---        | ---                     |
| 1     | foundation           | ~3 K       | **DONE** (`v0.1.1-foundation`) |
| **2** | **scene & render**   | **~5 K**   | **THIS spec**           |
| 3     | DPO + topology       | ~4 K       | pending                 |
| 4     | DEC + flow           | ~3 K       | pending                 |
| 5     | GPU offload          | ~2 K       | pending                 |

## Architecture

### Repo layout (extends Phase 1)

```
/home/lkz/aleph-cxx/
  CMakeLists.txt              # extended: add_subdirectory(render), apps
  foundation/                  # Phase 1 (immutable; consumed)
  render/                      # THIS phase
    CMakeLists.txt
    src/
      aleph.scene/             # SoA stores + unified BVH + Scene struct
      aleph.render.common/     # Camera + Film + tonemap + sky
      aleph.render.rt/         # path tracer + NEE + integrator + sampling
      aleph.render.sw/         # rasterizer + span buffer + lightmap + clip
      aleph.window/            # SDL2 wrapper — only file linking SDL2
      aleph.editor/            # immediate-mode UI + picking + orbit cam helpers
  apps/                        # new top-level dir for executables
    CMakeLists.txt
    aleph_rt/main.cpp          # CLI raytracer → PPM
    aleph_sw/main.cpp          # interactive SDL editor
  tests/
    scene/                     # Handle32, SoA, BVH, hit()
    render/                    # rt + sw component tests
    window/                    # SDL init smoke
    editor/                    # picking + UI logic
  bench/
    bench_main.cpp             # extended with render baselines
  docs/superpowers/specs/      # this spec + later phases
```

### Module dependency graph

```
        ┌─────────────── Foundation (Phase 1) ───────────────┐
        │ aleph.cpu → {math, alloc, io} → containers → threads │
        └──────────────────────────┬───────────────────────────┘
                                   │
                      ┌────────────┼────────────┐
                      ↓            ↓            ↓
                aleph.scene  aleph.render.common
                      │            │
                      ├────────────┼───────────┐
                      ↓            ↓           ↓
              aleph.render.rt  aleph.render.sw  aleph.window
                                   │                │
                                   │                ↓
                                   └──── aleph.editor (sw + window)
```

**Hard rules:**

- Only `aleph.window` and `aleph.editor` link SDL2. `aleph.render.sw` is
  headless — produces a `Film` (mdspan over rgb).
- `aleph.scene` holds data + algorithms (BVH build), is renderer-agnostic.
- `aleph.render.rt` and `aleph.render.sw` are **siblings**. Shared pieces
  (`Camera`, `Film`, tonemap, sky) live in `aleph.render.common`.
- `aleph.editor` orchestrates sw + window. Picking and orbit-camera math
  live here, not in sw.

### New executables

| Binary | Imports | SDL2? | Function |
| ---    | ---     | ---   | ---      |
| `aleph_rt` | scene + render.{rt,common} | no | path-trace scene → PPM |
| `aleph_sw` | scene + render.{sw,common} + window + editor | yes | interactive editor |

If SDL2 is missing at configure time, `aleph.window`, `aleph.editor`,
and `aleph_sw` are **skipped**. `aleph_rt` always builds.

### Naming

- Namespaces: `aleph::scene`, `aleph::render::common`, `aleph::render::rt`,
  `aleph::render::sw`, `aleph::window`, `aleph::editor`
- Types: `PascalCase`; functions: `snake_case`; concepts: `PascalCase`
- File-naming follows Phase 1: `aleph.<module>-<partition>.cppm` declares
  `export module aleph.<module>:<partition>;`

## Module: `aleph.scene`

### `Handle32`

```cpp
enum class HittableKind : u8 { Sphere = 0, Quad = 1, Tri = 2, BvhNode = 3 };
enum class MaterialKind : u8 {
    Lambertian = 0, Metal = 1, Dielectric = 2, Emissive = 3, TexturedLambertian = 4,
};

struct Handle32 {                                   // 4 bytes, packed
    u32 packed;
    constexpr HittableKind hittable_kind() const noexcept { return HittableKind(packed >> 24); }
    constexpr u32          index()          const noexcept { return packed & 0x00FFFFFFu; }
    static constexpr Handle32 make(HittableKind k, u32 idx) noexcept {
        return Handle32{ (u32(k) << 24) | (idx & 0x00FFFFFFu) };
    }
};

struct MaterialHandle { MaterialKind kind; u32 idx; };  // 8 bytes — cold path
```

Max 16 M entities per kind. Matches `aleph::containers::DenseIndex<Tag, T>`
ergonomics — Handle32 is just a packed version of that pattern for the BVH
hot path.

### SoA stores

Per-kind, all fields split per-axis where SIMD batching is anticipated.

```cpp
struct SphereSoA {
    std::vector<f32>            cx, cy, cz;     // center
    std::vector<f32>            r;
    std::vector<MaterialHandle> mat;
    std::vector<Aabb>           bbox;
};

struct QuadSoA {
    std::vector<f32> Qx, Qy, Qz;                // origin
    std::vector<f32> ux, uy, uz;                // edge u
    std::vector<f32> vx, vy, vz;                // edge v
    std::vector<f32> nx, ny, nz;                // unit normal
    std::vector<f32> D;                         // n·p = D
    std::vector<Vec3> w;                        // (u×v)/|u×v|² — cold path
    std::vector<MaterialHandle> mat;
    std::vector<Aabb>           bbox;
};

struct TriSoA {
    std::vector<f32> v0x, v0y, v0z;
    std::vector<f32> v1x, v1y, v1z;
    std::vector<f32> v2x, v2y, v2z;
    std::vector<MaterialHandle> mat;
    std::vector<Aabb>           bbox;
};
```

Material SoAs:

```cpp
struct LambertianSoA          { std::vector<Vec3> albedo; };
struct MetalSoA               { std::vector<Vec3> albedo; std::vector<f32> fuzz; };
struct DielectricSoA          { std::vector<f32>  ior; };
struct EmissiveSoA            { std::vector<Vec3> emit; };
struct TexturedLambertianSoA  { std::vector<u32>  tex_id; std::vector<Vec2> uv_scale; };
```

If `std::vector` hits the GCC 16 placement-new bug observed in Phase 1,
fall back to the manual `::operator new` + placement-new + destructor +
explicit Rule-of-5 pattern used by `WorkStealingDeque`, `Pool`, and
`DenseIndex` in Phase 1.

### `Scene` and builders

```cpp
struct Scene {
    SphereSoA spheres;
    QuadSoA   quads;
    TriSoA    tris;
    LambertianSoA           lamb;
    MetalSoA                metal;
    DielectricSoA           diel;
    EmissiveSoA             emis;
    TexturedLambertianSoA   tex_lamb;
    std::vector<Image>      textures;       // referenced by tex_lamb.tex_id
    std::vector<Handle32>   lights;         // emissive quads, for NEE
    BvhNodeArr              bvh;            // built once
};

Handle32 scene_add_sphere(Scene&, Vec3 center, f32 r, MaterialHandle);
Handle32 scene_add_quad  (Scene&, Vec3 Q, Vec3 u, Vec3 v, MaterialHandle);
Handle32 scene_add_tri   (Scene&, Vec3 v0, Vec3 v1, Vec3 v2, MaterialHandle);

MaterialHandle scene_add_lambertian(Scene&, Vec3 albedo);
MaterialHandle scene_add_metal     (Scene&, Vec3 albedo, f32 fuzz);
MaterialHandle scene_add_dielectric(Scene&, f32 ior);
MaterialHandle scene_add_emissive  (Scene&, Vec3 emit);
MaterialHandle scene_add_textured_lambertian(Scene&, u32 tex_id, Vec2 uv_scale);

void scene_build_bvh(Scene&, alloc::Arena& scratch);
```

### `Hit` and `hit()` dispatch

```cpp
struct HitRecord {
    Vec3 p;
    Vec3 normal;            // oriented against incident ray
    f32  t;
    f32  u, v;              // texture coords
    MaterialHandle mat;
    bool front_face;
};

[[nodiscard]] std::optional<HitRecord>
hit(const Scene&, Ray, f32 t_min, f32 t_max) noexcept;
```

Internal: stack-based BVH traversal (no recursion). On leaf:

```cpp
switch (h.hittable_kind()) {
    case HittableKind::Sphere: return hit_sphere(s.spheres, h.index(), r, t_min, t_max);
    case HittableKind::Quad:   return hit_quad  (s.quads,   h.index(), r, t_min, t_max);
    case HittableKind::Tri:    return hit_tri   (s.tris,    h.index(), r, t_min, t_max);
    case HittableKind::BvhNode: /* unreachable — leaves contain primitives */
}
```

The switch over 3 reachable kinds compiles to a small jump table.
**One branch per visited BVH leaf** — the only kind-dispatch on the hot path.

### Unified BVH

```cpp
struct BvhNode {
    Aabb bbox;
    Handle32 left;     // BvhNode kind → internal, else → leaf primitive
    Handle32 right;
};

struct BvhNodeArr {
    std::vector<BvhNode> nodes;   // last entry is the root
};
```

Build: median-split on centroid in the longest axis of the union bbox.
Single-threaded build initially; parallel build via
`aleph::threads::WorkStealingDeque` is deferred to a future phase.

Traversal: iterative stack-based (no recursion). AVX2 batched
ray-vs-4-AABB is deferred to the first benchmark that demands it
(V1 is scalar ray-vs-AABB which we already validated in Phase 1).

## Module: `aleph.render.common`

```cpp
struct Camera {
    Vec3 center{}, pixel00_loc{}, pixel_delta_u{}, pixel_delta_v{};
    Vec3 defocus_disk_u{}, defocus_disk_v{};
    bool has_defocus{false};
};

Camera make_camera(Vec3 lookfrom, Vec3 lookat, Vec3 vup,
                   f32 vfov_deg, int img_w, int img_h,
                   f32 defocus_deg, f32 focus_dist) noexcept;

Ray camera_get_ray(const Camera&, int px, int py, Pcg32& rng) noexcept;

struct Film {
    Vec3* pixels;
    int   width, height;
    int   stride_pixels;
};

Film film_alloc(alloc::Arena&, int w, int h, int stride = 0) noexcept;

u32 tonemap_argb8888_gamma2(Vec3 linear) noexcept;
u8  byte_from_linear(f32 x) noexcept;

struct Sky { Vec3 low, high; };
Vec3 sky_sample(const Sky&, Vec3 unit_dir) noexcept;
```

`Film` is a non-owning view (pointer + dimensions). The backing storage
is allocated from an `aleph::alloc::Arena` (per-frame for sw_renderer)
or `std::malloc` (one-shot for `aleph_rt`).

## Module: `aleph.render.rt`

```cpp
struct RenderOpts {
    int spp{16};
    int max_depth{8};
    u64 base_seed{42};
    int tile_size{32};
};

void path_trace(const Scene& scene, const Camera& cam, Sky sky,
                Film& out, threads::Pool& pool, RenderOpts opts) noexcept;

Vec3 ray_color(const Scene&, Ray r, int depth, Sky sky,
               bool include_emission, Pcg32& rng) noexcept;
```

**Algorithm** (parity with Sotark cxx26 main.cpp):

- Tile-based: `pool.parallel_for(0, n_tiles, render_tile)`
- Per-tile `Pcg32(base_seed, tile_id + 1)` → order-independent output
- NEE on Lambertian hits: for each light in `scene.lights`,
  sample a uniform point on the quad, shadow ray, add BRDF · cos · area /
  (dist² · π) · emit. Skip metal/dielectric (cone too narrow).
- `include_emission=false` flag on the indirect bounce when NEE fired,
  to prevent double-counting.
- Material scatter via `MaterialKind` switch — inline each handler;
  no virtual, no `std::variant`.

## Module: `aleph.render.sw`

```cpp
struct ClipVert   { f32 x, y, z, w, u, v; };
struct ScreenVert { f32 x, y, z, u, v, inv_w; };

struct Face {
    std::array<Vec3, 4> verts;
    std::array<Vec2, 4> uvs;
    u32 tex_id;
    u32 lightmap_id;
};

struct Lightmap {
    u32* texels;          // ARGB modulator
    int  w, h;
    f32  u_min, u_max, v_min, v_max;
};

struct SceneRT {                              // sw runtime — distinct from rt's Scene
    std::vector<Face>     faces;
    std::vector<Lightmap> lightmaps;
    // Lightmap pixel storage is owned by SceneRT (one contiguous buffer
    // sliced by `Lightmap::texels` pointer) rather than per-Lightmap
    // allocations — friendlier to cache and bake parallelism.
    std::vector<u32> lightmap_pool;
};

void rasterize(const SceneRT&, Mat4 mvp,
               Film& color_out, std::span<f32> depth_out,
               threads::Pool&) noexcept;

void bake_lightmaps(SceneRT&, Vec3 light_pos, f32 intensity, f32 ambient) noexcept;
```

**Pipeline (per frame):**

1. Compute MVP, sort faces front-to-back (insertion sort on dist²)
2. Project all face vertices to clip space (Vec4)
3. Per-stripe (split height by thread count) via `Pool::parallel_for`:
   - For each face in order, Sutherland-Hodgman near-plane clip
   - Triangulate → 0/1/2 sub-tris
   - Backface cull (signed area sign)
   - Scanline edge-walk with subspan FDIV
   - Emit span to thread-local span buffer
   - Sample texture + lightmap, modulate, write
4. (After workers join) Editor overlay (UI, picked-face outline, HUD text)

**SIMD planning:** subspan FDIV → AVX2 batched (8 pixels in
`__m256` for u, v interpolation). Initial implementation is **scalar
subspan FDIV** for parity with Sotark; AVX2 variant added in a bench-
driven pass after Phase 2 closes.

## Module: `aleph.window`

```cpp
struct Event {
    enum class Kind { Quit, KeyDown, MouseDown, MouseUp, MouseMove, MouseWheel };
    Kind kind;
    int  key, button;
    int  x, y, dx, dy;
    int  wheel;
    bool shift, ctrl, alt;
};

class Window {
public:
    Window(int w, int h, const char* title) noexcept;   // aborts on SDL fail
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    int  poll_events(std::span<Event> out) noexcept;
    void present() noexcept;

    u32* pixels() noexcept;
    int  pitch_pixels() const noexcept;
    int  width()  const noexcept;
    int  height() const noexcept;

    u32 ticks_ms() const noexcept;
    u64 perf_counter() const noexcept;
    u64 perf_frequency() const noexcept;

private:
    /* SDL_Window*, SDL_Surface* — opaque */
};
```

Only this module includes `<SDL2/SDL.h>`. All other code that wants
windowing depends on `aleph.window`, not on SDL2 directly. This keeps
the rest of the codebase swappable to SDL3 / GLFW / native later.

## Module: `aleph.editor`

```cpp
struct OrbitCam {
    Vec3 target;
    f32  yaw, pitch, radius;
};

Vec3 orbit_eye(const OrbitCam&) noexcept;
bool orbit_handle(OrbitCam&, const window::Event&) noexcept;

int  pick_face(const sw::SceneRT&, int sx, int sy,
               Vec3 eye, Vec3 target, Vec3 up,
               f32 vfov_rad, f32 aspect, int win_w, int win_h) noexcept;

struct UiCtx { /* mouse state, hot/active widget, target film */ };

void ui_begin(UiCtx&, Film*, int mx, int my, bool mouse_down, bool mouse_pressed) noexcept;
void ui_end  (UiCtx&) noexcept;
void ui_panel(UiCtx&, int x, int y, int w, int h, std::string_view title);
bool ui_button   (UiCtx&, int x, int y, int w, int h, std::string_view label);
bool ui_slider_f (UiCtx&, int x, int y, int w, int h, f32& value, f32 minv, f32 maxv);

void draw_text(Film&, int x, int y, std::string_view, u32 color);
void draw_text_shadowed(Film&, int x, int y, std::string_view, u32 fg);
```

8x8 bitmap font ported from Sotark cxx26 verbatim (data table, no
re-derivation needed).

## Apps

### `apps/aleph_rt/main.cpp` (~150 LOC)

- CLI: `aleph_rt <out.ppm> <scene> [spp] [depth] [seed] [threads] [aux]`
- Scenes: `cover`, `cornell`, `mesh`, `obj`, `earth` (Sotark parity)
- Build scene via `scene_add_*` helpers (one builder fn per scene)
- `scene_build_bvh()` then `path_trace()` then `write_ppm()`

### `apps/aleph_sw/main.cpp` (~400 LOC)

- `Window win(800, 600, "aleph_sw editor")`
- Allocate color/depth buffers backed by `win.pixels()` + `pitch_pixels()`
- Construct default `SceneRT` (floor + pillar + cubes, per Sotark)
- `bake_lightmaps()`
- Main loop: poll events → orbit/pick/UI → bake-if-changed → `rasterize()`
  → UI overlay → selection outline + HUD → `win.present()`

## Build / flags

Existing presets (`release-strict`, `release`, `debug`, `asan`, `bench`)
unchanged. New CMake additions:

```cmake
add_subdirectory(render)
add_subdirectory(apps)
```

SDL2 detection block replicated from Sotark cxx26. `ALEPH_HAVE_SDL2`
gates `aleph_window`, `aleph_editor`, `aleph_sw` targets.

## Testing

```
tests/
  scene/   test_{handle32,soa_append,bvh_build,hit_sphere,hit_dispatch}.cpp
  render/  test_{camera,rt_smoke,rt_determinism,sw_rasterize,sw_span_buffer,sw_lightmap,sw_clip}.cpp
  window/  test_window_init.cpp     # skipped if !ALEPH_HAVE_SDL2
  editor/  test_{orbit,pick,ui_slider}.cpp
```

~50 new doctest cases on top of Phase 1's 85 → ~135 total.

### New isolation tests

`tests/isolation/iso_scene.cpp`, `iso_render_common.cpp`, `iso_render_rt.cpp`,
`iso_render_sw.cpp`, `iso_window.cpp` (conditional), `iso_editor.cpp`
(conditional). Same pattern as Phase 1 — each links exactly one module
library and runs a trivial main.

## Benchmarks

Added to `bench/bench_main.cpp`:

| Bench                                | Target          |
| ---                                  | ---             |
| `hit_sphere (single)`                | ≤ 12 cycles     |
| `hit_quad (single)`                  | ≤ 25 cycles     |
| `BVH traversal (100 items, miss)`    | ≤ 200 cycles    |
| `BVH traversal (100 items, hit)`     | ≤ 400 cycles    |
| `rasterize_subspan (16 px run)`      | ≤ 80 cycles     |
| `span_buffer_emit (1024 px row)`     | ≤ 1500 cycles   |

Same caveat as Phase 1: TSC at ~3 GHz nominal vs targets assuming ~4 GHz
boost. Targets aspirational; document measured gaps.

## Success criteria

1. All Phase 1 success criteria still hold (no regression).
2. `aleph_rt cover 5 5 42 4 /tmp/out.ppm` runs in <1 s, produces valid
   PPM ≥ 10 KB.
3. `aleph_rt cornell 20 10 42 8 /tmp/out.ppm` runs in <5 s, lights
   visible (verified by reading the PPM back via `aleph::io::load_ppm`
   and checking pixel brightness in the light region).
4. `aleph_rt mesh 10 10 42 8 /tmp/out.ppm` runs in <2 s with icosphere
   (1 280 tris).
5. `aleph_sw` opens a window for ≥ 1 s without crash; lightmaps baked
   at startup; ESC exits cleanly.
6. All component + smoke tests green; new `iso_*` tests for each new
   module pass.
7. asan + ubsan run clean against the test suite (still SKIPPED if
   libsanitizer system package is absent — documented as system gap).
8. Module dependency graph cycle-free (existing isolation enforcement
   extends to new modules).

## Out of scope for Phase 2

- Spectral rendering (Phase 4+ if ever)
- BDPT / MLT / VCM (Phase 4+)
- Multi-scatter GGX, path guiding (Phase 4+)
- Discrete Exterior Calculus / sheaf cohomology (Phase 4)
- GPU offload (Phase 5)
- Asset hot-reload / scene serialization
- Multi-window / multi-display
- Audio (defined in Phase 4 if needed)
- AVX2 batched ray-vs-4-AABB (deferred until benchmark demands)
- Parallel BVH build (deferred — single-threaded is enough for V1)

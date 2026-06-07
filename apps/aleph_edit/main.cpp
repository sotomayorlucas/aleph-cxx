// apps/aleph_edit — the structural editor's SDL shell (Phase 6, SPEC §3.3).
//
//   gesture ─▶ Op ─▶ apply_op ─▶ Graph ─▶ lower_incremental ─▶ {Scene, SceneRT} ─▶ pixels
//
// This app OWNS the SDL window, the input loop and the immediate-mode UI; it
// drives a *headless* `aleph::edit::EditorController` (the single source of
// truth lives in the controller's graph). The controller never imports SDL —
// only this shell uses `aleph.window` + `aleph.editor` (SPEC §3 ARCHITECTURE).
//
// ── Hybrid view (SPEC §3.3) ─────────────────────────────────────────────────
//   * orbit drag  -> controller.camera().orbit() -> RASTER mode: rasterize
//                    controller.raster_scene() (render::sw::rasterize) + present.
//   * left-click  -> controller.pick() -> controller.select() (highlight node).
//   * UI panel    -> selected node id/kind + a material color slider ->
//                    controller.apply(SetMaterial) on the selected mesh.
//   * keys a/l/x  -> AddObject / AddLight / DeleteObject Ops.
//   * IDLE (~250ms after the last input) -> progressive path-trace of
//     controller.render_scene() (render::rt::path_trace, 1->N spp, blit each
//     pass); ANY event cancels back to raster.
//
// ── Headless mode (SPEC §3.3, testable artifact) ────────────────────────────
//   `--headless <outdir>`: no window. Run a fixed Op script through the
//   controller; after each step write a PPM (raster composite + a path-trace
//   pass). Print entity/light counts; exit 0.

#include <algorithm>   // std::fill, std::clamp, std::max
#include <array>       // std::array (SDL event buffer)
#include <cmath>       // std::sqrt, std::abs (wave-field heatmap)
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>    // std::optional, std::nullopt
#include <span>        // std::span (poll_events)
#include <string>
#include <string_view>
#include <thread>
#include <vector>

import aleph.math;
import aleph.types;
import aleph.graph;
import aleph.lowering;
import aleph.scene;
import aleph.render.rt;
import aleph.render.sw;
import aleph.render.common;
import aleph.edit;
import aleph.alloc;
import aleph.threads;

#if defined(ALEPH_HAVE_SDL2)
import aleph.window;
import aleph.editor;
#endif

namespace {

using aleph::math::Vec3;
using aleph::math::Mat4;
using aleph::math::f32;
using aleph::math::u32;
using aleph::types::NodeId;

// Raster preview supersample factor (SSAA): render the 3D scene at kSSAA× in each
// axis into a scratch film, then box-downsample (linear) into the 1× film. kSSAA=2
// → 4 samples/px. Aspect is preserved (kSSAA·W/kSSAA·H == W/H) so the 1× MVP is
// reused verbatim. UI overlays stay at 1× (drawn after the downsample).
constexpr int kSSAA = 2;

// Selection silhouette ring colour (linear). Drawn into the SSAA buffer; the
// box-downsample then anti-aliases it.
constexpr Vec3 kSelectionColor{1.0f, 0.55f, 0.1f};

// ── The initial scene graph (the truth the controller takes ownership of) ────
//
//   root Transform (identity)
//     ├─Contains─▶ Camera (looks at the origin)
//     ├─Contains─▶ Transform (identity) ─Contains─▶ Mesh sphere ─References─▶ Material (Lambertian red)
//     ├─Contains─▶ Transform (identity) ─Contains─▶ Mesh metal  ─References─▶ Material (Metal)
//     ├─Contains─▶ Transform (identity) ─Contains─▶ Mesh glass  ─References─▶ Material (Dielectric)
//     ├─Contains─▶ Transform (identity) ─Contains─▶ Mesh floor  ─References─▶ Material (TexturedLambertian checker)
//     └─Contains─▶ Light (Area, overhead quad)
//
// Each mesh gets its OWN identity Transform (root → Transform → Mesh) so it is
// independently posable: `transform_of` finds it and SetTransform/
// translate_selected move just that object, not the whole scene.
//
// Lowers cleanly (a Camera + valid References), so the controller is
// immediately pickable and renderable. `roots` collects the node ids the shell
// / script reference later (root Transform = AddObject/AddLight parent;
// `sphere` = the first selectable / deletable mesh).
struct InitialScene {
    aleph::graph::Graph g;
    NodeId root{};
    NodeId sphere{};
    NodeId floor{};
};

InitialScene build_initial_graph() {
    using namespace aleph::types;
    InitialScene s;
    aleph::graph::Graph& g = s.g;

    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, LocalTransform{Mat4::identity()}});

    const NodeId cam_id = g.alloc_node_id();
    Camera cam{cam_id, std::string("sensor0")};
    cam.look_from  = Vec3{0.0f, 1.0f, 5.0f};
    cam.look_at    = Vec3{0.0f, 0.5f, 0.0f};
    cam.up         = Vec3{0.0f, 1.0f, 0.0f};
    cam.vfov_deg   = 45.0f;
    cam.aperture   = 0.0f;
    cam.focus_dist = 5.0f;
    g.insert_node(std::move(cam));

    // A sphere sitting on the floor — the primary selectable object.
    s.sphere = g.alloc_node_id();
    Mesh sphere{s.sphere, std::string("sphere"), 0};
    sphere.geometry = SphereLocal{Vec3{0.0f, 0.5f, 0.0f}, 0.5f};
    g.insert_node(std::move(sphere));

    const NodeId sphere_mat = g.alloc_node_id();
    Material smat{sphere_mat, MaterialKind::Lambertian};
    smat.albedo = Vec3{0.85f, 0.15f, 0.15f};
    smat.emit   = Vec3{0.0f, 0.0f, 0.0f};
    g.insert_node(std::move(smat));

    // A chrome sphere to the matte sphere's left — exercises the Metal raster shade
    // (signed sky↔ground reflection + Blinn-Phong highlight).
    const NodeId metal_sphere = g.alloc_node_id();
    Mesh msphere{metal_sphere, std::string("metal_sphere"), 0};
    msphere.geometry = SphereLocal{Vec3{-1.5f, 0.5f, 0.0f}, 0.5f};
    g.insert_node(std::move(msphere));

    const NodeId metal_mat = g.alloc_node_id();
    Material mmat{metal_mat, MaterialKind::Metal};
    mmat.albedo = Vec3{0.85f, 0.85f, 0.9f};
    mmat.fuzz   = 0.05f;
    mmat.emit   = Vec3{0.0f, 0.0f, 0.0f};
    g.insert_node(std::move(mmat));

    // A glass sphere to the matte sphere's right — exercises the Dielectric raster
    // shade (Schlick Fresnel bright rim + dim center).
    const NodeId glass_sphere = g.alloc_node_id();
    Mesh gsphere{glass_sphere, std::string("glass_sphere"), 0};
    gsphere.geometry = SphereLocal{Vec3{1.5f, 0.5f, 0.0f}, 0.5f};
    g.insert_node(std::move(gsphere));

    const NodeId glass_mat = g.alloc_node_id();
    Material gmat{glass_mat, MaterialKind::Dielectric};
    gmat.ior  = 1.5f;
    gmat.emit = Vec3{0.0f, 0.0f, 0.0f};
    g.insert_node(std::move(gmat));

    // A large floor quad in the y=0 plane.
    s.floor = g.alloc_node_id();
    Mesh floor{s.floor, std::string("floor"), 0};
    floor.geometry = QuadLocal{Vec3{-4.0f, 0.0f, -4.0f},
                               Vec3{8.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, 8.0f}};
    g.insert_node(std::move(floor));

    const NodeId floor_mat = g.alloc_node_id();
    Material fmat{floor_mat, MaterialKind::TexturedLambertian};
    fmat.albedo   = Vec3{0.6f, 0.55f, 0.5f};   // neutral tan/grey checker base
    fmat.emit     = Vec3{0.0f, 0.0f, 0.0f};
    fmat.uv_scale = 8.0f;                       // 8 tiles across the 8-unit floor quad (uv∈[0,1])
    g.insert_node(std::move(fmat));

    // An overhead area light (its own emissive quad geometry).
    const NodeId light_id = g.alloc_node_id();
    Light light{light_id, LightKind::Area, std::string("emit0")};
    light.emission = Vec3{6.0f, 6.0f, 6.0f};
    light.geometry = QuadLocal{Vec3{-1.0f, 3.0f, -1.0f},
                               Vec3{2.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, 2.0f}};
    g.insert_node(std::move(light));

    // Per-object identity Transforms so each mesh is independently posable
    // (transform_of finds them; SetTransform/translate_selected move just one).
    const NodeId xf_sphere = g.alloc_node_id();
    g.insert_node(Transform{xf_sphere, 0, LocalTransform{Mat4::identity()}});
    const NodeId xf_metal = g.alloc_node_id();
    g.insert_node(Transform{xf_metal, 0, LocalTransform{Mat4::identity()}});
    const NodeId xf_glass = g.alloc_node_id();
    g.insert_node(Transform{xf_glass, 0, LocalTransform{Mat4::identity()}});
    const NodeId xf_floor = g.alloc_node_id();
    g.insert_node(Transform{xf_floor, 0, LocalTransform{Mat4::identity()}});

    (void)g.add_edge(EdgeKind::Contains,   s.root, cam_id);
    (void)g.add_edge(EdgeKind::Contains,   s.root, xf_sphere);
    (void)g.add_edge(EdgeKind::Contains,   xf_sphere, s.sphere);
    (void)g.add_edge(EdgeKind::Contains,   s.root, xf_metal);
    (void)g.add_edge(EdgeKind::Contains,   xf_metal, metal_sphere);
    (void)g.add_edge(EdgeKind::Contains,   s.root, xf_glass);
    (void)g.add_edge(EdgeKind::Contains,   xf_glass, glass_sphere);
    (void)g.add_edge(EdgeKind::Contains,   s.root, xf_floor);
    (void)g.add_edge(EdgeKind::Contains,   xf_floor, s.floor);
    (void)g.add_edge(EdgeKind::Contains,   s.root, light_id);
    (void)g.add_edge(EdgeKind::References, s.sphere, sphere_mat);
    (void)g.add_edge(EdgeKind::References, metal_sphere, metal_mat);
    (void)g.add_edge(EdgeKind::References, glass_sphere, glass_mat);
    (void)g.add_edge(EdgeKind::References, s.floor,  floor_mat);
    return s;
}

// ── A lattice demo scene (the wave-field stage, SPEC §3.2 physics seam) ──────
//
//   root Transform (identity)
//     ├─Contains─▶ Camera (overhead, framing the grid)
//     ├─Contains─▶ R×R Mesh (SphereLocal) ─References─▶ Material (Lambertian grey)
//     │            joined by Adjacent edges (4-neighbourhood) — the 1-skeleton the
//     │            wave Laplacian Δ is built over.
//     └─Contains─▶ Light (Area, overhead quad)
//
// `nodes` is the FLAT row-major Mesh id list (`nodes[z*R + x]`); `root` is the
// AddObject/AddLight/DeleteObject parent. The Adjacent edges are what couple the
// spheres into a single Δ vertex set so a kick at one node ripples to the rest.
struct LatticeScene {
    aleph::graph::Graph g;
    std::vector<NodeId> nodes;  // row-major Mesh ids: nodes[z*R + x]
    NodeId root{};
};

LatticeScene build_lattice_graph(int R) {
    using namespace aleph::types;
    LatticeScene s;
    aleph::graph::Graph& g = s.g;

    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, LocalTransform{Mat4::identity()}});

    const NodeId cam_id = g.alloc_node_id();
    Camera cam{cam_id, std::string("sensor0")};
    cam.look_from  = Vec3{(static_cast<f32>(R) - 1.0f) * 0.5f,
                          static_cast<f32>(R) * 0.9f,
                          static_cast<f32>(R) * 1.4f};
    cam.look_at    = Vec3{(static_cast<f32>(R) - 1.0f) * 0.5f, 0.0f,
                          (static_cast<f32>(R) - 1.0f) * 0.5f};
    cam.up         = Vec3{0.0f, 1.0f, 0.0f};
    cam.vfov_deg   = 45.0f;
    cam.aperture   = 0.0f;
    cam.focus_dist = 5.0f;
    g.insert_node(std::move(cam));
    (void)g.add_edge(EdgeKind::Contains, s.root, cam_id);

    // The R×R grid of small spheres (each with its own Lambertian Material).
    std::vector<std::vector<NodeId>> grid(
        static_cast<std::size_t>(R), std::vector<NodeId>(static_cast<std::size_t>(R)));
    for (int z = 0; z < R; ++z) {
        for (int x = 0; x < R; ++x) {
            const NodeId m = g.alloc_node_id();
            Mesh mesh{m, std::string("n") + std::to_string(z * R + x), 0};
            mesh.geometry = SphereLocal{Vec3{static_cast<f32>(x), 0.0f,
                                             static_cast<f32>(z)},
                                        0.35f};
            g.insert_node(std::move(mesh));

            const NodeId mat = g.alloc_node_id();
            Material mt{mat, MaterialKind::Lambertian};
            mt.albedo = Vec3{0.8f, 0.8f, 0.8f};
            mt.emit   = Vec3{0.0f, 0.0f, 0.0f};
            g.insert_node(std::move(mt));

            (void)g.add_edge(EdgeKind::Contains,   s.root, m);
            (void)g.add_edge(EdgeKind::References, m,      mat);
            grid[static_cast<std::size_t>(z)][static_cast<std::size_t>(x)] = m;
            s.nodes.push_back(m);
        }
    }

    // 4-neighbourhood Adjacent edges (the wave's coupling skeleton).
    for (int z = 0; z < R; ++z) {
        for (int x = 0; x < R; ++x) {
            const NodeId here = grid[static_cast<std::size_t>(z)][static_cast<std::size_t>(x)];
            if (x + 1 < R) {
                (void)g.add_edge(EdgeKind::Adjacent, here,
                                 grid[static_cast<std::size_t>(z)][static_cast<std::size_t>(x + 1)]);
            }
            if (z + 1 < R) {
                (void)g.add_edge(EdgeKind::Adjacent, here,
                                 grid[static_cast<std::size_t>(z + 1)][static_cast<std::size_t>(x)]);
            }
        }
    }

    // An overhead area light spanning the whole grid (x,z ∈ [-1, R]) so the far
    // lattice is lit, not just the near corner.
    const NodeId light_id = g.alloc_node_id();
    Light light{light_id, LightKind::Area, std::string("emit0")};
    light.emission = Vec3{6.0f, 6.0f, 6.0f};
    const f32 span = static_cast<f32>(R) + 1.0f;
    light.geometry = QuadLocal{Vec3{-1.0f, static_cast<f32>(R) * 1.5f, -1.0f},
                               Vec3{span, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, span}};
    g.insert_node(std::move(light));
    (void)g.add_edge(EdgeKind::Contains, s.root, light_id);
    return s;
}

// A Lambertian MaterialParams of a given albedo (the recolor payload).
aleph::lowering::MaterialParams lambertian(Vec3 albedo) {
    aleph::lowering::MaterialParams m{};
    m.kind   = aleph::types::MaterialKind::Lambertian;
    m.albedo = albedo;
    m.fuzz   = 0.0f;
    m.ior    = 1.5f;
    m.emit   = Vec3{0.0f, 0.0f, 0.0f};
    return m;
}

// ── The shared editor sky (raster clear gradient + path-trace background). ───
const aleph::render::common::Sky kSky{Vec3{0.45f, 0.58f, 0.78f},
                                      Vec3{0.78f, 0.86f, 0.98f}};

// Clear a Film to the sky gradient (top = high, bottom = low), the raster
// view's background. `unit_dir.y` runs +1 (top row) to −1 (bottom row).
void clear_sky(aleph::render::common::Film& film) noexcept {
    for (int y = 0; y < film.height; ++y) {
        const f32 t = (film.height > 1)
            ? static_cast<f32>(y) / static_cast<f32>(film.height - 1)
            : 0.0f;
        const Vec3 c = aleph::math::lerp(kSky.high, kSky.low, t);
        for (int x = 0; x < film.width; ++x) {
            film.pixels[y * film.stride_pixels + x] = c;
        }
    }
}

// The MVP for the controller's orbit camera at this image size. Mirrors the
// rasterizer convention used by aleph_sw (perspective * look_at).
Mat4 orbit_mvp(const aleph::edit::OrbitCamera& cam, int w, int h) noexcept {
    const Mat4 view = cam.view();
    const Mat4 proj = Mat4::perspective(
        aleph::math::deg_to_rad(cam.vfov_deg),
        static_cast<f32>(w) / static_cast<f32>(h),
        0.05f, 100.0f);
    return proj * view;
}

// Write a Film (linear Vec3) as a gamma-2.0 binary PPM. Returns false on a file
// error so the headless path can report and exit non-zero.
bool write_ppm(const char* path, const aleph::render::common::Film& film) {
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", film.width, film.height);
    for (int y = 0; y < film.height; ++y) {
        for (int x = 0; x < film.width; ++x) {
            const Vec3 lin = film.pixels[y * film.stride_pixels + x];
            const unsigned char rgb[3] = {
                aleph::render::common::byte_from_linear(lin.x),
                aleph::render::common::byte_from_linear(lin.y),
                aleph::render::common::byte_from_linear(lin.z),
            };
            std::fwrite(rgb, 1, 3, f);
        }
    }
    std::fclose(f);
    return true;
}

int thread_count() noexcept {
    int t = static_cast<int>(std::thread::hardware_concurrency());
    return t > 0 ? t : 1;
}

// ── Headless scripted mode (SPEC §3.3) ───────────────────────────────────────
// Run a fixed Op sequence through the controller; after each step rasterize +
// path-trace and write two PPMs. Print entity/light counts. Returns the process
// exit code (0 on success).
int run_headless(const std::string& outdir) {
    InitialScene init = build_initial_graph();
    const NodeId root   = init.root;
    const NodeId sphere = init.sphere;

    aleph::edit::EditorController controller{std::move(init.g)};

    constexpr int W = 320, H = 240;
    controller.set_viewport(W, H);
    // Frame the scene: orbit a touch above and around.
    auto& cam = controller.camera();
    cam.target = Vec3{0.0f, 0.5f, 0.0f};
    cam.yaw    = 0.4f;
    cam.pitch  = 0.25f;
    cam.radius = 5.0f;
    cam.vfov_deg = 45.0f;
    // Re-bake sw_ at the FINAL framed pose: the ctor baked while cam_ was still the
    // default {0,0,5} eye, but Metal/Dielectric vcol is view-dependent.
    controller.rebake_view();

    aleph::threads::Pool pool(thread_count());

    // Film (heap — W*H*sizeof(Vec3) is modest).
    std::vector<Vec3> film_px(static_cast<std::size_t>(W) * H);
    aleph::render::common::Film film{film_px.data(), W, H, W};

    // SSAA scratch: render raster at kSSAA× then box-downsample into `film`.
    std::vector<Vec3> ss_px(static_cast<std::size_t>(kSSAA) * kSSAA * W * H);
    std::vector<f32> ss_depth(static_cast<std::size_t>(kSSAA) * kSSAA * W * H, 0.0f);
    aleph::render::common::Film ss_film{ss_px.data(), kSSAA * W, kSSAA * H, kSSAA * W};

    // Render the controller's current state to two PPMs (raster + path-trace).
    int step_no = 0;
    auto dump = [&](const char* label) -> bool {
        // (1) Raster composite. `build_sw_scene` faces carry no lightmap
        // (lightmap_id == 0xFFFFFFFF) and a baked flat-lit albedo, so the
        // controller's SceneRT rasterizes directly (no per-face fixup needed).
        // Supersample at kSSAA× then box-downsample (linear) into the 1× film.
        clear_sky(ss_film);
        std::fill(ss_depth.begin(), ss_depth.end(), 0.0f);
        aleph::render::sw::rasterize(controller.raster_scene(),
                                     orbit_mvp(controller.camera(), kSSAA * W, kSSAA * H),
                                     ss_film, ss_depth, pool);
        aleph::render::sw::downsample_box(ss_film, film, kSSAA);
        std::string rp = outdir + "/step" + std::to_string(step_no) + "_" + label
                       + "_raster.ppm";
        if (!write_ppm(rp.c_str(), film)) {
            std::fprintf(stderr, "aleph_edit: cannot write %s\n", rp.c_str());
            return false;
        }
        if (std::getenv("ALEPH_DUMP_DEPTH") != nullptr) {
            // Visualize the SS depth buffer (1/w, near=bright) — take each 2×2
            // block's top-left sample as the 1× representative.
            std::vector<Vec3> dv(static_cast<std::size_t>(W) * H);
            const std::size_t ss   = static_cast<std::size_t>(kSSAA);
            const std::size_t sstr = ss * static_cast<std::size_t>(W);  // SS film stride
            for (std::size_t dy = 0; dy < static_cast<std::size_t>(H); ++dy) {
                for (std::size_t dx = 0; dx < static_cast<std::size_t>(W); ++dx) {
                    const std::size_t si = dy * ss * sstr + dx * ss;
                    const f32 d = std::min(1.0f, ss_depth[si] * 3.0f);
                    dv[dy * static_cast<std::size_t>(W) + dx] = Vec3{d, d, d};
                }
            }
            aleph::render::common::Film df{dv.data(), W, H, W};
            std::string dp = outdir + "/step" + std::to_string(step_no) + "_" + label
                           + "_depth.ppm";
            (void)write_ppm(dp.c_str(), df);
        }

        // (2) Path-trace pass of the SAME lowered scene under the SAME pose.
        clear_sky(film);
        const aleph::render::common::Camera pcam =
            controller.camera().render_camera(W, H);
        aleph::render::rt::RenderOpts opts{8, 6, 42ull, 32};
        aleph::render::rt::path_trace(controller.render_scene(), pcam, kSky,
                                      film, pool, opts);
        std::string pp = outdir + "/step" + std::to_string(step_no) + "_" + label
                       + "_pt.ppm";
        if (!write_ppm(pp.c_str(), film)) {
            std::fprintf(stderr, "aleph_edit: cannot write %s\n", pp.c_str());
            return false;
        }

        std::printf("aleph_edit[headless]: step %d (%s) entities=%zu lights=%zu "
                    "faces=%zu\n",
                    step_no, label,
                    controller.lowered().entities.size(),
                    controller.lowered().lights.size(),
                    controller.raster_scene().faces.size());
        ++step_no;
        return true;
    };

    // Apply an Op and report a structured failure (never throws).
    auto apply = [&](const aleph::lowering::Op& op, const char* what) -> bool {
        auto r = controller.apply(op);
        if (!r.has_value()) {
            std::fprintf(stderr, "aleph_edit: apply(%s) failed (OpError %d)\n",
                         what, static_cast<int>(r.error()));
            return false;
        }
        return true;
    };

    // ── step 0: the initial scene. ───────────────────────────────────────────
    if (!dump("init")) return 1;

    // ── step 1: AddObject (a second sphere). ─────────────────────────────────
    {
        aleph::lowering::AddObject add{};
        add.parent   = root;
        add.geometry = aleph::types::SphereLocal{Vec3{0.0f, 0.5f, 1.5f}, 0.5f};
        add.material = lambertian(Vec3{0.15f, 0.5f, 0.85f});
        if (!apply(aleph::lowering::Op{add}, "AddObject")) return 1;
    }
    if (!dump("add_object")) return 1;

    // ── step 2: AddLight (a second overhead light). ──────────────────────────
    {
        aleph::lowering::AddLight light{};
        light.parent   = root;
        light.kind     = aleph::types::LightKind::Area;
        light.emission = Vec3{4.0f, 4.0f, 4.0f};
        light.geometry = aleph::types::QuadLocal{Vec3{1.0f, 3.0f, 1.0f},
                                                 Vec3{1.0f, 0.0f, 0.0f},
                                                 Vec3{0.0f, 0.0f, 1.0f}};
        if (!apply(aleph::lowering::Op{light}, "AddLight")) return 1;
    }
    if (!dump("add_light")) return 1;

    // ── step 3: SetMaterial (recolor the original sphere green). ─────────────
    {
        aleph::lowering::SetMaterial set{};
        set.target = sphere;
        set.params = lambertian(Vec3{0.15f, 0.85f, 0.2f});
        if (!apply(aleph::lowering::Op{set}, "SetMaterial")) return 1;
    }
    if (!dump("set_material")) return 1;

    // ── step 4: DeleteObject (remove the original sphere). ───────────────────
    {
        if (!apply(aleph::lowering::Op{aleph::lowering::DeleteObject{sphere}},
                   "DeleteObject")) {
            return 1;
        }
    }
    if (!dump("delete_object")) return 1;

    std::printf("aleph_edit[headless]: done — %d steps, final entities=%zu "
                "lights=%zu\n",
                step_no,
                controller.lowered().entities.size(),
                controller.lowered().lights.size());
    return 0;
}

// ── Live-orbit view-tracking capture (visual slice 4c-i-b) ───────────────────
// Demonstrate that re-baking sw_ at the live eye (rebake_view_sw) makes the
// Metal/Dielectric raster vcol track the orbit: render the editor scene at two
// different orbit yaws — each preceded by rebake_view_sw() — into a side-by-side
// raster montage. The chrome sphere's sky/ground reflection + highlight should
// sit in a DIFFERENT place between the two poses (vs the frozen 4c-i behaviour).
// Not SDL-guarded so it runs headless. Returns the process exit code.
int run_orbit_track(const std::string& outdir) {
    InitialScene init = build_initial_graph();
    aleph::edit::EditorController controller{std::move(init.g)};

    constexpr int W = 320, H = 240;
    controller.set_viewport(W, H);
    auto& cam    = controller.camera();
    cam.target   = Vec3{0.0f, 0.5f, 0.0f};
    cam.pitch    = 0.25f;
    cam.radius   = 5.0f;
    cam.vfov_deg = 45.0f;

    if (!controller.has_view_dependent_material()) {
        std::fprintf(stderr,
            "aleph_edit[orbit-track]: scene has no Metal/Dielectric — nothing to track\n");
        return 1;
    }

    aleph::threads::Pool pool(thread_count());

    // The montage is two raster panels side by side (2W × H).
    std::vector<Vec3> mont_px(static_cast<std::size_t>(2 * W) * H);
    aleph::render::common::Film montage{mont_px.data(), 2 * W, H, 2 * W};

    std::vector<Vec3> ss_px(static_cast<std::size_t>(kSSAA) * kSSAA * W * H);
    std::vector<f32>  ss_depth(static_cast<std::size_t>(kSSAA) * kSSAA * W * H, 0.0f);
    aleph::render::common::Film ss_film{ss_px.data(), kSSAA * W, kSSAA * H, kSSAA * W};

    std::vector<Vec3> panel_px(static_cast<std::size_t>(W) * H);
    aleph::render::common::Film panel{panel_px.data(), W, H, W};

    // Render the controller's current raster view (eye = cam) into `panel`, then
    // copy it into the montage at horizontal offset `x0`.
    auto render_into = [&](int x0) {
        clear_sky(ss_film);
        std::fill(ss_depth.begin(), ss_depth.end(), 0.0f);
        aleph::render::sw::rasterize(controller.raster_scene(),
                                     orbit_mvp(controller.camera(), kSSAA * W, kSSAA * H),
                                     ss_film, ss_depth, pool);
        aleph::render::sw::downsample_box(ss_film, panel, kSSAA);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                montage.pixels[y * montage.stride_pixels + (x0 + x)] =
                    panel.pixels[y * panel.stride_pixels + x];
            }
        }
    };

    // Pose A: yaw 0.0; re-bake sw_ at this eye, render the left panel.
    cam.yaw = 0.0f;
    controller.rebake_view_sw();
    render_into(0);

    // Pose B: a large yaw swing; re-bake again, render the right panel. With
    // tracking ON the chrome reflection follows the eye, so the two panels differ
    // on the Metal/Dielectric spheres.
    cam.yaw = 1.2f;
    controller.rebake_view_sw();
    render_into(W);

    std::string mp = outdir + "/2026-06-06-live-orbit-tracking.ppm";
    if (!write_ppm(mp.c_str(), montage)) {
        std::fprintf(stderr, "aleph_edit: cannot write %s\n", mp.c_str());
        return 1;
    }
    std::printf("aleph_edit[orbit-track]: wrote %s (poseA yaw=0.0, poseB yaw=1.2, "
                "view-dependent=%d)\n",
                mp.c_str(),
                static_cast<int>(controller.has_view_dependent_material()));
    return 0;
}

// ── Headless wave capture (SPEC §3.2 physics seam) ───────────────────────────
// The thesis demo: kick a node at the centre of a lattice, capture the φ-wave as
// it ripples outward (N frames), then DELETE a node in the wavefront's path — the
// topology edit re-derives Δ and re-projects φ (survivors keep their value, the
// gap zeroes), so the ripple genuinely RE-ROUTES around the hole while the rest
// of the wave persists (N more frames). All frames are deterministic PPMs.
int run_wave(const std::string& outdir) {
    // R is large enough that a mid-run DeleteObject's local 2-hop dirty ball is
    // ≪ |E|, so the controller's localized κ_R rebuild engages (O(touched), not a
    // full rebuild) — the demonstrable win of this slice. (A small grid would make
    // the 2-hop neighbourhood a large fraction of |E| and trip the full-bounded
    // fallback, which is correct but doesn't show localization.)
    constexpr int R = 13, N = 24;
    constexpr aleph::math::f32 kDt = 0.02f;

    LatticeScene init = build_lattice_graph(R);
    // The kick site (grid centre) and a blocker the rightward wavefront reaches.
    const NodeId center  = init.nodes[static_cast<std::size_t>((R / 2) * R + (R / 2))];
    const NodeId blocker = init.nodes[static_cast<std::size_t>((R / 2) * R + (R / 2 + 2))];

    aleph::edit::EditorController controller{std::move(init.g)};
    constexpr int W = 320, H = 240;
    controller.set_viewport(W, H);
    auto& cam = controller.camera();
    cam.target   = Vec3{(static_cast<f32>(R) - 1.0f) * 0.5f, 0.0f,
                        (static_cast<f32>(R) - 1.0f) * 0.5f};
    cam.yaw      = 0.5f;
    cam.pitch    = 0.6f;
    cam.radius   = static_cast<f32>(R) * 1.7f;
    cam.vfov_deg = 45.0f;
    // Re-bake sw_ at the FINAL framed pose (the ctor baked at the default eye).
    controller.rebake_view();

    controller.enable_sim(true);
    (void)controller.kick(center, 1.5);

    aleph::threads::Pool pool(thread_count());
    std::vector<Vec3> film_px(static_cast<std::size_t>(W) * H);
    aleph::render::common::Film film{film_px.data(), W, H, W};

    // SSAA scratch: render raster at kSSAA× then box-downsample into `film`.
    std::vector<Vec3> ss_px(static_cast<std::size_t>(kSSAA) * kSSAA * W * H);
    std::vector<f32> ss_depth(static_cast<std::size_t>(kSSAA) * kSSAA * W * H, 0.0f);
    aleph::render::common::Film ss_film{ss_px.data(), kSSAA * W, kSSAA * H, kSSAA * W};

    int frame = 0;
    auto dump = [&]() -> bool {
        clear_sky(ss_film);
        std::fill(ss_depth.begin(), ss_depth.end(), 0.0f);
        aleph::render::sw::rasterize(controller.raster_scene(),
                                     orbit_mvp(controller.camera(), kSSAA * W, kSSAA * H),
                                     ss_film, ss_depth, pool);
        aleph::render::sw::downsample_box(ss_film, film, kSSAA);
        const std::string p =
            outdir + "/step" + std::to_string(frame) + "_wave_raster.ppm";
        if (!write_ppm(p.c_str(), film)) {
            std::fprintf(stderr, "aleph_edit: cannot write %s\n", p.c_str());
            return false;
        }
        // φ-FIELD HEATMAP: a top-down R×R image of the wave field on the lattice —
        // the propagating ripple reads here where the 3D raster's fixed colormap
        // keeps the low-amplitude front near-white. Per-frame normalized by max|φ|
        // with a signed-sqrt to lift the front relative to the kicked centre;
        // diverging white→red (φ>0) / white→blue (φ<0); a DELETED node renders as a
        // dark hole, so the post-delete frames show the wave routing AROUND it.
        {
            constexpr int kCell = 18;  // heatmap pixels per lattice cell
            const auto& u = controller.displacement();
            double maxabs = 0.0;
            for (const NodeId nd : init.nodes)
                if (const double* v = u.at(nd)) maxabs = std::max(maxabs, std::abs(*v));
            const double inv = (maxabs > 1e-12) ? 1.0 / maxabs : 0.0;
            const int HW = R * kCell;
            std::vector<unsigned char> img(static_cast<std::size_t>(HW) *
                                           static_cast<std::size_t>(HW) * 3u, 0u);
            for (int z = 0; z < R; ++z) {
                for (int x = 0; x < R; ++x) {
                    const NodeId nd = init.nodes[static_cast<std::size_t>(z * R + x)];
                    const double* v = u.at(nd);
                    double cr, cg, cb;
                    if (v == nullptr) {                       // deleted node = dark hole
                        cr = cg = cb = 0.15;
                    } else {
                        const double nf = (*v) * inv;          // ~[-1,1]
                        // Signed 4th-root: a strong perceptual lift (the kicked
                        // centre's |φ| dwarfs the propagating front; this compresses
                        // the huge dynamic range so the travelling ripple reads,
                        // like a log-scale field plot — sign/structure preserved).
                        const double t = (nf >= 0.0 ? 1.0 : -1.0) *
                                         std::pow(std::abs(nf), 0.25);
                        if (t >= 0.0) { cr = 1.0;       cg = 1.0 - t; cb = 1.0 - t; }
                        else          { cr = 1.0 + t;   cg = 1.0 + t; cb = 1.0;     }
                    }
                    const auto to8 = [](double c) {
                        return static_cast<unsigned char>(std::clamp(c, 0.0, 1.0) * 255.0);
                    };
                    const unsigned char r8 = to8(cr), g8 = to8(cg), b8 = to8(cb);
                    for (int dy = 0; dy < kCell; ++dy) {
                        for (int dx = 0; dx < kCell; ++dx) {
                            const std::size_t px =
                                (static_cast<std::size_t>(z * kCell + dy) *
                                     static_cast<std::size_t>(HW) +
                                 static_cast<std::size_t>(x * kCell + dx)) * 3u;
                            img[px] = r8; img[px + 1u] = g8; img[px + 2u] = b8;
                        }
                    }
                }
            }
            const std::string fp =
                outdir + "/step" + std::to_string(frame) + "_wave_field.ppm";
            if (std::FILE* f = std::fopen(fp.c_str(), "wb")) {
                std::fprintf(f, "P6\n%d %d\n255\n", HW, HW);
                std::fwrite(img.data(), 1, img.size(), f);
                std::fclose(f);
            }
        }
        ++frame;
        return true;
    };

    // Phase 1: the ripple expands from the kicked centre node.
    for (int i = 0; i < N; ++i) {
        if (auto sr = controller.step(kDt); !sr.has_value()) {
            std::fprintf(stderr, "aleph_edit[wave]: step failed (StepError %d)\n",
                         static_cast<int>(sr.error()));
            return 1;
        }
        if (!dump()) return 1;
    }

    // Topology change: delete a node in the wavefront's path. apply() re-derives
    // Δ and reprojects φ (survivors keep φ, the gap zeroes) — the ripple re-routes
    // around the hole.
    auto r = controller.apply(
        aleph::lowering::Op{aleph::lowering::DeleteObject{blocker}});
    if (!r.has_value()) {
        std::fprintf(stderr,
                     "aleph_edit[wave]: DeleteObject failed (OpError %d)\n",
                     static_cast<int>(r.error()));
        return 1;
    }
    // The localized win: the mid-run DeleteObject rebuilt Δ by recomputing only
    // the κ_R edges within 2 hops of the deleted node — O(touched) ≪ |E|.
    std::printf("aleph_edit[wave]: DeleteObject recomputed %d kappa_R edges "
                "(|E| = %zu)\n",
                controller.last_recompute_count(),
                controller.wave_operator().curvatures.size());

    // Phase 2: the wave continues, now re-routing around the deleted node.
    for (int i = 0; i < N; ++i) {
        if (auto sr = controller.step(kDt); !sr.has_value()) {
            std::fprintf(stderr, "aleph_edit[wave]: step failed (StepError %d)\n",
                         static_cast<int>(sr.error()));
            return 1;
        }
        if (!dump()) return 1;
    }

    std::printf("aleph_edit[wave]: wrote %d frames to %s\n", frame, outdir.c_str());
    return 0;
}

#if defined(ALEPH_HAVE_SDL2)

// ── Live SDL hybrid shell (SPEC §3.3) ────────────────────────────────────────
// Manual-smoke only (not auto-tested). Drives the headless controller from real
// input: orbit drag -> raster; left-click -> pick/select; UI material slider ->
// SetMaterial; keys a/l/x -> Add/Add/Delete; idle -> progressive path-trace.
int run_live(bool wave_demo = false) {
    constexpr int W = 800, H = 600;
    constexpr int R = 7;             // lattice resolution (wave demo)
    constexpr f32 kWaveDt = 0.02f;   // fixed physics sub-step per frame
    aleph::window::Window win(W, H, wave_demo
        ? "aleph_edit — wave on the shared Laplacian (click=select, K=kick, X=delete, drag=orbit)"
        : "aleph_edit — structural editor");

    // Scene: the wave demo seeds an R×R Adjacent lattice (so Δ couples the nodes);
    // the plain editor keeps the sphere+floor scene. Both yield a graph + root.
    NodeId root{};
    NodeId kick_seed{};
    aleph::graph::Graph scene_graph = [&] {
        if (wave_demo) {
            LatticeScene ls = build_lattice_graph(R);
            root      = ls.root;
            kick_seed = ls.nodes[static_cast<std::size_t>((R / 2) * R + (R / 2))];
            return std::move(ls.g);
        }
        InitialScene is = build_initial_graph();
        root = is.root;
        return std::move(is.g);
    }();
    aleph::edit::EditorController controller{std::move(scene_graph)};
    controller.set_viewport(W, H);
    auto& cam = controller.camera();
    if (wave_demo) {
        cam.target   = Vec3{(static_cast<f32>(R) - 1.0f) * 0.5f, 0.0f,
                            (static_cast<f32>(R) - 1.0f) * 0.5f};
        cam.yaw      = 0.5f;
        cam.pitch    = 0.6f;
        cam.radius   = static_cast<f32>(R) * 1.7f;
        cam.vfov_deg = 45.0f;
        controller.enable_sim(true);
        (void)controller.kick(kick_seed, 1.5);   // an initial ping to watch
    } else {
        cam.target   = Vec3{0.0f, 0.5f, 0.0f};
        cam.yaw      = 0.4f;
        cam.pitch    = 0.25f;
        cam.radius   = 5.0f;
        cam.vfov_deg = 45.0f;
    }
    // Re-bake sw_ at the FINAL framed pose (the ctor baked at the default eye);
    // Metal/Dielectric vcol is view-dependent.
    controller.rebake_view();

    aleph::threads::Pool pool(thread_count());
    aleph::editor::UiCtx ui{};

    std::vector<Vec3> film_px(static_cast<std::size_t>(W) * H);
    aleph::render::common::Film film{film_px.data(), W, H, W};

    // SSAA scratch: render the 3D scene at kSSAA× then box-downsample into `film`.
    // The UI overlay is drawn on `film` at 1× AFTER the downsample (never SSAA'd).
    std::vector<Vec3> ss_px(static_cast<std::size_t>(kSSAA) * kSSAA * W * H);
    std::vector<f32> ss_depth(static_cast<std::size_t>(kSSAA) * kSSAA * W * H, 0.0f);
    aleph::render::common::Film ss_film{ss_px.data(), kSSAA * W, kSSAA * H, kSSAA * W};

    // Selection-outline scratch: a depth buffer that receives a raster pass of
    // ONLY the selected entity's faces (coverage). `sel_film` is required by the
    // rasterize API but its colour output is unused — only `sel_depth` is read.
    std::vector<Vec3> sel_px(static_cast<std::size_t>(kSSAA) * kSSAA * W * H);
    std::vector<f32>  sel_depth(static_cast<std::size_t>(kSSAA) * kSSAA * W * H, 0.0f);
    aleph::render::common::Film sel_film{sel_px.data(), kSSAA * W, kSSAA * H, kSSAA * W};
    aleph::render::sw::SceneRT sel_scene;   // rebuilt per frame from the selection

    // Hybrid-mode state. We path-trace progressively (accumulate spp) only after
    // the input has been idle for kIdleMs; any event resets to raster.
    constexpr u32 kIdleMs   = 250;
    constexpr int kMaxSpp   = 256;   // total accumulated samples to converge to
    std::vector<Vec3> accum(static_cast<std::size_t>(W) * H);
    int   pt_samples = 0;            // samples accumulated so far this idle burst
    bool  tracing    = false;

    // ── Crossfade (T3) ───────────────────────────────────────────────────────
    // Blend in LINEAR space across every raster<->path-trace mode switch so the
    // hybrid view never hard-cuts. `presented` always holds the last shown frame
    // (linear); on a mode change we snapshot it into `fade_from` and ramp alpha
    // 0->1 over kFadeMs, presenting lerp(fade_from, src, alpha). Steady-state (no
    // mode change) presents `src` verbatim — crisp + responsive while orbiting;
    // the fade costs only a per-pixel lerp during the ~180ms ramp.
    enum class Mode { Raster, Tracing };
    constexpr u32 kFadeMs = 180;
    std::vector<Vec3> fade_from(static_cast<std::size_t>(W) * H);
    std::vector<Vec3> presented(static_cast<std::size_t>(W) * H);
    Mode prev_mode       = Mode::Raster;
    bool have_prev_frame = false;
    u32  fade_start_ms   = 0;

    bool running   = true;
    bool left_down = false, prev_left = false;
    int  mouse_x = 0, mouse_y = 0;
    u32  last_input_ms = win.ticks_ms();

    // ── Live-orbit view tracking (shell-only throttle) ───────────────────────
    // Orbit/zoom move `cam_` + the raster MVP every frame, but the baked Metal/
    // Dielectric vcol in `sw_` is pinned to the eye at the last bake. On a view
    // gesture mark `view_dirty`; in the raster block, re-bake `sw_` at the live
    // eye at most ~12 Hz (kViewRebakeMs) so chrome/glass track the orbit without
    // a re-bake every frame. Gated by `has_view_dependent_material()` so all-
    // Lambertian (incl. the wave lattice) never re-bakes. `last_rebake_ms = 0`
    // (NOT `now`) so the first orbit re-bakes on the first throttled frame.
    bool          view_dirty     = false;
    u32           last_rebake_ms = 0;
    constexpr u32 kViewRebakeMs  = 80;
    constexpr f32 kNudgeStep     = 0.1f;   // world units per arrow tap
    constexpr f32 kNudgeStepFast = 0.5f;   // with Shift held
    constexpr f32 kNudgeFwdEps   = 1e-5f;  // degenerate (straight-down) camera guard

    // The selected mesh's editable albedo (the UI color sliders bind to this and
    // emit a SetMaterial when changed). Seeded from the picked node's lowered
    // material on each new selection.
    Vec3 sel_albedo{0.8f, 0.8f, 0.8f};

    auto invalidate = [&]() noexcept {
        tracing       = false;
        pt_samples    = 0;
        last_input_ms = win.ticks_ms();
    };

    while (running) {
        std::array<aleph::window::Event, 64> evbuf{};
        const int nev = win.poll_events(std::span<aleph::window::Event>{evbuf});
        bool clicked_pick = false;
        bool key_add_obj = false, key_add_light = false, key_delete = false;
        bool key_kick = false;
        int nudge_dx = 0;   // -1 left, +1 right (camera right axis)
        int nudge_dz = 0;   // -1 toward camera eye, +1 toward look target (camera forward on ground)
        int nudge_dy = 0;   // -1 down, +1 up (world Y)
        bool nudge_fast = false;

        for (std::size_t i = 0; i < static_cast<std::size_t>(nev); ++i) {
            const auto& e = evbuf[i];
            invalidate();  // ANY event cancels the path trace back to raster
            switch (e.kind) {
                case aleph::window::Event::Kind::Quit:
                    running = false; break;
                case aleph::window::Event::Kind::KeyDown:
                    if (e.key == 27 /*ESC*/) running = false;
                    else if (e.key == 'a') key_add_obj = true;
                    else if (e.key == 'l') key_add_light = true;
                    else if (e.key == 'x') key_delete = true;
                    else if (e.key == 'k') key_kick = true;
                    else if (e.key == aleph::window::key::Left)  nudge_dx = -1;
                    else if (e.key == aleph::window::key::Right) nudge_dx = +1;
                    else if (e.key == aleph::window::key::Up)    nudge_dz = +1;
                    else if (e.key == aleph::window::key::Down)  nudge_dz = -1;
                    else if (e.key == 'q') nudge_dy = -1;
                    else if (e.key == 'e') nudge_dy = +1;
                    if (e.shift && (nudge_dx != 0 || nudge_dz != 0 || nudge_dy != 0))
                        nudge_fast = true;
                    break;
                case aleph::window::Event::Kind::MouseDown:
                    if (e.button == 1) { left_down = true; clicked_pick = true; }
                    mouse_x = e.x; mouse_y = e.y;
                    break;
                case aleph::window::Event::Kind::MouseUp:
                    if (e.button == 1) left_down = false;
                    break;
                case aleph::window::Event::Kind::MouseMove:
                    mouse_x = e.x; mouse_y = e.y;
                    if (left_down) {
                        controller.camera().orbit(static_cast<f32>(e.dx),
                                                  static_cast<f32>(e.dy));
                        view_dirty = true;
                    }
                    break;
                case aleph::window::Event::Kind::MouseWheel:
                    controller.camera().zoom(e.wheel > 0 ? (1.0f / 1.12f) : 1.12f);
                    view_dirty = true;
                    break;
                default: break;
            }
        }
        if (!running) break;

        // ── Gestures -> Ops. ──────────────────────────────────────────────────
        if (key_add_obj) {
            aleph::lowering::AddObject add{};
            add.parent   = root;
            add.geometry = aleph::types::SphereLocal{Vec3{0.0f, 0.5f, 0.0f}, 0.5f};
            add.material = lambertian(Vec3{0.3f, 0.6f, 0.9f});
            (void)controller.apply(aleph::lowering::Op{add});
        }
        if (key_add_light) {
            aleph::lowering::AddLight light{};
            light.parent   = root;
            light.emission = Vec3{4.0f, 4.0f, 4.0f};
            light.geometry = aleph::types::QuadLocal{Vec3{-0.5f, 3.0f, -0.5f},
                                                     Vec3{1.0f, 0.0f, 0.0f},
                                                     Vec3{0.0f, 0.0f, 1.0f}};
            (void)controller.apply(aleph::lowering::Op{light});
        }
        if (key_delete && controller.selected().has_value()) {
            const NodeId victim = *controller.selected();
            auto r = controller.apply(
                aleph::lowering::Op{aleph::lowering::DeleteObject{victim}});
            if (r.has_value()) controller.select(std::nullopt);
        }
        // Wave: ping the selected node (or the seed if nothing is selected).
        if (wave_demo && key_kick) {
            (void)controller.kick(controller.selected().value_or(kick_seed), 1.5);
        }

        // Arrow/Q-E nudge: move the selected object along camera-relative ground
        // axes (←/→ = camera right, ↑/↓ = camera forward on XZ) and world Y (Q/E).
        if (controller.selected().has_value()
            && (nudge_dx != 0 || nudge_dz != 0 || nudge_dy != 0)) {
            const Vec3 eye = controller.camera().look_from();
            const Vec3 tgt = controller.camera().look_at();
            Vec3 fwd{tgt.x - eye.x, 0.0f, tgt.z - eye.z};     // project to ground
            const f32 fl = std::sqrt(fwd.x * fwd.x + fwd.z * fwd.z);
            if (fl > kNudgeFwdEps) { fwd.x /= fl; fwd.z /= fl; }
            else                   { fwd = Vec3{0.0f, 0.0f, -1.0f}; } // looking straight down
            const Vec3 right{ -fwd.z, 0.0f, fwd.x };           // cross(fwd, +Y)
            const f32 step = (nudge_fast ? kNudgeStepFast : kNudgeStep);
            const Vec3 delta{
                (right.x * static_cast<f32>(nudge_dx)
                 + fwd.x * static_cast<f32>(nudge_dz)) * step,
                static_cast<f32>(nudge_dy) * step,
                (right.z * static_cast<f32>(nudge_dx)
                 + fwd.z * static_cast<f32>(nudge_dz)) * step};
            (void)controller.translate_selected(delta);
            invalidate();   // keep raster fresh after the nudge (matches SetMaterial)
        }

        // ── Pick on a fresh left-click. ───────────────────────────────────────
        if (clicked_pick && !prev_left) {
            std::optional<NodeId> hit = controller.pick(mouse_x, mouse_y);
            controller.select(hit);
            if (hit.has_value()) {
                // Seed the slider from the picked entity's lowered albedo.
                const std::uint32_t* idx = controller.lowered().handle_map.get(*hit);
                if (idx != nullptr
                    && *idx < controller.lowered().entities.size()) {
                    sel_albedo = controller.lowered().entities[*idx].material.albedo;
                }
            }
        }

        // ── Decide raster vs. progressive path-trace (idle). ──────────────────
        // Whenever idle we show the path trace (accumulating until kMaxSpp, then
        // holding the converged mean — it no longer flips back to raster on
        // convergence); any input pulls us back to raster. Both modes leave a
        // LINEAR image in `src`, which the crossfade block below presents.
        const u32 now = win.ticks_ms();
        // The wave demo stays in raster (φ is colormapped into vcol, which only the
        // rasterizer reads) and steps every frame, so it never goes idle/path-trace.
        const bool idle = !wave_demo && (now - last_input_ms) >= kIdleMs;

        Mode        mode;
        const Vec3* src = nullptr;

        if (idle) {
            mode = Mode::Tracing;
            // Progressive: accumulate a small batch of spp per frame into the
            // running mean. The first pass clears the accumulator.
            if (!tracing) {
                std::fill(accum.begin(), accum.end(), Vec3{});
                pt_samples = 0;
                tracing    = true;
            }
            if (pt_samples < kMaxSpp) {
                constexpr int kBatch = 8;
                const aleph::render::common::Camera pcam =
                    controller.camera().render_camera(W, H);
                // Render one batch into `film`, then fold into the accumulator;
                // the seed varies per batch so successive passes draw new samples.
                clear_sky(film);
                aleph::render::rt::RenderOpts opts{
                    kBatch, 8,
                    aleph::math::u64{42} + static_cast<aleph::math::u64>(pt_samples),
                    32};
                aleph::render::rt::path_trace(controller.render_scene(), pcam, kSky,
                                              film, pool, opts);
                const f32 wbatch = static_cast<f32>(kBatch);
                const f32 prev   = static_cast<f32>(pt_samples);
                const f32 inv    = 1.0f / (prev + wbatch);
                for (std::size_t i = 0; i < accum.size(); ++i) {
                    accum[i] = (accum[i] * prev + film.pixels[i] * wbatch) * inv;
                }
                pt_samples += kBatch;
            }
            src = accum.data();  // no UI overlay while tracing
        } else {
            mode    = Mode::Raster;
            tracing = false;

            // Live-orbit view tracking: re-bake `sw_` at the current eye so Metal/
            // Dielectric vcol follows the orbit. Throttled (~12 Hz) + gated to view-
            // dependent scenes (the wave lattice is all-Lambertian → never fires, so
            // it can't fight `step()`'s per-frame φ rebuild below). Runs before the
            // rasterize so the re-baked `sw_` is what gets drawn this frame.
            if (view_dirty && controller.has_view_dependent_material()
                    && (now - last_rebake_ms) >= kViewRebakeMs) {
                controller.rebake_view_sw();
                last_rebake_ms = now;
                view_dirty     = false;
            }

            // Advance the wave one fixed sub-step (re-bakes φ→vcol) before drawing.
            if (wave_demo) (void)controller.step(kWaveDt);

            // ── RASTER: rasterize the editor's view + UI overlay into `film`. ──
            // Supersample the 3D scene at kSSAA× then box-downsample (linear)
            // into the 1× `film`; the UI overlay below stays at 1×.
            clear_sky(ss_film);
            std::fill(ss_depth.begin(), ss_depth.end(), 0.0f);
            aleph::render::sw::rasterize(controller.raster_scene(),
                                         orbit_mvp(controller.camera(), kSSAA * W, kSSAA * H),
                                         ss_film, ss_depth, pool);

            // Selection silhouette: raster only the selected entity's faces into a
            // zero-cleared depth buffer (coverage), then ring it. X-ray by design
            // (depth starts at 0 => shows through occluders so you never lose the
            // selection). Drawn at SSAA so the downsample anti-aliases the ring.
            if (controller.selected().has_value()) {
                const auto& full = controller.raster_scene();
                const auto& fsrc = controller.face_source();
                const aleph::types::NodeId sel = *controller.selected();
                sel_scene.faces.clear();
                for (std::size_t i = 0; i < full.faces.size() && i < fsrc.size(); ++i) {
                    if (fsrc[i] == sel) {
                        aleph::render::sw::Face f = full.faces[i];
                        f.lightmap_id = 0xFFFFFFFFu;   // drop lightmap (coverage only)
                        sel_scene.faces.push_back(f);
                    }
                }
                if (!sel_scene.faces.empty()) {
                    std::fill(sel_depth.begin(), sel_depth.end(), 0.0f);
                    aleph::render::sw::rasterize(
                        sel_scene, orbit_mvp(controller.camera(), kSSAA * W, kSSAA * H),
                        sel_film, sel_depth, pool);
                    // radius = kSSAA → a ~1-display-pixel ring after the kSSAA
                    // box-downsample.
                    aleph::render::sw::draw_selection_outline(
                        ss_film, sel_depth, /*radius=*/kSSAA, kSelectionColor);
                }
            }
            aleph::render::sw::downsample_box(ss_film, film, kSSAA);

            // UI panel: selection + a material color slider that emits SetMaterial.
            const bool ui_mouse_pressed = left_down && !prev_left;
            aleph::editor::ui_begin(ui, &film, mouse_x, mouse_y, left_down,
                                    ui_mouse_pressed);
            aleph::editor::ui_panel(ui, W - 250, 50, 240, 210, "EDITOR");

            char line[96];
            if (controller.selected().has_value()) {
                std::snprintf(line, sizeof(line), "SELECTED NODE %u",
                              static_cast<unsigned>(controller.selected()->value));
            } else {
                std::snprintf(line, sizeof(line), "NO SELECTION");
            }
            aleph::editor::ui_label(ui, W - 242, 80, line, Vec3{1, 1, 1});

            aleph::editor::ui_label(ui, W - 242, 100, "MATERIAL RGB", Vec3{1, 1, 1});
            const Vec3 before = sel_albedo;
            aleph::editor::ui_slider_f(ui, W - 242, 116, 224, 14, sel_albedo.x, 0.0f, 1.0f);
            aleph::editor::ui_slider_f(ui, W - 242, 134, 224, 14, sel_albedo.y, 0.0f, 1.0f);
            aleph::editor::ui_slider_f(ui, W - 242, 152, 224, 14, sel_albedo.z, 0.0f, 1.0f);
            // A color swatch of the current albedo.
            aleph::editor::draw_rect(film, W - 242, 174, 60, 30, sel_albedo);

            aleph::editor::ui_label(ui, W - 242, 214, "A ADD  L LIGHT", Vec3{0.85f, 0.85f, 0.9f});
            aleph::editor::ui_label(ui, W - 242, 230,
                                    wave_demo ? "K KICK  X DELETE  DRAG ORBIT"
                                              : "X DELETE  DRAG ORBIT",
                                    Vec3{0.85f, 0.85f, 0.9f});
            aleph::editor::ui_end(ui);

            // If the slider moved a real selection's color, emit a SetMaterial Op.
            if ((sel_albedo.x != before.x || sel_albedo.y != before.y ||
                 sel_albedo.z != before.z)
                && controller.selected().has_value()) {
                aleph::lowering::SetMaterial set{};
                set.target = *controller.selected();
                set.params = lambertian(sel_albedo);
                (void)controller.apply(aleph::lowering::Op{set});
                invalidate();
            }

            // HUD.
            char hud[128];
            std::snprintf(hud, sizeof(hud), "ENTITIES %zu  LIGHTS %zu  FACES %zu",
                          controller.lowered().entities.size(),
                          controller.lowered().lights.size(),
                          controller.raster_scene().faces.size());
            aleph::editor::draw_rect(film, 8, 8, 420, 24, Vec3{0, 0, 0});
            aleph::editor::draw_text_shadowed(film, 14, 14, hud, Vec3{1, 1, 1});

            src = film.pixels;  // film stride == W (contiguous)
        }

        // ── Crossfade present (T3): lerp(fade_from, src, alpha) in linear. ────
        // A mode switch snapshots the last shown frame and restarts the ramp;
        // steady-state (alpha>=1) presents `src` verbatim. `presented` records
        // exactly what we show so the next transition fades from the real frame.
        if (have_prev_frame && mode != prev_mode) {
            std::copy(presented.begin(), presented.end(), fade_from.begin());
            fade_start_ms = now;
        }
        const u32 elapsed = now - fade_start_ms;
        const f32 alpha = (!have_prev_frame || elapsed >= kFadeMs)
            ? 1.0f
            : static_cast<f32>(elapsed) / static_cast<f32>(kFadeMs);
        u32* wpx = win.pixels();
        const int wp = win.pitch_pixels();
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const std::size_t i =
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(W)
                    + static_cast<std::size_t>(x);
                const Vec3 lin = (alpha >= 1.0f)
                    ? src[i]
                    : aleph::math::lerp(fade_from[i], src[i], alpha);
                presented[i] = lin;
                wpx[y * wp + x] = aleph::render::common::tonemap_argb8888_gamma2(lin);
            }
        }
        win.present();
        have_prev_frame = true;
        prev_mode  = mode;
        prev_left  = left_down;
    }
    return 0;
}

#endif  // ALEPH_HAVE_SDL2

}  // namespace

int main(int argc, char** argv) {
    // ── Arg parse ─────────────────────────────────────────────────────────────
    //   --headless <outdir>     scripted windowless edit demo (PPM pairs)
    //   --wave <outdir>         headless wave-field capture (deterministic frames)
    //   --orbit-track <outdir>  2-pose live-orbit view-tracking montage (1 PPM)
    //   --wave-live             interactive wave on the lattice (needs SDL2)
    //   (no args)               interactive structural editor (needs SDL2)
    bool wave_live = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--headless") {
            std::string outdir = (i + 1 < argc) ? std::string(argv[i + 1])
                                                : std::string("/tmp/edit_out");
            return run_headless(outdir);
        }
        if (arg == "--wave") {
            std::string outdir = (i + 1 < argc) ? std::string(argv[i + 1])
                                                : std::string("/tmp/wave");
            return run_wave(outdir);
        }
        if (arg == "--orbit-track") {
            std::string outdir = (i + 1 < argc) ? std::string(argv[i + 1])
                                                : std::string("/tmp/orbit_track");
            return run_orbit_track(outdir);
        }
        if (arg == "--wave-live") wave_live = true;
    }

#if defined(ALEPH_HAVE_SDL2)
    return run_live(wave_live);
#else
    (void)wave_live;
    std::fprintf(stderr,
                 "aleph_edit: built without SDL2 — only --headless / --wave / --orbit-track are available\n");
    return 0;
#endif
}

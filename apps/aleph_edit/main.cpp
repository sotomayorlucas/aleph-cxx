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

#include <algorithm>   // std::fill
#include <array>       // std::array (SDL event buffer)
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

// ── The initial scene graph (the truth the controller takes ownership of) ────
//
//   root Transform (identity)
//     ├─Contains─▶ Camera (looks at the origin)
//     ├─Contains─▶ Mesh (SphereLocal) ─References─▶ Material (Lambertian red)
//     ├─Contains─▶ Mesh (QuadLocal floor) ─References─▶ Material (grey)
//     └─Contains─▶ Light (Area, overhead quad)
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

    // A large floor quad in the y=0 plane.
    s.floor = g.alloc_node_id();
    Mesh floor{s.floor, std::string("floor"), 0};
    floor.geometry = QuadLocal{Vec3{-4.0f, 0.0f, -4.0f},
                               Vec3{8.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, 8.0f}};
    g.insert_node(std::move(floor));

    const NodeId floor_mat = g.alloc_node_id();
    Material fmat{floor_mat, MaterialKind::Lambertian};
    fmat.albedo = Vec3{0.6f, 0.6f, 0.62f};
    fmat.emit   = Vec3{0.0f, 0.0f, 0.0f};
    g.insert_node(std::move(fmat));

    // An overhead area light (its own emissive quad geometry).
    const NodeId light_id = g.alloc_node_id();
    Light light{light_id, LightKind::Area, std::string("emit0")};
    light.emission = Vec3{6.0f, 6.0f, 6.0f};
    light.geometry = QuadLocal{Vec3{-1.0f, 3.0f, -1.0f},
                               Vec3{2.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, 2.0f}};
    g.insert_node(std::move(light));

    (void)g.add_edge(EdgeKind::Contains,   s.root, cam_id);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.sphere);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.floor);
    (void)g.add_edge(EdgeKind::Contains,   s.root, light_id);
    (void)g.add_edge(EdgeKind::References, s.sphere, sphere_mat);
    (void)g.add_edge(EdgeKind::References, s.floor,  floor_mat);
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

// `build_sw_scene` emits one PLACEHOLDER `Lightmap{}` per face (its `texels`
// pointer is null) and points every face's `lightmap_id` at it — the bridge
// leaves lightmap baking to the shell (aleph_sw bakes a real lightmap pool).
// The editor's raster view wants FLAT albedo shading (the color the bridge
// already encoded into each face's UVs), so rather than bake a lightmap we hand
// the rasterizer a SceneRT whose faces carry NO lightmap: set every face's
// `lightmap_id = 0xFFFFFFFF`, which `rasterize` reads as "no modulation" (it
// draws the face's `tex(u,v)` directly). Without this, the rasterizer would
// dereference the placeholder lightmap's null `texels` and crash. We work on a
// COPY because `controller.raster_scene()` is const (the controller owns the
// truth-derived SceneRT); the copy is cheap (a few hundred small faces).
aleph::render::sw::SceneRT
flat_raster(const aleph::render::sw::SceneRT& src) {
    aleph::render::sw::SceneRT out = src;
    for (auto& f : out.faces) {
        f.lightmap_id = 0xFFFFFFFFu;  // sentinel: no lightmap (flat albedo)
    }
    return out;
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

    aleph::threads::Pool pool(thread_count());

    // Film + depth buffers (heap — W*H*sizeof(Vec3) is modest).
    std::vector<Vec3> film_px(static_cast<std::size_t>(W) * H);
    aleph::render::common::Film film{film_px.data(), W, H, W};
    std::vector<f32> depth(static_cast<std::size_t>(W) * H, 1.0f);

    // Render the controller's current state to two PPMs (raster + path-trace).
    int step_no = 0;
    auto dump = [&](const char* label) -> bool {
        // (1) Raster composite.
        clear_sky(film);
        std::fill(depth.begin(), depth.end(), 1.0f);
        const aleph::render::sw::SceneRT rsr = flat_raster(controller.raster_scene());
        aleph::render::sw::rasterize(rsr, orbit_mvp(controller.camera(), W, H),
                                     film, depth, pool);
        std::string rp = outdir + "/step" + std::to_string(step_no) + "_" + label
                       + "_raster.ppm";
        if (!write_ppm(rp.c_str(), film)) {
            std::fprintf(stderr, "aleph_edit: cannot write %s\n", rp.c_str());
            return false;
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
        add.geometry = aleph::types::SphereLocal{Vec3{1.5f, 0.5f, 0.0f}, 0.5f};
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

#if defined(ALEPH_HAVE_SDL2)

// ── Live SDL hybrid shell (SPEC §3.3) ────────────────────────────────────────
// Manual-smoke only (not auto-tested). Drives the headless controller from real
// input: orbit drag -> raster; left-click -> pick/select; UI material slider ->
// SetMaterial; keys a/l/x -> Add/Add/Delete; idle -> progressive path-trace.
int run_live() {
    constexpr int W = 800, H = 600;
    aleph::window::Window win(W, H, "aleph_edit — structural editor");

    InitialScene init = build_initial_graph();
    const NodeId root = init.root;
    aleph::edit::EditorController controller{std::move(init.g)};
    controller.set_viewport(W, H);
    auto& cam = controller.camera();
    cam.target   = Vec3{0.0f, 0.5f, 0.0f};
    cam.yaw      = 0.4f;
    cam.pitch    = 0.25f;
    cam.radius   = 5.0f;
    cam.vfov_deg = 45.0f;

    aleph::threads::Pool pool(thread_count());
    aleph::editor::UiCtx ui{};

    std::vector<Vec3> film_px(static_cast<std::size_t>(W) * H);
    aleph::render::common::Film film{film_px.data(), W, H, W};
    std::vector<f32> depth(static_cast<std::size_t>(W) * H, 1.0f);

    // Hybrid-mode state. We path-trace progressively (accumulate spp) only after
    // the input has been idle for kIdleMs; any event resets to raster.
    constexpr u32 kIdleMs   = 250;
    constexpr int kMaxSpp   = 256;   // total accumulated samples to converge to
    std::vector<Vec3> accum(static_cast<std::size_t>(W) * H);
    int   pt_samples = 0;            // samples accumulated so far this idle burst
    bool  tracing    = false;

    bool running   = true;
    bool left_down = false, prev_left = false;
    int  mouse_x = 0, mouse_y = 0;
    u32  last_input_ms = win.ticks_ms();

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
                    }
                    break;
                case aleph::window::Event::Kind::MouseWheel:
                    controller.camera().zoom(e.wheel > 0 ? (1.0f / 1.12f) : 1.12f);
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
        const u32 now = win.ticks_ms();
        const bool idle = (now - last_input_ms) >= kIdleMs;

        if (idle && pt_samples < kMaxSpp) {
            // Progressive: accumulate a small batch of spp per frame and blit the
            // running mean. The first pass clears the accumulator + sky.
            constexpr int kBatch = 8;
            if (!tracing) {
                std::fill(accum.begin(), accum.end(), Vec3{});
                pt_samples = 0;
                tracing    = true;
            }
            const aleph::render::common::Camera pcam =
                controller.camera().render_camera(W, H);
            // Render one batch into `film`, then fold into the accumulator. The
            // seed varies per batch so successive passes draw new samples.
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
            // Blit the converged mean to the window (no UI overlay while tracing).
            u32* wpx = win.pixels();
            const int wp = win.pitch_pixels();
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    wpx[y * wp + x] = aleph::render::common::tonemap_argb8888_gamma2(
                        accum[static_cast<std::size_t>(y) * static_cast<std::size_t>(W)
                              + static_cast<std::size_t>(x)]);
                }
            }
            win.present();
            prev_left = left_down;
            continue;
        }

        // ── RASTER mode: rasterize the editor's view + UI overlay. ────────────
        clear_sky(film);
        std::fill(depth.begin(), depth.end(), 1.0f);
        const aleph::render::sw::SceneRT rsr = flat_raster(controller.raster_scene());
        aleph::render::sw::rasterize(rsr, orbit_mvp(controller.camera(), W, H),
                                     film, depth, pool);

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
        aleph::editor::ui_label(ui, W - 242, 230, "X DELETE  DRAG ORBIT",
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

        // Tonemap into the window.
        u32* wpx = win.pixels();
        const int wp = win.pitch_pixels();
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const Vec3 lin = film.pixels[y * film.stride_pixels + x];
                wpx[y * wp + x] = aleph::render::common::tonemap_argb8888_gamma2(lin);
            }
        }
        win.present();
        prev_left = left_down;
    }
    return 0;
}

#endif  // ALEPH_HAVE_SDL2

}  // namespace

int main(int argc, char** argv) {
    // ── Arg parse: --headless <outdir> runs the scripted, windowless mode. ────
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--headless") {
            std::string outdir = (i + 1 < argc) ? std::string(argv[i + 1])
                                                : std::string("/tmp/edit_out");
            return run_headless(outdir);
        }
    }

#if defined(ALEPH_HAVE_SDL2)
    return run_live();
#else
    std::fprintf(stderr,
                 "aleph_edit: built without SDL2 — only --headless is available\n");
    return 0;
#endif
}

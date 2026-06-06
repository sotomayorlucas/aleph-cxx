// apps/aleph_lower_demo — Phase 5 end-to-end smoke (SPEC §8 item 9 / §9) plus
// the Phase 5.x-a light-grouping smoke (SPEC §5 test 6).
//
//   GraphScene ──lower()──▶ LoweredScene ──build──▶ RenderScene ──path_trace──▶ PPM
//
// The typed scene graph is the single source of truth (SPEC §1). This demo
// authors a small *enriched* graph (root Transform -Contains-> {Camera, Mesh,
// Light}, with the Mesh -References-> a Lambertian red Material), runs the
// deterministic lowering functor, translates the frozen semantic IR into the
// renderer's SoA `Scene`, path-traces it, and writes a PPM. It then prints the
// entity / light counts so the loop is observable.
//
// Determinism (SPEC §7): the LoweredScene is byte-identical for a given graph
// (insertion order, OrderedMap, f32). The render seed is fixed so the PPM is
// reproducible.
//
// ── Light-grouping mode (Phase 5.x-a, SPEC §5 test 6) ────────────────────────
// An optional flag — argv "grouped" or "--grouped" (in any position) — switches
// the demo to a MULTI-light grouped scene: several area lights, some sharing the
// mesh region they Influence so the VisibilitySheaf H⁰ grouping is NON-trivial
// (more than one group, at least one group with >1 light). That path uses the
// REAL `aleph::lowering::build_render_scene`, which bakes the IR's H⁰ light
// groups into `Scene::light_groups`; rendering enables `RenderOpts.grouped_nee`
// so the renderer reads that table (SPEC §4.2/§4.3). render.rt never imports
// `aleph.sheaf` — it only sees the plain `scene.light_groups` field.
//
// NOTE on `build_render_scene`: the DEFAULT (no-flag) path keeps the original
// thin local translation (`build_render_scene_local`) verbatim so it stays
// byte-stable. The grouped path needs `Scene::light_groups`, which only the
// shipped `aleph.lowering:build` partition populates, so it calls that instead.
//
// ── Adaptive-spp mode (Phase 5.x-b, SPEC §5 test 7) ──────────────────────────
// An optional flag — argv "adaptive" or "--adaptive" (in any position) —
// switches the demo to a MULTI-mesh scene wired by `Adjacent` edges, so the
// flow layer's Ollivier-Ricci curvature varies across meshes and the lowering
// bakes a NON-uniform per-primitive importance onto the `Scene` (SPEC §4.1/§4.2).
// That path uses the REAL `aleph::lowering::build_render_scene` (the only
// surface that bakes `Scene::{sphere,quad,tri}_importance`), and rendering
// enables `RenderOpts.adaptive_spp` so the integrator spends more samples where
// high-importance primitives are visible (SPEC §4.3). render.rt and aleph.scene
// never import `aleph.flow`: the importance reaches the renderer ONLY as the
// plain f32 array the lowering baked onto the Scene (SPEC §1 boundary).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <variant>

import aleph.math;
import aleph.types;
import aleph.graph;
import aleph.lowering;
import aleph.scene;
import aleph.render.rt;
import aleph.render.common;
import aleph.alloc;
import aleph.threads;

namespace {

using aleph::math::Vec3;
using aleph::math::Mat4;
using aleph::math::f32;
using aleph::math::u64;

// ── Author the canonical enriched scene graph (SPEC §8 item 1 shape) ─────────
//
//   root Transform (identity)
//     ├─Contains─▶ Camera (concrete pose)
//     ├─Contains─▶ Mesh (SphereLocal) ─References─▶ Material (Lambertian red)
//     └─Contains─▶ Light (Area, its own emissive geometry)
//
// The Mesh carries a *local* analytic sphere; the world transform is applied at
// lowering time. The Light is its OWN node (emission + geometry), per the §3
// light-table policy — not modeled as an emissive Mesh.
aleph::graph::Graph build_enriched_graph() {
    using namespace aleph::types;
    aleph::graph::Graph g;

    // Root Transform at identity — no incoming Contains ⇒ a lowering root.
    const NodeId root = g.alloc_node_id();
    g.insert_node(Transform{root, 0, LocalTransform{Mat4::identity()}});

    // Camera with a concrete pose looking at the origin.
    const NodeId cam_id = g.alloc_node_id();
    Camera cam{cam_id, std::string("sensor0")};
    cam.look_from  = Vec3{0.0f, 1.0f, 4.0f};
    cam.look_at    = Vec3{0.0f, 0.5f, 0.0f};
    cam.up         = Vec3{0.0f, 1.0f, 0.0f};
    cam.vfov_deg   = 45.0f;
    cam.aperture   = 0.0f;
    cam.focus_dist = 4.0f;
    g.insert_node(std::move(cam));

    // Mesh carrying an analytic sphere payload in LOCAL space.
    const NodeId mesh_id = g.alloc_node_id();
    Mesh mesh{mesh_id, std::string("sphere"), 0};
    mesh.geometry = SphereLocal{Vec3{0.0f, 0.5f, 0.0f}, 0.5f};
    g.insert_node(std::move(mesh));

    // Lambertian RED material (not emissive ⇒ stays out of the light table).
    const NodeId mat_id = g.alloc_node_id();
    Material mat{mat_id, MaterialKind::Lambertian};
    mat.albedo = Vec3{0.85f, 0.10f, 0.10f};
    mat.emit   = Vec3{0.0f, 0.0f, 0.0f};
    g.insert_node(std::move(mat));

    // An explicit Light node — area light hovering above, its own geometry.
    const NodeId light_id = g.alloc_node_id();
    Light light{light_id, LightKind::Area, std::string("emit0")};
    light.emission = Vec3{6.0f, 6.0f, 6.0f};
    light.geometry = QuadLocal{Vec3{-1.0f, 2.0f, -1.0f}, Vec3{2.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, 2.0f}};
    g.insert_node(std::move(light));

    // Hierarchy: root Contains the camera, the mesh and the light.
    (void)g.add_edge(EdgeKind::Contains,   root, cam_id);
    (void)g.add_edge(EdgeKind::Contains,   root, mesh_id);
    (void)g.add_edge(EdgeKind::Contains,   root, light_id);
    // The Mesh references its Material — must resolve, else DanglingReference.
    (void)g.add_edge(EdgeKind::References, mesh_id, mat_id);
    return g;
}

// ── Author a MULTI-light grouped scene (SPEC §5 test 6 / §4.1) ───────────────
//
// A fully lowerable scene whose VisibilitySheaf H⁰ grouping is NON-trivial:
//
//   root Transform (identity)
//     ├─Contains─▶ Camera
//     ├─Contains─▶ Mesh ma ─References─▶ Material (Lambertian grey)
//     ├─Contains─▶ Mesh mb ─References─▶ Material (Lambertian grey)
//     ├─Contains─▶ Light la   ─Influences─▶ ma         (group {la})
//     ├─Contains─▶ Light lb0  ─Influences─▶ mb     ┐
//     ├─Contains─▶ Light lb1  ─Influences─▶ mb     ┘    (group {lb0, lb1})
//     └─Contains─▶ Light lone (no Influences edge)      (group {lone})
//
// ma and mb are disjoint mesh regions (no Adjacent edge), so la's region and
// the mb region are SEPARATE H⁰ components ⇒ separate groups; lb0 and lb1 both
// Influence the SAME mesh mb, so they MERGE into one group; lone Influences
// nothing, so it is its own singleton. Result: three groups, one of size two —
// a grouping the path tracer's group-stratified NEE can act on.
aleph::graph::Graph build_grouped_graph() {
    using namespace aleph::types;
    aleph::graph::Graph g;

    const NodeId root = g.alloc_node_id();
    g.insert_node(Transform{root, 0, LocalTransform{Mat4::identity()}});

    const NodeId cam_id = g.alloc_node_id();
    Camera cam{cam_id, std::string("sensor0")};
    cam.look_from  = Vec3{0.0f, 1.5f, 6.0f};
    cam.look_at    = Vec3{0.0f, 0.5f, 0.0f};
    cam.up         = Vec3{0.0f, 1.0f, 0.0f};
    cam.vfov_deg   = 50.0f;
    cam.aperture   = 0.0f;
    cam.focus_dist = 6.0f;
    g.insert_node(std::move(cam));

    // Two disjoint analytic spheres (no Adjacent edge between them).
    const NodeId ma_id = g.alloc_node_id();
    Mesh ma{ma_id, std::string("sphere_a"), 0};
    ma.geometry = SphereLocal{Vec3{-1.5f, 0.5f, 0.0f}, 0.5f};
    g.insert_node(std::move(ma));

    const NodeId mb_id = g.alloc_node_id();
    Mesh mb{mb_id, std::string("sphere_b"), 0};
    mb.geometry = SphereLocal{Vec3{1.5f, 0.5f, 0.0f}, 0.5f};
    g.insert_node(std::move(mb));

    // One shared Lambertian grey material (non-emissive ⇒ stays out of the
    // light table). Both meshes reference it.
    const NodeId mat_id = g.alloc_node_id();
    Material mat{mat_id, MaterialKind::Lambertian};
    mat.albedo = Vec3{0.70f, 0.70f, 0.72f};
    mat.emit   = Vec3{0.0f, 0.0f, 0.0f};
    g.insert_node(std::move(mat));

    // Four standalone area Lights, each its OWN emissive quad geometry.
    auto add_area_light = [&](const char* name, Vec3 anchor) -> NodeId {
        const NodeId id = g.alloc_node_id();
        Light l{id, LightKind::Area, std::string(name)};
        l.emission = Vec3{6.0f, 6.0f, 6.0f};
        l.geometry = QuadLocal{anchor, Vec3{0.6f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.6f}};
        g.insert_node(std::move(l));
        return id;
    };
    const NodeId la   = add_area_light("la",   Vec3{-1.8f, 2.5f, 0.0f});
    const NodeId lb0  = add_area_light("lb0",  Vec3{1.2f, 2.5f, 0.0f});
    const NodeId lb1  = add_area_light("lb1",  Vec3{1.8f, 2.5f, 0.0f});
    const NodeId lone = add_area_light("lone", Vec3{0.0f, 3.0f, -1.5f});

    // Hierarchy: root Contains the camera, both meshes and all four lights.
    (void)g.add_edge(EdgeKind::Contains, root, cam_id);
    (void)g.add_edge(EdgeKind::Contains, root, ma_id);
    (void)g.add_edge(EdgeKind::Contains, root, mb_id);
    (void)g.add_edge(EdgeKind::Contains, root, la);
    (void)g.add_edge(EdgeKind::Contains, root, lb0);
    (void)g.add_edge(EdgeKind::Contains, root, lb1);
    (void)g.add_edge(EdgeKind::Contains, root, lone);

    // Material resolution for both meshes (else DanglingReference).
    (void)g.add_edge(EdgeKind::References, ma_id, mat_id);
    (void)g.add_edge(EdgeKind::References, mb_id, mat_id);

    // Influences edges drive the H⁰ grouping (SPEC §4.1):
    //   la->ma  => group {la}; lb0->mb, lb1->mb => group {lb0,lb1};
    //   lone has NO Influences edge => singleton {lone}.
    (void)g.add_edge(EdgeKind::Influences, la,  ma_id);
    (void)g.add_edge(EdgeKind::Influences, lb0, mb_id);
    (void)g.add_edge(EdgeKind::Influences, lb1, mb_id);
    return g;
}

// ── Author a MULTI-mesh ADAPTIVE scene (Phase 5.x-b, SPEC §5 test 7 / §4.1) ──
//
// A fully lowerable scene whose mesh `Adjacent` 1-skeleton has VARYING
// Ollivier-Ricci curvature, so the lowering bakes a NON-uniform per-primitive
// importance onto the Scene (the precondition for the adaptive-spp path to
// allocate samples differently across the image):
//
//   root Transform (identity)
//     ├─Contains─▶ Camera
//     ├─Contains─▶ Mesh m0 ─References─▶ Material (Lambertian grey)
//     ├─Contains─▶ Mesh m1 ─References─▶ Material
//     ├─Contains─▶ Mesh m2 ─References─▶ Material
//     ├─Contains─▶ Mesh m3 ─References─▶ Material   (tail)
//     ├─Contains─▶ Light (area, overhead)
//     │
//     ├─Adjacent─▶ m0–m1, m1–m2, m2–m0   (a 3-clique: high positive curvature)
//     └─Adjacent─▶ m0–m3                  (a bridge to a pendant: low curvature)
//
// The triangle {m0,m1,m2} is a clique — its edges have high Ollivier-Ricci
// curvature; the single bridge m0–m3 to the pendant m3 has markedly lower
// (here negative) curvature. After the lowering's per-Mesh mean + min-max
// normalize, the meshes land at distinct importances in [0,1] (m1/m2 high near
// the clique, the pendant m3 low), so the baked `Scene` importance is non-trivial
// and the adaptive sampler has something to act on. `aleph.flow` is never touched
// here — only `aleph.lowering` (inside `lower()`/`build_render_scene`) links it.
//
// The spheres are spread across the frame so the high- and low-importance
// primitives occupy different image regions, the way the §5 test-3 mechanism
// expects: high-importance pixels draw `max_spp_scale×` the samples of the rest.
aleph::graph::Graph build_adaptive_graph() {
    using namespace aleph::types;
    aleph::graph::Graph g;

    const NodeId root = g.alloc_node_id();
    g.insert_node(Transform{root, 0, LocalTransform{Mat4::identity()}});

    const NodeId cam_id = g.alloc_node_id();
    Camera cam{cam_id, std::string("sensor0")};
    cam.look_from  = Vec3{0.0f, 1.0f, 7.0f};
    cam.look_at    = Vec3{0.0f, 0.5f, 0.0f};
    cam.up         = Vec3{0.0f, 1.0f, 0.0f};
    cam.vfov_deg   = 55.0f;
    cam.aperture   = 0.0f;
    cam.focus_dist = 7.0f;
    g.insert_node(std::move(cam));

    // One shared Lambertian grey material (non-emissive ⇒ out of the light
    // table). Every mesh references it.
    const NodeId mat_id = g.alloc_node_id();
    Material mat{mat_id, MaterialKind::Lambertian};
    mat.albedo = Vec3{0.72f, 0.72f, 0.74f};
    mat.emit   = Vec3{0.0f, 0.0f, 0.0f};
    g.insert_node(std::move(mat));

    // Four analytic-sphere meshes spread across the frame. The fourth (m3) is
    // the pendant attached by a single Adjacent bridge.
    auto add_mesh = [&](const char* name, Vec3 center) -> NodeId {
        const NodeId id = g.alloc_node_id();
        Mesh m{id, std::string(name), 0};
        m.geometry = SphereLocal{center, 0.5f};
        g.insert_node(std::move(m));
        (void)g.add_edge(EdgeKind::References, id, mat_id);
        return id;
    };
    const NodeId m0 = add_mesh("m0", Vec3{-1.8f, 0.5f, 0.0f});
    const NodeId m1 = add_mesh("m1", Vec3{-0.6f, 0.5f, 0.0f});
    const NodeId m2 = add_mesh("m2", Vec3{ 0.6f, 0.5f, 0.0f});
    const NodeId m3 = add_mesh("m3", Vec3{ 1.8f, 0.5f, 0.0f});

    // A single overhead area light so the meshes are lit (its own emissive quad).
    const NodeId light_id = g.alloc_node_id();
    Light light{light_id, LightKind::Area, std::string("emit0")};
    light.emission = Vec3{6.0f, 6.0f, 6.0f};
    light.geometry = QuadLocal{Vec3{-1.5f, 3.0f, -1.0f}, Vec3{3.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, 2.0f}};
    g.insert_node(std::move(light));

    // Hierarchy: root Contains the camera, all meshes and the light.
    (void)g.add_edge(EdgeKind::Contains, root, cam_id);
    (void)g.add_edge(EdgeKind::Contains, root, m0);
    (void)g.add_edge(EdgeKind::Contains, root, m1);
    (void)g.add_edge(EdgeKind::Contains, root, m2);
    (void)g.add_edge(EdgeKind::Contains, root, m3);
    (void)g.add_edge(EdgeKind::Contains, root, light_id);

    // Mesh `Adjacent` 1-skeleton (the ONLY input flow's Ricci sees): a 3-clique
    // {m0,m1,m2} plus a bridge m0–m3 to the pendant m3. The clique edges carry
    // high curvature, the bridge low — so per-mesh mean curvature, and hence the
    // normalized importance, VARIES across meshes (SPEC §4.1).
    (void)g.add_edge(EdgeKind::Adjacent, m0, m1);
    (void)g.add_edge(EdgeKind::Adjacent, m1, m2);
    (void)g.add_edge(EdgeKind::Adjacent, m2, m0);
    (void)g.add_edge(EdgeKind::Adjacent, m0, m3);
    return g;
}

// ── A scene material handle from normalized IR params (SPEC §4.3 dispatch) ───
aleph::scene::MaterialHandle add_material(aleph::scene::Scene& s,
                                          const aleph::lowering::MaterialParams& m) {
    switch (m.kind) {
        case aleph::types::MaterialKind::Lambertian:
            return aleph::scene::scene_add_lambertian(s, m.albedo);
        case aleph::types::MaterialKind::Metal:
            return aleph::scene::scene_add_metal(s, m.albedo, m.fuzz);
        case aleph::types::MaterialKind::Dielectric:
            return aleph::scene::scene_add_dielectric(s, m.ior);
        case aleph::types::MaterialKind::Emissive:
            return aleph::scene::scene_add_emissive(s, m.emit);
        case aleph::types::MaterialKind::TexturedLambertian:
            return aleph::scene::scene_add_textured_lambertian(s, m.albedo, {m.uv_scale, m.uv_scale});
    }
    return aleph::scene::scene_add_lambertian(s, m.albedo);
}

// Add one world-space LoweredEntity's geometry to the Scene under `mat`.
void add_entity_geometry(aleph::scene::Scene& s,
                         const aleph::lowering::LoweredEntity& e,
                         aleph::scene::MaterialHandle mat) {
    std::visit(
        [&](const auto& geom) {
            using T = std::decay_t<decltype(geom)>;
            if constexpr (std::is_same_v<T, aleph::types::SphereLocal>) {
                (void)aleph::scene::scene_add_sphere(s, geom.center, geom.radius, mat);
            } else if constexpr (std::is_same_v<T, aleph::types::QuadLocal>) {
                (void)aleph::scene::scene_add_quad(s, geom.q, geom.u, geom.v, mat);
            } else {  // aleph::types::TriLocal
                (void)aleph::scene::scene_add_tri(s, geom.a, geom.b, geom.c, mat);
            }
        },
        e.world_geometry);
}

// ── build_render_scene (SPEC §4.3): thin, decision-free IR → Scene ───────────
// Per entity: add geometry + its resolved material. Per light: add geometry +
// an emissive material (the renderer's light list is populated by
// `scene_add_*` when the material kind is Emissive). Then build the BVH. No
// semantic decisions are taken here — every decision was made by `lower()`.
aleph::scene::Scene build_render_scene_local(const aleph::lowering::LoweredScene& ls,
                                             aleph::alloc::Arena& bvh_arena) {
    aleph::scene::Scene scene;

    for (const auto& e : ls.entities) {
        const aleph::scene::MaterialHandle mat = add_material(scene, e.material);
        add_entity_geometry(scene, e, mat);
    }
    for (const auto& l : ls.lights) {
        const aleph::scene::MaterialHandle mat = add_material(scene, l.material);
        add_entity_geometry(scene, l, mat);
    }

    aleph::scene::scene_build_bvh(scene, bvh_arena);
    return scene;
}

}  // namespace

int main(int argc, char** argv) {
    // ── 0. Parse args: optional mode flags ("grouped"/"--grouped" or
    // "adaptive"/"--adaptive") in any position, plus an optional output path (the
    // first non-flag arg). The two modes are mutually exclusive; if both are
    // given, "adaptive" wins (the multi-mesh Ricci path is the broader scene). ──
    bool grouped  = false;
    bool adaptive = false;
    std::string out_path;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "grouped" || arg == "--grouped") {
            grouped = true;
        } else if (arg == "adaptive" || arg == "--adaptive") {
            adaptive = true;
        } else if (out_path.empty()) {
            out_path = std::string(arg);
        }
    }
    if (adaptive) grouped = false;  // adaptive takes precedence over grouped
    const char* mode = adaptive ? "adaptive" : (grouped ? "grouped" : "default");
    if (out_path.empty()) {
        out_path = std::string("/tmp/aleph_lower_demo_") + mode + ".ppm";
        if (!adaptive && !grouped) out_path = "/tmp/aleph_lower_demo.ppm";
    }

    // ── 1. The graph is the truth. ───────────────────────────────────────────
    // Default: the single-light enriched scene. With --grouped: a multi-light
    // scene whose H⁰ light grouping is non-trivial (SPEC §5 test 6). With
    // --adaptive: a multi-mesh scene wired by `Adjacent` edges whose Ricci
    // curvature — and thus the baked per-primitive importance — VARIES across
    // meshes (SPEC §5 test 7 / §4.1).
    const aleph::graph::Graph g = adaptive ? build_adaptive_graph()
                                : grouped  ? build_grouped_graph()
                                           : build_enriched_graph();

    // ── 2. Lower: GraphScene → LoweredScene (frozen semantic IR). ─────────────
    auto lowered = aleph::lowering::lower(g);
    if (!lowered.has_value()) {
        const char* why = "unknown";
        switch (lowered.error()) {
            case aleph::lowering::LowerError::NoCamera:          why = "NoCamera"; break;
            case aleph::lowering::LowerError::DanglingReference: why = "DanglingReference"; break;
            case aleph::lowering::LowerError::InvalidHierarchy:  why = "InvalidHierarchy"; break;
        }
        std::fprintf(stderr, "aleph_lower_demo: lower() failed: %s\n", why);
        return 1;
    }
    const aleph::lowering::LoweredScene& ls = *lowered;

    // ── 3. Build the RenderScene (SoA + BVH). ─────────────────────────────────
    // Default path: the original thin local translation (byte-stable). Grouped
    // and adaptive paths: the shipped `aleph.lowering:build`, which additionally
    // bakes the IR's data render.rt reads — the H⁰ light groups into
    // `Scene::light_groups` (SPEC §4.2, grouped) and the per-entity Ricci
    // importance into `Scene::{sphere,quad,tri}_importance` (SPEC §4.2, adaptive).
    // `build_render_scene_local` does NOT bake importance, so the adaptive path
    // MUST use the shipped builder for the sampler to see non-uniform importance.
    static unsigned char bvh_scratch[1 << 20];
    aleph::alloc::Arena bvh_arena{bvh_scratch, sizeof(bvh_scratch)};
    aleph::scene::Scene scene =
        (grouped || adaptive) ? aleph::lowering::build_render_scene(ls).scene
                              : build_render_scene_local(ls, bvh_arena);

    // ── 4. Path-trace into a film. ────────────────────────────────────────────
    constexpr int W = 320, H = 240;
    alignas(64) static unsigned char film_scratch[W * H * sizeof(Vec3)];
    aleph::alloc::Arena film_arena{film_scratch, sizeof(film_scratch)};
    aleph::render::common::Film film = aleph::render::common::film_alloc(film_arena, W, H);

    const aleph::render::common::Camera cam = aleph::render::common::make_camera(
        ls.camera.look_from, ls.camera.look_at, ls.camera.up,
        ls.camera.vfov_deg, W, H, ls.camera.aperture, ls.camera.focus_dist);

    const aleph::render::common::Sky sky{Vec3{0.02f, 0.02f, 0.03f},
                                         Vec3{0.20f, 0.30f, 0.45f}};

    int threads = static_cast<int>(std::thread::hardware_concurrency());
    if (threads <= 0) threads = 1;
    aleph::threads::Pool pool(threads);
    // grouped_nee opts into group-stratified NEE off `scene.light_groups`;
    // adaptive_spp opts into the importance-weighted sample loop off the baked
    // `Scene` importance. Both default false so the default render is unchanged
    // (SPEC §4.3 / §5 test 4).
    aleph::render::rt::RenderOpts opts{32, 8, 42ull, 32};
    opts.grouped_nee  = grouped;
    opts.adaptive_spp = adaptive;
    opts.max_spp_scale = 4;
    aleph::render::rt::path_trace(scene, cam, sky, film, pool, opts);

    // ── 5. Write the PPM (gamma 2.0 via byte_from_linear), like aleph_rt. ─────
    std::FILE* f = std::fopen(out_path.c_str(), "wb");
    if (f == nullptr) {
        std::fprintf(stderr, "aleph_lower_demo: cannot open %s for writing\n",
                     out_path.c_str());
        return 1;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
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

    // ── 6. Report the loop's shape (entity / light / group counts). ───────────
    std::printf("aleph_lower_demo: mode=%s entities=%zu lights=%zu handles=%zu "
                "light_groups=%zu scene_groups=%zu grouped_nee=%d "
                "adaptive_spp=%d max_spp_scale=%d -> %s (%dx%d)\n",
                mode,
                ls.entities.size(), ls.lights.size(), ls.handle_map.size(),
                ls.light_groups.size(), scene.light_groups.size(),
                opts.grouped_nee ? 1 : 0, opts.adaptive_spp ? 1 : 0,
                opts.max_spp_scale, out_path.c_str(), W, H);
    return 0;
}

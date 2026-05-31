// apps/aleph_lower_demo — Phase 5 end-to-end smoke (SPEC §8 item 9 / §9).
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
// NOTE on `build_render_scene`: SPEC §4.3 sanctions that translation inside
// `aleph.lowering:build`. That partition is still a W3 stub, so this demo
// performs the *same* thin, decision-free translation locally (dispatch on the
// `GeometryPayload` variant + `MaterialKind`, forward to the matching
// `scene_add_*`, then build the BVH). When `:build` lands, the body of
// `build_render_scene_local` is its implementation verbatim.

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
    const std::string out_path =
        argc > 1 ? std::string(argv[1]) : std::string("/tmp/aleph_lower_demo.ppm");

    // ── 1. The graph is the truth. ───────────────────────────────────────────
    const aleph::graph::Graph g = build_enriched_graph();

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
    static unsigned char bvh_scratch[1 << 20];
    aleph::alloc::Arena bvh_arena{bvh_scratch, sizeof(bvh_scratch)};
    aleph::scene::Scene scene = build_render_scene_local(ls, bvh_arena);

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
    aleph::render::rt::path_trace(scene, cam, sky, film, pool,
        aleph::render::rt::RenderOpts{32, 8, 42ull, 32});

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

    // ── 6. Report the loop's shape (entity / light counts). ───────────────────
    std::printf("aleph_lower_demo: entities=%zu lights=%zu handles=%zu -> %s (%dx%d)\n",
                ls.entities.size(), ls.lights.size(), ls.handle_map.size(),
                out_path.c_str(), W, H);
    return 0;
}

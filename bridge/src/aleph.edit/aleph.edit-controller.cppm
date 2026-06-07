// aleph.edit:controller — the headless, testable core of the structural editor
// (Phase 6, SPEC §3.2).
//
// `EditorController` owns the typed scene graph (the SINGLE SOURCE OF TRUTH),
// lowers it, builds BOTH backends — the software-rasterizer `SceneRT` (with a
// face→entity map) and the path-tracer `scene::Scene` (with an entity→primitive
// map) — and turns editor gestures into the engine loop:
//
//   pick(px,py,w,h) -> camera ray -> nearest SceneRT face -> face_source -> NodeId
//   apply(Op)       -> apply_op(graph) + lower_incremental(prev,…) + rebuild
//   camera()        -> a small OWN orbit camera (look_from/look_at + orbit/zoom)
//
// ── HEADLESS (SPEC §3, ARCHITECTURE) ────────────────────────────────────────
// `aleph.edit` is the sanctioned cross-cutter: it may import every backend
// (render.sw / render.rt / scene) plus lowering / graph / dpo. It MUST NOT
// import `aleph.window` or `aleph.editor` — those pull SDL — so it stays pure
// logic and CI can drive it without a window. The pick is therefore the
// controller's OWN ray-vs-SceneRT-face test (using `build_sw_scene`'s
// `face_source`), and the camera is the controller's OWN small orbit camera —
// neither leans on `aleph.editor`'s SDL-coupled pick/orbit helpers.
//
// ── DETERMINISM / no-exceptions (SPEC §7, aleph_flags_isa) ──────────────────
// The graph is the truth; every derived structure (LoweredScene, SceneRT,
// Scene, the maps) is rebuilt from it by the same total, byte-deterministic
// lowering used everywhere else. `apply` is the only fallible surface and it
// returns `std::expected<void, OpError>` — never throws. A failed `apply_op`
// leaves the graph (and thus every derived structure) byte-for-byte unchanged.

module;
#include <algorithm>  // std::clamp
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

export module aleph.edit:controller;

import aleph.graph;           // Graph (the truth)
import aleph.types;           // NodeId, EdgeId
import aleph.containers;      // FlatSet (decompose_rewrite preserved set)
import aleph.math;            // Vec3, Mat4, f32, normalize/cross/dot
import aleph.lowering;        // lower / lower_incremental / Op / OpError / apply_op /
                              // LoweredScene / build_render_scene / build_sw_scene / SwBuild
import aleph.render.sw;       // SceneRT, Face
import aleph.render.common;   // Camera, make_camera
import aleph.scene;           // Scene
import aleph.dpo;             // RewriteRecord
import aleph.sheaf;           // OneSkeleton, decompose_rewrite (R/U/K cover)
import aleph.flow;            // WeightedLaplacian, build_laplacian{,_bounded,_local}, default_weight (Δ)
import aleph.sim;             // Section<f64>, WaveStepper, StepError (φ wave field)

export namespace aleph::edit {

// A small OWN orbit camera (SPEC §3.2). It is a pure value type holding the
// canonical orbit parameterization — a `target` the eye orbits, plus `yaw`,
// `pitch` (radians) and `radius` (the eye's spherical offset) and a vertical
// FOV. The eye (`look_from`) and look point (`look_at == target`) are DERIVED
// from these on demand, so the shell can drive `orbit`/`zoom` (mouse) or set the
// pose fields directly (tests). The convention matches the engine's right-handed
// look-at: yaw=0, pitch=0 puts the eye at `target + {0,0,radius}` (on +Z) looking
// down −Z at `target`. It exposes a `view()` Mat4 and a `render::common::Camera`
// for a given image size so the SAME pose drives both the controller's pick ray
// and any path-trace the shell runs. No SDL, no `aleph.editor` dependency — the
// controller is headless.
struct OrbitCamera {
    aleph::math::Vec3 target{0.0f, 0.0f, 0.0f};  // orbit center / look point
    aleph::math::f32  yaw{0.0f};                 // radians about world +Y
    aleph::math::f32  pitch{0.0f};               // radians off the XZ plane
    aleph::math::f32  radius{5.0f};              // eye distance from target
    aleph::math::Vec3 up{0.0f, 1.0f, 0.0f};
    aleph::math::f32  vfov_deg{40.0f};

    // The eye position derived from (target, yaw, pitch, radius). yaw=0,pitch=0
    // ⇒ eye on +Z at `radius`. Pure f32 trig; deterministic.
    [[nodiscard]] aleph::math::Vec3 look_from() const noexcept {
        using aleph::math::f32;
        const f32 cp = std::cos(pitch);
        return target + aleph::math::Vec3{
            radius * cp * std::sin(yaw),
            radius * std::sin(pitch),
            radius * cp * std::cos(yaw),
        };
    }
    // The look point is the orbit target.
    [[nodiscard]] aleph::math::Vec3 look_at() const noexcept { return target; }

    // Orbit by screen-space deltas (radians per pixel scaled by ORBIT_SPEED).
    // `dx` yaws about world up; `dy` pitches. Pitch is clamped shy of the poles
    // so the basis never degenerates (look direction parallel to `up`).
    void orbit(aleph::math::f32 dx, aleph::math::f32 dy) noexcept {
        using aleph::math::f32;
        constexpr f32 kOrbitSpeed = 0.008f;
        yaw   -= dx * kOrbitSpeed;
        pitch -= dy * kOrbitSpeed;
        pitch  = std::clamp(pitch, -1.5533f, 1.5533f);  // ~±89° — never reach a pole
    }

    // Dolly: `factor > 1` moves the eye away from `target`, `factor < 1` toward
    // it. `radius` is clamped to a sane band so the camera can neither cross the
    // target nor fly off. `target` is fixed.
    void zoom(aleph::math::f32 factor) noexcept {
        radius = std::clamp(radius * factor, 0.05f, 1.0e4f);
    }

    // World→view matrix (right-handed look-at): the eye at `look_from()` looking
    // toward `target` with world-up `up`. Delegates to `aleph.math::Mat4::
    // look_at`, the engine's canonical view builder (column-major, f forward),
    // so the controller's view convention is identical to the rest of the math
    // layer rather than a hand-rolled duplicate.
    [[nodiscard]] aleph::math::Mat4 view() const noexcept {
        return aleph::math::Mat4::look_at(look_from(), target, up);
    }

    // The render-side projection for an `w x h` image, built from this pose with
    // no defocus (a crisp editor view). Mirrors `lower()`'s pose→camera contract
    // so a path-trace of the controller's scene sees exactly this viewpoint.
    [[nodiscard]] aleph::render::common::Camera
    render_camera(int w, int h) const noexcept {
        return aleph::render::common::make_camera(
            look_from(), target, up, vfov_deg, w, h,
            /*defocus_angle_deg=*/0.0f, /*focus_dist=*/1.0f);
    }
};

// The headless editor controller (SPEC §3.2). Holds the graph (truth), the
// previous `LoweredScene` (for incremental re-lowering), the rasterizer build
// (`SceneRT` + `face_source`), the path-trace `Scene` + an entity↔primitive map
// (`prim_source`: per `entities` index → source NodeId), the orbit camera, and
// the current selection.
class EditorController {
public:
    // Take ownership of the truth, then FULL lower + build BOTH backends so the
    // controller is immediately pickable/renderable. A camera-less or otherwise
    // un-lowerable initial graph yields empty derived state (no faces, no
    // primitives) rather than a throw — `apply` later reports structured errors.
    explicit EditorController(aleph::graph::Graph initial)
        : graph_{std::move(initial)} {
        rebuild_full();
    }

    // ── Selection ───────────────────────────────────────────────────────────
    [[nodiscard]] std::optional<aleph::types::NodeId> selected() const noexcept {
        return selection_;
    }
    void select(std::optional<aleph::types::NodeId> sel) noexcept {
        selection_ = sel;
    }

    // ── transform_of(node) -> controlling Transform ─────────────────────────
    // Scan Contains edges into `node`; return the first source that is a
    // Transform node (each object owns exactly one such parent in the editor's
    // graph). `nullopt` if the node has no Transform parent. Passing a non-Mesh
    // id is well-defined: returns nullopt if no Transform parent exists. Pure
    // const query.
    [[nodiscard]] std::optional<aleph::types::NodeId>
    transform_of(aleph::types::NodeId node) const noexcept {
        for (auto [eid, e] : graph_.edges()) {
            (void)eid;
            if (e.kind == aleph::types::EdgeKind::Contains && e.dst == node) {
                const aleph::types::Node* src = graph_.node(e.src);
                if (src != nullptr
                    && aleph::types::kind_of(*src)
                           == aleph::types::NodeKind::Transform) {
                    return e.src;
                }
            }
        }
        return std::nullopt;
    }

    // ── Viewport (SPEC §3.2) ──────────────────────────────────────────────────
    // The controller owns the image size used by `pick(px,py)` and the orbit
    // camera's projection, so a 2-arg pixel pick suffices (the shell sets the
    // viewport when the window resizes). Non-positive sizes are ignored.
    [[nodiscard]] int width()  const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    void set_viewport(int w, int h) noexcept {
        if (w > 0) width_  = w;
        if (h > 0) height_ = h;
    }

    // ── pick(px,py) -> NodeId (SPEC §3.2) ────────────────────────────────────
    // Build a camera ray for pixel (px,py) of the current viewport from the orbit
    // camera, intersect it against the SceneRT faces (own ray-vs-triangle,
    // nearest hit), and return that face's `face_source` NodeId. `nullopt` on a
    // miss (no face hit). Pure const query — neither the graph nor the camera is
    // touched. The pixel→ray construction matches `render::common::make_camera`
    // (same r/u/f basis, same viewport mapping) so the pick agrees with what the
    // path-trace would render under this camera.
    [[nodiscard]] std::optional<aleph::types::NodeId>
    pick(int px, int py) const noexcept {
        const int w = width_;
        const int h = height_;
        if (w <= 0 || h <= 0 || sw_.scene.faces.empty()) return std::nullopt;

        using aleph::math::f32;
        using aleph::math::Vec3;
        constexpr f32 kPi = 3.14159265358979323846f;

        // Camera basis (matches make_camera: w' = normalize(from-at), u' =
        // normalize(cross(up, w')), v' = cross(w', u'); forward = -w').
        const Vec3 from = cam_.look_from();
        const Vec3 wv = aleph::math::normalize(from - cam_.look_at());
        const Vec3 uv = aleph::math::normalize(aleph::math::cross(cam_.up, wv));
        const Vec3 vv = aleph::math::cross(wv, uv);

        const f32 theta  = cam_.vfov_deg * (kPi / 180.0f);
        const f32 half_h = std::tan(theta * 0.5f);                 // at focus_dist=1
        const f32 aspect = static_cast<f32>(w) / static_cast<f32>(h);
        const f32 half_w = half_h * aspect;

        // Pixel center → NDC in [-1,1], y flipped (row 0 at the top).
        const f32 ndc_x = (2.0f * (static_cast<f32>(px) + 0.5f)
                           / static_cast<f32>(w)) - 1.0f;
        const f32 ndc_y = 1.0f - (2.0f * (static_cast<f32>(py) + 0.5f)
                                  / static_cast<f32>(h));

        const Vec3 origin = from;
        const Vec3 dir = aleph::math::normalize(
            uv * (ndc_x * half_w) + vv * (ndc_y * half_h) - wv);

        // Nearest hit over all faces (own ray-vs-triangle, Möller–Trumbore on the
        // first triangle {v0,v1,v2} of each face; SceneRT faces from build_sw are
        // degenerate quads carrying one triangle, so {0,1,2} is the whole face).
        f32         best_t = std::numeric_limits<f32>::infinity();
        std::size_t best_i = sw_.scene.faces.size();  // sentinel: no hit
        for (std::size_t i = 0; i < sw_.scene.faces.size(); ++i) {
            f32 t = 0.0f;
            if (ray_tri(origin, dir, sw_.scene.faces[i], t) && t < best_t) {
                best_t = t;
                best_i = i;
            }
        }
        if (best_i >= sw_.scene.faces.size()) return std::nullopt;  // miss
        // face_source is 1:1 with faces by construction (build_sw_scene).
        if (best_i >= sw_.face_source.size()) return std::nullopt;  // defensive
        return sw_.face_source[best_i];
    }

    // ── apply(Op) -> expected<void, OpError> (SPEC §3.2) ────────────────────
    // The engine loop: mutate the TRUTH via `apply_op`, then re-derive everything
    // from it. On `apply_op` failure the graph is unchanged (all-or-nothing,
    // SPEC §5) and we surface the structured `OpError` WITHOUT touching any
    // derived state. On success we `lower_incremental(prev, graph, op, rec)`
    // (byte-identical to a full re-lower) and rebuild the SceneRT + Scene + maps,
    // then adopt the new LoweredScene as `prev` for the next incremental step.
    [[nodiscard]] std::expected<void, aleph::lowering::OpError>
    apply(const aleph::lowering::Op& op) {
        // (0) Snapshot the pre-op graph (g_before) so a successful structural
        //     edit can localize the wave operator rebuild: decompose_rewrite +
        //     two_hop_touched_edges need both g_before and g_after. Captured
        //     UNCONDITIONALLY before apply_op (which mutates graph_ in place);
        //     on an apply_op failure graph_ is untouched and prev_graph_ is just
        //     a harmless extra copy never read. (Graph is move-only, so this is
        //     an exact id-preserving clone, not a copy-assign.)
        prev_graph_ = graph_.clone();

        // (1) Mutate the truth transactionally. Failure ⇒ graph untouched.
        auto rec = aleph::lowering::apply_op(graph_, op);
        if (!rec.has_value()) {
            return std::unexpected(rec.error());
        }

        // (2) Incrementally re-lower from `prev` (byte-identical to lower(graph)).
        //     A LowerError here means the committed graph is un-lowerable (e.g.
        //     the op removed the camera); we surface it as InvariantViolation —
        //     the op produced a state the renderer can't consume. The graph stays
        //     committed (the mutation already validated against the graph
        //     invariants); the derived state simply rebuilds empty below if so.
        auto lowered =
            aleph::lowering::lower_incremental(prev_, graph_, op, *rec, nullptr);
        if (!lowered.has_value()) {
            // Un-lowerable post-state: clear derived state to a coherent empty
            // view rather than leave stale geometry pointing at the old graph.
            prev_ = aleph::lowering::LoweredScene{};
            // The graph WAS committed by apply_op above, so keep the wave
            // operator/field consistent with it (not stale from the pre-op graph).
            if (sim_enabled_) rebuild_operator_and_reproject();
            rebuild_backends_from_prev();
            return std::unexpected(aleph::lowering::OpError::InvariantViolation);
        }

        // (3) Adopt the new IR as `prev`, re-derive the wave operator+field from
        //     the committed graph (survivors keep φ, new nodes start at 0), then
        //     rebuild both backends + the maps (which re-bakes φ→vcol).
        prev_ = std::move(*lowered);
        if (sim_enabled_) {
            // Δ depends only on the graph TOPOLOGY (Mesh nodes + Adjacent edges),
            // so skip the O(n²)+Wasserstein `build_laplacian` for attribute-only
            // ops (SetMaterial/SetTransform leave the skeleton — and thus φ's
            // indexing — intact). The RewriteRecord's create/delete sets are the
            // exact topology delta.
            const bool topo_changed = !rec->created_nodes.empty()
                                   || !rec->deleted_nodes.empty()
                                   || !rec->created_edges.empty()
                                   || !rec->deleted_edges.empty();
            if (topo_changed) {
                // Localizable structural ops (AddObject / DeleteObject) recompute
                // only the κ_R edges within 2 hops of the edit; ApplyRule (an
                // arbitrary rewrite) takes the full bounded rebuild. Attribute-only
                // ops (SetMaterial / SetTransform) never reach here — topo_changed
                // is false for them.
                const bool op_is_localizable =
                    std::holds_alternative<aleph::lowering::AddObject>(op) ||
                    std::holds_alternative<aleph::lowering::DeleteObject>(op);
                rebuild_operator_localized(*rec, op_is_localizable);
            }
        }
        rebuild_backends_from_prev();
        return {};
    }

    // ── Wave field (SPEC §3.2 physics seam) ──────────────────────────────────
    // Toggle the scalar-wave overlay. Enabling (re)builds the shared graph
    // Laplacian Δ from the committed graph and resets φ/φ̇ to zero over its
    // node_order. The graph stays the single source of truth — the field is a
    // derived structure rebuilt from it, never mutating it.
    void enable_sim(bool on) {
        sim_enabled_ = on;
        if (on) {
            // The wave operator is the BOUNDED-support curvature Laplacian κ_R
            // (build_laplacian_bounded): a pure function of each edge's radius-R
            // ball, so an edit can rebuild it byte-exact by recomputing only the
            // R-hop-dirty edges (build_laplacian_local, in apply()). The first
            // build has no valid prev operator/graph, so it is the full bounded
            // rebuild.
            operator_ = aleph::flow::build_laplacian_bounded(
                graph_, aleph::flow::default_weight);
            u_ = aleph::sim::Section<double>::zeros(operator_.node_order);
            v_ = aleph::sim::Section<double>::zeros(operator_.node_order);
        }
    }

    // Velocity impulse at node `n`; false (no-op) if `n` is not in the field's
    // node_order (e.g. a node that produced no Δ vertex). Routes to the VELOCITY
    // section v_ (matches the old ScalarField::kick: phi_dot[i] += amp).
    [[nodiscard]] bool kick(aleph::types::NodeId n, double amp) noexcept {
        return v_.add(n, amp);
    }

    // The current displacement field φ (u_) over the Laplacian's node_order,
    // read-only. (The render φ→vcol path reads the displacement, not velocity.)
    [[nodiscard]] const aleph::sim::Section<double>& displacement() const noexcept {
        return u_;
    }

    // The wave operator Δ (bounded-support κ_R Laplacian), read-only. Tests diff
    // its `matrix` byte-for-byte against a fresh `build_laplacian_bounded(graph)`
    // to certify the localized rebuild is byte-exact.
    [[nodiscard]] const aleph::flow::WeightedLaplacian& wave_operator() const noexcept {
        return operator_;
    }

    // Number of κ_R edges recomputed on the LAST topology edit (0 if the last
    // edit took the full-bounded fallback or was attribute-only). The localized
    // win: this is O(touched) ≪ |E| on a large lattice.
    [[nodiscard]] int last_recompute_count() const noexcept {
        return recompute_count_;
    }

    // Advance the wave one symplectic-Euler sub-step (φ̈ = −c²Δφ) on the shared Δ
    // and re-bake φ→vcol into the SW scene. No-op (success) when the sim is off.
    // The graph is NOT touched — `step` evolves only the derived field. A stepper
    // error (EmptyField / DimMismatch / NonFinite) is surfaced unchanged and the
    // SW scene is left as-is (do not re-bake a partially-updated/diverged field).
    std::expected<void, aleph::sim::StepError> step(aleph::math::f32 dt) {
        if (!sim_enabled_) return {};
        auto r = stepper_.step(u_, v_, operator_.matrix,
                               static_cast<aleph::math::f64>(dt));
        if (!r) return r;
        rebuild_backends_from_prev();  // re-bake vcol from the evolved φ
        return r;
    }

    // Re-bake sw_ (+ render_) at the CURRENT orbit pose. The shell calls this once
    // after setting the framed camera, since the ctor baked at the default pose and
    // the Metal/Dielectric vcol is view-dependent (so the construction-time bake used
    // the wrong eye). No-op-safe: a plain re-derive of both backends from `prev_`.
    void rebake_view() { rebuild_backends_from_prev(); }

    // Re-bake ONLY sw_ at the current orbit eye (view-dependent Metal/Dielectric
    // vcol tracks the camera). render_/BVH + prim_source_ are view-INDEPENDENT (the
    // PT integrates the view via rays; only consumed when idle) — left untouched.
    // Clock-free / deterministic: a pure re-bake at the current cam_.look_from().
    void rebake_view_sw() {
        if (sim_enabled_) { const std::vector<double> phi = gather_phi_entity();
                            sw_ = aleph::lowering::build_sw_scene(prev_, cam_.look_from(), &phi); }
        else              { sw_ = aleph::lowering::build_sw_scene(prev_, cam_.look_from()); }
    }

    // Only Metal/Dielectric vcol depends on the eye; Lambertian/TexturedLambertian/
    // Emissive are view-independent (shade_face's default branch never reads V), so
    // an all-Lambertian scene needs NO re-bake on orbit. True iff any entity is
    // Metal or Dielectric.
    [[nodiscard]] bool has_view_dependent_material() const {
        for (const auto& e : prev_.entities)
            if (e.material.kind == aleph::types::MaterialKind::Metal ||
                e.material.kind == aleph::types::MaterialKind::Dielectric) return true;
        return false;
    }

    // ── Accessors ────────────────────────────────────────────────────────────
    [[nodiscard]] OrbitCamera&       camera()       noexcept { return cam_; }
    [[nodiscard]] const OrbitCamera& camera() const noexcept { return cam_; }

    [[nodiscard]] const aleph::render::sw::SceneRT& raster_scene() const noexcept {
        return sw_.scene;
    }
    [[nodiscard]] const aleph::scene::Scene& render_scene() const noexcept {
        return render_.scene;
    }

    // The controller's current lowered IR (the incremental base `prev`). The
    // single derived structure both backends are built from; tests diff it
    // byte-for-byte against a fresh full `lower(graph)` (SPEC §2/§5.3).
    [[nodiscard]] const aleph::lowering::LoweredScene& lowered() const noexcept {
        return prev_;
    }

    // The graph truth (read-only) — useful for the shell when building Ops that
    // name existing nodes (e.g. a parent Transform for AddObject).
    [[nodiscard]] const aleph::graph::Graph& graph() const noexcept {
        return graph_;
    }

    // Per-rasterizer-face source NodeId (1:1 with `raster_scene().faces`).
    [[nodiscard]] const std::vector<aleph::types::NodeId>& face_source() const noexcept {
        return sw_.face_source;
    }
    // Per-`entities`-index source NodeId (the entity↔primitive map; aligned to
    // `prev`'s entities and to the order primitives were appended to the Scene).
    [[nodiscard]] const std::vector<aleph::types::NodeId>& prim_source() const noexcept {
        return prim_source_;
    }

private:
    // Full lower from the truth (used once at construction). Falls back to an
    // empty LoweredScene if the graph cannot be lowered (e.g. no Camera), then
    // builds coherent-but-empty backends.
    void rebuild_full() {
        auto lowered = aleph::lowering::lower(graph_);
        prev_ = lowered.has_value() ? std::move(*lowered)
                                    : aleph::lowering::LoweredScene{};
        rebuild_backends_from_prev();
    }

    // Re-derive the wave operator (full bounded rebuild) and re-project the field
    // after a graph edit. Δ is rebuilt from the (already-committed) graph;
    // `reproject` carries every surviving NodeId's (φ, φ̇) forward onto the new
    // node_order and zeros any new node. The graph is the truth — this only
    // rebuilds derived state. The FALLBACK rebuild: used by the un-lowerable error
    // path and whenever the localized path is not applicable. Resets
    // recompute_count_ to 0 (no localized recompute happened).
    void rebuild_operator_and_reproject() {
        operator_ = aleph::flow::build_laplacian_bounded(
            graph_, aleph::flow::default_weight);
        u_.reproject(operator_.node_order);  // survivors keep φ,  new nodes 0
        v_.reproject(operator_.node_order);  // survivors keep φ̇, new nodes 0
        recompute_count_ = 0;
    }

    // Re-derive the wave operator on a STRUCTURAL edit via the LOCALIZED bounded
    // rebuild (the asymptotic win). Decompose the rewrite (prev_graph_ → graph_)
    // into the Mayer-Vietoris cover, seed the 2-hop dirty cover from R's mesh
    // nodes plus the endpoints of the created/deleted Adjacent edges, and recompute
    // only those κ_R edges (caching the rest from `operator_`). The result is
    // BYTE-EXACT to a full build_laplacian_bounded(graph_) because κ_R(e) is a pure
    // function of e's radius-R ball: a non-dirty edge's ball is unchanged, so its
    // cached κ_R equals the full rebuild's bit-for-bit. Falls back to the full
    // bounded rebuild when the op is not localizable or the dirty cover is too
    // large to be worth it. After either path, reproject φ/φ̇ as usual.
    void rebuild_operator_localized(const aleph::dpo::RewriteRecord& rec,
                                    bool op_is_localizable) {
        using aleph::types::NodeId;
        using aleph::sheaf::OneSkeleton;

        if (!op_is_localizable) {
            rebuild_operator_and_reproject();  // full bounded fallback
            return;
        }

        // preserved = current graph node ids minus the just-created ones (the
        // image of the rule interface — the survivors decompose_rewrite keys on).
        // created_nodes is small (a single AddObject creates one Mesh, one
        // Material), so the linear membership check is cheap.
        const auto is_created = [&](NodeId id) {
            for (NodeId c : rec.created_nodes) {
                if (c == id) return true;
            }
            return false;
        };
        aleph::containers::FlatSet<NodeId> preserved;
        for (auto [id, node] : graph_.nodes()) {
            (void)node;
            if (!is_created(id)) preserved.insert(id);
        }

        auto [u, k, r] =
            aleph::sheaf::decompose_rewrite(prev_graph_, graph_, preserved);
        (void)u;
        (void)k;

        const OneSkeleton skel = OneSkeleton::from_graph(graph_);

        // seed = R's NEWLY-CREATED mesh nodes ∪ endpoints of created/deleted
        // Adjacent edges. R = preserved ∪ created, but the preserved interface
        // is the WHOLE surviving graph for a pure delete, so seeding all of R's
        // mesh nodes would mark every edge dirty and defeat localization. The
        // genuinely-new region is R's mesh vertices that were just created; that,
        // plus the endpoints of the created/deleted Adjacent edges (which name
        // exactly where the coupling topology changed, and survive an edge-only
        // delete), is the tight 2-hop seed that is still a sound over-approximation
        // of where κ_R changed (κ_R(e) only moves within R hops of an edited edge).
        std::vector<NodeId> seed;
        const OneSkeleton r_skel = r.one_skeleton(graph_);
        for (NodeId v : r_skel.vertices) {
            if (is_created(v)) seed.push_back(v);
        }
        for (aleph::types::EdgeId eid : rec.created_edges) {
            if (const aleph::types::Edge* e = graph_.edge(eid);
                e != nullptr && e->kind == aleph::types::EdgeKind::Adjacent) {
                seed.push_back(e->src);
                seed.push_back(e->dst);
            }
        }
        for (aleph::types::EdgeId eid : rec.deleted_edges) {
            if (const aleph::types::Edge* e = prev_graph_.edge(eid);
                e != nullptr && e->kind == aleph::types::EdgeKind::Adjacent) {
                seed.push_back(e->src);
                seed.push_back(e->dst);
            }
        }

        const auto dirty = aleph::flow::two_hop_touched_edges(skel, seed);

        // Localize only when the dirty cover is a small fraction of |E|; else the
        // bounded rebuild is no slower and avoids the cache bookkeeping.
        constexpr double kLocalFraction = 0.5;
        recompute_count_ = 0;
        if (static_cast<double>(dirty.size())
            <= kLocalFraction * static_cast<double>(skel.edges.size())) {
            operator_ = aleph::flow::build_laplacian_local(
                graph_, operator_, dirty, aleph::flow::default_weight,
                &recompute_count_);
        } else {
            operator_ = aleph::flow::build_laplacian_bounded(
                graph_, aleph::flow::default_weight);
        }
        u_.reproject(operator_.node_order);  // survivors keep φ,  new nodes 0
        v_.reproject(operator_.node_order);  // survivors keep φ̇, new nodes 0
    }

    // Gather a per-entity φ aligned to `prev_.entities` (each entity looked up in
    // the field by its `source` NodeId), for the wave-sim build_sw_scene path.
    // build_sw_scene indexes φ by entity `i`, so the three properties are load-
    // bearing: sized to `prev_.entities.size()`; each defaulted to 0.0 (the
    // "φ=0 neutral white" contract for entities with no field entry — e.g. a mesh
    // with no Adjacent edge, so it is not a Δ vertex); first-match-wins.
    [[nodiscard]] std::vector<double> gather_phi_entity() const {
        std::vector<double> phi(prev_.entities.size(), 0.0);
        for (std::size_t i = 0; i < prev_.entities.size(); ++i) {
            const aleph::types::NodeId src = prev_.entities[i].source;
            for (std::size_t j = 0; j < u_.order.size(); ++j)
                if (u_.order[j] == src) { phi[i] = u_.data[j]; break; }
        }
        return phi;
    }

    // Rebuild the rasterizer SceneRT (+ face_source), the path-trace Scene, and
    // the entity↔primitive map from the current `prev_` LoweredScene. Seam where
    // the lowered IR materializes into both renderer backends.
    void rebuild_backends_from_prev() {
        // Software rasterizer faces + face→NodeId map (SPEC §3.1): the view-
        // dependent half, factored into `rebake_view_sw()` (so an orbit can re-bake
        // ONLY sw_ at the live eye without rebuilding the view-independent render_/
        // BVH). Net behaviour here is unchanged — sw_ is baked at cam_.look_from()
        // exactly as before.
        rebake_view_sw();
        // Path-trace SoA/BVH scene + camera pose (camera pose is also mirrored by
        // the orbit camera, which the shell drives; we keep the IR camera for the
        // render bundle but pick/render use the orbit camera's pose).
        render_ = aleph::lowering::build_render_scene(prev_);
        // Entity↔primitive map: `build_render_scene` appends one primitive per
        // entity in `entities` order, so the source NodeId per primitive is just
        // each entity's `source`, in order.
        prim_source_.clear();
        prim_source_.reserve(prev_.entities.size());
        for (const auto& e : prev_.entities) {
            prim_source_.push_back(e.source);
        }
    }

    // Möller–Trumbore ray-vs-triangle on a SceneRT face's first triangle
    // {verts[0], verts[1], verts[2]}. Returns true and the hit distance `t` (> a
    // small epsilon, in front of the eye) on a hit inside the triangle; false on
    // a parallel ray, a back-of-eye hit, or a barycentric miss. `dir` need not be
    // normalized (t is in `dir` units); the caller passes a unit `dir` so `t` is
    // a world-space distance and the nearest-hit comparison is metric.
    [[nodiscard]] static bool ray_tri(aleph::math::Vec3 orig,
                                       aleph::math::Vec3 dir,
                                       const aleph::render::sw::Face& f,
                                       aleph::math::f32& t) noexcept {
        using aleph::math::f32;
        using aleph::math::Vec3;
        constexpr f32 kEps = 1e-7f;
        const Vec3 v0 = f.verts[0];
        const Vec3 v1 = f.verts[1];
        const Vec3 v2 = f.verts[2];
        const Vec3 e1 = v1 - v0;
        const Vec3 e2 = v2 - v0;
        const Vec3 p  = aleph::math::cross(dir, e2);
        const f32  det = aleph::math::dot(e1, p);
        if (det > -kEps && det < kEps) return false;  // ray parallel to triangle
        const f32  inv = 1.0f / det;
        const Vec3 tv  = orig - v0;
        const f32  u   = aleph::math::dot(tv, p) * inv;
        if (u < 0.0f || u > 1.0f) return false;
        const Vec3 q   = aleph::math::cross(tv, e1);
        const f32  v   = aleph::math::dot(dir, q) * inv;
        if (v < 0.0f || u + v > 1.0f) return false;
        const f32  tt  = aleph::math::dot(e2, q) * inv;
        if (tt <= 1e-4f) return false;  // behind the eye / too close
        t = tt;
        return true;
    }

    aleph::graph::Graph            graph_;       // the single source of truth
    aleph::graph::Graph            prev_graph_{}; // g_before snapshot (localized Δ rebuild)
    aleph::lowering::LoweredScene  prev_{};      // last lowered IR (incremental base)
    aleph::lowering::SwBuild       sw_{};        // SceneRT + face_source (raster pick)
    aleph::lowering::RenderScene   render_{};    // path-trace Scene + camera pose
    std::vector<aleph::types::NodeId> prim_source_{};  // per-entity source (entity↔primitive)
    aleph::flow::WeightedLaplacian operator_{};   // Δ, rebuilt from graph_
    aleph::sim::Section<double>    u_{};         // φ  (displacement) over operator_.node_order
    aleph::sim::Section<double>    v_{};         // φ̇ (velocity)     over operator_.node_order
    aleph::sim::WaveStepper        stepper_{};    // symplectic-Euler wave integrator
    int                            recompute_count_ = 0;  // κ_R edges recomputed on the last edit
    bool                           sim_enabled_ = false;
    OrbitCamera                    cam_{};       // OWN orbit camera (headless)
    std::optional<aleph::types::NodeId> selection_{};  // current selection
    int                            width_{800};  // viewport (px) for pick/projection
    int                            height_{600};
};

}  // namespace aleph::edit

# Intuitive Editor (select / see / move) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `aleph_edit` (no-args interactive mode) intuitive: click to select, see an anti-aliased silhouette outline on the selected object, and move it with the arrow keys (camera-relative, fixed step), with every object owning its own Transform.

**Architecture:** Each object gets a private `Transform` node (`root → Transform → Mesh`) so `SetTransform` moves only that object. The controller gains `transform_of` (mesh→Transform lookup) + `translate_selected` (compose a translation, emit `SetTransform`). The selection outline is a **second raster pass** of only the selected entity's faces into a scratch depth buffer, from which a pure `draw_selection_outline` post-process paints a ring — no change to the shared hot rasterizer. The window layer is extended to populate modifier flags and expose named arrow-key constants.

**Tech Stack:** C++26 modules (GCC 16), `std::expected`/`std::optional` (no exceptions/RTTI), doctest, CMake+Ninja, SDL2 (window layer only).

---

## Spec

`docs/superpowers/specs/2026-06-06-intuitive-editor-design.md`

## Conventions (read before starting)

- **Build (functional):** `cmake --build build-release`
- **Build (strict warnings gate):** `cmake --build build-release-strict 2>&1 | grep "warning:" | grep -v '^ninja:' | wc -l` → must be `0`
- **Run a single doctest case:** `./build-release/tests/aleph_tests --test-case="<name>"`
  (doctest is the test framework; `#include "doctest.h"`, `TEST_CASE("name") { CHECK(...); REQUIRE(...); }`.)
- **Full suite gate:** `ctest --test-dir build-release` → currently 20/20.
- **ASan gate (from repo root):** `LSAN_OPTIONS=suppressions=tests/asan.supp ASAN_OPTIONS=detect_leaks=1 ./build-asan/tests/aleph_tests`
- **C++ TDD reality:** a "failing test" for a not-yet-defined function fails by **not compiling** (e.g. `error: no member named 'transform_of'`). That red build IS the failing-test step; green after implementing.
- **Modules gotcha (gcc-16):** do NOT touch the dual-defined IR structs (`MaterialParams`/`LoweredEntity`/`Camera`/`Scene` in `:lower` and `:lowered`) — none of these tasks need to. If you must, edit both copies token-identically or the build dies with "Bad file data".
- **New test file:** add its path to the source list in `tests/CMakeLists.txt` (the list that ends at `sim/test_resonance.cpp)` around line 107), then rebuild.

## File Structure

| File | Responsibility | Tasks |
|---|---|---|
| `render/src/aleph.window/aleph.window-event.cppm` | named arrow-key constants (SDL-free) | 1 |
| `render/src/aleph.window/aleph.window-window.cppm` | populate `Event.shift/ctrl/alt`; static_assert keycodes | 1 |
| `bridge/src/aleph.edit/aleph.edit-controller.cppm` | `transform_of`, `translate_selected`, `selected_offset` | 2,3,9 |
| `bridge/src/aleph.lowering/aleph.lowering-ops.cppm` | `AddObject` mints a per-object `Transform` | 4 |
| `apps/aleph_edit/main.cpp` | per-object Transforms in initial scene; key→nudge wiring; outline wiring; panel hints | 5,6,8,9 |
| `render/src/aleph.render.sw/aleph.render.sw-outline.cppm` (new) | pure `draw_selection_outline` | 7 |
| `render/src/aleph.render.sw/aleph.render.sw.cppm` | export the new `:outline` partition | 7 |
| `tests/edit/test_translate.cpp` (new) | `transform_of` + `translate_selected` | 2,3 |
| `tests/lowering/test_add_object.cpp` | updated oracle (per-object Transform) | 4 |
| `tests/render/test_sw_outline.cpp` (new) | `draw_selection_outline` | 7 |
| `tests/CMakeLists.txt` | register the two new test files | 2,7 |

## Build Order

1. Window glue (Task 1) — needed by the key wiring.
2. Controller logic (Tasks 2, 3) — unit-tested in isolation, independent of AddObject.
3. Per-object Transform model (Tasks 4, 5) — AddObject + initial scene.
4. Key→nudge wiring (Task 6) — depends on 1, 3, 5.
5. Outline (Tasks 7, 8) — independent of 1–6.
6. Polish (Task 9) — depends on 2, 6.
7. Gates + finish (Task 10).

---

### Task 1: Window — modifier flags + named arrow keys

**Files:**
- Modify: `render/src/aleph.window/aleph.window-event.cppm`
- Modify: `render/src/aleph.window/aleph.window-window.cppm:46-85` (`poll_events`)

This is SDL glue (not runtime-unit-testable in `aleph_tests`); correctness is pinned by a **compile-time `static_assert`** that the constants equal the SDL keycodes, plus the build.

- [ ] **Step 1: Add SDL-free named key constants**

In `aleph.window-event.cppm`, inside `export namespace aleph::window {` after the `Event` struct (before the closing brace), add:

```cpp
// Named non-ASCII keys. Values are the SDL keycodes for the arrow keys
// (SDLK_RIGHT/LEFT/DOWN/UP = 0x40000000 | scancode). aleph.window-window.cppm
// static_asserts these equal SDLK_* so they cannot drift; keeping them here lets
// the app compare Event.key without including SDL.
namespace key {
    inline constexpr int Right = 1073741903;
    inline constexpr int Left  = 1073741904;
    inline constexpr int Down  = 1073741905;
    inline constexpr int Up    = 1073741906;
}
```

- [ ] **Step 2: Populate modifiers + guard the keycodes in `poll_events`**

In `aleph.window-window.cppm`, replace the body of `poll_events` (lines 46–85). Add the static_asserts just above the method and read the modifier state once per event:

```cpp
    int poll_events(std::span<Event> out) noexcept {
        static_assert(key::Right == SDLK_RIGHT && key::Left == SDLK_LEFT
                   && key::Down  == SDLK_DOWN  && key::Up   == SDLK_UP,
                      "aleph::window::key constants must match SDL keycodes");
        int n = 0;
        SDL_Event ev;
        const SDL_Keymod mod = SDL_GetModState();
        const bool sh = (mod & KMOD_SHIFT) != 0;
        const bool ct = (mod & KMOD_CTRL)  != 0;
        const bool al = (mod & KMOD_ALT)   != 0;
        while (n < static_cast<int>(out.size()) && SDL_PollEvent(&ev)) {
            Event& e = out[static_cast<std::span<Event>::size_type>(n)];
            e = Event{};
            e.shift = sh; e.ctrl = ct; e.alt = al;
            switch (ev.type) {
                case SDL_QUIT:           e.kind = Event::Kind::Quit; ++n; break;
                case SDL_KEYDOWN:
                    e.kind = Event::Kind::KeyDown;
                    e.key  = static_cast<int>(ev.key.keysym.sym);
                    ++n; break;
                case SDL_KEYUP:
                    e.kind = Event::Kind::KeyUp;
                    e.key  = static_cast<int>(ev.key.keysym.sym);
                    ++n; break;
                case SDL_MOUSEBUTTONDOWN:
                    e.kind = Event::Kind::MouseDown;
                    e.button = ev.button.button;
                    e.x = ev.button.x; e.y = ev.button.y;
                    ++n; break;
                case SDL_MOUSEBUTTONUP:
                    e.kind = Event::Kind::MouseUp;
                    e.button = ev.button.button;
                    e.x = ev.button.x; e.y = ev.button.y;
                    ++n; break;
                case SDL_MOUSEMOTION:
                    e.kind = Event::Kind::MouseMove;
                    e.x  = ev.motion.x;     e.y  = ev.motion.y;
                    e.dx = ev.motion.xrel;  e.dy = ev.motion.yrel;
                    ++n; break;
                case SDL_MOUSEWHEEL:
                    e.kind = Event::Kind::MouseWheel;
                    e.wheel = ev.wheel.y;
                    ++n; break;
                default: break;
            }
        }
        return n;
    }
```

(Note: `e = Event{}` already default-inits the modifier flags to `false`; the three assignments overwrite them with the live state. The `key` namespace is visible here because `:event` is imported by the window partition; if the build reports `key` undeclared, add `import :event;` — but `aleph.window-window.cppm` already imports it.)

- [ ] **Step 3: Build (functional + strict)**

Run: `cmake --build build-release && cmake --build build-release-strict 2>&1 | grep "warning:" | grep -v '^ninja:' | wc -l`
Expected: builds clean; warning count `0`. (The `static_assert` compiling is the correctness check for the key constants.)

- [ ] **Step 4: Commit**

```bash
git add render/src/aleph.window/aleph.window-event.cppm render/src/aleph.window/aleph.window-window.cppm
git commit -m "feat(window): populate Event modifiers + named arrow-key constants"
```

---

### Task 2: Controller — `transform_of(mesh)`

**Files:**
- Modify: `bridge/src/aleph.edit/aleph.edit-controller.cppm` (add public method near `selected()`, ~line 152)
- Create: `tests/edit/test_translate.cpp`
- Modify: `tests/CMakeLists.txt` (register the new test)

- [ ] **Step 1: Write the failing test**

Create `tests/edit/test_translate.cpp`:

```cpp
#include "doctest.h"

#include <optional>
#include <utility>

import aleph.math;       // Vec3, Mat4
import aleph.types;      // NodeId, Transform, Mesh, Material, geometry payloads
import aleph.graph;      // Graph
import aleph.edit;       // EditorController

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Vec3;
using aleph::math::Mat4;
using aleph::edit::EditorController;

namespace {

// root Transform ─Contains→ per-object Transform ─Contains→ Mesh ─References→ Material,
// plus a Camera under root so the controller lowers to a non-empty scene.
struct TwoXf { Graph g; NodeId root{}, xf{}, mesh{}; };

TwoXf make_one_object() {
    TwoXf s;
    Graph& g = s.g;
    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, LocalTransform{Mat4::identity()}});

    const NodeId cam = g.alloc_node_id();
    Camera c{cam, std::string("sensor0")};
    c.look_from = Vec3{0.0f, 1.0f, 5.0f};
    c.look_at   = Vec3{0.0f, 0.0f, 0.0f};
    c.up        = Vec3{0.0f, 1.0f, 0.0f};
    c.vfov_deg  = 45.0f;
    g.insert_node(std::move(c));

    s.xf = g.alloc_node_id();
    g.insert_node(Transform{s.xf, 0, LocalTransform{Mat4::identity()}});

    s.mesh = g.alloc_node_id();
    Mesh m{s.mesh, std::string("obj"), 0};
    m.geometry = SphereLocal{Vec3{0.0f, 0.5f, 0.0f}, 0.5f};
    g.insert_node(std::move(m));

    const NodeId mat = g.alloc_node_id();
    Material mm{mat, MaterialKind::Lambertian};
    mm.albedo = Vec3{0.8f, 0.2f, 0.2f};
    g.insert_node(std::move(mm));

    (void)g.add_edge(EdgeKind::Contains,   s.root, cam);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.xf);
    (void)g.add_edge(EdgeKind::Contains,   s.xf,  s.mesh);
    (void)g.add_edge(EdgeKind::References, s.mesh, mat);
    return s;
}

}  // namespace

TEST_CASE("transform_of returns the controlling Transform of a mesh") {
    TwoXf s = make_one_object();
    const NodeId xf = s.xf, mesh = s.mesh, root = s.root;
    EditorController ctl{std::move(s.g)};

    const std::optional<NodeId> got = ctl.transform_of(mesh);
    REQUIRE(got.has_value());
    CHECK(*got == xf);

    // A node with no Transform parent (the root itself) yields nullopt.
    CHECK_FALSE(ctl.transform_of(root).has_value());
}
```

- [ ] **Step 2: Register the test, build to confirm it fails**

Add `edit/test_translate.cpp` to the source list in `tests/CMakeLists.txt` (right after `edit/test_mv_controller.cpp`).
Run: `cmake --build build-release`
Expected: FAIL to compile — `error: 'class aleph::edit::EditorController' has no member named 'transform_of'`.

- [ ] **Step 3: Implement `transform_of`**

In `aleph.edit-controller.cppm`, add this public method just after `select()` (line ~152):

```cpp
    // ── transform_of(mesh) -> controlling Transform ─────────────────────────
    // Scan Contains edges into `mesh`; return the first source that is a
    // Transform node (each object owns exactly one such parent in the editor's
    // graph). `nullopt` if the node has no Transform parent. Pure const query.
    [[nodiscard]] std::optional<aleph::types::NodeId>
    transform_of(aleph::types::NodeId mesh) const noexcept {
        for (auto [eid, e] : graph_.edges()) {
            (void)eid;
            if (e.kind == aleph::types::EdgeKind::Contains && e.dst == mesh) {
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
```

- [ ] **Step 4: Build and run the test**

Run: `cmake --build build-release && ./build-release/tests/aleph_tests --test-case="transform_of returns the controlling Transform of a mesh"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add bridge/src/aleph.edit/aleph.edit-controller.cppm tests/edit/test_translate.cpp tests/CMakeLists.txt
git commit -m "feat(edit): EditorController::transform_of (mesh -> controlling Transform)"
```

---

### Task 3: Controller — `translate_selected(delta)`

**Files:**
- Modify: `bridge/src/aleph.edit/aleph.edit-controller.cppm` (add after `transform_of`)
- Modify: `tests/edit/test_translate.cpp` (append a test case)

- [ ] **Step 1: Write the failing test**

Append to `tests/edit/test_translate.cpp`:

```cpp
TEST_CASE("translate_selected moves only the selected object's Transform") {
    TwoXf s = make_one_object();
    const NodeId xf = s.xf, mesh = s.mesh;
    EditorController ctl{std::move(s.g)};

    // No selection -> no-op success (nothing moves).
    {
        auto r = ctl.translate_selected(Vec3{1.0f, 0.0f, 0.0f});
        CHECK(r.has_value());
        const auto* node = ctl.graph().node(xf);
        REQUIRE(node != nullptr);
        const Mat4& m = std::get<Transform>(*node).local.m;
        CHECK(m(0, 3) == doctest::Approx(0.0f));
    }

    // Select the mesh and nudge +X by 1.0, then +Y by 2.0 (accumulates).
    ctl.select(mesh);
    REQUIRE(ctl.translate_selected(Vec3{1.0f, 0.0f, 0.0f}).has_value());
    REQUIRE(ctl.translate_selected(Vec3{0.0f, 2.0f, 0.0f}).has_value());

    const auto* node = ctl.graph().node(xf);
    REQUIRE(node != nullptr);
    const Mat4& m = std::get<Transform>(*node).local.m;
    CHECK(m(0, 3) == doctest::Approx(1.0f));   // translation column (m[12..14])
    CHECK(m(1, 3) == doctest::Approx(2.0f));
    CHECK(m(2, 3) == doctest::Approx(0.0f));
}
```

- [ ] **Step 2: Build to confirm it fails**

Run: `cmake --build build-release`
Expected: FAIL to compile — `no member named 'translate_selected'`.

- [ ] **Step 3: Implement `translate_selected`**

In `aleph.edit-controller.cppm`, add right after `transform_of`:

```cpp
    // ── translate_selected(world_delta) ─────────────────────────────────────
    // Move ONLY the selected object by `world_delta` (world units): resolve its
    // controlling Transform, pre-multiply a translation onto its local pose, and
    // emit a SetTransform through apply() (which re-lowers + rebuilds). No
    // selection => no-op success. A selected mesh without a Transform parent =>
    // OpError::KindMismatch (should not happen once every object owns one).
    // Because the root Transform is identity, left-multiplying translate(d)
    // shifts the object by exactly `d` in world space.
    [[nodiscard]] std::expected<void, aleph::lowering::OpError>
    translate_selected(aleph::math::Vec3 world_delta) {
        if (!selection_.has_value()) return {};
        const std::optional<aleph::types::NodeId> tid = transform_of(*selection_);
        if (!tid.has_value()) {
            return std::unexpected(aleph::lowering::OpError::KindMismatch);
        }
        const aleph::types::Node* node = graph_.node(*tid);
        const aleph::math::Mat4 cur =
            std::get<aleph::types::Transform>(*node).local.m;
        const aleph::math::Mat4 nxt =
            aleph::math::Mat4::translate(world_delta) * cur;
        return apply(aleph::lowering::Op{
            aleph::lowering::SetTransform{*tid,
                aleph::types::LocalTransform{nxt}}});
    }
```

- [ ] **Step 4: Build and run**

Run: `cmake --build build-release && ./build-release/tests/aleph_tests --test-case="translate_selected moves only the selected object's Transform"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add bridge/src/aleph.edit/aleph.edit-controller.cppm tests/edit/test_translate.cpp
git commit -m "feat(edit): EditorController::translate_selected (nudge via SetTransform)"
```

---

### Task 4: `AddObject` mints a per-object Transform

**Files:**
- Modify: `bridge/src/aleph.lowering/aleph.lowering-ops.cppm:504-563` (AddObject branch)
- Modify: `tests/lowering/test_add_object.cpp` (ADD a new test case)

**Note:** the existing `test_add_object.cpp` cases assert entity/light **counts**, byte-stable survivors, and `world_geometry == local` — NOT the direct parent→Mesh topology. They therefore stay GREEN after this change (an identity Transform mints no entity and leaves world == local). So we ADD a focused test for the new behavior rather than editing the existing oracle. The seed graph helper is `make_seed()` (root id is `s.root`, returns a `Seed`).

- [ ] **Step 1: Add a focused failing test case**

Append to `tests/lowering/test_add_object.cpp` (it already has `using namespace aleph::types;`, `make_seed()`, and imports aleph.graph/aleph.lowering):

```cpp
// AddObject must mint a PER-OBJECT Transform so the new mesh is independently
// posable: parent ─Contains→ Transform ─Contains→ Mesh (not parent→Mesh direct).
TEST_CASE("lowering: AddObject mints a per-object Transform parent for the mesh") {
    Seed s = make_seed();

    aleph::lowering::AddObject add{};
    add.parent   = s.root;
    add.geometry = SphereLocal{Vec3{3, 0, 0}, 0.5f};
    add.material = aleph::lowering::MaterialParams{};  // default Lambertian
    aleph::lowering::Op op = add;
    auto applied = aleph::lowering::apply_op(s.g, op);
    REQUIRE(applied.has_value());

    // The added Mesh is the created node of kind Mesh.
    const aleph::dpo::RewriteRecord& rec = *applied;
    NodeId new_mesh{};
    bool found_mesh = false;
    for (const NodeId id : rec.created_nodes) {
        const Node* n = s.g.node(id);
        if (n != nullptr && kind_of(*n) == NodeKind::Mesh) {
            new_mesh = id; found_mesh = true;
        }
    }
    REQUIRE(found_mesh);

    // Walk UP one Contains edge from the mesh: its parent must be a Transform...
    NodeId owning_xf{};
    bool mesh_has_xf_parent = false;
    for (auto [eid, e] : s.g.edges()) {
        (void)eid;
        if (e.kind == EdgeKind::Contains && e.dst == new_mesh) {
            const Node* src = s.g.node(e.src);
            REQUIRE(src != nullptr);
            CHECK(kind_of(*src) == NodeKind::Transform);
            owning_xf = e.src; mesh_has_xf_parent = true;
        }
    }
    CHECK(mesh_has_xf_parent);

    // ...and THAT Transform must be Contained by the original parent (root).
    bool xf_under_parent = false;
    for (auto [eid, e] : s.g.edges()) {
        (void)eid;
        if (e.kind == EdgeKind::Contains && e.src == s.root && e.dst == owning_xf)
            xf_under_parent = true;
    }
    CHECK(xf_under_parent);
}
```

- [ ] **Step 2: Build to confirm the new case fails**

Run: `cmake --build build-release && ./build-release/tests/aleph_tests --test-case="lowering: AddObject mints a per-object Transform parent for the mesh"`
Expected: FAIL — current `AddObject` attaches the Mesh directly to `parent`, so the mesh's Contains-parent is the root (not a Transform-with-a-Transform-parent): `xf_under_parent` is false and `kind_of(*src) == Transform` fails (root is a Transform, but it is not itself Contained by root, so `xf_under_parent` is false → CHECK fails).

- [ ] **Step 3: Implement the Transform mint in `AddObject`**

In `aleph.lowering-ops.cppm`, in the `AddObject` `commit_structural` lambda (lines 524–562), replace the mesh/material/edge block so a Transform is minted first and the Mesh hangs off it:

```cpp
                        // Survivors first (ids preserved), then fast-forward the
                        // allocator so new ids are collision-free.
                        detail::clone_nodes(g, post);

                        // Per-object Transform (identity) so the object is
                        // independently posable: parent ─Contains→ Transform
                        // ─Contains→ Mesh. (Transform→Transform→Mesh is
                        // invariant-legal; lowering composes nested Transforms.)
                        const aleph::types::NodeId xf_id = post.alloc_node_id();
                        post.insert_node(aleph::types::Node{aleph::types::Transform{
                            xf_id, 0,
                            aleph::types::LocalTransform{aleph::math::Mat4::identity()}}});
                        rec.created_nodes.push_back(xf_id);

                        const aleph::types::NodeId mesh_id = post.alloc_node_id();
                        aleph::types::Mesh mesh{};
                        mesh.id       = mesh_id;
                        mesh.geometry = o.geometry;
                        post.insert_node(aleph::types::Node{std::move(mesh)});
                        rec.created_nodes.push_back(mesh_id);

                        const aleph::types::NodeId mat_id = post.alloc_node_id();
                        post.insert_node(aleph::types::Node{
                            detail::material_from(mat_id, o.material)});
                        rec.created_nodes.push_back(mat_id);

                        // Reconstruct every surviving edge (insertion order).
                        for (auto [eid, e] : g.edges()) {
                            (void)eid;
                            auto r = post.add_edge(e.kind, e.src, e.dst);
                            if (!r.has_value()) {
                                return std::unexpected(OpError::InvariantViolation);
                            }
                        }

                        // Mesh —References→ Material (satisfies MaterialReferenced).
                        auto ref = post.add_edge(aleph::types::EdgeKind::References,
                                                 mesh_id, mat_id);
                        if (!ref.has_value()) {
                            return std::unexpected(OpError::EdgeTypeMismatch);
                        }
                        rec.created_edges.push_back(*ref);

                        // parent —Contains→ Transform —Contains→ Mesh.
                        auto pcon = post.add_edge(aleph::types::EdgeKind::Contains,
                                                  o.parent, xf_id);
                        if (!pcon.has_value()) {
                            return std::unexpected(OpError::EdgeTypeMismatch);
                        }
                        rec.created_edges.push_back(*pcon);
                        auto con = post.add_edge(aleph::types::EdgeKind::Contains,
                                                 xf_id, mesh_id);
                        if (!con.has_value()) {
                            return std::unexpected(OpError::EdgeTypeMismatch);
                        }
                        rec.created_edges.push_back(*con);
                        return {};
```

Update the branch's doc comment (lines 505–510) to read "Create a per-object Transform + Mesh + Material and the edges that make the object whole and invariant-valid (parent→Transform→Mesh, Mesh→Material)."

- [ ] **Step 4: Build and run the new + existing AddObject tests**

Run: `cmake --build build-release && ./build-release/tests/aleph_tests --test-case="lowering: AddObject*"`
Expected: PASS — the new per-object-Transform case AND the three existing `AddObject*`/compose cases all pass (the existing ones check counts/geometry/byte-stable survivors, which an identity Transform leaves unchanged).

- [ ] **Step 5: Run the whole binary (catch any id/structure pins elsewhere)**

Run: `./build-release/tests/aleph_tests`
Expected: PASS. The existing `test_add_object` cases stay green by construction. `test_determinism` / `test_incremental` check **properties** (full==incremental, repeatable bytes), not absolute AddObject node-ids, so they should also stay green — but if any test pinned the pre-change id sequence of an AddObject result, update only the changed counts/ids (one extra Transform node per AddObject), never weakening an invariant check. Re-run until green.

- [ ] **Step 6: Commit**

```bash
git add bridge/src/aleph.lowering/aleph.lowering-ops.cppm tests/lowering/test_add_object.cpp
git commit -m "feat(lowering): AddObject mints a per-object Transform (parent->Transform->Mesh)"
```

---

### Task 5: Initial scene — a Transform per object

**Files:**
- Modify: `apps/aleph_edit/main.cpp:91-182` (`build_initial_graph`)

The app's interactive/headless scene must also give every mesh a private Transform, or `transform_of` returns `nullopt` for the starting objects. Transforms are identity, so the rendered image is byte-identical (no visual regression). Not unit-tested (app code); validated by build + the controller logic from Tasks 2/3 operating on it + the manual check in Task 10.

- [ ] **Step 1: Restructure the wiring**

In `build_initial_graph`, give the sphere, metal sphere, glass sphere, and floor each an identity Transform between `root` and the mesh. Replace the edge-wiring block (lines 172–181) and add four Transform nodes. Concretely, after each mesh is inserted, allocate a Transform; then wire `root→Transform`, `Transform→Mesh`. Final wiring section:

```cpp
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
```

(The four `g.alloc_node_id()` + `insert_node` calls can go just above this block, as shown. Leave the Camera and Light directly under `root` — moving them is out of scope.)

- [ ] **Step 2: Build and smoke-render headless**

Run: `cmake --build build-release --target aleph_edit_app && ./build-release/apps/aleph_edit/aleph_edit --headless /tmp/edit_smoke && ls /tmp/edit_smoke | head`
Expected: builds; writes PPM pairs without error (the scene still lowers — Transforms are identity).

- [ ] **Step 3: Commit**

```bash
git add apps/aleph_edit/main.cpp
git commit -m "feat(aleph_edit): per-object identity Transforms in the initial scene"
```

---

### Task 6: Wire arrow-key nudge into `run_live`

**Files:**
- Modify: `apps/aleph_edit/main.cpp` (`run_live`, the event switch ~871-901 and the gesture→Op block ~905-931)

Camera-relative ground-plane nudge. Not unit-tested (interactive); the moved-the-right-thing logic is covered by Task 3. Validated by build + Task 10 manual check.

- [ ] **Step 1: Capture the nudge keys in the event switch**

In `run_live`, add nudge accumulators next to the existing `key_*` flags (around line 865-866):

```cpp
        int nudge_dx = 0;   // -1 left, +1 right (camera right axis)
        int nudge_dz = 0;   // -1 toward camera, +1 away (camera forward axis)
        int nudge_dy = 0;   // -1 down, +1 up (world Y)
        bool nudge_fast = false;
```

In the `Event::Kind::KeyDown` case (after the existing `'k'` handling, line ~879), add:

```cpp
                    else if (e.key == aleph::window::key::Left)  nudge_dx = -1;
                    else if (e.key == aleph::window::key::Right) nudge_dx = +1;
                    else if (e.key == aleph::window::key::Up)    nudge_dz = +1;
                    else if (e.key == aleph::window::key::Down)  nudge_dz = -1;
                    else if (e.key == 'q') nudge_dy = -1;
                    else if (e.key == 'e') nudge_dy = +1;
                    if (e.shift) nudge_fast = true;
```

- [ ] **Step 2: Emit the translate in the gesture→Op block**

After the `key_kick` handling (line ~931), add:

```cpp
        // Arrow/Q-E nudge: move the selected object along camera-relative ground
        // axes (←/→ = camera right, ↑/↓ = camera forward on XZ) and world Y (Q/E).
        if (controller.selected().has_value()
            && (nudge_dx != 0 || nudge_dz != 0 || nudge_dy != 0)) {
            const Vec3 eye = controller.camera().look_from();
            const Vec3 tgt = controller.camera().look_at();
            Vec3 fwd{tgt.x - eye.x, 0.0f, tgt.z - eye.z};     // project to ground
            const f32 fl = std::sqrt(fwd.x * fwd.x + fwd.z * fwd.z);
            if (fl > 1e-5f) { fwd.x /= fl; fwd.z /= fl; }
            else            { fwd = Vec3{0.0f, 0.0f, -1.0f}; } // looking straight down
            const Vec3 right{ -fwd.z, 0.0f, fwd.x };           // cross(fwd, +Y)
            const f32 step = (nudge_fast ? 0.5f : 0.1f);
            const Vec3 delta{
                (right.x * static_cast<f32>(nudge_dx)
                 + fwd.x * static_cast<f32>(nudge_dz)) * step,
                static_cast<f32>(nudge_dy) * step,
                (right.z * static_cast<f32>(nudge_dx)
                 + fwd.z * static_cast<f32>(nudge_dz)) * step};
            (void)controller.translate_selected(delta);
        }
```

(`<cmath>` is already included for `--wave`; `Vec3`/`f32` aliases are already in scope in this file.)

- [ ] **Step 3: Build (functional + strict)**

Run: `cmake --build build-release --target aleph_edit_app && cmake --build build-release-strict 2>&1 | grep "warning:" | grep -v '^ninja:' | wc -l`
Expected: builds; warnings `0`.

- [ ] **Step 4: Commit**

```bash
git add apps/aleph_edit/main.cpp
git commit -m "feat(aleph_edit): arrow/Q-E nudge moves the selected object (camera-relative)"
```

---

### Task 7: `draw_selection_outline` (pure post-process)

**Files:**
- Create: `render/src/aleph.render.sw/aleph.render.sw-outline.cppm`
- Modify: `render/src/aleph.render.sw/aleph.render.sw.cppm` (export the partition)
- Create: `tests/render/test_sw_outline.cpp`
- Modify: `tests/CMakeLists.txt` (register)

- [ ] **Step 1: Write the failing test**

Create `tests/render/test_sw_outline.cpp`:

```cpp
#include "doctest.h"

#include <vector>

import aleph.math;          // Vec3
import aleph.render.common; // Film
import aleph.render.sw;     // draw_selection_outline

using aleph::math::Vec3;
using aleph::math::f32;

TEST_CASE("draw_selection_outline rings the covered silhouette, not the inside") {
    constexpr int W = 9, H = 9;
    std::vector<Vec3> px(static_cast<std::size_t>(W) * H, Vec3{0.0f, 0.0f, 0.0f});
    aleph::render::common::Film fb{px.data(), W, H, W};

    // Coverage: a 3x3 block at rows/cols [3..5] (depth > 0 == covered).
    std::vector<f32> sel_depth(static_cast<std::size_t>(W) * H, 0.0f);
    for (int y = 3; y <= 5; ++y)
        for (int x = 3; x <= 5; ++x)
            sel_depth[static_cast<std::size_t>(y) * W + x] = 1.0f;

    const Vec3 orange{1.0f, 0.5f, 0.1f};
    aleph::render::sw::draw_selection_outline(fb, sel_depth, /*radius=*/1, orange);

    auto at = [&](int x, int y) -> Vec3 { return px[static_cast<std::size_t>(y) * W + x]; };

    // A covered (inside) pixel is NOT painted (outline lives OUTSIDE the silhouette).
    CHECK(at(4, 4).x == doctest::Approx(0.0f));
    // A pixel just outside the block, adjacent to coverage, IS painted.
    CHECK(at(2, 4).x == doctest::Approx(1.0f));   // left of the block
    CHECK(at(4, 2).y == doctest::Approx(0.5f));   // above the block
    CHECK(at(6, 6).z == doctest::Approx(0.1f));   // diagonal corner (Chebyshev r=1)
    // A far pixel (distance > radius from any covered pixel) is untouched.
    CHECK(at(0, 0).x == doctest::Approx(0.0f));
}
```

- [ ] **Step 2: Register + build to confirm it fails**

Add `render/test_sw_outline.cpp` to `tests/CMakeLists.txt` (after `render/test_adaptive_spp.cpp`).
Run: `cmake --build build-release`
Expected: FAIL to compile — `draw_selection_outline` is not a member of `aleph::render::sw`.

- [ ] **Step 3: Implement the partition**

Create `render/src/aleph.render.sw/aleph.render.sw-outline.cppm`:

```cpp
module;
#include <span>
#include <cstddef>

export module aleph.render.sw:outline;

import aleph.math;
import aleph.render.common;

export namespace aleph::render::sw {

// Paint `color` into `fb` at every pixel that is NOT covered but lies within
// `radius` (Chebyshev) of a covered pixel — i.e. a ring hugging the OUTSIDE of
// the silhouette. Coverage is `sel_depth[y*W + x] > 0` (the depth written by a
// rasterize pass of only the selected entity's faces into a zero-cleared
// buffer). `sel_depth` is indexed y*fb.width + x (matching rasterize's depth);
// `fb` is written at y*fb.stride_pixels + x. No allocation, no read of `fb`'s
// existing colour. Caller runs this at SSAA resolution before the downsample so
// the box filter anti-aliases the ring for free.
inline void draw_selection_outline(aleph::render::common::Film& fb,
                                   std::span<const aleph::math::f32> sel_depth,
                                   int radius,
                                   aleph::math::Vec3 color) noexcept {
    const int W = fb.width, H = fb.height;
    auto covered = [&](int x, int y) noexcept -> bool {
        return sel_depth[static_cast<std::size_t>(y) * static_cast<std::size_t>(W)
                         + static_cast<std::size_t>(x)] > 0.0f;
    };
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (covered(x, y)) continue;             // inside the silhouette
            bool edge = false;
            for (int dy = -radius; dy <= radius && !edge; ++dy) {
                const int ny = y + dy;
                if (ny < 0 || ny >= H) continue;
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int nx = x + dx;
                    if (nx < 0 || nx >= W) continue;
                    if (covered(nx, ny)) { edge = true; break; }
                }
            }
            if (edge) {
                fb.pixels[static_cast<std::size_t>(y)
                          * static_cast<std::size_t>(fb.stride_pixels)
                          + static_cast<std::size_t>(x)] = color;
            }
        }
    }
}

}  // namespace aleph::render::sw
```

- [ ] **Step 4: Export the partition**

In `render/src/aleph.render.sw/aleph.render.sw.cppm`, add `export import :outline;` alongside the other `export import :…` lines (next to `:rasterize`).

- [ ] **Step 5: Build and run**

Run: `cmake --build build-release && ./build-release/tests/aleph_tests --test-case="draw_selection_outline rings the covered silhouette, not the inside"`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add render/src/aleph.render.sw/aleph.render.sw-outline.cppm render/src/aleph.render.sw/aleph.render.sw.cppm tests/render/test_sw_outline.cpp tests/CMakeLists.txt
git commit -m "feat(render.sw): draw_selection_outline (silhouette ring from a coverage buffer)"
```

---

### Task 8: Wire the outline into `run_live`

**Files:**
- Modify: `apps/aleph_edit/main.cpp` (`run_live`: scratch buffers ~806; raster block `1010-1018` — `rasterize` then `downsample_box(ss_film, film, kSSAA)` at line 1018)

Second raster pass of only the selected entity's faces → coverage depth → `draw_selection_outline` into `ss_film` before the existing downsample. Not unit-tested (app); the outline math is covered by Task 7.

- [ ] **Step 1: Allocate scratch buffers for the selection pass**

Near the existing SSAA scratch allocation in `run_live` (the `ss_px`/`ss_depth` block, ~line 806), add:

```cpp
    // Selection-outline scratch: a throwaway colour film + a depth buffer that
    // receive a raster pass of ONLY the selected entity's faces (coverage).
    std::vector<Vec3> sel_px(static_cast<std::size_t>(kSSAA) * kSSAA * W * H);
    std::vector<f32>  sel_depth(static_cast<std::size_t>(kSSAA) * kSSAA * W * H, 0.0f);
    aleph::render::common::Film sel_film{sel_px.data(), kSSAA * W, kSSAA * H, kSSAA * W};
    aleph::render::sw::SceneRT sel_scene;   // rebuilt per frame from the selection
```

- [ ] **Step 2: Draw the outline after the main raster, before downsample**

Find the raster block in `run_live` that calls `aleph::render::sw::rasterize(controller.raster_scene(), ...)` into `ss_film` and then `downsample_box(ss_film, film, kSSAA)`. **Between** the `rasterize` call and the `downsample_box` call, insert:

```cpp
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
                aleph::render::sw::draw_selection_outline(
                    ss_film, sel_depth, kSSAA, Vec3{1.0f, 0.55f, 0.1f});
            }
        }
        aleph::render::sw::downsample_box(ss_film, film, kSSAA);
```

(This block ends with the `downsample_box(ss_film, film, kSSAA)` call, so delete the pre-existing standalone `downsample_box(ss_film, film, kSSAA);` line it replaces — it must not run twice. `controller.face_source()` (`aleph.edit-controller.cppm:410`) returns `const std::vector<NodeId>&`, 1:1 with `raster_scene().faces`; both are confirmed public accessors.)

- [ ] **Step 3: Build (functional + strict)**

Run: `cmake --build build-release --target aleph_edit_app && cmake --build build-release-strict 2>&1 | grep "warning:" | grep -v '^ninja:' | wc -l`
Expected: builds; warnings `0`.

- [ ] **Step 4: Commit**

```bash
git add apps/aleph_edit/main.cpp
git commit -m "feat(aleph_edit): selection silhouette outline via a second raster pass"
```

---

### Task 9: Discoverability — hints + offset readout

**Files:**
- Modify: `apps/aleph_edit/main.cpp` (`run_live` UI panel block, the key-hint `ui_label` calls ~1043-1047)

Surface the new keys and the selected object's accumulated offset. Reuses `ui_label`; not unit-tested.

- [ ] **Step 1: Grow the panel and add a move-keys hint + offset line**

In the UI panel block of `run_live` (the `ui_panel`/`ui_label` calls at `apps/aleph_edit/main.cpp:1024-1047`): bump the panel height so the new lines fit. Change line 1024 from:

```cpp
            aleph::editor::ui_panel(ui, W - 250, 50, 240, 210, "EDITOR");
```
to:
```cpp
            aleph::editor::ui_panel(ui, W - 250, 50, 240, 260, "EDITOR");
```

Then, immediately after the existing hint labels (after line 1047, the `"X DELETE  DRAG ORBIT"` label, before `ui_end`), insert:

```cpp
            aleph::editor::ui_label(ui, W - 242, 246,
                                    "ARROWS MOVE  Q/E UP-DN  SHIFT FAST",
                                    Vec3{0.7f, 0.7f, 0.7f});
            if (controller.selected().has_value()) {
                if (auto tid = controller.transform_of(*controller.selected())) {
                    const aleph::types::Node* tn = controller.graph().node(*tid);
                    const aleph::math::Mat4& tm =
                        std::get<aleph::types::Transform>(*tn).local.m;
                    char off[48];
                    std::snprintf(off, sizeof(off), "OFFSET %.2f %.2f %.2f",
                                  static_cast<double>(tm(0, 3)),
                                  static_cast<double>(tm(1, 3)),
                                  static_cast<double>(tm(2, 3)));
                    aleph::editor::ui_label(ui, W - 242, 262, off,
                                            Vec3{0.9f, 0.8f, 0.5f});
                }
            }
```

(`std::snprintf` (`<cstdio>`), `std::get`/`std::variant`, and `aleph::types::Transform` are all already used in this file. The panel now spans y=[50, 310]; the two new labels at y=246/262 fit inside it.)

- [ ] **Step 2: Build (functional + strict)**

Run: `cmake --build build-release --target aleph_edit_app && cmake --build build-release-strict 2>&1 | grep "warning:" | grep -v '^ninja:' | wc -l`
Expected: builds; warnings `0`.

- [ ] **Step 3: Commit**

```bash
git add apps/aleph_edit/main.cpp
git commit -m "feat(aleph_edit): panel hints for move keys + selected-object offset readout"
```

---

### Task 10: Gates + manual verification + finish

**Files:** none (verification only)

- [ ] **Step 1: Full functional suite**

Run: `cmake --build build-release && ctest --test-dir build-release`
Expected: 20/20 (the new test cases run inside the `aleph_tests` suite).

- [ ] **Step 2: Strict warnings gate**

Run: `cmake --build build-release-strict 2>&1 | grep "warning:" | grep -v '^ninja:' | wc -l`
Expected: `0`.

- [ ] **Step 3: ASan/UBSan gate**

Run (from repo root): `cmake --build build-asan && LSAN_OPTIONS=suppressions=tests/asan.supp ASAN_OPTIONS=detect_leaks=1 ./build-asan/tests/aleph_tests`
Expected: all green, no leaks.

- [ ] **Step 4: Manual check (if a display is available)**

Run: `./build-release/apps/aleph_edit/aleph_edit`
Verify: click the red sphere → an orange silhouette outline appears; arrow keys slide it on the floor relative to the view; `Q`/`E` raise/lower it; holding Shift moves faster; the panel shows `OFFSET` updating; other objects do not move.
(If headless-only, state that this step was skipped and rely on Tasks 2/3/7 unit coverage.)

- [ ] **Step 5: Finish the branch**

Use **superpowers:finishing-a-development-branch** to merge `editor-direct-manip` → `main`, push, and clean up.

---

## Self-Review notes (filled during writing)

- **Spec coverage:** Pieza 1 → Tasks 4,5; Pieza 2 → Tasks 1,2,3,6; Pieza 3 → Tasks 7,8; Pieza 4 → Task 9; testing/gates → Task 10. All spec pieces mapped.
- **Mechanism refinement vs spec:** the spec proposed an optional `id_out` param on `rasterize`; this plan instead uses a **second raster pass + pure `draw_selection_outline`** (Task 7/8). Same outcome (anti-aliased silhouette), but zero change to the shared hot `rast_scan_textured` and its existing tests — strictly lower risk. The outline is x-ray (shows through occluders), which is desirable for a selection indicator.
- **Type consistency:** `transform_of` / `translate_selected` / `selected()` / `graph()` / `camera()` / `raster_scene()` / `face_source()` are all `EditorController` members used consistently; `Mat4::translate`, `Mat4::operator()(r,c)` (col-major, translation at `m(·,3)`), `LocalTransform{Mat4}`, `Op{SetTransform{NodeId, LocalTransform}}`, `OpError::KindMismatch`, `aleph::window::key::{Left,Right,Up,Down}` all match the real signatures verified in the source.

# Aleph Editor Workflow Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make editor project load/save, OBJ import, undo/redo, and refine/save key workflow test-backed while preserving the graph as the only semantic truth.

**Architecture:** Add `ImportObj` to the existing `aleph.lowering::Op` vocabulary as a transactional graph edit, then let `EditorController` own graph-history snapshots and rederive all render/sim state after history jumps. Keep `apps/aleph_edit` as a thin shell that parses CLI/key gestures into graph I/O or `Op`s.

**Tech Stack:** C++26 modules, doctest, CMake/Ninja, `std::expected`, existing `aleph.graph` serialization, `aleph.io::load_obj`, `aleph.lowering`, `aleph.edit`, and `apps/aleph_edit`.

---

## File Structure

- `bridge/src/aleph.lowering/aleph.lowering-ops.cppm`
  - Owns editor `Op` vocabulary and transactional graph mutations. Add `ImportObj`.
- `bridge/src/aleph.lowering/CMakeLists.txt`
  - Link `aleph_io` because `ImportObj` parses OBJ bytes.
- `tests/lowering/test_import_obj.cpp`
  - New focused tests for `ImportObj` behavior, rollback, cap, and material preservation.
- `tests/CMakeLists.txt`
  - Add the new lowering test source.
- `bridge/src/aleph.edit/aleph.edit-controller.cppm`
  - Owns the graph truth and derived state. Add undo/redo graph snapshots and history restoration.
- `tests/edit/test_controller.cpp`
  - Extend controller tests with undo/redo graph truth and redo clearing.
- `tests/edit/test_sim_controller.cpp`
  - Extend sim tests with undo/redo history jumps while sim is enabled.
- `tests/edit/test_project_workflow.cpp`
  - New app-adjacent workflow tests for import -> save -> load without SDL.
- `apps/aleph_edit/CMakeLists.txt`
  - Link app shell against `aleph_io` and `aleph_dpo`.
- `apps/aleph_edit/main.cpp`
  - Add CLI parsing for `--load`, `--save`, `--import`; add live keys `u`, `y`, `r`, `s`; keep shell thin.

## Current Draft Note

The worktree already contains an uncommitted draft touching the B files above.
Do not revert it. Treat it as candidate implementation, not accepted behavior.
Each task must:

1. Add or update tests first.
2. Run the targeted tests.
3. If the draft already makes the new tests pass, stage only the relevant draft
   hunks plus tests.
4. If tests fail, fix only the task-owned files.
5. Commit only the task-owned files.

Pre-existing unrelated dirty/untracked files may remain in `git status`. Do not
stage them unless they are explicitly listed in the task.

## Task 1: Transactional ImportObj Op

**Files:**
- Create: `tests/lowering/test_import_obj.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `bridge/src/aleph.lowering/aleph.lowering-ops.cppm`
- Modify: `bridge/src/aleph.lowering/CMakeLists.txt`

- [ ] **Step 1: Add the failing ImportObj tests**

Create `tests/lowering/test_import_obj.cpp`:

```cpp
#include "doctest.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

import aleph.math;
import aleph.types;
import aleph.graph;
import aleph.lowering;

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Vec3;

namespace {

struct Seed {
    Graph  g;
    NodeId root{}, cam{};
};

Seed make_seed() {
    Seed s;
    Graph& g = s.g;

    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, LocalTransform{aleph::math::Mat4::identity()}});

    s.cam = g.alloc_node_id();
    Camera cam{s.cam, std::string("sensor0")};
    cam.look_from = Vec3{0, 0, 5};
    cam.look_at   = Vec3{0, 0, 0};
    cam.up        = Vec3{0, 1, 0};
    cam.vfov_deg  = 40.0f;
    g.insert_node(std::move(cam));

    (void)g.add_edge(EdgeKind::Contains, s.root, s.cam);
    return s;
}

std::vector<std::byte> bytes_of(const char* text) {
    std::vector<std::byte> out;
    while (*text != '\0') {
        out.push_back(static_cast<std::byte>(*text));
        ++text;
    }
    return out;
}

std::vector<std::byte> one_triangle_obj() {
    return bytes_of(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
}

aleph::lowering::MaterialParams imported_material() {
    aleph::lowering::MaterialParams m{};
    m.kind     = MaterialKind::TexturedLambertian;
    m.albedo   = Vec3{0.25f, 0.5f, 0.75f};
    m.fuzz     = 0.0f;
    m.ior      = 1.5f;
    m.emit     = Vec3{0, 0, 0};
    m.uv_scale = 7.0f;
    return m;
}

}  // namespace

TEST_CASE("lowering: ImportObj creates one TriLocal entity and shared material") {
    Seed s = make_seed();
    const std::size_t nodes_before = s.g.node_count();
    const std::size_t edges_before = s.g.edge_count();

    aleph::lowering::ImportObj imp{};
    imp.parent    = s.root;
    imp.obj_bytes = one_triangle_obj();
    imp.material  = imported_material();

    auto applied = aleph::lowering::apply_op(s.g, aleph::lowering::Op{std::move(imp)});
    REQUIRE(applied.has_value());

    CHECK(s.g.node_count() == nodes_before + 3);  // group Transform + Material + Mesh
    CHECK(s.g.edge_count() == edges_before + 3);  // root->group, group->mesh, mesh->mat
    REQUIRE(applied->created_nodes.size() == 3);
    REQUIRE(applied->created_edges.size() == 3);

    auto lowered = aleph::lowering::lower(s.g);
    REQUIRE(lowered.has_value());
    REQUIRE(lowered->entities.size() == 1);

    const aleph::lowering::LoweredEntity& e = lowered->entities[0];
    CHECK(e.material.kind == MaterialKind::TexturedLambertian);
    CHECK(e.material.albedo == Vec3{0.25f, 0.5f, 0.75f});
    CHECK(e.material.uv_scale == doctest::Approx(7.0f));
    REQUIRE(std::holds_alternative<TriLocal>(e.world_geometry));
    const TriLocal& tri = std::get<TriLocal>(e.world_geometry);
    CHECK(tri.a == Vec3{0, 0, 0});
    CHECK(tri.b == Vec3{1, 0, 0});
    CHECK(tri.c == Vec3{0, 1, 0});
}

TEST_CASE("lowering: ImportObj invalid OBJ rolls back graph") {
    Seed s = make_seed();
    const std::size_t nodes_before = s.g.node_count();
    const std::size_t edges_before = s.g.edge_count();

    aleph::lowering::ImportObj imp{};
    imp.parent    = s.root;
    imp.obj_bytes = bytes_of("f 1 2 3\n");
    imp.material  = imported_material();

    auto applied = aleph::lowering::apply_op(s.g, aleph::lowering::Op{std::move(imp)});
    REQUIRE_FALSE(applied.has_value());
    CHECK(applied.error() == aleph::lowering::OpError::InvariantViolation);
    CHECK(s.g.node_count() == nodes_before);
    CHECK(s.g.edge_count() == edges_before);
}

TEST_CASE("lowering: ImportObj validates parent") {
    Seed s = make_seed();

    aleph::lowering::ImportObj missing{};
    missing.parent    = NodeId{999999};
    missing.obj_bytes = one_triangle_obj();
    missing.material  = imported_material();
    auto no_parent = aleph::lowering::apply_op(s.g, aleph::lowering::Op{std::move(missing)});
    REQUIRE_FALSE(no_parent.has_value());
    CHECK(no_parent.error() == aleph::lowering::OpError::NodeNotFound);

    aleph::lowering::ImportObj wrong_kind{};
    wrong_kind.parent    = s.cam;
    wrong_kind.obj_bytes = one_triangle_obj();
    wrong_kind.material  = imported_material();
    auto not_transform = aleph::lowering::apply_op(
        s.g, aleph::lowering::Op{std::move(wrong_kind)});
    REQUIRE_FALSE(not_transform.has_value());
    CHECK(not_transform.error() == aleph::lowering::OpError::KindMismatch);
}

TEST_CASE("lowering: ImportObj rejects triangle cap overflow") {
    Seed s = make_seed();
    std::string obj =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n";
    for (int i = 0; i < 4097; ++i) {
        obj += "f 1 2 3\n";
    }

    aleph::lowering::ImportObj imp{};
    imp.parent    = s.root;
    imp.obj_bytes = bytes_of(obj.c_str());
    imp.material  = imported_material();

    auto applied = aleph::lowering::apply_op(s.g, aleph::lowering::Op{std::move(imp)});
    REQUIRE_FALSE(applied.has_value());
    CHECK(applied.error() == aleph::lowering::OpError::InvariantViolation);
}
```

Add the file to `tests/CMakeLists.txt` immediately after `lowering/test_add_object.cpp`:

```cmake
    lowering/test_import_obj.cpp
```

- [ ] **Step 2: Run ImportObj tests and verify the current state**

Run:

```bash
cmake --build build --target aleph_tests -j$(nproc)
./build/tests/aleph_tests -tc="lowering: ImportObj*"
```

Expected on a clean pre-B base: build fails because `ImportObj` does not exist.
Expected with the current draft already present: tests may compile and pass. If
they pass, keep the implementation hunks but still stage only this task's files.

- [ ] **Step 3: Implement ImportObj in `aleph.lowering:ops`**

In `bridge/src/aleph.lowering/aleph.lowering-ops.cppm`, ensure the module
fragment includes:

```cpp
#include <span>
```

Ensure imports include:

```cpp
import aleph.io;      // load_obj (ImportObj)
```

Add this structural op after `AddObject`:

```cpp
struct ImportObj {
    types::NodeId          parent{};
    std::vector<std::byte> obj_bytes;
    MaterialParams         material{};
};
```

Update the `Op` variant:

```cpp
using Op = std::variant<SetTransform, SetMaterial,
                        AddObject, ImportObj, AddLight, DeleteObject, ApplyRule>;
```

Add this visitor branch immediately after the `AddObject` branch:

```cpp
            } else if constexpr (std::is_same_v<T, ImportObj>) {
                const aleph::types::Node* parent = g.node(o.parent);
                if (parent == nullptr) {
                    return std::unexpected(OpError::NodeNotFound);
                }
                if (aleph::types::kind_of(*parent) != aleph::types::NodeKind::Transform) {
                    return std::unexpected(OpError::KindMismatch);
                }
                auto mesh = aleph::io::load_obj(std::span<const std::byte>{
                    o.obj_bytes.data(), o.obj_bytes.size()});
                if (!mesh.has_value() || mesh->tris.empty()) {
                    return std::unexpected(OpError::InvariantViolation);
                }
                constexpr std::size_t kMaxTris = 4096;
                if (mesh->tris.size() > kMaxTris) {
                    return std::unexpected(OpError::InvariantViolation);
                }
                return detail::commit_structural(
                    g,
                    [&](aleph::graph::Graph& post, aleph::dpo::RewriteRecord& rec)
                        -> std::expected<void, OpError> {
                        detail::clone_nodes(g, post);

                        const aleph::types::NodeId group_xf = post.alloc_node_id();
                        post.insert_node(aleph::types::Node{aleph::types::Transform{
                            group_xf, 0,
                            aleph::types::LocalTransform{aleph::math::Mat4::identity()}}});
                        rec.created_nodes.push_back(group_xf);

                        const aleph::types::NodeId mat_id = post.alloc_node_id();
                        post.insert_node(aleph::types::Node{
                            detail::material_from(mat_id, o.material)});
                        rec.created_nodes.push_back(mat_id);

                        for (auto [eid, e] : g.edges()) {
                            (void)eid;
                            auto r = post.add_edge(e.kind, e.src, e.dst);
                            if (!r.has_value()) {
                                return std::unexpected(OpError::InvariantViolation);
                            }
                        }

                        auto pcon = post.add_edge(aleph::types::EdgeKind::Contains,
                                                  o.parent, group_xf);
                        if (!pcon.has_value()) {
                            return std::unexpected(OpError::EdgeTypeMismatch);
                        }
                        rec.created_edges.push_back(*pcon);

                        for (const auto& tri : mesh->tris) {
                            const aleph::types::NodeId mesh_id = post.alloc_node_id();
                            aleph::types::Mesh m{};
                            m.id           = mesh_id;
                            m.geometry_ref = "imported";
                            m.tris_count   = 1;
                            m.geometry     = aleph::types::TriLocal{
                                aleph::math::Vec3{
                                    mesh->verts[static_cast<std::size_t>(tri[0])].x,
                                    mesh->verts[static_cast<std::size_t>(tri[0])].y,
                                    mesh->verts[static_cast<std::size_t>(tri[0])].z},
                                aleph::math::Vec3{
                                    mesh->verts[static_cast<std::size_t>(tri[1])].x,
                                    mesh->verts[static_cast<std::size_t>(tri[1])].y,
                                    mesh->verts[static_cast<std::size_t>(tri[1])].z},
                                aleph::math::Vec3{
                                    mesh->verts[static_cast<std::size_t>(tri[2])].x,
                                    mesh->verts[static_cast<std::size_t>(tri[2])].y,
                                    mesh->verts[static_cast<std::size_t>(tri[2])].z},
                            };
                            post.insert_node(aleph::types::Node{std::move(m)});
                            rec.created_nodes.push_back(mesh_id);

                            auto ref = post.add_edge(aleph::types::EdgeKind::References,
                                                     mesh_id, mat_id);
                            if (!ref.has_value()) {
                                return std::unexpected(OpError::EdgeTypeMismatch);
                            }
                            rec.created_edges.push_back(*ref);

                            auto con = post.add_edge(aleph::types::EdgeKind::Contains,
                                                     group_xf, mesh_id);
                            if (!con.has_value()) {
                                return std::unexpected(OpError::EdgeTypeMismatch);
                            }
                            rec.created_edges.push_back(*con);
                        }
                        return {};
                    });
```

Update the final visitor `static_assert` to include `ImportObj`:

```cpp
                                  || std::is_same_v<T, ImportObj>
```

In `bridge/src/aleph.lowering/CMakeLists.txt`, add `aleph_io` to the public
`target_link_libraries` list for `aleph_lowering`.

- [ ] **Step 4: Run ImportObj tests and lowerings**

Run:

```bash
cmake --build build --target aleph_tests -j$(nproc)
./build/tests/aleph_tests -tc="lowering: ImportObj*"
./build/tests/aleph_tests -tc="lowering*"
```

Expected: ImportObj tests pass; all lowering tests pass.

- [ ] **Step 5: Commit ImportObj**

Run:

```bash
git add tests/CMakeLists.txt \
        tests/lowering/test_import_obj.cpp \
        bridge/src/aleph.lowering/aleph.lowering-ops.cppm \
        bridge/src/aleph.lowering/CMakeLists.txt
git commit -m "feat(lowering): add transactional OBJ import op"
```

Expected: commit contains only the four listed files.

## Task 2: Controller Undo/Redo

**Files:**
- Modify: `bridge/src/aleph.edit/aleph.edit-controller.cppm`
- Modify: `tests/edit/test_controller.cpp`
- Modify: `tests/edit/test_sim_controller.cpp`

- [ ] **Step 1: Add controller undo/redo tests**

Append these tests to `tests/edit/test_controller.cpp`:

```cpp
TEST_CASE("edit: undo and redo restore graph truth and lowered state") {
    TwoMesh ctl = make_two_mesh();
    aleph::edit::EditorController c{std::move(ctl.g)};

    REQUIRE_FALSE(c.can_undo());
    REQUIRE_FALSE(c.can_redo());
    const std::size_t base_entities = c.lowered().entities.size();
    const std::size_t base_nodes    = c.graph().node_count();

    aleph::lowering::AddObject add{};
    add.parent   = ctl.root;
    add.geometry = SphereLocal{Vec3{0, 2, 0}, 0.5f};
    add.material = green_lambertian();
    REQUIRE(c.apply(aleph::lowering::Op{add}).has_value());

    CHECK(c.can_undo());
    CHECK_FALSE(c.can_redo());
    CHECK(c.lowered().entities.size() == base_entities + 1);
    CHECK(c.graph().node_count() > base_nodes);
    CHECK(lowered_matches_full(c, c.graph()));

    REQUIRE(c.undo());
    CHECK(c.lowered().entities.size() == base_entities);
    CHECK(c.graph().node_count() == base_nodes);
    CHECK_FALSE(c.can_undo());
    CHECK(c.can_redo());
    CHECK(lowered_matches_full(c, c.graph()));
    CHECK(no_dangling_faces(c));

    REQUIRE(c.redo());
    CHECK(c.lowered().entities.size() == base_entities + 1);
    CHECK(c.can_undo());
    CHECK_FALSE(c.can_redo());
    CHECK(lowered_matches_full(c, c.graph()));
    CHECK(no_dangling_faces(c));
}

TEST_CASE("edit: undo followed by new apply clears redo") {
    TwoMesh ctl = make_two_mesh();
    aleph::edit::EditorController c{std::move(ctl.g)};

    aleph::lowering::AddObject add1{};
    add1.parent   = ctl.root;
    add1.geometry = SphereLocal{Vec3{0, 2, 0}, 0.5f};
    add1.material = green_lambertian();
    REQUIRE(c.apply(aleph::lowering::Op{add1}).has_value());
    REQUIRE(c.undo());
    REQUIRE(c.can_redo());

    aleph::lowering::AddObject add2{};
    add2.parent   = ctl.root;
    add2.geometry = SphereLocal{Vec3{0, -2, 0}, 0.5f};
    add2.material = green_lambertian();
    REQUIRE(c.apply(aleph::lowering::Op{add2}).has_value());

    CHECK(c.can_undo());
    CHECK_FALSE(c.can_redo());
    CHECK_FALSE(c.redo());
    CHECK(lowered_matches_full(c, c.graph()));
}

TEST_CASE("edit: failed apply does not push undo history") {
    TwoMesh ctl = make_two_mesh();
    aleph::edit::EditorController c{std::move(ctl.g)};

    const std::size_t nodes_before = c.graph().node_count();
    const std::size_t edges_before = c.graph().edge_count();

    aleph::lowering::AddObject bad{};
    bad.parent   = NodeId{999999};
    bad.geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
    bad.material = green_lambertian();

    auto r = c.apply(aleph::lowering::Op{bad});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == aleph::lowering::OpError::NodeNotFound);
    CHECK_FALSE(c.can_undo());
    CHECK_FALSE(c.can_redo());
    CHECK(c.graph().node_count() == nodes_before);
    CHECK(c.graph().edge_count() == edges_before);
    CHECK(lowered_matches_full(c, c.graph()));
}
```

Append this test to `tests/edit/test_sim_controller.cpp`:

```cpp
TEST_CASE("EditorController sim: undo/redo history jumps rebuild wave operator") {
    AB s = make_ab();
    const NodeId root = s.root;
    aleph::edit::EditorController ctl{std::move(s.g)};
    ctl.set_viewport(64, 48);

    ctl.enable_sim(true);
    const std::size_t base_nodes = ctl.graph().node_count();
    const std::size_t base_order = ctl.wave_operator().node_order.size();
    REQUIRE(base_order == 2);

    aleph::lowering::AddObject add{};
    add.parent   = root;
    add.geometry = SphereLocal{Vec3{2, 0, 0}, 0.4f};
    add.material = aleph::lowering::MaterialParams{};
    REQUIRE(ctl.apply(aleph::lowering::Op{add}).has_value());
    CHECK(ctl.graph().node_count() > base_nodes);
    CHECK(ctl.can_undo());

    REQUIRE(ctl.undo());
    CHECK(ctl.graph().node_count() == base_nodes);
    CHECK(ctl.wave_operator().node_order.size() == base_order);
    REQUIRE(ctl.step(0.01f).has_value());

    REQUIRE(ctl.redo());
    CHECK(ctl.graph().node_count() > base_nodes);
    CHECK(ctl.wave_operator().node_order.size() >= base_order);
    REQUIRE(ctl.step(0.01f).has_value());
}
```

- [ ] **Step 2: Run undo tests and verify failure/current state**

Run:

```bash
cmake --build build --target aleph_tests -j$(nproc)
./build/tests/aleph_tests -tc="edit: undo*" -tc="edit: failed apply*" -tc="EditorController sim: undo/redo*"
```

Expected on a clean pre-B base: build fails because `can_undo`, `can_redo`,
`undo`, and `redo` do not exist. Expected with the current draft: tests may pass.

- [ ] **Step 3: Implement controller history**

In `bridge/src/aleph.edit/aleph.edit-controller.cppm`, add public methods after
`pick()`:

```cpp
    [[nodiscard]] bool can_undo() const noexcept { return !undo_stack_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !redo_stack_.empty(); }

    [[nodiscard]] bool undo() {
        if (undo_stack_.empty()) return false;
        redo_stack_.push_back(graph_.clone());
        graph_ = std::move(undo_stack_.back());
        undo_stack_.pop_back();
        prev_graph_ = graph_.clone();
        restore_after_history_jump();
        return true;
    }

    [[nodiscard]] bool redo() {
        if (redo_stack_.empty()) return false;
        undo_stack_.push_back(graph_.clone());
        graph_ = std::move(redo_stack_.back());
        redo_stack_.pop_back();
        prev_graph_ = graph_.clone();
        restore_after_history_jump();
        return true;
    }
```

At the end of successful `apply()`, after `rebuild_backends_from_prev();` and
before `return {};`, add:

```cpp
        undo_stack_.push_back(std::move(prev_graph_));
        if (undo_stack_.size() > kMaxUndo) {
            undo_stack_.erase(undo_stack_.begin());
        }
        redo_stack_.clear();
```

In the private section, add:

```cpp
    static constexpr std::size_t kMaxUndo = 64;

    void restore_after_history_jump() {
        rebuild_full();
        if (sim_enabled_) rebuild_operator_and_reproject();
        selection_ = std::nullopt;
    }
```

Add private members next to `prev_graph_`:

```cpp
    std::vector<aleph::graph::Graph> undo_stack_{};
    std::vector<aleph::graph::Graph> redo_stack_{};
```

- [ ] **Step 4: Run controller undo tests**

Run:

```bash
cmake --build build --target aleph_tests -j$(nproc)
./build/tests/aleph_tests -tc="edit: undo*" -tc="edit: failed apply*" -tc="EditorController sim: undo/redo*"
./build/tests/aleph_tests -tc="edit:*" -tc="EditorController sim:*"
```

Expected: targeted undo tests pass and existing edit/controller tests pass.

- [ ] **Step 5: Commit controller history**

Run:

```bash
git add bridge/src/aleph.edit/aleph.edit-controller.cppm \
        tests/edit/test_controller.cpp \
        tests/edit/test_sim_controller.cpp
git commit -m "feat(edit): add graph undo redo history"
```

Expected: commit contains only the three listed files.

## Task 3: Project Workflow Tests

**Files:**
- Create: `tests/edit/test_project_workflow.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add import/save/load workflow tests**

Create `tests/edit/test_project_workflow.cpp`:

```cpp
#include "doctest.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

import aleph.math;
import aleph.types;
import aleph.graph;
import aleph.lowering;

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Vec3;

namespace {

struct Seed {
    Graph  g;
    NodeId root{}, cam{};
};

Seed make_seed() {
    Seed s;
    Graph& g = s.g;

    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, LocalTransform{aleph::math::Mat4::identity()}});

    s.cam = g.alloc_node_id();
    Camera cam{s.cam, std::string("sensor0")};
    cam.look_from = Vec3{0, 0, 5};
    cam.look_at   = Vec3{0, 0, 0};
    cam.up        = Vec3{0, 1, 0};
    cam.vfov_deg  = 40.0f;
    g.insert_node(std::move(cam));
    (void)g.add_edge(EdgeKind::Contains, s.root, s.cam);
    return s;
}

std::vector<std::byte> bytes_of(const char* text) {
    std::vector<std::byte> out;
    while (*text != '\0') {
        out.push_back(static_cast<std::byte>(*text));
        ++text;
    }
    return out;
}

std::filesystem::path unique_project_path(const char* name) {
    return std::filesystem::temp_directory_path()
        / (std::string("aleph_") + name + "_" + std::to_string(::getpid()) + ".aleph");
}

}  // namespace

TEST_CASE("project workflow: import OBJ, save graph, load graph, lower imported tri") {
    Seed s = make_seed();

    aleph::lowering::MaterialParams mat{};
    mat.kind     = MaterialKind::Lambertian;
    mat.albedo   = Vec3{0.6f, 0.7f, 0.8f};
    mat.uv_scale = 4.0f;

    aleph::lowering::ImportObj imp{};
    imp.parent = s.root;
    imp.obj_bytes = bytes_of(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
    imp.material = mat;
    REQUIRE(aleph::lowering::apply_op(s.g, aleph::lowering::Op{std::move(imp)}).has_value());

    const std::filesystem::path path = unique_project_path("project_workflow");
    REQUIRE(aleph::graph::save_graph_file(s.g, s.root, path.string()).has_value());
    auto loaded = aleph::graph::load_graph_file(path.string());
    REQUIRE(loaded.has_value());
    CHECK(loaded->root == s.root);

    auto lowered = aleph::lowering::lower(loaded->graph);
    REQUIRE(lowered.has_value());
    REQUIRE(lowered->entities.size() == 1);
    CHECK(lowered->entities[0].material.albedo == Vec3{0.6f, 0.7f, 0.8f});
    CHECK(lowered->entities[0].material.uv_scale == doctest::Approx(4.0f));
    CHECK(std::holds_alternative<TriLocal>(lowered->entities[0].world_geometry));

    std::filesystem::remove(path);
}
```

Add `edit/test_project_workflow.cpp` to `tests/CMakeLists.txt` immediately after
`edit/test_controller.cpp`:

```cmake
    edit/test_project_workflow.cpp
```

- [ ] **Step 2: Run project workflow test**

Run:

```bash
cmake --build build --target aleph_tests -j$(nproc)
./build/tests/aleph_tests -tc="project workflow:*"
```

Expected: the project workflow test passes.

- [ ] **Step 3: Commit project workflow test**

Run:

```bash
git add tests/CMakeLists.txt tests/edit/test_project_workflow.cpp
git commit -m "test(edit): cover import save load project workflow"
```

Expected: commit contains only the two listed files.

## Task 4: Editor App CLI And Live Bindings

**Files:**
- Modify: `apps/aleph_edit/CMakeLists.txt`
- Modify: `apps/aleph_edit/main.cpp`

- [ ] **Step 1: Wire app dependencies**

In `apps/aleph_edit/CMakeLists.txt`, ensure `target_link_libraries` includes
`aleph_io` and `aleph_dpo`:

```cmake
target_link_libraries(aleph_edit_app PRIVATE
    aleph_edit aleph_window aleph_editor
    aleph_render_sw aleph_render_rt aleph_render_common aleph_lowering
    aleph_graph aleph_types aleph_scene aleph_io aleph_dpo
    aleph_math aleph_threads aleph_alloc
    aleph_flags_test)
```

- [ ] **Step 2: Add app imports and launch option types**

In `apps/aleph_edit/main.cpp`, add:

```cpp
#include <limits>
```

Add module imports:

```cpp
import aleph.io;
import aleph.dpo;
```

Add these types near the existing scene factory helpers:

```cpp
struct LaunchOptions {
    std::optional<std::string> load_path;
    std::optional<std::string> save_path;
    std::optional<std::string> import_obj_path;
};

struct BootScene {
    aleph::graph::Graph g;
    NodeId              root{};
    NodeId              sphere{};
};

struct ObjImportInfo {
    Vec3        center{};
    f32         radius{5.0f};
    std::size_t tris{};
};
```

- [ ] **Step 3: Add graph boot/import/save helpers**

Add these helpers after `lambertian()`:

```cpp
BootScene build_minimal_graph() {
    BootScene boot;
    aleph::graph::Graph& g = boot.g;

    boot.root = g.alloc_node_id();
    g.insert_node(Transform{boot.root, 0, LocalTransform{Mat4::identity()}});

    const NodeId cam_id = g.alloc_node_id();
    Camera cam{cam_id, std::string("sensor0")};
    cam.look_from = Vec3{0.0f, 1.0f, 5.0f};
    cam.look_at   = Vec3{0.0f, 0.0f, 0.0f};
    cam.up        = Vec3{0.0f, 1.0f, 0.0f};
    cam.vfov_deg  = 45.0f;
    g.insert_node(std::move(cam));

    const NodeId light_id = g.alloc_node_id();
    Light light{light_id, LightKind::Area, std::string("emit0")};
    light.emission = Vec3{8.0f, 8.0f, 8.0f};
    light.geometry = QuadLocal{Vec3{-2.0f, 4.0f, -2.0f},
                               Vec3{4.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, 4.0f}};
    g.insert_node(std::move(light));

    (void)g.add_edge(EdgeKind::Contains, boot.root, cam_id);
    (void)g.add_edge(EdgeKind::Contains, boot.root, light_id);
    return boot;
}

void frame_camera_on_import(aleph::edit::OrbitCamera& cam, const ObjImportInfo& info) {
    cam.target   = info.center;
    cam.yaw      = 0.4f;
    cam.pitch    = 0.25f;
    cam.radius   = std::clamp(info.radius, 0.5f, 1.0e4f);
    cam.vfov_deg = 45.0f;
}

std::vector<std::byte> read_file_bytes(const std::string& path) {
    auto mapped = aleph::io::MappedFile::open_read(path);
    if (!mapped.has_value()) return {};
    const auto bytes = mapped->bytes();
    return std::vector<std::byte>(bytes.begin(), bytes.end());
}

BootScene boot_scene_from_options(const LaunchOptions& opts, bool wave_demo = false) {
    if (opts.load_path.has_value()) {
        auto loaded = aleph::graph::load_graph_file(*opts.load_path);
        if (!loaded.has_value()) {
            std::fprintf(stderr, "aleph_edit: cannot load graph from %s\n",
                         opts.load_path->c_str());
            std::exit(1);
        }
        BootScene boot{};
        boot.g    = std::move(loaded->graph);
        boot.root = loaded->root;
        for (auto [id, node] : boot.g.nodes()) {
            if (aleph::types::kind_of(node) == aleph::types::NodeKind::Mesh) {
                boot.sphere = id;
                break;
            }
        }
        return boot;
    }
    if (wave_demo) {
        LatticeScene ls = build_lattice_graph(7);
        return BootScene{std::move(ls.g), ls.root, ls.nodes[24]};
    }
    if (opts.import_obj_path.has_value()) {
        return build_minimal_graph();
    }
    InitialScene init = build_initial_graph();
    return BootScene{std::move(init.g), init.root, init.sphere};
}
```

Add `apply_import_obj()` and `save_project()` helpers. Use this exact behavior:

```cpp
bool apply_import_obj(aleph::edit::EditorController& controller, NodeId root,
                      const std::string& path, ObjImportInfo* out_info = nullptr) {
    const std::vector<std::byte> obj_bytes = read_file_bytes(path);
    if (obj_bytes.empty()) {
        std::fprintf(stderr, "aleph_edit: cannot read OBJ %s\n", path.c_str());
        return false;
    }

    auto parsed = aleph::io::load_obj(std::span<const std::byte>{
        obj_bytes.data(), obj_bytes.size()});
    if (!parsed.has_value()) {
        std::fprintf(stderr, "aleph_edit: invalid OBJ %s: %s\n",
                     path.c_str(), parsed.error().c_str());
        return false;
    }
    if (parsed->tris.empty()) {
        std::fprintf(stderr, "aleph_edit: OBJ %s has no triangles\n", path.c_str());
        return false;
    }
    constexpr std::size_t kMaxTris = 4096;
    if (parsed->tris.size() > kMaxTris) {
        std::fprintf(stderr,
                     "aleph_edit: OBJ %s has %zu triangles (max %zu)\n",
                     path.c_str(), parsed->tris.size(), kMaxTris);
        return false;
    }

    Vec3 bmin{ std::numeric_limits<f32>::max(),
               std::numeric_limits<f32>::max(),
               std::numeric_limits<f32>::max() };
    Vec3 bmax{ std::numeric_limits<f32>::lowest(),
               std::numeric_limits<f32>::lowest(),
               std::numeric_limits<f32>::lowest() };
    for (const aleph::io::Vec3f& v : parsed->verts) {
        bmin.x = std::min(bmin.x, v.x); bmax.x = std::max(bmax.x, v.x);
        bmin.y = std::min(bmin.y, v.y); bmax.y = std::max(bmax.y, v.y);
        bmin.z = std::min(bmin.z, v.z); bmax.z = std::max(bmax.z, v.z);
    }

    aleph::lowering::ImportObj imp{};
    imp.parent    = root;
    imp.obj_bytes = obj_bytes;
    imp.material  = lambertian(Vec3{0.7f, 0.7f, 0.75f});
    auto r = controller.apply(aleph::lowering::Op{std::move(imp)});
    if (!r.has_value()) {
        std::fprintf(stderr, "aleph_edit: ImportObj failed (OpError %d)\n",
                     static_cast<int>(r.error()));
        return false;
    }

    const Vec3 center{
        0.5f * (bmin.x + bmax.x),
        0.5f * (bmin.y + bmax.y),
        0.5f * (bmin.z + bmax.z),
    };
    const Vec3 ext{bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z};
    const f32 diag = std::sqrt(ext.x * ext.x + ext.y * ext.y + ext.z * ext.z);

    std::printf("aleph_edit: imported %zu triangles from %s\n",
                parsed->tris.size(), path.c_str());

    if (out_info != nullptr) {
        out_info->center = center;
        out_info->radius = std::max(1.5f, diag * 1.2f);
        out_info->tris   = parsed->tris.size();
    }
    return true;
}

bool save_project(const aleph::edit::EditorController& controller, NodeId root,
                  const std::string& path) {
    auto r = aleph::graph::save_graph_file(controller.graph(), root, path);
    if (!r.has_value()) {
        std::fprintf(stderr, "aleph_edit: cannot save graph to %s\n", path.c_str());
        return false;
    }
    std::printf("aleph_edit: saved graph to %s\n", path.c_str());
    return true;
}
```

- [ ] **Step 4: Thread launch options through headless/live**

Change:

```cpp
int run_headless(const std::string& outdir)
```

to:

```cpp
int run_headless(const std::string& outdir, const LaunchOptions& opts)
```

Inside `run_headless`, create the controller from `boot_scene_from_options(opts)`.
If `opts.import_obj_path` is present, call `apply_import_obj()` before rendering.
Frame the camera with `frame_camera_on_import()` when import succeeds. Save on
exit if `opts.save_path` is present.

Change:

```cpp
int run_live(bool wave_demo = false)
```

to:

```cpp
int run_live(bool wave_demo, const LaunchOptions& opts)
```

Create the controller from `boot_scene_from_options(opts, wave_demo)`. If import
is present, call `apply_import_obj()`. On imported non-wave scenes, frame the
camera with `frame_camera_on_import()`.

- [ ] **Step 5: Add live key bindings**

In the event loop local booleans, add:

```cpp
        bool key_undo = false, key_redo = false, key_refine = false, key_save = false;
```

In key handling, add:

```cpp
                    else if (e.key == 'u') key_undo = true;
                    else if (e.key == 'y') key_redo = true;
                    else if (e.key == 'r') key_refine = true;
                    else if (e.key == 's') key_save = true;
```

Before existing add/delete gesture handling, add:

```cpp
        if (key_undo)  (void)controller.undo();
        if (key_redo)  (void)controller.redo();
        if (key_save && opts.save_path.has_value()) {
            (void)save_project(controller, root, *opts.save_path);
        }
        if (key_refine) {
            (void)controller.apply(aleph::lowering::Op{
                aleph::lowering::ApplyRule{&aleph::dpo::rules::refine_cell()}});
        }
```

Before `run_live` returns, save on exit when requested:

```cpp
    if (opts.save_path.has_value()) {
        (void)save_project(controller, root, *opts.save_path);
    }
```

- [ ] **Step 6: Parse CLI options in `main()`**

Replace the current one-shot argument parsing with:

```cpp
    LaunchOptions opts;
    bool wave_live = false;
    std::optional<std::string> headless_outdir;
    std::optional<std::string> wave_outdir;
    std::optional<std::string> orbit_outdir;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--load") {
            if (i + 1 < argc) opts.load_path = std::string(argv[++i]);
        } else if (arg == "--save") {
            if (i + 1 < argc) opts.save_path = std::string(argv[++i]);
        } else if (arg == "--import") {
            if (i + 1 < argc) opts.import_obj_path = std::string(argv[++i]);
        } else if (arg == "--headless") {
            headless_outdir = (i + 1 < argc) ? std::string(argv[++i])
                                             : std::string("/tmp/edit_out");
        } else if (arg == "--wave") {
            wave_outdir = (i + 1 < argc) ? std::string(argv[++i])
                                         : std::string("/tmp/wave");
        } else if (arg == "--orbit-track") {
            orbit_outdir = (i + 1 < argc) ? std::string(argv[++i])
                                          : std::string("/tmp/orbit_track");
        } else if (arg == "--wave-live") {
            wave_live = true;
        } else {
            std::fprintf(stderr,
                         "aleph_edit: unknown argument '%.*s' "
                         "(use --import <file.obj> to load a model)\n",
                         static_cast<int>(arg.size()), arg.data());
        }
    }

    if (headless_outdir.has_value()) {
        return run_headless(*headless_outdir, opts);
    }
    if (wave_outdir.has_value()) {
        return run_wave(*wave_outdir);
    }
    if (orbit_outdir.has_value()) {
        return run_orbit_track(*orbit_outdir);
    }

#if defined(ALEPH_HAVE_SDL2)
    return run_live(wave_live, opts);
#else
    (void)wave_live;
    (void)opts;
```

- [ ] **Step 7: Build app and run CLI smoke**

Run:

```bash
cmake --build build --target aleph_tests aleph_edit_app -j$(nproc)
tmpdir=$(mktemp -d)
printf 'v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n' > "$tmpdir/tiny.obj"
./build/apps/aleph_edit/aleph_edit --headless "$tmpdir/out-import" --import "$tmpdir/tiny.obj" --save "$tmpdir/tiny.aleph"
test -s "$tmpdir/tiny.aleph"
./build/apps/aleph_edit/aleph_edit --headless "$tmpdir/out-load" --load "$tmpdir/tiny.aleph"
test -s "$tmpdir/out-import/step0_raster.ppm"
test -s "$tmpdir/out-load/step0_raster.ppm"
```

Expected: build succeeds; both headless runs exit 0; saved project and first
raster PPMs exist and are non-empty.

- [ ] **Step 8: Run targeted tests**

Run:

```bash
./build/tests/aleph_tests -tc="project workflow:*"
./build/tests/aleph_tests -tc="lowering: ImportObj*"
./build/tests/aleph_tests -tc="edit: undo*" -tc="edit: failed apply*"
```

Expected: all pass.

- [ ] **Step 9: Commit app workflow**

Run:

```bash
git add apps/aleph_edit/CMakeLists.txt apps/aleph_edit/main.cpp
git commit -m "feat(app): wire editor project IO and OBJ import"
```

Expected: commit contains only the app CMake and app main changes.

## Task 5: Final Verification

**Files:**
- No source changes expected.

- [ ] **Step 1: Build all required targets**

Run:

```bash
cmake --build build --target aleph_tests aleph_edit_app -j$(nproc)
```

Expected: build succeeds.

- [ ] **Step 2: Run targeted B tests**

Run:

```bash
./build/tests/aleph_tests -tc="lowering: ImportObj*"
./build/tests/aleph_tests -tc="edit: undo*" -tc="edit: failed apply*" -tc="EditorController sim: undo/redo*"
./build/tests/aleph_tests -tc="project workflow:*"
```

Expected: all targeted B tests pass.

- [ ] **Step 3: Run protected existing suites**

Run:

```bash
./build/tests/aleph_tests -tc="graph serialization*"
./build/tests/aleph_tests -tc="lowering*"
./build/tests/aleph_tests -tc="edit:*"
./build/tests/aleph_tests -tc="mv-controller:*"
```

Expected: all pass.

- [ ] **Step 4: Run app headless smoke**

Run:

```bash
tmpdir=$(mktemp -d)
printf 'v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n' > "$tmpdir/tiny.obj"
./build/apps/aleph_edit/aleph_edit --headless "$tmpdir/out-import" --import "$tmpdir/tiny.obj" --save "$tmpdir/tiny.aleph"
./build/apps/aleph_edit/aleph_edit --headless "$tmpdir/out-load" --load "$tmpdir/tiny.aleph"
test -s "$tmpdir/tiny.aleph"
test -s "$tmpdir/out-import/step0_raster.ppm"
test -s "$tmpdir/out-load/step0_raster.ppm"
```

Expected: every command exits 0.

- [ ] **Step 5: Run full suite and isolation tests**

Run:

```bash
timeout 300 ./build/tests/aleph_tests --duration=true
ctest --test-dir build -E '^aleph_tests$' --output-on-failure
```

Expected: full `aleph_tests` passes without exclusions; non-`aleph_tests`
isolation tests pass.

- [ ] **Step 6: Check staged/unrelated files**

Run:

```bash
git status --short --branch
git diff --check HEAD
```

Expected: no staged files. Any remaining dirty files should be explicitly
identified as pre-existing or intentionally deferred.

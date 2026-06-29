# Aleph Serialization Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Harden `aleph-graph/1` loading so malformed project files return structured errors instead of aborting, while preserving graph semantics and allocator determinism.

**Architecture:** Add minimal safe graph primitives (`try_insert_node`, direct allocator watermark sync), then make the serialization parser strict at the external-input boundary. Keep the file format unchanged and add focused tests for invalid input, full payload round trips, allocator behavior, and `uv_scale` preservation through op-created materials.

**Tech Stack:** C++26 modules, `std::expected`, doctest, CMake/Ninja, existing `aleph.graph`, `aleph.types`, `aleph.lowering` modules.

---

## File Structure

- `graph/src/aleph.types/aleph.types-id.cppm`
  - Owns `IdAllocator`. Add direct watermark synchronization methods so loaded high IDs do not require draining allocation loops.
- `graph/src/aleph.graph/aleph.graph-graph.cppm`
  - Owns `Graph`. Add non-fatal node insertion and direct allocator synchronization wrappers.
- `graph/src/aleph.graph/aleph.graph-serialization.cppm`
  - Owns `aleph-graph/1` save/load. Make parsing strict, require the header, use non-fatal node insertion, and validate trailing tokens.
- `bridge/src/aleph.lowering/aleph.lowering-ops.cppm`
  - Owns op material projection. Copy `MaterialParams::uv_scale` into fresh `Material` nodes.
- `tests/graph/test_graph_basic.cpp`
  - Add unit tests for `try_insert_node` and direct allocator synchronization.
- `tests/graph/test_graph_serialization.cpp`
  - Expand from happy-path topology tests to payload and invalid-input coverage.
- `tests/lowering/test_add_object.cpp`
  - Add a regression that `AddObject` preserves `uv_scale` through re-lowering.

## Task 1: Safe Graph Insert And Direct Allocator Sync

**Files:**
- Modify: `graph/src/aleph.types/aleph.types-id.cppm`
- Modify: `graph/src/aleph.graph/aleph.graph-graph.cppm`
- Test: `tests/graph/test_graph_basic.cpp`

- [ ] **Step 1: Write failing graph API tests**

Append these tests to `tests/graph/test_graph_basic.cpp`:

```cpp
TEST_CASE("Graph: try_insert_node rejects duplicate id without abort") {
    Graph g;
    const NodeId id = g.alloc_node_id();

    auto first = g.try_insert_node(Mesh{id, std::string("cube"), 12});
    REQUIRE(first.has_value());

    auto second = g.try_insert_node(Material{id, MaterialKind::Lambertian});
    REQUIRE_FALSE(second.has_value());
    CHECK(second.error() == GraphError::DuplicateNode);
    CHECK(g.node_count() == 1);
    const Node* n = g.node(id);
    REQUIRE(n != nullptr);
    CHECK(kind_of(*n) == NodeKind::Mesh);
}

TEST_CASE("Graph: sync_node_allocator advances directly past loaded ids") {
    Graph g;
    g.sync_node_allocator_to_at_least(1024);
    CHECK(g.alloc_node_id().value == 1024);
    CHECK(g.alloc_node_id().value == 1025);

    g.sync_node_allocator_to_at_least(12);
    CHECK(g.alloc_node_id().value == 1026);
}
```

- [ ] **Step 2: Run graph API tests and verify they fail**

Run:

```bash
cmake --build build --target aleph_tests -j$(nproc)
./build/tests/aleph_tests -tc="Graph: try_insert_node*" -tc="Graph: sync_node_allocator*"
```

Expected: build fails because `try_insert_node`, `GraphError::DuplicateNode`, and `sync_node_allocator_to_at_least` do not exist yet.

- [ ] **Step 3: Add direct watermark sync to `IdAllocator`**

In `graph/src/aleph.types/aleph.types-id.cppm`, add the two methods below after `edge_watermark()`:

```cpp
    void sync_node_to_at_least(std::uint32_t next) noexcept {
        if (node_next_ < next) node_next_ = next;
    }

    void sync_edge_to_at_least(std::uint32_t next) noexcept {
        if (edge_next_ < next) edge_next_ = next;
    }
```

- [ ] **Step 4: Add safe insertion and allocator wrappers to `Graph`**

In `graph/src/aleph.graph/aleph.graph-graph.cppm`, update `GraphError`:

```cpp
enum class GraphError {
    NodeNotFound,
    EdgeNotFound,
    EdgeTypeMismatch,
    DuplicateNode,
};
```

Replace `insert_node()` with this checked pair:

```cpp
    std::expected<void, GraphError> try_insert_node(aleph::types::Node n) {
        const aleph::types::NodeId id = aleph::types::id_of(n);
        const bool fresh = nodes_.insert(id, std::move(n));
        if (!fresh) return std::unexpected(GraphError::DuplicateNode);
        return {};
    }

    void insert_node(aleph::types::Node n) {
        auto r = try_insert_node(std::move(n));
        if (!r.has_value()) std::abort();
    }
```

Replace `sync_node_allocator()` with direct sync helpers:

```cpp
    void sync_node_allocator_to_at_least(std::uint32_t next) noexcept {
        ids_.sync_node_to_at_least(next);
    }

    void sync_edge_allocator_to_at_least(std::uint32_t next) noexcept {
        ids_.sync_edge_to_at_least(next);
    }

    void sync_node_allocator() noexcept {
        std::uint32_t max_id = 0;
        bool any = false;
        for (auto [nid, node] : nodes_) {
            (void)node;
            any = true;
            if (nid.value > max_id) max_id = nid.value;
        }
        if (any && max_id != UINT32_MAX) {
            sync_node_allocator_to_at_least(max_id + 1);
        }
    }
```

- [ ] **Step 5: Run graph API tests and verify they pass**

Run:

```bash
cmake --build build --target aleph_tests -j$(nproc)
./build/tests/aleph_tests -tc="Graph: try_insert_node*" -tc="Graph: sync_node_allocator*"
```

Expected: both test cases pass.

- [ ] **Step 6: Commit graph API changes**

```bash
git add graph/src/aleph.types/aleph.types-id.cppm \
        graph/src/aleph.graph/aleph.graph-graph.cppm \
        tests/graph/test_graph_basic.cpp
git commit -m "fix(graph): add safe node insertion and allocator sync"
```

## Task 2: Serialization Invalid-Input Tests

**Files:**
- Modify: `tests/graph/test_graph_serialization.cpp`

- [ ] **Step 1: Add reusable minimal graph text fixtures**

Inside the anonymous namespace in `tests/graph/test_graph_serialization.cpp`, add:

```cpp
constexpr const char* kIdentity =
    "1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1";

std::string minimal_graph_text() {
    return std::string{"aleph-graph/1\n"}
        + "root 0\n"
        + "node 0 transform 0 " + kIdentity + "\n"
        + "node 1 camera sensor0 0 1 5 0 0 0 0 1 0 45 0 1\n"
        + "edge contains 0 1\n";
}
```

- [ ] **Step 2: Add invalid-input tests**

Append these tests to `tests/graph/test_graph_serialization.cpp`:

```cpp
TEST_CASE("graph serialization: rejects missing header") {
    std::string text = minimal_graph_text();
    text.erase(0, std::string{"aleph-graph/1\n"}.size());

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == aleph::graph::SerializationError::InvalidHeader);
}

TEST_CASE("graph serialization: rejects duplicate node id without abort") {
    const std::string text = std::string{"aleph-graph/1\n"}
        + "root 0\n"
        + "node 0 transform 0 " + kIdentity + "\n"
        + "node 0 camera sensor0 0 1 5 0 0 0 0 1 0 45 0 1\n";

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == aleph::graph::SerializationError::InvalidNode);
}

TEST_CASE("graph serialization: rejects numeric token with trailing garbage") {
    std::string text = minimal_graph_text();
    const std::string bad = "node 1 camera sensor0 0 1 5x 0 0 0 0 1 0 45 0 1";
    const std::string good = "node 1 camera sensor0 0 1 5 0 0 0 0 1 0 45 0 1";
    const std::size_t pos = text.find(good);
    REQUIRE(pos != std::string::npos);
    text.replace(pos, good.size(), bad);

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == aleph::graph::SerializationError::ParseError);
}

TEST_CASE("graph serialization: rejects trailing fields on a valid line") {
    std::string text = minimal_graph_text();
    const std::string old_root = "root 0\n";
    const std::size_t pos = text.find(old_root);
    REQUIRE(pos != std::string::npos);
    text.replace(pos, old_root.size(), "root 0 extra\n");

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == aleph::graph::SerializationError::ParseError);
}

TEST_CASE("graph serialization: rejects missing root node") {
    std::string text = minimal_graph_text();
    const std::size_t pos = text.find("root 0\n");
    REQUIRE(pos != std::string::npos);
    text.replace(pos, std::string{"root 0\n"}.size(), "root 99\n");

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == aleph::graph::SerializationError::ParseError);
}

TEST_CASE("graph serialization: rejects invalid edge endpoint") {
    std::string text = minimal_graph_text();
    text += "edge contains 0 99\n";

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == aleph::graph::SerializationError::InvalidEdge);
}

TEST_CASE("graph serialization: rejects incompatible edge kind") {
    std::string text = minimal_graph_text();
    text += "edge references 0 1\n";

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == aleph::graph::SerializationError::InvalidEdge);
}

TEST_CASE("graph serialization: allocator advances past largest loaded node id") {
    const std::string text = std::string{"aleph-graph/1\n"}
        + "root 99\n"
        + "node 99 transform 0 " + kIdentity + "\n"
        + "node 100 camera sensor0 0 1 5 0 0 0 0 1 0 45 0 1\n"
        + "edge contains 99 100\n";

    auto loaded = aleph::graph::load_graph_string(text);
    REQUIRE(loaded.has_value());
    CHECK(loaded->graph.alloc_node_id().value == 101);
}
```

- [ ] **Step 3: Run serialization tests and verify failures**

Run:

```bash
cmake --build build --target aleph_tests -j$(nproc)
./build/tests/aleph_tests -tc="graph serialization*"
```

Expected: at least the duplicate-ID case aborts or fails before implementation; missing header and trailing garbage are accepted by the old parser and fail their checks.

- [ ] **Step 4: Commit failing tests only if your workflow allows red commits**

If keeping every commit green, skip this commit and include these tests with Task 3. If red commits are acceptable in your execution mode:

```bash
git add tests/graph/test_graph_serialization.cpp
git commit -m "test(graph): cover malformed graph serialization"
```

## Task 3: Strict Serialization Parser

**Files:**
- Modify: `graph/src/aleph.graph/aleph.graph-serialization.cppm`
- Test: `tests/graph/test_graph_serialization.cpp`

- [ ] **Step 1: Add strict parsing helpers**

In `graph/src/aleph.graph/aleph.graph-serialization.cppm`, add includes:

```cpp
#include <cerrno>
#include <cmath>
#include <limits>
```

Inside `namespace detail`, add:

```cpp
[[nodiscard]] inline bool rest_is_blank(std::string_view line) noexcept {
    for (char c : line) {
        if (c != ' ' && c != '\t') return false;
    }
    return true;
}
```

Update `append_string()` so empty strings and backslashes are quoted:

```cpp
inline void append_string(std::string& out, std::string_view s) {
    if (!s.empty()
        && s.find(' ') == std::string_view::npos
        && s.find('\t') == std::string_view::npos
        && s.find('"') == std::string_view::npos
        && s.find('\\') == std::string_view::npos) {
        out += s;
        return;
    }
    out += '"';
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    out += '"';
}
```

Update `next_token()` to skip tabs as whitespace:

```cpp
[[nodiscard]] inline std::string_view next_token(std::string_view& line) {
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
        line.remove_prefix(1);
    }
    if (line.empty()) return {};
    if (line.front() == '"') {
        line.remove_prefix(1);
        std::string_view rest = line;
        std::size_t i = 0;
        while (i < rest.size()) {
            if (rest[i] == '\\' && i + 1 < rest.size()) { i += 2; continue; }
            if (rest[i] == '"') break;
            ++i;
        }
        if (i >= rest.size()) return {};
        const std::string_view tok = rest.substr(0, i);
        line = rest.substr(i + 1);
        return tok;
    }
    std::size_t sp = 0;
    while (sp < line.size() && line[sp] != ' ' && line[sp] != '\t') ++sp;
    const std::string_view tok = line.substr(0, sp);
    line = (sp == line.size()) ? std::string_view{} : line.substr(sp + 1);
    return tok;
}
```

- [ ] **Step 2: Make numeric parsing consume full tokens**

Replace `parse_f32()` and `parse_u32()` with:

```cpp
[[nodiscard]] inline bool parse_f32(std::string_view tok, aleph::math::f32& out) {
    if (tok.empty()) return false;
    char buf[64];
    if (tok.size() >= sizeof(buf)) return false;
    std::memcpy(buf, tok.data(), tok.size());
    buf[tok.size()] = '\0';
    errno = 0;
    char* end = nullptr;
    const double v = std::strtod(buf, &end);
    if (end != buf + tok.size()) return false;
    if (errno == ERANGE || !std::isfinite(v)) return false;
    out = static_cast<aleph::math::f32>(v);
    return std::isfinite(out);
}

[[nodiscard]] inline bool parse_u32(std::string_view tok, std::uint32_t& out) {
    if (tok.empty()) return false;
    if (tok.front() == '-') return false;
    char buf[32];
    if (tok.size() >= sizeof(buf)) return false;
    std::memcpy(buf, tok.data(), tok.size());
    buf[tok.size()] = '\0';
    errno = 0;
    char* end = nullptr;
    const unsigned long v = std::strtoul(buf, &end, 10);
    if (end != buf + tok.size()) return false;
    if (errno == ERANGE || v > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    out = static_cast<std::uint32_t>(v);
    return true;
}
```

- [ ] **Step 3: Reject trailing tokens in node lines**

In `parse_node_line()`, before each successful `return aleph::types::Node{...};`, check `rest_is_blank(rest)`.

Example for `Mesh`:

```cpp
        if (!parse_geometry(rest, m.geometry)) {
            return std::unexpected(SerializationError::ParseError);
        }
        if (!rest_is_blank(rest)) return std::unexpected(SerializationError::ParseError);
        return aleph::types::Node{std::move(m)};
```

Apply the same pattern to `Material`, `Light`, `Volume`, `Camera`, `Texture`, and `Transform`.

- [ ] **Step 4: Require header and use safe node insertion**

Replace the top-level body of `load_graph_string()` with this structure:

```cpp
[[nodiscard]] inline std::expected<LoadedGraph, SerializationError>
load_graph_string(std::string_view text) {
    LoadedGraph loaded{};
    aleph::types::NodeId root{};
    bool have_root = false;
    bool have_header = false;
    std::uint32_t max_node_id = 0;
    bool have_node = false;
    std::vector<detail::PendingEdge> pending_edges;

    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = (end < text.size()) ? end + 1 : end;

        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.remove_suffix(1);
        }
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            line.remove_prefix(1);
        }
        if (line.empty() || line.front() == '#') continue;

        if (!have_header) {
            if (line != "aleph-graph/1") {
                return std::unexpected(SerializationError::InvalidHeader);
            }
            have_header = true;
            continue;
        }
        if (line == "aleph-graph/1") {
            return std::unexpected(SerializationError::InvalidHeader);
        }

        std::string_view rest = line;
        const std::string_view tag = detail::next_token(rest);
        if (tag == "root") {
            if (!detail::parse_node_id(detail::next_token(rest), root)) {
                return std::unexpected(SerializationError::ParseError);
            }
            if (!detail::rest_is_blank(rest)) {
                return std::unexpected(SerializationError::ParseError);
            }
            have_root = true;
            continue;
        }
        if (tag == "node") {
            auto node = detail::parse_node_line(line);
            if (!node.has_value()) return std::unexpected(node.error());
            const aleph::types::NodeId id = aleph::types::id_of(*node);
            auto inserted = loaded.graph.try_insert_node(std::move(*node));
            if (!inserted.has_value()) {
                return std::unexpected(SerializationError::InvalidNode);
            }
            have_node = true;
            if (id.value > max_node_id) max_node_id = id.value;
            continue;
        }
        if (tag == "edge") {
            detail::PendingEdge pe{};
            const auto kind = detail::parse_edge_kind(detail::next_token(rest));
            if (!kind.has_value()) return std::unexpected(SerializationError::ParseError);
            pe.kind = *kind;
            if (!detail::parse_node_id(detail::next_token(rest), pe.src)) {
                return std::unexpected(SerializationError::ParseError);
            }
            if (!detail::parse_node_id(detail::next_token(rest), pe.dst)) {
                return std::unexpected(SerializationError::ParseError);
            }
            if (!detail::rest_is_blank(rest)) {
                return std::unexpected(SerializationError::ParseError);
            }
            pending_edges.push_back(pe);
            continue;
        }
        return std::unexpected(SerializationError::ParseError);
    }

    if (!have_header) return std::unexpected(SerializationError::InvalidHeader);
    if (!have_root) return std::unexpected(SerializationError::InvalidHeader);
    if (loaded.graph.node(root) == nullptr) {
        return std::unexpected(SerializationError::ParseError);
    }

    if (have_node && max_node_id != std::numeric_limits<std::uint32_t>::max()) {
        loaded.graph.sync_node_allocator_to_at_least(max_node_id + 1);
    }

    for (const detail::PendingEdge& pe : pending_edges) {
        auto eid = loaded.graph.add_edge(pe.kind, pe.src, pe.dst);
        if (!eid.has_value()) return std::unexpected(SerializationError::InvalidEdge);
    }

    if (!validate_all(loaded.graph, static_cast<std::size_t>(-1)).has_value()) {
        return std::unexpected(SerializationError::InvariantViolation);
    }

    loaded.root = root;
    return loaded;
}
```

- [ ] **Step 5: Run serialization tests**

Run:

```bash
cmake --build build --target aleph_tests -j$(nproc)
./build/tests/aleph_tests -tc="graph serialization*"
```

Expected: all graph serialization tests pass.

- [ ] **Step 6: Commit strict parser changes**

```bash
git add graph/src/aleph.graph/aleph.graph-serialization.cppm \
        tests/graph/test_graph_serialization.cpp
git commit -m "fix(graph): harden graph serialization parser"
```

## Task 4: Full Payload Round Trip Coverage

**Files:**
- Modify: `tests/graph/test_graph_serialization.cpp`

- [ ] **Step 1: Add payload assertion helpers**

Inside the anonymous namespace in `tests/graph/test_graph_serialization.cpp`, add:

```cpp
const Node& require_node(const Graph& g, NodeId id) {
    const Node* n = g.node(id);
    REQUIRE(n != nullptr);
    return *n;
}

void check_vec3(aleph::math::Vec3 actual, aleph::math::Vec3 expected) {
    CHECK(actual.x == doctest::Approx(expected.x));
    CHECK(actual.y == doctest::Approx(expected.y));
    CHECK(actual.z == doctest::Approx(expected.z));
}
```

- [ ] **Step 2: Add a full payload round-trip test**

Append:

```cpp
TEST_CASE("graph serialization: round-trip preserves node payloads") {
    Graph g;
    const NodeId root = g.alloc_node_id();
    aleph::math::Mat4 xf = aleph::math::Mat4::identity();
    xf.m[12] = 2.0f;
    xf.m[13] = 3.0f;
    xf.m[14] = 4.0f;
    g.insert_node(Transform{root, 7, LocalTransform{xf}});

    const NodeId cam = g.alloc_node_id();
    Camera camera{cam, std::string("sensor with space")};
    camera.look_from = aleph::math::Vec3{1.0f, 2.0f, 3.0f};
    camera.look_at = aleph::math::Vec3{0.0f, 0.5f, -1.0f};
    camera.up = aleph::math::Vec3{0.0f, 1.0f, 0.0f};
    camera.vfov_deg = 55.0f;
    camera.aperture = 0.25f;
    camera.focus_dist = 8.0f;
    g.insert_node(std::move(camera));

    const NodeId mesh = g.alloc_node_id();
    Mesh m{mesh, std::string("tri mesh"), 1};
    m.geometry = TriLocal{
        aleph::math::Vec3{0.0f, 0.0f, 0.0f},
        aleph::math::Vec3{1.0f, 0.0f, 0.0f},
        aleph::math::Vec3{0.0f, 1.0f, 0.0f}};
    g.insert_node(std::move(m));

    const NodeId mat = g.alloc_node_id();
    Material material{mat, MaterialKind::TexturedLambertian};
    material.albedo = aleph::math::Vec3{0.2f, 0.4f, 0.6f};
    material.fuzz = 0.1f;
    material.ior = 1.33f;
    material.emit = aleph::math::Vec3{0.5f, 0.6f, 0.7f};
    material.uv_scale = 12.0f;
    g.insert_node(std::move(material));

    const NodeId tex = g.alloc_node_id();
    g.insert_node(Texture{tex, 320, 200, TextureFormat::Rgb8});

    const NodeId light = g.alloc_node_id();
    Light l{light, LightKind::Directional, std::string("sun \"key\"")};
    l.emission = aleph::math::Vec3{9.0f, 8.0f, 7.0f};
    l.geometry = QuadLocal{
        aleph::math::Vec3{-1.0f, 3.0f, -1.0f},
        aleph::math::Vec3{2.0f, 0.0f, 0.0f},
        aleph::math::Vec3{0.0f, 0.0f, 2.0f}};
    g.insert_node(std::move(l));

    (void)g.add_edge(EdgeKind::Contains, root, cam);
    (void)g.add_edge(EdgeKind::Contains, root, mesh);
    (void)g.add_edge(EdgeKind::Contains, root, light);
    (void)g.add_edge(EdgeKind::References, mesh, mat);
    (void)g.add_edge(EdgeKind::References, mat, tex);

    const std::string saved = aleph::graph::save_graph_string(g, root);
    auto loaded = aleph::graph::load_graph_string(saved);
    REQUIRE(loaded.has_value());

    const auto& loaded_xf = std::get<Transform>(require_node(loaded->graph, root));
    CHECK(loaded_xf.pose_slot == 7);
    CHECK(loaded_xf.local.m.m[12] == doctest::Approx(2.0f));
    CHECK(loaded_xf.local.m.m[13] == doctest::Approx(3.0f));
    CHECK(loaded_xf.local.m.m[14] == doctest::Approx(4.0f));

    const auto& loaded_cam = std::get<Camera>(require_node(loaded->graph, cam));
    CHECK(loaded_cam.sensor_id == "sensor with space");
    check_vec3(loaded_cam.look_from, aleph::math::Vec3{1.0f, 2.0f, 3.0f});
    check_vec3(loaded_cam.look_at, aleph::math::Vec3{0.0f, 0.5f, -1.0f});
    CHECK(loaded_cam.vfov_deg == doctest::Approx(55.0f));
    CHECK(loaded_cam.aperture == doctest::Approx(0.25f));
    CHECK(loaded_cam.focus_dist == doctest::Approx(8.0f));

    const auto& loaded_mesh = std::get<Mesh>(require_node(loaded->graph, mesh));
    CHECK(loaded_mesh.geometry_ref == "tri mesh");
    CHECK(loaded_mesh.tris_count == 1);
    REQUIRE(std::holds_alternative<TriLocal>(loaded_mesh.geometry));
    const auto& tri = std::get<TriLocal>(loaded_mesh.geometry);
    check_vec3(tri.b, aleph::math::Vec3{1.0f, 0.0f, 0.0f});

    const auto& loaded_mat = std::get<Material>(require_node(loaded->graph, mat));
    CHECK(loaded_mat.kind == MaterialKind::TexturedLambertian);
    check_vec3(loaded_mat.albedo, aleph::math::Vec3{0.2f, 0.4f, 0.6f});
    CHECK(loaded_mat.fuzz == doctest::Approx(0.1f));
    CHECK(loaded_mat.ior == doctest::Approx(1.33f));
    check_vec3(loaded_mat.emit, aleph::math::Vec3{0.5f, 0.6f, 0.7f});
    CHECK(loaded_mat.uv_scale == doctest::Approx(12.0f));

    const auto& loaded_tex = std::get<Texture>(require_node(loaded->graph, tex));
    CHECK(loaded_tex.width == 320);
    CHECK(loaded_tex.height == 200);
    CHECK(loaded_tex.format == TextureFormat::Rgb8);

    const auto& loaded_light = std::get<Light>(require_node(loaded->graph, light));
    CHECK(loaded_light.kind == LightKind::Directional);
    CHECK(loaded_light.emit_ref == "sun \"key\"");
    check_vec3(loaded_light.emission, aleph::math::Vec3{9.0f, 8.0f, 7.0f});
    REQUIRE(std::holds_alternative<QuadLocal>(loaded_light.geometry));
}
```

- [ ] **Step 3: Run payload round-trip test**

Run:

```bash
cmake --build build --target aleph_tests -j$(nproc)
./build/tests/aleph_tests -tc="graph serialization: round-trip preserves node payloads"
```

Expected: pass after Task 3. If it fails on quoted strings or payload fields, fix `append_string()`, `next_token()`, or the node-specific parse branch, then rerun.

- [ ] **Step 4: Commit payload coverage**

```bash
git add tests/graph/test_graph_serialization.cpp
git commit -m "test(graph): cover serialized node payloads"
```

## Task 5: Preserve `uv_scale` Through Material Projection

**Files:**
- Modify: `bridge/src/aleph.lowering/aleph.lowering-ops.cppm`
- Test: `tests/lowering/test_add_object.cpp`

- [ ] **Step 1: Add failing `uv_scale` regression**

In `tests/lowering/test_add_object.cpp`, in the existing `TEST_CASE("lowering: AddObject grows entity count by one; survivors stable")`, set a non-default `uv_scale` on `new_mat`:

```cpp
    new_mat.uv_scale = 9.0f;
```

Then add this assertion near the existing checks on `added->material`:

```cpp
    CHECK(added->material.uv_scale == doctest::Approx(9.0f));
```

- [ ] **Step 2: Run the AddObject test and verify it fails**

Run:

```bash
cmake --build build --target aleph_tests -j$(nproc)
./build/tests/aleph_tests -tc="lowering: AddObject grows entity count by one; survivors stable"
```

Expected: test fails because `added->material.uv_scale` is still the default `4.0f`.

- [ ] **Step 3: Copy `uv_scale` in `material_from()`**

In `bridge/src/aleph.lowering/aleph.lowering-ops.cppm`, update `detail::material_from()`:

```cpp
    m.emit   = p.emit;
    m.uv_scale = p.uv_scale;
    return m;
```

- [ ] **Step 4: Run the AddObject test and verify it passes**

Run:

```bash
cmake --build build --target aleph_tests -j$(nproc)
./build/tests/aleph_tests -tc="lowering: AddObject grows entity count by one; survivors stable"
```

Expected: test passes.

- [ ] **Step 5: Commit material projection fix**

```bash
git add bridge/src/aleph.lowering/aleph.lowering-ops.cppm \
        tests/lowering/test_add_object.cpp
git commit -m "fix(lowering): preserve material uv scale in ops"
```

## Task 6: Final Verification For A

**Files:**
- No code changes.

- [ ] **Step 1: Build the main test target**

Run:

```bash
cmake --build build --target aleph_tests -j$(nproc)
```

Expected: build completes without errors.

- [ ] **Step 2: Run focused graph serialization tests**

Run:

```bash
./build/tests/aleph_tests -tc="graph serialization*"
```

Expected: all graph serialization tests pass, including invalid-input cases.

- [ ] **Step 3: Run lowering and regression subsets**

Run:

```bash
./build/tests/aleph_tests -tc="lowering*" -tc="regression_*"
```

Expected: all selected tests pass.

- [ ] **Step 4: Run full doctest binary**

Run:

```bash
./build/tests/aleph_tests
```

Expected: all doctest cases pass. If SDL window tests fail because the host has a broken display, record the exact failing test and rerun the non-window subset before reporting.

- [ ] **Step 5: Inspect git status**

Run:

```bash
git status --short
```

Expected: only pre-existing unrelated user changes remain, or a clean tree if each task commit staged exactly its own files.

## Self-Review

- Spec coverage:
  - Structured load errors instead of aborts: Task 1 and Task 3.
  - Strict parser: Task 2 and Task 3.
  - Direct allocator sync: Task 1 and Task 2 allocator test.
  - `uv_scale` preservation: Task 5.
  - Tests beyond happy path: Tasks 2, 4, and 5.
- Type consistency:
  - `GraphError::DuplicateNode` is introduced before tests check it.
  - `try_insert_node()` returns `std::expected<void, GraphError>` and is consumed that way in loader code.
  - `sync_node_allocator_to_at_least()` is introduced in `Graph` and used in serialization tests and loader implementation.
- Scope:
  - The plan keeps `aleph-graph/1` unchanged and does not change renderer/editor behavior beyond preserving material semantics.

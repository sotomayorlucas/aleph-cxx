# Aleph-cxx Sub-phase 4a Implementation Plan — `aleph.types` + `aleph.graph`

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the Rust `aleph-types` (5 files, 464 LOC) and `aleph-graph` (3 files, 843 LOC) crates into C++26 modules `aleph.types` and `aleph.graph` under a new `graph/` subtree, with a new `OrderedMap<K,V>` container in `aleph.containers`, a `tla_cxx_sync` test that detects drift against `formal/scene_graph.tla`, and a fixture binary that validates the 8-node canonical scene. Tag `v0.3.0-graph` when green.

**Architecture:** Two new C++26 module libraries (`aleph_types`, `aleph_graph`) under `graph/src/`, plus a new partition `aleph.containers:ordered_map` in the existing `foundation/src/aleph.containers/`. All modules link `aleph_flags_isa` (Phase 1 workaround: no `-fno-exceptions/-fno-rtti` so test BMI dialect matches). Storage uses `OrderedMap<NodeId, Node>` / `OrderedMap<EdgeId, Edge>` for IndexMap-semantics determinism. `Node` is `std::variant<Mesh, Material, Light, Volume, Camera, Texture, Transform>`. The TLA+ sync test parses `formal/scene_graph.tla` with a small regex-based extractor and compares against C++ enum data.

**Tech Stack:** GCC 16.1.1, C++26 modules (FILE_SET CXX_MODULES), CMake 3.28+, doctest (header-only, already vendored at `third_party/doctest.h`).

**Reference:** Spec at `docs/superpowers/specs/2026-05-27-aleph-cxx-graph-design.md` Section 4. Rust source at `/home/lkz/aleph-engine/aleph-types/src/` and `/home/lkz/aleph-engine/aleph-graph/src/`. TLA+ source at `/home/lkz/aleph-engine/formal/`.

---

## File structure (new files created by this plan)

```
foundation/src/aleph.containers/
└── aleph.containers-ordered_map.cppm        # Task 2

graph/
├── CMakeLists.txt                            # Task 1
└── src/
    ├── aleph.types/
    │   ├── CMakeLists.txt                    # Task 1
    │   ├── aleph.types.cppm                  # Task 8
    │   ├── aleph.types-id.cppm               # Task 3
    │   ├── aleph.types-attribute.cppm        # Task 4
    │   ├── aleph.types-node.cppm             # Tasks 5+6
    │   └── aleph.types-edge.cppm             # Task 7
    └── aleph.graph/
        ├── CMakeLists.txt                    # Task 1
        ├── aleph.graph.cppm                  # Task 13
        ├── aleph.graph-graph.cppm            # Tasks 9+10
        └── aleph.graph-invariants.cppm       # Tasks 11+12

formal/
├── scene_graph.tla                           # Task 14 (copy)
├── dpo_rules.tla                             # Task 14 (copy, parsed in 4b)
└── check.sh                                  # Task 14 (copy)

apps/aleph_graph_fixture/
├── CMakeLists.txt                            # Task 16
└── main.cpp                                  # Task 16

tests/
├── containers/test_ordered_map.cpp           # Task 2
├── graph/
│   ├── test_id.cpp                           # Task 3
│   ├── test_attribute.cpp                    # Task 4
│   ├── test_node.cpp                         # Tasks 5+6
│   ├── test_edge.cpp                         # Task 7
│   ├── test_graph_basic.cpp                  # Task 9
│   ├── test_graph_edges.cpp                  # Task 10
│   ├── test_invariants_1_5.cpp               # Task 11
│   └── test_invariants_6_10.cpp              # Task 12
├── isolation/
│   ├── iso_types.cpp                         # Task 8
│   └── iso_graph.cpp                         # Task 13
└── tla_cxx_sync.cpp                          # Task 15
```

**Modified files:**
- `foundation/src/aleph.containers/CMakeLists.txt` (Task 2: add `aleph.containers-ordered_map.cppm`)
- `foundation/src/aleph.containers/aleph.containers.cppm` (Task 2: `export import :ordered_map;`)
- `CMakeLists.txt` (Task 1: `add_subdirectory(graph)`, copy formal/)
- `tests/CMakeLists.txt` (Tasks 2/3/.../12: add new test files to `aleph_tests`; link `aleph_types`, `aleph_graph`)
- `tests/isolation/CMakeLists.txt` (Task 8/13: register `iso_types`, `iso_graph`)
- `apps/CMakeLists.txt` (Task 16: add `aleph_graph_fixture`)

---

## Task 1: Scaffolding — directory layout + CMake wiring

**Files:**
- Create: `graph/CMakeLists.txt`
- Create: `graph/src/aleph.types/CMakeLists.txt` (placeholder)
- Create: `graph/src/aleph.graph/CMakeLists.txt` (placeholder)
- Modify: `CMakeLists.txt`

Goal: get `add_subdirectory(graph)` building cleanly with empty module libraries, so subsequent tasks just add source files.

- [ ] **Step 1: Create `graph/CMakeLists.txt`**

```cmake
add_subdirectory(src/aleph.types)
add_subdirectory(src/aleph.graph)
```

- [ ] **Step 2: Create placeholder `graph/src/aleph.types/CMakeLists.txt`**

```cmake
add_library(aleph_types)
# Populated by Tasks 3-8.
target_link_libraries(aleph_types PRIVATE aleph_flags_isa)
```

- [ ] **Step 3: Create placeholder `graph/src/aleph.graph/CMakeLists.txt`**

```cmake
add_library(aleph_graph)
# Populated by Tasks 9-13.
target_link_libraries(aleph_graph
    PUBLIC  aleph_types aleph_containers
    PRIVATE aleph_flags_isa)
```

- [ ] **Step 4: Modify root `CMakeLists.txt` — add `add_subdirectory(graph)` after foundation**

In `CMakeLists.txt`, after the `add_subdirectory(foundation)` line, add:

```cmake
add_subdirectory(graph)
```

- [ ] **Step 5: Configure + build (should fail with "no sources" since libraries are empty)**

Run: `cmake --preset release 2>&1 | tail -5 && cmake --build build-release --target aleph_types 2>&1 | tail -10`

Expected: CMake configures OK; build of `aleph_types` fails because there are no module sources yet. This is fine — Tasks 3-7 add the sources.

- [ ] **Step 6: Commit**

```bash
cd /home/lkz/aleph-cxx
git add CMakeLists.txt graph/
git commit -m "task 1: scaffolding — graph/ subtree with empty aleph_types + aleph_graph libraries"
```

---

## Task 2: `OrderedMap<K,V>` partition for `aleph.containers`

**Files:**
- Create: `foundation/src/aleph.containers/aleph.containers-ordered_map.cppm`
- Modify: `foundation/src/aleph.containers/aleph.containers.cppm`
- Modify: `foundation/src/aleph.containers/CMakeLists.txt`
- Create: `tests/containers/test_ordered_map.cpp`
- Modify: `tests/CMakeLists.txt`

**Design:** OrderedMap is a hash table + insertion-order doubly-linked list. Same semantics as Rust `IndexMap<K, V>`. API: `insert(k, v) -> bool` (true if new), `get(k) -> const V*`, `get_mut(k) -> V*`, `contains(k)`, `remove(k) -> std::optional<V>`, `size()`, `clear()`, plus iteration in insertion order yielding `std::pair<const K&, V&>`. Keys hashed via `std::hash<K>`.

- [ ] **Step 1: Write the failing test**

Create `tests/containers/test_ordered_map.cpp`:

```cpp
#include "doctest.h"
import aleph.containers;

#include <string>
#include <vector>

using aleph::containers::OrderedMap;

TEST_CASE("OrderedMap: insert returns true once, false on duplicate; size tracks") {
    OrderedMap<int, std::string> m;
    CHECK(m.size() == 0);
    CHECK(m.insert(7, "seven"));
    CHECK(m.size() == 1);
    CHECK(!m.insert(7, "siete"));   // duplicate key
    CHECK(m.size() == 1);
    CHECK(*m.get(7) == "seven");    // value unchanged
}

TEST_CASE("OrderedMap: iteration order matches insertion order") {
    OrderedMap<int, int> m;
    m.insert(3, 30);
    m.insert(1, 10);
    m.insert(4, 40);
    m.insert(1, 99);   // duplicate, ignored
    m.insert(5, 50);

    std::vector<int> keys, vals;
    for (auto [k, v] : m) {
        keys.push_back(k);
        vals.push_back(v);
    }
    CHECK(keys == std::vector<int>{3, 1, 4, 5});
    CHECK(vals == std::vector<int>{30, 10, 40, 50});
}

TEST_CASE("OrderedMap: get returns nullptr on miss, pointer on hit") {
    OrderedMap<int, int> m;
    m.insert(1, 100);
    CHECK(m.get(1) != nullptr);
    CHECK(*m.get(1) == 100);
    CHECK(m.get(2) == nullptr);
}

TEST_CASE("OrderedMap: get_mut allows in-place update without changing order") {
    OrderedMap<int, int> m;
    m.insert(1, 10);
    m.insert(2, 20);
    m.insert(3, 30);
    *m.get_mut(2) = 99;

    std::vector<int> vals;
    for (auto [k, v] : m) vals.push_back(v);
    CHECK(vals == std::vector<int>{10, 99, 30});
}

TEST_CASE("OrderedMap: remove returns value and tightens iteration") {
    OrderedMap<int, int> m;
    m.insert(1, 10);
    m.insert(2, 20);
    m.insert(3, 30);
    auto v = m.remove(2);
    REQUIRE(v.has_value());
    CHECK(*v == 20);
    CHECK(m.size() == 2);
    CHECK(m.get(2) == nullptr);

    std::vector<int> keys;
    for (auto [k, _] : m) keys.push_back(k);
    CHECK(keys == std::vector<int>{1, 3});
}

TEST_CASE("OrderedMap: re-insert after remove appends at tail") {
    OrderedMap<int, int> m;
    m.insert(1, 10);
    m.insert(2, 20);
    m.insert(3, 30);
    m.remove(1);
    m.insert(1, 11);

    std::vector<int> keys;
    for (auto [k, _] : m) keys.push_back(k);
    CHECK(keys == std::vector<int>{2, 3, 1});
}

TEST_CASE("OrderedMap: clear empties without resetting bucket capacity") {
    OrderedMap<int, int> m;
    for (int i = 0; i < 100; ++i) m.insert(i, i);
    m.clear();
    CHECK(m.size() == 0);
    CHECK(m.get(50) == nullptr);
}
```

- [ ] **Step 2: Run test to confirm it fails to compile**

Run: `cd /home/lkz/aleph-cxx && cmake --build build-release --target aleph_tests 2>&1 | tail -10`
Expected: FAIL — `OrderedMap` is undefined.

- [ ] **Step 3: Write the OrderedMap partition**

Create `foundation/src/aleph.containers/aleph.containers-ordered_map.cppm`:

```cpp
module;
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

export module aleph.containers:ordered_map;

export namespace aleph::containers {

// OrderedMap<K,V> — hash table + insertion-order doubly-linked list.
// Same semantics as Rust IndexMap<K,V>:
//  - O(1) average insert / lookup / remove
//  - iteration order is insertion order; remove tightens iteration (no holes)
//  - re-inserting after removal appends at tail
//
// Requires: K is hashable via std::hash<K> and equality-comparable.
template <typename K, typename V>
class OrderedMap {
    struct Slot {
        K     key;
        V     value;
        std::size_t prev;   // doubly-linked list (sentinel = SIZE_MAX)
        std::size_t next;
    };

    // Open-addressing index: bucket -> slot index (or SIZE_MAX = empty).
    // Grows when load > 0.7; rehashes deterministically.
    std::vector<std::size_t> buckets_;
    // Slot storage. Vacancies form a free list via `next` field.
    std::vector<Slot>        slots_;
    std::size_t              head_  {SIZE_MAX};   // head of linked list
    std::size_t              tail_  {SIZE_MAX};   // tail of linked list
    std::size_t              free_  {SIZE_MAX};   // free-list head
    std::size_t              count_ {0};
    static constexpr std::size_t TOMBSTONE = SIZE_MAX - 1;

    static constexpr std::size_t INITIAL_BUCKETS = 16;

    void ensure_buckets() {
        if (buckets_.empty()) buckets_.assign(INITIAL_BUCKETS, SIZE_MAX);
    }

    std::size_t hash_to_bucket(const K& key) const noexcept {
        return std::hash<K>{}(key) & (buckets_.size() - 1);
    }

    // Linear probing. Returns the slot index for `key`, or the first
    // empty bucket if not present. `out_idx` is the bucket position.
    std::size_t probe(const K& key, std::size_t& out_idx) const noexcept {
        out_idx = hash_to_bucket(key);
        const std::size_t mask = buckets_.size() - 1;
        for (std::size_t step = 0; step < buckets_.size(); ++step) {
            const std::size_t b = (out_idx + step) & mask;
            const std::size_t s = buckets_[b];
            if (s == SIZE_MAX) { out_idx = b; return SIZE_MAX; }
            if (s != TOMBSTONE && slots_[s].key == key) {
                out_idx = b;
                return s;
            }
        }
        out_idx = SIZE_MAX;
        return SIZE_MAX;
    }

    void maybe_rehash() {
        if (buckets_.empty()) { ensure_buckets(); return; }
        // load = (alive slots) / buckets
        if (count_ * 10 >= buckets_.size() * 7) {
            const std::size_t new_n = buckets_.size() * 2;
            buckets_.assign(new_n, SIZE_MAX);
            // walk linked list (= insertion order) and re-bucket each slot.
            for (std::size_t s = head_; s != SIZE_MAX; s = slots_[s].next) {
                std::size_t b;
                probe(slots_[s].key, b);   // returns SIZE_MAX (we just cleared)
                buckets_[b] = s;
            }
        }
    }

    std::size_t take_slot(K k, V v) {
        std::size_t s;
        if (free_ != SIZE_MAX) {
            s = free_;
            free_ = slots_[s].next;
            slots_[s].key   = std::move(k);
            slots_[s].value = std::move(v);
        } else {
            slots_.push_back(Slot{std::move(k), std::move(v), SIZE_MAX, SIZE_MAX});
            s = slots_.size() - 1;
        }
        slots_[s].prev = tail_;
        slots_[s].next = SIZE_MAX;
        if (tail_ != SIZE_MAX) slots_[tail_].next = s;
        else                   head_ = s;
        tail_ = s;
        return s;
    }

public:
    OrderedMap() = default;

    std::size_t size() const noexcept { return count_; }
    bool empty() const noexcept { return count_ == 0; }

    // Insert (k, v). Returns true if newly inserted. On duplicate key,
    // the existing value is NOT overwritten (use get_mut for that).
    bool insert(K k, V v) {
        maybe_rehash();
        ensure_buckets();
        std::size_t b;
        const std::size_t existing = probe(k, b);
        if (existing != SIZE_MAX) return false;
        const std::size_t s = take_slot(std::move(k), std::move(v));
        buckets_[b] = s;
        ++count_;
        return true;
    }

    // Look up. Returns nullptr if absent.
    const V* get(const K& key) const noexcept {
        if (buckets_.empty()) return nullptr;
        std::size_t b;
        const std::size_t s = probe(key, b);
        if (s == SIZE_MAX) return nullptr;
        return &slots_[s].value;
    }

    V* get_mut(const K& key) noexcept {
        if (buckets_.empty()) return nullptr;
        std::size_t b;
        const std::size_t s = probe(key, b);
        if (s == SIZE_MAX) return nullptr;
        return &slots_[s].value;
    }

    bool contains(const K& key) const noexcept { return get(key) != nullptr; }

    // Remove. Returns the value if present, std::nullopt otherwise.
    std::optional<V> remove(const K& key) {
        if (buckets_.empty()) return std::nullopt;
        std::size_t b;
        const std::size_t s = probe(key, b);
        if (s == SIZE_MAX) return std::nullopt;

        V out = std::move(slots_[s].value);

        // unlink from doubly-linked list
        const std::size_t p = slots_[s].prev;
        const std::size_t n = slots_[s].next;
        if (p != SIZE_MAX) slots_[p].next = n; else head_ = n;
        if (n != SIZE_MAX) slots_[n].prev = p; else tail_ = p;

        // mark bucket as tombstone (linear probing needs this)
        buckets_[b] = TOMBSTONE;

        // return slot to free list
        slots_[s].next = free_;
        free_ = s;
        --count_;
        return out;
    }

    void clear() noexcept {
        for (auto& b : buckets_) b = SIZE_MAX;
        head_ = tail_ = free_ = SIZE_MAX;
        slots_.clear();
        count_ = 0;
    }

    // ── Iteration in insertion order ─────────────────────────────
    class const_iterator {
        const OrderedMap* m_;
        std::size_t s_;
    public:
        const_iterator(const OrderedMap* m, std::size_t s) noexcept : m_{m}, s_{s} {}
        std::pair<const K&, const V&> operator*() const noexcept {
            return {m_->slots_[s_].key, m_->slots_[s_].value};
        }
        const_iterator& operator++() noexcept { s_ = m_->slots_[s_].next; return *this; }
        bool operator==(const const_iterator& o) const noexcept { return s_ == o.s_; }
        bool operator!=(const const_iterator& o) const noexcept { return s_ != o.s_; }
    };
    class iterator {
        OrderedMap* m_;
        std::size_t s_;
    public:
        iterator(OrderedMap* m, std::size_t s) noexcept : m_{m}, s_{s} {}
        std::pair<const K&, V&> operator*() const noexcept {
            return {m_->slots_[s_].key, m_->slots_[s_].value};
        }
        iterator& operator++() noexcept { s_ = m_->slots_[s_].next; return *this; }
        bool operator==(const iterator& o) const noexcept { return s_ == o.s_; }
        bool operator!=(const iterator& o) const noexcept { return s_ != o.s_; }
    };
    iterator       begin()        noexcept { return iterator(this, head_); }
    iterator       end()          noexcept { return iterator(this, SIZE_MAX); }
    const_iterator begin()  const noexcept { return const_iterator(this, head_); }
    const_iterator end()    const noexcept { return const_iterator(this, SIZE_MAX); }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend()   const noexcept { return end(); }
};

}  // namespace aleph::containers
```

- [ ] **Step 4: Export the partition from the containers module**

Modify `foundation/src/aleph.containers/aleph.containers.cppm`:

```cpp
export module aleph.containers;
export import :flat_set;
export import :small_vector;
export import :dense_index;
export import :ordered_map;
```

- [ ] **Step 5: Add to the containers CMake module set**

Modify `foundation/src/aleph.containers/CMakeLists.txt`:

```cmake
add_library(aleph_containers)
target_sources(aleph_containers
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.containers.cppm
        aleph.containers-small_vector.cppm
        aleph.containers-flat_set.cppm
        aleph.containers-dense_index.cppm
        aleph.containers-ordered_map.cppm)
target_link_libraries(aleph_containers
    PUBLIC  aleph_alloc
    PRIVATE aleph_flags_isa)
```

- [ ] **Step 6: Register the new test file with `aleph_tests`**

In `tests/CMakeLists.txt`, find the line `containers/test_dense_index.cpp` and add immediately after it:

```cmake
    containers/test_ordered_map.cpp
```

- [ ] **Step 7: Build + run tests**

Run: `cd /home/lkz/aleph-cxx && cmake --build build-release --target aleph_tests 2>&1 | tail -5 && ctest --test-dir build-release -R aleph_tests --output-on-failure 2>&1 | tail -20`

Expected: build green; doctest reports 7 new test cases in `aleph_tests` all passing; total assertion count went up.

- [ ] **Step 8: Commit**

```bash
git add foundation/src/aleph.containers/ tests/containers/test_ordered_map.cpp tests/CMakeLists.txt
git commit -m "task 2: aleph.containers:ordered_map — IndexMap-equiv (insertion-order + O(1) lookup)"
```

---

## Task 3: `aleph.types:id` — `NodeId`, `EdgeId`, `IdAllocator`

**Files:**
- Create: `graph/src/aleph.types/aleph.types-id.cppm`
- Create: `tests/graph/test_id.cpp`
- Modify: `graph/src/aleph.types/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/graph/test_id.cpp`:

```cpp
#include "doctest.h"
import aleph.types;

#include <type_traits>
#include <functional>

using aleph::types::EdgeId;
using aleph::types::IdAllocator;
using aleph::types::NodeId;

TEST_CASE("NodeId / EdgeId: strong typedefs, not interconvertible") {
    static_assert(!std::is_convertible_v<NodeId, EdgeId>);
    static_assert(!std::is_convertible_v<EdgeId, NodeId>);
    static_assert(!std::is_convertible_v<int, NodeId>);     // explicit ctor required
}

TEST_CASE("NodeId / EdgeId: value, equality, hashability") {
    constexpr NodeId a{7}, b{7}, c{8};
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a.value == 7);

    constexpr EdgeId e{42};
    CHECK(e.value == 42);

    // hash works (needed for OrderedMap<NodeId, Node>)
    std::hash<NodeId> h;
    CHECK(h(a) == h(b));
}

TEST_CASE("IdAllocator: monotonic per-kind allocation from 0") {
    IdAllocator ids;
    CHECK(ids.alloc_node().value == 0);
    CHECK(ids.alloc_node().value == 1);
    CHECK(ids.alloc_edge().value == 0);    // independent counter
    CHECK(ids.alloc_node().value == 2);
    CHECK(ids.alloc_edge().value == 1);
}

TEST_CASE("IdAllocator: copyable as watermark snapshot") {
    IdAllocator a;
    a.alloc_node(); a.alloc_node(); a.alloc_edge();
    IdAllocator b = a;     // snapshot
    CHECK(b.alloc_node().value == 2);
    CHECK(b.alloc_edge().value == 1);
    // original untouched
    CHECK(a.alloc_node().value == 2);
    CHECK(a.alloc_edge().value == 1);
}
```

- [ ] **Step 2: Run to confirm fail**

Run: `cd /home/lkz/aleph-cxx && cmake --build build-release --target aleph_tests 2>&1 | tail -5`
Expected: FAIL — module `aleph.types` not found.

- [ ] **Step 3: Implement `aleph.types-id.cppm`**

```cpp
module;
#include <cstdint>
#include <functional>

export module aleph.types:id;

export namespace aleph::types {

// Strong typedef over u32. Two ids of different kinds are NOT
// interconvertible (different types). Constructor is explicit.
struct NodeId {
    std::uint32_t value{};
    constexpr explicit NodeId(std::uint32_t v = 0) noexcept : value{v} {}
    constexpr bool operator==(const NodeId& o) const noexcept { return value == o.value; }
    constexpr bool operator!=(const NodeId& o) const noexcept { return value != o.value; }
    constexpr bool operator< (const NodeId& o) const noexcept { return value <  o.value; }
};

struct EdgeId {
    std::uint32_t value{};
    constexpr explicit EdgeId(std::uint32_t v = 0) noexcept : value{v} {}
    constexpr bool operator==(const EdgeId& o) const noexcept { return value == o.value; }
    constexpr bool operator!=(const EdgeId& o) const noexcept { return value != o.value; }
    constexpr bool operator< (const EdgeId& o) const noexcept { return value <  o.value; }
};

// Two independent monotone counters. Cloneable as a watermark snapshot
// (the Rust workspace uses the same idiom for Mayer-Vietoris graph clones).
class IdAllocator {
public:
    constexpr IdAllocator() noexcept = default;
    constexpr IdAllocator(const IdAllocator&) noexcept = default;
    constexpr IdAllocator& operator=(const IdAllocator&) noexcept = default;

    NodeId alloc_node() noexcept { return NodeId{node_next_++}; }
    EdgeId alloc_edge() noexcept { return EdgeId{edge_next_++}; }

    std::uint32_t node_watermark() const noexcept { return node_next_; }
    std::uint32_t edge_watermark() const noexcept { return edge_next_; }

private:
    std::uint32_t node_next_{0};
    std::uint32_t edge_next_{0};
};

}  // namespace aleph::types

// std::hash specialisations so NodeId / EdgeId can be OrderedMap keys.
export template <> struct std::hash<aleph::types::NodeId> {
    std::size_t operator()(aleph::types::NodeId k) const noexcept {
        return std::hash<std::uint32_t>{}(k.value);
    }
};
export template <> struct std::hash<aleph::types::EdgeId> {
    std::size_t operator()(aleph::types::EdgeId k) const noexcept {
        return std::hash<std::uint32_t>{}(k.value);
    }
};
```

- [ ] **Step 4: Wire into `aleph_types` library**

Modify `graph/src/aleph.types/CMakeLists.txt`:

```cmake
add_library(aleph_types)
target_sources(aleph_types
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.types-id.cppm)
target_link_libraries(aleph_types PRIVATE aleph_flags_isa)
```

(`aleph.types.cppm` umbrella is added in Task 8.)

- [ ] **Step 5: Wire test file into `aleph_tests`**

In `tests/CMakeLists.txt`, add `graph/test_id.cpp` to the source list. Find the block listing scene tests and add a new block above it:

```cmake
    graph/test_id.cpp
```

Also add `aleph_types` to the `target_link_libraries(aleph_tests PRIVATE ...)` line. The list goes:

```cmake
target_link_libraries(aleph_tests PRIVATE
    aleph_flags_test aleph_cpu aleph_math aleph_alloc aleph_containers aleph_threads aleph_io
    aleph_types
    aleph_scene aleph_render_common aleph_render_rt aleph_render_sw)
```

- [ ] **Step 6: Build + test**

Run: `cd /home/lkz/aleph-cxx && cmake --build build-release --target aleph_tests 2>&1 | tail -5 && ctest --test-dir build-release -R aleph_tests --output-on-failure 2>&1 | tail -10`

Expected: PASS — 4 new test cases.

- [ ] **Step 7: Commit**

```bash
git add graph/src/aleph.types/aleph.types-id.cppm graph/src/aleph.types/CMakeLists.txt tests/graph/test_id.cpp tests/CMakeLists.txt
git commit -m "task 3: aleph.types:id — NodeId/EdgeId strong typedefs + IdAllocator"
```

---

## Task 4: `aleph.types:attribute` — 4 enum tags

**Files:**
- Create: `graph/src/aleph.types/aleph.types-attribute.cppm`
- Create: `tests/graph/test_attribute.cpp`
- Modify: `graph/src/aleph.types/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/graph/test_attribute.cpp`:

```cpp
#include "doctest.h"
import aleph.types;

using namespace aleph::types;

TEST_CASE("MaterialKind values") {
    CHECK(static_cast<int>(MaterialKind::Lambertian) == 0);
    CHECK(static_cast<int>(MaterialKind::Metal)      == 1);
    CHECK(static_cast<int>(MaterialKind::Dielectric) == 2);
    CHECK(static_cast<int>(MaterialKind::Emissive)   == 3);
}

TEST_CASE("LightKind values") {
    CHECK(static_cast<int>(LightKind::Point)       == 0);
    CHECK(static_cast<int>(LightKind::Area)        == 1);
    CHECK(static_cast<int>(LightKind::Directional) == 2);
}

TEST_CASE("MediumKind values") {
    CHECK(static_cast<int>(MediumKind::Vacuum)        == 0);
    CHECK(static_cast<int>(MediumKind::Homogeneous)   == 1);
    CHECK(static_cast<int>(MediumKind::Heterogeneous) == 2);
}

TEST_CASE("TextureFormat values") {
    CHECK(static_cast<int>(TextureFormat::Rgba8) == 0);
    CHECK(static_cast<int>(TextureFormat::Rgb8)  == 1);
    CHECK(static_cast<int>(TextureFormat::R32F)  == 2);
}
```

- [ ] **Step 2: Run to confirm fail**

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -5`
Expected: FAIL — `MaterialKind` undefined.

- [ ] **Step 3: Implement `aleph.types-attribute.cppm`**

```cpp
module;
#include <cstdint>

export module aleph.types:attribute;

export namespace aleph::types {

// Material kind tag. BSDF parameters folded as attributes in M1.
enum class MaterialKind : std::uint8_t {
    Lambertian = 0,
    Metal      = 1,
    Dielectric = 2,
    Emissive   = 3,
};

enum class LightKind : std::uint8_t {
    Point       = 0,
    Area        = 1,
    Directional = 2,
};

enum class MediumKind : std::uint8_t {
    Vacuum        = 0,
    Homogeneous   = 1,
    Heterogeneous = 2,
};

enum class TextureFormat : std::uint8_t {
    Rgba8 = 0,
    Rgb8  = 1,
    R32F  = 2,
};

}  // namespace aleph::types
```

- [ ] **Step 4: Add to module CMake**

Modify `graph/src/aleph.types/CMakeLists.txt`:

```cmake
add_library(aleph_types)
target_sources(aleph_types
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.types-id.cppm
        aleph.types-attribute.cppm)
target_link_libraries(aleph_types PRIVATE aleph_flags_isa)
```

- [ ] **Step 5: Register test in tests/CMakeLists.txt**

After `graph/test_id.cpp` add `graph/test_attribute.cpp`.

- [ ] **Step 6: Build + test**

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -5 && ctest --test-dir build-release -R aleph_tests --output-on-failure 2>&1 | tail -10`

Expected: PASS — 4 new test cases.

- [ ] **Step 7: Commit**

```bash
git add graph/src/aleph.types/aleph.types-attribute.cppm graph/src/aleph.types/CMakeLists.txt tests/graph/test_attribute.cpp tests/CMakeLists.txt
git commit -m "task 4: aleph.types:attribute — MaterialKind / LightKind / MediumKind / TextureFormat enums"
```

---

## Task 5: `aleph.types:node` — 7 node structs + NodeKind

**Files:**
- Create: `graph/src/aleph.types/aleph.types-node.cppm`
- Create: `tests/graph/test_node.cpp`
- Modify: `graph/src/aleph.types/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

This task ships the 7 node structs + `NodeKind` enum + `as_tla()` + `ALL[]`. Task 6 adds the `Node` variant + `kind()`/`id()` helpers; we split because the variant is a larger logical piece.

- [ ] **Step 1: Write the failing test**

Create `tests/graph/test_node.cpp`:

```cpp
#include "doctest.h"
import aleph.types;

#include <string>
#include <string_view>

using namespace aleph::types;

TEST_CASE("NodeKind: enum values and ALL array") {
    CHECK(static_cast<int>(NodeKind::Mesh)      == 0);
    CHECK(static_cast<int>(NodeKind::Material)  == 1);
    CHECK(static_cast<int>(NodeKind::Light)     == 2);
    CHECK(static_cast<int>(NodeKind::Volume)    == 3);
    CHECK(static_cast<int>(NodeKind::Camera)    == 4);
    CHECK(static_cast<int>(NodeKind::Texture)   == 5);
    CHECK(static_cast<int>(NodeKind::Transform) == 6);
    CHECK(NodeKind::ALL.size() == 7);
}

TEST_CASE("NodeKind::as_tla: lowercase strings matching scene_graph.tla") {
    CHECK(as_tla(NodeKind::Mesh)      == "mesh");
    CHECK(as_tla(NodeKind::Material)  == "material");
    CHECK(as_tla(NodeKind::Light)     == "light");
    CHECK(as_tla(NodeKind::Volume)    == "volume");
    CHECK(as_tla(NodeKind::Camera)    == "camera");
    CHECK(as_tla(NodeKind::Texture)   == "texture");
    CHECK(as_tla(NodeKind::Transform) == "transform");
}

TEST_CASE("Mesh node carries geometry ref + tri count") {
    Mesh m{NodeId{4}, std::string("cube"), 12};
    CHECK(m.id.value == 4);
    CHECK(m.geometry_ref == "cube");
    CHECK(m.tris_count == 12);
}

TEST_CASE("Material node carries kind tag only") {
    Material mat{NodeId{6}, MaterialKind::Lambertian};
    CHECK(mat.id.value == 6);
    CHECK(mat.kind == MaterialKind::Lambertian);
}

TEST_CASE("Light node carries kind + emit ref") {
    Light l{NodeId{3}, LightKind::Point, std::string("ies/std")};
    CHECK(l.id.value == 3);
    CHECK(l.kind == LightKind::Point);
    CHECK(l.emit_ref == "ies/std");
}

TEST_CASE("Volume node carries medium tag") {
    Volume v{NodeId{9}, MediumKind::Homogeneous};
    CHECK(v.id.value == 9);
    CHECK(v.medium == MediumKind::Homogeneous);
}

TEST_CASE("Camera node carries sensor ref") {
    Camera c{NodeId{2}, std::string("default")};
    CHECK(c.id.value == 2);
    CHECK(c.sensor_id == "default");
}

TEST_CASE("Texture node carries dims + format") {
    Texture t{NodeId{7}, 256, 256, TextureFormat::Rgb8};
    CHECK(t.width == 256);
    CHECK(t.format == TextureFormat::Rgb8);
}

TEST_CASE("Transform node carries pose slot") {
    Transform tr{NodeId{1}, 5};
    CHECK(tr.pose_slot == 5);
}
```

- [ ] **Step 2: Run to confirm fail**

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -5`
Expected: FAIL — node types undefined.

- [ ] **Step 3: Implement node structs + NodeKind**

Create `graph/src/aleph.types/aleph.types-node.cppm`:

```cpp
module;
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

export module aleph.types:node;

import :id;
import :attribute;

export namespace aleph::types {

// ── Node structs ─────────────────────────────────────────────────────

struct Mesh {
    NodeId        id{};
    std::string   geometry_ref;
    std::uint32_t tris_count{};
};

struct Material {
    NodeId       id{};
    MaterialKind kind{MaterialKind::Lambertian};
};

struct Light {
    NodeId      id{};
    LightKind   kind{LightKind::Point};
    std::string emit_ref;
};

struct Volume {
    NodeId     id{};
    MediumKind medium{MediumKind::Vacuum};
};

struct Camera {
    NodeId      id{};
    std::string sensor_id;
};

struct Texture {
    NodeId        id{};
    std::uint32_t width{};
    std::uint32_t height{};
    TextureFormat format{TextureFormat::Rgba8};
};

struct Transform {
    NodeId        id{};
    std::uint32_t pose_slot{};
};

// ── NodeKind tag ─────────────────────────────────────────────────────

enum class NodeKind : std::uint8_t {
    Mesh      = 0,
    Material  = 1,
    Light     = 2,
    Volume    = 3,
    Camera    = 4,
    Texture   = 5,
    Transform = 6,
};

// ALL in enumeration order (matches TLA+ enum order).
inline constexpr std::array<NodeKind, 7> all_node_kinds() noexcept {
    return {
        NodeKind::Mesh,      NodeKind::Material,
        NodeKind::Light,     NodeKind::Volume,
        NodeKind::Camera,    NodeKind::Texture,
        NodeKind::Transform,
    };
}

// Canonical lowercase name (must match scene_graph.tla NodeKind set).
constexpr std::string_view as_tla(NodeKind k) noexcept {
    switch (k) {
        case NodeKind::Mesh:      return "mesh";
        case NodeKind::Material:  return "material";
        case NodeKind::Light:     return "light";
        case NodeKind::Volume:    return "volume";
        case NodeKind::Camera:    return "camera";
        case NodeKind::Texture:   return "texture";
        case NodeKind::Transform: return "transform";
    }
    return "";   // unreachable
}

}  // namespace aleph::types

// NodeKind needs an `ALL` constexpr array reachable as `NodeKind::ALL`
// to mirror the Rust idiom. Implement via a wrapper variable since C++26
// can't add static members to enums directly.
export namespace aleph::types {

struct NodeKindOps {
    static constexpr std::array<NodeKind, 7> ALL = all_node_kinds();
};

}

// Allow the spelling `NodeKind::ALL` via a using-alias inside the export
// block above by re-exposing through a wrapper. We use `NodeKind::ALL`
// style via a namespace-scope inline constexpr instead, since enums
// don't accept inner members.
//
// Tests reference `NodeKind::ALL` — provide it as a free-standing
// constexpr inline reachable through the type.
//
// Workaround: define an inline constexpr at the namespace level and
// the tests above use `NodeKind::ALL` — which we cannot literally
// provide on a scoped enum. Replace test usage to `all_node_kinds()`.
```

Now an adjustment to the test file — the original test used `NodeKind::ALL.size()`. We need to change the API call style.

**Revisit Step 1**: edit the test to use `all_node_kinds().size()` (replace `NodeKind::ALL.size()` line):

```cpp
TEST_CASE("NodeKind: enum values and ALL array") {
    CHECK(static_cast<int>(NodeKind::Mesh)      == 0);
    CHECK(static_cast<int>(NodeKind::Material)  == 1);
    CHECK(static_cast<int>(NodeKind::Light)     == 2);
    CHECK(static_cast<int>(NodeKind::Volume)    == 3);
    CHECK(static_cast<int>(NodeKind::Camera)    == 4);
    CHECK(static_cast<int>(NodeKind::Texture)   == 5);
    CHECK(static_cast<int>(NodeKind::Transform) == 6);
    CHECK(all_node_kinds().size() == 7);
    CHECK(all_node_kinds()[0] == NodeKind::Mesh);
}
```

Re-save `tests/graph/test_node.cpp` with that change before building.

Also simplify the source file: drop the `NodeKindOps` workaround block. Final clean version of `graph/src/aleph.types/aleph.types-node.cppm`:

```cpp
module;
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

export module aleph.types:node;

import :id;
import :attribute;

export namespace aleph::types {

struct Mesh {
    NodeId        id{};
    std::string   geometry_ref;
    std::uint32_t tris_count{};
};
struct Material {
    NodeId       id{};
    MaterialKind kind{MaterialKind::Lambertian};
};
struct Light {
    NodeId      id{};
    LightKind   kind{LightKind::Point};
    std::string emit_ref;
};
struct Volume {
    NodeId     id{};
    MediumKind medium{MediumKind::Vacuum};
};
struct Camera {
    NodeId      id{};
    std::string sensor_id;
};
struct Texture {
    NodeId        id{};
    std::uint32_t width{};
    std::uint32_t height{};
    TextureFormat format{TextureFormat::Rgba8};
};
struct Transform {
    NodeId        id{};
    std::uint32_t pose_slot{};
};

enum class NodeKind : std::uint8_t {
    Mesh      = 0,
    Material  = 1,
    Light     = 2,
    Volume    = 3,
    Camera    = 4,
    Texture   = 5,
    Transform = 6,
};

inline constexpr std::array<NodeKind, 7> all_node_kinds() noexcept {
    return {
        NodeKind::Mesh,      NodeKind::Material,
        NodeKind::Light,     NodeKind::Volume,
        NodeKind::Camera,    NodeKind::Texture,
        NodeKind::Transform,
    };
}

constexpr std::string_view as_tla(NodeKind k) noexcept {
    switch (k) {
        case NodeKind::Mesh:      return "mesh";
        case NodeKind::Material:  return "material";
        case NodeKind::Light:     return "light";
        case NodeKind::Volume:    return "volume";
        case NodeKind::Camera:    return "camera";
        case NodeKind::Texture:   return "texture";
        case NodeKind::Transform: return "transform";
    }
    return "";
}

}  // namespace aleph::types
```

- [ ] **Step 4: Add to CMake module set**

Modify `graph/src/aleph.types/CMakeLists.txt`:

```cmake
add_library(aleph_types)
target_sources(aleph_types
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.types-id.cppm
        aleph.types-attribute.cppm
        aleph.types-node.cppm)
target_link_libraries(aleph_types PRIVATE aleph_flags_isa)
```

- [ ] **Step 5: Add test to `tests/CMakeLists.txt`**

After `graph/test_attribute.cpp` add `graph/test_node.cpp`.

- [ ] **Step 6: Build + test**

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -5 && ctest --test-dir build-release -R aleph_tests --output-on-failure 2>&1 | tail -10`

Expected: PASS — 9 new test cases.

- [ ] **Step 7: Commit**

```bash
git add graph/src/aleph.types/aleph.types-node.cppm graph/src/aleph.types/CMakeLists.txt tests/graph/test_node.cpp tests/CMakeLists.txt
git commit -m "task 5: aleph.types:node — 7 node structs + NodeKind enum + as_tla"
```

---

## Task 6: `aleph.types:node` — `Node` variant + helpers

**Files:**
- Modify: `graph/src/aleph.types/aleph.types-node.cppm`
- Modify: `tests/graph/test_node.cpp`

Adds the `Node` tagged-union (= `std::variant`), plus `kind()` and `id()` helpers that mirror the Rust API.

- [ ] **Step 1: Append failing tests to `tests/graph/test_node.cpp`**

At the bottom of the file:

```cpp
TEST_CASE("Node variant: holds any of the 7 kinds") {
    Node n = Mesh{NodeId{4}, std::string("cube"), 12};
    CHECK(kind_of(n) == NodeKind::Mesh);
    CHECK(id_of(n).value == 4);

    n = Camera{NodeId{2}, std::string("default")};
    CHECK(kind_of(n) == NodeKind::Camera);
    CHECK(id_of(n).value == 2);

    n = Light{NodeId{3}, LightKind::Area, std::string("ies/area")};
    CHECK(kind_of(n) == NodeKind::Light);
    CHECK(id_of(n).value == 3);
}

TEST_CASE("Node variant: visit dispatches per kind") {
    Node n = Material{NodeId{6}, MaterialKind::Dielectric};
    auto label = std::visit([](auto const& x) -> std::string_view {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, Mesh>)      return "mesh";
        else if constexpr (std::is_same_v<T, Material>) return "material";
        else                                            return "other";
    }, n);
    CHECK(label == "material");
}
```

- [ ] **Step 2: Run to confirm fail**

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -5`
Expected: FAIL — `Node`, `kind_of`, `id_of` undefined.

- [ ] **Step 3: Add Node variant + helpers to `aleph.types-node.cppm`**

Add to the bottom of `graph/src/aleph.types/aleph.types-node.cppm`, inside `export namespace aleph::types { ... }`:

```cpp
// Add to module preamble (`module;` header block) at top:
//   #include <variant>
```

Update the preamble:

```cpp
module;
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
```

Then append before the closing `}  // namespace aleph::types`:

```cpp
// Tagged union of all node kinds. The tag mirrors NodeKind.
using Node = std::variant<
    Mesh, Material, Light, Volume, Camera, Texture, Transform
>;

constexpr NodeKind kind_of(const Node& n) noexcept {
    return std::visit([](auto const& x) constexpr -> NodeKind {
        using T = std::decay_t<decltype(x)>;
        if constexpr      (std::is_same_v<T, Mesh>)      return NodeKind::Mesh;
        else if constexpr (std::is_same_v<T, Material>)  return NodeKind::Material;
        else if constexpr (std::is_same_v<T, Light>)     return NodeKind::Light;
        else if constexpr (std::is_same_v<T, Volume>)    return NodeKind::Volume;
        else if constexpr (std::is_same_v<T, Camera>)    return NodeKind::Camera;
        else if constexpr (std::is_same_v<T, Texture>)   return NodeKind::Texture;
        else                                              return NodeKind::Transform;
    }, n);
}

constexpr NodeId id_of(const Node& n) noexcept {
    return std::visit([](auto const& x) constexpr { return x.id; }, n);
}
```

- [ ] **Step 4: Build + test**

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -5 && ctest --test-dir build-release -R aleph_tests --output-on-failure 2>&1 | tail -10`

Expected: PASS — 2 new test cases.

- [ ] **Step 5: Commit**

```bash
git add graph/src/aleph.types/aleph.types-node.cppm tests/graph/test_node.cpp
git commit -m "task 6: aleph.types:node — Node variant + kind_of/id_of visitors"
```

---

## Task 7: `aleph.types:edge` — `EdgeKind` + `allows()` + `Edge`

**Files:**
- Create: `graph/src/aleph.types/aleph.types-edge.cppm`
- Create: `tests/graph/test_edge.cpp`
- Modify: `graph/src/aleph.types/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/graph/test_edge.cpp`:

```cpp
#include "doctest.h"
import aleph.types;

using namespace aleph::types;

TEST_CASE("EdgeKind: enum values and as_tla") {
    CHECK(static_cast<int>(EdgeKind::Adjacent)   == 0);
    CHECK(static_cast<int>(EdgeKind::Contains)   == 1);
    CHECK(static_cast<int>(EdgeKind::Influences) == 2);
    CHECK(static_cast<int>(EdgeKind::References) == 3);
    CHECK(as_tla(EdgeKind::Adjacent)   == "adjacent");
    CHECK(as_tla(EdgeKind::Contains)   == "contains");
    CHECK(as_tla(EdgeKind::Influences) == "influences");
    CHECK(as_tla(EdgeKind::References) == "references");
    CHECK(all_edge_kinds().size() == 4);
}

TEST_CASE("EdgeKind::allows — Adjacent only (Mesh, Mesh)") {
    CHECK( allows(EdgeKind::Adjacent, NodeKind::Mesh,     NodeKind::Mesh));
    CHECK(!allows(EdgeKind::Adjacent, NodeKind::Mesh,     NodeKind::Material));
    CHECK(!allows(EdgeKind::Adjacent, NodeKind::Texture,  NodeKind::Texture));
    CHECK(!allows(EdgeKind::Adjacent, NodeKind::Volume,   NodeKind::Volume));
}

TEST_CASE("EdgeKind::allows — Contains: Transform parents subset") {
    CHECK( allows(EdgeKind::Contains, NodeKind::Transform, NodeKind::Transform));
    CHECK( allows(EdgeKind::Contains, NodeKind::Transform, NodeKind::Mesh));
    CHECK( allows(EdgeKind::Contains, NodeKind::Transform, NodeKind::Light));
    CHECK( allows(EdgeKind::Contains, NodeKind::Transform, NodeKind::Camera));
    CHECK( allows(EdgeKind::Contains, NodeKind::Transform, NodeKind::Volume));
    CHECK(!allows(EdgeKind::Contains, NodeKind::Mesh,      NodeKind::Transform));
    CHECK(!allows(EdgeKind::Contains, NodeKind::Transform, NodeKind::Material));
    CHECK(!allows(EdgeKind::Contains, NodeKind::Transform, NodeKind::Texture));
}

TEST_CASE("EdgeKind::allows — Influences: Light/Volume/Material -> Mesh only") {
    CHECK( allows(EdgeKind::Influences, NodeKind::Light,    NodeKind::Mesh));
    CHECK( allows(EdgeKind::Influences, NodeKind::Volume,   NodeKind::Mesh));
    CHECK( allows(EdgeKind::Influences, NodeKind::Material, NodeKind::Mesh));
    CHECK(!allows(EdgeKind::Influences, NodeKind::Mesh,     NodeKind::Light));
    CHECK(!allows(EdgeKind::Influences, NodeKind::Light,    NodeKind::Light));
}

TEST_CASE("EdgeKind::allows — References: Mesh->Material or Material->Texture") {
    CHECK( allows(EdgeKind::References, NodeKind::Mesh,     NodeKind::Material));
    CHECK( allows(EdgeKind::References, NodeKind::Material, NodeKind::Texture));
    CHECK(!allows(EdgeKind::References, NodeKind::Material, NodeKind::Mesh));
    CHECK(!allows(EdgeKind::References, NodeKind::Mesh,     NodeKind::Texture));
}

TEST_CASE("Edge struct: id, kind, src, dst") {
    Edge e{EdgeId{7}, EdgeKind::Influences, NodeId{3}, NodeId{4}};
    CHECK(e.id.value == 7);
    CHECK(e.kind == EdgeKind::Influences);
    CHECK(e.src.value == 3);
    CHECK(e.dst.value == 4);
}
```

- [ ] **Step 2: Run to confirm fail**

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -5`
Expected: FAIL.

- [ ] **Step 3: Implement `aleph.types-edge.cppm`**

```cpp
module;
#include <array>
#include <cstdint>
#include <string_view>

export module aleph.types:edge;

import :id;
import :node;

export namespace aleph::types {

enum class EdgeKind : std::uint8_t {
    Adjacent   = 0,
    Contains   = 1,
    Influences = 2,
    References = 3,
};

inline constexpr std::array<EdgeKind, 4> all_edge_kinds() noexcept {
    return {EdgeKind::Adjacent, EdgeKind::Contains,
            EdgeKind::Influences, EdgeKind::References};
}

constexpr std::string_view as_tla(EdgeKind k) noexcept {
    switch (k) {
        case EdgeKind::Adjacent:   return "adjacent";
        case EdgeKind::Contains:   return "contains";
        case EdgeKind::Influences: return "influences";
        case EdgeKind::References: return "references";
    }
    return "";
}

// Type-compatibility matrix. Mirrors EdgeKind::allows in
// /home/lkz/aleph-engine/aleph-types/src/edge.rs and EdgeTypeCompat
// in formal/scene_graph.tla.
constexpr bool allows(EdgeKind kind, NodeKind src, NodeKind dst) noexcept {
    switch (kind) {
        case EdgeKind::Adjacent:
            return src == NodeKind::Mesh && dst == NodeKind::Mesh;
        case EdgeKind::Contains:
            if (src != NodeKind::Transform) return false;
            return dst == NodeKind::Transform
                || dst == NodeKind::Mesh
                || dst == NodeKind::Light
                || dst == NodeKind::Camera
                || dst == NodeKind::Volume;
        case EdgeKind::Influences:
            if (dst != NodeKind::Mesh) return false;
            return src == NodeKind::Light
                || src == NodeKind::Volume
                || src == NodeKind::Material;
        case EdgeKind::References:
            return (src == NodeKind::Mesh     && dst == NodeKind::Material)
                || (src == NodeKind::Material && dst == NodeKind::Texture);
    }
    return false;
}

struct Edge {
    EdgeId   id{};
    EdgeKind kind{EdgeKind::Adjacent};
    NodeId   src{};
    NodeId   dst{};
};

}  // namespace aleph::types
```

- [ ] **Step 4: Add to module CMake**

Modify `graph/src/aleph.types/CMakeLists.txt`:

```cmake
add_library(aleph_types)
target_sources(aleph_types
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.types-id.cppm
        aleph.types-attribute.cppm
        aleph.types-node.cppm
        aleph.types-edge.cppm)
target_link_libraries(aleph_types PRIVATE aleph_flags_isa)
```

- [ ] **Step 5: Add test file to tests/CMakeLists.txt**

After `graph/test_node.cpp` add `graph/test_edge.cpp`.

- [ ] **Step 6: Build + test**

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -5 && ctest --test-dir build-release -R aleph_tests --output-on-failure 2>&1 | tail -10`

Expected: PASS — 6 new test cases.

- [ ] **Step 7: Commit**

```bash
git add graph/src/aleph.types/aleph.types-edge.cppm graph/src/aleph.types/CMakeLists.txt tests/graph/test_edge.cpp tests/CMakeLists.txt
git commit -m "task 7: aleph.types:edge — EdgeKind + allows() compatibility matrix + Edge struct"
```

---

## Task 8: `aleph.types` umbrella module + isolation test

**Files:**
- Create: `graph/src/aleph.types/aleph.types.cppm`
- Create: `tests/isolation/iso_types.cpp`
- Modify: `graph/src/aleph.types/CMakeLists.txt`
- Modify: `tests/isolation/CMakeLists.txt`

- [ ] **Step 1: Create the umbrella module**

```cpp
// graph/src/aleph.types/aleph.types.cppm
export module aleph.types;
export import :id;
export import :attribute;
export import :node;
export import :edge;
```

- [ ] **Step 2: Update CMake to include the umbrella**

Modify `graph/src/aleph.types/CMakeLists.txt`:

```cmake
add_library(aleph_types)
target_sources(aleph_types
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.types.cppm
        aleph.types-id.cppm
        aleph.types-attribute.cppm
        aleph.types-node.cppm
        aleph.types-edge.cppm)
target_link_libraries(aleph_types PRIVATE aleph_flags_isa)
```

- [ ] **Step 3: Create the isolation test**

```cpp
// tests/isolation/iso_types.cpp
import aleph.types;

int main() {
    using namespace aleph::types;
    Node n = Mesh{NodeId{0}, std::string("x"), 1};
    return kind_of(n) == NodeKind::Mesh ? 0 : 1;
}
```

- [ ] **Step 4: Register isolation test**

Modify `tests/isolation/CMakeLists.txt` — add after the existing entries:

```cmake
aleph_iso_test(types aleph_types)
```

- [ ] **Step 5: Build + test**

Run: `cmake --build build-release 2>&1 | tail -3 && ctest --test-dir build-release --output-on-failure 2>&1 | tail -15`

Expected: build OK; `iso_types` passes (returns 0); all existing tests still pass.

- [ ] **Step 6: Commit**

```bash
git add graph/src/aleph.types/aleph.types.cppm graph/src/aleph.types/CMakeLists.txt tests/isolation/iso_types.cpp tests/isolation/CMakeLists.txt
git commit -m "task 8: aleph.types umbrella module + iso_types isolation test"
```

---

## Task 9: `aleph.graph:graph` — `Graph` struct + node ops

**Files:**
- Create: `graph/src/aleph.graph/aleph.graph-graph.cppm`
- Create: `tests/graph/test_graph_basic.cpp`
- Modify: `graph/src/aleph.graph/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

This task ships the `Graph` struct + `alloc_node_id` / `alloc_edge_id` / `insert_node` / `remove_node_cascade` / accessors. Task 10 adds `add_edge` and edge-side operations.

- [ ] **Step 1: Write the failing test**

Create `tests/graph/test_graph_basic.cpp`:

```cpp
#include "doctest.h"
import aleph.graph;
import aleph.types;

#include <string>

using namespace aleph::graph;
using namespace aleph::types;

TEST_CASE("Graph: empty by default") {
    Graph g;
    CHECK(g.node_count() == 0);
    CHECK(g.edge_count() == 0);
}

TEST_CASE("Graph: alloc_node_id is monotonic and independent of alloc_edge_id") {
    Graph g;
    CHECK(g.alloc_node_id().value == 0);
    CHECK(g.alloc_node_id().value == 1);
    CHECK(g.alloc_edge_id().value == 0);
    CHECK(g.alloc_node_id().value == 2);
}

TEST_CASE("Graph: insert_node + lookup") {
    Graph g;
    auto id = g.alloc_node_id();
    g.insert_node(Mesh{id, std::string("cube"), 12});
    CHECK(g.node_count() == 1);
    const Node* n = g.node(id);
    REQUIRE(n != nullptr);
    CHECK(kind_of(*n) == NodeKind::Mesh);
    CHECK(id_of(*n) == id);
}

TEST_CASE("Graph: node returns nullptr on miss") {
    Graph g;
    CHECK(g.node(NodeId{999}) == nullptr);
}

TEST_CASE("Graph: nodes() iteration is insertion order") {
    Graph g;
    auto a = g.alloc_node_id(); g.insert_node(Mesh{a, std::string("a"), 1});
    auto b = g.alloc_node_id(); g.insert_node(Material{b, MaterialKind::Lambertian});
    auto c = g.alloc_node_id(); g.insert_node(Camera{c, std::string("cam")});

    std::vector<NodeId> ids;
    for (auto [id, node] : g.nodes()) ids.push_back(id);
    CHECK(ids.size() == 3);
    CHECK(ids[0] == a);
    CHECK(ids[1] == b);
    CHECK(ids[2] == c);
}

TEST_CASE("Graph: remove_node_cascade removes node (edges in task 10)") {
    Graph g;
    auto a = g.alloc_node_id(); g.insert_node(Mesh{a, std::string("a"), 1});
    auto b = g.alloc_node_id(); g.insert_node(Mesh{b, std::string("b"), 2});
    g.remove_node_cascade(a);
    CHECK(g.node_count() == 1);
    CHECK(g.node(a) == nullptr);
    CHECK(g.node(b) != nullptr);
}
```

- [ ] **Step 2: Run to confirm fail**

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -5`
Expected: FAIL — module `aleph.graph` not found.

- [ ] **Step 3: Implement `aleph.graph-graph.cppm` (node-side only)**

```cpp
module;
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <utility>

export module aleph.graph:graph;

import aleph.containers;
import aleph.types;

export namespace aleph::graph {

// Errors returned by graph mutation primitives. Invariant violations
// detected post-mutation surface separately as InvariantError.
enum class GraphError {
    NodeNotFound,
    EdgeNotFound,
    EdgeTypeMismatch,
};

// Typed attributed graph G = (V, E, τ, α). τ (typing) lives in each
// node/edge's `kind`; α (attributes) lives in each node's payload.
class Graph {
public:
    using NodeMap = aleph::containers::OrderedMap<aleph::types::NodeId,  aleph::types::Node>;
    using EdgeMap = aleph::containers::OrderedMap<aleph::types::EdgeId,  aleph::types::Edge>;

    Graph() = default;

    // ── ID allocation ─────────────────────────────────────────────
    aleph::types::NodeId alloc_node_id() noexcept { return ids_.alloc_node(); }
    aleph::types::EdgeId alloc_edge_id() noexcept { return ids_.alloc_edge(); }

    // ── Node ops ──────────────────────────────────────────────────
    // Aborts on id collision (allocator misuse). Use alloc_node_id()
    // to get a fresh id before calling.
    void insert_node(aleph::types::Node n) {
        const aleph::types::NodeId id = aleph::types::id_of(n);
        const bool fresh = nodes_.insert(id, std::move(n));
        if (!fresh) std::abort();   // allocator misuse: re-issued id
    }

    const aleph::types::Node* node(aleph::types::NodeId id) const noexcept {
        return nodes_.get(id);
    }

    std::size_t node_count() const noexcept { return nodes_.size(); }

    NodeMap::const_iterator nodes_begin() const noexcept { return nodes_.cbegin(); }
    NodeMap::const_iterator nodes_end()   const noexcept { return nodes_.cend(); }

    // Range-for support via lightweight view objects (= IteratorRange).
    struct NodeRange {
        const NodeMap* m;
        NodeMap::const_iterator begin() const noexcept { return m->cbegin(); }
        NodeMap::const_iterator end()   const noexcept { return m->cend(); }
    };
    NodeRange nodes() const noexcept { return {&nodes_}; }

    // Stub for edges-side; Task 10 fills in.
    std::size_t edge_count() const noexcept { return edges_.size(); }

    // Removes node + (in Task 10) cascades incident edges.
    void remove_node_cascade(aleph::types::NodeId id) {
        // Task 10 adds the edge-cascade loop above this line.
        (void)nodes_.remove(id);
    }

protected:
    NodeMap                    nodes_{};
    EdgeMap                    edges_{};
    aleph::types::IdAllocator  ids_{};
};

}  // namespace aleph::graph
```

- [ ] **Step 4: Wire into `aleph_graph` library**

Modify `graph/src/aleph.graph/CMakeLists.txt`:

```cmake
add_library(aleph_graph)
target_sources(aleph_graph
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.graph-graph.cppm)
target_link_libraries(aleph_graph
    PUBLIC  aleph_types aleph_containers
    PRIVATE aleph_flags_isa)
```

- [ ] **Step 5: Wire test + add library to test link list**

In `tests/CMakeLists.txt`:
- Add `graph/test_graph_basic.cpp` to sources (after `graph/test_edge.cpp`).
- Append `aleph_graph` to the `target_link_libraries(aleph_tests PRIVATE ...)` list (after `aleph_types`).

The link list becomes:

```cmake
target_link_libraries(aleph_tests PRIVATE
    aleph_flags_test aleph_cpu aleph_math aleph_alloc aleph_containers aleph_threads aleph_io
    aleph_types aleph_graph
    aleph_scene aleph_render_common aleph_render_rt aleph_render_sw)
```

- [ ] **Step 6: Build + test**

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -5 && ctest --test-dir build-release -R aleph_tests --output-on-failure 2>&1 | tail -10`

Expected: PASS — 6 new test cases.

- [ ] **Step 7: Commit**

```bash
git add graph/src/aleph.graph/aleph.graph-graph.cppm graph/src/aleph.graph/CMakeLists.txt tests/graph/test_graph_basic.cpp tests/CMakeLists.txt
git commit -m "task 9: aleph.graph:graph — Graph struct + insert_node + node accessors"
```

---

## Task 10: `aleph.graph:graph` — `add_edge` + `remove_node_cascade` (with edges)

**Files:**
- Modify: `graph/src/aleph.graph/aleph.graph-graph.cppm`
- Create: `tests/graph/test_graph_edges.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/graph/test_graph_edges.cpp`:

```cpp
#include "doctest.h"
import aleph.graph;
import aleph.types;

#include <string>
#include <vector>

using namespace aleph::graph;
using namespace aleph::types;

static auto make_basic_two_mesh() {
    struct Out { Graph g; NodeId a; NodeId b; NodeId mat; };
    Graph g;
    auto a = g.alloc_node_id(); g.insert_node(Mesh{a, std::string("a"), 1});
    auto b = g.alloc_node_id(); g.insert_node(Mesh{b, std::string("b"), 1});
    auto mat = g.alloc_node_id(); g.insert_node(Material{mat, MaterialKind::Lambertian});
    return Out{std::move(g), a, b, mat};
}

TEST_CASE("Graph::add_edge: success on compatible types") {
    auto [g, a, b, mat] = make_basic_two_mesh();
    auto e = g.add_edge(EdgeKind::Adjacent, a, b);
    REQUIRE(e.has_value());
    CHECK(g.edge_count() == 1);
    const Edge* ep = g.edge(*e);
    REQUIRE(ep != nullptr);
    CHECK(ep->kind == EdgeKind::Adjacent);
    CHECK(ep->src == a);
    CHECK(ep->dst == b);
}

TEST_CASE("Graph::add_edge: rejects incompatible types") {
    auto [g, a, b, mat] = make_basic_two_mesh();
    auto e = g.add_edge(EdgeKind::Adjacent, a, mat);  // Mesh-Mesh required
    REQUIRE(!e.has_value());
    CHECK(e.error() == GraphError::EdgeTypeMismatch);
    CHECK(g.edge_count() == 0);
}

TEST_CASE("Graph::add_edge: rejects unknown src/dst") {
    auto [g, a, b, mat] = make_basic_two_mesh();
    auto e = g.add_edge(EdgeKind::Adjacent, a, NodeId{999});
    REQUIRE(!e.has_value());
    CHECK(e.error() == GraphError::NodeNotFound);
}

TEST_CASE("Graph::edges() iterates in insertion order") {
    auto [g, a, b, mat] = make_basic_two_mesh();
    auto e1 = *g.add_edge(EdgeKind::References, a, mat);
    auto e2 = *g.add_edge(EdgeKind::References, b, mat);
    auto e3 = *g.add_edge(EdgeKind::Adjacent,   a, b);

    std::vector<EdgeId> ids;
    for (auto [id, e] : g.edges()) ids.push_back(id);
    CHECK(ids.size() == 3);
    CHECK(ids[0] == e1);
    CHECK(ids[1] == e2);
    CHECK(ids[2] == e3);
}

TEST_CASE("Graph::remove_node_cascade: cascades incident edges") {
    auto [g, a, b, mat] = make_basic_two_mesh();
    auto e1 = *g.add_edge(EdgeKind::References, a, mat);
    auto e2 = *g.add_edge(EdgeKind::References, b, mat);
    auto e3 = *g.add_edge(EdgeKind::Adjacent,   a, b);
    CHECK(g.edge_count() == 3);

    g.remove_node_cascade(a);   // should drop e1 and e3
    CHECK(g.node_count() == 2);
    CHECK(g.edge_count() == 1);
    CHECK(g.edge(e1) == nullptr);
    CHECK(g.edge(e2) != nullptr);
    CHECK(g.edge(e3) == nullptr);
}

TEST_CASE("Graph::in_degree: counts incoming edges by dst") {
    auto [g, a, b, mat] = make_basic_two_mesh();
    g.add_edge(EdgeKind::References, a, mat);
    g.add_edge(EdgeKind::References, b, mat);
    CHECK(g.in_degree(mat) == 2);
    CHECK(g.in_degree(a)   == 0);
}
```

- [ ] **Step 2: Run to confirm fail**

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -5`
Expected: FAIL — `add_edge`, `edge()`, `in_degree`, edges-cascade undefined.

- [ ] **Step 3: Add `add_edge` + edges-side methods + cascade**

In `graph/src/aleph.graph/aleph.graph-graph.cppm`, add inside the `Graph` class body (after the node methods, before `protected:`):

```cpp
    // ── Edge ops ──────────────────────────────────────────────────
    std::expected<aleph::types::EdgeId, GraphError>
    add_edge(aleph::types::EdgeKind kind,
             aleph::types::NodeId   src,
             aleph::types::NodeId   dst) {
        const aleph::types::Node* sn = nodes_.get(src);
        if (!sn) return std::unexpected(GraphError::NodeNotFound);
        const aleph::types::Node* dn = nodes_.get(dst);
        if (!dn) return std::unexpected(GraphError::NodeNotFound);
        if (!aleph::types::allows(kind, aleph::types::kind_of(*sn), aleph::types::kind_of(*dn))) {
            return std::unexpected(GraphError::EdgeTypeMismatch);
        }
        const aleph::types::EdgeId id = ids_.alloc_edge();
        edges_.insert(id, aleph::types::Edge{id, kind, src, dst});
        return id;
    }

    const aleph::types::Edge* edge(aleph::types::EdgeId id) const noexcept {
        return edges_.get(id);
    }

    struct EdgeRange {
        const EdgeMap* m;
        EdgeMap::const_iterator begin() const noexcept { return m->cbegin(); }
        EdgeMap::const_iterator end()   const noexcept { return m->cend(); }
    };
    EdgeRange edges() const noexcept { return {&edges_}; }

    std::size_t in_degree(aleph::types::NodeId id) const noexcept {
        std::size_t n = 0;
        for (auto [eid, e] : edges_) if (e.dst == id) ++n;
        return n;
    }
```

Replace the stub `remove_node_cascade` with the cascade-aware version:

```cpp
    void remove_node_cascade(aleph::types::NodeId id) {
        // Collect incident edge ids first; we can't mutate while iterating.
        std::vector<aleph::types::EdgeId> incident;
        for (auto [eid, e] : edges_) {
            if (e.src == id || e.dst == id) incident.push_back(eid);
        }
        for (auto eid : incident) (void)edges_.remove(eid);
        (void)nodes_.remove(id);
    }
```

Add `#include <vector>` to the module preamble at the top.

- [ ] **Step 4: Add the test to `tests/CMakeLists.txt`**

After `graph/test_graph_basic.cpp` add `graph/test_graph_edges.cpp`.

- [ ] **Step 5: Build + test**

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -5 && ctest --test-dir build-release -R aleph_tests --output-on-failure 2>&1 | tail -10`

Expected: PASS — 6 new test cases.

- [ ] **Step 6: Commit**

```bash
git add graph/src/aleph.graph/aleph.graph-graph.cppm tests/graph/test_graph_edges.cpp tests/CMakeLists.txt
git commit -m "task 10: aleph.graph:graph — add_edge + edge accessors + remove_node_cascade"
```

---

## Task 11: `aleph.graph:invariants` — invariants 1–5 + error type

**Files:**
- Create: `graph/src/aleph.graph/aleph.graph-invariants.cppm`
- Create: `tests/graph/test_invariants_1_5.cpp`
- Modify: `graph/src/aleph.graph/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/graph/test_invariants_1_5.cpp`:

```cpp
#include "doctest.h"
import aleph.graph;
import aleph.types;

#include <string>

using namespace aleph::graph;
using namespace aleph::types;

TEST_CASE("INVARIANT_NAMES: 10 canonical names in spec order") {
    CHECK(INVARIANT_NAMES.size() == 10);
    CHECK(INVARIANT_NAMES[0] == "TypedNodes");
    CHECK(INVARIANT_NAMES[1] == "TypedEdges");
    CHECK(INVARIANT_NAMES[2] == "EdgeEndpointsExist");
    CHECK(INVARIANT_NAMES[3] == "EdgeTypeCompat");
    CHECK(INVARIANT_NAMES[4] == "TransformAcyclic");
}

TEST_CASE("check_typed_nodes / check_typed_edges: vacuously true on empty graph") {
    Graph g;
    CHECK(check_typed_nodes(g).has_value());
    CHECK(check_typed_edges(g).has_value());
}

TEST_CASE("check_edge_endpoints_exist: passes when all endpoints present") {
    Graph g;
    auto a = g.alloc_node_id(); g.insert_node(Mesh{a, std::string("a"), 1});
    auto b = g.alloc_node_id(); g.insert_node(Mesh{b, std::string("b"), 1});
    g.add_edge(EdgeKind::Adjacent, a, b);
    CHECK(check_edge_endpoints_exist(g).has_value());
}

TEST_CASE("check_edge_type_compat: passes on a well-typed graph") {
    Graph g;
    auto a   = g.alloc_node_id(); g.insert_node(Mesh{a, std::string("a"), 1});
    auto b   = g.alloc_node_id(); g.insert_node(Mesh{b, std::string("b"), 1});
    auto mat = g.alloc_node_id(); g.insert_node(Material{mat, MaterialKind::Lambertian});
    g.add_edge(EdgeKind::Adjacent,   a, b);
    g.add_edge(EdgeKind::References, a, mat);
    CHECK(check_edge_type_compat(g).has_value());
}

TEST_CASE("check_transform_acyclic: detects Contains cycle among Transforms") {
    Graph g;
    auto t1 = g.alloc_node_id(); g.insert_node(Transform{t1, 0});
    auto t2 = g.alloc_node_id(); g.insert_node(Transform{t2, 1});
    auto t3 = g.alloc_node_id(); g.insert_node(Transform{t3, 2});
    g.add_edge(EdgeKind::Contains, t1, t2);
    g.add_edge(EdgeKind::Contains, t2, t3);
    g.add_edge(EdgeKind::Contains, t3, t1);   // cycle!
    auto r = check_transform_acyclic(g);
    REQUIRE(!r.has_value());
    CHECK(r.error() == InvariantError::TransformAcyclic);
}

TEST_CASE("check_transform_acyclic: passes on a tree") {
    Graph g;
    auto root  = g.alloc_node_id(); g.insert_node(Transform{root, 0});
    auto left  = g.alloc_node_id(); g.insert_node(Transform{left, 1});
    auto right = g.alloc_node_id(); g.insert_node(Transform{right, 2});
    g.add_edge(EdgeKind::Contains, root, left);
    g.add_edge(EdgeKind::Contains, root, right);
    CHECK(check_transform_acyclic(g).has_value());
}
```

- [ ] **Step 2: Run to confirm fail**

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -5`
Expected: FAIL — `InvariantError`, `INVARIANT_NAMES`, `check_*` undefined.

- [ ] **Step 3: Implement invariants 1–5**

Create `graph/src/aleph.graph/aleph.graph-invariants.cppm`:

```cpp
module;
#include <array>
#include <cstdint>
#include <expected>
#include <string_view>
#include <unordered_set>
#include <vector>

export module aleph.graph:invariants;

import aleph.types;
import :graph;

export namespace aleph::graph {

enum class InvariantError : std::uint8_t {
    TypedNodes              = 0,
    TypedEdges              = 1,
    EdgeEndpointsExist      = 2,
    EdgeTypeCompat          = 3,
    TransformAcyclic        = 4,
    CameraExclusive         = 5,
    MaterialReferenced      = 6,
    UniqueIds               = 7,
    ContainsAntireflexive   = 8,
    BoundedDegree           = 9,
};

inline constexpr std::array<std::string_view, 10> INVARIANT_NAMES = {
    "TypedNodes",
    "TypedEdges",
    "EdgeEndpointsExist",
    "EdgeTypeCompat",
    "TransformAcyclic",
    "CameraExclusive",
    "MaterialReferenced",
    "UniqueIds",
    "ContainsAntireflexive",
    "BoundedDegree",
};

// ── Invariant 1: TypedNodes ─────────────────────────────────────────
// Every node's kind is in NodeKind::ALL. Tagged-union makes this
// trivially true at the C++ level; kept for parity with TLA+ which
// can model untyped values.
inline std::expected<void, InvariantError> check_typed_nodes(const Graph&) noexcept {
    return {};
}

// ── Invariant 2: TypedEdges ─────────────────────────────────────────
inline std::expected<void, InvariantError> check_typed_edges(const Graph&) noexcept {
    return {};
}

// ── Invariant 3: EdgeEndpointsExist ─────────────────────────────────
inline std::expected<void, InvariantError>
check_edge_endpoints_exist(const Graph& g) noexcept {
    for (auto [eid, e] : g.edges()) {
        if (!g.node(e.src) || !g.node(e.dst)) {
            return std::unexpected(InvariantError::EdgeEndpointsExist);
        }
    }
    return {};
}

// ── Invariant 4: EdgeTypeCompat ─────────────────────────────────────
inline std::expected<void, InvariantError>
check_edge_type_compat(const Graph& g) noexcept {
    for (auto [eid, e] : g.edges()) {
        const aleph::types::Node* sn = g.node(e.src);
        const aleph::types::Node* dn = g.node(e.dst);
        if (!sn || !dn) {
            // covered by EdgeEndpointsExist, but stay safe
            return std::unexpected(InvariantError::EdgeTypeCompat);
        }
        if (!aleph::types::allows(e.kind, aleph::types::kind_of(*sn), aleph::types::kind_of(*dn))) {
            return std::unexpected(InvariantError::EdgeTypeCompat);
        }
    }
    return {};
}

// ── Invariant 5: TransformAcyclic ───────────────────────────────────
// Contains-only subgraph restricted to Transform nodes must be a DAG.
inline std::expected<void, InvariantError>
check_transform_acyclic(const Graph& g) noexcept {
    // DFS with WHITE/GRAY/BLACK. GRAY-edge target => cycle.
    enum { WHITE, GRAY, BLACK };
    std::unordered_map<std::uint32_t, int> color;   // NodeId.value -> color

    // Collect transform nodes
    std::vector<aleph::types::NodeId> ts;
    for (auto [nid, n] : g.nodes()) {
        if (aleph::types::kind_of(n) == aleph::types::NodeKind::Transform) {
            ts.push_back(nid);
            color[nid.value] = WHITE;
        }
    }

    // For each transform, run DFS along Contains edges.
    std::vector<std::pair<aleph::types::NodeId, std::size_t>> stack;
    // Pre-compute adjacency: NodeId.value -> list of contains-children NodeIds
    std::unordered_map<std::uint32_t, std::vector<aleph::types::NodeId>> kids;
    for (auto [eid, e] : g.edges()) {
        if (e.kind != aleph::types::EdgeKind::Contains) continue;
        const aleph::types::Node* sn = g.node(e.src);
        const aleph::types::Node* dn = g.node(e.dst);
        if (!sn || !dn) continue;
        if (aleph::types::kind_of(*sn) != aleph::types::NodeKind::Transform) continue;
        if (aleph::types::kind_of(*dn) != aleph::types::NodeKind::Transform) continue;
        kids[e.src.value].push_back(e.dst);
    }

    for (auto root : ts) {
        if (color[root.value] != WHITE) continue;
        stack.clear();
        stack.push_back({root, 0});
        color[root.value] = GRAY;
        while (!stack.empty()) {
            auto& [u, i] = stack.back();
            const auto& children = kids[u.value];
            if (i < children.size()) {
                auto v = children[i++];
                int c = color[v.value];
                if (c == GRAY) {
                    return std::unexpected(InvariantError::TransformAcyclic);
                }
                if (c == WHITE) {
                    color[v.value] = GRAY;
                    stack.push_back({v, 0});
                }
            } else {
                color[u.value] = BLACK;
                stack.pop_back();
            }
        }
    }
    return {};
}

}  // namespace aleph::graph
```

Note: this file uses `std::unordered_map`. We accept it here (not for Graph storage — but for ephemeral local algorithm state, order doesn't matter). Add `#include <unordered_map>` to the preamble.

Adjust the preamble:

```cpp
module;
#include <array>
#include <cstdint>
#include <expected>
#include <string_view>
#include <unordered_map>
#include <vector>
```

- [ ] **Step 4: Add invariants partition to module CMake**

Modify `graph/src/aleph.graph/CMakeLists.txt`:

```cmake
add_library(aleph_graph)
target_sources(aleph_graph
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.graph-graph.cppm
        aleph.graph-invariants.cppm)
target_link_libraries(aleph_graph
    PUBLIC  aleph_types aleph_containers
    PRIVATE aleph_flags_isa)
```

- [ ] **Step 5: Add test to `tests/CMakeLists.txt`**

After `graph/test_graph_edges.cpp` add `graph/test_invariants_1_5.cpp`.

- [ ] **Step 6: Build + test**

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -5 && ctest --test-dir build-release -R aleph_tests --output-on-failure 2>&1 | tail -10`

Expected: PASS — 6 new test cases.

- [ ] **Step 7: Commit**

```bash
git add graph/src/aleph.graph/aleph.graph-invariants.cppm graph/src/aleph.graph/CMakeLists.txt tests/graph/test_invariants_1_5.cpp tests/CMakeLists.txt
git commit -m "task 11: aleph.graph:invariants — InvariantError + INVARIANT_NAMES + check 1-5"
```

---

## Task 12: invariants 6–10 + `validate_all`

**Files:**
- Modify: `graph/src/aleph.graph/aleph.graph-invariants.cppm`
- Create: `tests/graph/test_invariants_6_10.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/graph/test_invariants_6_10.cpp`:

```cpp
#include "doctest.h"
import aleph.graph;
import aleph.types;

#include <string>

using namespace aleph::graph;
using namespace aleph::types;

TEST_CASE("check_camera_exclusive: passes with one camera, fails with zero or two") {
    Graph g;
    {  // zero cameras
        auto m = g.alloc_node_id(); g.insert_node(Mesh{m, std::string("m"), 1});
        auto r = check_camera_exclusive(g);
        REQUIRE(!r.has_value());
        CHECK(r.error() == InvariantError::CameraExclusive);
    }
    auto c = g.alloc_node_id(); g.insert_node(Camera{c, std::string("cam")});
    CHECK(check_camera_exclusive(g).has_value());
    auto c2 = g.alloc_node_id(); g.insert_node(Camera{c2, std::string("alt")});
    {
        auto r = check_camera_exclusive(g);
        REQUIRE(!r.has_value());
        CHECK(r.error() == InvariantError::CameraExclusive);
    }
}

TEST_CASE("check_material_referenced: each Mesh references exactly one Material") {
    Graph g;
    auto mesh = g.alloc_node_id(); g.insert_node(Mesh{mesh, std::string("m"), 1});
    auto mat  = g.alloc_node_id(); g.insert_node(Material{mat, MaterialKind::Lambertian});
    // 0 References -> fail
    {
        auto r = check_material_referenced(g);
        REQUIRE(!r.has_value());
        CHECK(r.error() == InvariantError::MaterialReferenced);
    }
    g.add_edge(EdgeKind::References, mesh, mat);
    CHECK(check_material_referenced(g).has_value());
    // 2 References -> fail
    auto mat2 = g.alloc_node_id(); g.insert_node(Material{mat2, MaterialKind::Metal});
    g.add_edge(EdgeKind::References, mesh, mat2);
    {
        auto r = check_material_referenced(g);
        REQUIRE(!r.has_value());
        CHECK(r.error() == InvariantError::MaterialReferenced);
    }
}

TEST_CASE("check_unique_ids: graph constructor guarantees this (vacuous PASS)") {
    Graph g;
    auto a = g.alloc_node_id(); g.insert_node(Mesh{a, std::string("a"), 1});
    auto b = g.alloc_node_id(); g.insert_node(Mesh{b, std::string("b"), 1});
    CHECK(check_unique_ids(g).has_value());
}

TEST_CASE("check_contains_antireflexive: Contains must not be symmetric") {
    Graph g;
    auto t1 = g.alloc_node_id(); g.insert_node(Transform{t1, 0});
    auto t2 = g.alloc_node_id(); g.insert_node(Transform{t2, 1});
    g.add_edge(EdgeKind::Contains, t1, t2);
    CHECK(check_contains_antireflexive(g).has_value());
    g.add_edge(EdgeKind::Contains, t2, t1);
    auto r = check_contains_antireflexive(g);
    REQUIRE(!r.has_value());
    CHECK(r.error() == InvariantError::ContainsAntireflexive);
}

TEST_CASE("check_bounded_degree: rejects in-degree above limit") {
    Graph g;
    auto mat = g.alloc_node_id(); g.insert_node(Material{mat, MaterialKind::Lambertian});
    for (int i = 0; i < 5; ++i) {
        auto m = g.alloc_node_id(); g.insert_node(Mesh{m, std::string("m"), 1});
        g.add_edge(EdgeKind::References, m, mat);
    }
    // 5 meshes -> mat in-degree 5
    CHECK(check_bounded_degree(g, 5).has_value());
    auto r = check_bounded_degree(g, 4);
    REQUIRE(!r.has_value());
    CHECK(r.error() == InvariantError::BoundedDegree);
}

TEST_CASE("validate_all: full suite on a well-formed scene") {
    Graph g;
    auto root  = g.alloc_node_id(); g.insert_node(Transform{root, 0});
    auto child = g.alloc_node_id(); g.insert_node(Transform{child, 1});
    auto cam   = g.alloc_node_id(); g.insert_node(Camera{cam, std::string("default")});
    auto light = g.alloc_node_id(); g.insert_node(Light{light, LightKind::Point, std::string("ies/std")});
    auto a     = g.alloc_node_id(); g.insert_node(Mesh{a, std::string("a"), 1});
    auto b     = g.alloc_node_id(); g.insert_node(Mesh{b, std::string("b"), 1});
    auto mat   = g.alloc_node_id(); g.insert_node(Material{mat, MaterialKind::Lambertian});
    auto tex   = g.alloc_node_id(); g.insert_node(Texture{tex, 256, 256, TextureFormat::Rgb8});
    g.add_edge(EdgeKind::Contains,   root,  child);
    g.add_edge(EdgeKind::Contains,   child, a);
    g.add_edge(EdgeKind::Contains,   child, b);
    g.add_edge(EdgeKind::Contains,   root,  cam);
    g.add_edge(EdgeKind::References, a,     mat);
    g.add_edge(EdgeKind::References, b,     mat);
    g.add_edge(EdgeKind::References, mat,   tex);
    g.add_edge(EdgeKind::Influences, light, a);

    auto r = validate_all(g, 64);
    CHECK(r.has_value());
}
```

- [ ] **Step 2: Run to confirm fail**

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -5`
Expected: FAIL — `check_camera_exclusive` etc undefined.

- [ ] **Step 3: Add invariants 6–10 + validate_all**

Append to `graph/src/aleph.graph/aleph.graph-invariants.cppm` (still inside `export namespace aleph::graph {`):

```cpp
// ── Invariant 6: CameraExclusive ────────────────────────────────────
// Exactly one Camera node in the graph.
inline std::expected<void, InvariantError>
check_camera_exclusive(const Graph& g) noexcept {
    std::size_t n = 0;
    for (auto [nid, node] : g.nodes()) {
        if (aleph::types::kind_of(node) == aleph::types::NodeKind::Camera) ++n;
    }
    return (n == 1) ? std::expected<void, InvariantError>{}
                    : std::unexpected(InvariantError::CameraExclusive);
}

// ── Invariant 7: MaterialReferenced ─────────────────────────────────
// Every Mesh has exactly one References edge to a Material.
inline std::expected<void, InvariantError>
check_material_referenced(const Graph& g) noexcept {
    for (auto [mid, mnode] : g.nodes()) {
        if (aleph::types::kind_of(mnode) != aleph::types::NodeKind::Mesh) continue;
        std::size_t mat_refs = 0;
        for (auto [eid, e] : g.edges()) {
            if (e.kind != aleph::types::EdgeKind::References) continue;
            if (e.src != mid) continue;
            const aleph::types::Node* dn = g.node(e.dst);
            if (dn && aleph::types::kind_of(*dn) == aleph::types::NodeKind::Material) ++mat_refs;
        }
        if (mat_refs != 1) return std::unexpected(InvariantError::MaterialReferenced);
    }
    return {};
}

// ── Invariant 8: UniqueIds ──────────────────────────────────────────
// OrderedMap key uniqueness already enforces this. Vacuous PASS.
inline std::expected<void, InvariantError>
check_unique_ids(const Graph&) noexcept { return {}; }

// ── Invariant 9: ContainsAntireflexive ──────────────────────────────
// No pair (a, b) has BOTH Contains(a, b) AND Contains(b, a).
inline std::expected<void, InvariantError>
check_contains_antireflexive(const Graph& g) noexcept {
    // Collect Contains edges as (src, dst) pairs; check no reverse exists.
    std::vector<std::pair<aleph::types::NodeId, aleph::types::NodeId>> pairs;
    for (auto [eid, e] : g.edges()) {
        if (e.kind == aleph::types::EdgeKind::Contains) pairs.push_back({e.src, e.dst});
    }
    for (const auto& [a, b] : pairs) {
        for (const auto& [c, d] : pairs) {
            if (a == d && b == c) {
                return std::unexpected(InvariantError::ContainsAntireflexive);
            }
        }
    }
    return {};
}

// ── Invariant 10: BoundedDegree ─────────────────────────────────────
inline std::expected<void, InvariantError>
check_bounded_degree(const Graph& g, std::size_t limit) noexcept {
    for (auto [nid, n] : g.nodes()) {
        if (g.in_degree(nid) > limit) {
            return std::unexpected(InvariantError::BoundedDegree);
        }
    }
    return {};
}

// ── validate_all: runs every check in canonical order ───────────────
inline std::expected<void, InvariantError>
validate_all(const Graph& g, std::size_t max_in_degree) noexcept {
    if (auto r = check_typed_nodes(g);           !r.has_value()) return r;
    if (auto r = check_typed_edges(g);           !r.has_value()) return r;
    if (auto r = check_edge_endpoints_exist(g);  !r.has_value()) return r;
    if (auto r = check_edge_type_compat(g);      !r.has_value()) return r;
    if (auto r = check_transform_acyclic(g);     !r.has_value()) return r;
    if (auto r = check_camera_exclusive(g);      !r.has_value()) return r;
    if (auto r = check_material_referenced(g);   !r.has_value()) return r;
    if (auto r = check_unique_ids(g);            !r.has_value()) return r;
    if (auto r = check_contains_antireflexive(g);!r.has_value()) return r;
    if (auto r = check_bounded_degree(g, max_in_degree); !r.has_value()) return r;
    return {};
}
```

- [ ] **Step 4: Add test to `tests/CMakeLists.txt`**

After `graph/test_invariants_1_5.cpp` add `graph/test_invariants_6_10.cpp`.

- [ ] **Step 5: Build + test**

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -5 && ctest --test-dir build-release -R aleph_tests --output-on-failure 2>&1 | tail -10`

Expected: PASS — 6 new test cases.

- [ ] **Step 6: Commit**

```bash
git add graph/src/aleph.graph/aleph.graph-invariants.cppm tests/graph/test_invariants_6_10.cpp tests/CMakeLists.txt
git commit -m "task 12: aleph.graph:invariants — checks 6-10 + validate_all"
```

---

## Task 13: `aleph.graph` umbrella module + isolation test

**Files:**
- Create: `graph/src/aleph.graph/aleph.graph.cppm`
- Create: `tests/isolation/iso_graph.cpp`
- Modify: `graph/src/aleph.graph/CMakeLists.txt`
- Modify: `tests/isolation/CMakeLists.txt`

- [ ] **Step 1: Create the umbrella**

```cpp
// graph/src/aleph.graph/aleph.graph.cppm
export module aleph.graph;
export import :graph;
export import :invariants;
```

- [ ] **Step 2: Update module CMake**

Modify `graph/src/aleph.graph/CMakeLists.txt`:

```cmake
add_library(aleph_graph)
target_sources(aleph_graph
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.graph.cppm
        aleph.graph-graph.cppm
        aleph.graph-invariants.cppm)
target_link_libraries(aleph_graph
    PUBLIC  aleph_types aleph_containers
    PRIVATE aleph_flags_isa)
```

- [ ] **Step 3: Create the isolation test**

```cpp
// tests/isolation/iso_graph.cpp
import aleph.graph;
import aleph.types;

#include <string>

int main() {
    aleph::graph::Graph g;
    auto a = g.alloc_node_id();
    g.insert_node(aleph::types::Mesh{a, std::string("x"), 1});
    auto b = g.alloc_node_id();
    g.insert_node(aleph::types::Mesh{b, std::string("y"), 1});
    g.add_edge(aleph::types::EdgeKind::Adjacent, a, b);
    return g.edge_count() == 1 ? 0 : 1;
}
```

- [ ] **Step 4: Register isolation test**

Add to `tests/isolation/CMakeLists.txt`:

```cmake
aleph_iso_test(graph aleph_graph)
```

- [ ] **Step 5: Build + test**

Run: `cmake --build build-release 2>&1 | tail -3 && ctest --test-dir build-release --output-on-failure 2>&1 | tail -15`

Expected: build OK; `iso_graph` and `iso_types` pass; all existing tests still pass.

- [ ] **Step 6: Commit**

```bash
git add graph/src/aleph.graph/aleph.graph.cppm graph/src/aleph.graph/CMakeLists.txt tests/isolation/iso_graph.cpp tests/isolation/CMakeLists.txt
git commit -m "task 13: aleph.graph umbrella module + iso_graph isolation test"
```

---

## Task 14: `formal/` directory — TLA+ specs

**Files:**
- Create: `formal/scene_graph.tla` (copy from aleph-engine)
- Create: `formal/dpo_rules.tla` (copy from aleph-engine; parsed in 4b)
- Create: `formal/scene_graph.cfg` (copy from aleph-engine)
- Create: `formal/dpo_rules.cfg` (copy from aleph-engine)
- Create: `formal/check.sh` (copy + adapt from aleph-engine)
- Create: `formal/README.md`

These files are direct copies — TLA+ is language-agnostic, the spec is the same one the Rust workspace uses.

- [ ] **Step 1: Copy the four .tla / .cfg files**

```bash
cp /home/lkz/aleph-engine/formal/scene_graph.tla   /home/lkz/aleph-cxx/formal/scene_graph.tla
cp /home/lkz/aleph-engine/formal/scene_graph.cfg   /home/lkz/aleph-cxx/formal/scene_graph.cfg
cp /home/lkz/aleph-engine/formal/dpo_rules.tla     /home/lkz/aleph-cxx/formal/dpo_rules.tla
cp /home/lkz/aleph-engine/formal/dpo_rules.cfg     /home/lkz/aleph-cxx/formal/dpo_rules.cfg
cp /home/lkz/aleph-engine/formal/check.sh          /home/lkz/aleph-cxx/formal/check.sh
chmod +x /home/lkz/aleph-cxx/formal/check.sh
```

- [ ] **Step 2: Create a short README explaining the relationship**

Create `formal/README.md`:

```markdown
# TLA+ specs (aleph-cxx)

These are byte-identical copies of `aleph-engine/formal/{scene_graph,dpo_rules}.{tla,cfg}`.
The aleph-cxx C++26 enums + invariant names + rule names must mirror them
exactly — verified by `tests/tla_cxx_sync.cpp`.

To run TLC against the specs:

```sh
TLA_TOOLS=/tmp/tla2tools.jar ./formal/check.sh
```

If `tla2tools.jar` is missing, `check.sh` prints a download hint and exits 1.
TLC is **not** required for the C++ build — it's a separate model-checking
step. The C++ side has its own `tla_cxx_sync` regression test that doesn't
need a JVM.

## Files

| File              | What it specifies                                       |
|-------------------|----------------------------------------------------------|
| `scene_graph.tla` | Typed graph G=(V,E,τ,α) + 10 well-formedness invariants |
| `scene_graph.cfg` | TLC config: `NodesMax=8`, `EdgesMax=12`, `MaxDegree=8`  |
| `dpo_rules.tla`   | 4 DPO rules (spawn_light, remove_object, replace_material, refine_cell); parsed by tla_cxx_sync in Sub-phase 4b |
| `dpo_rules.cfg`   | TLC config for DPO rule preservation checking            |
| `check.sh`        | One-liner that runs TLC on both .tla files               |
```

- [ ] **Step 3: Verify the files copied correctly**

Run: `ls -la /home/lkz/aleph-cxx/formal/ && diff /home/lkz/aleph-engine/formal/scene_graph.tla /home/lkz/aleph-cxx/formal/scene_graph.tla`

Expected: files present; diff produces no output (identical).

- [ ] **Step 4: Commit**

```bash
cd /home/lkz/aleph-cxx
git add formal/
git commit -m "task 14: formal/ — TLA+ specs copied from aleph-engine (scene_graph + dpo_rules)"
```

---

## Task 15: `tla_cxx_sync` — TLA+ parser + drift detector

**Files:**
- Create: `tests/tla_cxx_sync.cpp`
- Modify: `tests/CMakeLists.txt`

This is a single C++ source file that compiles into the `aleph_tests` executable and runs as a doctest case. It parses `formal/scene_graph.tla` and checks:

1. The set `NodeKind == { "mesh", ..., "transform" }` matches `aleph::types::all_node_kinds()` + `as_tla`.
2. The set `EdgeKind == { "adjacent", ..., "references" }` matches `all_edge_kinds()` + `as_tla`.
3. The `EdgeTypeCompat` table matches the cartesian-product check of `aleph::types::allows`.
4. The `INVARIANT names` (extracted by regex over `\* Name` block comments) match `INVARIANT_NAMES`.

- [ ] **Step 1: Write the test**

Create `tests/tla_cxx_sync.cpp`:

```cpp
#include "doctest.h"
import aleph.types;
import aleph.graph;

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in.is_open(), "failed to open " << path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Path to formal/scene_graph.tla, resolved relative to the source tree
// at build time via -DALEPH_TLA_PATH=... (set in tests/CMakeLists.txt).
constexpr const char* TLA_SCENE_GRAPH =
#ifdef ALEPH_TLA_SCENE_GRAPH_PATH
    ALEPH_TLA_SCENE_GRAPH_PATH;
#else
    "formal/scene_graph.tla";
#endif

// Extract a set literal of the form `NAME == { "a", "b", "c" }` from the .tla.
// Returns the inner strings; empty vector on parse failure.
std::vector<std::string> extract_string_set(const std::string& source,
                                              std::string_view name) {
    // Match `<name>\s*==\s*\{([^}]+)\}`
    const std::regex re(std::string(name) + R"(\s*==\s*\{([^}]+)\})");
    std::smatch m;
    if (!std::regex_search(source, m, re)) return {};
    const std::string body = m[1];
    std::vector<std::string> out;
    const std::regex str_re(R"(\"([^\"]+)\")");
    auto it = std::sregex_iterator(body.begin(), body.end(), str_re);
    auto end = std::sregex_iterator();
    for (; it != end; ++it) out.push_back((*it)[1].str());
    return out;
}

// Extract EdgeTypeCompat entries: maps "kind" -> set of (src,dst) pairs.
// Parses the table:
//   EdgeTypeCompat ==
//     [ adjacent   |-> { <<"mesh", "mesh">> },
//       contains   |-> { <<"transform", "transform">>, ... }, ... ]
std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>>
extract_edge_type_compat(const std::string& source) {
    // Find "EdgeTypeCompat == [" then read until matching closing "]"
    const std::regex header(R"(EdgeTypeCompat\s*==\s*\[)");
    std::smatch m;
    if (!std::regex_search(source, m, header)) return {};
    const std::size_t start = static_cast<std::size_t>(m.position(0)) + m.length(0);
    int depth = 1;
    std::size_t end = start;
    while (end < source.size() && depth > 0) {
        if (source[end] == '[') ++depth;
        if (source[end] == ']') --depth;
        if (depth == 0) break;
        ++end;
    }
    if (end >= source.size()) return {};
    const std::string body = source.substr(start, end - start);

    // Each entry: kind_name "|->" "{" pairs "}"
    std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> out;
    const std::regex entry(R"((\w+)\s*\|->\s*\{([^}]+)\})");
    auto it = std::sregex_iterator(body.begin(), body.end(), entry);
    auto end_it = std::sregex_iterator();
    for (; it != end_it; ++it) {
        const std::string kind = (*it)[1].str();
        const std::string pairs_body = (*it)[2].str();
        const std::regex pair_re(R"(<<\s*\"([^\"]+)\"\s*,\s*\"([^\"]+)\"\s*>>)");
        auto pit = std::sregex_iterator(pairs_body.begin(), pairs_body.end(), pair_re);
        for (; pit != end_it; ++pit) {
            out[kind].push_back({(*pit)[1].str(), (*pit)[2].str()});
        }
    }
    return out;
}

}  // namespace

TEST_CASE("tla_cxx_sync: NodeKind enum matches scene_graph.tla NodeKind set") {
    const std::string src = read_file(TLA_SCENE_GRAPH);
    auto tla_kinds = extract_string_set(src, "NodeKind");
    REQUIRE(tla_kinds.size() == 7);

    std::vector<std::string> cxx_kinds;
    for (auto k : aleph::types::all_node_kinds()) {
        cxx_kinds.push_back(std::string(aleph::types::as_tla(k)));
    }

    // Sets must match; ordering only matters for as_tla — TLA has set semantics.
    std::sort(tla_kinds.begin(), tla_kinds.end());
    std::sort(cxx_kinds.begin(), cxx_kinds.end());
    CHECK(tla_kinds == cxx_kinds);
}

TEST_CASE("tla_cxx_sync: EdgeKind enum matches scene_graph.tla EdgeKind set") {
    const std::string src = read_file(TLA_SCENE_GRAPH);
    auto tla_kinds = extract_string_set(src, "EdgeKind");
    REQUIRE(tla_kinds.size() == 4);

    std::vector<std::string> cxx_kinds;
    for (auto k : aleph::types::all_edge_kinds()) {
        cxx_kinds.push_back(std::string(aleph::types::as_tla(k)));
    }
    std::sort(tla_kinds.begin(), tla_kinds.end());
    std::sort(cxx_kinds.begin(), cxx_kinds.end());
    CHECK(tla_kinds == cxx_kinds);
}

TEST_CASE("tla_cxx_sync: EdgeTypeCompat matches aleph::types::allows") {
    const std::string src = read_file(TLA_SCENE_GRAPH);
    auto tla_table = extract_edge_type_compat(src);
    REQUIRE(tla_table.size() == 4);

    // Build the C++ table by enumerating all 7x7 NodeKind pairs per EdgeKind.
    auto build_cxx_pairs = [](aleph::types::EdgeKind k) {
        std::vector<std::pair<std::string, std::string>> v;
        for (auto a : aleph::types::all_node_kinds()) {
            for (auto b : aleph::types::all_node_kinds()) {
                if (aleph::types::allows(k, a, b)) {
                    v.push_back({std::string(aleph::types::as_tla(a)),
                                  std::string(aleph::types::as_tla(b))});
                }
            }
        }
        std::sort(v.begin(), v.end());
        return v;
    };

    for (auto k : aleph::types::all_edge_kinds()) {
        auto cxx_pairs = build_cxx_pairs(k);
        auto& tla_pairs = tla_table[std::string(aleph::types::as_tla(k))];
        std::sort(tla_pairs.begin(), tla_pairs.end());
        CHECK_MESSAGE(cxx_pairs == tla_pairs,
            "Edge kind " << aleph::types::as_tla(k) << " compat drift");
    }
}

TEST_CASE("tla_cxx_sync: INVARIANT_NAMES matches list comment in scene_graph.tla") {
    // The .tla file has a numbered block comment:
    //   \*  1. `TypedNodes`
    //   \*  2. `TypedEdges`
    // We parse those lines and check against INVARIANT_NAMES.
    const std::string src = read_file(TLA_SCENE_GRAPH);
    const std::regex inv_re(R"(\\\*\s+\d+\.\s+`(\w+)`)");
    auto it = std::sregex_iterator(src.begin(), src.end(), inv_re);
    auto end = std::sregex_iterator();
    std::vector<std::string> tla_names;
    for (; it != end; ++it) tla_names.push_back((*it)[1].str());
    REQUIRE(tla_names.size() == aleph::graph::INVARIANT_NAMES.size());
    for (std::size_t i = 0; i < tla_names.size(); ++i) {
        CHECK(tla_names[i] == std::string(aleph::graph::INVARIANT_NAMES[i]));
    }
}
```

- [ ] **Step 2: Wire the build-time TLA+ path macro into `tests/CMakeLists.txt`**

In `tests/CMakeLists.txt`, find the `target_include_directories(aleph_tests ...)` line, and after it add:

```cmake
target_compile_definitions(aleph_tests PRIVATE
    ALEPH_TLA_SCENE_GRAPH_PATH="${CMAKE_SOURCE_DIR}/formal/scene_graph.tla")
```

Add `tla_cxx_sync.cpp` to the `aleph_tests` source list (top-level, alongside `test_main.cpp` / `test_smoke.cpp`):

```cmake
    tla_cxx_sync.cpp
```

- [ ] **Step 3: Build + test**

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -5 && ctest --test-dir build-release -R aleph_tests --output-on-failure 2>&1 | tail -10`

Expected: PASS — 4 new test cases.

- [ ] **Step 4: Commit**

```bash
git add tests/tla_cxx_sync.cpp tests/CMakeLists.txt
git commit -m "task 15: tla_cxx_sync — TLA+ drift detector for NodeKind/EdgeKind/EdgeTypeCompat/INVARIANT_NAMES"
```

---

## Task 16: `apps/aleph_graph_fixture` — canonical 8-node scene binary

**Files:**
- Create: `apps/aleph_graph_fixture/main.cpp`
- Create: `apps/aleph_graph_fixture/CMakeLists.txt`
- Modify: `apps/CMakeLists.txt`

- [ ] **Step 1: Create the fixture binary**

Create `apps/aleph_graph_fixture/main.cpp`:

```cpp
#include <cstdio>
#include <string>

import aleph.graph;
import aleph.types;

// Builds the canonical 8-node fixture scene that mirrors
// FixtureInit in formal/scene_graph.tla:
//
//     0  transform (root)
//     1  transform (child)
//     2  camera
//     3  light
//     4  mesh A
//     5  mesh B
//     6  material
//     7  texture
//
//   Edges:
//     0  Contains  : root  -> child
//     1  Contains  : child -> mesh A
//     2  Contains  : child -> mesh B
//     3  Contains  : root  -> camera
//     4  References: mesh A -> material
//     5  References: mesh B -> material
//     6  References: material -> texture
//     7  Influences: light -> mesh A

using namespace aleph::graph;
using namespace aleph::types;

int main() {
    Graph g;
    const auto root  = g.alloc_node_id();
    g.insert_node(Transform{root, 0});
    const auto child = g.alloc_node_id();
    g.insert_node(Transform{child, 1});
    const auto cam   = g.alloc_node_id();
    g.insert_node(Camera{cam, std::string("default")});
    const auto light = g.alloc_node_id();
    g.insert_node(Light{light, LightKind::Point, std::string("ies/std")});
    const auto a     = g.alloc_node_id();
    g.insert_node(Mesh{a, std::string("cube"), 12});
    const auto b     = g.alloc_node_id();
    g.insert_node(Mesh{b, std::string("sphere"), 320});
    const auto mat   = g.alloc_node_id();
    g.insert_node(Material{mat, MaterialKind::Lambertian});
    const auto tex   = g.alloc_node_id();
    g.insert_node(Texture{tex, 256, 256, TextureFormat::Rgb8});

    auto check = [&](auto r, const char* what) {
        if (!r.has_value()) {
            std::fprintf(stderr, "fixture: add_edge %s failed\n", what);
            std::exit(1);
        }
    };
    check(g.add_edge(EdgeKind::Contains,   root,  child),  "root -> child");
    check(g.add_edge(EdgeKind::Contains,   child, a),       "child -> mesh A");
    check(g.add_edge(EdgeKind::Contains,   child, b),       "child -> mesh B");
    check(g.add_edge(EdgeKind::Contains,   root,  cam),     "root -> camera");
    check(g.add_edge(EdgeKind::References, a,     mat),     "mesh A -> material");
    check(g.add_edge(EdgeKind::References, b,     mat),     "mesh B -> material");
    check(g.add_edge(EdgeKind::References, mat,   tex),     "material -> texture");
    check(g.add_edge(EdgeKind::Influences, light, a),       "light -> mesh A");

    std::printf("fixture: %zu nodes, %zu edges\n", g.node_count(), g.edge_count());

    const auto r = validate_all(g, /*max_in_degree*/ 8);
    if (!r.has_value()) {
        static const char* names[] = {
            "TypedNodes", "TypedEdges", "EdgeEndpointsExist", "EdgeTypeCompat",
            "TransformAcyclic", "CameraExclusive", "MaterialReferenced",
            "UniqueIds", "ContainsAntireflexive", "BoundedDegree",
        };
        const auto idx = static_cast<std::size_t>(r.error());
        std::fprintf(stderr, "fixture: FAILED invariant %zu (%s)\n", idx, names[idx]);
        return 2;
    }
    std::printf("fixture: all 10 invariants pass\n");
    return 0;
}
```

- [ ] **Step 2: Create the app CMake**

Create `apps/aleph_graph_fixture/CMakeLists.txt`:

```cmake
add_executable(aleph_graph_fixture main.cpp)
target_link_libraries(aleph_graph_fixture PRIVATE
    aleph_types aleph_graph aleph_containers aleph_alloc
    aleph_flags_test)
```

(Uses `aleph_flags_test` since main.cpp uses `std::fprintf` / `std::printf` and needs the default toolchain mode — same convention as `aleph_rt` and `aleph_sw` in Phase 2.)

- [ ] **Step 3: Wire into apps**

Modify `apps/CMakeLists.txt`:

```cmake
add_subdirectory(aleph_rt)
add_subdirectory(aleph_graph_fixture)
if(ALEPH_HAVE_SDL2)
    add_subdirectory(aleph_sw)
endif()
```

- [ ] **Step 4: Build + smoke**

Run:

```bash
cmake --build build-release --target aleph_graph_fixture 2>&1 | tail -3
./build-release/apps/aleph_graph_fixture/aleph_graph_fixture
echo "exit code: $?"
```

Expected: build OK; stdout shows:

```
fixture: 8 nodes, 8 edges
fixture: all 10 invariants pass
exit code: 0
```

- [ ] **Step 5: Commit**

```bash
git add apps/aleph_graph_fixture/ apps/CMakeLists.txt
git commit -m "task 16: apps/aleph_graph_fixture — canonical 8-node scene + validate_all"
```

---

## Task 17: Final validation + tag `v0.3.0-graph`

**Files:** none modified. Validation + tagging only.

- [ ] **Step 1: Clean rebuild**

```bash
cd /home/lkz/aleph-cxx
rm -rf build-release
cmake --preset release 2>&1 | tail -5
cmake --build build-release 2>&1 | tail -5
```

Expected: build green, no errors, no new warnings beyond Phase 1/2 pre-existing ones.

- [ ] **Step 2: Run full ctest**

```bash
ctest --test-dir build-release --output-on-failure 2>&1 | tail -25
```

Expected: all isolation tests pass (now includes `iso_types`, `iso_graph`); `aleph_tests` runs all doctest cases including the new graph + invariant + tla_cxx_sync ones (4 + 7 + 9 + 6 + 6 + 6 + 6 + 6 = ~50 new); previous Phase 2 cases (135) still pass.

- [ ] **Step 3: Smoke test the fixture binary again on the fresh build**

```bash
./build-release/apps/aleph_graph_fixture/aleph_graph_fixture
echo "exit: $?"
```

Expected: `exit: 0` and the success messages.

- [ ] **Step 4: Run TLC if `tlc` is in PATH (optional gate)**

```bash
if command -v tlc &> /dev/null || [ -f "$TLA_TOOLS" ]; then
    TLA_TOOLS=${TLA_TOOLS:-/tmp/tla2tools.jar} ./formal/check.sh 2>&1 | tail -15
else
    echo "(tlc not installed — skipping TLA+ model check; tla_cxx_sync covers C-side drift)"
fi
```

Expected: either "No error has been found" (twice), or the skip message.

- [ ] **Step 5: Validate Sub-phase 4a success criteria from spec §4.6**

```bash
echo "=== Sub-phase 4a Success Criteria ==="
echo "Modules built:"; ls build-release/graph/src/aleph.types/libaleph_types.a build-release/graph/src/aleph.graph/libaleph_graph.a 2>&1
echo "OrderedMap bench (informal):"
echo "  (no bench harness change in 4a; check tests pass at 4a-bench in 4b)"
echo "tla_cxx_sync test:"
ctest --test-dir build-release -R aleph_tests --output-on-failure 2>&1 | grep -E "(tla_cxx_sync|pass|fail)" | tail -5
echo "iso_types + iso_graph:"
ctest --test-dir build-release -R "iso_(types|graph)" --output-on-failure 2>&1 | tail -5
```

Expected:
- `libaleph_types.a` and `libaleph_graph.a` present.
- `tla_cxx_sync` cases pass.
- Both isolation tests pass.

- [ ] **Step 6: Tag**

```bash
git tag -a v0.3.0-graph -m "Sub-phase 4a complete: aleph.types + aleph.graph + OrderedMap + tla_cxx_sync + fixture"
git tag -l v0.3.0-graph
git log v0.3.0-graph --oneline -1
```

Expected: tag created on the latest commit (Task 16's commit).

- [ ] **Step 7: Final report**

Write a brief status note (commit message body or PR text):

```
Sub-phase 4a Validation Report
==============================
Modules:         aleph.types (4 partitions), aleph.graph (2 partitions)
Foundation ext:  OrderedMap added to aleph.containers
TLA+ sync:       tla_cxx_sync passes (NodeKind + EdgeKind + EdgeTypeCompat + INVARIANT_NAMES)
Isolation:       iso_types + iso_graph pass
Fixture binary:  8 nodes, 8 edges, all 10 invariants pass
ctest:           N/N pass (count from step 2)
Tag:             v0.3.0-graph (local)

Next: Sub-phase 4b (aleph.dpo) — plan to be written.
```

- [ ] **Step 8: Decision point — merge to main + push, or stay on branch**

Per the spec §8.5 strategy, each sub-phase merges to main when its tag is cut. Two options at this step:

A. Merge + push now:
   ```bash
   git checkout main && git merge --ff-only phase-4-graph
   git push origin main && git push origin v0.3.0-graph
   # branch phase-4-graph stays alive for Sub-phase 4b
   git checkout phase-4-graph
   ```

B. Stay on branch (Sub-phase 4b continues on same branch, all 4 sub-phases batch-merge at the end).

The spec recommends A (independent merges per sub-phase, easier rollback). Confirm with the user before pushing.

---

## Self-Review

**1. Spec coverage** (spec §4):

- ✅ `aleph.types` with 4 partitions (id, attribute, node, edge) → Tasks 3-7, umbrella in Task 8
- ✅ `Node` as `std::variant` → Task 6
- ✅ `NodeId`/`EdgeId` strong typedefs → Task 3
- ✅ `IdAllocator` → Task 3
- ✅ `NodeKind::ALL` (renamed `all_node_kinds()` for enum constraints) + `as_tla()` → Task 5
- ✅ `EdgeKind::allows(NodeKind, NodeKind)` → Task 7
- ✅ `aleph.graph` with `:graph` + `:invariants` → Tasks 9-12, umbrella in Task 13
- ✅ Storage via `OrderedMap<NodeId, Node>` / `<EdgeId, Edge>` → Task 9 (uses `OrderedMap` from Task 2)
- ✅ 10 invariants in canonical order + `INVARIANT_NAMES` array → Tasks 11-12
- ✅ `validate_all(g, max_in_degree)` → Task 12
- ✅ `std::expected<T, E>` for errors → Tasks 9, 10, 11, 12
- ✅ `tla_cxx_sync` parsing `formal/scene_graph.tla` → Task 15
- ✅ Fixture validator binary → Task 16
- ✅ LOC budget ~2050 → covered (~250 OrderedMap + ~400 types + ~600 graph + ~200 tla_cxx_sync + ~600 tests)

**2. Placeholder scan**: No "TBD" / "TODO" / "fill in" / "similar to" markers. Each step has runnable code or commands.

**3. Type consistency**:

- `NodeId.value` (Task 3) used consistently through later tasks.
- `OrderedMap` API (`insert`, `get`, `get_mut`, `remove`, `contains`, `size`, iteration) defined in Task 2, used identically in Task 9-12.
- `aleph::types::id_of(Node)`, `kind_of(Node)` — defined in Task 6, used in Task 9-12.
- `aleph::types::allows(EdgeKind, NodeKind, NodeKind)` — Task 7, used in Task 10 (`add_edge`) and Task 11 (`check_edge_type_compat`).
- `InvariantError` enum values: defined in Task 11 (entries 0-4), extended in Task 12 (entries 5-9) — order matches `INVARIANT_NAMES`.

**4. One adjustment from initial draft**: original test used `NodeKind::ALL.size()` (Rust idiom); C++ scoped enums can't have inner members, so Task 5 uses `all_node_kinds()` free function instead. Test files corrected inline.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-27-aleph-cxx-graph-4a.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, two-stage review (spec compliance + code quality) between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints for review.

Which approach?

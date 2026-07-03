module;
#include <cstdint>
#include <functional>

export module aleph.types:id;

export namespace aleph::types {

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

class IdAllocator {
public:
    constexpr IdAllocator() noexcept = default;
    constexpr IdAllocator(const IdAllocator&) noexcept = default;
    constexpr IdAllocator& operator=(const IdAllocator&) noexcept = default;

    NodeId alloc_node() noexcept { return NodeId{node_next_++}; }
    EdgeId alloc_edge() noexcept { return EdgeId{edge_next_++}; }

    std::uint32_t node_watermark() const noexcept { return node_next_; }
    std::uint32_t edge_watermark() const noexcept { return edge_next_; }

    void sync_node_to_at_least(std::uint32_t next) noexcept {
        if (node_next_ < next) node_next_ = next;
    }

    void sync_edge_to_at_least(std::uint32_t next) noexcept {
        if (edge_next_ < next) edge_next_ = next;
    }

private:
    std::uint32_t node_next_{0};
    std::uint32_t edge_next_{0};
};

}  // namespace aleph::types

template <> struct std::hash<aleph::types::NodeId> {
    std::size_t operator()(aleph::types::NodeId k) const noexcept {
        return std::hash<std::uint32_t>{}(k.value);
    }
};

template <> struct std::hash<aleph::types::EdgeId> {
    std::size_t operator()(aleph::types::EdgeId k) const noexcept {
        return std::hash<std::uint32_t>{}(k.value);
    }
};

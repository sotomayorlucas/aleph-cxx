module;
#include <cstddef>
#include <cstdint>

export module aleph.alloc:slab;

export namespace aleph::alloc {

// Fixed-size slab. Each block is `BlockSize` bytes, aligned to `BlockSize`
// (rounded up to power of two). Released blocks are linked via an
// intrusive single-linked free list embedded in the block.
template<std::size_t BlockSize>
class Slab {
    static_assert(BlockSize >= sizeof(void*),
                  "BlockSize must hold at least a pointer");
public:
    Slab(void* buffer, std::size_t total_bytes) noexcept
        : base_{static_cast<unsigned char*>(buffer)},
          cap_{(total_bytes / BlockSize) * BlockSize} {}

    [[nodiscard]] void* allocate() noexcept {
        if (free_head_) {
            void* p = free_head_;
            free_head_ = *static_cast<void**>(p);
            return p;
        }
        if (off_ + BlockSize > cap_) return nullptr;
        void* p = base_ + off_;
        off_ += BlockSize;
        return p;
    }

    void deallocate(void* p) noexcept {
        *static_cast<void**>(p) = free_head_;
        free_head_ = p;
    }

    std::size_t capacity_blocks() const noexcept { return cap_ / BlockSize; }

private:
    unsigned char* base_{nullptr};
    std::size_t    cap_{0};
    std::size_t    off_{0};
    void*          free_head_{nullptr};
};

}  // namespace aleph::alloc

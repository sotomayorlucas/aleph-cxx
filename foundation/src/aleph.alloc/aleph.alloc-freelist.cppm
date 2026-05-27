module;
#include <cstddef>
#include <cstdint>

export module aleph.alloc:freelist;

export namespace aleph::alloc {

// Mixed-size with size-segregated free lists for the buckets
// {8, 16, 32, 64, 128, 256}. Requests above 256 fall through to bump.
class FreeList {
public:
    static constexpr int N_BUCKETS = 6;
    static constexpr std::size_t bucket_size(int i) noexcept {
        return 1u << (i + 3);   // 8, 16, 32, 64, 128, 256
    }

    FreeList(void* buffer, std::size_t total_bytes) noexcept
        : base_{static_cast<unsigned char*>(buffer)}, cap_{total_bytes} {}

    [[nodiscard]] void* allocate(std::size_t bytes) noexcept {
        const int b = bucket_for(bytes);
        if (b >= 0) {
            if (heads_[b]) {
                void* p = heads_[b];
                heads_[b] = *static_cast<void**>(p);
                return p;
            }
            return bump(bucket_size(b), bucket_align(b));
        }
        return bump(bytes, 64);   // oversized -> 64-byte aligned bump
    }

    void deallocate(void* p, std::size_t bytes) noexcept {
        const int b = bucket_for(bytes);
        if (b >= 0) {
            *static_cast<void**>(p) = heads_[b];
            heads_[b] = p;
        }
        // oversized: leak in bump region; reclaimed by reset() (not exposed here)
    }

private:
    static constexpr int bucket_for(std::size_t bytes) noexcept {
        for (int i = 0; i < N_BUCKETS; ++i)
            if (bytes <= bucket_size(i)) return i;
        return -1;
    }
    static constexpr std::size_t bucket_align(int i) noexcept {
        const std::size_t s = bucket_size(i);
        return s < 64 ? s : 64;
    }

    void* bump(std::size_t bytes, std::size_t align) noexcept {
        const std::uintptr_t cur = reinterpret_cast<std::uintptr_t>(base_) + off_;
        const std::uintptr_t aligned = (cur + (align - 1)) & ~(align - 1);
        const std::size_t pad = aligned - cur;
        if (off_ + pad + bytes > cap_) return nullptr;
        off_ += pad + bytes;
        return reinterpret_cast<void*>(aligned);
    }

    unsigned char* base_{nullptr};
    std::size_t    cap_{0};
    std::size_t    off_{0};
    void*          heads_[N_BUCKETS]{};
};

}  // namespace aleph::alloc

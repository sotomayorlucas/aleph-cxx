module;
#include <cstddef>
#include <cstdint>

export module aleph.alloc:frame;

export namespace aleph::alloc {

// Double-buffered bump allocator. release_frame() swaps the active buffer
// and resets it — the just-released buffer remains valid until the NEXT
// release (giving render code a frame of grace to read its results).
class Frame {
public:
    Frame(void* buf_a, void* buf_b, std::size_t size_each) noexcept
        : bufs_{static_cast<unsigned char*>(buf_a),
                static_cast<unsigned char*>(buf_b)},
          cap_{size_each} {}

    [[nodiscard]] void* allocate(std::size_t bytes, std::size_t align) noexcept {
        unsigned char* base = bufs_[active_];
        const std::uintptr_t cur = reinterpret_cast<std::uintptr_t>(base) + off_;
        const std::uintptr_t aligned = (cur + (align - 1)) & ~(align - 1);
        const std::size_t pad = aligned - cur;
        if (off_ + pad + bytes > cap_) return nullptr;
        off_ += pad + bytes;
        return reinterpret_cast<void*>(aligned);
    }

    void release_frame() noexcept {
        active_ ^= 1;
        off_ = 0;
    }

    std::size_t bytes_in_use_current() const noexcept { return off_; }
    std::size_t capacity()             const noexcept { return cap_; }

private:
    unsigned char* bufs_[2];
    std::size_t    cap_{0};
    std::size_t    off_{0};
    int            active_{0};
};

}  // namespace aleph::alloc

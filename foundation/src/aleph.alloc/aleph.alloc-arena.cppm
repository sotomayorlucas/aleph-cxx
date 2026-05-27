module;
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory_resource>

export module aleph.alloc:arena;

export namespace aleph::alloc {

// Bump-pointer arena over caller-provided storage. Single-thread.
// All ops noexcept. Allocation failure returns nullptr.
class Arena final : public std::pmr::memory_resource {
public:
    Arena(void* buffer, std::size_t size) noexcept
        : base_{static_cast<unsigned char*>(buffer)}, cap_{size} {}

    [[nodiscard]] void* allocate(std::size_t bytes, std::size_t align) noexcept {
        const std::uintptr_t cur = reinterpret_cast<std::uintptr_t>(base_) + off_;
        const std::uintptr_t aligned = (cur + (align - 1)) & ~(align - 1);
        const std::size_t pad = aligned - cur;
        if (off_ + pad + bytes > cap_) return nullptr;
        off_ += pad + bytes;
        peak_ = off_ > peak_ ? off_ : peak_;
        return reinterpret_cast<void*>(aligned);
    }

    void reset() noexcept { off_ = 0; }

    std::size_t bytes_in_use() const noexcept { return off_; }
    std::size_t peak_in_use()  const noexcept { return peak_; }
    std::size_t capacity()     const noexcept { return cap_; }

protected:
    void* do_allocate(std::size_t bytes, std::size_t align) override {
        void* p = allocate(bytes, align);
        if (!p) std::abort();   // pmr contract: never return null
        return p;
    }
    void do_deallocate(void*, std::size_t, std::size_t) noexcept override { /* no-op */ }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

private:
    unsigned char* base_{nullptr};
    std::size_t    cap_{0};
    std::size_t    off_{0};
    std::size_t    peak_{0};
};

}  // namespace aleph::alloc

module;
#include <cstdint>
#include <utility>
#include <new>
#include <vector>

export module aleph.containers:dense_index;

export namespace aleph::containers {

// Strongly-typed integer handle.
// GCC 16 workaround: manual operator== instead of defaulted.
template<typename Tag>
struct Handle {
    std::uint32_t value{};
    constexpr bool operator==(const Handle& o) const noexcept { return value == o.value; }
    constexpr bool operator!=(const Handle& o) const noexcept { return value != o.value; }
};

// Vector with typed Handle<Tag> indexing. Replaces hash map for cases
// where stable insertion-order iteration is needed (= sustituye IndexMap).
//
// GCC 16 workaround: std::vector placement-new is broken in modules.
// Use a manual reserve/construct strategy instead.
template<typename Tag, typename T>
class DenseIndex {
public:
    DenseIndex() noexcept = default;

    ~DenseIndex() {
        for (std::uint32_t i = 0; i < size_; ++i) {
            data_[i].~T();
        }
        ::operator delete(data_);
    }

    // Deleted copy — shallow copy + double-free on second destructor.
    DenseIndex(const DenseIndex&)            = delete;
    DenseIndex& operator=(const DenseIndex&) = delete;

    DenseIndex(DenseIndex&& o) noexcept { steal_from(o); }
    DenseIndex& operator=(DenseIndex&& o) noexcept {
        if (this != &o) { release(); steal_from(o); }
        return *this;
    }

    Handle<Tag> push(const T& v) {
        const std::uint32_t id = static_cast<std::uint32_t>(size_);
        if (id >= capacity_) grow();
        new (data_ + id) T(v);
        ++size_;
        return Handle<Tag>{id};
    }

    Handle<Tag> push(T&& v) {
        const std::uint32_t id = static_cast<std::uint32_t>(size_);
        if (id >= capacity_) grow();
        new (data_ + id) T(std::move(v));
        ++size_;
        return Handle<Tag>{id};
    }

    T&       operator[](Handle<Tag> h)       noexcept { return data_[h.value]; }
    const T& operator[](Handle<Tag> h) const noexcept { return data_[h.value]; }

    std::size_t size()  const noexcept { return size_; }
    bool        empty() const noexcept { return size_ == 0; }

    auto begin()       noexcept { return data_; }
    auto end()         noexcept { return data_ + size_; }
    auto begin() const noexcept { return data_; }
    auto end()   const noexcept { return data_ + size_; }

private:
    T* data_ = nullptr;
    std::uint32_t size_ = 0;
    std::uint32_t capacity_ = 0;

    void release() noexcept {
        if (data_) {
            for (std::uint32_t i = 0; i < size_; ++i) data_[i].~T();
            ::operator delete(data_);
            data_ = nullptr; size_ = 0; capacity_ = 0;
        }
    }

    void steal_from(DenseIndex& o) noexcept {
        data_     = o.data_;     o.data_     = nullptr;
        size_     = o.size_;     o.size_     = 0;
        capacity_ = o.capacity_; o.capacity_ = 0;
    }

    void grow() {
        const std::uint32_t new_cap = (capacity_ == 0) ? 16 : capacity_ * 2;
        T* new_data = static_cast<T*>(::operator new(new_cap * sizeof(T)));
        for (std::uint32_t i = 0; i < size_; ++i) {
            new (new_data + i) T(std::move(data_[i]));
            data_[i].~T();
        }
        ::operator delete(data_);
        data_ = new_data;
        capacity_ = new_cap;
    }
};

}  // namespace aleph::containers

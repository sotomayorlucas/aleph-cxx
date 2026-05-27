module;
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>
#include <new>

export module aleph.containers:small_vector;

export namespace aleph::containers {

// Vector with N elements of inline storage; spills to heap (malloc/free)
// when capacity exceeds N. Trivially-copyable T only (no destructor calls
// in destroy path — keeps move/copy bookkeeping simple).
template<typename T, std::size_t N>
class SmallVector {
    static_assert(std::is_trivially_copyable_v<T>,
                  "SmallVector requires trivially-copyable T");
    static_assert(N > 0, "SmallVector requires N > 0");
public:
    SmallVector() noexcept = default;

    SmallVector(SmallVector&& o) noexcept { steal_from(o); }

    SmallVector& operator=(SmallVector&& o) noexcept {
        if (this != &o) { release(); steal_from(o); }
        return *this;
    }

    ~SmallVector() { release(); }

    void push_back(const T& v) noexcept {
        if (sz_ == cap_) grow();
        data_[sz_++] = v;
    }

    T&       operator[](std::size_t i)       noexcept { return data_[i]; }
    const T& operator[](std::size_t i) const noexcept { return data_[i]; }

    std::size_t size()     const noexcept { return sz_; }
    std::size_t capacity() const noexcept { return cap_; }
    bool        is_inline() const noexcept { return data_ == inline_storage(); }

    T*       begin()       noexcept { return data_; }
    T*       end()         noexcept { return data_ + sz_; }
    const T* begin() const noexcept { return data_; }
    const T* end()   const noexcept { return data_ + sz_; }

private:
    alignas(T) unsigned char inline_buf_[sizeof(T) * N]{};
    T*          data_{reinterpret_cast<T*>(inline_buf_)};
    std::size_t sz_{0};
    std::size_t cap_{N};

    T* inline_storage() noexcept { return reinterpret_cast<T*>(inline_buf_); }
    const T* inline_storage() const noexcept { return reinterpret_cast<const T*>(inline_buf_); }

    void grow() noexcept {
        const std::size_t new_cap = cap_ * 2;
        T* new_data = static_cast<T*>(std::malloc(sizeof(T) * new_cap));
        if (!new_data) std::abort();                   // OOM is fatal; no exceptions in foundation
        std::memcpy(new_data, data_, sizeof(T) * sz_);
        if (!is_inline()) std::free(data_);
        data_ = new_data;
        cap_  = new_cap;
    }

    void release() noexcept {
        if (!is_inline()) { std::free(data_); data_ = inline_storage(); }
        sz_ = 0;
        cap_ = N;
    }

    void steal_from(SmallVector& o) noexcept {
        if (o.is_inline()) {
            std::memcpy(inline_storage(), o.inline_storage(), sizeof(T) * o.sz_);
            data_ = inline_storage();
        } else {
            data_ = o.data_;
            o.data_ = o.inline_storage();
        }
        sz_  = o.sz_;
        cap_ = o.cap_;
        o.sz_  = 0;
        o.cap_ = N;
    }
};

}  // namespace aleph::containers

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
template<typename Tag, typename T>
class DenseIndex {
public:
    // GCC 16 placement-new SFINAE bug workaround: avoid vector's push_back
    // which triggers problematic noexcept checks. Instead, resize and copy.
    Handle<Tag> push(const T& v) {
        const std::uint32_t id = static_cast<std::uint32_t>(data_.size());
        data_.resize(data_.size() + 1, v);
        return Handle<Tag>{id};
    }

    T&       operator[](Handle<Tag> h)       noexcept { return data_[h.value]; }
    const T& operator[](Handle<Tag> h) const noexcept { return data_[h.value]; }

    std::size_t size()  const noexcept { return data_.size(); }
    bool        empty() const noexcept { return data_.empty(); }

    auto begin()       noexcept { return data_.begin(); }
    auto end()         noexcept { return data_.end(); }
    auto begin() const noexcept { return data_.begin(); }
    auto end()   const noexcept { return data_.end(); }

private:
    std::vector<T> data_;
};

}  // namespace aleph::containers

module;
#include <algorithm>
#include <cstddef>
#include <vector>
#include <functional>

export module aleph.containers:flat_set;

export namespace aleph::containers {

template<typename K, typename Compare = std::less<K>>
class FlatSet {
public:
    void insert(const K& k) {
        auto it = std::lower_bound(data_.begin(), data_.end(), k, cmp_);
        if (it == data_.end() || cmp_(k, *it)) data_.insert(it, k);
    }

    [[nodiscard]] bool contains(const K& k) const noexcept {
        auto it = std::lower_bound(data_.begin(), data_.end(), k, cmp_);
        return it != data_.end() && !cmp_(k, *it);
    }

    [[nodiscard]] std::size_t size()  const noexcept { return data_.size(); }
    [[nodiscard]] bool        empty() const noexcept { return data_.empty(); }

    const K& operator[](std::size_t i) const noexcept { return data_[i]; }

    auto begin() const noexcept { return data_.begin(); }
    auto end()   const noexcept { return data_.end();   }

private:
    std::vector<K> data_;
    Compare        cmp_{};
};

}  // namespace aleph::containers

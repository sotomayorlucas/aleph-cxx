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

template <typename K, typename V>
class OrderedMap {
    struct Slot {
        K           key;
        V           value;
        std::size_t prev;
        std::size_t next;
    };

    // GCC 16 workaround: std::vector<Slot> push_back is broken in modules
    // when Slot has non-trivial members (K, V may be std::string etc.).
    // Use manual operator new + placement-new, mirroring DenseIndex pattern.
    Slot*               slots_    = nullptr;
    std::size_t         slots_sz_ = 0;
    std::size_t         slots_cap_= 0;

    std::vector<std::size_t> buckets_;
    std::size_t              head_        {SIZE_MAX};
    std::size_t              tail_        {SIZE_MAX};
    std::size_t              free_        {SIZE_MAX};
    std::size_t              count_       {0};
    std::size_t              tombstones_  {0};

    static constexpr std::size_t TOMBSTONE       = SIZE_MAX - 1;
    static constexpr std::size_t INITIAL_BUCKETS = 16;

    // ---- slots_ raw storage helpers ----------------------------------------
    void slots_release() noexcept {
        if (slots_) {
            for (std::size_t i = 0; i < slots_sz_; ++i) slots_[i].~Slot();
            ::operator delete(slots_);
            slots_ = nullptr; slots_sz_ = 0; slots_cap_ = 0;
        }
    }

    void slots_grow() {
        const std::size_t new_cap = (slots_cap_ == 0) ? 16 : slots_cap_ * 2;
        Slot* nd = static_cast<Slot*>(::operator new(new_cap * sizeof(Slot)));
        for (std::size_t i = 0; i < slots_sz_; ++i) {
            new (nd + i) Slot(std::move(slots_[i]));
            slots_[i].~Slot();
        }
        ::operator delete(slots_);
        slots_     = nd;
        slots_cap_ = new_cap;
    }

    std::size_t slots_emplace(K k, V v) {
        if (slots_sz_ >= slots_cap_) slots_grow();
        const std::size_t idx = slots_sz_++;
        new (slots_ + idx) Slot{std::move(k), std::move(v), SIZE_MAX, SIZE_MAX};
        return idx;
    }
    // ------------------------------------------------------------------------

    void ensure_buckets() {
        if (buckets_.empty()) buckets_.assign(INITIAL_BUCKETS, SIZE_MAX);
    }

    std::size_t hash_to_bucket(const K& key) const noexcept {
        return std::hash<K>{}(key) & (buckets_.size() - 1);
    }

    // Returns slot index (SIZE_MAX = not found). out_idx = bucket position used.
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
        // Include tombstones in the load check: tombstones occupy buckets and
        // can exhaust the table even when live count_ is low.  Rehash when
        // (live + tombstone) slots cross 70% of bucket capacity.
        if ((count_ + tombstones_) * 10 >= buckets_.size() * 7) {
            const std::size_t new_n = buckets_.size() * 2;
            buckets_.assign(new_n, SIZE_MAX);
            const std::size_t mask = new_n - 1;
            for (std::size_t s = head_; s != SIZE_MAX; s = slots_[s].next) {
                std::size_t b = std::hash<K>{}(slots_[s].key) & mask;
                while (buckets_[b] != SIZE_MAX) b = (b + 1) & mask;
                buckets_[b] = s;
            }
            tombstones_ = 0;  // rehash wipes all tombstones
        }
    }

    std::size_t take_slot(K k, V v) {
        std::size_t s;
        if (free_ != SIZE_MAX) {
            s     = free_;
            free_ = slots_[s].next;
            slots_[s].key   = std::move(k);
            slots_[s].value = std::move(v);
        } else {
            s = slots_emplace(std::move(k), std::move(v));
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

    ~OrderedMap() { slots_release(); }

    OrderedMap(const OrderedMap&)            = delete;
    OrderedMap& operator=(const OrderedMap&) = delete;

    OrderedMap(OrderedMap&& o) noexcept
        : slots_(o.slots_), slots_sz_(o.slots_sz_), slots_cap_(o.slots_cap_),
          buckets_(std::move(o.buckets_)),
          head_(o.head_), tail_(o.tail_), free_(o.free_),
          count_(o.count_), tombstones_(o.tombstones_)
    {
        o.slots_ = nullptr; o.slots_sz_ = 0; o.slots_cap_ = 0;
        o.head_ = o.tail_ = o.free_ = SIZE_MAX;
        o.count_ = 0; o.tombstones_ = 0;
    }

    OrderedMap& operator=(OrderedMap&& o) noexcept {
        if (this != &o) {
            slots_release();
            slots_       = o.slots_;       o.slots_       = nullptr;
            slots_sz_    = o.slots_sz_;    o.slots_sz_    = 0;
            slots_cap_   = o.slots_cap_;   o.slots_cap_   = 0;
            buckets_     = std::move(o.buckets_);
            head_        = o.head_;        o.head_        = SIZE_MAX;
            tail_        = o.tail_;        o.tail_        = SIZE_MAX;
            free_        = o.free_;        o.free_        = SIZE_MAX;
            count_       = o.count_;       o.count_       = 0;
            tombstones_  = o.tombstones_;  o.tombstones_  = 0;
        }
        return *this;
    }

    std::size_t size()  const noexcept { return count_; }
    bool        empty() const noexcept { return count_ == 0; }

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

    std::optional<V> remove(const K& key) {
        if (buckets_.empty()) return std::nullopt;
        std::size_t b;
        const std::size_t s = probe(key, b);
        if (s == SIZE_MAX) return std::nullopt;

        V out = std::move(slots_[s].value);
        const std::size_t p = slots_[s].prev;
        const std::size_t n = slots_[s].next;
        if (p != SIZE_MAX) slots_[p].next = n; else head_ = n;
        if (n != SIZE_MAX) slots_[n].prev = p; else tail_ = p;
        buckets_[b] = TOMBSTONE;
        ++tombstones_;
        slots_[s].next = free_;
        free_ = s;
        --count_;
        return out;
    }

    void clear() noexcept {
        for (auto& bkt : buckets_) bkt = SIZE_MAX;
        // Destroy all live slot K/V, then placement-new empty Slots so the
        // raw storage is ready for reuse.
        for (std::size_t s = head_; s != SIZE_MAX; ) {
            const std::size_t nxt = slots_[s].next;
            slots_[s].~Slot();
            new (slots_ + s) Slot{};     // re-initialise indices to SIZE_MAX / 0
            s = nxt;
        }
        // Free-list slots: value was moved-out by remove(), but key was NOT —
        // destroy them explicitly to avoid leaking heap-owning K types.
        for (std::size_t s = free_; s != SIZE_MAX; ) {
            const std::size_t nxt = slots_[s].next;
            slots_[s].~Slot();
            new (slots_ + s) Slot{};
            s = nxt;
        }
        head_ = tail_ = free_ = SIZE_MAX;
        slots_sz_ = 0;
        count_ = 0;
        tombstones_ = 0;
    }

    // ---- iterators ----------------------------------------------------------
    class const_iterator {
        const OrderedMap* m_;
        std::size_t       s_;
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

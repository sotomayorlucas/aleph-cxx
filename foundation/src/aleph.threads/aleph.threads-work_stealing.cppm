module;
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <new>

export module aleph.threads:work_stealing;

export namespace aleph::threads {

// Chase-Lev work-stealing deque. Owner thread pushes/pops at the
// "bottom"; thieves steal from the "top". Lock-free.
//
// Storage grows by doubling. T must be trivially-copyable (sufficient
// for handle/index payloads used in BVH build).
//
// GCC 16 note: std::vector<T> hits the placement-new module bug for
// some types. Use ::operator new/delete with raw copies instead —
// safe because T is constrained to trivially-copyable.
template<typename T>
    requires __is_trivially_copyable(T)
class WorkStealingDeque {
public:
    WorkStealingDeque() {
        cap_ = initial_cap;
        buf_ = static_cast<T*>(::operator new(cap_ * sizeof(T)));
    }

    ~WorkStealingDeque() {
        ::operator delete(buf_);
    }

    // Non-copyable, non-movable (atomics + raw pointer ownership).
    WorkStealingDeque(const WorkStealingDeque&)            = delete;
    WorkStealingDeque& operator=(const WorkStealingDeque&) = delete;

    void push(const T& v) {
        const std::int64_t b = bottom_.load(std::memory_order_relaxed);
        const std::int64_t t = top_.load(std::memory_order_acquire);
        if (b - t >= static_cast<std::int64_t>(cap_)) {
            // Grow: double capacity, copy live entries.
            const std::size_t new_cap = cap_ * 2;
            T* grown = static_cast<T*>(::operator new(new_cap * sizeof(T)));
            for (std::int64_t i = t; i < b; ++i) {
                const auto ui = static_cast<std::size_t>(i);
                grown[ui & (new_cap - 1)] = buf_[ui & (cap_ - 1)];
            }
            ::operator delete(buf_);
            buf_ = grown;
            cap_ = new_cap;
        }
        buf_[static_cast<std::size_t>(b) & (cap_ - 1)] = v;
        std::atomic_thread_fence(std::memory_order_release);
        bottom_.store(b + 1, std::memory_order_relaxed);
    }

    // Owner pop — LIFO from the bottom end.
    bool pop(T& out) {
        const std::int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
        bottom_.store(b, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        std::int64_t t = top_.load(std::memory_order_relaxed);
        if (t <= b) {
            out = buf_[static_cast<std::size_t>(b) & (cap_ - 1)];
            if (t == b) {
                // Last element — race with thieves.
                if (!top_.compare_exchange_strong(t, t + 1,
                        std::memory_order_seq_cst, std::memory_order_relaxed)) {
                    bottom_.store(b + 1, std::memory_order_relaxed);
                    return false;
                }
                bottom_.store(b + 1, std::memory_order_relaxed);
            }
            return true;
        }
        bottom_.store(b + 1, std::memory_order_relaxed);
        return false;
    }

    // Thief steal — FIFO from the top end.
    bool steal(T& out) {
        std::int64_t t = top_.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        const std::int64_t b = bottom_.load(std::memory_order_acquire);
        if (t < b) {
            out = buf_[static_cast<std::size_t>(t) & (cap_ - 1)];
            if (!top_.compare_exchange_strong(t, t + 1,
                    std::memory_order_seq_cst, std::memory_order_relaxed)) {
                return false;
            }
            return true;
        }
        return false;
    }

    bool empty() const noexcept {
        return bottom_.load(std::memory_order_acquire) <=
               top_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t initial_cap = 64;

    T*          buf_;
    std::size_t cap_;

    // Separate cache lines: top_ is written by thieves, bottom_ by the owner.
    // False-sharing between them would degrade performance significantly.
    alignas(64) std::atomic<std::int64_t> top_{0};
    alignas(64) std::atomic<std::int64_t> bottom_{0};
};

}  // namespace aleph::threads

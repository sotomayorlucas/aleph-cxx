module;
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <type_traits>

export module aleph.threads:work_stealing;

export namespace aleph::threads {

// Chase-Lev work-stealing deque — bounded variant.
//
// The original grow path had a use-after-free: old buffers were freed while
// thieves might still be reading them (Chase-Lev correctness requires
// immortal-epoch retirement, not immediate delete). Rather than implement
// epoch reclamation in foundation, the deque is bounded to capacity_v entries.
// 4096 is large enough for BVH build subtree tasks.  push() aborts (not
// returns an error) on overflow — foundation policy: no exceptions, OOM/overflow
// is fatal.
//
// T must be trivially-copyable (sufficient for handle/index payloads).
template<typename T>
class WorkStealingDeque {
    static_assert(std::is_trivially_copyable_v<T>,
                  "WorkStealingDeque requires trivially-copyable T");
public:
    static constexpr std::size_t capacity_v = 4096;   // bounded; aborts on overflow

    WorkStealingDeque() {
        buf_ = static_cast<T*>(::operator new(sizeof(T) * capacity_v));
    }
    ~WorkStealingDeque() { ::operator delete(buf_); }

    // Non-copyable, non-movable (atomics + raw pointer ownership).
    WorkStealingDeque(const WorkStealingDeque&)            = delete;
    WorkStealingDeque& operator=(const WorkStealingDeque&) = delete;
    WorkStealingDeque(WorkStealingDeque&&)                 = delete;
    WorkStealingDeque& operator=(WorkStealingDeque&&)      = delete;

    void push(const T& v) {
        const std::int64_t b = bottom_.load(std::memory_order_relaxed);
        const std::int64_t t = top_.load(std::memory_order_acquire);
        if (b - t >= static_cast<std::int64_t>(capacity_v)) std::abort();
        buf_[static_cast<std::size_t>(b) & (capacity_v - 1)] = v;
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
            out = buf_[static_cast<std::size_t>(b) & (capacity_v - 1)];
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
            out = buf_[static_cast<std::size_t>(t) & (capacity_v - 1)];
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
    T*                                    buf_{nullptr};

    // Separate cache lines: top_ is written by thieves, bottom_ by the owner.
    // False-sharing between them would degrade performance significantly.
    alignas(64) std::atomic<std::int64_t> top_{0};
    alignas(64) std::atomic<std::int64_t> bottom_{0};
};

}  // namespace aleph::threads

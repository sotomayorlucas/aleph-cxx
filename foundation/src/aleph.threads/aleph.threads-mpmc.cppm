module;
#include <atomic>
#include <cstddef>
#include <cstdint>

export module aleph.threads:mpmc;

export namespace aleph::threads {

// Vyukov bounded MPMC ring. Capacity must be a power of two >= 2.
// Wait-free for uncontended pushes/pops; spin-loop on contention.
template<typename T, std::size_t Capacity>
class MpmcRing {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
public:
    MpmcRing() noexcept {
        for (std::size_t i = 0; i < Capacity; ++i)
            cells_[i].seq.store(i, std::memory_order_relaxed);
    }

    [[nodiscard]] bool try_push(const T& v) noexcept {
        Cell* cell;
        std::size_t pos = enq_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &cells_[pos & MASK];
            std::size_t seq = cell->seq.load(std::memory_order_acquire);
            std::intptr_t diff = (std::intptr_t)seq - (std::intptr_t)pos;
            if (diff == 0) {
                if (enq_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    break;
            } else if (diff < 0) {
                return false;   // full
            } else {
                pos = enq_.load(std::memory_order_relaxed);
            }
        }
        cell->data = v;
        cell->seq.store(pos + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_pop(T& out) noexcept {
        Cell* cell;
        std::size_t pos = deq_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &cells_[pos & MASK];
            std::size_t seq = cell->seq.load(std::memory_order_acquire);
            std::intptr_t diff = (std::intptr_t)seq - (std::intptr_t)(pos + 1);
            if (diff == 0) {
                if (deq_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    break;
            } else if (diff < 0) {
                return false;   // empty
            } else {
                pos = deq_.load(std::memory_order_relaxed);
            }
        }
        out = cell->data;
        cell->seq.store(pos + MASK + 1, std::memory_order_release);
        return true;
    }

private:
    static constexpr std::size_t MASK = Capacity - 1;
    struct alignas(64) Cell {
        std::atomic<std::size_t> seq;
        T                        data;
    };
    alignas(64) Cell cells_[Capacity];
    alignas(64) std::atomic<std::size_t> enq_{0};
    alignas(64) std::atomic<std::size_t> deq_{0};
};

}  // namespace aleph::threads

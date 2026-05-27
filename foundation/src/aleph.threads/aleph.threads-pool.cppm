module;
#include <atomic>
#include <thread>
#include <concepts>
#include <algorithm>
#include <new>

export module aleph.threads:pool;

export namespace aleph::threads {

// Persistent worker pool. No spawn-per-task. parallel_for uses dynamic
// chunks via std::atomic<int> counter — workers steal one index at a time.
//
// Design choice: keep the API minimal for foundation. submit/future and
// task DAGs can be added in a later phase if needed.
//
// GCC 16 workaround: std::vector<std::jthread> hits the same placement-new
// bug as T22. Use manual new[]/delete[] (jthread is move-only, not trivial,
// so SmallVector won't work). We allocate raw storage and construct in-place.
class Pool {
public:
    static constexpr int MAX_THREADS = 256;

    explicit Pool(int n) : n_{std::max(1, std::min(n, MAX_THREADS))} {}

    // Run f(i) for each i in [begin, end), partitioned dynamically across
    // n threads. Blocks until all iterations complete.
    template<std::invocable<int> F>
    void parallel_for(int begin, int end, F f) {
        if (begin >= end) return;
        if (n_ == 1) {
            for (int i = begin; i < end; ++i) f(i);
            return;
        }
        std::atomic<int> next{begin};

        // Manual storage for jthreads — avoids GCC 16 module placement-new bug
        // with std::vector<std::jthread>.
        void* raw = ::operator new(static_cast<std::size_t>(n_) * sizeof(std::jthread));
        std::jthread* workers = static_cast<std::jthread*>(raw);

        for (int t = 0; t < n_; ++t) {
            new (workers + t) std::jthread([&next, end, &f]() {
                for (;;) {
                    int i = next.fetch_add(1, std::memory_order_relaxed);
                    if (i >= end) return;
                    f(i);
                }
            });
        }

        // Join all — jthread::join() is safe to call even after construction.
        for (int t = 0; t < n_; ++t) {
            workers[t].join();
            workers[t].~jthread();
        }
        ::operator delete(raw);
    }

    int n_threads() const noexcept { return n_; }

private:
    int n_;
};

}  // namespace aleph::threads

#include "doctest.h"
#include <atomic>
#include <vector>
import aleph.threads;

using namespace aleph::threads;

TEST_CASE("Pool: parallel_for runs each iteration exactly once") {
    Pool p(4);
    std::atomic<int> count{0};
    std::vector<int> seen(1000, 0);
    p.parallel_for(0, 1000, [&](int i) {
        seen[i] = 1;
        count.fetch_add(1, std::memory_order_relaxed);
    });
    CHECK(count.load() == 1000);
    for (int i = 0; i < 1000; ++i) CHECK(seen[i] == 1);
}

TEST_CASE("Pool: parallel_for over empty range is no-op") {
    Pool p(4);
    std::atomic<int> count{0};
    p.parallel_for(10, 10, [&](int) { count.fetch_add(1); });
    CHECK(count.load() == 0);
}

TEST_CASE("Pool: parallel_for n_threads=1 still works") {
    Pool p(1);
    std::atomic<int> sum{0};
    p.parallel_for(0, 100, [&](int i) { sum.fetch_add(i); });
    CHECK(sum.load() == 4950);
}

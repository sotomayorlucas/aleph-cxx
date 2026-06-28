#include "doctest.h"

#include <atomic>
#include <cstddef>
#include <vector>

import aleph.threads;

using aleph::threads::Pool;

TEST_CASE("thread pool stress: 10000 parallel_for iterations") {
    Pool pool(4);
    std::atomic<std::uint64_t> counter{0};
    constexpr int kTasks = 10000;

    pool.parallel_for(0, kTasks, [&](int) {
        counter.fetch_add(1, std::memory_order_relaxed);
    });

    CHECK(counter.load() == static_cast<std::uint64_t>(kTasks));
}

TEST_CASE("thread pool stress: parallel vector fill") {
    Pool pool(4);
    std::vector<std::uint32_t> data(4096, 0);

    pool.parallel_for(0, static_cast<int>(data.size()), [&](int i) {
        data[static_cast<std::size_t>(i)] =
            static_cast<std::uint32_t>(static_cast<std::size_t>(i) * 3 + 7);
    });

    for (std::size_t i = 0; i < data.size(); ++i) {
        CHECK(data[i] == static_cast<std::uint32_t>(i * 3 + 7));
    }
}

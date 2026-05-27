#include "doctest.h"
#include <atomic>
#include <thread>
#include <vector>
import aleph.threads;

using namespace aleph::threads;

TEST_CASE("MpmcRing: single-thread push/pop preserves FIFO") {
    MpmcRing<int, 16> q;
    CHECK(q.try_push(1));
    CHECK(q.try_push(2));
    CHECK(q.try_push(3));
    int v;
    CHECK(q.try_pop(v)); CHECK(v == 1);
    CHECK(q.try_pop(v)); CHECK(v == 2);
    CHECK(q.try_pop(v)); CHECK(v == 3);
    CHECK_FALSE(q.try_pop(v));
}

TEST_CASE("MpmcRing: full ring rejects push") {
    MpmcRing<int, 4> q;
    CHECK(q.try_push(1));
    CHECK(q.try_push(2));
    CHECK(q.try_push(3));
    CHECK(q.try_push(4));
    CHECK_FALSE(q.try_push(5));
}

TEST_CASE("MpmcRing: 2 producers + 2 consumers, all items received once") {
    MpmcRing<int, 1024> q;
    constexpr int per = 5000;
    std::atomic<int> received_sum{0};
    std::atomic<int> received_n{0};

    auto producer = [&](int base) {
        for (int i = 0; i < per; ++i) {
            while (!q.try_push(base + i)) std::this_thread::yield();
        }
    };
    auto consumer = [&]() {
        int v;
        while (received_n.load() < 2 * per) {
            if (q.try_pop(v)) {
                received_sum.fetch_add(v);
                received_n.fetch_add(1);
            }
        }
    };

    std::jthread p1(producer, 0);
    std::jthread p2(producer, per);
    std::jthread c1(consumer);
    std::jthread c2(consumer);
    p1.join(); p2.join(); c1.join(); c2.join();

    CHECK(received_sum.load() == (2 * per - 1) * per);
    CHECK(received_n.load() == 2 * per);
}

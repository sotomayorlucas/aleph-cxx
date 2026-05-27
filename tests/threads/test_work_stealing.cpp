#include "doctest.h"
#include <atomic>
#include <thread>
import aleph.threads;

using namespace aleph::threads;

TEST_CASE("WorkStealingDeque<int>: owner push/pop FIFO from same end") {
    WorkStealingDeque<int> q;
    q.push(10);
    q.push(20);
    q.push(30);
    int v;
    CHECK(q.pop(v)); CHECK(v == 30);   // LIFO from owner end
    CHECK(q.pop(v)); CHECK(v == 20);
    CHECK(q.pop(v)); CHECK(v == 10);
    CHECK_FALSE(q.pop(v));
}

TEST_CASE("WorkStealingDeque: thieves steal from far end") {
    WorkStealingDeque<int> q;
    for (int i = 0; i < 100; ++i) q.push(i);

    std::atomic<int> stolen{0};
    std::atomic<int> sum{0};
    auto thief = [&] {
        int v;
        for (;;) {
            if (q.steal(v)) {
                stolen.fetch_add(1);
                sum.fetch_add(v);
            } else if (q.empty()) {
                break;
            }
        }
    };
    std::jthread t1(thief), t2(thief);
    t1.join(); t2.join();
    CHECK(stolen.load() == 100);
    CHECK(sum.load() == 99 * 100 / 2);
}

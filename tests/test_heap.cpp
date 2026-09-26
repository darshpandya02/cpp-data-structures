#include <dsl/binary_heap.hpp>

#include <algorithm>
#include <functional>
#include <queue>
#include <string>
#include <vector>

#include "doctest.h"
#include "support.hpp"

using dsl::BinaryHeap;
using dsl::PriorityQueue;
using test::Tracked;

TEST_CASE("BinaryHeap: max-heap by default, min-heap with std::greater") {
    BinaryHeap<int> h{3, 1, 4, 1, 5, 9, 2, 6};
    CHECK(h.is_valid());
    std::vector<int> out;
    while (!h.empty()) out.push_back(h.extract_top());
    CHECK(out == std::vector<int>{9, 6, 5, 4, 3, 2, 1, 1});

    PriorityQueue<int, std::greater<int>> mn;
    for (int x : {5, 3, 8, 1}) mn.push(x);
    CHECK(mn.top() == 1);
    mn.pop();
    CHECK(mn.top() == 3);
    CHECK(mn.size() == 3);
}

TEST_CASE("BinaryHeap: edge cases") {
    BinaryHeap<int> h;
    CHECK(h.empty());
    CHECK(h.is_valid());
    h.push(1);
    CHECK(h.top() == 1);
    h.pop();
    CHECK(h.empty());
    h.push(1);
    h.push(2);
    h.pop();
    CHECK(h.top() == 1);
    for (int i = 0; i < 10; ++i) h.push(7);  // duplicates
    CHECK(h.is_valid());
    h.clear();
    CHECK(h.empty());
}

TEST_CASE("BinaryHeap: heapify from a range is O(n) and valid") {
    std::vector<int> v(10000);
    for (int i = 0; i < 10000; ++i) v[static_cast<std::size_t>(i)] = (i * 7919) % 10007;
    BinaryHeap<int> h(v.begin(), v.end());
    CHECK(h.is_valid());
    CHECK(h.top() == *std::max_element(v.begin(), v.end()));
}

TEST_CASE("BinaryHeap: move-only and tracked elements") {
    test::LiveGuard<Tracked> guard;
    {
        BinaryHeap<Tracked> h;
        for (int i = 0; i < 500; ++i) h.emplace((i * 31) % 500);
        Tracked::countdown = 1;  // sifting must only move, never copy
        int prev = 1 << 30;
        while (!h.empty()) {
            const int t = h.extract_top().v;
            REQUIRE(t <= prev);
            prev = t;
        }
        Tracked::countdown = -1;
    }
    CHECK(guard.leaked() == 0);

    BinaryHeap<std::unique_ptr<int>, bool (*)(const std::unique_ptr<int>&, const std::unique_ptr<int>&)> p(
        [](const std::unique_ptr<int>& a, const std::unique_ptr<int>& b) { return *a < *b; });
    p.push(std::make_unique<int>(2));
    p.push(std::make_unique<int>(5));
    p.push(std::make_unique<int>(1));
    CHECK(*p.top() == 5);
}

TEST_CASE("BinaryHeap: randomized operations match std::priority_queue") {
    for (unsigned seed = 1; seed <= 20; ++seed) {
        auto g = test::rng(seed);
        BinaryHeap<int> h;
        std::priority_queue<int> q;
        for (int step = 0; step < 4000; ++step) {
            if (q.empty() || test::rand_int(g, 0, 2) != 0) {
                const int x = test::rand_int(g, -100, 100);
                h.push(x);
                q.push(x);
            } else {
                REQUIRE(h.top() == q.top());
                h.pop();
                q.pop();
            }
            REQUIRE(h.size() == q.size());
            if (!q.empty()) REQUIRE(h.top() == q.top());
        }
        CHECK(h.is_valid());
    }
}

// The containers are not synchronized, but like the standard containers they
// promise that const member functions do not write shared state. Several
// threads may read one container at once. These tests exercise that under
// ThreadSanitizer (make tsan): any hidden write in a const path is a race.
#include <dsl/dsl.hpp>

#include <atomic>
#include <thread>
#include <vector>

#include "doctest.h"

namespace {

template <class F>
void run_readers(int threads, F f) {
    std::vector<std::thread> ts;
    for (int t = 0; t < threads; ++t) ts.emplace_back(f, t);
    for (auto& t : ts) t.join();
}

}  // namespace

TEST_CASE("concurrent readers: HashMap, AVLTree, DynamicArray, DoublyLinkedList") {
    dsl::HashMap<int, int> map;
    dsl::AVLTree<int, int> tree;
    dsl::DynamicArray<int> arr;
    dsl::DoublyLinkedList<int> list;
    for (int i = 0; i < 20000; ++i) {
        map[i] = i;
        tree.insert(i, i);
        arr.push_back(i);
        list.push_back(i);
    }
    const auto& cmap = map;
    const auto& ctree = tree;
    const auto& carr = arr;
    const auto& clist = list;

    std::atomic<long> total{0};
    run_readers(4, [&](int id) {
        long local = 0;
        for (int i = id; i < 20000; i += 4) {
            local += cmap.find(i)->second;
            local += ctree.find(i)->second;
            local += carr[static_cast<std::size_t>(i)];
            local += cmap.contains(-i - 1) ? 1 : 0;
        }
        for (const auto& kv : ctree) local += kv.first & 1;
        for (int x : clist) local += x & 1;
        total += local;
    });
    // 4 * sum(0..19999) from the three lookups, plus 2 * 10000 odd keys per thread.
    CHECK(total.load() == 3L * (19999L * 20000 / 2) + 4L * 2 * 10000);
}

TEST_CASE("one container per thread, written concurrently") {
    std::atomic<int> ok{0};
    run_readers(4, [&](int id) {
        dsl::HashMap<int, int> m;
        dsl::AVLTree<int, int> t;
        dsl::Queue<int> q;
        for (int i = 0; i < 5000; ++i) {
            m[i * id] = i;
            t.insert(i, i);
            q.push(i);
            if (i % 3 == 0) q.pop();
        }
        if (t.validate() && t.size() == 5000) ++ok;
    });
    CHECK(ok.load() == 4);
}

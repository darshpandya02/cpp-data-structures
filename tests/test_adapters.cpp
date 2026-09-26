// CircularBuffer, Stack, Queue.
#include <dsl/circular_buffer.hpp>
#include <dsl/doubly_linked_list.hpp>
#include <dsl/queue.hpp>
#include <dsl/singly_linked_list.hpp>
#include <dsl/stack.hpp>

#include <algorithm>
#include <deque>
#include <queue>
#include <sstream>
#include <stack>
#include <string>
#include <vector>

#include "doctest.h"
#include "support.hpp"

using dsl::CircularBuffer;
using test::Tracked;

TEST_CASE("CircularBuffer: push and pop at both ends across the wrap point") {
    CircularBuffer<int> b;
    CHECK(b.empty());
    CHECK(b.capacity() == 0);
    b.reserve(4);
    for (int i = 0; i < 4; ++i) b.push_back(i);
    CHECK(b.full());
    b.pop_front();
    b.pop_front();
    b.push_back(4);
    b.push_back(5);  // wraps to physical slots 0 and 1
    CHECK(b.capacity() == 4);
    CHECK(std::vector<int>(b.begin(), b.end()) == std::vector<int>{2, 3, 4, 5});
    b.push_front(1);  // full: grows, keeps logical order
    CHECK(b.capacity() == 8);
    CHECK(std::vector<int>(b.begin(), b.end()) == std::vector<int>{1, 2, 3, 4, 5});
    CHECK(b[0] == 1);
    CHECK(b.at(4) == 5);
    CHECK_THROWS_AS(b.at(5), std::out_of_range);
    CHECK(b.back() == 5);
    b.pop_back();
    CHECK(b.back() == 4);
    CHECK(std::vector<int>(b.rbegin(), b.rend()) == std::vector<int>{4, 3, 2, 1});
}

TEST_CASE("CircularBuffer: random-access iterators work with <algorithm>") {
    CircularBuffer<int> b;
    b.reserve(8);
    for (int i = 0; i < 6; ++i) b.push_back(i);
    for (int i = 0; i < 5; ++i) {
        b.pop_front();
        b.push_back(10 - i);
    }
    std::sort(b.begin(), b.end());
    CHECK(std::is_sorted(b.begin(), b.end()));
    CHECK(b.end() - b.begin() == 6);
    CHECK(*(b.begin() + 2) == b[2]);
    CHECK(std::lower_bound(b.begin(), b.end(), 7) - b.begin() == 2);
}

TEST_CASE("CircularBuffer: overwrite mode keeps the newest N") {
    CircularBuffer<int> b;
    b.reserve(3);
    int overwritten = 0;
    for (int i = 1; i <= 10; ++i) overwritten += b.push_back_overwrite(i) ? 1 : 0;
    CHECK(overwritten == 7);
    CHECK(b.capacity() == 3);
    CHECK(std::vector<int>(b.begin(), b.end()) == std::vector<int>{8, 9, 10});
    std::ostringstream os;
    os << b;
    CHECK(os.str() == "[8, 9, 10]");
}

TEST_CASE("CircularBuffer: copy, move, self-referencing push, no leaks") {
    test::LiveGuard<Tracked> guard;
    {
        CircularBuffer<Tracked> b;
        b.reserve(4);
        for (int i = 0; i < 4; ++i) b.push_back(i);
        b.pop_front();
        b.push_back(4);   // wrapped
        b.push_back(b[0]);  // full, argument refers into the buffer
        CHECK(b.back().v == 1);
        b.push_front(b.back());
        CHECK(b.front().v == 1);
        CircularBuffer<Tracked> c = b;
        CHECK(c == b);
        CircularBuffer<Tracked> d = std::move(c);
        CHECK(c.empty());  // NOLINT
        CHECK(d == b);
        c = d;
        d.clear();
        CHECK(c.size() == 6);
        Tracked x(5);
        c.reserve(c.size());
        while (!c.full()) c.push_back(0);
        const auto before = c;
        Tracked::countdown = 1;
        CHECK_THROWS_AS(c.push_back(x), test::Boom);  // strong guarantee on growth
        Tracked::countdown = -1;
        CHECK(c == before);
    }
    CHECK(guard.leaked() == 0);
}

TEST_CASE("CircularBuffer: allocator accounting") {
    using Alloc = test::CountingAllocator<int>;
    Alloc alloc;
    {
        CircularBuffer<int, Alloc> b(alloc);
        for (int i = 0; i < 1000; ++i) b.push_back(i);
        for (int i = 0; i < 500; ++i) b.pop_front();
        CircularBuffer<int, Alloc> o{Alloc{}};
        o = std::move(b);
        CHECK(o.size() == 500);
        CHECK(o.front() == 500);
    }
    CHECK(alloc.stats->live_bytes == 0);
}

TEST_CASE("CircularBuffer: randomized operations match std::deque") {
    for (unsigned seed = 1; seed <= 20; ++seed) {
        auto g = test::rng(seed);
        CircularBuffer<int> b;
        std::deque<int> d;
        for (int step = 0; step < 3000; ++step) {
            const int op = test::rand_int(g, 0, 5);
            const int x = test::rand_int(g, 0, 1000);
            if (op == 0) {
                b.push_back(x);
                d.push_back(x);
            } else if (op == 1) {
                b.push_front(x);
                d.push_front(x);
            } else if (op == 2 && !d.empty()) {
                b.pop_front();
                d.pop_front();
            } else if (op == 3 && !d.empty()) {
                b.pop_back();
                d.pop_back();
            } else if (op == 4 && !d.empty()) {
                const auto i = static_cast<std::size_t>(test::rand_int(g, 0, static_cast<int>(d.size()) - 1));
                REQUIRE(b[i] == d[i]);
            } else if (op == 5 && b.capacity() > 0 && step % 7 == 0) {
                b.push_back_overwrite(x);
                // Mirror the overwrite semantics on the deque.
                d.push_back(x);
                if (d.size() > b.size()) d.pop_front();
            }
            REQUIRE(b.size() == d.size());
        }
        CHECK(std::equal(b.begin(), b.end(), d.begin(), d.end()));
    }
}

TEST_CASE("Stack: LIFO over each backing container") {
    dsl::Stack<int> s;
    dsl::Stack<int, dsl::DoublyLinkedList<int>> ls;
    dsl::Stack<int, dsl::CircularBuffer<int>> cs;
    std::stack<int> ref;
    for (int i = 0; i < 100; ++i) {
        s.push(i);
        ls.push(i);
        cs.emplace(i);
        ref.push(i);
    }
    while (!ref.empty()) {
        REQUIRE(s.top() == ref.top());
        REQUIRE(ls.top() == ref.top());
        REQUIRE(cs.top() == ref.top());
        s.pop();
        ls.pop();
        cs.pop();
        ref.pop();
    }
    CHECK(s.empty());
    CHECK(ls.empty());
    CHECK(cs.empty());
    s.push(1);
    s.push(2);
    std::ostringstream os;
    os << s;
    CHECK(os.str() == "[1, 2>");
    dsl::Stack<int> t;
    t.push(1);
    CHECK(s != t);
    t.push(2);
    CHECK(s == t);
}

TEST_CASE("Queue: FIFO over each backing container") {
    dsl::Queue<std::string> q;
    dsl::Queue<std::string, dsl::DoublyLinkedList<std::string>> lq;
    dsl::Queue<std::string, dsl::SinglyLinkedList<std::string>> sq;
    std::queue<std::string> ref;
    auto g = test::rng(7);
    for (int step = 0; step < 5000; ++step) {
        if (ref.empty() || test::rand_int(g, 0, 2) != 0) {
            const std::string v = std::to_string(step);
            q.push(v);
            lq.push(v);
            sq.push(v);
            ref.push(v);
        } else {
            REQUIRE(q.front() == ref.front());
            REQUIRE(lq.front() == ref.front());
            REQUIRE(sq.front() == ref.front());
            q.pop();
            lq.pop();
            sq.pop();
            ref.pop();
        }
        REQUIRE(q.size() == ref.size());
        if (!ref.empty()) REQUIRE(q.back() == ref.back());
    }
    dsl::Queue<int> a, b;
    a.push(1);
    a.push(2);
    b.push(0);
    b.push(1);
    b.push(2);
    b.pop();
    CHECK(a == b);
    std::ostringstream os;
    os << a;
    CHECK(os.str() == "<1, 2]");
}

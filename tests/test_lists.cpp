#include <dsl/doubly_linked_list.hpp>
#include <dsl/singly_linked_list.hpp>

#include <algorithm>
#include <forward_list>
#include <iterator>
#include <list>
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "support.hpp"

using dsl::DoublyLinkedList;
using dsl::SinglyLinkedList;
using test::Tracked;

template <class L>
std::vector<int> to_vec(const L& l) {
    std::vector<int> v;
    for (const auto& x : l) {
        if constexpr (requires { x.v; }) v.push_back(x.v);
        else v.push_back(x);
    }
    return v;
}

// ---------------------------------------------------------------------------
// SinglyLinkedList
// ---------------------------------------------------------------------------

TEST_CASE("SinglyLinkedList: basics") {
    SinglyLinkedList<int> l;
    CHECK(l.empty());
    CHECK(l.begin() == l.end());
    l.push_back(2);
    l.push_front(1);
    l.push_back(3);
    CHECK(to_vec(l) == std::vector<int>{1, 2, 3});
    CHECK(l.front() == 1);
    CHECK(l.back() == 3);
    CHECK(l.size() == 3);
    l.pop_front();
    CHECK(to_vec(l) == std::vector<int>{2, 3});
    l.pop_front();
    l.pop_front();
    CHECK(l.empty());
    l.push_back(7);  // tail must be reset after the list emptied
    CHECK(l.front() == 7);
    CHECK(l.back() == 7);
}

TEST_CASE("SinglyLinkedList: insert_after / erase_after keep the tail right") {
    SinglyLinkedList<int> l{1, 2, 3};
    auto it = l.begin();
    ++it;
    l.insert_after(it, 25);
    CHECK(to_vec(l) == std::vector<int>{1, 2, 25, 3});
    auto last = std::next(l.begin(), 3);
    l.insert_after(last, 4);  // after the tail: becomes the new tail
    CHECK(l.back() == 4);
    l.erase_after(std::next(l.begin(), 3));  // erase the tail
    CHECK(l.back() == 3);
    l.push_back(5);
    CHECK(to_vec(l) == std::vector<int>{1, 2, 25, 3, 5});
    l.erase_after(l.before_begin());
    CHECK(l.front() == 2);
}

TEST_CASE("SinglyLinkedList: reverse, remove_if, printing, equality") {
    SinglyLinkedList<int> l{1, 2, 3, 4, 5, 6};
    l.reverse();
    CHECK(to_vec(l) == std::vector<int>{6, 5, 4, 3, 2, 1});
    CHECK(l.back() == 1);
    l.push_back(0);
    CHECK(l.remove_if([](int x) { return x % 2 == 0; }) == 4);
    CHECK(to_vec(l) == std::vector<int>{5, 3, 1});
    CHECK(l.back() == 1);
    CHECK(l.remove(1) == 1);
    CHECK(l.back() == 3);
    std::ostringstream os;
    os << l;
    CHECK(os.str() == "[5, 3]");
    CHECK(l == SinglyLinkedList<int>{5, 3});
    CHECK(l != SinglyLinkedList<int>{5, 3, 1});
    SinglyLinkedList<int> e;
    e.reverse();
    CHECK(e.empty());
}

TEST_CASE("SinglyLinkedList: copy, move, swap") {
    test::LiveGuard<Tracked> guard;
    {
        SinglyLinkedList<Tracked> a{1, 2, 3};
        SinglyLinkedList<Tracked> b = a;
        CHECK(a == b);
        SinglyLinkedList<Tracked> c = std::move(a);
        CHECK(a.empty());  // NOLINT
        c.push_back(4);    // tail of the moved-to list is valid
        CHECK(c.back().v == 4);
        a.push_back(9);  // and so is the moved-from list's
        CHECK(a.back().v == 9);
        b = c;
        CHECK(b.size() == 4);
        b = std::move(a);
        CHECK(b.size() == 1);
        swap(b, c);
        CHECK(b.size() == 4);
        CHECK(c.back().v == 9);
        SinglyLinkedList<Tracked> empty;
        swap(empty, c);
        CHECK(c.empty());
        c.push_back(1);  // tail fixed after swapping with an empty list
        CHECK(c.size() == 1);
    }
    CHECK(guard.leaked() == 0);
}

TEST_CASE("SinglyLinkedList: strong guarantee on push") {
    test::LiveGuard<Tracked> guard;
    SinglyLinkedList<Tracked> l{1, 2};
    Tracked x(3);
    Tracked::countdown = 1;
    CHECK_THROWS_AS(l.push_back(x), test::Boom);
    CHECK(to_vec(l) == std::vector<int>{1, 2});
}

TEST_CASE("SinglyLinkedList: allocator accounting") {
    using Alloc = test::CountingAllocator<int>;
    Alloc alloc;
    {
        SinglyLinkedList<int, Alloc> l(alloc);
        for (int i = 0; i < 100; ++i) l.push_back(i);
        CHECK(alloc.stats->allocations == 100);
        l.remove_if([](int x) { return x < 50; });
        SinglyLinkedList<int, Alloc> other{Alloc{}};
        other = std::move(l);  // unequal, non-propagating: element-wise
        CHECK(other.size() == 50);
        CHECK(alloc.stats->live_bytes == 0);  // l was cleared
    }
    CHECK(alloc.stats->allocations == alloc.stats->deallocations);
}

TEST_CASE("SinglyLinkedList: randomized operations match std::forward_list") {
    for (unsigned seed = 1; seed <= 20; ++seed) {
        auto g = test::rng(seed);
        SinglyLinkedList<int> a;
        std::forward_list<int> f;
        std::size_t n = 0;
        for (int step = 0; step < 2000; ++step) {
            const int op = test::rand_int(g, 0, 6);
            const int x = test::rand_int(g, 0, 100);
            if (op <= 1) {
                a.push_front(x);
                f.push_front(x);
                ++n;
            } else if (op == 2) {
                a.push_back(x);
                auto it = f.before_begin();
                for (std::size_t i = 0; i < n; ++i) ++it;
                f.insert_after(it, x);
                ++n;
            } else if (op == 3 && n) {
                a.pop_front();
                f.pop_front();
                --n;
            } else if (op == 4 && n) {
                const auto k = static_cast<std::size_t>(test::rand_int(g, 0, static_cast<int>(n) - 1));
                auto ia = a.before_begin();
                auto ib = f.before_begin();
                for (std::size_t i = 0; i < k; ++i) ++ia, ++ib;
                a.erase_after(ia);
                f.erase_after(ib);
                --n;
            } else if (op == 5) {
                a.reverse();
                f.reverse();
            } else if (op == 6) {
                const int before = static_cast<int>(a.size());
                a.remove(x);
                f.remove(x);
                n = static_cast<std::size_t>(std::distance(f.begin(), f.end()));
                REQUIRE(static_cast<int>(a.size()) <= before);
            }
            REQUIRE(a.size() == n);
            if (n) REQUIRE(a.back() == *std::next(f.begin(), static_cast<long>(n) - 1));
        }
        CHECK(std::equal(a.begin(), a.end(), f.begin(), f.end()));
    }
}

// ---------------------------------------------------------------------------
// DoublyLinkedList
// ---------------------------------------------------------------------------

TEST_CASE("DoublyLinkedList: basics and bidirectional iteration") {
    DoublyLinkedList<int> l;
    CHECK(l.empty());
    CHECK(l.begin() == l.end());
    l.push_back(2);
    l.push_front(1);
    l.push_back(3);
    CHECK(to_vec(l) == std::vector<int>{1, 2, 3});
    CHECK(*std::prev(l.end()) == 3);
    std::vector<int> rev(l.rbegin(), l.rend());
    CHECK(rev == std::vector<int>{3, 2, 1});
    l.pop_back();
    l.pop_front();
    CHECK(to_vec(l) == std::vector<int>{2});
    l.pop_back();
    CHECK(l.empty());
}

TEST_CASE("DoublyLinkedList: insert and erase keep iterators to other elements valid") {
    DoublyLinkedList<int> l{1, 2, 3, 4};
    auto two = std::next(l.begin());
    auto four = std::prev(l.end());
    l.insert(two, 15);
    l.erase(std::next(two));  // erase 3
    CHECK(*two == 2);
    CHECK(*four == 4);
    CHECK(to_vec(l) == std::vector<int>{1, 15, 2, 4});
    auto it = l.erase(l.begin(), two);
    CHECK(it == two);
    CHECK(to_vec(l) == std::vector<int>{2, 4});
}

TEST_CASE("DoublyLinkedList: reverse, sort (stable), splice, remove_if") {
    DoublyLinkedList<int> l{5, 1, 4, 2, 3};
    l.reverse();
    CHECK(to_vec(l) == std::vector<int>{3, 2, 4, 1, 5});
    l.sort();
    CHECK(to_vec(l) == std::vector<int>{1, 2, 3, 4, 5});
    l.sort(std::greater<>());
    CHECK(to_vec(l) == std::vector<int>{5, 4, 3, 2, 1});
    CHECK(*std::prev(l.end()) == 1);

    // Stability: sort pairs by first only, the second keeps its order.
    DoublyLinkedList<std::pair<int, int>> p{{2, 0}, {1, 1}, {2, 2}, {1, 3}, {2, 4}};
    p.sort([](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<int> seconds;
    for (const auto& [k, v] : p) seconds.push_back(v);
    CHECK(seconds == std::vector<int>{1, 3, 0, 2, 4});

    DoublyLinkedList<int> other{10, 11};
    l.splice(std::next(l.begin()), other);
    CHECK(other.empty());
    CHECK(to_vec(l) == std::vector<int>{5, 10, 11, 4, 3, 2, 1});
    CHECK(l.size() == 7);
    CHECK(l.remove_if([](int x) { return x > 4; }) == 3);
    CHECK(to_vec(l) == std::vector<int>{4, 3, 2, 1});
    std::ostringstream os;
    os << l;
    CHECK(os.str() == "[4, 3, 2, 1]");
}

TEST_CASE("DoublyLinkedList: copy, move and swap re-point the sentinel") {
    test::LiveGuard<Tracked> guard;
    {
        DoublyLinkedList<Tracked> a{1, 2, 3};
        DoublyLinkedList<Tracked> b = a;
        CHECK(a == b);
        DoublyLinkedList<Tracked> c = std::move(a);
        CHECK(a.empty());  // NOLINT
        CHECK(std::prev(c.end())->v == 3);
        c.push_back(4);
        CHECK(c.size() == 4);
        std::vector<int> rev;
        for (auto it = c.rbegin(); it != c.rend(); ++it) rev.push_back(it->v);
        CHECK(rev == std::vector<int>{4, 3, 2, 1});
        DoublyLinkedList<Tracked> empty;
        swap(c, empty);
        CHECK(c.empty());
        CHECK(empty.size() == 4);
        CHECK(std::prev(empty.end())->v == 4);
        c = empty;
        CHECK(c == empty);
        c = std::move(b);
        CHECK(c.size() == 3);
    }
    CHECK(guard.leaked() == 0);
}

TEST_CASE("DoublyLinkedList: failed construction from a range does not leak") {
    test::LiveGuard<Tracked> guard;
    std::vector<Tracked> src{1, 2, 3, 4, 5};
    Tracked::countdown = 4;
    CHECK_THROWS_AS((DoublyLinkedList<Tracked>(src.begin(), src.end())), test::Boom);
    Tracked::countdown = -1;
    src.clear();
    CHECK(guard.leaked() == 0);
}

TEST_CASE("DoublyLinkedList: allocator accounting") {
    using Alloc = test::CountingAllocator<int>;
    Alloc alloc;
    {
        DoublyLinkedList<int, Alloc> l(alloc);
        for (int i = 0; i < 64; ++i) l.push_front(i);
        l.sort();
        auto copy = l;
        CHECK(copy.get_allocator() == alloc);  // select_on_container_copy_construction returns a copy
        DoublyLinkedList<int, Alloc> other{Alloc{}};
        other = std::move(copy);
        CHECK(other.size() == 64);
    }
    CHECK(alloc.stats->live_bytes == 0);
    CHECK(alloc.stats->allocations == alloc.stats->deallocations);
}

TEST_CASE("DoublyLinkedList: randomized operations match std::list") {
    for (unsigned seed = 1; seed <= 20; ++seed) {
        auto g = test::rng(seed);
        DoublyLinkedList<int> a;
        std::list<int> s;
        for (int step = 0; step < 2000; ++step) {
            const int op = test::rand_int(g, 0, 8);
            const int x = test::rand_int(g, 0, 50);
            if (op == 0) {
                a.push_front(x);
                s.push_front(x);
            } else if (op == 1) {
                a.push_back(x);
                s.push_back(x);
            } else if (op == 2 && !s.empty()) {
                a.pop_front();
                s.pop_front();
            } else if (op == 3 && !s.empty()) {
                a.pop_back();
                s.pop_back();
            } else if (op == 4) {
                const auto k = test::rand_int(g, 0, static_cast<int>(s.size()));
                a.insert(std::next(a.begin(), k), x);
                s.insert(std::next(s.begin(), k), x);
            } else if (op == 5 && !s.empty()) {
                const auto k = test::rand_int(g, 0, static_cast<int>(s.size()) - 1);
                a.erase(std::next(a.begin(), k));
                s.erase(std::next(s.begin(), k));
            } else if (op == 6) {
                a.reverse();
                s.reverse();
            } else if (op == 7 && step % 50 == 0) {
                a.sort();
                s.sort();
            } else if (op == 8) {
                a.remove(x);
                s.remove(x);
            }
            REQUIRE(a.size() == s.size());
        }
        CHECK(std::equal(a.begin(), a.end(), s.begin(), s.end()));
        CHECK(std::equal(a.rbegin(), a.rend(), s.rbegin(), s.rend()));
    }
}

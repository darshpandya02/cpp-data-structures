#include <dsl/dynamic_array.hpp>

#include <algorithm>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "support.hpp"

using dsl::DynamicArray;
using test::Tracked;
using test::ThrowingMove;

TEST_CASE("DynamicArray: empty state") {
    DynamicArray<int> a;
    CHECK(a.empty());
    CHECK(a.size() == 0);
    CHECK(a.capacity() == 0);
    CHECK(a.begin() == a.end());
    CHECK(a.data() == nullptr);
    CHECK_THROWS_AS(a.at(0), std::out_of_range);
    a.shrink_to_fit();
    a.clear();
    CHECK(a.empty());
}

TEST_CASE("DynamicArray: push_back, indexing, iteration") {
    DynamicArray<int> a;
    for (int i = 0; i < 1000; ++i) a.push_back(i);
    REQUIRE(a.size() == 1000);
    CHECK(a.front() == 0);
    CHECK(a.back() == 999);
    CHECK(a[500] == 500);
    CHECK(a.at(999) == 999);
    CHECK_THROWS_AS(a.at(1000), std::out_of_range);
    CHECK(std::accumulate(a.begin(), a.end(), 0L) == 999L * 1000 / 2);
    CHECK(*a.rbegin() == 999);
    std::sort(a.begin(), a.end(), std::greater<>());  // raw-pointer iterators work with <algorithm>
    CHECK(a.front() == 999);
}

TEST_CASE("DynamicArray: growth policies") {
    DynamicArray<int, std::allocator<int>, dsl::growth::Doubling> d;
    DynamicArray<int, std::allocator<int>, dsl::growth::OneAndHalf> g;
    std::vector<std::size_t> dc, gc;
    for (int i = 0; i < 100; ++i) {
        d.push_back(i);
        g.push_back(i);
        if (dc.empty() || dc.back() != d.capacity()) dc.push_back(d.capacity());
        if (gc.empty() || gc.back() != g.capacity()) gc.push_back(g.capacity());
    }
    CHECK(dc == std::vector<std::size_t>{4, 8, 16, 32, 64, 128});
    CHECK(gc == std::vector<std::size_t>{4, 6, 9, 13, 19, 28, 42, 63, 94, 141});
}

TEST_CASE("DynamicArray: constructors and assignment") {
    DynamicArray<std::string> a(3, "x");
    CHECK(a.size() == 3);
    CHECK(a[2] == "x");
    DynamicArray<int> v(5);
    CHECK(std::all_of(v.begin(), v.end(), [](int x) { return x == 0; }));
    std::vector<int> src{1, 2, 3, 4};
    DynamicArray<int> r(src.begin(), src.end());
    CHECK(r == DynamicArray<int>{1, 2, 3, 4});

    DynamicArray<int> c = r;  // copy
    CHECK(c == r);
    c[0] = 42;
    CHECK(r[0] == 1);

    DynamicArray<int> m = std::move(c);  // move leaves source empty
    CHECK(m[0] == 42);
    CHECK(c.empty());  // NOLINT: checking moved-from state on purpose

    DynamicArray<int> big{9, 9, 9, 9, 9, 9, 9, 9, 9};
    big = r;  // shrinks
    CHECK(big == r);
    DynamicArray<int> small{1};
    small = big;  // grows
    CHECK(small == r);
    auto& self = small;
    small = self;  // self-assignment
    CHECK(small == r);
    small = {7, 8};
    CHECK(small == DynamicArray<int>{7, 8});
    CHECK(small != r);
}

TEST_CASE("DynamicArray: insert, erase, resize") {
    DynamicArray<int> a{1, 2, 3};
    a.insert(a.begin(), 0);
    a.insert(a.end(), 4);
    a.insert(a.begin() + 2, 99);
    CHECK(a == DynamicArray<int>{0, 1, 99, 2, 3, 4});
    auto it = a.erase(a.begin() + 2);
    CHECK(*it == 2);
    a.erase(a.begin(), a.begin() + 2);
    CHECK(a == DynamicArray<int>{2, 3, 4});
    a.resize(5, 7);
    CHECK(a == DynamicArray<int>{2, 3, 4, 7, 7});
    a.resize(1);
    CHECK(a == DynamicArray<int>{2});
    a.reserve(100);
    CHECK(a.capacity() >= 100);
    a.shrink_to_fit();
    CHECK(a.capacity() == 1);
    a.pop_back();
    CHECK(a.empty());
}

TEST_CASE("DynamicArray: self-referencing push_back and insert") {
    DynamicArray<std::string> a{"hello"};
    a.shrink_to_fit();
    for (int i = 0; i < 10; ++i) a.push_back(a[0]);  // reference into the buffer that reallocates
    CHECK(std::all_of(a.begin(), a.end(), [](const std::string& s) { return s == "hello"; }));
    a.shrink_to_fit();
    a.insert(a.begin() + 1, a.back());
    CHECK(a[1] == "hello");
    a.insert(a.begin(), a[3]);
    CHECK(a[0] == "hello");
    CHECK(a.size() == 13);
}

TEST_CASE("DynamicArray: operator<<") {
    std::ostringstream os;
    os << DynamicArray<int>{1, 2, 3} << ' ' << DynamicArray<int>{};
    CHECK(os.str() == "[1, 2, 3] []");
}

TEST_CASE("DynamicArray: strong guarantee when a copy throws during reallocation") {
    test::LiveGuard<ThrowingMove> guard;
    {
        DynamicArray<ThrowingMove> a;
        for (int i = 0; i < 4; ++i) a.push_back(ThrowingMove(i));
        a.shrink_to_fit();
        REQUIRE(a.size() == a.capacity());
        const auto* old_data = a.data();
        // The next push_back reallocates. Moves may throw, so the array copies:
        // 1 copy for the new element plus 4 relocations. Make the 3rd one throw.
        ThrowingMove::countdown = 3;
        ThrowingMove extra(100);
        CHECK_THROWS_AS(a.push_back(extra), test::Boom);
        ThrowingMove::countdown = -1;
        CHECK(a.size() == 4);
        CHECK(a.data() == old_data);
        for (int i = 0; i < 4; ++i) CHECK(a[static_cast<std::size_t>(i)].v == i);
    }
    CHECK(guard.leaked() == 0);
}

TEST_CASE("DynamicArray: strong guarantee when the new element's constructor throws") {
    test::LiveGuard<Tracked> guard;
    {
        DynamicArray<Tracked> a{1, 2, 3};
        const auto cap = a.capacity();
        Tracked::countdown = 1;  // the copy of `x` throws
        Tracked x(9);
        CHECK_THROWS_AS(a.push_back(x), test::Boom);
        CHECK(a == DynamicArray<Tracked>{1, 2, 3});
        CHECK(a.capacity() == cap);
    }
    CHECK(guard.leaked() == 0);
}

TEST_CASE("DynamicArray: uses noexcept moves when growing") {
    test::LiveGuard<Tracked> guard;
    DynamicArray<Tracked> a;
    for (int i = 0; i < 100; ++i) a.emplace_back(i);
    Tracked::countdown = 1;  // any copy would throw; growth must only move
    for (int i = 0; i < 1000; ++i) a.emplace_back(i);
    Tracked::countdown = -1;
    CHECK(a.size() == 1100);
}

TEST_CASE("DynamicArray: no leaks across operations with a counting allocator") {
    using Alloc = test::CountingAllocator<Tracked>;
    test::LiveGuard<Tracked> guard;
    Alloc alloc;
    {
        DynamicArray<Tracked, Alloc> a(alloc);
        for (int i = 0; i < 200; ++i) a.push_back(i);
        a.erase(a.begin(), a.begin() + 50);
        a.insert(a.begin() + 10, Tracked(-5));
        auto b = a;
        b.resize(20);
        a = b;
        a.shrink_to_fit();
        DynamicArray<Tracked, Alloc> other{Alloc{}};  // different allocator
        other.push_back(1);
        other = std::move(a);  // non-propagating, unequal: element-wise move
        CHECK(other.size() == 20);
        CHECK(other.get_allocator().stats != alloc.stats);
    }
    CHECK(alloc.stats->live_bytes == 0);
    CHECK(alloc.stats->allocations == alloc.stats->deallocations);
    CHECK(guard.leaked() == 0);
}

TEST_CASE("DynamicArray: propagating allocator follows on move and copy assignment") {
    using Alloc = test::CountingAllocator<int, true>;
    Alloc a1, a2;
    DynamicArray<int, Alloc> x(a1), y(a2);
    x.push_back(1);
    y.push_back(2);
    y = std::move(x);
    CHECK(y.get_allocator() == a1);
    DynamicArray<int, Alloc> z(a2);
    z = y;
    CHECK(z.get_allocator() == a1);
    z.swap(x);
    CHECK(x.get_allocator() == a1);
}

TEST_CASE("DynamicArray: randomized operations match std::vector") {
    for (unsigned seed = 1; seed <= 20; ++seed) {
        auto g = test::rng(seed);
        DynamicArray<int> a;
        std::vector<int> v;
        for (int step = 0; step < 2000; ++step) {
            const int op = test::rand_int(g, 0, 9);
            const int x = test::rand_int(g, -1000, 1000);
            if (op <= 3) {
                a.push_back(x);
                v.push_back(x);
            } else if (op == 4 && !v.empty()) {
                a.pop_back();
                v.pop_back();
            } else if (op == 5) {
                const auto i = static_cast<std::size_t>(test::rand_int(g, 0, static_cast<int>(v.size())));
                a.insert(a.begin() + i, x);
                v.insert(v.begin() + static_cast<long>(i), x);
            } else if (op == 6 && !v.empty()) {
                const auto i = static_cast<std::size_t>(test::rand_int(g, 0, static_cast<int>(v.size()) - 1));
                a.erase(a.begin() + i);
                v.erase(v.begin() + static_cast<long>(i));
            } else if (op == 7) {
                const auto n = static_cast<std::size_t>(test::rand_int(g, 0, 64));
                a.resize(n, x);
                v.resize(n, x);
            } else if (op == 8 && !v.empty()) {
                const auto i = static_cast<std::size_t>(test::rand_int(g, 0, static_cast<int>(v.size()) - 1));
                a[i] = x;
                v[i] = x;
            } else if (op == 9) {
                a.shrink_to_fit();
            }
            REQUIRE(a.size() == v.size());
        }
        CHECK(std::equal(a.begin(), a.end(), v.begin(), v.end()));
    }
}

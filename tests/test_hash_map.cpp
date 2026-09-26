#include <dsl/hash_map.hpp>

#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "doctest.h"
#include "support.hpp"

using dsl::HashMap;
using test::Tracked;

TEST_CASE("HashMap: empty map") {
    HashMap<int, int> m;
    CHECK(m.empty());
    CHECK(m.bucket_count() == 0);
    CHECK(m.begin() == m.end());
    CHECK(m.find(1) == m.end());
    CHECK_FALSE(m.contains(1));
    CHECK(m.erase(1) == 0);
    CHECK_THROWS_AS(m.at(1), std::out_of_range);
    CHECK(m.load_factor() == 0.0f);
    m.clear();
}

TEST_CASE("HashMap: insert, lookup, update, erase") {
    HashMap<std::string, int> m;
    CHECK(m.insert("one", 1).second);
    CHECK_FALSE(m.insert("one", 100).second);
    CHECK(m.at("one") == 1);
    m["two"] = 2;
    m["two"] += 1;
    CHECK(m.at("two") == 3);
    m.insert_or_assign("one", 11);
    CHECK(m.at("one") == 11);
    auto it = m.find("two");
    REQUIRE(it != m.end());
    it.value() = 22;  // mutable value through the iterator
    CHECK(m.at("two") == 22);
    CHECK(it->first == "two");
    CHECK(m.size() == 2);
    CHECK(m.erase("one") == 1);
    CHECK(m.erase("one") == 0);
    CHECK(m.size() == 1);
    CHECK_FALSE(m.contains("one"));
}

TEST_CASE("HashMap: grows by doubling at the load factor") {
    HashMap<int, int> m;
    std::vector<std::size_t> caps;
    for (int i = 0; i < 1000; ++i) {
        m[i] = i;
        if (caps.empty() || caps.back() != m.bucket_count()) caps.push_back(m.bucket_count());
        REQUIRE(m.load_factor() <= m.max_load_factor());
    }
    CHECK(caps == std::vector<std::size_t>{8, 16, 32, 64, 128, 256, 512, 1024, 2048});
    for (int i = 0; i < 1000; ++i) REQUIRE(m.at(i) == i);
    m.reserve(10000);
    CHECK(m.bucket_count() == 16384);
    for (int i = 0; i < 1000; ++i) REQUIRE(m.at(i) == i);
    m.rehash(0);  // shrink back to the smallest table that fits
    CHECK(m.bucket_count() == 2048);
    CHECK(m.size() == 1000);
    m.max_load_factor(0.5f);
    CHECK(m.load_factor() <= 0.5f);
    CHECK_THROWS_AS(m.max_load_factor(1.0f), std::invalid_argument);
}

TEST_CASE("HashMap: Robin Hood keeps probes short with the identity hash") {
    HashMap<int, int> m;
    for (int i = 0; i < 100000; ++i) m[i * 1024] = i;  // strided keys: bad for a plain modulo
    CHECK(m.size() == 100000);
    CHECK(m.max_probe_length() < 32);
}

struct ConstantHash {
    std::size_t operator()(int) const noexcept { return 42; }
};

TEST_CASE("HashMap: survives a hash function where every key collides") {
    HashMap<int, int, ConstantHash> m;
    for (int i = 0; i < 500; ++i) m[i] = i;
    for (int i = 0; i < 500; ++i) REQUIRE(m.at(i) == i);
    CHECK(m.max_probe_length() == 500);
    for (int i = 0; i < 500; i += 2) m.erase(i);  // backward shift across the wrap point
    for (int i = 0; i < 500; ++i) REQUIRE(m.contains(i) == (i % 2 == 1));
}

TEST_CASE("HashMap: equality ignores order, printing is complete") {
    HashMap<int, int> a, b;
    for (int i = 0; i < 100; ++i) a[i] = i * 2;
    for (int i = 99; i >= 0; --i) b[i] = i * 2;
    b.reserve(5000);  // different table size, different slot order
    CHECK(a == b);
    b[5] = 0;
    CHECK(a != b);
    HashMap<int, int> one{{7, 49}};
    std::ostringstream os;
    os << one;
    CHECK(os.str() == "{7: 49}");
}

TEST_CASE("HashMap: copy, move, no leaks") {
    test::LiveGuard<Tracked> guard;
    {
        HashMap<std::string, Tracked> m;
        for (int i = 0; i < 300; ++i) m.try_emplace(std::to_string(i), i);
        HashMap<std::string, Tracked> c = m;
        CHECK(c == m);
        for (int i = 0; i < 300; i += 3) c.erase(std::to_string(i));
        HashMap<std::string, Tracked> d = std::move(c);
        CHECK(c.empty());  // NOLINT
        CHECK(d.size() == 200);
        m = d;
        CHECK(m == d);
        m = std::move(d);
        CHECK(m.size() == 200);
        m.clear();
        CHECK(m.empty());
        m["x"] = 1;
    }
    CHECK(guard.leaked() == 0);
}

TEST_CASE("HashMap: a throwing key/value copy leaves the map unchanged") {
    test::LiveGuard<Tracked> guard;
    HashMap<int, Tracked> m;
    for (int i = 0; i < 7; ++i) m.try_emplace(i, i);  // next insert triggers a rehash
    const auto before = m;
    Tracked v(99);
    Tracked::countdown = 1;
    CHECK_THROWS_AS(m.insert(100, v), test::Boom);
    Tracked::countdown = -1;
    CHECK(m == before);
    CHECK(m.bucket_count() == before.bucket_count());
}

TEST_CASE("HashMap: allocator accounting and unequal-allocator move") {
    using Alloc = test::CountingAllocator<std::pair<int, int>>;
    Alloc alloc;
    {
        HashMap<int, int, std::hash<int>, std::equal_to<int>, Alloc> m(alloc);
        for (int i = 0; i < 5000; ++i) m[i] = i;
        HashMap<int, int, std::hash<int>, std::equal_to<int>, Alloc> o{Alloc{}};
        o = std::move(m);
        CHECK(o.size() == 5000);
        CHECK(o.at(4999) == 4999);
    }
    CHECK(alloc.stats->live_bytes == 0);
    CHECK(alloc.stats->allocations == alloc.stats->deallocations);
}

TEST_CASE("HashMap: randomized operations match std::unordered_map") {
    for (unsigned seed = 1; seed <= 20; ++seed) {
        auto g = test::rng(seed);
        HashMap<int, int> h;
        std::unordered_map<int, int> u;
        const int key_space = test::rand_int(g, 8, 5000);
        for (int step = 0; step < 5000; ++step) {
            const int op = test::rand_int(g, 0, 7);
            const int k = test::rand_int(g, -key_space, key_space);
            if (op <= 2) {
                REQUIRE(h.insert(k, step).second == u.insert({k, step}).second);
            } else if (op <= 4) {
                REQUIRE(h.erase(k) == u.erase(k));
            } else if (op == 5) {
                auto it = h.find(k);
                auto jt = u.find(k);
                REQUIRE((it == h.end()) == (jt == u.end()));
                if (jt != u.end()) REQUIRE(it->second == jt->second);
            } else if (op == 6) {
                h[k] += 3;
                u[k] += 3;
            } else if (step % 1000 == 0) {
                h.rehash(0);
            }
            REQUIRE(h.size() == u.size());
        }
        std::size_t n = 0;
        for (const auto& [k, v] : h) {
            REQUIRE(u.at(k) == v);
            ++n;
        }
        CHECK(n == u.size());
    }
}

#include <dsl/avl_tree.hpp>
#include <dsl/binary_search_tree.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "support.hpp"

using dsl::AVLTree;
using dsl::BinarySearchTree;
using test::Tracked;

template <class Tree>
std::vector<int> keys_of(const Tree& t) {
    std::vector<int> k;
    for (const auto& kv : t) k.push_back(kv.first);
    return k;
}

template <class Tree>
std::vector<int> preorder_keys(const Tree& t) {
    std::vector<int> k;
    t.preorder([&](const auto& kv) { k.push_back(kv.first); });
    return k;
}

TEST_CASE_TEMPLATE("ordered map: common behaviour", Tree, BinarySearchTree<int, std::string>, AVLTree<int, std::string>) {
    Tree t;
    CHECK(t.empty());
    CHECK(t.height() == 0);
    CHECK(t.begin() == t.end());
    CHECK(t.find(1) == t.end());
    CHECK_THROWS_AS(t.at(1), std::out_of_range);

    for (int k : {50, 30, 70, 20, 40, 60, 80}) CHECK(t.insert(k, std::to_string(k)).second);
    CHECK_FALSE(t.insert(50, "dup").second);
    CHECK(t.at(50) == "50");
    CHECK(t.size() == 7);
    CHECK(keys_of(t) == std::vector<int>{20, 30, 40, 50, 60, 70, 80});
    CHECK(t.validate());

    t[45] = "x";
    CHECK(t.at(45) == "x");
    t.insert_or_assign(45, "y");
    CHECK(t.at(45) == "y");
    CHECK(t.contains(45));
    CHECK(t.count(46) == 0);
    CHECK(t.lower_bound(41)->first == 45);
    CHECK(t.lower_bound(81) == t.end());
    CHECK(std::prev(t.end())->first == 80);  // --end() reaches the maximum
    std::vector<int> rev;
    for (auto it = t.rbegin(); it != t.rend(); ++it) rev.push_back(it->first);
    CHECK(rev == std::vector<int>{80, 70, 60, 50, 45, 40, 30, 20});

    // erase: leaf, one child, two children, root, missing
    CHECK(t.erase(20) == 1);
    CHECK(t.erase(40) == 1);
    CHECK(t.erase(50) == 1);
    CHECK(t.erase(999) == 0);
    CHECK(t.validate());
    CHECK(keys_of(t) == std::vector<int>{30, 45, 60, 70, 80});

    auto next = t.erase(t.find(60));
    CHECK(next->first == 70);
    CHECK(t.validate());

    std::ostringstream os;
    os << t;
    CHECK(os.str() == "{30: 30, 45: y, 70: 70, 80: 80}");

    t.clear();
    CHECK(t.empty());
    CHECK(t.validate());
}

TEST_CASE_TEMPLATE("ordered map: copy and move", Tree, BinarySearchTree<int, Tracked>, AVLTree<int, Tracked>) {
    test::LiveGuard<Tracked> guard;
    {
        Tree a;
        for (int i = 0; i < 200; ++i) a.insert((i * 37) % 200, Tracked(i));
        Tree b = a;
        CHECK(a == b);
        CHECK(preorder_keys(a) == preorder_keys(b));  // same shape, not just same keys
        CHECK(b.validate());
        b.erase(5);
        CHECK(a != b);
        Tree c = std::move(b);
        CHECK(b.empty());  // NOLINT
        CHECK(c.size() == 199);
        a = c;
        CHECK(a == c);
        a = std::move(c);
        CHECK(a.size() == 199);
        auto& self = a;
        a = self;  // self-assignment
        CHECK(a.validate());
    }
    CHECK(guard.leaked() == 0);
}

TEST_CASE("ordered map: a failed copy frees what it built") {
    test::LiveGuard<Tracked> guard;
    AVLTree<int, Tracked> a;
    for (int i = 0; i < 100; ++i) a.insert(i, Tracked(i));
    Tracked::countdown = 60;
    CHECK_THROWS_AS((AVLTree<int, Tracked>(a)), test::Boom);
    Tracked::countdown = -1;
    a.clear();
    CHECK(guard.leaked() == 0);
}

TEST_CASE("BinarySearchTree: sorted input degenerates, and deep trees are handled without recursion") {
    BinarySearchTree<int, int> t;
    const int n = 200000;
    for (int i = 0; i < n; ++i) t.insert(i, i);
    CHECK(t.height() == static_cast<std::size_t>(n));  // a linked list
    auto copy = t;                                     // iterative clone
    CHECK(copy.size() == static_cast<std::size_t>(n));
    CHECK(copy.height() == static_cast<std::size_t>(n));
    CHECK(copy.validate());
    // destructors run here: iterative too
}

TEST_CASE("AVLTree: sorted input stays balanced") {
    AVLTree<int, int> t;
    const int n = 200000;
    for (int i = 0; i < n; ++i) t.insert(i, i);
    CHECK(t.validate());
    CHECK(t.height() <= static_cast<std::size_t>(1.4405 * std::log2(n + 2)));
    CHECK(t.height() == 18);  // perfectly balanced for 2^18 - 1 > n > 2^17
    for (int i = 0; i < n; i += 2) t.erase(i);
    CHECK(t.validate());
    CHECK(t.size() == static_cast<std::size_t>(n / 2));
}

TEST_CASE("AVLTree: the four rotation cases") {
    struct Case {
        std::vector<int> insert;
        std::vector<int> expected_preorder;
        std::size_t rotations;
    };
    const Case cases[] = {
        {{1, 2, 3}, {2, 1, 3}, 1},  // right-right: single left rotation
        {{3, 2, 1}, {2, 1, 3}, 1},  // left-left: single right rotation
        {{1, 3, 2}, {2, 1, 3}, 2},  // right-left: double rotation
        {{3, 1, 2}, {2, 1, 3}, 2},  // left-right: double rotation
    };
    for (const auto& c : cases) {
        AVLTree<int, int> t;
        for (int k : c.insert) t.insert(k, k);
        CHECK(preorder_keys(t) == c.expected_preorder);
        CHECK(t.rotations() == c.rotations);
        CHECK(t.validate());
    }
}

TEST_CASE("AVLTree: print_shape") {
    AVLTree<int, int> t;
    for (int i = 1; i <= 7; ++i) t.insert(i, i);
    std::ostringstream os;
    t.print_shape(os);
    CHECK(os.str() ==
          "    ┌── 7\n"
          "┌── 6\n"
          "│   └── 5\n"
          "4\n"
          "│   ┌── 3\n"
          "└── 2\n"
          "    └── 1\n");
    std::ostringstream e;
    AVLTree<int, int>().print_shape(e);
    CHECK(e.str() == "(empty)\n");
}

TEST_CASE("ordered map: custom comparator") {
    AVLTree<std::string, int, std::greater<>> t;
    for (const char* s : {"b", "a", "d", "c"}) t.insert(s, 0);
    std::vector<std::string> order;
    for (const auto& kv : t) order.push_back(kv.first);
    CHECK(order == std::vector<std::string>{"d", "c", "b", "a"});
    CHECK(t.validate());
}

TEST_CASE_TEMPLATE("ordered map: randomized operations match std::map", Tree, BinarySearchTree<int, int>,
                   AVLTree<int, int>) {
    for (unsigned seed = 1; seed <= 20; ++seed) {
        auto g = test::rng(seed);
        Tree t;
        std::map<int, int> m;
        const int key_space = test::rand_int(g, 10, 2000);
        for (int step = 0; step < 4000; ++step) {
            const int op = test::rand_int(g, 0, 9);
            const int k = test::rand_int(g, 0, key_space);
            if (op <= 3) {
                const bool a = t.insert(k, step).second;
                const bool b = m.insert({k, step}).second;
                REQUIRE(a == b);
            } else if (op <= 5) {
                REQUIRE(t.erase(k) == m.erase(k));
            } else if (op == 6) {
                auto it = t.find(k);
                auto jt = m.find(k);
                REQUIRE((it == t.end()) == (jt == m.end()));
                if (jt != m.end()) REQUIRE(it->second == jt->second);
            } else if (op == 7) {
                auto it = t.lower_bound(k);
                auto jt = m.lower_bound(k);
                REQUIRE((it == t.end()) == (jt == m.end()));
                if (jt != m.end()) REQUIRE(it->first == jt->first);
            } else if (op == 8) {
                t[k] += 1;
                m[k] += 1;
            } else if (op == 9 && !m.empty()) {
                auto it = t.lower_bound(k);
                auto jt = m.lower_bound(k);
                if (jt != m.end()) {
                    it = t.erase(it);
                    jt = m.erase(jt);
                    REQUIRE((it == t.end()) == (jt == m.end()));
                }
            }
            REQUIRE(t.size() == m.size());
            if (step % 500 == 0) REQUIRE(t.validate());
        }
        CHECK(t.validate());
        CHECK(std::equal(t.begin(), t.end(), m.begin(), m.end(),
                         [](const auto& a, const auto& b) { return a.first == b.first && a.second == b.second; }));
    }
}

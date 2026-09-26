// dsl-demo: a small command-line tour of the library.
//
//   dsl-demo avl [KEY...]      insert keys into an AVL tree, print its shape after each one
//   dsl-demo sorted N          insert 1..N into a plain BST and an AVL tree, compare heights
//   dsl-demo hashmap [N]       insert N keys into the hash map, report every rehash
//   dsl-demo containers        one line per container: build it, print it with operator<<
//   dsl-demo                   all of the above with default arguments

#include <dsl/dsl.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void avl(const std::vector<int>& keys) {
    std::cout << "== AVL tree: insert " << keys.size() << " keys, shape after each insert\n"
              << "   (drawn sideways: root on the left, larger keys above)\n";
    dsl::AVLTree<int, int> t;
    for (int k : keys) {
        const auto before = t.rotations();
        t.insert(k, k);
        const auto r = t.rotations() - before;
        std::cout << "\ninsert " << k << ": ";
        if (r == 0) std::cout << "no rotation";
        else if (r == 1) std::cout << "1 rotation (single)";
        else std::cout << r << " rotations (double)";
        std::cout << ", height " << t.height() << ", valid AVL: " << (t.validate() ? "yes" : "NO") << '\n';
        t.print_shape(std::cout);
    }
    std::cout << "\nin order: " << t << '\n';
}

void sorted(int n) {
    std::cout << "\n== insert 1.." << n << " in sorted order\n";
    dsl::BinarySearchTree<int, int> bst;
    dsl::AVLTree<int, int> tree;
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    for (int i = 1; i <= n; ++i) bst.insert(i, i);
    auto t1 = clock::now();
    for (int i = 1; i <= n; ++i) tree.insert(i, i);
    auto t2 = clock::now();
    auto ms = [](auto d) { return std::chrono::duration<double, std::milli>(d).count(); };
    std::printf("BinarySearchTree: height %6zu  (%.1f ms)\n", bst.height(), ms(t1 - t0));
    std::printf("AVLTree:          height %6zu  (%.1f ms, %zu rotations)\n", tree.height(), ms(t2 - t1),
                tree.rotations());
}

void hashmap(int n) {
    std::cout << "\n== HashMap: insert " << n << " keys, max load factor " << dsl::HashMap<int, int>().max_load_factor()
              << "\n";
    dsl::HashMap<int, int> m;
    std::size_t buckets = m.bucket_count();
    for (int i = 0; i < n; ++i) {
        m[i * 7] = i;
        if (m.bucket_count() != buckets) {
            std::printf("insert #%-6d rehash %5zu -> %5zu buckets   load now %.3f   longest probe %zu\n", i + 1,
                        buckets, m.bucket_count(), static_cast<double>(m.load_factor()), m.max_probe_length());
            buckets = m.bucket_count();
        }
    }
    std::printf("final: %zu keys in %zu buckets, load %.3f, longest probe %zu\n", m.size(), m.bucket_count(),
                static_cast<double>(m.load_factor()), m.max_probe_length());
    std::size_t removed = 0;
    for (int i = 0; i < n; i += 2) removed += m.erase(i * 7);
    std::printf("erased %zu keys (backward shift, no tombstones): %zu left, longest probe %zu\n", removed, m.size(),
                m.max_probe_length());
    bool ok = true;
    for (int i = 0; i < n; ++i) ok = ok && (m.contains(i * 7) == (i % 2 == 1));
    std::printf("every remaining key found, every erased key gone: %s\n", ok ? "yes" : "NO");
}

void containers() {
    std::cout << "\n== each container, printed with operator<<\n";
    dsl::DynamicArray<int> a{5, 3, 8};
    a.push_back(1);
    a.insert(a.begin(), 9);
    std::cout << "DynamicArray      " << a << "  size " << a.size() << " capacity " << a.capacity() << '\n';

    dsl::SinglyLinkedList<int> s{1, 2, 3};
    s.push_front(0);
    s.reverse();
    std::cout << "SinglyLinkedList  " << s << "  (reversed)\n";

    dsl::DoublyLinkedList<int> d{4, 1, 3, 2};
    d.sort();
    std::cout << "DoublyLinkedList  " << d << "  (merge sorted)\n";

    dsl::CircularBuffer<int> c;
    c.reserve(4);
    for (int i = 1; i <= 7; ++i) c.push_back_overwrite(i);
    std::cout << "CircularBuffer    " << c << "  (capacity 4, pushed 1..7)\n";

    dsl::Stack<std::string> st;
    for (const char* w : {"a", "b", "c"}) st.push(w);
    std::cout << "Stack             " << st << "  top " << st.top() << '\n';

    dsl::Queue<int> q;
    for (int i = 1; i <= 4; ++i) q.push(i);
    q.pop();
    std::cout << "Queue             " << q << "  front " << q.front() << '\n';

    dsl::PriorityQueue<int> pq{3, 1, 4, 1, 5, 9, 2, 6};
    std::cout << "PriorityQueue     top " << pq.top() << ", popped in order:";
    while (!pq.empty()) std::cout << ' ' << pq.extract_top();
    std::cout << '\n';

    dsl::HashMap<std::string, int> h{{"x", 1}};
    h["y"] = 2;
    std::cout << "HashMap           size " << h.size() << ", h[\"y\"] = " << h.at("y") << '\n';

    dsl::AVLTree<std::string, int> t{{"pear", 3}, {"apple", 1}, {"fig", 2}};
    std::cout << "AVLTree           " << t << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    const std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "avl") {
        std::vector<int> keys;
        for (int i = 2; i < argc; ++i) keys.push_back(std::atoi(argv[i]));
        if (keys.empty()) keys = {10, 20, 30, 40, 50, 25};
        avl(keys);
    } else if (cmd == "sorted") {
        sorted(argc > 2 ? std::atoi(argv[2]) : 10000);
    } else if (cmd == "hashmap") {
        hashmap(argc > 2 ? std::atoi(argv[2]) : 1000);
    } else if (cmd == "containers") {
        containers();
    } else if (cmd.empty()) {
        avl({10, 20, 30, 40, 50, 25});
        sorted(10000);
        hashmap(1000);
        containers();
    } else {
        std::fprintf(stderr, "usage: %s [avl KEY... | sorted N | hashmap N | containers]\n", argv[0]);
        return 2;
    }
    return 0;
}

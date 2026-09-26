// Benchmarks: each dsl container against its standard-library counterpart.
//
// Every case builds its input outside the timed region, runs the dsl and the
// std version alternately (so both see the same machine state), and reports
// the median over several repetitions as nanoseconds per element.
//
//   build/bench            full run: n = 1e3, 1e4, 1e5, 1e6
//   build/bench --quick    n = 1e3 and 1e5, fewer repetitions
//   build/bench --filter hash   only groups whose name contains "hash"

#include <dsl/dsl.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <forward_list>
#include <functional>
#include <list>
#include <map>
#include <numeric>
#include <queue>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using u64 = std::uint64_t;
using Clock = std::chrono::steady_clock;

// Keeps the optimizer from deleting work whose result is otherwise unused.
template <class T>
inline void keep(const T& v) {
    asm volatile("" : : "r,m"(v) : "memory");
}

struct Timer {
    Clock::time_point t0 = Clock::now();
    double ns() const { return std::chrono::duration<double, std::nano>(Clock::now() - t0).count(); }
};

struct Options {
    std::vector<std::size_t> sizes{1000, 10000, 100000, 1000000};
    int min_reps = 5;
    double budget_ns = 3e8;  // soft time cap per (case, size) measurement
    std::string filter;
} opt;

struct Row {
    std::string group, op;
    std::size_t n;
    double dsl, std_;
};
std::vector<Row> rows;

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// Runs `dsl_run` and `std_run` alternately. Each returns the elapsed ns of
// its timed region. Results are ns per element (divided by `ops`).
template <class A, class B>
void measure(const char* group, const char* op, std::size_t n, std::size_t ops, A dsl_run, B std_run) {
    std::vector<double> a, b;
    const int reps = std::max(opt.min_reps, static_cast<int>(std::min<std::size_t>(51, 200000 / n)));
    double spent = 0;
    for (int r = 0; r < reps; ++r) {
        const double x = dsl_run();
        const double y = std_run();
        a.push_back(x / static_cast<double>(ops));
        b.push_back(y / static_cast<double>(ops));
        spent += x + y;
        if (r + 1 >= 3 && spent > opt.budget_ns) break;
    }
    rows.push_back({group, op, n, median(a), median(b)});
    std::fprintf(stderr, "  %-22s %-16s n=%-8zu dsl %9.2f  std %9.2f ns/op\n", group, op, n, rows.back().dsl,
                 rows.back().std_);
}

std::vector<u64> random_keys(std::size_t n, u64 seed) {
    std::mt19937_64 g(seed);
    std::vector<u64> v(n);
    for (auto& x : v) x = g();
    return v;
}
std::vector<u64> shuffled(std::vector<u64> v, u64 seed) {
    std::shuffle(v.begin(), v.end(), std::mt19937_64(seed));
    return v;
}

// The same loops behind a call boundary, as they run when the container is a
// member or a function parameter rather than a local the optimizer can see
// all of. See the README: the gap between the two variants is inlining.
template <class C>
[[gnu::noinline]] void fill_back(C& c, std::size_t n) {
    for (u64 i = 0; i < n; ++i) c.push_back(i);
}
template <class Q>
[[gnu::noinline]] u64 cycle_queue(Q& q, std::size_t n) {
    u64 s = 0;
    for (u64 i = 0; i < n; ++i) {
        q.push(i);
        s += q.front();
        q.pop();
    }
    return s;
}

bool enabled(const char* group) { return opt.filter.empty() || std::strstr(group, opt.filter.c_str()); }

// ---------------------------------------------------------------------------

void bench_array(std::size_t n) {
    const char* g = "DynamicArray / vector";
    if (!enabled(g)) return;
    measure(g, "push_back", n, n,
            [&] {
                Timer t;
                dsl::DynamicArray<u64> a;
                for (u64 i = 0; i < n; ++i) a.push_back(i);
                keep(a.data());
                return t.ns();
            },
            [&] {
                Timer t;
                std::vector<u64> a;
                for (u64 i = 0; i < n; ++i) a.push_back(i);
                keep(a.data());
                return t.ns();
            });
    measure(g, "push_back (by ref)", n, n,
            [&] {
                Timer t;
                dsl::DynamicArray<u64> a;
                fill_back(a, n);
                keep(a.data());
                return t.ns();
            },
            [&] {
                Timer t;
                std::vector<u64> a;
                fill_back(a, n);
                keep(a.data());
                return t.ns();
            });
    dsl::DynamicArray<u64> a;
    std::vector<u64> v;
    for (u64 i = 0; i < n; ++i) a.push_back(i), v.push_back(i);
    measure(g, "iterate", n, n,
            [&] {
                Timer t;
                u64 s = 0;
                for (u64 x : a) s += x;
                keep(s);
                return t.ns();
            },
            [&] {
                Timer t;
                u64 s = 0;
                for (u64 x : v) s += x;
                keep(s);
                return t.ns();
            });
    if (n <= 100000) {
        // insert at the front: O(n) each, so n/10 inserts into a full array
        const std::size_t k = std::max<std::size_t>(n / 10, 1);
        measure(g, "insert front", n, k,
                [&] {
                    dsl::DynamicArray<u64> b(a);
                    Timer t;
                    for (std::size_t i = 0; i < k; ++i) b.insert(b.begin(), i);
                    keep(b.data());
                    return t.ns();
                },
                [&] {
                    std::vector<u64> b(v);
                    Timer t;
                    for (std::size_t i = 0; i < k; ++i) b.insert(b.begin(), i);
                    keep(b.data());
                    return t.ns();
                });
    }
}

void bench_lists(std::size_t n) {
    const char* g = "DoublyLinkedList / list";
    if (enabled(g)) {
        measure(g, "push_back", n, n,
                [&] {
                    Timer t;
                    dsl::DoublyLinkedList<u64> l;
                    for (u64 i = 0; i < n; ++i) l.push_back(i);
                    keep(l.size());
                    return t.ns();
                },
                [&] {
                    Timer t;
                    std::list<u64> l;
                    for (u64 i = 0; i < n; ++i) l.push_back(i);
                    keep(l.size());
                    return t.ns();
                });
        const auto keys = random_keys(n, 1);
        dsl::DoublyLinkedList<u64> dl(keys.begin(), keys.end());
        std::list<u64> sl(keys.begin(), keys.end());
        measure(g, "iterate", n, n,
                [&] {
                    Timer t;
                    u64 s = 0;
                    for (u64 x : dl) s += x;
                    keep(s);
                    return t.ns();
                },
                [&] {
                    Timer t;
                    u64 s = 0;
                    for (u64 x : sl) s += x;
                    keep(s);
                    return t.ns();
                });
        measure(g, "erase every 2nd", n, n / 2,
                [&] {
                    auto l = dl;
                    Timer t;
                    for (auto it = l.begin(); it != l.end();) {
                        it = l.erase(it);
                        if (it != l.end()) ++it;
                    }
                    keep(l.size());
                    return t.ns();
                },
                [&] {
                    auto l = sl;
                    Timer t;
                    for (auto it = l.begin(); it != l.end();) {
                        it = l.erase(it);
                        if (it != l.end()) ++it;
                    }
                    keep(l.size());
                    return t.ns();
                });
        measure(g, "sort", n, n,
                [&] {
                    auto l = dl;
                    Timer t;
                    l.sort();
                    keep(l.front());
                    return t.ns();
                },
                [&] {
                    auto l = sl;
                    Timer t;
                    l.sort();
                    keep(l.front());
                    return t.ns();
                });
    }
    const char* g2 = "SinglyLinkedList / forward_list";
    if (enabled(g2)) {
        measure(g2, "push_front", n, n,
                [&] {
                    Timer t;
                    dsl::SinglyLinkedList<u64> l;
                    for (u64 i = 0; i < n; ++i) l.push_front(i);
                    keep(l.size());
                    return t.ns();
                },
                [&] {
                    Timer t;
                    std::forward_list<u64> l;
                    for (u64 i = 0; i < n; ++i) l.push_front(i);
                    keep(l.front());
                    return t.ns();
                });
    }
}

template <class Tree>
void bench_ordered(const char* g, std::size_t n, bool sorted_case) {
    if (!enabled(g)) return;
    const auto keys = random_keys(n, 2);
    const auto probe = shuffled(keys, 3);
    measure(g, "insert random", n, n,
            [&] {
                Timer t;
                Tree m;
                for (u64 k : keys) m.insert(k, k);
                keep(m.size());
                return t.ns();
            },
            [&] {
                Timer t;
                std::map<u64, u64> m;
                for (u64 k : keys) m.emplace(k, k);
                keep(m.size());
                return t.ns();
            });
    Tree tm;
    std::map<u64, u64> sm;
    for (u64 k : keys) tm.insert(k, k), sm.emplace(k, k);
    measure(g, "find (hit)", n, n,
            [&] {
                Timer t;
                u64 s = 0;
                for (u64 k : probe) s += tm.find(k)->second;
                keep(s);
                return t.ns();
            },
            [&] {
                Timer t;
                u64 s = 0;
                for (u64 k : probe) s += sm.find(k)->second;
                keep(s);
                return t.ns();
            });
    measure(g, "iterate", n, n,
            [&] {
                Timer t;
                u64 s = 0;
                for (const auto& kv : tm) s += kv.second;
                keep(s);
                return t.ns();
            },
            [&] {
                Timer t;
                u64 s = 0;
                for (const auto& kv : sm) s += kv.second;
                keep(s);
                return t.ns();
            });
    measure(g, "erase all", n, n,
            [&] {
                Tree m = tm;
                Timer t;
                for (u64 k : probe) m.erase(k);
                keep(m.size());
                return t.ns();
            },
            [&] {
                auto m = sm;
                Timer t;
                for (u64 k : probe) m.erase(k);
                keep(m.size());
                return t.ns();
            });
    if (sorted_case) {
        measure(g, "insert sorted", n, n,
                [&] {
                    Timer t;
                    Tree m;
                    for (u64 i = 0; i < n; ++i) m.insert(i, i);
                    keep(m.size());
                    return t.ns();
                },
                [&] {
                    Timer t;
                    std::map<u64, u64> m;
                    for (u64 i = 0; i < n; ++i) m.emplace(i, i);
                    keep(m.size());
                    return t.ns();
                });
    }
}

void bench_hash(std::size_t n) {
    const char* g = "HashMap / unordered_map";
    if (!enabled(g)) return;
    const auto keys = random_keys(n, 4);
    const auto probe = shuffled(keys, 5);
    const auto misses = random_keys(n, 6);
    measure(g, "insert", n, n,
            [&] {
                Timer t;
                dsl::HashMap<u64, u64> m;
                for (u64 k : keys) m.insert(k, k);
                keep(m.size());
                return t.ns();
            },
            [&] {
                Timer t;
                std::unordered_map<u64, u64> m;
                for (u64 k : keys) m.emplace(k, k);
                keep(m.size());
                return t.ns();
            });
    measure(g, "insert (reserved)", n, n,
            [&] {
                Timer t;
                dsl::HashMap<u64, u64> m;
                m.reserve(n);
                for (u64 k : keys) m.insert(k, k);
                keep(m.size());
                return t.ns();
            },
            [&] {
                Timer t;
                std::unordered_map<u64, u64> m;
                m.reserve(n);
                for (u64 k : keys) m.emplace(k, k);
                keep(m.size());
                return t.ns();
            });
    dsl::HashMap<u64, u64> hm;
    std::unordered_map<u64, u64> um;
    for (u64 k : keys) hm.insert(k, k), um.emplace(k, k);
    measure(g, "find (hit)", n, n,
            [&] {
                Timer t;
                u64 s = 0;
                for (u64 k : probe) s += hm.find(k)->second;
                keep(s);
                return t.ns();
            },
            [&] {
                Timer t;
                u64 s = 0;
                for (u64 k : probe) s += um.find(k)->second;
                keep(s);
                return t.ns();
            });
    measure(g, "find (miss)", n, n,
            [&] {
                Timer t;
                u64 s = 0;
                for (u64 k : misses) s += hm.contains(k);
                keep(s);
                return t.ns();
            },
            [&] {
                Timer t;
                u64 s = 0;
                for (u64 k : misses) s += um.contains(k);
                keep(s);
                return t.ns();
            });
    measure(g, "iterate", n, n,
            [&] {
                Timer t;
                u64 s = 0;
                for (const auto& kv : hm) s += kv.second;
                keep(s);
                return t.ns();
            },
            [&] {
                Timer t;
                u64 s = 0;
                for (const auto& kv : um) s += kv.second;
                keep(s);
                return t.ns();
            });
    measure(g, "erase all", n, n,
            [&] {
                auto m = hm;
                Timer t;
                for (u64 k : probe) m.erase(k);
                keep(m.size());
                return t.ns();
            },
            [&] {
                auto m = um;
                Timer t;
                for (u64 k : probe) m.erase(k);
                keep(m.size());
                return t.ns();
            });
}

void bench_heap(std::size_t n) {
    const char* g = "BinaryHeap / priority_queue";
    if (!enabled(g)) return;
    const auto keys = random_keys(n, 7);
    measure(g, "push", n, n,
            [&] {
                Timer t;
                dsl::BinaryHeap<u64> h;
                for (u64 k : keys) h.push(k);
                keep(h.top());
                return t.ns();
            },
            [&] {
                Timer t;
                std::priority_queue<u64> h;
                for (u64 k : keys) h.push(k);
                keep(h.top());
                return t.ns();
            });
    measure(g, "pop all", n, n,
            [&] {
                dsl::BinaryHeap<u64> h(keys.begin(), keys.end());
                Timer t;
                u64 s = 0;
                while (!h.empty()) {
                    s += h.top();
                    h.pop();
                }
                keep(s);
                return t.ns();
            },
            [&] {
                std::priority_queue<u64> h(keys.begin(), keys.end());
                Timer t;
                u64 s = 0;
                while (!h.empty()) {
                    s += h.top();
                    h.pop();
                }
                keep(s);
                return t.ns();
            });
    measure(g, "heapify", n, n,
            [&] {
                Timer t;
                dsl::BinaryHeap<u64> h(keys.begin(), keys.end());
                keep(h.top());
                return t.ns();
            },
            [&] {
                Timer t;
                std::priority_queue<u64> h(keys.begin(), keys.end());
                keep(h.top());
                return t.ns();
            });
}

void bench_queue(std::size_t n) {
    const char* g = "Queue / std::queue";
    if (!enabled(g)) return;
    // Steady state: a queue of ~1000 elements, n push+pop pairs.
    measure(g, "push+pop", n, n,
            [&] {
                dsl::Queue<u64> q;
                for (u64 i = 0; i < 1000; ++i) q.push(i);
                Timer t;
                u64 s = 0;
                for (u64 i = 0; i < n; ++i) {
                    q.push(i);
                    s += q.front();
                    q.pop();
                }
                keep(s);
                return t.ns();
            },
            [&] {
                std::queue<u64> q;
                for (u64 i = 0; i < 1000; ++i) q.push(i);
                Timer t;
                u64 s = 0;
                for (u64 i = 0; i < n; ++i) {
                    q.push(i);
                    s += q.front();
                    q.pop();
                }
                keep(s);
                return t.ns();
            });
    measure(g, "push+pop (by ref)", n, n,
            [&] {
                dsl::Queue<u64> q;
                for (u64 i = 0; i < 1000; ++i) q.push(i);
                Timer t;
                keep(cycle_queue(q, n));
                return t.ns();
            },
            [&] {
                std::queue<u64> q;
                for (u64 i = 0; i < 1000; ++i) q.push(i);
                Timer t;
                keep(cycle_queue(q, n));
                return t.ns();
            });
}

std::string human(std::size_t n) {
    if (n >= 1000000 && n % 1000000 == 0) return std::to_string(n / 1000000) + "M";
    if (n >= 1000 && n % 1000 == 0) return std::to_string(n / 1000) + "k";
    return std::to_string(n);
}

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, v < 10 ? "%.2f" : "%.1f", v);
    return buf;
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--quick")) {
            opt.sizes = {1000, 100000};
            opt.min_reps = 3;
            opt.budget_ns = 1e8;
        } else if (!std::strcmp(argv[i], "--filter") && i + 1 < argc) {
            opt.filter = argv[++i];
        } else {
            std::fprintf(stderr, "usage: %s [--quick] [--filter GROUP]\n", argv[0]);
            return 2;
        }
    }

    for (std::size_t n : opt.sizes) {
        std::fprintf(stderr, "n = %zu\n", n);
        bench_array(n);
        bench_lists(n);
        bench_ordered<dsl::AVLTree<u64, u64>>("AVLTree / map", n, true);
        bench_ordered<dsl::BinarySearchTree<u64, u64>>("BinarySearchTree / map", n, n <= 10000);
        bench_hash(n);
        bench_heap(n);
        bench_queue(n);
    }

    // Markdown table: one row per (structure, operation), one column per size.
    // Each cell is "dsl / std" in ns per element, with the ratio underneath.
    std::vector<std::pair<std::string, std::string>> keys;
    for (const auto& r : rows) {
        std::pair<std::string, std::string> k{r.group, r.op};
        if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
    }
    std::stable_sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::printf("| structure | operation |");
    for (std::size_t n : opt.sizes) std::printf(" n = %s |", human(n).c_str());
    std::printf("\n|---|---|");
    for (std::size_t i = 0; i < opt.sizes.size(); ++i) std::printf("--:|");
    std::printf("\n");
    std::string last_group;
    for (const auto& [group, op] : keys) {
        std::printf("| %s | %s |", group == last_group ? "" : group.c_str(), op.c_str());
        last_group = group;
        for (std::size_t n : opt.sizes) {
            auto it = std::find_if(rows.begin(), rows.end(),
                                   [&](const Row& r) { return r.group == group && r.op == op && r.n == n; });
            if (it == rows.end()) std::printf(" |");
            else std::printf(" %s / %s (%.2fx) |", num(it->dsl).c_str(), num(it->std_).c_str(), it->dsl / it->std_);
        }
        std::printf("\n");
    }
    return 0;
}

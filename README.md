# cpp-data-structures

Rebuilt from scratch in 2026. The original 2023 project code was not preserved.

A header-only C++20 library of classic data structures in namespace `dsl`, written with
manual memory management (RAII, the rule of five, allocator-aware containers), tested
against the standard library containers under sanitizers, and benchmarked against them.

Recorded demo (build, tests, sanitizers, leak check, the demo program and a benchmark run):
https://project-demos-gamma.vercel.app/data-structures/

```sh
make test     # 64 doctest cases
make asan     # the same tests under AddressSanitizer + UndefinedBehaviorSanitizer
make tsan     # the same tests under ThreadSanitizer
make leaks    # macOS: the tests under leaks(1)
make bench    # benchmarks against std:: (-O2)
make demo && build/dsl-demo
```

Requirements: a C++20 compiler (tested with Apple clang 21 on macOS, and g++ and clang++
on Ubuntu 24.04 in CI) and `make`. The only third-party code is
[doctest](https://github.com/doctest/doctest) 2.4.12, vendored in `third_party/` for the tests.

## What is in it

| Header | Type | Counterpart |
|---|---|---|
| `dynamic_array.hpp` | `DynamicArray<T, Alloc, Growth>`: contiguous growable array | `std::vector` |
| `singly_linked_list.hpp` | `SinglyLinkedList<T, Alloc>`: with a tail pointer, so `push_back` is O(1) | `std::forward_list` |
| `doubly_linked_list.hpp` | `DoublyLinkedList<T, Alloc>`: circular, sentinel node, merge sort | `std::list` |
| `circular_buffer.hpp` | `CircularBuffer<T, Alloc>`: ring buffer, growable or fixed-size with overwrite | `std::deque` (partly) |
| `stack.hpp` | `Stack<T, Container = DynamicArray<T>>`: adapter | `std::stack` |
| `queue.hpp` | `Queue<T, Container = CircularBuffer<T>>`: adapter | `std::queue` |
| `binary_search_tree.hpp` | `BinarySearchTree<K, V, Compare>`: unbalanced, ordered map | `std::map` |
| `avl_tree.hpp` | `AVLTree<K, V, Compare>`: self-balancing ordered map | `std::map` |
| `hash_map.hpp` | `HashMap<K, V, Hash, KeyEqual, Alloc>`: open addressing, Robin Hood probing | `std::unordered_map` |
| `binary_heap.hpp` | `BinaryHeap<T, Compare, Container>` and the alias `PriorityQueue` | `std::priority_queue` |

`#include <dsl/dsl.hpp>` pulls in all of them.

## API overview

The interfaces follow the standard containers where there is one, so the names are the
familiar ones (`push_back`, `emplace`, `find`, `erase`, `try_emplace`, `insert_or_assign`,
`lower_bound`, `reserve`, ...). Every iterable container has iterators that work with
`<algorithm>`, `operator==` and `operator<<`.

```cpp
#include <dsl/dsl.hpp>

dsl::DynamicArray<int> a{3, 1, 2};
a.push_back(4);
std::sort(a.begin(), a.end());          // iterators are raw pointers
std::cout << a << '\n';                 // [1, 2, 3, 4]

dsl::DoublyLinkedList<std::string> l{"b", "a"};
l.sort();                               // stable merge sort, relinks nodes
l.splice(l.end(), other_list);          // O(1)

dsl::CircularBuffer<int> last3;
last3.reserve(3);
for (int i = 1; i <= 10; ++i) last3.push_back_overwrite(i);   // [8, 9, 10]

dsl::AVLTree<int, std::string> t;
t.insert(2, "two");
t[1] = "one";
t.print_shape(std::cout);               // draws the tree sideways
auto it = t.lower_bound(2);             // ordered iteration, both directions

dsl::HashMap<std::string, int> h;
h["x"] += 1;
h.try_emplace("y", 2);
auto f = h.find("x");
f.value() = 10;                         // f->first and f->second are read-only
std::cout << h.bucket_count() << ' ' << h.max_probe_length() << '\n';

dsl::PriorityQueue<int, std::greater<int>> pq{5, 1, 3};   // min-heap
int smallest = pq.extract_top();
```

`build/dsl-demo` (source in `examples/dsl_demo.cpp`) shows each structure in action: the
AVL tree's shape after every insert, a sorted-input comparison of the plain BST and the
AVL tree, and every rehash of the hash map.

## Complexity

n is the number of elements, h the height of the tree.

| Structure | Access / lookup | Insert | Erase | Notes |
|---|---|---|---|---|
| DynamicArray | O(1) index | O(1) amortized at the end, O(n) elsewhere | O(1) at the end, O(n) elsewhere | growth factor 2 (or 1.5 with `growth::OneAndHalf`) |
| SinglyLinkedList | O(n) | O(1) front, back, or after an iterator | O(1) front or after an iterator | `reverse` O(n) |
| DoublyLinkedList | O(n) | O(1) anywhere given an iterator | O(1) given an iterator | `sort` O(n log n), `splice` O(1) |
| CircularBuffer | O(1) index | O(1) amortized at either end | O(1) at either end | `push_back_overwrite` O(1), never allocates |
| Stack, Queue | O(1) top / front | O(1) amortized | O(1) | adapters over the containers above |
| BinarySearchTree | O(h) | O(h) | O(h) | h = O(log n) on random input, O(n) on sorted input |
| AVLTree | O(log n) | O(log n) | O(log n) | h < 1.44 log2(n + 2) |
| HashMap | O(1) expected | O(1) expected, amortized | O(1) expected | max load factor 0.875, capacity doubles |
| BinaryHeap | O(1) top | O(log n) | O(log n) pop | build from a range O(n) |

In-order iteration of the trees is O(n) in total (O(h) for a single step in the worst case).

## Design notes

**Memory management.** Every container owns its memory through RAII and implements all
five special members. Allocation goes through `std::allocator_traits`, so custom allocators
work in `DynamicArray`, both lists, `CircularBuffer` and `HashMap` (node containers rebind
the allocator to their node type). The `propagate_on_container_{copy,move}_assignment` and
`propagate_on_container_swap` traits are honoured, and move assignment between unequal,
non-propagating allocators falls back to moving element by element. The tests use a
stateful counting allocator to check that every allocation is returned. The trees allocate
nodes with `new`/`delete` and are not allocator-aware.

**Exception safety.** `DynamicArray::push_back`/`emplace_back` give the strong guarantee:
the new element is constructed before anything else changes, and on reallocation elements
are moved only if their move constructor is `noexcept` (`std::move_if_noexcept`), otherwise
copied. If anything throws, the old buffer is untouched. The same holds for `CircularBuffer`
growth, list insertion, copy assignment everywhere (copy-and-swap), and insertion into the
trees. The tests check this with an element type that throws on the Nth copy.

**Inheritance, used where it fits.** There are three single-inheritance relationships:

- `ContainerBase<Derived>` (CRTP) is the common base of every iterable container. From
  `begin()`, `end()` and `size()` it provides `empty()`, `operator==` and `operator<<`. It has
  no virtual functions, so it adds no size and no indirect calls, and its destructor is
  protected. The hash map opts into set-style equality with `unordered_equality = true`.
- `BstBase<K, V, Compare, Derived>` holds the whole ordered-map implementation: lookup,
  iteration, CLRS insertion and deletion, copying and printing. `BinarySearchTree` and
  `AVLTree` derive from it, and the AVL tree only adds rotations through two hooks
  (`after_insert`, `after_erase`) that the base calls at compile time. The plain BST pays
  nothing for them.
- In the linked lists, value nodes derive from a value-less `NodeBase` that holds only the
  links. The list's head sentinel is a `NodeBase`, so an empty list allocates nothing and
  `T` does not need to be default-constructible.

**Operator overloading.** `==` (and `!=` through C++20 rewriting) and `<<` on all
containers, `[]` on the array, circular buffer and maps, and full iterator operator sets:
`*`, `->`, `++`, `--` and, for the ring buffer's random-access iterator, `+ - += -= [] <=>`.

**Trees.** Deletion relinks nodes instead of swapping values, so iterators and references to
other elements stay valid (the key is `const`, so values could not be swapped anyway). Copy,
destruction and `height()` are iterative and use parent pointers, so a degenerate BST with
200,000 nodes (a test case) is copied and destroyed without deep recursion. The AVL tree
stores subtree heights and stops rebalancing at the first node whose height did not change.
That early exit made sorted insertion about 2x faster than walking to the root every time.

**Hash map.** Open addressing with Robin Hood linear probing. Slots hold `std::pair<K, V>`,
and a parallel array holds 16-bit probe distances (0 means empty). A lookup stops as soon as
it meets an element closer to its home slot than the current probe distance, so misses are
cheap. Erase uses backward-shift deletion, so there are no tombstones. The home slot comes
from the high bits of `hash * 2^64/phi` (Fibonacci hashing), which spreads `std::hash<int>`
(the identity function in libc++ and libstdc++) across a power-of-two table. With the
identity hash and keys spaced 1024 apart, 100,000 keys have a longest probe below 32, and
the tests check this.

**Benchmark-driven changes.** Three changes came out of the benchmarks, each checked
against the previous numbers:

- Tree lookup was a three-way branch at every level (less, greater or equal). On random
  keys the branch predictor cannot guess that. Lookup is now a `lower_bound` descent with
  one comparison per level, then one equality check at the end, which is also how libc++
  does it. That brought `AVLTree::find` from 1.3x to 1.9x slower than `std::map` (10k to 1M,
  in a run under heavy load) to 8 to 20 percent faster.
- The AVL tree used to update heights all the way to the root after every change. It now
  stops at the first subtree whose height is unchanged. Sorted insertion got about 2x
  faster.
- `CircularBuffer::push_back` had its growth code inlined, which made it too large to
  inline into callers. The caller's argument was also passed by reference into the growth
  call, and that forced the caller's loop counter into memory. The growth paths
  (`CircularBuffer` and `DynamicArray`) are now out of line and receive a temporary built on
  the slow path. That took `Queue` push+pop from about 3.6x to about 1.6x slower than
  `std::queue` when the queue is a local variable. It is still slower (see below).

## Testing

64 doctest test cases (978,254 assertions) in `tests/`:

- Unit tests per structure, including edge cases: empty containers, a single element,
  wrap-around in the ring buffer, erasing the tail of the singly linked list, arguments that
  refer to an element of the same container (`a.push_back(a[0])` during reallocation),
  self-assignment, moved-from state, the four AVL rotation cases, and a hash function under
  which every key collides.
- Property-style randomized tests: for each structure, 20 seeds times thousands of random
  operations are applied to the dsl container and its `std::` counterpart, and the results
  are compared after every operation. Tree invariants (`validate()`: ordering, parent links,
  stored heights, AVL balance) are checked along the way.
- Exception-safety tests with an element type that throws on the Nth copy or move, checking
  that the container is unchanged and that no element leaked (live-instance counting).
- Allocator tests with a stateful counting allocator: allocations equal deallocations, and
  unequal-allocator move assignment works.
- Concurrency tests: four threads read one container at the same time (const member
  functions must not write), and four threads each build their own containers. These are
  there for ThreadSanitizer.

To check that the tests catch real bugs, I made three deliberate breaks: an AVL balance
threshold of 2, a hash map erase that stops shifting too early, and an unstable list merge.
Seven test cases failed.

Results on the machine below:

| Run | Result |
|---|---|
| `make test` | 64/64 test cases passed |
| `make asan` (`-fsanitize=address,undefined -fno-sanitize-recover=all`) | 64/64 passed, no reports |
| `make tsan` (`-fsanitize=thread`) | 64/64 passed, no reports |
| `make leaks` (macOS `leaks --atExit`) | `0 leaks for 0 total leaked bytes` |

LeakSanitizer does not run on Apple silicon (ASan reports `detect_leaks is not supported on
this platform`), so on macOS leaks are checked with `leaks(1)`. I confirmed it reports a
deliberately leaked allocation. On Linux, CI runs ASan with `detect_leaks=1`.

CI (`.github/workflows/ci.yml`) runs `test`, `asan` and `tsan` with both g++ and clang++ on
Ubuntu 24.04, with `-Werror`, and builds and smoke-runs the benchmark and the demo.

## Benchmarks

`bench/bench.cpp` is a small harness built on `std::chrono::steady_clock`. For each case it
builds the input outside the timed region, runs the dsl version and the std version
alternately, repeats (5 to 51 times, depending on n), and reports the median in
nanoseconds per element. Keys are random 64-bit integers from a fixed seed, and lookups use
a shuffled order.

- Machine: MacBook Air, Apple M5 (4 performance + 6 efficiency cores), 24 GB, macOS 26.5.2
- Compiler: Apple clang 21.0.0 (clang-2100.1.1.101), libc++, `-std=c++20 -O2 -DNDEBUG`
- Run on 2026-09-26 at 17:11 EDT (load average 1.7 at the start, 4.2 at the end, because
  other programs were running). Because dsl and std alternate, the ratios are more
  reliable than the absolute numbers. An earlier run under a load average of about 5 gave
  times about 3x higher.

Each cell is `dsl / std` in ns per element, followed by the ratio. Below 1.00x means dsl was faster.

| structure | operation | n = 1k | n = 10k | n = 100k | n = 1M |
|---|---|--:|--:|--:|--:|
| AVLTree / map | insert random | 20.0 / 16.8 (1.19x) | 59.0 / 52.1 (1.13x) | 101.2 / 91.2 (1.11x) | 929.6 / 894.5 (1.04x) |
|  | find (hit) | 15.1 / 10.9 (1.39x) | 25.9 / 28.3 (0.92x) | 49.2 / 56.4 (0.87x) | 399.7 / 501.5 (0.80x) |
|  | iterate | 1.96 / 2.04 (0.96x) | 4.65 / 4.17 (1.12x) | 10.4 / 10.2 (1.01x) | 150.9 / 145.0 (1.04x) |
|  | erase all | 29.8 / 33.2 (0.90x) | 62.1 / 64.9 (0.96x) | 99.3 / 112.7 (0.88x) | 885.7 / 1053.2 (0.84x) |
|  | insert sorted | 17.1 / 14.4 (1.19x) | 17.1 / 14.9 (1.15x) | 18.5 / 18.8 (0.98x) | 32.0 / 39.1 (0.82x) |
| BinaryHeap / priority_queue | push | 2.12 / 1.71 (1.24x) | 2.55 / 1.90 (1.35x) | 6.06 / 6.69 (0.91x) | 9.12 / 9.40 (0.97x) |
|  | pop all | 7.71 / 8.83 (0.87x) | 13.2 / 15.6 (0.84x) | 24.4 / 25.9 (0.94x) | 86.0 / 85.6 (1.00x) |
|  | heapify | 0.75 / 0.75 (1.00x) | 0.84 / 0.79 (1.07x) | 2.41 / 2.07 (1.16x) | 3.50 / 3.85 (0.91x) |
| BinarySearchTree / map | insert random | 11.4 / 14.9 (0.77x) | 41.1 / 49.3 (0.83x) | 77.5 / 87.3 (0.89x) | 660.7 / 623.6 (1.06x) |
|  | find (hit) | 21.1 / 10.1 (2.08x) | 51.3 / 28.1 (1.82x) | 100.5 / 56.1 (1.79x) | 863.1 / 353.9 (2.44x) |
|  | iterate | 1.71 / 1.79 (0.95x) | 4.21 / 3.81 (1.10x) | 13.0 / 11.0 (1.18x) | 100.3 / 92.3 (1.09x) |
|  | erase all | 25.4 / 23.8 (1.07x) | 67.0 / 66.1 (1.01x) | 116.4 / 112.0 (1.04x) | 722.8 / 665.5 (1.09x) |
|  | insert sorted | 377.2 / 12.9 (29.21x) | 7849.5 / 16.8 (467.23x) | | |
| DoublyLinkedList / list | push_back | 6.62 / 6.58 (1.01x) | 4.22 / 4.14 (1.02x) | 4.13 / 4.33 (0.95x) | 4.38 / 4.30 (1.02x) |
|  | iterate | 1.62 / 0.71 (2.30x) | 0.87 / 0.78 (1.12x) | 0.85 / 0.84 (1.02x) | 0.88 / 0.86 (1.03x) |
|  | erase every 2nd | 11.8 / 11.8 (1.01x) | 7.64 / 6.95 (1.10x) | 9.50 / 9.61 (0.99x) | 7.68 / 7.82 (0.98x) |
|  | sort | 13.8 / 13.2 (1.04x) | 40.3 / 39.0 (1.03x) | 66.3 / 61.4 (1.08x) | 154.8 / 130.5 (1.19x) |
| DynamicArray / vector | push_back | 1.83 / 0.88 (2.09x) | 1.10 / 0.44 (2.49x) | 1.24 / 0.36 (3.47x) | 1.18 / 0.36 (3.26x) |
|  | push_back (by ref) | 2.83 / 3.21 (0.88x) | 1.77 / 1.94 (0.91x) | 1.74 / 2.05 (0.85x) | 1.78 / 1.97 (0.91x) |
|  | iterate | 0.08 / 0.08 (1.00x) | 0.06 / 0.06 (1.00x) | 0.06 / 0.06 (1.03x) | 0.07 / 0.07 (1.04x) |
|  | insert front | 112.5 / 112.5 (1.00x) | 817.2 / 819.9 (1.00x) | 12287.0 / 12237.7 (1.00x) | |
| HashMap / unordered_map | insert | 10.2 / 11.1 (0.92x) | 18.6 / 15.9 (1.17x) | 29.6 / 22.6 (1.31x) | 67.3 / 169.4 (0.40x) |
|  | insert (reserved) | 3.25 / 8.79 (0.37x) | 3.78 / 11.0 (0.34x) | 11.4 / 14.6 (0.79x) | 21.2 / 69.4 (0.30x) |
|  | find (hit) | 1.08 / 0.75 (1.45x) | 1.62 / 1.30 (1.24x) | 10.5 / 6.98 (1.50x) | 10.6 / 24.1 (0.44x) |
|  | find (miss) | 1.21 / 1.29 (0.93x) | 2.58 / 2.92 (0.88x) | 10.8 / 12.3 (0.88x) | 12.6 / 29.2 (0.43x) |
|  | iterate | 1.00 / 0.71 (1.41x) | 0.84 / 2.02 (0.42x) | 1.46 / 4.09 (0.36x) | 5.79 / 53.8 (0.11x) |
|  | erase all | 1.17 / 15.6 (0.07x) | 1.50 / 16.0 (0.09x) | 7.65 / 25.3 (0.30x) | 11.5 / 145.1 (0.08x) |
| Queue / std::queue | push+pop | 1.00 / 0.67 (1.50x) | 0.99 / 0.61 (1.61x) | 1.04 / 0.64 (1.62x) | 1.52 / 0.96 (1.59x) |
|  | push+pop (by ref) | 2.08 / 0.62 (3.33x) | 2.06 / 0.58 (3.56x) | 2.21 / 0.62 (3.60x) | 3.03 / 0.80 (3.79x) |
| SinglyLinkedList / forward_list | push_front | 5.17 / 4.88 (1.06x) | 4.37 / 4.14 (1.06x) | 4.19 / 3.92 (1.07x) | 5.56 / 5.50 (1.01x) |

What the numbers say:

- **Where std wins.**
  - `std::queue` (libc++ `deque`) is 1.5x to 1.6x faster than `dsl::Queue` over
    `CircularBuffer` at steady-state push+pop, and 3.3x to 3.8x faster when the queue is
    passed by reference. I have not closed this gap.
  - `push_back` into a local `std::vector` is 2x to 3.5x faster. See the inlining point
    below.
  - `std::map` inserts random keys 4 to 19 percent faster than `AVLTree`, and it is faster
    on sorted input up to 10k elements.
  - Hit lookups in `std::unordered_map` are 1.2x to 1.5x faster up to 100k elements.
  - `std::list::sort` is 1.08x to 1.19x faster at 100k and 1M.
  - The plain BST finds are 1.8x to 2.4x slower than `std::map`. With random keys its height
    is 25, 35, 43 and 53 at 1k to 1M elements, against 12, 16, 20 and 24 for the AVL tree.
    On sorted input the BST is 29x slower at 1k and 467x slower at 10k. That is the
    expected O(n^2), and it is why the AVL tree exists.
- **Inlining, not the data structure.** In the `push_back` row the vector is a local
  variable. libc++ inlines its whole growth path into the loop, so clang keeps the
  vector's pointers in registers. The `(by ref)` row runs the same loop inside a function
  that takes the container by reference, as happens when it is a class member or a
  parameter. There `DynamicArray` is 9 to 15 percent faster than `std::vector`.
- **Where dsl wins.**
  - The Robin Hood hash map beats `std::unordered_map` on erase (3x to 14x), iteration from
    10k up (2.4x to 9x), misses (1.07x to 2.3x), and inserts into a reserved table (1.3x to
    3.3x). At 1M it also wins inserts without `reserve` (2.5x) and hit lookups (2.3x).
    `std::unordered_map` allocates one node per element and frees it on erase; the
    open-addressing table does neither.
  - `AVLTree` finds are 8 to 20 percent faster than `std::map` from 10k up, and erase is 4
    to 16 percent faster at every size.
- **Load factor shows up.** The hash map's hit lookups are slower than std at 100k but
  faster at 1M. At 100k the table is 76 percent full (100,000 keys in 131,072 slots). At 1M
  it is 48 percent full (2^21 slots), so probes are shorter.
- **Parity.** Array and list iteration, list insertion and erase, `insert front` on the
  array, and the heap are within a few percent of std at most sizes. That is expected,
  because they do the same work.

## Limitations

- Not thread-safe for writers, like the standard containers. Concurrent reads of an
  unchanging container are fine (tested under TSan).
- The trees use `new`/`delete` for nodes and do not take an allocator.
- `HashMap` iterators are invalidated by any insert (rehash) and by erase (backward shift
  moves elements). There is no `erase(iterator)` and no heterogeneous lookup.
- `HashMap` throws `std::length_error` if a probe sequence would exceed 65,535 slots, which
  only a degenerate hash function can cause. Insert gives the strong guarantee only when
  `K` and `V` have non-throwing moves.
- `BinarySearchTree` is unbalanced on purpose (it is the baseline for the AVL tree).
  `print_shape` recurses and is meant for small trees.
- Benchmarks were run on one machine with other load present, with one compiler and one
  standard library (libc++). libstdc++ would give different std numbers.

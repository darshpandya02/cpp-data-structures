#pragma once
// Test helpers: an element type that counts live objects and can be told to
// throw, and an allocator that counts allocations.

#include <cstddef>
#include <memory>
#include <new>
#include <ostream>
#include <random>
#include <stdexcept>
#include <type_traits>

namespace test {

struct Boom : std::runtime_error {
    Boom() : std::runtime_error("boom") {}
};

/// Counts live instances. Copies (and moves, when NoexceptMove is false)
/// decrement `countdown`; the operation that takes it to zero throws.
/// countdown < 0 disables throwing.
template <bool NoexceptMove>
struct BasicTracked {
    static inline long live = 0;
    static inline long countdown = -1;
    int v = 0;

    static void tick() {
        if (countdown > 0 && --countdown == 0) throw Boom();
    }

    BasicTracked() { ++live; }
    BasicTracked(int x) : v(x) { ++live; }  // NOLINT: implicit on purpose
    BasicTracked(const BasicTracked& o) : v(o.v) {
        tick();
        ++live;
    }
    BasicTracked(BasicTracked&& o) noexcept(NoexceptMove) : v(o.v) {
        if constexpr (!NoexceptMove) tick();
        o.v = -1;
        ++live;
    }
    BasicTracked& operator=(const BasicTracked& o) {
        tick();
        v = o.v;
        return *this;
    }
    BasicTracked& operator=(BasicTracked&& o) noexcept(NoexceptMove) {
        if constexpr (!NoexceptMove) tick();
        v = o.v;
        o.v = -1;
        return *this;
    }
    ~BasicTracked() { --live; }

    friend bool operator==(const BasicTracked& a, const BasicTracked& b) { return a.v == b.v; }
    friend bool operator<(const BasicTracked& a, const BasicTracked& b) { return a.v < b.v; }
    friend std::ostream& operator<<(std::ostream& os, const BasicTracked& t) { return os << t.v; }
};

using Tracked = BasicTracked<true>;         // moves never throw
using ThrowingMove = BasicTracked<false>;   // moves may throw, so containers copy

/// Resets the counters for one test and checks nothing leaked at the end.
template <class T>
struct LiveGuard {
    long start = T::live;
    LiveGuard() { T::countdown = -1; }
    ~LiveGuard() { T::countdown = -1; }
    long leaked() const { return T::live - start; }
};

struct AllocStats {
    long allocations = 0;
    long deallocations = 0;
    long live_bytes = 0;
};

/// Stateful allocator. Two instances are equal only if they share stats,
/// which makes the "unequal allocators" code paths reachable in tests.
template <class T, bool Propagate = false>
struct CountingAllocator {
    using value_type = T;
    using propagate_on_container_copy_assignment = std::bool_constant<Propagate>;
    using propagate_on_container_move_assignment = std::bool_constant<Propagate>;
    using propagate_on_container_swap = std::bool_constant<Propagate>;
    using is_always_equal = std::false_type;
    template <class U>
    struct rebind {
        using other = CountingAllocator<U, Propagate>;
    };

    std::shared_ptr<AllocStats> stats = std::make_shared<AllocStats>();

    CountingAllocator() = default;
    // Copy only: moving an allocator must leave the source unchanged, and a
    // defaulted move would empty the shared_ptr.
    CountingAllocator(const CountingAllocator&) = default;
    CountingAllocator& operator=(const CountingAllocator&) = default;
    template <class U>
    CountingAllocator(const CountingAllocator<U, Propagate>& o) noexcept : stats(o.stats) {}  // NOLINT

    T* allocate(std::size_t n) {
        ++stats->allocations;
        stats->live_bytes += static_cast<long>(n * sizeof(T));
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }
    void deallocate(T* p, std::size_t n) noexcept {
        ++stats->deallocations;
        stats->live_bytes -= static_cast<long>(n * sizeof(T));
        ::operator delete(p);
    }

    template <class U>
    bool operator==(const CountingAllocator<U, Propagate>& o) const noexcept {
        return stats == o.stats;
    }
};

inline std::mt19937 rng(unsigned seed) { return std::mt19937(seed); }

inline int rand_int(std::mt19937& g, int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(g); }

}  // namespace test

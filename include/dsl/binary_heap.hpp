#pragma once
/// @file binary_heap.hpp
/// @brief Array-backed binary heap and the PriorityQueue built on it.

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <utility>

#include "dynamic_array.hpp"

namespace dsl {

/// Binary heap stored in a random-access container (DynamicArray by
/// default). With the default `std::less` it is a max-heap: top() is the
/// largest element, the same convention as std::priority_queue.
///
/// Building from a range uses Floyd's bottom-up heapify, O(n). push and pop
/// are O(log n). Sifting moves a "hole" instead of swapping, so each level
/// costs one move instead of three.
template <class T, class Compare = std::less<T>, class Container = DynamicArray<T>>
class BinaryHeap {
public:
    using value_type = T;
    using container_type = Container;
    using value_compare = Compare;
    using size_type = std::size_t;
    using const_reference = const T&;

    BinaryHeap() = default;
    explicit BinaryHeap(const Compare& comp) : comp_(comp) {}

    /// O(n) construction from a range.
    template <std::input_iterator It>
    BinaryHeap(It first, It last, const Compare& comp = Compare()) : c_(first, last), comp_(comp) {
        heapify();
    }
    BinaryHeap(std::initializer_list<T> il, const Compare& comp = Compare())
        : BinaryHeap(il.begin(), il.end(), comp) {}
    explicit BinaryHeap(Container c, const Compare& comp = Compare()) : c_(std::move(c)), comp_(comp) { heapify(); }

    [[nodiscard]] bool empty() const noexcept { return c_.size() == 0; }
    size_type size() const noexcept { return c_.size(); }

    /// The largest element under Compare. Precondition: !empty().
    const_reference top() const noexcept { return c_[0]; }

    void push(const T& v) { emplace(v); }
    void push(T&& v) { emplace(std::move(v)); }
    template <class... Args>
    void emplace(Args&&... args) {
        c_.emplace_back(std::forward<Args>(args)...);
        sift_up(c_.size() - 1);
    }

    /// Removes the top element. O(log n). Precondition: !empty().
    void pop() {
        const size_type n = c_.size();
        if (n > 1) c_[0] = std::move(c_[n - 1]);
        c_.pop_back();
        if (n > 2) sift_down(0);
    }

    /// Removes and returns the top element. Precondition: !empty().
    T extract_top() {
        T v = std::move(c_[0]);
        pop();
        return v;
    }

    void clear() noexcept { c_.clear(); }
    void reserve(size_type n) { c_.reserve(n); }

    /// The underlying array, in heap order (not sorted).
    const Container& container() const noexcept { return c_; }

    /// Checks the heap property. O(n). Used by the tests.
    bool is_valid() const {
        for (size_type i = 1; i < c_.size(); ++i)
            if (comp_(c_[(i - 1) / 2], c_[i])) return false;
        return true;
    }

    void swap(BinaryHeap& o) noexcept {
        using std::swap;
        c_.swap(o.c_);
        swap(comp_, o.comp_);
    }

private:
    Container c_;
    [[no_unique_address]] Compare comp_{};

    void heapify() {
        const size_type n = c_.size();
        for (size_type i = n / 2; i-- > 0;) sift_down(i);
    }

    void sift_up(size_type i) {
        T v = std::move(c_[i]);
        while (i > 0) {
            const size_type parent = (i - 1) / 2;
            if (!comp_(c_[parent], v)) break;
            c_[i] = std::move(c_[parent]);
            i = parent;
        }
        c_[i] = std::move(v);
    }

    void sift_down(size_type i) {
        const size_type n = c_.size();
        T v = std::move(c_[i]);
        for (;;) {
            size_type child = 2 * i + 1;
            if (child >= n) break;
            if (child + 1 < n && comp_(c_[child], c_[child + 1])) ++child;
            if (!comp_(v, c_[child])) break;
            c_[i] = std::move(c_[child]);
            i = child;
        }
        c_[i] = std::move(v);
    }
};

/// A priority queue is a binary heap; the alias gives it the familiar name.
template <class T, class Compare = std::less<T>, class Container = DynamicArray<T>>
using PriorityQueue = BinaryHeap<T, Compare, Container>;

}  // namespace dsl

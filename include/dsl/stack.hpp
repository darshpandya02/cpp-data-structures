#pragma once
/// @file stack.hpp
/// @brief LIFO adapter over any container with back operations.

#include <cstddef>
#include <ostream>
#include <utility>

#include "dynamic_array.hpp"

namespace dsl {

/// LIFO stack. An adapter, like std::stack: it holds a container by value
/// and exposes only the stack operations. Any container with `back()`,
/// `emplace_back()`, `pop_back()` and `size()` works, for example
/// DynamicArray (the default), DoublyLinkedList or CircularBuffer.
template <class T, class Container = DynamicArray<T>>
class Stack {
public:
    using container_type = Container;
    using value_type = T;
    using size_type = std::size_t;
    using reference = T&;
    using const_reference = const T&;

    Stack() = default;
    explicit Stack(const Container& c) : c_(c) {}
    explicit Stack(Container&& c) noexcept : c_(std::move(c)) {}

    [[nodiscard]] bool empty() const noexcept { return c_.size() == 0; }
    size_type size() const noexcept { return c_.size(); }

    reference top() noexcept { return c_.back(); }
    const_reference top() const noexcept { return c_.back(); }

    void push(const T& v) { c_.emplace_back(v); }
    void push(T&& v) { c_.emplace_back(std::move(v)); }
    template <class... Args>
    reference emplace(Args&&... args) {
        return c_.emplace_back(std::forward<Args>(args)...);
    }
    /// Removes the top element. Precondition: !empty().
    void pop() noexcept { c_.pop_back(); }

    const Container& container() const noexcept { return c_; }

    void swap(Stack& o) noexcept { c_.swap(o.c_); }
    friend void swap(Stack& a, Stack& b) noexcept { a.swap(b); }

    friend bool operator==(const Stack& a, const Stack& b) { return a.c_ == b.c_; }

    /// Prints bottom to top, e.g. `[1, 2, 3>` where 3 is the top.
    friend std::ostream& operator<<(std::ostream& os, const Stack& s) {
        os << '[';
        bool first = true;
        for (const auto& v : s.c_) {
            if (!first) os << ", ";
            first = false;
            os << v;
        }
        return os << '>';
    }

private:
    Container c_;
};

}  // namespace dsl

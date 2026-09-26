#pragma once
/// @file queue.hpp
/// @brief FIFO adapter over any container with front and back operations.

#include <cstddef>
#include <ostream>
#include <utility>

#include "circular_buffer.hpp"

namespace dsl {

/// FIFO queue. An adapter, like std::queue. Any container with `front()`,
/// `back()`, `emplace_back()`, `pop_front()` and `size()` works:
/// CircularBuffer (the default, one contiguous allocation that is reused as
/// the queue cycles), DoublyLinkedList or SinglyLinkedList.
template <class T, class Container = CircularBuffer<T>>
class Queue {
public:
    using container_type = Container;
    using value_type = T;
    using size_type = std::size_t;
    using reference = T&;
    using const_reference = const T&;

    Queue() = default;
    explicit Queue(const Container& c) : c_(c) {}
    explicit Queue(Container&& c) noexcept : c_(std::move(c)) {}

    [[nodiscard]] bool empty() const noexcept { return c_.size() == 0; }
    size_type size() const noexcept { return c_.size(); }

    reference front() noexcept { return c_.front(); }
    const_reference front() const noexcept { return c_.front(); }
    reference back() noexcept { return c_.back(); }
    const_reference back() const noexcept { return c_.back(); }

    void push(const T& v) { c_.emplace_back(v); }
    void push(T&& v) { c_.emplace_back(std::move(v)); }
    template <class... Args>
    reference emplace(Args&&... args) {
        return c_.emplace_back(std::forward<Args>(args)...);
    }
    /// Removes the front element. Precondition: !empty().
    void pop() noexcept { c_.pop_front(); }

    const Container& container() const noexcept { return c_; }

    void swap(Queue& o) noexcept { c_.swap(o.c_); }
    friend void swap(Queue& a, Queue& b) noexcept { a.swap(b); }

    friend bool operator==(const Queue& a, const Queue& b) { return a.c_ == b.c_; }

    /// Prints front to back, e.g. `<1, 2, 3]` where 1 is the front.
    friend std::ostream& operator<<(std::ostream& os, const Queue& q) {
        os << '<';
        bool first = true;
        for (const auto& v : q.c_) {
            if (!first) os << ", ";
            first = false;
            os << v;
        }
        return os << ']';
    }

private:
    Container c_;
};

}  // namespace dsl

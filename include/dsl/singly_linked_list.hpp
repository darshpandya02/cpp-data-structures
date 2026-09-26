#pragma once
/// @file singly_linked_list.hpp
/// @brief Singly linked list with O(1) push_front and push_back.

#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

#include "container_base.hpp"

namespace dsl {

/// A singly linked list, similar to std::forward_list but it also keeps a
/// tail pointer and its size, so push_back() and size() are O(1).
///
/// Nodes come from `Alloc` rebound to the node type. Insertion and removal
/// happen "after" a position, as in std::forward_list, and before_begin()
/// gives the position before the first element.
template <class T, class Alloc = std::allocator<T>>
class SinglyLinkedList : public ContainerBase<SinglyLinkedList<T, Alloc>> {
    // The head sentinel only needs a `next` pointer, so value nodes derive
    // from a value-less base and the list owns one base node as its head.
    struct NodeBase {
        NodeBase* next = nullptr;
    };
    struct Node : NodeBase {
        T value;
        template <class... Args>
        explicit Node(Args&&... args) : value(std::forward<Args>(args)...) {}
    };
    using NodeAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<Node>;
    using node_traits = std::allocator_traits<NodeAlloc>;

    template <bool Const>
    class Iter {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using reference = std::conditional_t<Const, const T&, T&>;
        using pointer = std::conditional_t<Const, const T*, T*>;

        Iter() = default;
        template <bool C = Const, class = std::enable_if_t<C>>
        Iter(const Iter<false>& it) noexcept : n_(it.n_) {}  // iterator -> const_iterator

        reference operator*() const noexcept { return static_cast<Node*>(n_)->value; }
        pointer operator->() const noexcept { return &static_cast<Node*>(n_)->value; }
        Iter& operator++() noexcept {
            n_ = n_->next;
            return *this;
        }
        Iter operator++(int) noexcept {
            Iter t = *this;
            n_ = n_->next;
            return t;
        }
        friend bool operator==(const Iter& a, const Iter& b) noexcept { return a.n_ == b.n_; }

    private:
        friend class SinglyLinkedList;
        template <bool>
        friend class Iter;
        explicit Iter(NodeBase* n) noexcept : n_(n) {}
        NodeBase* n_ = nullptr;
    };

public:
    using value_type = T;
    using allocator_type = Alloc;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using iterator = Iter<false>;
    using const_iterator = Iter<true>;

    SinglyLinkedList() = default;
    explicit SinglyLinkedList(const Alloc& a) noexcept : alloc_(a) {}
    template <std::input_iterator It>
    SinglyLinkedList(It first, It last, const Alloc& a = Alloc()) : alloc_(a) {
        for (; first != last; ++first) emplace_back(*first);
    }
    SinglyLinkedList(std::initializer_list<T> il, const Alloc& a = Alloc())
        : SinglyLinkedList(il.begin(), il.end(), a) {}

    SinglyLinkedList(const SinglyLinkedList& o)
        : SinglyLinkedList(o, node_traits::select_on_container_copy_construction(o.alloc_)) {}
    SinglyLinkedList(const SinglyLinkedList& o, const Alloc& a) : alloc_(a) {
        for (const auto& v : o) emplace_back(v);
    }
    SinglyLinkedList(SinglyLinkedList&& o) noexcept : alloc_(std::move(o.alloc_)) { take(o); }

    ~SinglyLinkedList() { clear(); }

    /// Strong guarantee: builds a full copy before touching *this.
    SinglyLinkedList& operator=(const SinglyLinkedList& o) {
        if (this != &o) {
            constexpr bool pocca = node_traits::propagate_on_container_copy_assignment::value;
            SinglyLinkedList tmp(o, pocca ? Alloc(o.alloc_) : Alloc(alloc_));
            swap_all(tmp);
        }
        return *this;
    }

    SinglyLinkedList& operator=(SinglyLinkedList&& o) noexcept(
        node_traits::propagate_on_container_move_assignment::value || node_traits::is_always_equal::value) {
        if (this == &o) return *this;
        if (node_traits::propagate_on_container_move_assignment::value || alloc_ == o.alloc_) {
            SinglyLinkedList tmp(std::move(o));
            swap_all(tmp);
        } else {
            SinglyLinkedList tmp(std::make_move_iterator(o.begin()), std::make_move_iterator(o.end()), Alloc(alloc_));
            swap_all(tmp);
            o.clear();
        }
        return *this;
    }

    allocator_type get_allocator() const { return Alloc(alloc_); }

    // ---- access and iteration ----------------------------------------------

    reference front() noexcept { return static_cast<Node*>(head_.next)->value; }
    const_reference front() const noexcept { return static_cast<Node*>(head_.next)->value; }
    reference back() noexcept { return static_cast<Node*>(tail_)->value; }
    const_reference back() const noexcept { return static_cast<Node*>(tail_)->value; }

    iterator before_begin() noexcept { return iterator(&head_); }
    const_iterator before_begin() const noexcept { return const_iterator(const_cast<NodeBase*>(&head_)); }
    iterator begin() noexcept { return iterator(head_.next); }
    iterator end() noexcept { return iterator(nullptr); }
    const_iterator begin() const noexcept { return const_iterator(head_.next); }
    const_iterator end() const noexcept { return const_iterator(nullptr); }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend() const noexcept { return end(); }

    size_type size() const noexcept { return size_; }

    // ---- modifiers ----------------------------------------------------------

    void push_front(const T& v) { emplace_front(v); }
    void push_front(T&& v) { emplace_front(std::move(v)); }
    template <class... Args>
    reference emplace_front(Args&&... args) {
        return *emplace_after(before_begin(), std::forward<Args>(args)...);
    }

    void push_back(const T& v) { emplace_back(v); }
    void push_back(T&& v) { emplace_back(std::move(v)); }
    template <class... Args>
    reference emplace_back(Args&&... args) {
        return *emplace_after(iterator(tail_), std::forward<Args>(args)...);
    }

    /// Removes the first element. O(1). Precondition: !empty().
    void pop_front() noexcept { erase_after(before_begin()); }

    iterator insert_after(const_iterator pos, const T& v) { return emplace_after(pos, v); }
    iterator insert_after(const_iterator pos, T&& v) { return emplace_after(pos, std::move(v)); }

    /// Inserts a new element after pos. O(1). Strong guarantee.
    template <class... Args>
    iterator emplace_after(const_iterator pos, Args&&... args) {
        Node* n = make_node(std::forward<Args>(args)...);
        NodeBase* p = pos.n_;
        n->next = p->next;
        p->next = n;
        if (p == tail_) tail_ = n;
        ++size_;
        return iterator(n);
    }

    /// Removes the element after pos and returns an iterator to the element
    /// that followed it. O(1).
    iterator erase_after(const_iterator pos) noexcept {
        NodeBase* p = pos.n_;
        Node* victim = static_cast<Node*>(p->next);
        p->next = victim->next;
        if (victim == tail_) tail_ = p;
        drop_node(victim);
        --size_;
        return iterator(p->next);
    }

    /// Removes every element for which pred returns true. O(n).
    template <class Pred>
    size_type remove_if(Pred pred) {
        size_type removed = 0;
        NodeBase* p = &head_;
        while (p->next) {
            if (pred(static_cast<Node*>(p->next)->value)) {
                erase_after(const_iterator(p));
                ++removed;
            } else {
                p = p->next;
            }
        }
        return removed;
    }
    size_type remove(const T& v) {
        return remove_if([&](const T& x) { return x == v; });
    }

    /// Reverses the list in place by relinking nodes. O(n), no allocation.
    void reverse() noexcept {
        NodeBase* prev = nullptr;
        NodeBase* cur = head_.next;
        tail_ = cur ? cur : &head_;
        while (cur) {
            NodeBase* next = cur->next;
            cur->next = prev;
            prev = cur;
            cur = next;
        }
        head_.next = prev;
    }

    void clear() noexcept {
        NodeBase* n = head_.next;
        while (n) {
            NodeBase* next = n->next;
            drop_node(static_cast<Node*>(n));
            n = next;
        }
        head_.next = nullptr;
        tail_ = &head_;
        size_ = 0;
    }

    void swap(SinglyLinkedList& o) noexcept {
        detail::swap_allocators(alloc_, o.alloc_);
        swap_nodes(o);
    }
    friend void swap(SinglyLinkedList& a, SinglyLinkedList& b) noexcept { a.swap(b); }

private:
    [[no_unique_address]] NodeAlloc alloc_{};
    NodeBase head_;
    NodeBase* tail_ = &head_;
    size_type size_ = 0;

    template <class... Args>
    Node* make_node(Args&&... args) {
        Node* n = node_traits::allocate(alloc_, 1);
        try {
            node_traits::construct(alloc_, n, std::forward<Args>(args)...);
        } catch (...) {
            node_traits::deallocate(alloc_, n, 1);
            throw;
        }
        return n;
    }
    void drop_node(Node* n) noexcept {
        node_traits::destroy(alloc_, n);
        node_traits::deallocate(alloc_, n, 1);
    }

    // Moves o's nodes into *this (which must be empty). The head sentinel is a
    // member, so a moved list has to re-point its tail when it becomes empty.
    void take(SinglyLinkedList& o) noexcept {
        head_.next = std::exchange(o.head_.next, nullptr);
        tail_ = head_.next ? o.tail_ : &head_;
        size_ = std::exchange(o.size_, 0);
        o.tail_ = &o.head_;
    }

    void swap_nodes(SinglyLinkedList& o) noexcept {
        std::swap(head_.next, o.head_.next);
        std::swap(size_, o.size_);
        std::swap(tail_, o.tail_);
        if (!head_.next) tail_ = &head_;
        if (!o.head_.next) o.tail_ = &o.head_;
    }

    // Used by the assignment operators: tmp was built with the allocator that
    // *this should end up with, so both the nodes and the allocators move.
    void swap_all(SinglyLinkedList& o) noexcept {
        using std::swap;
        swap(alloc_, o.alloc_);
        swap_nodes(o);
    }
};

}  // namespace dsl

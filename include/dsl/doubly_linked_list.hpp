#pragma once
/// @file doubly_linked_list.hpp
/// @brief Circular doubly linked list with a sentinel node.

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

#include "container_base.hpp"

namespace dsl {

/// A doubly linked list (the library's std::list).
///
/// The list is circular around a sentinel node that holds no value, so there
/// are no null checks on insert or erase and end() is always valid: `--end()`
/// is the last element. Iterators and references stay valid until their own
/// element is erased. sort() is a stable merge sort that relinks nodes and
/// never copies or moves the elements.
template <class T, class Alloc = std::allocator<T>>
class DoublyLinkedList : public ContainerBase<DoublyLinkedList<T, Alloc>> {
    struct NodeBase {
        NodeBase* prev;
        NodeBase* next;
    };
    struct Node : NodeBase {
        T value;
        template <class... Args>
        explicit Node(Args&&... args) : NodeBase{nullptr, nullptr}, value(std::forward<Args>(args)...) {}
    };
    using NodeAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<Node>;
    using node_traits = std::allocator_traits<NodeAlloc>;

    template <bool Const>
    class Iter {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using reference = std::conditional_t<Const, const T&, T&>;
        using pointer = std::conditional_t<Const, const T*, T*>;

        Iter() = default;
        template <bool C = Const, class = std::enable_if_t<C>>
        Iter(const Iter<false>& it) noexcept : n_(it.n_) {}

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
        Iter& operator--() noexcept {
            n_ = n_->prev;
            return *this;
        }
        Iter operator--(int) noexcept {
            Iter t = *this;
            n_ = n_->prev;
            return t;
        }
        friend bool operator==(const Iter& a, const Iter& b) noexcept { return a.n_ == b.n_; }

    private:
        friend class DoublyLinkedList;
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
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    DoublyLinkedList() noexcept(noexcept(NodeAlloc())) { reset_sentinel(); }
    explicit DoublyLinkedList(const Alloc& a) noexcept : alloc_(a) { reset_sentinel(); }
    template <std::input_iterator It>
    DoublyLinkedList(It first, It last, const Alloc& a = Alloc()) : DoublyLinkedList(a) {
        try {
            for (; first != last; ++first) emplace_back(*first);
        } catch (...) {
            clear();  // the destructor does not run when a constructor throws
            throw;
        }
    }
    DoublyLinkedList(std::initializer_list<T> il, const Alloc& a = Alloc())
        : DoublyLinkedList(il.begin(), il.end(), a) {}

    DoublyLinkedList(const DoublyLinkedList& o)
        : DoublyLinkedList(o, Alloc(node_traits::select_on_container_copy_construction(o.alloc_))) {}
    DoublyLinkedList(const DoublyLinkedList& o, const Alloc& a) : DoublyLinkedList(o.begin(), o.end(), a) {}
    DoublyLinkedList(DoublyLinkedList&& o) noexcept : alloc_(std::move(o.alloc_)) {
        reset_sentinel();
        take(o);
    }

    ~DoublyLinkedList() { clear(); }

    DoublyLinkedList& operator=(const DoublyLinkedList& o) {
        if (this != &o) {
            constexpr bool pocca = node_traits::propagate_on_container_copy_assignment::value;
            DoublyLinkedList tmp(o, pocca ? Alloc(o.alloc_) : Alloc(alloc_));
            swap_all(tmp);
        }
        return *this;
    }

    DoublyLinkedList& operator=(DoublyLinkedList&& o) noexcept(
        node_traits::propagate_on_container_move_assignment::value || node_traits::is_always_equal::value) {
        if (this == &o) return *this;
        if (node_traits::propagate_on_container_move_assignment::value || alloc_ == o.alloc_) {
            DoublyLinkedList tmp(std::move(o));
            swap_all(tmp);
        } else {
            DoublyLinkedList tmp(std::make_move_iterator(o.begin()), std::make_move_iterator(o.end()), Alloc(alloc_));
            swap_all(tmp);
            o.clear();
        }
        return *this;
    }

    allocator_type get_allocator() const { return Alloc(alloc_); }

    // ---- access and iteration ----------------------------------------------

    reference front() noexcept { return value_of(head_.next); }
    const_reference front() const noexcept { return value_of(head_.next); }
    reference back() noexcept { return value_of(head_.prev); }
    const_reference back() const noexcept { return value_of(head_.prev); }

    iterator begin() noexcept { return iterator(head_.next); }
    iterator end() noexcept { return iterator(&head_); }
    const_iterator begin() const noexcept { return const_iterator(head_.next); }
    const_iterator end() const noexcept { return const_iterator(sentinel()); }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend() const noexcept { return end(); }
    reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

    size_type size() const noexcept { return size_; }

    // ---- modifiers ----------------------------------------------------------

    void push_front(const T& v) { emplace(begin(), v); }
    void push_front(T&& v) { emplace(begin(), std::move(v)); }
    void push_back(const T& v) { emplace(end(), v); }
    void push_back(T&& v) { emplace(end(), std::move(v)); }
    template <class... Args>
    reference emplace_front(Args&&... args) {
        return *emplace(begin(), std::forward<Args>(args)...);
    }
    template <class... Args>
    reference emplace_back(Args&&... args) {
        return *emplace(end(), std::forward<Args>(args)...);
    }
    void pop_front() noexcept { erase(begin()); }
    void pop_back() noexcept { erase(const_iterator(head_.prev)); }

    iterator insert(const_iterator pos, const T& v) { return emplace(pos, v); }
    iterator insert(const_iterator pos, T&& v) { return emplace(pos, std::move(v)); }

    /// Inserts before pos. O(1). Strong guarantee.
    template <class... Args>
    iterator emplace(const_iterator pos, Args&&... args) {
        Node* n = make_node(std::forward<Args>(args)...);
        link_before(pos.n_, n);
        ++size_;
        return iterator(n);
    }

    /// Removes the element at pos, returns the element after it. O(1).
    iterator erase(const_iterator pos) noexcept {
        NodeBase* n = pos.n_;
        NodeBase* next = n->next;
        unlink(n);
        drop_node(static_cast<Node*>(n));
        --size_;
        return iterator(next);
    }
    iterator erase(const_iterator first, const_iterator last) noexcept {
        while (first != last) first = erase(first);
        return iterator(last.n_);
    }

    template <class Pred>
    size_type remove_if(Pred pred) {
        size_type removed = 0;
        for (auto it = cbegin(); it != cend();) {
            if (pred(*it)) {
                it = erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        return removed;
    }
    size_type remove(const T& v) {
        return remove_if([&](const T& x) { return x == v; });
    }

    /// Moves every node of `other` before pos. O(1), no allocation.
    /// Precondition: the allocators compare equal.
    void splice(const_iterator pos, DoublyLinkedList& other) noexcept {
        if (other.empty() || &other == this) return;
        NodeBase* first = other.head_.next;
        NodeBase* last = other.head_.prev;
        other.reset_sentinel();
        NodeBase* p = pos.n_;
        first->prev = p->prev;
        p->prev->next = first;
        last->next = p;
        p->prev = last;
        size_ += std::exchange(other.size_, 0);
    }

    /// Reverses the list in place. O(n), no allocation.
    void reverse() noexcept {
        NodeBase* n = &head_;
        do {
            std::swap(n->prev, n->next);
            n = n->prev;  // the old next
        } while (n != &head_);
    }

    /// Stable merge sort on the nodes. O(n log n) comparisons, O(log n)
    /// stack, no element copies or moves.
    template <class Compare = std::less<>>
    void sort(Compare comp = Compare()) {
        if (size_ < 2) return;
        // Work on a null-terminated singly linked chain, then restore prev.
        head_.prev->next = nullptr;
        NodeBase* sorted = merge_sort(head_.next, size_, comp);
        NodeBase* prev = &head_;
        for (NodeBase* n = sorted; n; n = n->next) {
            n->prev = prev;
            prev->next = n;
            prev = n;
        }
        prev->next = &head_;
        head_.prev = prev;
    }

    void clear() noexcept {
        NodeBase* n = head_.next;
        while (n != &head_) {
            NodeBase* next = n->next;
            drop_node(static_cast<Node*>(n));
            n = next;
        }
        reset_sentinel();
        size_ = 0;
    }

    void swap(DoublyLinkedList& o) noexcept {
        detail::swap_allocators(alloc_, o.alloc_);
        swap_nodes(o);
    }
    friend void swap(DoublyLinkedList& a, DoublyLinkedList& b) noexcept { a.swap(b); }

private:
    [[no_unique_address]] NodeAlloc alloc_{};
    NodeBase head_;  // sentinel: head_.next is the first node, head_.prev the last
    size_type size_ = 0;

    static T& value_of(NodeBase* n) noexcept { return static_cast<Node*>(n)->value; }
    NodeBase* sentinel() const noexcept { return const_cast<NodeBase*>(&head_); }
    void reset_sentinel() noexcept { head_.prev = head_.next = &head_; }

    static void link_before(NodeBase* pos, NodeBase* n) noexcept {
        n->next = pos;
        n->prev = pos->prev;
        pos->prev->next = n;
        pos->prev = n;
    }
    static void unlink(NodeBase* n) noexcept {
        n->prev->next = n->next;
        n->next->prev = n->prev;
    }

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

    // *this must be empty. First and last nodes point at the sentinel, so they
    // have to be re-pointed at this object's sentinel.
    void take(DoublyLinkedList& o) noexcept {
        if (o.size_ == 0) return;
        head_.next = o.head_.next;
        head_.prev = o.head_.prev;
        head_.next->prev = &head_;
        head_.prev->next = &head_;
        size_ = std::exchange(o.size_, 0);
        o.reset_sentinel();
    }

    void swap_nodes(DoublyLinkedList& o) noexcept {
        DoublyLinkedList* a = this;
        DoublyLinkedList* b = &o;
        // Detach both chains, then re-attach them crosswise.
        NodeBase* a_first = a->size_ ? a->head_.next : nullptr;
        NodeBase* a_last = a->size_ ? a->head_.prev : nullptr;
        NodeBase* b_first = b->size_ ? b->head_.next : nullptr;
        NodeBase* b_last = b->size_ ? b->head_.prev : nullptr;
        attach(a, b_first, b_last);
        attach(b, a_first, a_last);
        std::swap(a->size_, b->size_);
    }
    static void attach(DoublyLinkedList* l, NodeBase* first, NodeBase* last) noexcept {
        if (!first) {
            l->reset_sentinel();
            return;
        }
        l->head_.next = first;
        l->head_.prev = last;
        first->prev = &l->head_;
        last->next = &l->head_;
    }

    void swap_all(DoublyLinkedList& o) noexcept {
        using std::swap;
        swap(alloc_, o.alloc_);
        swap_nodes(o);
    }

    template <class Compare>
    static NodeBase* merge_sort(NodeBase* head, size_type n, Compare& comp) {
        if (n < 2) {
            if (head) head->next = nullptr;
            return head;
        }
        const size_type half = n / 2;
        NodeBase* mid = head;
        for (size_type i = 1; i < half; ++i) mid = mid->next;
        NodeBase* right = mid->next;
        mid->next = nullptr;
        NodeBase* a = merge_sort(head, half, comp);
        NodeBase* b = merge_sort(right, n - half, comp);
        NodeBase dummy{nullptr, nullptr};
        NodeBase* tail = &dummy;
        while (a && b) {
            if (comp(value_of(b), value_of(a))) {  // take from b only if strictly less: stable
                tail->next = b;
                b = b->next;
            } else {
                tail->next = a;
                a = a->next;
            }
            tail = tail->next;
        }
        tail->next = a ? a : b;
        return dummy.next;
    }
};

}  // namespace dsl

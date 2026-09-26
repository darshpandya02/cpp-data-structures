#pragma once
/// @file circular_buffer.hpp
/// @brief Ring buffer with O(1) push/pop at both ends.

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "container_base.hpp"

namespace dsl {

/// A ring buffer over one contiguous allocation.
///
/// It can be used two ways:
/// - As a growable double-ended queue: push_back()/push_front() double the
///   capacity when the buffer is full (amortized O(1), strong guarantee).
///   This is the default container of dsl::Queue.
/// - As a fixed-size history buffer: reserve(n) once, then
///   push_back_overwrite() drops the oldest element when the buffer is full
///   and never allocates.
///
/// Capacity is exact (not rounded to a power of two). Index wrap-around is a
/// compare and subtract rather than a modulo.
template <class T, class Alloc = std::allocator<T>>
class CircularBuffer : public ContainerBase<CircularBuffer<T, Alloc>> {
    using traits = std::allocator_traits<Alloc>;

    template <bool Const>
    class Iter {
        using Buf = std::conditional_t<Const, const CircularBuffer, CircularBuffer>;

    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using reference = std::conditional_t<Const, const T&, T&>;
        using pointer = std::conditional_t<Const, const T*, T*>;

        Iter() = default;
        template <bool C = Const, class = std::enable_if_t<C>>
        Iter(const Iter<false>& it) noexcept : buf_(it.buf_), i_(it.i_) {}

        reference operator*() const noexcept { return (*buf_)[i_]; }
        pointer operator->() const noexcept { return &(*buf_)[i_]; }
        reference operator[](difference_type n) const noexcept { return (*buf_)[i_ + static_cast<std::size_t>(n)]; }

        Iter& operator++() noexcept { return ++i_, *this; }
        Iter operator++(int) noexcept { Iter t = *this; ++i_; return t; }
        Iter& operator--() noexcept { return --i_, *this; }
        Iter operator--(int) noexcept { Iter t = *this; --i_; return t; }
        Iter& operator+=(difference_type n) noexcept { i_ += static_cast<std::size_t>(n); return *this; }
        Iter& operator-=(difference_type n) noexcept { i_ -= static_cast<std::size_t>(n); return *this; }
        friend Iter operator+(Iter it, difference_type n) noexcept { return it += n; }
        friend Iter operator+(difference_type n, Iter it) noexcept { return it += n; }
        friend Iter operator-(Iter it, difference_type n) noexcept { return it -= n; }
        friend difference_type operator-(const Iter& a, const Iter& b) noexcept {
            return static_cast<difference_type>(a.i_) - static_cast<difference_type>(b.i_);
        }
        friend bool operator==(const Iter& a, const Iter& b) noexcept { return a.i_ == b.i_; }
        friend auto operator<=>(const Iter& a, const Iter& b) noexcept { return a.i_ <=> b.i_; }

    private:
        friend class CircularBuffer;
        template <bool>
        friend class Iter;
        Iter(Buf* b, std::size_t i) noexcept : buf_(b), i_(i) {}
        Buf* buf_ = nullptr;
        std::size_t i_ = 0;  // logical index, 0 == front
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

    CircularBuffer() = default;
    explicit CircularBuffer(const Alloc& a) noexcept : alloc_(a) {}
    template <std::input_iterator It>
    CircularBuffer(It first, It last, const Alloc& a = Alloc()) : alloc_(a) {
        try {
            for (; first != last; ++first) emplace_back(*first);
        } catch (...) {
            release();
            throw;
        }
    }
    CircularBuffer(std::initializer_list<T> il, const Alloc& a = Alloc()) : CircularBuffer(il.begin(), il.end(), a) {}

    CircularBuffer(const CircularBuffer& o)
        : CircularBuffer(o, traits::select_on_container_copy_construction(o.alloc_)) {}
    CircularBuffer(const CircularBuffer& o, const Alloc& a) : alloc_(a) {
        if (o.size_ == 0) return;
        buf_ = traits::allocate(alloc_, o.size_);
        cap_ = o.size_;
        size_type i = 0;
        try {
            for (; i < o.size_; ++i) traits::construct(alloc_, buf_ + i, o[i]);
        } catch (...) {
            size_ = i;
            release();
            throw;
        }
        size_ = i;
    }
    CircularBuffer(CircularBuffer&& o) noexcept
        : alloc_(std::move(o.alloc_)),
          buf_(std::exchange(o.buf_, nullptr)),
          cap_(std::exchange(o.cap_, 0)),
          head_(std::exchange(o.head_, 0)),
          size_(std::exchange(o.size_, 0)) {}

    ~CircularBuffer() { release(); }

    CircularBuffer& operator=(const CircularBuffer& o) {
        if (this != &o) {
            constexpr bool pocca = traits::propagate_on_container_copy_assignment::value;
            CircularBuffer tmp(o, pocca ? o.alloc_ : alloc_);
            swap_all(tmp);
        }
        return *this;
    }
    CircularBuffer& operator=(CircularBuffer&& o) noexcept(
        traits::propagate_on_container_move_assignment::value || traits::is_always_equal::value) {
        if (this == &o) return *this;
        if (traits::propagate_on_container_move_assignment::value || alloc_ == o.alloc_) {
            CircularBuffer tmp(std::move(o));
            swap_all(tmp);
        } else {
            CircularBuffer tmp(std::make_move_iterator(o.begin()), std::make_move_iterator(o.end()), alloc_);
            swap_all(tmp);
            o.clear();
        }
        return *this;
    }

    allocator_type get_allocator() const { return alloc_; }

    // ---- access -------------------------------------------------------------

    /// Logical index: 0 is the front (oldest) element.
    reference operator[](size_type i) noexcept { return buf_[physical(i)]; }
    const_reference operator[](size_type i) const noexcept { return buf_[physical(i)]; }
    reference at(size_type i) {
        if (i >= size_) throw std::out_of_range("CircularBuffer::at: index out of range");
        return (*this)[i];
    }
    const_reference at(size_type i) const {
        if (i >= size_) throw std::out_of_range("CircularBuffer::at: index out of range");
        return (*this)[i];
    }
    reference front() noexcept { return buf_[head_]; }
    const_reference front() const noexcept { return buf_[head_]; }
    reference back() noexcept { return (*this)[size_ - 1]; }
    const_reference back() const noexcept { return (*this)[size_ - 1]; }

    iterator begin() noexcept { return iterator(this, 0); }
    iterator end() noexcept { return iterator(this, size_); }
    const_iterator begin() const noexcept { return const_iterator(this, 0); }
    const_iterator end() const noexcept { return const_iterator(this, size_); }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend() const noexcept { return end(); }
    reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

    size_type size() const noexcept { return size_; }
    size_type capacity() const noexcept { return cap_; }
    bool full() const noexcept { return size_ == cap_; }

    /// Ensures capacity() >= n. Strong guarantee.
    void reserve(size_type n) {
        if (n > cap_) reallocate(n);
    }

    // ---- modifiers ----------------------------------------------------------

    void push_back(const T& v) { emplace_back(v); }
    void push_back(T&& v) { emplace_back(std::move(v)); }
    void push_front(const T& v) { emplace_front(v); }
    void push_front(T&& v) { emplace_front(std::move(v)); }

    /// Appends at the back, growing if full. Amortized O(1), strong guarantee.
    template <class... Args>
    reference emplace_back(Args&&... args) {
        if (size_ == cap_) [[unlikely]] {
            T tmp(std::forward<Args>(args)...);  // see DynamicArray::emplace_back
            return grow_and_emplace(false, tmp);
        }
        T* slot = buf_ + physical(size_);
        traits::construct(alloc_, slot, std::forward<Args>(args)...);
        ++size_;
        return *slot;
    }

    /// Prepends at the front, growing if full. Amortized O(1), strong guarantee.
    template <class... Args>
    reference emplace_front(Args&&... args) {
        if (size_ == cap_) [[unlikely]] {
            T tmp(std::forward<Args>(args)...);  // see DynamicArray::emplace_back
            return grow_and_emplace(true, tmp);
        }
        const size_type h = head_ == 0 ? cap_ - 1 : head_ - 1;
        traits::construct(alloc_, buf_ + h, std::forward<Args>(args)...);
        head_ = h;
        ++size_;
        return buf_[h];
    }

    /// Appends at the back. When the buffer is full the oldest element is
    /// dropped instead of growing, so this never allocates once capacity()
    /// is non-zero. Returns true if an element was overwritten.
    /// Precondition: capacity() > 0.
    bool push_back_overwrite(const T& v) {
        if (size_ < cap_) {
            push_back(v);
            return false;
        }
        buf_[head_] = v;  // the oldest slot becomes the newest
        head_ = head_ + 1 == cap_ ? 0 : head_ + 1;
        return true;
    }

    void pop_front() noexcept {
        traits::destroy(alloc_, buf_ + head_);
        head_ = head_ + 1 == cap_ ? 0 : head_ + 1;
        --size_;
    }
    void pop_back() noexcept {
        traits::destroy(alloc_, buf_ + physical(size_ - 1));
        --size_;
    }

    void clear() noexcept {
        while (size_) pop_back();
        head_ = 0;
    }

    void swap(CircularBuffer& o) noexcept {
        detail::swap_allocators(alloc_, o.alloc_);
        swap_state(o);
    }
    friend void swap(CircularBuffer& a, CircularBuffer& b) noexcept { a.swap(b); }

private:
    [[no_unique_address]] Alloc alloc_{};
    T* buf_ = nullptr;
    size_type cap_ = 0;
    size_type head_ = 0;  // physical index of the front element
    size_type size_ = 0;

    size_type physical(size_type i) const noexcept {
        const size_type p = head_ + i;
        return p >= cap_ ? p - cap_ : p;
    }

    void release() noexcept {
        clear();
        if (buf_) traits::deallocate(alloc_, buf_, cap_);
        buf_ = nullptr;
        cap_ = 0;
    }

    void swap_state(CircularBuffer& o) noexcept {
        std::swap(buf_, o.buf_);
        std::swap(cap_, o.cap_);
        std::swap(head_, o.head_);
        std::swap(size_, o.size_);
    }
    void swap_all(CircularBuffer& o) noexcept {
        using std::swap;
        swap(alloc_, o.alloc_);
        swap_state(o);
    }

    // Copies/moves the elements, in logical order, into nb starting at `offset`.
    void relocate_into(T* nb, size_type offset) {
        size_type i = 0;
        try {
            for (; i < size_; ++i) traits::construct(alloc_, nb + offset + i, std::move_if_noexcept((*this)[i]));
        } catch (...) {
            for (size_type j = 0; j < i; ++j) traits::destroy(alloc_, nb + offset + j);
            throw;
        }
    }

    void adopt(T* nb, size_type new_cap, size_type new_size) noexcept {
        release();
        buf_ = nb;
        cap_ = new_cap;
        head_ = 0;
        size_ = new_size;
    }

    void reallocate(size_type new_cap) {
        T* nb = traits::allocate(alloc_, new_cap);
        try {
            relocate_into(nb, 0);
        } catch (...) {
            traits::deallocate(alloc_, nb, new_cap);
            throw;
        }
        adopt(nb, new_cap, size_);
    }

    // Grows and places `v` at the front or back (moved if that cannot throw).
    // Kept out of line so the fast paths above stay small enough to inline.
    [[gnu::noinline]] reference grow_and_emplace(bool at_front, T& v) {
        const size_type new_cap = cap_ == 0 ? 4 : cap_ * 2;
        T* nb = traits::allocate(alloc_, new_cap);
        T* slot = nb + (at_front ? 0 : size_);
        try {
            traits::construct(alloc_, slot, std::move_if_noexcept(v));
        } catch (...) {
            traits::deallocate(alloc_, nb, new_cap);
            throw;
        }
        try {
            relocate_into(nb, at_front ? 1 : 0);
        } catch (...) {
            traits::destroy(alloc_, slot);
            traits::deallocate(alloc_, nb, new_cap);
            throw;
        }
        adopt(nb, new_cap, size_ + 1);
        return *slot;
    }
};

}  // namespace dsl

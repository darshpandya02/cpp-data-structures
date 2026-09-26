#pragma once
/// @file dynamic_array.hpp
/// @brief A contiguous, growable array (the library's std::vector).

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "container_base.hpp"

namespace dsl {

/// Growth policies for DynamicArray. `next(cap, needed)` returns the new
/// capacity when `needed` elements do not fit in `cap`.
namespace growth {

/// Multiply capacity by 2 (what libc++ and libstdc++ do).
struct Doubling {
    static constexpr std::size_t next(std::size_t cap, std::size_t needed) noexcept {
        return std::max(needed, cap == 0 ? std::size_t{4} : cap * 2);
    }
};

/// Multiply capacity by 1.5 (what MSVC and folly::fbvector do). Freed blocks
/// can be reused by later growth, at the cost of more reallocations.
struct OneAndHalf {
    static constexpr std::size_t next(std::size_t cap, std::size_t needed) noexcept {
        return std::max(needed, cap < 4 ? std::size_t{4} : cap + cap / 2);
    }
};

}  // namespace growth

/// A contiguous dynamic array.
///
/// - Iterators are raw pointers, so every `<algorithm>` works on it.
/// - `push_back`/`emplace_back` give the strong exception guarantee: if the
///   element's constructor or a reallocation throws, the array is unchanged.
///   During reallocation elements are moved only if their move constructor is
///   `noexcept`, otherwise copied (`std::move_if_noexcept`).
/// - Allocator-aware, including the propagate_on_container_* traits.
///
/// @tparam T      element type
/// @tparam Alloc  allocator for T
/// @tparam Growth growth policy (growth::Doubling or growth::OneAndHalf)
template <class T, class Alloc = std::allocator<T>, class Growth = growth::Doubling>
class DynamicArray : public ContainerBase<DynamicArray<T, Alloc, Growth>> {
    using traits = std::allocator_traits<Alloc>;

public:
    using value_type = T;
    using allocator_type = Alloc;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = T*;
    using const_iterator = const T*;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    static_assert(std::is_same_v<typename traits::pointer, T*>,
                  "DynamicArray needs an allocator whose pointer type is T*");

    // ---- construction -------------------------------------------------------

    DynamicArray() noexcept(noexcept(Alloc())) = default;
    explicit DynamicArray(const Alloc& a) noexcept : alloc_(a) {}

    /// n value-initialized elements.
    explicit DynamicArray(size_type n, const Alloc& a = Alloc()) : alloc_(a) {
        reserve(n);
        append_n(n, [&](T* slot) { traits::construct(alloc_, slot); });
    }
    DynamicArray(size_type n, const T& value, const Alloc& a = Alloc()) : alloc_(a) {
        reserve(n);
        append_n(n, [&](T* slot) { traits::construct(alloc_, slot, value); });
    }
    template <std::input_iterator It>
    DynamicArray(It first, It last, const Alloc& a = Alloc()) : alloc_(a) {
        try {
            if constexpr (std::forward_iterator<It>) {
                const auto n = static_cast<size_type>(std::distance(first, last));
                reserve(n);
                append_n(n, [&](T* slot) { traits::construct(alloc_, slot, *first++); });
            } else {
                for (; first != last; ++first) emplace_back(*first);
            }
        } catch (...) {
            release();  // the destructor does not run when a constructor throws
            throw;
        }
    }
    DynamicArray(std::initializer_list<T> il, const Alloc& a = Alloc())
        : DynamicArray(il.begin(), il.end(), a) {}

    DynamicArray(const DynamicArray& other)
        : DynamicArray(other, traits::select_on_container_copy_construction(other.alloc_)) {}
    DynamicArray(const DynamicArray& other, const Alloc& a) : alloc_(a) {
        reserve(other.size_);
        const T* src = other.data_;
        try {
            append_n(other.size_, [&](T* slot) { traits::construct(alloc_, slot, *src++); });
        } catch (...) {
            release();
            throw;
        }
    }

    DynamicArray(DynamicArray&& other) noexcept
        : alloc_(std::move(other.alloc_)),
          data_(std::exchange(other.data_, nullptr)),
          size_(std::exchange(other.size_, 0)),
          cap_(std::exchange(other.cap_, 0)) {}

    ~DynamicArray() { release(); }

    DynamicArray& operator=(const DynamicArray& other) {
        if (this == &other) return *this;
        if constexpr (traits::propagate_on_container_copy_assignment::value) {
            if (alloc_ != other.alloc_) {
                release();
                alloc_ = other.alloc_;
            } else {
                alloc_ = other.alloc_;
            }
        }
        assign(other.begin(), other.end());
        return *this;
    }

    DynamicArray& operator=(DynamicArray&& other) noexcept(
        traits::propagate_on_container_move_assignment::value || traits::is_always_equal::value) {
        if (this == &other) return *this;
        if constexpr (traits::propagate_on_container_move_assignment::value) {
            release();
            alloc_ = std::move(other.alloc_);
            steal(other);
        } else if (alloc_ == other.alloc_) {
            release();
            steal(other);
        } else {
            // Different allocators that do not propagate: the buffer cannot
            // change hands, so move the elements one by one.
            assign(std::make_move_iterator(other.begin()), std::make_move_iterator(other.end()));
            other.clear();
        }
        return *this;
    }

    DynamicArray& operator=(std::initializer_list<T> il) {
        assign(il.begin(), il.end());
        return *this;
    }

    /// Replaces the contents with [first, last). Strong guarantee when a
    /// reallocation is needed, basic otherwise.
    template <std::input_iterator It>
    void assign(It first, It last) {
        if constexpr (std::forward_iterator<It>) {
            const auto n = static_cast<size_type>(std::distance(first, last));
            if (n > cap_) {
                DynamicArray tmp(first, last, alloc_);
                swap_buffers(tmp);
                return;
            }
            size_type i = 0;
            for (; i < size_ && first != last; ++i, ++first) data_[i] = *first;
            if (i < size_) {
                destroy_range(data_ + i, data_ + size_);
                size_ = i;
            }
            for (; first != last; ++first) {
                traits::construct(alloc_, data_ + size_, *first);
                ++size_;
            }
        } else {
            clear();
            for (; first != last; ++first) emplace_back(*first);
        }
    }

    allocator_type get_allocator() const noexcept { return alloc_; }

    // ---- element access -----------------------------------------------------

    reference operator[](size_type i) noexcept { return data_[i]; }
    const_reference operator[](size_type i) const noexcept { return data_[i]; }

    /// Bounds-checked access. Throws std::out_of_range.
    reference at(size_type i) {
        check_index(i);
        return data_[i];
    }
    const_reference at(size_type i) const {
        check_index(i);
        return data_[i];
    }

    reference front() noexcept { return data_[0]; }
    const_reference front() const noexcept { return data_[0]; }
    reference back() noexcept { return data_[size_ - 1]; }
    const_reference back() const noexcept { return data_[size_ - 1]; }
    T* data() noexcept { return data_; }
    const T* data() const noexcept { return data_; }

    // ---- iterators ----------------------------------------------------------

    iterator begin() noexcept { return data_; }
    iterator end() noexcept { return data_ + size_; }
    const_iterator begin() const noexcept { return data_; }
    const_iterator end() const noexcept { return data_ + size_; }
    const_iterator cbegin() const noexcept { return data_; }
    const_iterator cend() const noexcept { return data_ + size_; }
    reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

    // ---- capacity -----------------------------------------------------------

    size_type size() const noexcept { return size_; }
    size_type capacity() const noexcept { return cap_; }
    size_type max_size() const noexcept {
        return std::min<size_type>(traits::max_size(alloc_),
                                   std::numeric_limits<difference_type>::max() / sizeof(T));
    }

    /// Ensures capacity() >= n. Strong guarantee.
    void reserve(size_type n) {
        if (n <= cap_) return;
        if (n > max_size()) throw std::length_error("DynamicArray::reserve: too large");
        reallocate(n);
    }

    /// Releases unused capacity. Strong guarantee.
    void shrink_to_fit() {
        if (size_ == cap_) return;
        if (size_ == 0) {
            release();
            return;
        }
        reallocate(size_);
    }

    // ---- modifiers ----------------------------------------------------------

    void clear() noexcept {
        destroy_range(data_, data_ + size_);
        size_ = 0;
    }

    void push_back(const T& value) { emplace_back(value); }
    void push_back(T&& value) { emplace_back(std::move(value)); }

    /// Appends an element constructed from args. Amortized O(1).
    /// Strong exception guarantee. `args` may refer to an element of this
    /// array: the new element is constructed before anything is moved.
    template <class... Args>
    reference emplace_back(Args&&... args) {
        if (size_ < cap_) {
            traits::construct(alloc_, data_ + size_, std::forward<Args>(args)...);
            return data_[size_++];
        }
        // Slow path: build the element here and pass the temporary on. The
        // caller's argument then never has its address passed to an
        // out-of-line call, so it can stay in a register (this matters for
        // loops like `for (i...) a.push_back(i)`). Building it first also
        // makes an argument that refers into this array safe.
        T tmp(std::forward<Args>(args)...);
        return grow_and_append(tmp);
    }

    void pop_back() noexcept {
        --size_;
        traits::destroy(alloc_, data_ + size_);
    }

    iterator insert(const_iterator pos, const T& value) { return emplace(pos, value); }
    iterator insert(const_iterator pos, T&& value) { return emplace(pos, std::move(value)); }

    /// Inserts before pos. O(n - index). Strong guarantee when inserting at
    /// the end or when T's move operations do not throw, basic otherwise.
    template <class... Args>
    iterator emplace(const_iterator pos, Args&&... args) {
        const auto idx = static_cast<size_type>(pos - data_);
        if (idx == size_) {
            emplace_back(std::forward<Args>(args)...);
            return data_ + idx;
        }
        T tmp(std::forward<Args>(args)...);  // may alias an element; build it first
        if (size_ < cap_) {
            traits::construct(alloc_, data_ + size_, std::move(data_[size_ - 1]));
            ++size_;
            std::move_backward(data_ + idx, data_ + size_ - 2, data_ + size_ - 1);
            data_[idx] = std::move(tmp);
            return data_ + idx;
        }
        const size_type new_cap = next_capacity(size_ + 1);
        T* nb = traits::allocate(alloc_, new_cap);
        try {
            traits::construct(alloc_, nb + idx, std::move(tmp));
        } catch (...) {
            traits::deallocate(alloc_, nb, new_cap);
            throw;
        }
        size_type built = 0;
        try {
            for (; built < idx; ++built) traits::construct(alloc_, nb + built, std::move_if_noexcept(data_[built]));
            for (size_type i = idx; i < size_; ++i, ++built)
                traits::construct(alloc_, nb + i + 1, std::move_if_noexcept(data_[i]));
        } catch (...) {
            // nb[idx] and the first `built` source positions were constructed.
            for (size_type i = 0; i < built; ++i) traits::destroy(alloc_, nb + (i < idx ? i : i + 1));
            traits::destroy(alloc_, nb + idx);
            traits::deallocate(alloc_, nb, new_cap);
            throw;
        }
        replace_buffer(nb, new_cap, size_ + 1);
        return data_ + idx;
    }

    /// Removes the element at pos. O(n - index).
    iterator erase(const_iterator pos) { return erase(pos, pos + 1); }

    /// Removes [first, last). O(n - index).
    iterator erase(const_iterator first, const_iterator last) {
        T* f = data_ + (first - data_);
        T* l = data_ + (last - data_);
        if (f != l) {
            T* new_end = std::move(l, data_ + size_, f);
            destroy_range(new_end, data_ + size_);
            size_ = static_cast<size_type>(new_end - data_);
        }
        return f;
    }

    void resize(size_type n) { resize_impl(n); }
    void resize(size_type n, const T& value) { resize_impl(n, value); }

    void swap(DynamicArray& other) noexcept {
        detail::swap_allocators(alloc_, other.alloc_);
        swap_buffers(other);
    }
    friend void swap(DynamicArray& a, DynamicArray& b) noexcept { a.swap(b); }

private:
    [[no_unique_address]] Alloc alloc_{};
    T* data_ = nullptr;
    size_type size_ = 0;
    size_type cap_ = 0;

    /// Constructs n elements after the current end (capacity must suffice).
    /// The loop counter is a local, so the compiler can keep it in a
    /// register instead of reloading size_ after every element store. On an
    /// exception the new elements are destroyed and size_ is unchanged.
    template <class Make>
    void append_n(size_type n, Make make) {
        T* const base = data_ + size_;
        size_type i = 0;
        try {
            for (; i < n; ++i) make(base + i);
        } catch (...) {
            destroy_range(base, base + i);
            throw;
        }
        size_ += n;
    }

    void check_index(size_type i) const {
        if (i >= size_) throw std::out_of_range("DynamicArray::at: index out of range");
    }

    size_type next_capacity(size_type needed) const {
        if (needed > max_size()) throw std::length_error("DynamicArray: too large");
        return std::min(Growth::next(cap_, needed), max_size());
    }

    void destroy_range(T* f, T* l) noexcept {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (; f != l; ++f) traits::destroy(alloc_, f);
        }
    }

    void release() noexcept {
        if (data_) {
            destroy_range(data_, data_ + size_);
            traits::deallocate(alloc_, data_, cap_);
        }
        data_ = nullptr;
        size_ = cap_ = 0;
    }

    void steal(DynamicArray& other) noexcept {
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
        cap_ = std::exchange(other.cap_, 0);
    }

    void swap_buffers(DynamicArray& other) noexcept {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
        std::swap(cap_, other.cap_);
    }

    /// Constructs size_ elements in dst from the current buffer. On failure
    /// everything built so far is destroyed and the exception is rethrown,
    /// so the source is untouched (elements are only moved when that cannot
    /// throw).
    void relocate_into(T* dst) {
        size_type i = 0;
        try {
            for (; i < size_; ++i) traits::construct(alloc_, dst + i, std::move_if_noexcept(data_[i]));
        } catch (...) {
            destroy_range(dst, dst + i);
            throw;
        }
    }

    void replace_buffer(T* nb, size_type new_cap, size_type new_size) noexcept {
        release();
        data_ = nb;
        cap_ = new_cap;
        size_ = new_size;
    }

    void reallocate(size_type new_cap) {
        T* nb = traits::allocate(alloc_, new_cap);
        try {
            relocate_into(nb);
        } catch (...) {
            traits::deallocate(alloc_, nb, new_cap);
            throw;
        }
        replace_buffer(nb, new_cap, size_);
    }

    /// Reallocates and appends `v` (moved if that cannot throw, else copied).
    /// Strong guarantee.
    [[gnu::noinline]] reference grow_and_append(T& v) {
        const size_type new_cap = next_capacity(size_ + 1);
        T* nb = traits::allocate(alloc_, new_cap);
        try {
            traits::construct(alloc_, nb + size_, std::move_if_noexcept(v));
        } catch (...) {
            traits::deallocate(alloc_, nb, new_cap);
            throw;
        }
        try {
            relocate_into(nb);
        } catch (...) {
            traits::destroy(alloc_, nb + size_);
            traits::deallocate(alloc_, nb, new_cap);
            throw;
        }
        replace_buffer(nb, new_cap, size_ + 1);
        return data_[size_ - 1];
    }

    template <class... V>
    void resize_impl(size_type n, const V&... value) {
        if (n <= size_) {
            destroy_range(data_ + n, data_ + size_);
            size_ = n;
            return;
        }
        reserve(n);
        append_n(n - size_, [&](T* slot) { traits::construct(alloc_, slot, value...); });
    }
};

}  // namespace dsl

#pragma once
/// @file hash_map.hpp
/// @brief Open-addressing hash map with Robin Hood probing.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#include "container_base.hpp"

namespace dsl {

/// Hash map with open addressing and Robin Hood linear probing.
///
/// Layout: one array of slots holding `std::pair<K, V>` and a parallel array
/// of 16-bit probe distances (0 = empty, d = the element sits d - 1 slots
/// after its home slot). The distance array has one extra non-zero entry at
/// the end, so iteration stops without a bounds check.
///
/// - Insert: while probing, an element that is closer to its home than the
///   one being inserted gives up its slot ("take from the rich"). This keeps
///   probe lengths short and nearly equal.
/// - Lookup stops as soon as it meets a slot whose distance is smaller than
///   the current probe length, so misses are cheap too.
/// - Erase uses backward-shift deletion: the following elements move back
///   one slot. There are no tombstones, so the table does not degrade after
///   many erases.
/// - Capacity is a power of two. The home slot is taken from the high bits
///   of hash * 2^64/phi (Fibonacci hashing), which spreads even the identity
///   std::hash<int> across the table.
/// - The table doubles when size would exceed max_load_factor() * capacity
///   (default 0.875).
///
/// Iterators give read access to the pair (`it->first`, `it->second`);
/// `it.value()` gives a mutable reference to the value. Keys cannot be
/// changed through an iterator. Any insert may rehash and invalidate all
/// iterators; erase moves elements and invalidates iterators too.
///
/// Exception safety: strong for insert when K and V have non-throwing moves
/// (the elements are rehashed with moves), basic otherwise.
template <class K, class V, class Hash = std::hash<K>, class KeyEqual = std::equal_to<K>,
          class Alloc = std::allocator<std::pair<K, V>>>
class HashMap : public ContainerBase<HashMap<K, V, Hash, KeyEqual, Alloc>> {
public:
    using key_type = K;
    using mapped_type = V;
    using value_type = std::pair<K, V>;
    using size_type = std::size_t;
    using hasher = Hash;
    using key_equal = KeyEqual;
    using allocator_type = Alloc;

    /// Tells ContainerBase to compare two maps as sets of pairs.
    static constexpr bool unordered_equality = true;

private:
    using dist_t = std::uint16_t;
    using slot_traits = std::allocator_traits<Alloc>;
    using DistAlloc = typename slot_traits::template rebind_alloc<dist_t>;
    using dist_traits = std::allocator_traits<DistAlloc>;
    static constexpr unsigned kMaxDist = 0xFFFF;
    static constexpr size_type kMinCapacity = 8;

    template <bool Const>
    class Iter {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::pair<K, V>;
        using difference_type = std::ptrdiff_t;
        using reference = const value_type&;
        using pointer = const value_type*;

        Iter() = default;
        template <bool C = Const, class = std::enable_if_t<C>>
        Iter(const Iter<false>& it) noexcept : d_(it.d_), s_(it.s_) {}

        reference operator*() const noexcept { return *s_; }
        pointer operator->() const noexcept { return s_; }
        /// Mutable access to the mapped value (not available on const_iterator).
        template <bool C = Const, class = std::enable_if_t<!C>>
        V& value() const noexcept {
            return s_->second;
        }

        Iter& operator++() noexcept {
            do {
                ++d_;
                ++s_;
            } while (*d_ == 0);
            return *this;
        }
        Iter operator++(int) noexcept {
            Iter t = *this;
            ++*this;
            return t;
        }
        friend bool operator==(const Iter& a, const Iter& b) noexcept { return a.d_ == b.d_; }

    private:
        friend class HashMap;
        template <bool>
        friend class Iter;
        Iter(const dist_t* d, value_type* s) noexcept : d_(d), s_(s) {}
        const dist_t* d_ = nullptr;
        value_type* s_ = nullptr;
    };

public:
    using iterator = Iter<false>;
    using const_iterator = Iter<true>;

    // ---- construction ----------------------------------------------------------

    HashMap() = default;
    explicit HashMap(size_type expected, const Hash& h = Hash(), const KeyEqual& eq = KeyEqual(),
                     const Alloc& a = Alloc())
        : hash_(h), eq_(eq), alloc_(a), dalloc_(a) {
        reserve(expected);
    }
    explicit HashMap(const Alloc& a) : alloc_(a), dalloc_(a) {}
    HashMap(std::initializer_list<value_type> il) {
        reserve(il.size());
        for (const auto& kv : il) insert(kv);
    }

    HashMap(const HashMap& o) : HashMap(o, slot_traits::select_on_container_copy_construction(o.alloc_)) {}

    /// Copies the table layout slot for slot, so no rehashing is needed.
    HashMap(const HashMap& o, const Alloc& a)
        : hash_(o.hash_), eq_(o.eq_), alloc_(a), dalloc_(a), mlf_(o.mlf_) {
        if (o.cap_ == 0) return;
        allocate_table(o.cap_);
        size_type i = 0;
        try {
            for (; i < cap_; ++i) {
                if (o.dist_[i]) {
                    slot_traits::construct(alloc_, slots_ + i, o.slots_[i]);
                    dist_[i] = o.dist_[i];
                }
            }
        } catch (...) {
            for (size_type j = 0; j < i; ++j)
                if (dist_[j]) slot_traits::destroy(alloc_, slots_ + j);
            free_table();
            throw;
        }
        size_ = o.size_;
    }

    HashMap(HashMap&& o) noexcept
        : hash_(std::move(o.hash_)),
          eq_(std::move(o.eq_)),
          alloc_(std::move(o.alloc_)),
          dalloc_(std::move(o.dalloc_)),
          slots_(std::exchange(o.slots_, nullptr)),
          dist_(std::exchange(o.dist_, nullptr)),
          cap_(std::exchange(o.cap_, 0)),
          size_(std::exchange(o.size_, 0)),
          shift_(std::exchange(o.shift_, 64)),
          threshold_(std::exchange(o.threshold_, 0)),
          mlf_(o.mlf_) {}

    ~HashMap() { release(); }

    HashMap& operator=(const HashMap& o) {
        if (this != &o) {
            constexpr bool pocca = slot_traits::propagate_on_container_copy_assignment::value;
            HashMap tmp(o, pocca ? o.alloc_ : alloc_);
            swap_all(tmp);
        }
        return *this;
    }
    HashMap& operator=(HashMap&& o) noexcept(slot_traits::propagate_on_container_move_assignment::value ||
                                             slot_traits::is_always_equal::value) {
        if (this == &o) return *this;
        if (slot_traits::propagate_on_container_move_assignment::value || alloc_ == o.alloc_) {
            HashMap tmp(std::move(o));
            swap_all(tmp);
        } else {
            HashMap tmp(alloc_);
            tmp.hash_ = o.hash_;
            tmp.eq_ = o.eq_;
            tmp.mlf_ = o.mlf_;
            tmp.reserve(o.size_);
            for (size_type i = 0; i < o.cap_; ++i)
                if (o.dist_[i]) tmp.insert_new(std::move(o.slots_[i]));
            swap_all(tmp);
            o.clear();
        }
        return *this;
    }

    allocator_type get_allocator() const { return alloc_; }

    // ---- iteration ---------------------------------------------------------------

    iterator begin() noexcept { return first_occupied<iterator>(); }
    iterator end() noexcept { return iterator(dist_ + cap_, slots_ + cap_); }
    const_iterator begin() const noexcept { return const_cast<HashMap*>(this)->begin(); }
    const_iterator end() const noexcept { return const_cast<HashMap*>(this)->end(); }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend() const noexcept { return end(); }

    // ---- size and table ------------------------------------------------------------

    size_type size() const noexcept { return size_; }
    size_type bucket_count() const noexcept { return cap_; }
    float load_factor() const noexcept { return cap_ ? static_cast<float>(size_) / static_cast<float>(cap_) : 0.0f; }
    float max_load_factor() const noexcept { return mlf_; }

    /// Sets the load factor that triggers growth. Must be in (0, 1).
    void max_load_factor(float f) {
        if (!(f > 0.0f && f < 1.0f)) throw std::invalid_argument("HashMap::max_load_factor must be in (0, 1)");
        mlf_ = f;
        threshold_ = compute_threshold(cap_);
        if (size_ > threshold_) reserve(size_);
    }

    /// Makes room for n elements without further rehashing.
    void reserve(size_type n) {
        size_type cap = kMinCapacity;
        while (compute_threshold(cap) < n) cap *= 2;
        if (cap > cap_) rehash_to(cap);
    }

    /// Rehashes into a table with at least `buckets` slots (rounded up to a
    /// power of two, and never too small for the current size).
    void rehash(size_type buckets) {
        size_type cap = kMinCapacity;
        while (cap < buckets || compute_threshold(cap) < size_) cap *= 2;
        if (cap != cap_) rehash_to(cap);
    }

    /// Longest probe sequence in the table (1 = every element is in its home
    /// slot). O(capacity). Useful to see what the Robin Hood rule buys.
    size_type max_probe_length() const noexcept {
        dist_t m = 0;
        for (size_type i = 0; i < cap_; ++i)
            if (dist_[i] > m) m = dist_[i];
        return m;
    }

    // ---- lookup --------------------------------------------------------------------

    iterator find(const K& key) noexcept {
        const size_type i = find_index(key);
        return i == npos ? end() : iterator(dist_ + i, slots_ + i);
    }
    const_iterator find(const K& key) const noexcept { return const_cast<HashMap*>(this)->find(key); }
    bool contains(const K& key) const noexcept { return find_index(key) != npos; }
    size_type count(const K& key) const noexcept { return contains(key) ? 1 : 0; }

    V& at(const K& key) {
        const size_type i = find_index(key);
        if (i == npos) throw std::out_of_range("HashMap::at: key not found");
        return slots_[i].second;
    }
    const V& at(const K& key) const { return const_cast<HashMap*>(this)->at(key); }

    // ---- modifiers -------------------------------------------------------------------

    /// Inserts (key, V(args...)) if the key is absent; otherwise does nothing
    /// (args are not consumed). Returns the element and whether it was inserted.
    template <class KK, class... Args>
    std::pair<iterator, bool> try_emplace(KK&& key, Args&&... args) {
        if (const size_type i = find_index(key); i != npos) return {iterator(dist_ + i, slots_ + i), false};
        value_type kv(std::piecewise_construct, std::forward_as_tuple(std::forward<KK>(key)),
                      std::forward_as_tuple(std::forward<Args>(args)...));
        if (size_ + 1 > threshold_) grow();
        const size_type i = insert_new(std::move(kv));
        return {iterator(dist_ + i, slots_ + i), true};
    }

    std::pair<iterator, bool> insert(const value_type& kv) { return try_emplace(kv.first, kv.second); }
    std::pair<iterator, bool> insert(value_type&& kv) { return try_emplace(std::move(kv.first), std::move(kv.second)); }
    std::pair<iterator, bool> insert(const K& key, const V& value) { return try_emplace(key, value); }

    template <class VV>
    std::pair<iterator, bool> insert_or_assign(const K& key, VV&& value) {
        auto r = try_emplace(key, std::forward<VV>(value));
        if (!r.second) r.first.value() = std::forward<VV>(value);
        return r;
    }

    V& operator[](const K& key) { return try_emplace(key).first.value(); }
    V& operator[](K&& key) { return try_emplace(std::move(key)).first.value(); }

    /// Removes the key if present (backward-shift deletion). Returns 0 or 1.
    size_type erase(const K& key) {
        size_type i = find_index(key);
        if (i == npos) return 0;
        slot_traits::destroy(alloc_, slots_ + i);
        dist_[i] = 0;
        size_type j = (i + 1) & (cap_ - 1);
        while (dist_[j] > 1) {  // shift back every element that is not at home
            slot_traits::construct(alloc_, slots_ + i, std::move(slots_[j]));
            slot_traits::destroy(alloc_, slots_ + j);
            dist_[i] = static_cast<dist_t>(dist_[j] - 1);
            dist_[j] = 0;
            i = j;
            j = (j + 1) & (cap_ - 1);
        }
        --size_;
        return 1;
    }

    /// Destroys all elements, keeps the table.
    void clear() noexcept {
        for (size_type i = 0; i < cap_; ++i) {
            if (dist_[i]) {
                slot_traits::destroy(alloc_, slots_ + i);
                dist_[i] = 0;
            }
        }
        size_ = 0;
    }

    void swap(HashMap& o) noexcept {
        detail::swap_allocators(alloc_, o.alloc_);
        detail::swap_allocators(dalloc_, o.dalloc_);
        swap_table(o);
    }
    friend void swap(HashMap& a, HashMap& b) noexcept { a.swap(b); }

private:
    static constexpr size_type npos = static_cast<size_type>(-1);

    [[no_unique_address]] Hash hash_{};
    [[no_unique_address]] KeyEqual eq_{};
    [[no_unique_address]] Alloc alloc_{};
    [[no_unique_address]] DistAlloc dalloc_{};
    value_type* slots_ = nullptr;
    dist_t* dist_ = nullptr;
    size_type cap_ = 0;
    size_type size_ = 0;
    unsigned shift_ = 64;  // 64 - log2(cap_)
    size_type threshold_ = 0;
    float mlf_ = 0.875f;

    size_type compute_threshold(size_type cap) const noexcept {
        return static_cast<size_type>(static_cast<double>(cap) * static_cast<double>(mlf_));
    }

    size_type home(const K& key) const noexcept {
        const auto h = static_cast<std::uint64_t>(hash_(key));
        return static_cast<size_type>((h * 0x9E3779B97F4A7C15ull) >> shift_);
    }

    size_type find_index(const K& key) const noexcept {
        if (size_ == 0) return npos;
        size_type i = home(key);
        for (unsigned d = 1;; ++d) {
            if (dist_[i] < d) return npos;  // empty, or an element closer to home: key absent
            if (dist_[i] == d && eq_(slots_[i].first, key)) return i;
            i = (i + 1) & (cap_ - 1);
        }
    }

    /// Places an element whose key is known to be absent. The table must have
    /// room. Returns the slot where the new element ended up.
    ///
    /// `kv` doubles as the carry register: when a richer resident is found,
    /// it is swapped into `kv` and the loop goes on placing it. Callers only
    /// pass objects they are about to discard.
    size_type insert_new(value_type&& kv) {
        size_type i = home(kv.first);
        unsigned d = 1;
        size_type placed = npos;
        for (;;) {
            if (dist_[i] == 0) {
                slot_traits::construct(alloc_, slots_ + i, std::move(kv));
                dist_[i] = static_cast<dist_t>(d);
                ++size_;
                return placed == npos ? i : placed;
            }
            if (dist_[i] < d) {  // the resident is closer to home: it moves on instead
                using std::swap;
                swap(kv, slots_[i]);
                const unsigned resident = dist_[i];
                dist_[i] = static_cast<dist_t>(d);
                d = resident;
                if (placed == npos) placed = i;
            }
            i = (i + 1) & (cap_ - 1);
            if (++d > kMaxDist)  // only reachable with a degenerate hash function
                throw std::length_error("HashMap: probe sequence too long (the hash function is very poor)");
        }
    }

    void grow() { rehash_to(cap_ ? cap_ * 2 : kMinCapacity); }

    void allocate_table(size_type cap) {
        slots_ = slot_traits::allocate(alloc_, cap);
        try {
            dist_ = dist_traits::allocate(dalloc_, cap + 1);
        } catch (...) {
            slot_traits::deallocate(alloc_, slots_, cap);
            slots_ = nullptr;
            throw;
        }
        for (size_type i = 0; i < cap; ++i) dist_[i] = 0;
        dist_[cap] = 1;  // end marker for iteration
        cap_ = cap;
        shift_ = 64u - static_cast<unsigned>(std::countr_zero(static_cast<std::uint64_t>(cap)));
        threshold_ = compute_threshold(cap);
    }

    void free_table() noexcept {
        if (slots_) slot_traits::deallocate(alloc_, slots_, cap_);
        if (dist_) dist_traits::deallocate(dalloc_, dist_, cap_ + 1);
        slots_ = nullptr;
        dist_ = nullptr;
        cap_ = 0;
        shift_ = 64;
        threshold_ = 0;
    }

    void release() noexcept {
        clear();
        free_table();
    }

    void rehash_to(size_type new_cap) {
        HashMap next(alloc_);
        next.hash_ = hash_;
        next.eq_ = eq_;
        next.mlf_ = mlf_;
        next.allocate_table(new_cap);
        for (size_type i = 0; i < cap_; ++i)
            if (dist_[i]) next.insert_new(std::move(slots_[i]));
        swap_all(next);  // the old table (now in `next`) is destroyed here
    }

    template <class It>
    It first_occupied() const noexcept {
        if (!dist_) return It(nullptr, nullptr);
        size_type i = 0;
        while (dist_[i] == 0) ++i;  // stops at the end marker
        return It(dist_ + i, slots_ + i);
    }

    void swap_table(HashMap& o) noexcept {
        using std::swap;
        swap(hash_, o.hash_);
        swap(eq_, o.eq_);
        swap(slots_, o.slots_);
        swap(dist_, o.dist_);
        swap(cap_, o.cap_);
        swap(size_, o.size_);
        swap(shift_, o.shift_);
        swap(threshold_, o.threshold_);
        swap(mlf_, o.mlf_);
    }
    void swap_all(HashMap& o) noexcept {
        using std::swap;
        swap(alloc_, o.alloc_);
        swap(dalloc_, o.dalloc_);
        swap_table(o);
    }
};

}  // namespace dsl

#pragma once
/// @file container_base.hpp
/// @brief CRTP base shared by every iterable container in the library.

#include <algorithm>
#include <cstddef>
#include <memory>
#include <ostream>
#include <type_traits>
#include <utility>

namespace dsl {

namespace detail {

template <class T>
struct is_pair : std::false_type {};
template <class A, class B>
struct is_pair<std::pair<A, B>> : std::true_type {};

template <class T>
void print_element(std::ostream& os, const T& v) {
    os << v;
}
template <class A, class B>
void print_element(std::ostream& os, const std::pair<A, B>& p) {
    print_element(os, p.first);
    os << ": ";
    print_element(os, p.second);
}

/// Swaps two allocators only when the allocator says it should follow the
/// contents (propagate_on_container_swap). Otherwise the allocators must
/// compare equal, the same precondition the standard containers have.
template <class Alloc>
void swap_allocators(Alloc& a, Alloc& b) noexcept {
    if constexpr (std::allocator_traits<Alloc>::propagate_on_container_swap::value) {
        using std::swap;
        swap(a, b);
    }
}

}  // namespace detail

/// Common base for the iterable containers.
///
/// It is a CRTP mixin rather than a virtual interface: `Derived` supplies
/// `begin()`, `end()` and `size()`, and this base adds the operations that
/// can be written once in terms of them:
///
/// - `empty()`
/// - `operator==` (and, through C++20 rewriting, `operator!=`). Sequences
///   compare element by element. Containers that declare
///   `static constexpr bool unordered_equality = true` (the hash map) compare
///   as sets of key/value pairs instead.
/// - `operator<<`, which prints `[a, b, c]` or `{k: v, ...}` for maps.
///
/// There are no virtual functions, so the base adds no size and no indirect
/// calls. The destructor is protected, so a container cannot be deleted
/// through a `ContainerBase*`.
template <class Derived>
class ContainerBase {
public:
    /// True when the container holds no elements. O(1).
    [[nodiscard]] bool empty() const noexcept { return self().size() == 0; }

    friend bool operator==(const Derived& a, const Derived& b) {
        if (a.size() != b.size()) return false;
        if constexpr (requires { Derived::unordered_equality; }) {
            for (const auto& kv : a) {
                auto it = b.find(kv.first);
                if (it == b.end() || !(it->second == kv.second)) return false;
            }
            return true;
        } else {
            return std::equal(a.begin(), a.end(), b.begin(), b.end());
        }
    }

    friend std::ostream& operator<<(std::ostream& os, const Derived& c) {
        using value_type = std::remove_cv_t<typename Derived::value_type>;
        constexpr bool is_map = detail::is_pair<value_type>::value;
        os << (is_map ? '{' : '[');
        bool first = true;
        for (const auto& v : c) {
            if (!first) os << ", ";
            first = false;
            detail::print_element(os, v);
        }
        return os << (is_map ? '}' : ']');
    }

protected:
    ContainerBase() = default;
    ContainerBase(const ContainerBase&) = default;
    ContainerBase(ContainerBase&&) = default;
    ContainerBase& operator=(const ContainerBase&) = default;
    ContainerBase& operator=(ContainerBase&&) = default;
    ~ContainerBase() = default;

private:
    const Derived& self() const noexcept { return static_cast<const Derived&>(*this); }
};

}  // namespace dsl

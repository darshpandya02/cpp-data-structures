#pragma once
/// @file avl_tree.hpp
/// @brief Self-balancing (AVL) ordered map.

#include <cstddef>
#include <cstdlib>
#include <functional>
#include <initializer_list>

#include "binary_search_tree.hpp"

namespace dsl {

/// AVL tree used as an ordered map.
///
/// Same interface as BinarySearchTree (both derive from BstBase), but after
/// every insert and erase it walks from the changed node up to the root,
/// updating subtree heights and rotating wherever the two child heights
/// differ by more than one. That keeps the height below 1.44 log2(n + 2),
/// so lookup, insert and erase are O(log n) even for sorted input.
///
/// rotations() counts single rotations since construction (a double
/// rotation counts as two). The demo program uses it to show when the tree
/// rebalances.
template <class K, class V, class Compare = std::less<K>>
class AVLTree : public BstBase<K, V, Compare, AVLTree<K, V, Compare>> {
    using Base = BstBase<K, V, Compare, AVLTree<K, V, Compare>>;
    using Node = typename Base::Node;
    friend Base;  // the base calls the after_insert/after_erase hooks

public:
    AVLTree() = default;
    explicit AVLTree(const Compare& c) : Base(c) {}
    AVLTree(std::initializer_list<typename Base::value_type> il) {
        for (const auto& kv : il) this->insert(kv);
    }

    /// Height in O(1), read from the root (0 for an empty tree).
    std::size_t height() const noexcept { return static_cast<std::size_t>(h(this->root_)); }

    std::size_t rotations() const noexcept { return rotations_; }

    /// Checks the BST invariants plus stored heights and the AVL balance
    /// condition at every node. O(n). Used by the tests.
    bool validate() const {
        return this->validate_nodes([](const Node* n) {
            return n->height == 1 + max(h(n->left), h(n->right)) && std::abs(balance(n)) <= 1;
        });
    }

private:
    std::size_t rotations_ = 0;

    static int h(const Node* n) noexcept { return n ? n->height : 0; }
    static int max(int a, int b) noexcept { return a > b ? a : b; }
    static void update(Node* n) noexcept { n->height = 1 + max(h(n->left), h(n->right)); }
    static int balance(const Node* n) noexcept { return h(n->left) - h(n->right); }

    //      x                y
    //     / \              / \
    //    a   y     ->     x   c
    //       / \          / \
    //      b   c        a   b
    Node* rotate_left(Node* x) noexcept {
        Node* y = x->right;
        x->right = y->left;
        if (y->left) y->left->parent = x;
        this->transplant(x, y);
        y->left = x;
        x->parent = y;
        update(x);
        update(y);
        ++rotations_;
        return y;
    }

    Node* rotate_right(Node* x) noexcept {
        Node* y = x->left;
        x->left = y->right;
        if (y->right) y->right->parent = x;
        this->transplant(x, y);
        y->right = x;
        x->parent = y;
        update(x);
        update(y);
        ++rotations_;
        return y;
    }

    /// Walks from n towards the root restoring heights and balance. It stops
    /// at the first subtree whose height is the same as before the change:
    /// nothing above it can have changed either.
    void rebalance_from(Node* n) noexcept {
        while (n) {
            const int old_height = n->height;
            update(n);
            const int b = balance(n);
            if (b > 1) {
                if (balance(n->left) < 0) rotate_left(n->left);  // left-right case
                n = rotate_right(n);
            } else if (b < -1) {
                if (balance(n->right) > 0) rotate_right(n->right);  // right-left case
                n = rotate_left(n);
            }
            if (n->height == old_height) return;
            n = n->parent;
        }
    }

    void after_insert(Node* n) noexcept { rebalance_from(n->parent); }
    void after_erase(Node* n) noexcept { rebalance_from(n); }
};

}  // namespace dsl

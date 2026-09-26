#pragma once
/// @file binary_search_tree.hpp
/// @brief Ordered map as a binary search tree, plus the shared tree base.

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <ostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

#include "container_base.hpp"

namespace dsl {

namespace detail {

template <class K, class V>
struct TreeNode {
    std::pair<const K, V> kv;
    TreeNode* left = nullptr;
    TreeNode* right = nullptr;
    TreeNode* parent = nullptr;
    int height = 1;  // subtree height; kept up to date by AVLTree only

    template <class... Args>
    explicit TreeNode(Args&&... args) : kv(std::forward<Args>(args)...) {}
};

}  // namespace detail

/// Shared implementation of the ordered maps (BinarySearchTree, AVLTree).
///
/// Holds everything that does not depend on balancing: lookup, ordered
/// iteration, insertion and removal of nodes, copying, and printing. The
/// balanced tree derives from it and adds rebalancing through two hooks,
/// `after_insert(node)` and `after_erase(node)`, which `Derived` may define.
/// They are resolved at compile time (CRTP), so the plain BST pays nothing.
///
/// All traversals are iterative (parent pointers), so a degenerate BST with
/// a million nodes does not overflow the stack when it is copied, destroyed
/// or measured. Only print_shape() recurses, and it is meant for small trees.
template <class K, class V, class Compare, class Derived>
class BstBase : public ContainerBase<Derived> {
protected:
    using Node = detail::TreeNode<K, V>;

    template <bool Const>
    class Iter {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = std::pair<const K, V>;
        using difference_type = std::ptrdiff_t;
        using reference = std::conditional_t<Const, const value_type&, value_type&>;
        using pointer = std::conditional_t<Const, const value_type*, value_type*>;

        Iter() = default;
        template <bool C = Const, class = std::enable_if_t<C>>
        Iter(const Iter<false>& it) noexcept : n_(it.n_), tree_(it.tree_) {}

        reference operator*() const noexcept { return n_->kv; }
        pointer operator->() const noexcept { return &n_->kv; }
        Iter& operator++() noexcept {
            n_ = successor(n_);
            return *this;
        }
        Iter operator++(int) noexcept {
            Iter t = *this;
            ++*this;
            return t;
        }
        Iter& operator--() noexcept {
            n_ = n_ ? predecessor(n_) : max_node(tree_->root_);  // --end() is the last element
            return *this;
        }
        Iter operator--(int) noexcept {
            Iter t = *this;
            --*this;
            return t;
        }
        friend bool operator==(const Iter& a, const Iter& b) noexcept { return a.n_ == b.n_; }

    private:
        friend class BstBase;
        template <bool>
        friend class Iter;
        Iter(Node* n, const BstBase* t) noexcept : n_(n), tree_(t) {}
        Node* n_ = nullptr;
        const BstBase* tree_ = nullptr;
    };

public:
    using key_type = K;
    using mapped_type = V;
    using value_type = std::pair<const K, V>;
    using key_compare = Compare;
    using size_type = std::size_t;
    using iterator = Iter<false>;
    using const_iterator = Iter<true>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    // ---- iteration (in key order) --------------------------------------------

    iterator begin() noexcept { return iterator(min_node(root_), this); }
    iterator end() noexcept { return iterator(nullptr, this); }
    const_iterator begin() const noexcept { return const_iterator(min_node(root_), this); }
    const_iterator end() const noexcept { return const_iterator(nullptr, this); }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend() const noexcept { return end(); }
    reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

    size_type size() const noexcept { return size_; }

    // ---- lookup ---------------------------------------------------------------

    /// O(height).
    iterator find(const K& key) noexcept { return iterator(find_node(key), this); }
    const_iterator find(const K& key) const noexcept { return const_iterator(find_node(key), this); }
    bool contains(const K& key) const noexcept { return find_node(key) != nullptr; }
    size_type count(const K& key) const noexcept { return contains(key) ? 1 : 0; }

    /// Throws std::out_of_range if the key is missing.
    V& at(const K& key) {
        Node* n = find_node(key);
        if (!n) throw std::out_of_range("tree::at: key not found");
        return n->kv.second;
    }
    const V& at(const K& key) const { return const_cast<BstBase*>(this)->at(key); }

    /// First element whose key is not less than `key`.
    iterator lower_bound(const K& key) noexcept { return iterator(lower_bound_node(key), this); }
    const_iterator lower_bound(const K& key) const noexcept { return const_iterator(lower_bound_node(key), this); }

    /// Number of levels (0 for an empty tree). O(n): it walks the whole tree.
    size_type height() const noexcept {
        size_type best = 0, depth = 0;
        const Node* prev = nullptr;
        const Node* n = root_;
        while (n) {
            if (prev == n->parent) {  // arrived from above
                ++depth;
                if (depth > best) best = depth;
                prev = n;
                if (n->left) n = n->left;
                else if (n->right) n = n->right;
                else { n = n->parent; --depth; }
            } else if (prev == n->left && n->right) {
                prev = n;
                n = n->right;
            } else {  // both subtrees done
                prev = n;
                n = n->parent;
                --depth;
            }
        }
        return best;
    }

    // ---- modifiers ------------------------------------------------------------

    /// Inserts (key, value) if the key is absent. Returns the element and
    /// whether it was inserted. Existing values are left unchanged.
    template <class KK, class... Args>
    std::pair<iterator, bool> try_emplace(KK&& key, Args&&... args) {
        auto [n, inserted] = insert_node(std::forward<KK>(key), std::forward<Args>(args)...);
        return {iterator(n, this), inserted};
    }
    std::pair<iterator, bool> insert(const value_type& kv) { return try_emplace(kv.first, kv.second); }
    std::pair<iterator, bool> insert(const K& key, const V& value) { return try_emplace(key, value); }

    template <class VV>
    std::pair<iterator, bool> insert_or_assign(const K& key, VV&& value) {
        auto r = try_emplace(key, std::forward<VV>(value));
        if (!r.second) r.first->second = std::forward<VV>(value);
        return r;
    }

    /// Value for `key`, default-constructing it if absent.
    V& operator[](const K& key) { return try_emplace(key).first->second; }
    V& operator[](K&& key) { return try_emplace(std::move(key)).first->second; }

    /// Removes the key if present. Returns the number removed (0 or 1).
    size_type erase(const K& key) {
        Node* n = find_node(key);
        if (!n) return 0;
        erase_node(n);
        return 1;
    }

    /// Removes the element at pos, returns the next one. Other iterators and
    /// references stay valid: nodes are relinked, never copied.
    iterator erase(const_iterator pos) {
        Node* next = successor(pos.n_);
        erase_node(pos.n_);
        return iterator(next, this);
    }

    void clear() noexcept {
        destroy_all(root_);
        root_ = nullptr;
        size_ = 0;
    }

    void swap(BstBase& o) noexcept {
        using std::swap;
        swap(root_, o.root_);
        swap(size_, o.size_);
        swap(comp_, o.comp_);
    }

    // ---- inspection -------------------------------------------------------------

    /// Draws the tree sideways: the root is on the left, larger keys are
    /// above it and smaller keys below. Needs `operator<<` for K.
    void print_shape(std::ostream& os) const {
        if (!root_) {
            os << "(empty)\n";
            return;
        }
        print_subtree(os, root_, "", false, true);
    }

    /// Visits keys in pre-order (root, left, right): the order that would
    /// rebuild the same shape. Used by the tests and the demo.
    template <class F>
    void preorder(F&& f) const {
        const Node* prev = nullptr;
        const Node* n = root_;
        while (n) {
            if (prev == n->parent) {
                f(n->kv);
                prev = n;
                if (n->left) n = n->left;
                else if (n->right) n = n->right;
                else n = n->parent;
            } else if (prev == n->left && n->right) {
                prev = n;
                n = n->right;
            } else {
                prev = n;
                n = n->parent;
            }
        }
    }

    /// Checks ordering, parent links and the element count. O(n).
    bool validate_structure() const {
        return validate_nodes([](const Node*) { return true; });
    }

protected:
    Node* root_ = nullptr;
    size_type size_ = 0;
    [[no_unique_address]] Compare comp_{};

    BstBase() = default;
    explicit BstBase(const Compare& c) : comp_(c) {}
    BstBase(const BstBase& o) : ContainerBase<Derived>(), root_(clone_tree(o.root_)), size_(o.size_), comp_(o.comp_) {}
    BstBase(BstBase&& o) noexcept
        : root_(std::exchange(o.root_, nullptr)), size_(std::exchange(o.size_, 0)), comp_(o.comp_) {}
    BstBase& operator=(const BstBase& o) {
        if (this != &o) {
            BstBase tmp(o);  // strong guarantee
            swap(tmp);
        }
        return *this;
    }
    BstBase& operator=(BstBase&& o) noexcept {
        if (this != &o) {
            clear();
            swap(o);
        }
        return *this;
    }
    ~BstBase() { destroy_all(root_); }

    /// validate_structure() plus an extra per-node check from the derived tree.
    template <class F>
    bool validate_nodes(F&& check) const {
        size_type seen = 0;
        const Node* prev = nullptr;
        for (const Node* n = min_node(root_); n; n = successor(n)) {
            if (prev && !comp_(prev->kv.first, n->kv.first)) return false;
            if (n->left && n->left->parent != n) return false;
            if (n->right && n->right->parent != n) return false;
            if (!check(n)) return false;
            prev = n;
            ++seen;
        }
        return seen == size_ && (!root_ || !root_->parent);
    }

    static Node* min_node(Node* n) noexcept {
        if (n)
            while (n->left) n = n->left;
        return n;
    }
    static Node* max_node(Node* n) noexcept {
        if (n)
            while (n->right) n = n->right;
        return n;
    }
    static Node* successor(const Node* n) noexcept {
        if (n->right) return min_node(n->right);
        Node* p = n->parent;
        while (p && n == p->right) {
            n = p;
            p = p->parent;
        }
        return p;
    }
    static Node* predecessor(const Node* n) noexcept {
        if (n->left) return max_node(n->left);
        Node* p = n->parent;
        while (p && n == p->left) {
            n = p;
            p = p->parent;
        }
        return p;
    }

    /// A lower_bound descent (one comparison per level, which the compiler
    /// can turn into a conditional move) followed by one equality check.
    /// Measured faster than a three-way branch at every level, whose
    /// outcome the branch predictor cannot guess on random keys.
    Node* find_node(const K& key) const noexcept {
        Node* n = lower_bound_node(key);
        return n && !comp_(key, n->kv.first) ? n : nullptr;
    }

    Node* lower_bound_node(const K& key) const noexcept {
        Node* n = root_;
        Node* best = nullptr;
        while (n) {
            if (comp_(n->kv.first, key)) n = n->right;
            else {
                best = n;
                n = n->left;
            }
        }
        return best;
    }

    /// Standard BST insertion; the derived tree rebalances from the new leaf.
    template <class KK, class... Args>
    std::pair<Node*, bool> insert_node(KK&& key, Args&&... args) {
        Node* parent = nullptr;
        Node** link = &root_;
        while (*link) {
            parent = *link;
            if (comp_(key, parent->kv.first)) link = &parent->left;
            else if (comp_(parent->kv.first, key)) link = &parent->right;
            else return {parent, false};
        }
        Node* n = new Node(std::piecewise_construct, std::forward_as_tuple(std::forward<KK>(key)),
                           std::forward_as_tuple(std::forward<Args>(args)...));
        n->parent = parent;
        *link = n;
        ++size_;
        derived().after_insert(n);
        return {n, true};
    }

    /// Replaces the subtree rooted at u with the one rooted at v.
    void transplant(Node* u, Node* v) noexcept {
        if (!u->parent) root_ = v;
        else if (u == u->parent->left) u->parent->left = v;
        else u->parent->right = v;
        if (v) v->parent = u->parent;
    }

    /// Unlinks and frees z (CLRS deletion, relinking nodes instead of
    /// swapping values so other iterators stay valid). Then calls
    /// after_erase() on the deepest node whose subtree changed.
    void erase_node(Node* z) noexcept {
        Node* fix;
        if (!z->left) {
            fix = z->parent;
            transplant(z, z->right);
        } else if (!z->right) {
            fix = z->parent;
            transplant(z, z->left);
        } else {
            Node* y = min_node(z->right);  // successor, has no left child
            if (y->parent != z) {
                fix = y->parent;
                transplant(y, y->right);
                y->right = z->right;
                y->right->parent = y;
            } else {
                fix = y;
            }
            transplant(z, y);
            y->left = z->left;
            y->left->parent = y;
            y->height = z->height;
        }
        delete z;
        --size_;
        derived().after_erase(fix);
    }

    // Default hooks: a plain BST does nothing after insert or erase.
    void after_insert(Node*) noexcept {}
    void after_erase(Node*) noexcept {}

private:
    Derived& derived() noexcept { return static_cast<Derived&>(*this); }

    static void destroy_all(Node* n) noexcept {
        // Post-order deletion using parent pointers: O(n) time, O(1) space.
        while (n) {
            if (n->left) n = n->left;
            else if (n->right) n = n->right;
            else {
                Node* p = n->parent;
                if (p) (p->left == n ? p->left : p->right) = nullptr;
                delete n;
                n = p;
            }
        }
    }

    static Node* clone_node(const Node* s, Node* parent) {
        Node* d = new Node(s->kv);
        d->height = s->height;
        d->parent = parent;
        return d;
    }

    /// Iterative pre-order copy. On an allocation or copy failure the part
    /// built so far is freed and the exception propagates.
    static Node* clone_tree(const Node* src) {
        if (!src) return nullptr;
        Node* root = clone_node(src, nullptr);
        try {
            const Node* s = src;
            Node* d = root;
            for (;;) {
                if (s->left && !d->left) {
                    d->left = clone_node(s->left, d);
                    s = s->left;
                    d = d->left;
                } else if (s->right && !d->right) {
                    d->right = clone_node(s->right, d);
                    s = s->right;
                    d = d->right;
                } else if (s == src) {
                    break;
                } else {
                    s = s->parent;
                    d = d->parent;
                }
            }
        } catch (...) {
            destroy_all(root);
            throw;
        }
        return root;
    }

    static void print_subtree(std::ostream& os, const Node* n, const std::string& prefix, bool lower, bool root) {
        if (n->right) print_subtree(os, n->right, prefix + (root ? "" : lower ? "│   " : "    "), false, false);
        os << prefix << (root ? "" : lower ? "└── " : "┌── ");
        detail::print_element(os, n->kv.first);
        os << '\n';
        if (n->left) print_subtree(os, n->left, prefix + (root ? "" : lower ? "    " : "│   "), true, false);
    }
};

/// Unbalanced binary search tree used as an ordered map.
///
/// Operations are O(height): O(log n) on random input, O(n) on sorted input,
/// where it degenerates into a linked list. It exists as the baseline the
/// AVL tree is compared against.
template <class K, class V, class Compare = std::less<K>>
class BinarySearchTree : public BstBase<K, V, Compare, BinarySearchTree<K, V, Compare>> {
    using Base = BstBase<K, V, Compare, BinarySearchTree<K, V, Compare>>;
    friend Base;

public:
    BinarySearchTree() = default;
    explicit BinarySearchTree(const Compare& c) : Base(c) {}
    BinarySearchTree(std::initializer_list<typename Base::value_type> il) {
        for (const auto& kv : il) this->insert(kv);
    }

    bool validate() const { return this->validate_structure(); }
};

}  // namespace dsl

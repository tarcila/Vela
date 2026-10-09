// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/ObjectPool.hpp"
#include "vsr/core/TypeMacros.hpp"
// std
#include <cstdint>
#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

namespace vsr::core {

template <typename T>
struct Forest;

// Optional value-specific storage relocation, not a public payload edit.
template <typename T>
struct ForestStorageTraits
{
  static T relocate(T &&value) noexcept(
      std::is_nothrow_move_constructible_v<T>);
  static void relocateAssign(T &destination, T &&source) noexcept(
      std::is_nothrow_move_assignable_v<T>);
};

/*
 * Intrusive tree node that stores a value and maintains parent, sibling, and
 * child references within an owning Forest; accessed via ObjectPoolRef handles.
 *
 * Example:
 *   auto child = node->insert_last_child(MyValue{});
 *   child->value().name = "leaf";
 *   child->erase_self();
 */
template <typename T>
struct ForestNode
{
  using Ref = ObjectPoolRef<ForestNode<T>>;

  ForestNode() = default;
  ForestNode(T initialValue, Forest<T> *f);
  VSR_DEFAULT_COPYABLE(ForestNode)
  ForestNode(ForestNode &&o) noexcept(
      noexcept(ForestStorageTraits<T>::relocate(std::move(o.m_value))));
  ForestNode &operator=(ForestNode &&o) noexcept(noexcept(
      ForestStorageTraits<T>::relocateAssign(m_value, std::move(o.m_value))));

  T &value();
  const T &value() const;

  T &operator*();
  const T &operator*() const;
  T *operator->();
  const T *operator->() const;
  operator bool() const;

  bool isRoot() const;
  bool isLeaf() const;

  Forest<T> *container() const;

  Ref next() const;
  Ref parent() const;
  Ref sibling() const;
  Ref prev() const;

  size_t index() const;

  // Creation stamp within this Forest, independent of pool slot reuse.
  // Storage relocation and move_subtree preserve it; copies get a new stamp.
  std::uint64_t lifetime() const
  {
    return m_lifetime;
  }

  // Latest creation/reparent event on this node, not sibling list changes.
  // The Forest clock orders events so a max over a chain detects change-back.
  std::uint64_t topologyVersion() const
  {
    return m_topologyVersion;
  }

  Ref insert_first_child(T &&v);
  Ref insert_last_child(T &&v);
  Ref insert_last_child_at(size_t index, T &&v);

  void erase_subtree();
  void erase_self(); // remove this node from the forest

 private:
  Ref self() const;
  Forest<T> *forest() const;

  friend struct Forest<T>;
  template <typename U>
  friend bool operator==(const ForestNode<U> &a, const ForestNode<U> &b);

  std::uint64_t m_lifetime{0};
  std::uint64_t m_topologyVersion{0};
  T m_value{};
  Ref m_prev;
  Ref m_children_begin;
  Ref m_children_end;
  Ref m_next;
  Ref m_parent;
  Ref m_self;
  Forest<T> *m_forest{nullptr};
};

template <typename T>
using ForestNodeRef = ObjectPoolRef<ForestNode<T>>;

template <typename T>
bool operator==(const ForestNode<T> &a, const ForestNode<T> &b);
template <typename T>
bool operator!=(const ForestNode<T> &a, const ForestNode<T> &b);

// clang-format off
/*
 * Virtual visitor base for Forest traversal; override preChildren/postChildren
 * to execute logic before and after descending into each node's children.
 *
 * Example:
 *   struct Printer : ForestVisitor<int> {
 *     bool preChildren(ForestNode<int> &n, int lvl) override {
 *       std::cout << *n << '\n'; return true;
 *     }
 *   };
 *   Printer p; forest.traverse(forest.root(), p);
 */
template <typename T>
struct ForestVisitor
{
  virtual ~ForestVisitor() = default;
  virtual bool preChildren(ForestNode<T> &n, int level);
  virtual void postChildren(ForestNode<T> &n, int level);
  virtual bool preChildren_const(const ForestNode<T> &n, int level);
  virtual void postChildren_const(const ForestNode<T> &n, int level);
};
// clang-format on

template <typename T>
using ForestVisitorEntryFunction =
    std::function<bool(ForestNode<T> &n, int level)>;
template <typename T>
using ForestVisitorExitFunction =
    std::function<void(ForestNode<T> &n, int level)>;

template <typename T>
using ConstForestVisitorEntryFunction =
    std::function<bool(const ForestNode<T> &n, int level)>;
template <typename T>
using ConstForestVisitorExitFunction =
    std::function<void(const ForestNode<T> &n, int level)>;

/*
 * Cycle-free tree container (based on stlab::forest) backed by an ObjectPool;
 * supports multi-level traversal, subtree copy/move, and ancestor queries.
 * See: https://stlab.cc/2020/12/01/forest-introduction.html
 *
 * Example:
 *   Forest<std::string> f("root");
 *   auto child = f.insert_last_child(f.root(), "child");
 *   f.traverse(f.root(), [](auto &n, int){ std::cout << *n; return true; });
 */
template <typename T>
struct Forest
{
  using Ref = ObjectPoolRef<Forest<T>>;
  using Node = ForestNode<T>;
  using NodeRef = typename ForestNode<T>::Ref;
  using Visitor = ForestVisitor<T>;

  // Optional destination binding after successful storage, including root.
  // Does not run for allocation gaps, rejected slots or storage relocation.
  using ValueBinding = std::function<void(T &)>;

  // Optional successful membership edits, separate from storage binding.
  // Root construction, reserve, gaps and rejected slots are not edits.
  using MembershipChange = std::function<void()>;

  // Optional copy preflight, before any destination storage or membership
  // edits. Generic values need no validation; clients may reject incompatible
  // owners.
  using CopyValidation = std::function<void(const T &)>;

  Forest(T &&initialRootValue,
      ValueBinding bindValue = {},
      MembershipChange membershipChange = {},
      CopyValidation validateCopy = {});
  Forest() = delete;
  ~Forest() = default;

  VSR_NOT_COPYABLE(Forest)
  VSR_NOT_MOVEABLE(Forest)

  void reserve(size_t size);
  bool profileStorage(StorageProfile &profile, size_t reserveSize) const;

  // ForestNode access //

  size_t size() const;
  size_t capacity() const; // to get highest possible node index
  bool empty() const;

  NodeRef at(size_t i) const;
  T &operator[](size_t i);
  const T &operator[](size_t i) const;

  NodeRef root() const;

  // A short-lived append plan may predict creation stamps without issuing
  // live nodes. Recheck the cursor at commit; intervening insertions stale it.
  std::uint64_t next_lifetime() const;
  bool can_issue_nodes(size_t count) const;

  // Mutation //

  NodeRef insert_first_child(NodeRef n, T &&v);
  NodeRef insert_last_child(NodeRef n, T &&v);
  // Append a child that occupies node slot `index`, so a forest rebuilt from
  // a record of another forest keeps that forest's node indices; returns an
  // invalid ref when the slot is occupied.
  NodeRef insert_last_child_at(NodeRef n, size_t index, T &&v);

  void erase(NodeRef n);
  void erase_subtree(NodeRef n);
  NodeRef copy_subtree(NodeRef source, NodeRef newParent);
  void move_subtree(NodeRef source, NodeRef newParent);

  void clear();

  // Queries //

  bool isAncestorOf(NodeRef potentialAncestor, NodeRef node) const;

  // Traversal //

  void traverse(NodeRef start, ForestVisitor<T> &visitor);
  void traverse(NodeRef start, ForestVisitorEntryFunction<T> &&f);
  void traverse(NodeRef start,
      ForestVisitorEntryFunction<T> &&onNodeEntry,
      ForestVisitorExitFunction<T> &&onNodeExit);

  void traverse_const(NodeRef start, ForestVisitor<T> &visitor) const;
  void traverse_const(
      NodeRef start, ConstForestVisitorEntryFunction<T> &&f) const;
  void traverse_const(NodeRef start,
      ConstForestVisitorEntryFunction<T> &&onNodeEntry,
      ConstForestVisitorExitFunction<T> &&onNodeExit) const;

 private:
  void traverse_impl(NodeRef n, ForestVisitor<T> &visitor, int level);
  void traverse_impl_const(
      NodeRef n, ForestVisitor<T> &visitor, int level) const;
  NodeRef make_ForestNode(T &&v);
  NodeRef make_ForestNode_at(size_t index, T &&v);
  void link_last_child(NodeRef n, NodeRef newForestNode);
  void noteMembershipChange();
  NodeRef copy_subtree_impl(NodeRef source, NodeRef newParent);

  ValueBinding m_bindValue;
  MembershipChange m_membershipChange;
  CopyValidation m_validateCopy;
  ObjectPool<ForestNode<T>> m_nodes;
  NodeRef m_root;
  std::uint64_t m_nextLifetime{1};
  std::uint64_t m_topologyClock{0};
  std::uint64_t nextTopologyVersion()
  {
    if (++m_topologyClock == 0)
      std::terminate();
    return m_topologyClock;
  }
};

// Algorithms /////////////////////////////////////////////////////////////////

template <typename T, typename FCN>
void foreach_child(ForestNodeRef<T> node, FCN &&fcn);

template <typename T, typename FCN>
void forall_children(ForestNodeRef<T> node, FCN &&fcn);

template <typename T, typename PREDICATE>
ForestNodeRef<T> find_first_child(ForestNodeRef<T> node, PREDICATE &&pred);

///////////////////////////////////////////////////////////////////////////////
// Inlined definitions ////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

template <typename T>
inline T ForestStorageTraits<T>::relocate(T &&value) noexcept(
    std::is_nothrow_move_constructible_v<T>)
{
  return T(std::move(value));
}

template <typename T>
inline void ForestStorageTraits<T>::relocateAssign(
    T &destination, T &&source) noexcept(std::is_nothrow_move_assignable_v<T>)
{
  destination = std::move(source);
}

// ForestNode<> //

template <typename T>
inline ForestNode<T>::ForestNode(ForestNode &&o) noexcept(
    noexcept(ForestStorageTraits<T>::relocate(std::move(o.m_value))))
    : m_lifetime(o.m_lifetime),
      m_topologyVersion(o.m_topologyVersion),
      m_value(ForestStorageTraits<T>::relocate(std::move(o.m_value))),
      m_prev(o.m_prev),
      m_children_begin(o.m_children_begin),
      m_children_end(o.m_children_end),
      m_next(o.m_next),
      m_parent(o.m_parent),
      m_self(o.m_self),
      m_forest(o.m_forest)
{}

template <typename T>
inline ForestNode<T> &ForestNode<T>::operator=(ForestNode &&o) noexcept(
    noexcept(
        ForestStorageTraits<T>::relocateAssign(m_value, std::move(o.m_value))))
{
  if (this == &o)
    return *this;
  ForestStorageTraits<T>::relocateAssign(m_value, std::move(o.m_value));
  m_lifetime = o.m_lifetime;
  m_topologyVersion = o.m_topologyVersion;
  m_prev = o.m_prev;
  m_children_begin = o.m_children_begin;
  m_children_end = o.m_children_end;
  m_next = o.m_next;
  m_parent = o.m_parent;
  m_self = o.m_self;
  m_forest = o.m_forest;
  return *this;
}

template <typename T>
inline ForestNode<T>::ForestNode(T v, Forest<T> *f)
    : m_value(std::move(v)), m_forest(f)
{}

template <typename T>
inline T &ForestNode<T>::value()
{
  return m_value;
}

template <typename T>
inline const T &ForestNode<T>::value() const
{
  return m_value;
}

template <typename T>
inline T &ForestNode<T>::operator*()
{
  return value();
}

template <typename T>
inline const T &ForestNode<T>::operator*() const
{
  return value();
}

template <typename T>
inline T *ForestNode<T>::operator->()
{
  return &m_value;
}

template <typename T>
inline const T *ForestNode<T>::operator->() const
{
  return &m_value;
}

template <typename T>
inline ForestNode<T>::operator bool() const
{
  return m_self;
}

template <typename T>
inline bool ForestNode<T>::isRoot() const
{
  return *this && !m_parent;
}

template <typename T>
inline bool ForestNode<T>::isLeaf() const
{
  return *this && m_children_begin == self() && m_children_end == self();
}

template <typename T>
Forest<T> *ForestNode<T>::container() const
{
  return m_forest;
}

template <typename T>
inline typename ForestNode<T>::Ref ForestNode<T>::next() const
{
  return m_children_begin != self() ? m_children_begin : m_next;
}

template <typename T>
inline typename ForestNode<T>::Ref ForestNode<T>::parent() const
{
  return m_parent;
}

template <typename T>
inline typename ForestNode<T>::Ref ForestNode<T>::sibling() const
{
  return m_next;
}

template <typename T>
inline typename ForestNode<T>::Ref ForestNode<T>::prev() const
{
  return m_prev;
}

template <typename T>
inline size_t ForestNode<T>::index() const
{
  return self().index();
}

template <typename T>
inline typename ForestNode<T>::Ref ForestNode<T>::insert_first_child(T &&v)
{
  if (auto *f = forest(); f != nullptr)
    return f->insert_first_child(self(), std::forward<T>(v));
  else
    return {};
}

template <typename T>
inline typename ForestNode<T>::Ref ForestNode<T>::insert_last_child(T &&v)
{
  if (auto *f = forest(); f != nullptr)
    return f->insert_last_child(self(), std::forward<T>(v));
  else
    return {};
}

template <typename T>
inline typename ForestNode<T>::Ref ForestNode<T>::insert_last_child_at(
    size_t index, T &&v)
{
  if (auto *f = forest(); f != nullptr)
    return f->insert_last_child_at(self(), index, std::forward<T>(v));
  else
    return {};
}

template <typename T>
inline void ForestNode<T>::erase_subtree()
{
  if (auto *f = forest(); f != nullptr)
    f->erase_subtree(self());
}

template <typename T>
inline void ForestNode<T>::erase_self()
{
  if (auto *f = forest(); f != nullptr && self())
    f->erase(self());
}

template <typename T>
inline typename ForestNode<T>::Ref ForestNode<T>::self() const
{
  return m_self;
}

template <typename T>
inline Forest<T> *ForestNode<T>::forest() const
{
  return m_forest;
}

template <typename T>
inline bool operator==(const ForestNode<T> &a, const ForestNode<T> &b)
{
  return a.self() == b.self();
}

template <typename T>
inline bool operator!=(const ForestNode<T> &a, const ForestNode<T> &b)
{
  return !(a == b);
}

// ForestVisitor<> //

template <typename T>
inline bool ForestVisitor<T>::preChildren(ForestNode<T> &, int)
{
  return true;
}

template <typename T>
inline void ForestVisitor<T>::postChildren(ForestNode<T> &, int)
{}

template <typename T>
inline bool ForestVisitor<T>::preChildren_const(const ForestNode<T> &, int)
{
  return true;
}

template <typename T>
inline void ForestVisitor<T>::postChildren_const(const ForestNode<T> &, int)
{}

// Forest<> //

template <typename T>
inline Forest<T>::Forest(T &&v,
    ValueBinding bindValue,
    MembershipChange membershipChange,
    CopyValidation validateCopy)
    : m_bindValue(std::move(bindValue)),
      m_membershipChange(std::move(membershipChange)),
      m_validateCopy(std::move(validateCopy))
{
  m_root = make_ForestNode(std::forward<T>(v));
}

template <typename T>
inline bool Forest<T>::profileStorage(
    StorageProfile &profile, size_t reserveSize) const
{
  return m_nodes.profileStorage(profile, reserveSize);
}

template <typename T>
inline void Forest<T>::reserve(size_t size)
{
  m_nodes.reserve(size);
}

template <typename T>
inline size_t Forest<T>::size() const
{
  return m_nodes.size();
}

template <typename T>
inline size_t Forest<T>::capacity() const
{
  return m_nodes.capacity();
}

template <typename T>
inline bool Forest<T>::empty() const
{
  return size() == 1;
}

template <typename T>
inline typename Forest<T>::NodeRef Forest<T>::at(size_t i) const
{
  return m_nodes.at(i);
}

template <typename T>
inline T &Forest<T>::operator[](size_t i)
{
  return *m_nodes[i];
}

template <typename T>
inline const T &Forest<T>::operator[](size_t i) const
{
  return *m_nodes[i];
}

template <typename T>
inline typename Forest<T>::NodeRef Forest<T>::root() const
{
  return m_root;
}

template <typename T>
inline std::uint64_t Forest<T>::next_lifetime() const
{
  return m_nextLifetime;
}

template <typename T>
inline bool Forest<T>::can_issue_nodes(size_t count) const
{
  return !count
      || (m_nextLifetime && count - 1 <= UINT64_MAX - m_nextLifetime
          && count <= UINT64_MAX - m_topologyClock);
}

template <typename T>
inline typename Forest<T>::NodeRef Forest<T>::insert_first_child(
    Forest<T>::NodeRef n, T &&v)
{
  auto newForestNode = make_ForestNode(std::forward<T>(v));
  newForestNode->m_parent = n;
  newForestNode->m_prev = n->self();
  newForestNode->m_next = n->m_children_begin;
  if (n->isLeaf())
    n->m_children_end = newForestNode;
  else
    n->m_children_begin->m_prev = newForestNode;
  n->m_children_begin = newForestNode;
  noteMembershipChange();
  return newForestNode;
}

template <typename T>
inline typename Forest<T>::NodeRef Forest<T>::insert_last_child(
    Forest<T>::NodeRef n, T &&v)
{
  auto newForestNode = make_ForestNode(std::forward<T>(v));
  link_last_child(n, newForestNode);
  return newForestNode;
}

template <typename T>
inline typename Forest<T>::NodeRef Forest<T>::insert_last_child_at(
    Forest<T>::NodeRef n, size_t index, T &&v)
{
  auto newForestNode = make_ForestNode_at(index, std::forward<T>(v));
  if (!newForestNode)
    return {};
  link_last_child(n, newForestNode);
  return newForestNode;
}

template <typename T>
inline void Forest<T>::link_last_child(
    Forest<T>::NodeRef n, Forest<T>::NodeRef newForestNode)
{
  newForestNode->m_parent = n;
  newForestNode->m_prev = n->m_children_end;
  newForestNode->m_next = n->self();
  if (n->isLeaf())
    n->m_children_begin = newForestNode;
  else
    n->m_children_end->m_next = newForestNode;
  n->m_children_end = newForestNode;
  noteMembershipChange();
}

template <typename T>
inline void Forest<T>::noteMembershipChange()
{
  if (m_membershipChange)
    m_membershipChange();
}

template <typename T>
inline void Forest<T>::erase(Forest<T>::NodeRef n)
{
  erase_subtree(n);

  if (n->isRoot())
    return;

  auto self = n->self();
  auto prev = n->prev();
  auto next = n->next();

  const bool prevIsParent = self == prev->m_children_begin;
  if (prevIsParent)
    prev->m_children_begin = next;
  else
    prev->m_next = next;

  const bool nextIsParent = self == next->m_children_end;
  if (nextIsParent)
    next->m_children_end = prev;
  else
    next->m_prev = prev;

  m_nodes.erase(n.index());
  noteMembershipChange();
}

template <typename T>
inline void Forest<T>::erase_subtree(Forest<T>::NodeRef n)
{
  if (n->isLeaf())
    return;

  struct SubtreeIndices : public ForestVisitor<T>
  {
    bool preChildren(ForestNode<T> &node, int level) override
    {
      if (level != 0)
        indices.push_back(node.self().index());
      return true;
    }

    std::vector<size_t> indices;
  };

  SubtreeIndices visitor;
  traverse(n, visitor);

  for (auto &i : visitor.indices)
    m_nodes.erase(i);

  n->m_children_begin = n;
  n->m_children_end = n;
  noteMembershipChange();
}

template <typename T>
inline typename Forest<T>::NodeRef Forest<T>::copy_subtree(
    Forest<T>::NodeRef source, Forest<T>::NodeRef newParent)
{
  if (!source || !newParent)
    return NodeRef();

  if (source->isRoot())
    return NodeRef();

  if (newParent->container() != this)
    return NodeRef();

  if (source == newParent) {
    newParent = newParent->parent();
  }

  if (m_validateCopy) {
    source->container()->traverse_const(source, [&](const auto &node, int) {
      m_validateCopy(node.value());
      return true;
    });
  }
  return copy_subtree_impl(source, newParent);
}

template <typename T>
inline typename Forest<T>::NodeRef Forest<T>::copy_subtree_impl(
    Forest<T>::NodeRef source, Forest<T>::NodeRef newParent)
{
  auto newNode = insert_last_child(newParent, T(source->value()));

  if (!source->isLeaf()) {
    auto child = source->next();
    while (child && child != source) {
      copy_subtree_impl(child, newNode);
      child = child->sibling();
    }
  }

  return newNode;
}

template <typename T>
inline bool Forest<T>::isAncestorOf(
    Forest<T>::NodeRef potentialAncestor, Forest<T>::NodeRef node) const
{
  if (!potentialAncestor || !node)
    return false;

  auto current = node;
  while (current.valid()) {
    if (current == potentialAncestor)
      return true;
    current = current->parent();
  }

  return false;
}

template <typename T>
inline void Forest<T>::move_subtree(
    Forest<T>::NodeRef source, Forest<T>::NodeRef newParent)
{
  if (!source || !newParent || source == newParent)
    return;

  // Reparent preserves storage and therefore cannot cross Forest ownership.
  if (source->container() != this || newParent->container() != this)
    return;

  if (source->isRoot())
    return;

  if (isAncestorOf(source, newParent))
    return;

  // If already a child of newParent, no-op
  if (source->parent() == newParent)
    return;

  // Detach source from its current parent
  // Use m_next (sibling) directly, not next() which may return first child
  auto self = source->self();
  auto prev = source->prev();
  auto nextSibling = source->sibling();

  const bool prevIsParent = self == prev->m_children_begin;
  if (prevIsParent)
    prev->m_children_begin = nextSibling;
  else
    prev->m_next = nextSibling;

  const bool nextIsParent = self == nextSibling->m_children_end;
  if (nextIsParent)
    nextSibling->m_children_end = prev;
  else
    nextSibling->m_prev = prev;

  // Attach source to newParent as last child
  source->m_topologyVersion = nextTopologyVersion();
  source->m_parent = newParent;
  source->m_prev = newParent->m_children_end;
  source->m_next = newParent->self();
  if (newParent->isLeaf())
    newParent->m_children_begin = source;
  else
    newParent->m_children_end->m_next = source;
  newParent->m_children_end = source;
  noteMembershipChange();
}

template <typename T>
inline void Forest<T>::clear()
{
  erase_subtree(m_root);
}

template <typename T>
inline void Forest<T>::traverse(
    Forest<T>::NodeRef start, ForestVisitor<T> &visitor)
{
  traverse_impl(start, visitor, 0);
}

template <typename T>
inline void Forest<T>::traverse(
    Forest<T>::NodeRef start, ForestVisitorEntryFunction<T> &&f)
{
  struct FcnVisitor : public ForestVisitor<T>
  {
    FcnVisitor(ForestVisitorEntryFunction<T> &f) : fcn(f) {}
    bool preChildren(vsr::core::ForestNode<T> &node, int level) override
    {
      return fcn(node, level);
    }
    ForestVisitorEntryFunction<T> &fcn;
  };

  FcnVisitor visitor(f);
  traverse_impl(start, visitor, 0);
}

template <typename T>
inline void Forest<T>::traverse(Forest<T>::NodeRef start,
    ForestVisitorEntryFunction<T> &&onNodeEntry,
    ForestVisitorExitFunction<T> &&onNodeExit)
{
  struct FcnVisitor : public ForestVisitor<T>
  {
    FcnVisitor(
        ForestVisitorEntryFunction<T> &f1, ForestVisitorExitFunction<T> &f2)
        : onEntry(f1), onExit(f2)
    {}
    bool preChildren(ForestNode<T> &node, int level) override
    {
      return onEntry(node, level);
    }
    void postChildren(ForestNode<T> &node, int level) override
    {
      onExit(node, level);
    }
    ForestVisitorEntryFunction<T> &onEntry;
    ForestVisitorExitFunction<T> &onExit;
  };

  FcnVisitor visitor(onNodeEntry, onNodeExit);
  traverse_impl(start, visitor, 0);
}

template <typename T>
inline void Forest<T>::traverse_const(
    Forest<T>::NodeRef start, ForestVisitor<T> &visitor) const
{
  traverse_impl_const(start, visitor, 0);
}

template <typename T>
inline void Forest<T>::traverse_const(
    Forest<T>::NodeRef start, ConstForestVisitorEntryFunction<T> &&f) const
{
  struct FcnVisitor : public ForestVisitor<T>
  {
    FcnVisitor(ConstForestVisitorEntryFunction<T> &f) : fcn(f) {}
    bool preChildren_const(const ForestNode<T> &node, int level) override
    {
      return fcn(node, level);
    }
    ConstForestVisitorEntryFunction<T> &fcn;
  };

  FcnVisitor visitor(f);
  traverse_impl_const(start, visitor, 0);
}

template <typename T>
inline void Forest<T>::traverse_const(Forest<T>::NodeRef start,
    ConstForestVisitorEntryFunction<T> &&onNodeEntry,
    ConstForestVisitorExitFunction<T> &&onNodeExit) const
{
  struct FcnVisitor : public ForestVisitor<T>
  {
    FcnVisitor(ConstForestVisitorEntryFunction<T> &f1,
        ConstForestVisitorExitFunction<T> &f2)
        : onEntry(f1), onExit(f2)
    {}
    bool preChildren_const(const ForestNode<T> &node, int level) override
    {
      return onEntry(node, level);
    }
    void postChildren_const(const ForestNode<T> &node, int level) override
    {
      onExit(node, level);
    }
    ConstForestVisitorEntryFunction<T> &onEntry;
    ConstForestVisitorExitFunction<T> &onExit;
  };

  FcnVisitor visitor(onNodeEntry, onNodeExit);
  traverse_impl_const(start, visitor, 0);
}

template <typename T>
inline void Forest<T>::traverse_impl(
    NodeRef n, ForestVisitor<T> &visitor, int level)
{
  const bool traverseChildren = visitor.preChildren(*n, level);
  if (traverseChildren && !n->isLeaf()) {
    for (auto s = n->next(); s && s != n; s = s->sibling())
      traverse_impl(s, visitor, level + 1);
  }
  visitor.postChildren(*n, level);
}

template <typename T>
inline void Forest<T>::traverse_impl_const(
    NodeRef n, ForestVisitor<T> &visitor, int level) const
{
  const bool traverseChildren = visitor.preChildren_const(*n, level);
  if (traverseChildren && !n->isLeaf()) {
    for (auto s = n->next(); s && s != n; s = s->sibling())
      traverse_impl_const(s, visitor, level + 1);
  }
  visitor.postChildren_const(*n, level);
}

template <typename T>
inline typename Forest<T>::NodeRef Forest<T>::make_ForestNode(T &&v)
{
  auto n = m_nodes.emplace(std::forward<T>(v), this);
  n->m_topologyVersion = nextTopologyVersion();
  if (m_nextLifetime == 0)
    std::terminate();
  n->m_lifetime = m_nextLifetime++;
  n->m_self = n;
  n->m_children_end = n;
  n->m_children_begin = n;
  if (m_bindValue)
    m_bindValue(n->value());
  return n;
}

template <typename T>
inline typename Forest<T>::NodeRef Forest<T>::make_ForestNode_at(
    size_t index, T &&v)
{
  auto n = m_nodes.emplace_at(index, std::forward<T>(v), this);
  if (!n)
    return {};
  n->m_topologyVersion = nextTopologyVersion();
  if (m_nextLifetime == 0)
    std::terminate();
  n->m_lifetime = m_nextLifetime++;
  n->m_self = n;
  n->m_children_end = n;
  n->m_children_begin = n;
  if (m_bindValue)
    m_bindValue(n->value());
  return n;
}

// Algorithms //

template <typename T, typename FCN>
inline void foreach_child(ForestNodeRef<T> node, FCN &&fcn)
{
  if (auto *forest = node->container(); forest != nullptr) {
    forest->traverse(node, [&](auto &v, int level) {
      if (level != 0)
        fcn(*v);
      return level == 0;
    });
  }
}

template <typename T, typename FCN>
inline void foreach_child_const(ForestNodeRef<T> node, FCN &&fcn)
{
  if (auto *forest = node->container(); forest != nullptr) {
    forest->traverse_const(node, [&](const auto &v, int level) {
      if (level != 0)
        fcn(*v);
      return level == 0;
    });
  }
}

template <typename T, typename FCN>
inline void forall_children(ForestNodeRef<T> node, FCN &&fcn)
{
  if (auto *forest = node->container(); forest != nullptr) {
    forest->traverse(node, [&](auto &v, int level) {
      if (level != 0)
        fcn(*v);
      return true;
    });
  }
}

template <typename T, typename FCN>
inline void forall_children_const(ForestNodeRef<T> node, FCN &&fcn)
{
  if (auto *forest = node->container(); forest != nullptr) {
    forest->traverse_const(node, [&](const auto &v, int level) {
      if (level != 0)
        fcn(*v);
      return true;
    });
  }
}

template <typename T, typename PREDICATE>
inline ForestNodeRef<T> find_first_child(ForestNodeRef<T> n, PREDICATE &&p)
{
  for (auto s = n->next(); s && s != n; s = s->sibling()) {
    if (p(**s))
      return s;
  }
  return {};
}

} // namespace vsr::core
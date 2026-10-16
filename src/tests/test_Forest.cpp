// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/core/Forest.hpp"
// std
#include <stdexcept>
#include <vector>

TEST_CASE(
    "Forest child-list history survives change-back and relocation", "[Forest]")
{
  vsr::core::Forest<int> tree(-1);
  auto root = tree.root();
  CHECK(root->childrenVersion() == 1);
  auto left = tree.insert_last_child(root, 1);
  auto right = tree.insert_first_child(root, 2);
  CHECK(root->childrenVersion() == 3);
  auto child = tree.insert_last_child(left, 3);
  CHECK(left->childrenVersion() == 2);
  CHECK(right->childrenVersion() == 1);
  tree.move_subtree(child, right);
  CHECK(left->childrenVersion() == 3);
  CHECK(right->childrenVersion() == 2);
  tree.move_subtree(child, left);
  CHECK(left->childrenVersion() == 4);
  CHECK(right->childrenVersion() == 3);
  tree.erase(child);
  CHECK(left->childrenVersion() == 5);
  CHECK(left->isLeaf());
  tree.reserve(4096);
  CHECK(left->childrenVersion() == 5);
  CHECK(right->childrenVersion() == 3);
  CHECK(root->childrenVersion() == 3);
  tree.erase_subtree(root);
  CHECK(root->childrenVersion() == 4);
  tree.clear();
  CHECK(root->childrenVersion() == 4); // Empty clear is not a child-list edit.
}

SCENARIO("Forest value binding is optional and only binds stored members",
    "[Forest]")
{
  int bindings = 0;
  vsr::core::Forest<int> tree(-1, [&](int &value) {
    ++bindings;
    value *= 10;
  });
  CHECK(bindings == 1);
  CHECK(tree.root()->value() == -10);
  auto first = tree.root()->insert_first_child(2);
  auto last = tree.insert_last_child(tree.root(), 3);
  auto chosen = tree.root()->insert_last_child_at(20, 4);
  CHECK(bindings == 4);
  CHECK(first->value() == 20);
  CHECK(last->value() == 30);
  CHECK(chosen->value() == 40);
  CHECK(tree.size() == 4);
  CHECK_FALSE(tree.at(10));
  CHECK_FALSE(tree.root()->insert_last_child_at(20, 5));
  CHECK_FALSE(tree.root()->insert_last_child_at(VSR_INVALID_INDEX, 5));
  tree.reserve(4096);
  CHECK(bindings == 4);
  tree.erase(first);
  auto reused = tree.root()->insert_last_child(6);
  CHECK(reused.index() == first.index());
  CHECK(reused->value() == 60);
  CHECK(bindings == 5);
  tree.clear();
  CHECK(tree.size() == 1);
  CHECK(tree.root()->value() == -10);
  CHECK(bindings == 5);
}

SCENARIO("Forest membership instrumentation is optional and allocation neutral",
    "[Forest]")
{
  using vsr::core::Forest;
  Forest<int> *observed = nullptr;
  std::vector<size_t> sizes;
  int bindings = 0;
  Forest<int> tree(
      -1,
      [&](int &) { ++bindings; },
      [&]() {
        REQUIRE(observed);
        sizes.push_back(observed->size());
      });
  observed = &tree;
  CHECK(bindings == 1);
  CHECK(sizes.empty());
  CHECK(tree.root()->isRoot());
  tree.reserve(1024);
  tree.clear();
  tree.erase(tree.root());
  CHECK(sizes.empty());
  auto first = tree.root()->insert_first_child(2);
  auto last = tree.insert_last_child_at(tree.root(), 8, 3);
  CHECK(tree.root()->next() == first);
  CHECK(first->sibling() == last);
  CHECK(last->sibling() == tree.root());
  CHECK(sizes == std::vector<size_t>{2, 3});
  CHECK(bindings == 3);
  for (size_t i = 2; i < 8; ++i)
    CHECK_FALSE(tree.at(i));
  CHECK_FALSE(tree.root()->insert_last_child_at(8, 4));
  CHECK_FALSE(tree.root()->insert_last_child_at(VSR_INVALID_INDEX, 4));
  tree.erase_subtree(first);
  tree.reserve(2048);
  CHECK(sizes == std::vector<size_t>{2, 3});
  CHECK(bindings == 3);
  first->erase_self();
  CHECK(sizes == std::vector<size_t>{2, 3, 2});
  tree.clear();
  CHECK(sizes == std::vector<size_t>{2, 3, 2, 1});
  CHECK(tree.root()->value() == -1);
  CHECK(bindings == 3);
  tree.clear();
  CHECK(sizes.size() == 4);
}

SCENARIO("Forest copy validation is optional and precedes all clone edits",
    "[Forest]")
{
  using vsr::core::Forest;
  Forest<int> source(-1);
  auto branch = source.root()->insert_last_child(2);
  branch->insert_last_child(3);
  branch->insert_last_child(4);
  int bindings = 0;
  int changes = 0;
  std::vector<int> validated;
  Forest<int> destination(
      -1,
      [&](int &) { ++bindings; },
      [&]() { ++changes; },
      [&](const int &value) {
        validated.push_back(value);
        if (value == 4)
          throw std::runtime_error("rejected value");
      });
  CHECK(bindings == 1);
  CHECK_THROWS_AS(
      destination.copy_subtree(branch, destination.root()), std::runtime_error);
  CHECK(validated == std::vector<int>{2, 3, 4});
  CHECK(destination.size() == 1);
  CHECK(destination.root()->isLeaf());
  CHECK(bindings == 1);
  CHECK(changes == 0);
  validated.clear();
  source.erase(branch->next()->sibling());
  auto clone = destination.copy_subtree(branch, destination.root());
  REQUIRE(clone);
  CHECK(validated == std::vector<int>{2, 3});
  CHECK(clone->value() == 2);
  CHECK(clone->next()->value() == 3);
  CHECK(clone->parent() == destination.root());
  CHECK(destination.size() == 3);
  CHECK(bindings == 3);
  CHECK(changes == 2);
  auto sibling = destination.root()->insert_last_child(5);
  destination.move_subtree(clone, sibling);
  CHECK(clone->parent() == sibling);
  CHECK(changes == 4);
  destination.move_subtree(clone, sibling);
  CHECK(changes == 4);

  Forest<int> plain(-10);
  auto plainCopy = plain.copy_subtree(branch, plain.root());
  REQUIRE(plainCopy);
  CHECK(plainCopy->value() == 2);
  CHECK(plainCopy->next()->value() == 3);
  CHECK(plain.size() == 3);
  CHECK(source.size() == 3);
}

SCENARIO("vsr::core::Forest<> interface", "[Forest]")
{
  GIVEN("A normally constructed Forest")
  {
    vsr::core::Forest<int> f{-1};

    THEN("The Forest is empty")
    {
      REQUIRE(f.empty());
    }

    THEN("An out-of-bounds node is invalid")
    {
      REQUIRE(!f.at(1));
    }

    THEN("The first node is the root")
    {
      REQUIRE(f.at(0)->isRoot());
      REQUIRE(*f.at(0) == f.root());
      REQUIRE(!f.root()->parent());
    }

    THEN("The first node is the root value")
    {
      REQUIRE(f[0] == -1);
      REQUIRE(f.root()->value() == -1);
    }

    THEN("The root node is a leaf node")
    {
      REQUIRE(f.root()->isLeaf());
    }

    WHEN("A child is pushed on the front of the root")
    {
      auto c = f.insert_first_child(f.root(), 3);

      THEN("The forest size is now 2")
      {
        REQUIRE(f.size() == 2);
      }

      THEN("The root node value is unchanged")
      {
        REQUIRE(f.root()->value() == -1);
      }

      THEN("The next node after root points back to the parent")
      {
        REQUIRE(c->parent() == f.root());
        REQUIRE(f.root()->next()->parent() == f.root());
      }

      THEN("The next node after root holds the new value")
      {
        REQUIRE(f.root()->next());
        REQUIRE(f.root()->next()->value() == 3);
      }

      THEN("The root node is no longer a leaf but is still the root")
      {
        REQUIRE(!f.root()->isLeaf());
        REQUIRE(f.root()->isRoot());
      }

      THEN("Erasing the new node returns the forest to being empty")
      {
        f.erase(c);
        REQUIRE(f.empty());
        REQUIRE(f.root()->isLeaf());
      }

      WHEN("Another child is prepended")
      {
        auto c2 = f.insert_first_child(f.root(), 5);
        THEN("The next node after root holds the new value")
        {
          REQUIRE(f.root()->next());
          REQUIRE(f.root()->next()->value() == 5);
        }

        THEN("The next node after the first child holds the new value")
        {
          REQUIRE(f.root()->next()->next());
          REQUIRE(f.root()->next()->next()->value() == 3);
        }

        THEN("The second child points back to the root")
        {
          REQUIRE(c2->parent() == f.root());
          REQUIRE(f.root()->next()->next()->next());
          REQUIRE(f.root() == f.root()->next()->next()->next());
        }

        THEN("Erasing all children from the root empties the forest")
        {
          f.root()->erase_subtree();
          REQUIRE(f.empty());
          REQUIRE(f.root()->isLeaf());
        }
      }

      WHEN("Another child is appended")
      {
        auto c3 = f.insert_last_child(f.root(), 5);
        THEN("The next node after root holds the new value")
        {
          REQUIRE(f.root()->next());
          REQUIRE(f.root()->next()->value() == 3);
        }

        THEN("The next node after the first child holds the new value")
        {
          REQUIRE(f.root()->next()->next());
          REQUIRE(f.root()->next()->next()->value() == 5);
        }

        THEN("The second child points back to the root")
        {
          REQUIRE(c3->parent() == f.root());
          REQUIRE(f.root()->next()->next()->next());
          REQUIRE(f.root() == f.root()->next()->next()->next());
        }
      }
    }
  }
}

SCENARIO("vsr::core::Forest<> reparent operations", "[Forest]")
{
  GIVEN("A Forest with a hierarchical structure")
  {
    vsr::core::Forest<int> f{0}; // root = 0
    auto child1 = f.insert_last_child(f.root(), 1);
    auto child2 = f.insert_last_child(f.root(), 2);
    auto grandchild1 = f.insert_last_child(child1, 11);
    auto grandchild2 = f.insert_last_child(child1, 12);

    THEN("Initial structure is correct")
    {
      REQUIRE(f.size() == 5);
      REQUIRE(child1->parent() == f.root());
      REQUIRE(child2->parent() == f.root());
      REQUIRE(grandchild1->parent() == child1);
      REQUIRE(grandchild2->parent() == child1);
    }

    WHEN("A leaf node is reparented")
    {
      f.move_subtree(grandchild1, child2);

      THEN("The leaf node has a new parent")
      {
        REQUIRE(grandchild1->parent() == child2);
      }

      THEN("The old parent no longer has this child")
      {
        REQUIRE(child1->next() == grandchild2);
      }

      THEN("The new parent has the child")
      {
        REQUIRE(child2->next() == grandchild1);
      }

      THEN("Forest size is unchanged")
      {
        REQUIRE(f.size() == 5);
      }
    }

    WHEN("A node with children is reparented")
    {
      f.move_subtree(child1, child2);

      THEN("The subtree moves together")
      {
        REQUIRE(child1->parent() == child2);
        REQUIRE(grandchild1->parent() == child1);
        REQUIRE(grandchild2->parent() == child1);
      }

      THEN("The new parent has the child")
      {
        REQUIRE(child2->next() == child1);
      }

      THEN("Forest size is unchanged")
      {
        REQUIRE(f.size() == 5);
      }
    }

    WHEN("Attempting to reparent to a descendant")
    {
      f.move_subtree(child1, grandchild1);

      THEN("The operation is prevented (circular dependency)")
      {
        REQUIRE(child1->parent() == f.root());
        REQUIRE(grandchild1->parent() == child1);
      }
    }

    WHEN("Attempting to reparent to the same parent")
    {
      f.move_subtree(child1, f.root());

      THEN("The operation is a no-op")
      {
        REQUIRE(child1->parent() == f.root());
        REQUIRE(f.size() == 5);
      }
    }

    WHEN("Attempting to reparent to itself")
    {
      f.move_subtree(child1, child1);

      THEN("The operation is prevented")
      {
        REQUIRE(child1->parent() == f.root());
        REQUIRE(f.size() == 5);
      }
    }

    WHEN("Attempting to reparent the root")
    {
      f.move_subtree(f.root(), child1);

      THEN("The operation is prevented")
      {
        REQUIRE(f.root()->isRoot());
        REQUIRE(!f.root()->parent());
      }
    }

    WHEN("Multiple nodes are reparented in sequence")
    {
      f.move_subtree(grandchild1, child2);
      f.move_subtree(grandchild2, child2);

      THEN("Both nodes have the new parent")
      {
        REQUIRE(grandchild1->parent() == child2);
        REQUIRE(grandchild2->parent() == child2);
      }

      THEN("The old parent has no children")
      {
        REQUIRE(child1->isLeaf());
      }

      THEN("Forest size is unchanged")
      {
        REQUIRE(f.size() == 5);
      }
    }
  }

  GIVEN("Edge case: null node refs")
  {
    vsr::core::Forest<int> f{0};
    auto child = f.insert_last_child(f.root(), 1);

    WHEN("Reparenting with null child")
    {
      vsr::core::ForestNodeRef<int> nullNode;
      f.move_subtree(nullNode, child);

      THEN("The operation is safely ignored")
      {
        REQUIRE(f.size() == 2);
      }
    }

    WHEN("Reparenting to null parent")
    {
      vsr::core::ForestNodeRef<int> nullNode;
      f.move_subtree(child, nullNode);

      THEN("The operation is safely ignored")
      {
        REQUIRE(child->parent() == f.root());
      }
    }
  }
}

SCENARIO("vsr::core::Forest<> rebuild with chosen node indices", "[Forest]")
{
  GIVEN("A forest whose node slots are sparse")
  {
    vsr::core::Forest<int> source{-1};
    source.insert_last_child(source.root(), 1);
    auto doomed = source.insert_last_child(source.root(), 2);
    auto kept = source.insert_last_child(source.root(), 3);
    source.erase(doomed);
    REQUIRE(kept.index() == 3);
    REQUIRE(source.size() == 3);

    WHEN("Another forest is built placing each child at the source's index")
    {
      vsr::core::Forest<int> rebuilt{-1};
      auto first = rebuilt.insert_last_child_at(rebuilt.root(), 1, 1);
      auto third = rebuilt.root()->insert_last_child_at(3, 3);

      THEN("The node indices match the source")
      {
        REQUIRE(first.index() == 1);
        REQUIRE(third.index() == 3);
        REQUIRE(rebuilt.size() == 3);
        REQUIRE(rebuilt.capacity() == 4);
        REQUIRE(!rebuilt.at(2));
        REQUIRE(rebuilt.at(3)->value() == kept->value());
      }

      THEN("The children are linked in insertion order under the root")
      {
        REQUIRE(third->parent() == rebuilt.root());
        REQUIRE(rebuilt.root()->next() == first);
        REQUIRE(first->sibling() == third);
        REQUIRE(third->sibling() == rebuilt.root());
      }

      THEN("An occupied slot is refused without changing the tree")
      {
        REQUIRE(!rebuilt.insert_last_child_at(rebuilt.root(), 1, 9));
        REQUIRE(rebuilt.size() == 3);
        REQUIRE(*first == 1);
      }

      THEN("Erasing and re-placing round-trips the slot")
      {
        rebuilt.erase(first);
        REQUIRE(!rebuilt.at(1));
        auto again = rebuilt.insert_last_child_at(third, 1, 7);
        REQUIRE(again.index() == 1);
        REQUIRE(again->parent() == third);
        REQUIRE(rebuilt.size() == 3);
      }
    }
  }
}

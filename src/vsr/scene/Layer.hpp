// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scene
#include "vsr/scene/LayerNodeData.hpp"

namespace vsr::scene {

struct Scene;

namespace detail {
class PreparedSurfaceStorage;
}

/*
 * Named Forest-based hierarchy of LayerNodeData nodes owned by a Scene;
 * nodes carry transforms or Object references and can be traversed, queried
 * for ancestry, and toggled active per-layer.
 *
 * Example:
 *   auto *layer = scene.addLayer("main");
 *   auto child = scene.insertChildObjectNode(layer->root(), surfaceRef);
 *   layer->traverse(layer->root(), myVisitor);
 */
struct Layer final
{
  using Visitor = ForestVisitor<LayerNodeData>;
  using VisitorEntryFunction = ForestVisitorEntryFunction<LayerNodeData>;
  using VisitorExitFunction = ForestVisitorExitFunction<LayerNodeData>;
  using ConstVisitorEntryFunction =
      ConstForestVisitorEntryFunction<LayerNodeData>;
  using ConstVisitorExitFunction =
      ConstForestVisitorExitFunction<LayerNodeData>;

  Layer(Scene *scene, std::string name = "<unnamed layer>");
  ~Layer();

  Scene *scene() const;

  // Process-local creation stamp, never reused. Not an external handle.
  std::uint64_t lifetime() const
  {
    return m_lifetime;
  }

  const std::string &name() const;
  std::uint64_t metadataSequence() const;
  void setName(std::string name);

  // Nodes within the layer //

  size_t size() const;
  size_t capacity() const;
  bool profileStorage(StorageProfile &profile, size_t reserveSize) const;
  bool empty() const;

  LayerNodeRef root() const;
  LayerNodeRef at(size_t i) const;
  void erase(LayerNodeRef obj);
  void clear();

  bool isAncestorOf(LayerNodeRef potentialAncestor, LayerNodeRef node) const;

  void traverse(LayerNodeRef start, Visitor &visitor);
  void traverse(LayerNodeRef start, VisitorEntryFunction &&f);
  void traverse(LayerNodeRef start,
      VisitorEntryFunction &&onNodeEntry,
      VisitorExitFunction &&onNodeExit);

  void traverse_const(LayerNodeRef start, Visitor &visitor) const;
  void traverse_const(LayerNodeRef start, ConstVisitorEntryFunction &&f) const;
  void traverse_const(LayerNodeRef start,
      ConstVisitorEntryFunction &&onNodeEntry,
      ConstVisitorExitFunction &&onNodeExit) const;

 private:
  void noteMembershipChange();
  void noteMetadataChange();
  void revokeMembership();
  void validateCopy(const LayerNodeData &value) const;

  friend struct Scene; // Scene removal revokes tracking, not object resolution.
  friend class detail::PreparedSurfaceStorage;
  friend class detail::PreparedHdriContent;
  friend class detail::PreparedSurfaceContent;
  friend class detail::PreparedSurfaceRemoval;
  friend class detail::PreparedHdriRemoval;
  friend struct LayerNodeData;
  const std::uint64_t m_lifetime;
  std::shared_ptr<detail::LayerOwnerState> m_owner;
  std::string m_name;
  std::uint64_t m_metadataSequence{1};
  Forest<LayerNodeData> m_tree;
};

inline bool Layer::profileStorage(
    StorageProfile &profile, size_t reserveSize) const
{
  return m_tree.profileStorage(profile, reserveSize);
}

using LayerVisitor = Layer::Visitor;

} // namespace vsr::scene

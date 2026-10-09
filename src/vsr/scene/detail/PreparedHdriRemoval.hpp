// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scene
#include "vsr/scene/Scene.hpp"
// std
#include <exception>
#include <optional>
#include <vector>

namespace vsr::scene::detail {

/* Exact two-node HDRI removal at an owning-thread safe point. Eligibility is
 * the integration caller's responsibility. Preparation owns empty pool values,
 * free-index capacity and callback tombstones; commit has no callbacks/default
 * Array construction. Tombstones keep the removed data alive until notification
 * completes, so the caller must cover their temporary physical ownership.
 * No pooled references survive preparation, and owner death revokes the plan.
 */
class PreparedHdriRemoval
{
 public:
  PreparedHdriRemoval(Scene &scene,
      LayerNodeRef root,
      LayerNodeRef lightNode,
      size_t lightIndex,
      size_t arrayIndex,
      bool ownsLayer);
  VSR_NOT_COPYABLE(PreparedHdriRemoval)
  VSR_NOT_MOVEABLE(PreparedHdriRemoval)

  static std::weak_ptr<SceneDependencyState> sceneLifetime(const Scene &scene);
  static std::weak_ptr<LayerOwnerState> layerLifetime(const Layer &layer);
  bool isCurrent() const;
  bool committed() const;
  const Light *removedLight() const;
  const Array *removedArray() const;
  const Layer *removedLayer() const;
  bool commit();
  void notify();

 private:
  std::weak_ptr<SceneDependencyState> m_scene;
  std::weak_ptr<LayerOwnerState> m_layer;
  std::weak_ptr<ArrayLifetime> m_arrayLife;
  std::vector<std::weak_ptr<BaseUpdateDelegate>> m_delegates;
  size_t m_rootIndex{0};
  size_t m_nodeIndex{0};
  size_t m_lightIndex{0};
  size_t m_arrayIndex{0};
  std::uint64_t m_rootLife{0};
  std::uint64_t m_nodeLife{0};
  std::uint64_t m_revision{0};
  Token m_layerKey;
  LayerPtr m_ownedLayer;
  bool m_active{false};
  Array m_emptyArray;
  Light m_emptyLight;
  std::optional<Array> m_removedArray;
  std::optional<Light> m_removedLight;
  bool m_committed{false};
  bool m_notified{false};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline std::weak_ptr<SceneDependencyState> PreparedHdriRemoval::sceneLifetime(
    const Scene &scene)
{
  return scene.m_dependencyState;
}

inline std::weak_ptr<LayerOwnerState> PreparedHdriRemoval::layerLifetime(
    const Layer &layer)
{
  return layer.m_owner;
}

inline PreparedHdriRemoval::PreparedHdriRemoval(Scene &scene,
    LayerNodeRef root,
    LayerNodeRef lightNode,
    size_t lightIndex,
    size_t arrayIndex,
    bool ownsLayer)
    : m_scene(scene.m_dependencyState),
      m_rootIndex(root.index()),
      m_nodeIndex(lightNode.index()),
      m_lightIndex(lightIndex),
      m_arrayIndex(arrayIndex)
{
  if (!root || !lightNode || !root->value().layer()
      || root->value().layer()->scene() != &scene || lightNode->parent() != root
      || !lightNode->isLeaf())
    throw std::runtime_error("Invalid prepared HDRI removal");
  auto *layer = root->value().layer();
  m_layer = layer->m_owner;
  m_rootLife = root->lifetime();
  m_nodeLife = lightNode->lifetime();
  auto array = scene.getObject<Array>(arrayIndex);
  if (!array || !scene.getObject<Light>(lightIndex) || array->isMapped())
    throw std::runtime_error("Unavailable HDRI removal objects");
  m_arrayLife = array->lifetime();
  m_revision = scene.m_dependencyState->revision;
  m_delegates.reserve(scene.m_updateDelegate.m_delegates.size());
  for (const auto &delegate : scene.m_updateDelegate.m_delegates)
    m_delegates.emplace_back(delegate);
  if (ownsLayer) {
    m_layerKey = scene.getLayerName(layer);
    const auto *state = scene.m_layers.at(m_layerKey);
    if (!state || state->ptr.get() != layer || layer->size() != 3
        || root->parent() != layer->root())
      throw std::runtime_error("Invalid owned HDRI Layer closure");
    m_ownedLayer = state->ptr;
    m_active = state->active;
  }
  scene.m_db.array.reserve(scene.m_db.array.capacity());
  scene.m_db.light.reserve(scene.m_db.light.capacity());
  layer->m_tree.reserve(layer->capacity());
}

inline bool PreparedHdriRemoval::isCurrent() const
{
  const auto scene = m_scene.lock();
  const auto layer = m_layer.lock();
  const auto array = m_arrayLife.lock();
  if (m_committed || !scene || !scene->scene || !layer || !layer->layer
      || !array || !array->array || !layer->dependencyAttached
      || scene->revision != m_revision || array->array->isMapped()
      || array->array->index() != m_arrayIndex)
    return false;
  auto *s = scene->scene;
  auto *l = layer->layer;
  auto root = l->at(m_rootIndex);
  auto node = l->at(m_nodeIndex);
  if (!root || !node || root->lifetime() != m_rootLife
      || node->lifetime() != m_nodeLife || node->parent() != root
      || !node->isLeaf() || root->next() != node || node->sibling() != root
      || !s->m_db.array.can_erase_reserved(1)
      || !s->m_db.light.can_erase_reserved(1)
      || !s->getObject<Light>(m_lightIndex))
    return false;
  const auto &delegates = s->m_updateDelegate.m_delegates;
  if (delegates.size() != m_delegates.size())
    return false;
  for (size_t i = 0; i < delegates.size(); ++i)
    if (m_delegates[i].lock() != delegates[i])
      return false;
  return true;
}

inline bool PreparedHdriRemoval::committed() const
{
  return m_committed;
}

inline const Light *PreparedHdriRemoval::removedLight() const
{
  return m_removedLight ? &*m_removedLight : nullptr;
}

inline const Array *PreparedHdriRemoval::removedArray() const
{
  return m_removedArray ? &*m_removedArray : nullptr;
}

inline const Layer *PreparedHdriRemoval::removedLayer() const
{
  return m_ownedLayer.get();
}

inline bool PreparedHdriRemoval::commit()
{
  if (!isCurrent())
    return false;
  auto *s = m_scene.lock()->scene;
  auto *layer = m_layer.lock()->layer;
  auto *light = s->m_db.light.at(m_lightIndex).data();
  auto *array = s->m_db.array.at(m_arrayIndex).data();
  // The exact closure was checked by the caller. Balance its internal uses
  // without fallible zero-use notifications inside the irreversible boundary.
  auto &node = layer->at(m_nodeIndex)->value();
  node.m_layerOwner.reset();
  node.m_sceneOwner.reset();
  node.m_attached = false;
  light->m_useCounts.layer = 0;
  array->m_useCounts.parameter = 0;
  light->m_scene = nullptr;
  light->m_updateDelegate = nullptr;
  array->m_scene = nullptr;
  array->m_updateDelegate = nullptr;
  m_removedLight.emplace(std::move(*light));
  m_removedArray.emplace(std::move(*array));
  layer->m_tree.erase(layer->at(m_nodeIndex));
  layer->m_tree.erase(layer->at(m_rootIndex));
  s->m_db.light.erase_prepared(m_lightIndex, std::move(m_emptyLight));
  s->m_db.array.erase_prepared(m_arrayIndex, std::move(m_emptyArray));
  if (m_ownedLayer) {
    layer->revokeMembership();
    if (m_active)
      --s->m_numActiveLayers;
    s->m_layers.erase(m_layerKey);
  }
  s->m_dependencyState->noteChange();
  m_committed = true;
  return true;
}

inline void PreparedHdriRemoval::notify()
{
  if (!m_committed || m_notified)
    return;
  const auto scene = m_scene.lock();
  const auto owner = m_layer.lock();
  if (!scene || !scene->scene || !owner || !owner->layer)
    return;
  m_notified = true;
  std::exception_ptr first;
  const auto signal = [&](auto &&callback) {
    try {
      callback();
    } catch (...) {
      if (!first)
        first = std::current_exception();
    }
  };
  for (const auto &weak : m_delegates)
    if (const auto delegate = weak.lock())
      signal([&] { delegate->signalUpdateBatchBegin(); });
  for (const auto &weak : m_delegates) {
    const auto delegate = weak.lock();
    if (!delegate)
      continue;
    signal([&] { delegate->signalObjectRemoved(&*m_removedLight); });
    signal([&] { delegate->signalObjectRemoved(&*m_removedArray); });
    if (m_ownedLayer)
      signal([&] { delegate->signalLayerRemoved(m_ownedLayer.get()); });
    else
      signal([&] { delegate->signalLayerStructureUpdated(owner->layer); });
  }
  for (const auto &weak : m_delegates)
    if (const auto delegate = weak.lock())
      signal([&] { delegate->signalUpdateBatchEnd(); });
  if (first)
    std::rethrow_exception(first);
}

} // namespace vsr::scene::detail

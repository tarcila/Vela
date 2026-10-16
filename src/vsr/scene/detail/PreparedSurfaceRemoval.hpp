// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scene
#include "vsr/scene/detail/PreparedHdriRemoval.hpp"
// std
#include <array>

namespace vsr::scene::detail {

/* Final safe-point removal only. The integration layer checks the complete
 * alias-aware closure before construction and again before commit. All empty
 * replacements, free lists and tombstone owners exist before writes. */
class PreparedSurfaceRemoval
{
 public:
  PreparedSurfaceRemoval(Scene &scene,
      LayerNodeRef root,
      LayerNodeRef node,
      size_t geometry,
      size_t material,
      size_t surface,
      const std::array<size_t, 64> &arrays,
      size_t count,
      bool ownsLayer);
  VSR_NOT_COPYABLE(PreparedSurfaceRemoval)
  VSR_NOT_MOVEABLE(PreparedSurfaceRemoval)
  bool isCurrent() const;
  bool committed() const;
  const Object *removed(size_t index) const;
  const Layer *removedLayer() const;
  bool commit();
  void notify();

 private:
  std::weak_ptr<SceneDependencyState> m_scene;
  std::weak_ptr<LayerOwnerState> m_layer;
  std::vector<std::weak_ptr<BaseUpdateDelegate>> m_delegates;
  std::array<size_t, 64> m_indices{};
  size_t m_count{0}, m_geometryIndex{0}, m_materialIndex{0}, m_surfaceIndex{0};
  size_t m_rootIndex{0}, m_nodeIndex{0};
  std::uint64_t m_rootLife{0}, m_nodeLife{0}, m_revision{0};
  LayerPtr m_ownedLayer;
  Token m_layerKey;
  bool m_active{false}, m_committed{false}, m_notified{false};
  std::vector<Array> m_emptyArrays;
  std::vector<Array> m_arrays;
  Geometry m_emptyGeometry;
  Material m_emptyMaterial;
  Surface m_emptySurface;
  std::optional<Geometry> m_geometry;
  std::optional<Material> m_material;
  std::optional<Surface> m_surface;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline PreparedSurfaceRemoval::PreparedSurfaceRemoval(Scene &scene,
    LayerNodeRef root,
    LayerNodeRef node,
    size_t geometry,
    size_t material,
    size_t surface,
    const std::array<size_t, 64> &arrays,
    size_t count,
    bool ownsLayer)
    : m_scene(scene.m_dependencyState),
      m_indices(arrays),
      m_count(count),
      m_geometryIndex(geometry),
      m_materialIndex(material),
      m_surfaceIndex(surface),
      m_rootIndex(root.index()),
      m_nodeIndex(node.index())
{
  if (!count || count > 64 || !root || !node || !root->value().layer()
      || root->value().layer()->scene() != &scene || node->parent() != root
      || root->next() != node || node->sibling() != root || !node->isLeaf())
    throw std::runtime_error("Invalid Surface removal closure");
  auto *layer = root->value().layer();
  m_layer = layer->m_owner;
  m_rootLife = root->lifetime();
  m_nodeLife = node->lifetime();
  m_revision = scene.m_dependencyState->revision;
  for (const auto &d : scene.m_updateDelegate.m_delegates)
    m_delegates.emplace_back(d);
  m_emptyArrays.reserve(count);
  m_arrays.reserve(count);
  for (size_t i = 0; i < count; ++i)
    m_emptyArrays.emplace_back();
  if (ownsLayer) {
    m_layerKey = scene.getLayerName(layer);
    const auto *state = scene.m_layers.at(m_layerKey);
    if (!state || state->ptr.get() != layer || layer->size() != 3)
      throw std::runtime_error("Invalid owned Surface Layer");
    m_ownedLayer = state->ptr;
    m_active = state->active;
  }
  scene.m_db.array.reserve(scene.m_db.array.capacity());
  scene.m_db.geometry.reserve(scene.m_db.geometry.capacity());
  scene.m_db.material.reserve(scene.m_db.material.capacity());
  scene.m_db.surface.reserve(scene.m_db.surface.capacity());
  layer->m_tree.reserve(layer->capacity());
}
inline bool PreparedSurfaceRemoval::isCurrent() const
{
  const auto s = m_scene.lock();
  const auto l = m_layer.lock();
  if (m_committed || !s || !s->scene || !l || !l->layer
      || !l->dependencyAttached || s->revision != m_revision)
    return false;
  const auto root = l->layer->at(m_rootIndex);
  const auto node = l->layer->at(m_nodeIndex);
  if (!root || !node || root->lifetime() != m_rootLife
      || node->lifetime() != m_nodeLife || node->parent() != root
      || root->next() != node || node->sibling() != root || !node->isLeaf())
    return false;
  auto &db = s->scene->m_db;
  if (!db.array.can_erase_reserved(m_count)
      || !db.geometry.can_erase_reserved(1)
      || !db.material.can_erase_reserved(1) || !db.surface.can_erase_reserved(1)
      || !db.geometry.at(m_geometryIndex) || !db.material.at(m_materialIndex)
      || !db.surface.at(m_surfaceIndex))
    return false;
  for (size_t i = 0; i < m_count; ++i)
    if (!db.array.at(m_indices[i]) || db.array.at(m_indices[i])->isMapped())
      return false;
  const auto &now = s->scene->m_updateDelegate.m_delegates;
  if (now.size() != m_delegates.size())
    return false;
  for (size_t i = 0; i < now.size(); ++i)
    if (now[i] != m_delegates[i].lock())
      return false;
  return true;
}
inline bool PreparedSurfaceRemoval::committed() const
{
  return m_committed;
}
inline const Object *PreparedSurfaceRemoval::removed(size_t index) const
{
  if (!m_committed)
    return nullptr;
  if (index < m_count)
    return &m_arrays[index];
  if (index == m_count)
    return &*m_geometry;
  if (index == m_count + 1)
    return &*m_material;
  return index == m_count + 2 ? &*m_surface : nullptr;
}
inline const Layer *PreparedSurfaceRemoval::removedLayer() const
{
  return m_ownedLayer.get();
}
inline bool PreparedSurfaceRemoval::commit()
{
  if (!isCurrent())
    return false;
  auto *s = m_scene.lock()->scene;
  auto *l = m_layer.lock()->layer;
  auto &node = l->at(m_nodeIndex)->value();
  node.m_layerOwner.reset();
  node.m_sceneOwner.reset();
  node.m_attached = false;
  const auto detach = [](Object &o) {
    o.m_scene = nullptr;
    o.m_updateDelegate = nullptr;
    o.m_useCounts = {};
  };
  for (size_t i = 0; i < m_count; ++i) {
    auto *a = s->m_db.array.at(m_indices[i]).data();
    detach(*a);
    m_arrays.emplace_back(std::move(*a));
    s->m_db.array.erase_prepared(m_indices[i], std::move(m_emptyArrays[i]));
  }
  auto *g = s->m_db.geometry.at(m_geometryIndex).data();
  auto *m = s->m_db.material.at(m_materialIndex).data();
  auto *surface = s->m_db.surface.at(m_surfaceIndex).data();
  detach(*g);
  detach(*m);
  detach(*surface);
  m_geometry.emplace(std::move(*g));
  m_material.emplace(std::move(*m));
  m_surface.emplace(std::move(*surface));
  l->m_tree.erase(l->at(m_nodeIndex));
  l->m_tree.erase(l->at(m_rootIndex));
  s->m_db.geometry.erase_prepared(m_geometryIndex, std::move(m_emptyGeometry));
  s->m_db.material.erase_prepared(m_materialIndex, std::move(m_emptyMaterial));
  s->m_db.surface.erase_prepared(m_surfaceIndex, std::move(m_emptySurface));
  if (m_ownedLayer) {
    l->revokeMembership();
    if (m_active)
      --s->m_numActiveLayers;
    s->m_layers.erase(m_layerKey);
  }
  s->m_dependencyState->noteChange();
  m_committed = true;
  return true;
}
inline void PreparedSurfaceRemoval::notify()
{
  if (!m_committed || m_notified)
    return;
  const auto s = m_scene.lock();
  const auto l = m_layer.lock();
  if (!s || !s->scene || !l || !l->layer)
    return;
  m_notified = true;
  std::exception_ptr first;
  const auto signal = [&](auto &&f) {
    try {
      f();
    } catch (...) {
      if (!first)
        first = std::current_exception();
    }
  };
  for (const auto &w : m_delegates)
    if (const auto d = w.lock())
      signal([&] { d->signalUpdateBatchBegin(); });
  for (const auto &w : m_delegates) {
    const auto d = w.lock();
    if (!d)
      continue;
    for (size_t i = 0; i < m_count + 3; ++i)
      signal([&] { d->signalObjectRemoved(removed(i)); });
    if (m_ownedLayer)
      signal([&] { d->signalLayerRemoved(m_ownedLayer.get()); });
    else
      signal([&] { d->signalLayerStructureUpdated(l->layer); });
  }
  for (const auto &w : m_delegates)
    if (const auto d = w.lock())
      signal([&] { d->signalUpdateBatchEnd(); });
  if (first)
    std::rethrow_exception(first);
}

} // namespace vsr::scene::detail

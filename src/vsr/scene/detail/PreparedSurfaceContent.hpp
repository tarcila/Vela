// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scene
#include "vsr/scene/detail/PreparedHdriContent.hpp"
// std
#include <array>

namespace vsr::integration::content {
class PreparedSurfacePublication;
}

namespace vsr::scene::detail {

/* Owning-thread structural seam only. The source is a validated private
 * triangle closure, not an external graph-authoring interface. Copies and
 * remapping are detached; reserve may relocate live pools but never publishes
 * objects. The caller owes authority, input currency, charged reservations,
 * identity/recovery enrollment and synchronization ownership before the first
 * write. */
class PreparedSurfaceContent
{
 public:
  PreparedSurfaceContent(Scene &scene,
      LayerNodeRef parent,
      const Geometry &geometry,
      const Material &material,
      const LayerNodeData &root,
      Token newLayerName = {});
  VSR_NOT_COPYABLE(PreparedSurfaceContent)
  VSR_NOT_MOVEABLE(PreparedSurfaceContent)
  static bool profileStorage(Scene &scene,
      LayerNodeRef parent,
      const Geometry &geometry,
      const Material &material,
      bool newLayer,
      StorageProfile &profile);
  bool isCurrent() const;
  bool committed() const;
  size_t surfaceIndex() const;
  LayerNodeRef root() const;
  LayerNodeRef node() const;
  bool commit();
  void notify();

 private:
  friend class ::vsr::integration::content::PreparedSurfacePublication;
  bool delegatesCurrent(const Scene &scene) const;
  std::weak_ptr<SceneDependencyState> m_scene;
  std::weak_ptr<LayerOwnerState> m_layer;
  std::vector<std::weak_ptr<BaseUpdateDelegate>> m_delegates;
  LayerPtr m_newLayer;
  Token m_layerName;
  std::optional<LayerMap> m_layers;
  struct Ancestor
  {
    size_t index;
    std::uint64_t lifetime, topology, edit;
  };
  std::vector<Ancestor> m_ancestors;
  std::vector<Array> m_arrays;
  std::vector<size_t> m_arrayUses;
  Geometry m_geometry{tokens::geometry::triangle};
  Material m_material{tokens::material::matte};
  Surface m_surface;
  LayerNodeData m_rootData;
  LayerNodeData m_nodeData;
  size_t m_arrayIndex{0}, m_geometryIndex{0}, m_materialIndex{0},
      m_surfaceIndex{0};
  size_t m_parentIndex{0}, m_extent{0};
  std::uint64_t m_rootLife{0}, m_revision{0};
  bool m_committed{false}, m_notified{false};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline PreparedSurfaceContent::PreparedSurfaceContent(Scene &scene,
    LayerNodeRef parent,
    const Geometry &geometry,
    const Material &material,
    const LayerNodeData &rootData,
    Token newLayerName)
    : m_scene(scene.m_dependencyState),
      m_layerName(newLayerName),
      m_rootData(nullptr, rootData.getTransform(), rootData.name().c_str()),
      m_nodeData(nullptr, "Surface")
{
  if (geometry.subtype() != tokens::geometry::triangle || !geometry.scene()
      || material.scene() != geometry.scene()
      || material.subtype() != tokens::material::matte
      || !rootData.isTransform() || geometry.numParameters() > 64)
    throw std::runtime_error("Invalid private Surface closure");
  for (const auto &delegate : scene.m_updateDelegate.m_delegates)
    m_delegates.emplace_back(delegate);
  if (!parent && !newLayerName) {
    if (!scene.numberOfLayers())
      newLayerName = m_layerName = Token("default");
    else
      parent = scene.layer(0)->root();
  }
  if (newLayerName) {
    if (parent || scene.layer(newLayerName))
      throw std::runtime_error("Surface Layer must be new");
    m_newLayer = std::make_shared<Layer>(&scene, newLayerName.str());
    m_newLayer->m_owner->dependencyAttached = false;
    m_layers = scene.m_layers;
    m_layers->set(newLayerName, LayerState{m_newLayer, true});
    parent = m_newLayer->root();
  }
  if (!parent || !parent->value().layer()
      || parent->value().layer()->scene() != &scene)
    throw std::runtime_error("Invalid Surface destination");
  auto *layer = parent->value().layer();
  m_layer = layer->m_owner;
  m_parentIndex = parent.index();
  for (auto n = parent;; n = n->parent()) {
    if (m_ancestors.size() == 1024)
      throw std::runtime_error("Surface ancestry exceeds bounds");
    m_ancestors.push_back({n.index(),
        n->lifetime(),
        n->topologyVersion(),
        n->value().editVersion()});
    if (n->isRoot())
      break;
  }
  m_arrayIndex = scene.m_db.array.capacity();
  m_geometryIndex = scene.m_db.geometry.capacity();
  m_materialIndex = scene.m_db.material.capacity();
  m_surfaceIndex = scene.m_db.surface.capacity();
  m_extent = layer->capacity();
  m_rootLife = layer->m_tree.next_lifetime();
  if (!layer->m_tree.can_issue_nodes(2))
    throw std::runtime_error("Surface node identity exhausted");
  std::array<const Array *, 64> sources{};
  m_arrays.reserve(geometry.numParameters());
  m_arrayUses.reserve(geometry.numParameters());
  for (size_t i = 0; i < geometry.numParameters(); ++i) {
    const auto *name = geometry.parameterNameAt(i);
    const auto &p = geometry.parameterAt(i);
    const auto *a = geometry.parameterValueAsObject<Array>(name);
    if (!a || !a->isHost() || a->isMapped() || a->type() != ANARI_ARRAY1D
        || anari::isObject(a->elementType()))
      throw std::runtime_error("Invalid private Surface Array");
    size_t index = 0;
    while (index < m_arrays.size() && sources[index] != a)
      ++index;
    if (index == m_arrays.size()) {
      sources[index] = a;
      m_arrays.emplace_back(a->elementType(), a->size());
      m_arrays.back().setData(a->data());
      if (scene.m_imageWrites
          && !m_arrays.back().trackImageWrites(scene.m_imageWrites))
        throw std::runtime_error("Surface write tracking unavailable");
      m_arrayUses.push_back(0);
    }
    ++m_arrayUses[index];
    m_geometry.setParameter(name, Any(ANARI_ARRAY1D, m_arrayIndex + index));
    m_geometry.parameter(name)->setEnabled(p.isEnabled());
  }
  if (m_arrays.empty())
    throw std::runtime_error("Surface requires Arrays");
  // Only the deliberately authored basic matte description is accepted here.
  m_material.setParameter("color", material.parameter("color")->value());
  m_material.setParameter("opacity", material.parameter("opacity")->value());
  m_material.setParameter("alphaMode", "opaque");
  m_material.parameter("alphaMode")->setStringSelection(0);
  m_surface.setName("Surface");
  m_surface.setParameter("geometry", Any(ANARI_GEOMETRY, m_geometryIndex));
  m_surface.setParameter("material", Any(ANARI_MATERIAL, m_materialIndex));
  m_nodeData.setAsObject(ANARI_SURFACE, m_surfaceIndex);
  scene.m_db.array.reserve(m_arrayIndex + m_arrays.size());
  scene.m_db.geometry.reserve(m_geometryIndex + 1);
  scene.m_db.material.reserve(m_materialIndex + 1);
  scene.m_db.surface.reserve(m_surfaceIndex + 1);
  layer->m_tree.reserve(m_extent + 2);
}

inline bool PreparedSurfaceContent::profileStorage(Scene &scene,
    LayerNodeRef parent,
    const Geometry &geometry,
    const Material &material,
    bool newLayer,
    StorageProfile &profile)
{
#ifndef __GLIBCXX__
  return false;
#else
  if (geometry.numParameters() > 64 || !geometry.numParameters()
      || !PreparedHdriContent::profileStorage(
          scene, parent, 0, newLayer, profile))
    return false;
  const auto object = [&](const Object &o) {
    if (o.numMetadata() || o.numParameters() > 64 || !profile.string(o.m_name)
        || !profile.string(o.m_description)
        || !profile.add(o.m_parameters.backingCapacity(),
            sizeof(Object::ParameterMap::item_t)))
      return false;
    for (size_t i = 0; i < o.numParameters(); ++i) {
      const auto &p = o.parameterAt(i);
      if (p.stringValues().size() > 64 || !profile.string(p.description())
          || !profile.add(p.stringValues().capacity(), sizeof(std::string)))
        return false;
      for (const auto *a : {&p.value(), &p.min(), &p.max()})
        if (a->stringBackingCapacity() > 65535
            || !profile.add(a->stringBackingCapacity() + 1))
          return false;
      for (const auto &v : p.stringValues())
        if (!profile.string(v))
          return false;
    }
    return true;
  };
  const auto pool = [&](const auto &p) {
    if (p.capacity() >= 32768 || !p.profileStorage(profile, p.capacity() + 1))
      return false;
    for (size_t i = 0; i < p.capacity(); ++i)
      if (!object(p[i]))
        return false;
    return true;
  };
  if (!pool(scene.m_db.geometry) || !pool(scene.m_db.material)
      || !pool(scene.m_db.surface))
    return false;
  std::array<const Array *, 64> seen{};
  size_t count = 0;
  for (size_t i = 0; i < geometry.numParameters(); ++i) {
    const auto *a =
        geometry.parameterValueAsObject<Array>(geometry.parameterNameAt(i));
    if (!a || !a->isHost() || a->isMapped() || a->type() != ANARI_ARRAY1D
        || anari::isObject(a->elementType()))
      return false;
    bool found = false;
    for (size_t j = 0; j < count; ++j)
      found |= seen[j] == a;
    if (found)
      continue;
    seen[count++] = a;
    if (!profile.add(a->size(), a->elementSize())
        || !profile.add(sizeof(ArrayLifetime) + sizeof(ParameterOwnerLifetime)))
      return false;
  }
  // Include old/new Array pool growth, detached values and vector growth.
  // HDRI's Array profile already covers old slots and one new slot.
  if (count > 32768 - scene.m_db.array.capacity()
      || !scene.m_db.array.profileStorage(
          profile, scene.m_db.array.capacity() + count)
      || !profile.add(geometry.numParameters(), sizeof(Array) + sizeof(size_t)))
    return false;
  // Geometry's successive setParameter capacities overlap by at most 3*N.
  // Matte defaults are the same constructor used by the validated source;
  // profile it three times for initial/grown/copied string backing overlap.
  for (size_t i = 0; i < 3; ++i)
    if (!object(material))
      return false;
  return profile.add(
             3 * geometry.numParameters(), sizeof(Object::ParameterMap::item_t))
      && profile.add(6, sizeof(Object::ParameterMap::item_t))
      && profile.add(2 * 44 + 3 * sizeof(ParameterOwnerLifetime))
      && profile.add(sizeof(PreparedSurfaceContent));
#endif
}

inline bool PreparedSurfaceContent::delegatesCurrent(const Scene &scene) const
{
  const auto &now = scene.m_updateDelegate.m_delegates;
  if (now.size() != m_delegates.size())
    return false;
  for (size_t i = 0; i < now.size(); ++i)
    if (now[i] != m_delegates[i].lock())
      return false;
  return true;
}

inline bool PreparedSurfaceContent::isCurrent() const
{
  const auto s = m_scene.lock();
  const auto l = m_layer.lock();
  if (m_committed || !s || !s->scene || !l || !l->layer
      || !delegatesCurrent(*s->scene))
    return false;
  auto &db = s->scene->m_db;
  if (db.array.capacity() != m_arrayIndex
      || db.geometry.capacity() != m_geometryIndex
      || db.material.capacity() != m_materialIndex
      || db.surface.capacity() != m_surfaceIndex
      || !db.array.can_append_reserved() || !db.geometry.can_append_reserved()
      || !db.material.can_append_reserved() || !db.surface.can_append_reserved()
      || l->layer->capacity() != m_extent
      || l->layer->m_tree.next_lifetime() != m_rootLife)
    return false;
  for (const auto &a : m_ancestors) {
    const auto n = l->layer->at(a.index);
    if (!n || n->lifetime() != a.lifetime || n->topologyVersion() != a.topology
        || n->value().editVersion() != a.edit)
      return false;
  }
  if (m_newLayer) {
    if (s->scene->m_layers.size() + 1 != m_layers->size()
        || s->scene->layer(m_layerName))
      return false;
    for (const auto &e : s->scene->m_layers) {
      const auto *p = m_layers->at(e.first);
      if (!p || p->ptr != e.second.ptr || p->active != e.second.active)
        return false;
    }
  } else if (!l->dependencyAttached)
    return false;
  return true;
}

inline bool PreparedSurfaceContent::committed() const
{
  return m_committed;
}
inline size_t PreparedSurfaceContent::surfaceIndex() const
{
  return m_surfaceIndex;
}
inline LayerNodeRef PreparedSurfaceContent::root() const
{
  const auto l = m_layer.lock();
  if (!m_committed || !l || !l->layer)
    return {};
  auto n = l->layer->at(m_extent);
  return n && n->lifetime() == m_rootLife ? n : LayerNodeRef{};
}
inline LayerNodeRef PreparedSurfaceContent::node() const
{
  const auto l = m_layer.lock();
  if (!root())
    return {};
  auto n = l->layer->at(m_extent + 1);
  return n && n->lifetime() == m_rootLife + 1 ? n : LayerNodeRef{};
}

inline bool PreparedSurfaceContent::commit()
{
  if (!isCurrent())
    return false;
  auto *s = m_scene.lock()->scene;
  auto *l = m_layer.lock()->layer;
  const auto bind =
      [&](Object &o, size_t index, size_t parameters, size_t layers) {
        o.m_scene = s;
        o.m_index = index;
        o.m_updateDelegate = &s->m_updateDelegate;
        o.m_useCounts.parameter = parameters;
        o.m_useCounts.layer = layers;
      };
  for (size_t i = 0; i < m_arrays.size(); ++i) {
    auto a = s->m_db.array.append_reserved(std::move(m_arrays[i]));
    bind(*a, m_arrayIndex + i, m_arrayUses[i], 0);
  }
  auto g = s->m_db.geometry.append_reserved(std::move(m_geometry));
  auto m = s->m_db.material.append_reserved(std::move(m_material));
  auto surface = s->m_db.surface.append_reserved(std::move(m_surface));
  bind(*g, m_geometryIndex, 1, 0);
  bind(*m, m_materialIndex, 1, 0);
  bind(*surface, m_surfaceIndex, 0, 0);
  const auto root = l->m_tree.insert_last_child_at(
      l->at(m_parentIndex), m_extent, std::move(m_rootData));
  l->m_tree.insert_last_child_at(root, m_extent + 1, std::move(m_nodeData));
  if (m_newLayer) {
    std::swap(s->m_layers, *m_layers);
    m_layers.reset();
    m_newLayer->m_owner->dependencyAttached = true;
    ++s->m_numActiveLayers;
  }
  s->m_dependencyState->noteChange();
  m_revision = s->m_dependencyState->revision;
  m_committed = true;
  return true;
}

inline void PreparedSurfaceContent::notify()
{
  if (!m_committed || m_notified)
    return;
  const auto s = m_scene.lock();
  const auto l = m_layer.lock();
  if (!s || !s->scene || !l || !l->layer)
    return;
  if (s->revision != m_revision || !delegatesCurrent(*s->scene))
    throw std::runtime_error("Surface notification requires commit safe point");
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
    for (size_t i = 0; i < m_arrays.size(); ++i)
      signal([&] {
        d->signalObjectAdded(s->scene->m_db.array.at(m_arrayIndex + i).data());
      });
    signal([&] {
      d->signalObjectAdded(s->scene->m_db.geometry.at(m_geometryIndex).data());
    });
    signal([&] {
      d->signalObjectAdded(s->scene->m_db.material.at(m_materialIndex).data());
    });
    signal([&] {
      d->signalObjectAdded(s->scene->m_db.surface.at(m_surfaceIndex).data());
    });
    if (m_newLayer)
      signal([&] { d->signalLayerAdded(m_newLayer.get()); });
    signal([&] { d->signalLayerStructureUpdated(l->layer); });
  }
  for (const auto &w : m_delegates)
    if (const auto d = w.lock())
      signal([&] { d->signalUpdateBatchEnd(); });
  if (first)
    std::rethrow_exception(first);
}

} // namespace vsr::scene::detail

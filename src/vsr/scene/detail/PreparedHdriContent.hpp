// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scene
#include "vsr/scene/Scene.hpp"
// std
#include <cmath>
#include <cstring>
#include <exception>
#include <optional>

namespace vsr::integration::content {
class PreparedHdriPublication;
}

namespace vsr::scene::detail {

struct HdriSettings
{
  float scale{1.f};
  math::float3 up{0.f, 1.f, 0.f};
  math::float3 direction{1.f, 0.f, 0.f};
  math::mat4 transform{math::IDENTITY_MAT4};
};

// Allocation-free validation shared by admission and detached preparation.
inline const char *validateHdriSettings(const HdriSettings &settings)
{
  if (!std::isfinite(settings.scale) || settings.scale < 0.f)
    return "hdri_scale_must_be_finite_and_nonnegative";
  for (size_t column = 0; column < 4; ++column)
    for (size_t row = 0; row < 4; ++row)
      if (!std::isfinite(settings.transform[column][row]))
        return "hdri_transform_must_be_finite";
  if (settings.transform[0][3] != 0.f || settings.transform[1][3] != 0.f
      || settings.transform[2][3] != 0.f || settings.transform[3][3] != 1.f)
    return "hdri_transform_must_be_affine";
  const auto finite = [](const math::float3 &v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
  };
  const auto upLength = math::length(settings.up);
  const auto directionLength = math::length(settings.direction);
  const auto crossLength =
      math::length(math::cross(settings.up, settings.direction));
  if (!finite(settings.up) || !finite(settings.direction)
      || !std::isfinite(upLength) || !std::isfinite(directionLength)
      || !std::isfinite(crossLength) || upLength == 0.f
      || directionLength == 0.f || crossLength == 0.f)
    return "hdri_up_and_direction_must_be_finite_nonzero_and_nonparallel";
  return nullptr;
}

/* Internal, owning-thread append seam, not an admitted agent operation.
 * All values, data, pool/forest capacity and optional Layer ownership are
 * prepared while detached. commit() never calls ordinary Scene constructors
 * or delegates. notify() is a separate post-write boundary: failures there
 * cannot undo publication. The caller must prepare identity, receipt and
 * recovery ownership before commit, and check authority/source currency.
 * No live pooled pointer or ObjectPoolRef is retained across preparation. */
class PreparedHdriContent
{
 public:
  PreparedHdriContent(Scene &scene,
      LayerNodeRef parent,
      const char *rootName,
      size_t width,
      size_t height,
      const math::float3 *radiance,
      const HdriSettings &settings = {},
      Token newLayerName = {});
  VSR_NOT_COPYABLE(PreparedHdriContent)
  VSR_NOT_MOVEABLE(PreparedHdriContent)

  // Allocation-free, finite libstdc++ backing/growth admission. A false
  // result prohibits construction, even if logical Scene counts are small.
  static bool profileStorage(Scene &scene,
      LayerNodeRef parent,
      size_t pixels,
      bool newLayer,
      StorageProfile &profile);
  bool isCurrent() const;
  bool committed() const;
  size_t arrayIndex() const;
  size_t lightIndex() const;
  LayerNodeRef root() const;
  LayerNodeRef lightNode() const;
  Layer *ownedLayer() const;
  bool commit();
  void notify();

 private:
  friend class ::vsr::integration::content::PreparedHdriPublication;
  static size_t checkedWidth(size_t width, size_t height);
  static const char *checkedName(const char *name);
  bool delegatesCurrent(const Scene &scene) const;
  std::vector<std::weak_ptr<BaseUpdateDelegate>> m_delegates;
  std::weak_ptr<SceneDependencyState> m_scene;
  std::weak_ptr<LayerOwnerState> m_layer;
  LayerPtr m_newLayer;
  Token m_newLayerName;
  std::optional<LayerMap> m_layers;
  struct Ancestor
  {
    size_t index;
    std::uint64_t lifetime;
    std::uint64_t topology;
    std::uint64_t edit;
  };
  std::vector<Ancestor> m_ancestors;
  size_t m_parentIndex{0};
  std::uint64_t m_parentLifetime{0};
  size_t m_arrayIndex{0};
  size_t m_lightIndex{0};
  size_t m_nodeExtent{0};
  size_t m_layerCount{0};
  Array m_radiance;
  Light m_light;
  LayerNodeData m_rootData;
  LayerNodeData m_lightData;
  std::uint64_t m_rootLifetime{0};
  std::uint64_t m_lightNodeLifetime{0};
  std::uint64_t m_commitRevision{0};
  bool m_committed{false};
  bool m_notified{false};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline bool PreparedHdriContent::profileStorage(Scene &scene,
    LayerNodeRef parent,
    size_t pixels,
    bool newLayer,
    StorageProfile &profile)
{
#ifndef __GLIBCXX__
  return false;
#else
  constexpr size_t MAX_SLOTS = 32768;
  constexpr size_t MAX_METADATA = 64u << 10;
  auto &arrays = scene.m_db.array;
  auto &lights = scene.m_db.light;
  if (arrays.capacity() >= MAX_SLOTS || lights.capacity() >= MAX_SLOTS
      || scene.m_layers.size() >= MAX_SLOTS
      || scene.m_updateDelegate.m_delegates.size() > 1024
      || pixels > (1u << 20))
    return false;
  if (!arrays.profileStorage(profile, arrays.capacity() + 1)
      || !lights.profileStorage(profile, lights.capacity() + 1))
    return false;
  const auto any = [](StorageProfile &p, const Any &v) {
    const auto capacity = v.stringBackingCapacity();
    return capacity < MAX_METADATA && p.add(capacity + 1);
  };
  const auto object = [&](const Object &o) {
    StorageProfile metadata(MAX_METADATA);
    if (o.numMetadata() || o.numParameters() > 64 || !metadata.string(o.m_name)
        || !metadata.string(o.m_description)
        || !metadata.add(o.m_parameters.backingCapacity(),
            sizeof(Object::ParameterMap::item_t)))
      return false;
    for (const auto &entry : o.m_parameters) {
      const auto &p = entry.second;
      if (p.m_stringValues.size() > 64 || !metadata.string(p.m_description)
          || !any(metadata, p.m_value) || !any(metadata, p.m_min)
          || !any(metadata, p.m_max)
          || !metadata.add(p.m_stringValues.capacity(), sizeof(std::string)))
        return false;
      for (const auto &v : p.m_stringValues)
        if (!metadata.string(v))
          return false;
    }
    return metadata.valid() && profile.add(metadata.bytes());
  };
  // Inspect constructed empty slots too: erase can leave default Parameters.
  for (size_t i = 0; i < arrays.capacity(); ++i)
    if (!object(arrays[i]))
      return false;
  for (size_t i = 0; i < lights.capacity(); ++i)
    if (!object(lights[i]))
      return false;
  if (!parent && !newLayer && scene.numberOfLayers())
    parent = scene.layer(0)->root();
  if (parent) {
    auto *layer = parent->value().layer();
    if (!layer || layer->scene() != &scene || layer->capacity() > MAX_SLOTS - 2
        || !layer->m_tree.profileStorage(profile, layer->capacity() + 2))
      return false;
    for (size_t i = 0; i < layer->capacity(); ++i) {
      const auto &n = layer->m_tree[i];
      StorageProfile metadata(MAX_METADATA);
      if (n.m_instanceParameters.size() > 64 || !metadata.string(n.m_name)
          || !any(metadata, n.m_value) || !any(metadata, n.m_defaultValue)
          || !metadata.add(n.m_instanceParameters.backingCapacity(),
              sizeof(InstanceParameterMap::item_t)))
        return false;
      for (const auto &p : n.m_instanceParameters)
        if (!metadata.string(p.first) || !any(metadata, p.second))
          return false;
      if (!metadata.valid() || !profile.add(metadata.bytes()))
        return false;
    }
  }
  // LayerMap copy allocates size, push_back grows by at most 2*size under
  // libstdc++. Count original plus copied plus grown storage simultaneously.
  if (newLayer || !scene.numberOfLayers()) {
    if (!profile.add(scene.m_layers.backingCapacity(), sizeof(LayerMap::item_t))
        || !profile.add(scene.m_layers.size(), sizeof(LayerMap::item_t))
        || !profile.add(
            2 * (scene.m_layers.size() + 1), sizeof(LayerMap::item_t))
        || !profile.add(6, sizeof(LayerNode) + sizeof(size_t)))
      return false;
  }
  // Light(hdri) creates four defaults then radiance: vector capacities 1/2/4/8,
  // at most old4+new8 concurrently, including copied descriptions. Each of
  // its four descriptions has <=60 bytes. Parameter value/min/max are inline.
  // Both ancestry vectors double up to 1024 entries; include old/new overlap.
  return profile.add(pixels, sizeof(math::float3))
      && profile.add(12, sizeof(Object::ParameterMap::item_t))
      && profile.add(8, 61) && profile.add(3 * 1024, sizeof(Ancestor))
      && profile.add(scene.m_updateDelegate.m_delegates.size(),
          sizeof(std::weak_ptr<BaseUpdateDelegate>))
      && profile.add(sizeof(PreparedHdriContent) + sizeof(Layer)
          + sizeof(LayerOwnerState) + sizeof(ParameterOwnerLifetime)
          + sizeof(ArrayLifetime) + 2 * 257);
#endif
}

inline size_t PreparedHdriContent::checkedWidth(size_t width, size_t height)
{
  constexpr size_t MAX_PIXELS = 16u << 20;
  if (!width || !height || width > 16384 || height > 16384
      || width > MAX_PIXELS / height)
    throw std::runtime_error("Prepared HDRI dimensions exceed bounds");
  return width;
}

inline const char *PreparedHdriContent::checkedName(const char *name)
{
  if (!name || !*name || std::strlen(name) > 256)
    throw std::runtime_error("Invalid HDRI Content Root name");
  return name;
}

inline PreparedHdriContent::PreparedHdriContent(Scene &scene,
    LayerNodeRef parent,
    const char *rootName,
    size_t width,
    size_t height,
    const math::float3 *radiance,
    const HdriSettings &settings,
    Token newLayerName)
    : m_scene(scene.m_dependencyState),
      m_newLayerName(newLayerName),
      m_radiance(ANARI_FLOAT32_VEC3, checkedWidth(width, height), height),
      m_light(tokens::light::hdri),
      m_rootData(nullptr, settings.transform, checkedName(rootName)),
      m_lightData(nullptr, "HDRI Light")
{
  m_delegates.reserve(scene.m_updateDelegate.m_delegates.size());
  for (const auto &delegate : scene.m_updateDelegate.m_delegates)
    m_delegates.emplace_back(delegate);
  if (!radiance)
    throw std::runtime_error("Invalid prepared HDRI radiance");
  if (const auto *reason = validateHdriSettings(settings))
    throw std::runtime_error(reason);
  if (!parent && !newLayerName) {
    if (scene.m_layers.empty())
      newLayerName = m_newLayerName = Token("default");
    else
      parent = scene.layer(0)->root();
  }
  if (newLayerName) {
    if (parent || scene.layer(newLayerName))
      throw std::runtime_error("New HDRI Layer must not replace a Layer");
    m_newLayer = std::make_shared<Layer>(&scene, newLayerName.str());
    m_newLayer->m_owner->dependencyAttached = false;
    m_layers = scene.m_layers;
    m_layers->set(newLayerName, LayerState{m_newLayer, true});
    parent = m_newLayer->root();
  }
  if (!parent || !parent->value().layer()
      || parent->value().layer()->scene() != &scene)
    throw std::runtime_error("Invalid HDRI destination");
  auto *layer = parent->value().layer();
  m_layer = layer->m_owner;
  m_parentIndex = parent.index();
  m_parentLifetime = parent->lifetime();
  for (auto node = parent;; node = node->parent()) {
    if (m_ancestors.size() == 1024)
      throw std::runtime_error("HDRI destination ancestry exceeds bounds");
    m_ancestors.push_back({node.index(),
        node->lifetime(),
        node->topologyVersion(),
        node->value().editVersion()});
    if (node->isRoot())
      break;
  }
  m_layerCount = scene.m_layers.size();
  m_nodeExtent = layer->capacity();
  m_rootLifetime = layer->m_tree.next_lifetime();
  if (!layer->m_tree.can_issue_nodes(2))
    throw std::runtime_error("HDRI node identity exhausted");
  m_lightNodeLifetime = m_rootLifetime + 1;
  m_arrayIndex = scene.m_db.array.capacity();
  m_lightIndex = scene.m_db.light.capacity();
  m_radiance.setData(radiance, width * height);
  if (scene.m_imageWrites && !m_radiance.trackImageWrites(scene.m_imageWrites))
    throw std::runtime_error("HDRI write tracking unavailable");
  m_light.setParameter("scale", settings.scale);
  m_light.setParameter("up", settings.up);
  m_light.setParameter("direction", settings.direction);
  m_light.setParameter("radiance", Any(ANARI_ARRAY2D, m_arrayIndex));
  m_lightData.setAsObject(ANARI_LIGHT, m_lightIndex);
  // reserve() may relocate storage but changes no logical Scene content.
  scene.m_db.array.reserve(m_arrayIndex + 1);
  scene.m_db.light.reserve(m_lightIndex + 1);
  layer->m_tree.reserve(m_nodeExtent + 2);
}

inline bool PreparedHdriContent::delegatesCurrent(const Scene &scene) const
{
  const auto &now = scene.m_updateDelegate.m_delegates;
  if (now.size() != m_delegates.size())
    return false;
  for (size_t i = 0; i < now.size(); ++i)
    if (m_delegates[i].lock() != now[i])
      return false;
  return true;
}

inline bool PreparedHdriContent::isCurrent() const
{
  const auto scene = m_scene.lock();
  const auto layer = m_layer.lock();
  if (m_committed || !scene || !scene->scene || !layer || !layer->layer)
    return false;
  auto *s = scene->scene;
  auto *l = layer->layer;
  const auto parent = l->at(m_parentIndex);
  if (!delegatesCurrent(*s) || !parent || parent->lifetime() != m_parentLifetime
      || s->m_db.array.capacity() != m_arrayIndex
      || s->m_db.light.capacity() != m_lightIndex
      || !s->m_db.array.can_append_reserved()
      || !s->m_db.light.can_append_reserved() || l->capacity() != m_nodeExtent
      || l->m_tree.next_lifetime() != m_rootLifetime
      || !l->m_tree.can_issue_nodes(2))
    return false;
  for (const auto &ancestor : m_ancestors) {
    const auto node = l->at(ancestor.index);
    if (!node || node->lifetime() != ancestor.lifetime
        || node->topologyVersion() != ancestor.topology
        || node->value().editVersion() != ancestor.edit)
      return false;
  }
  // This final-safe-point preparation is deliberately short-lived. A new
  // Layer map must never swap over intervening host Layer/active-state edits.
  if (m_newLayer) {
    if (s->m_layers.size() != m_layerCount || s->layer(m_newLayerName))
      return false;
    for (const auto &entry : s->m_layers) {
      const auto *prepared = m_layers->at(entry.first);
      if (!prepared || prepared->ptr != entry.second.ptr
          || prepared->active != entry.second.active)
        return false;
    }
  } else if (!layer->dependencyAttached)
    return false;
  return true;
}

inline bool PreparedHdriContent::committed() const
{
  return m_committed;
}

inline size_t PreparedHdriContent::arrayIndex() const
{
  return m_arrayIndex;
}

inline size_t PreparedHdriContent::lightIndex() const
{
  return m_lightIndex;
}

inline LayerNodeRef PreparedHdriContent::root() const
{
  const auto scene = m_scene.lock();
  const auto layer = m_layer.lock();
  if (!m_committed || !scene || !scene->scene || !layer || !layer->layer)
    return {};
  auto node = layer->layer->at(m_nodeExtent);
  return node && node->lifetime() == m_rootLifetime ? node : LayerNodeRef{};
}

inline LayerNodeRef PreparedHdriContent::lightNode() const
{
  const auto scene = m_scene.lock();
  const auto layer = m_layer.lock();
  if (!m_committed || !scene || !scene->scene || !layer || !layer->layer)
    return {};
  auto node = layer->layer->at(m_nodeExtent + 1);
  return node && node->lifetime() == m_lightNodeLifetime ? node
                                                         : LayerNodeRef{};
}

inline Layer *PreparedHdriContent::ownedLayer() const
{
  return m_newLayer.get();
}

inline bool PreparedHdriContent::commit()
{
  if (!isCurrent())
    return false;
  auto *s = m_scene.lock()->scene;
  auto *layer = m_layer.lock()->layer;
  auto array = s->m_db.array.append_reserved(std::move(m_radiance));
  auto light = s->m_db.light.append_reserved(std::move(m_light));
  array->m_scene = s;
  array->m_index = m_arrayIndex;
  array->m_updateDelegate = &s->m_updateDelegate;
  array->m_useCounts.parameter = 1;
  light->m_scene = s;
  light->m_index = m_lightIndex;
  light->m_updateDelegate = &s->m_updateDelegate;
  const auto root = layer->m_tree.insert_last_child_at(
      layer->at(m_parentIndex), m_nodeExtent, std::move(m_rootData));
  const auto lightNode = layer->m_tree.insert_last_child_at(
      root, m_nodeExtent + 1, std::move(m_lightData));
  m_rootLifetime = root->lifetime();
  m_lightNodeLifetime = lightNode->lifetime();
  if (m_newLayer) {
    std::swap(s->m_layers, *m_layers);
    m_layers.reset();
    m_newLayer->m_owner->dependencyAttached = true;
    ++s->m_numActiveLayers;
  }
  s->m_dependencyState->noteChange();
  m_commitRevision = s->m_dependencyState->revision;
  m_committed = true;
  return true;
}

inline void PreparedHdriContent::notify()
{
  if (!m_committed || m_notified)
    return;
  const auto life = m_scene.lock();
  const auto owner = m_layer.lock();
  if (!life || !life->scene || !owner || !owner->layer)
    return;
  auto *s = life->scene;
  if (life->revision != m_commitRevision || !delegatesCurrent(*s))
    throw std::runtime_error(
        "HDRI notifications require the commit safe point");
  m_notified = true;
  std::exception_ptr first;
  // Finish all post-write signals before rethrowing the first failure.
  const auto signal = [&](auto &&f) {
    try {
      f();
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
    signal([&] {
      delegate->signalObjectAdded(s->m_db.array.at(m_arrayIndex).data());
    });
    signal([&] {
      delegate->signalObjectAdded(s->m_db.light.at(m_lightIndex).data());
    });
    if (m_newLayer)
      signal([&] { delegate->signalLayerAdded(m_newLayer.get()); });
    signal([&] { delegate->signalLayerStructureUpdated(owner->layer); });
  }
  for (const auto &weak : m_delegates)
    if (const auto delegate = weak.lock())
      signal([&] { delegate->signalUpdateBatchEnd(); });
  if (first)
    std::rethrow_exception(first);
}

} // namespace vsr::scene::detail

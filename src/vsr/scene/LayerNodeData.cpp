// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_core
#include "vsr/core/Logging.hpp"
// vsr_scene
#include "vsr/scene/Layer.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <atomic>
#include <exception>
#include <stdexcept>

namespace vsr::scene {

LayerNodeData::LayerNodeData(Layer *layer, const char *n)
{
  if (layer) {
    m_layerOwner = layer->m_owner;
    m_sceneOwner = layer->m_owner->scene;
  }
  setEmpty();
  m_name = n;
}

LayerNodeData::LayerNodeData(Layer *layer, const math::mat4 &m, const char *n)
    : LayerNodeData(layer, n)
{
  setAsTransform(m);
}

LayerNodeData::LayerNodeData(Layer *layer, const math::mat3 &m, const char *n)
    : LayerNodeData(layer, n)
{
  setAsTransform(m);
}

LayerNodeData::LayerNodeData(Layer *layer, Object *o, const char *n)
    : LayerNodeData(layer, n)
{
  setAsObject(o);
}

LayerNodeData::LayerNodeData(const LayerNodeData &o)
    : m_editVersion(o.m_editVersion), m_transformVersion(o.m_transformVersion)
{
  m_name = o.m_name;
  m_enabled = o.m_enabled;
  m_value = o.m_value;
  m_defaultValue = o.m_defaultValue;
  m_srt = o.m_srt;
  m_instanceParameters = o.m_instanceParameters;
  m_layerOwner = o.m_layerOwner;
  m_sceneOwner = o.m_sceneOwner;
  incObjectUseCount();
}

LayerNodeData::LayerNodeData(LayerNodeData &&o)
    : m_editVersion(o.m_editVersion), m_transformVersion(o.m_transformVersion)
{
  m_name = std::move(o.m_name);
  m_enabled = std::move(o.m_enabled);
  m_value = std::move(o.m_value);
  m_defaultValue = std::move(o.m_defaultValue);
  m_srt = std::move(o.m_srt);
  m_instanceParameters = std::move(o.m_instanceParameters);
  m_layerOwner = o.m_layerOwner;
  m_sceneOwner = o.m_sceneOwner;
  o.m_value.reset();
  o.noteEdit();
}

LayerNodeData::LayerNodeData(LayerNodeData &&o, StorageRelocationTag) noexcept
    : m_name(std::move(o.m_name)),
      m_enabled(o.m_enabled),
      m_editVersion(o.m_editVersion),
      m_transformVersion(o.m_transformVersion),
      m_value(std::move(o.m_value)),
      m_defaultValue(std::move(o.m_defaultValue)),
      m_srt(o.m_srt),
      m_instanceParameters(std::move(o.m_instanceParameters)),
      m_layerOwner(std::move(o.m_layerOwner)),
      m_sceneOwner(std::move(o.m_sceneOwner)),
      m_attached(o.m_attached)
{
  o.m_attached = false;
  o.m_value.reset();
}

void LayerNodeData::relocateAssign(LayerNodeData &&o) noexcept
{
  if (this == &o)
    return;
  decObjectUseCount();
  m_name = std::move(o.m_name);
  m_enabled = o.m_enabled;
  m_editVersion = o.m_editVersion;
  m_transformVersion = o.m_transformVersion;
  m_value = std::move(o.m_value);
  m_defaultValue = std::move(o.m_defaultValue);
  m_srt = o.m_srt;
  m_instanceParameters = std::move(o.m_instanceParameters);
  m_layerOwner = std::move(o.m_layerOwner);
  m_sceneOwner = std::move(o.m_sceneOwner);
  m_attached = o.m_attached;
  o.m_attached = false;
  o.m_value.reset();
}

void LayerNodeData::bind(Layer *layer)
{
  // Empty and expired weak references differ: never revive a dead Scene's
  // object indices when allocator storage is reused by another Scene.
  std::weak_ptr<detail::SceneDependencyState> empty;
  if (!m_sceneOwner.owner_before(empty) && !empty.owner_before(m_sceneOwner)) {
    m_sceneOwner = layer->m_owner->scene;
    incObjectUseCount();
  }
  m_layerOwner = layer->m_owner;
  m_attached = true;
}

void LayerNodeData::checkAssignmentOwner(const LayerNodeData &o) const
{
  if (!m_attached
      || (!m_sceneOwner.owner_before(o.m_sceneOwner)
          && !o.m_sceneOwner.owner_before(m_sceneOwner)))
    return;
  bool hasObjectIndices = o.isObject() || o.m_defaultValue.holdsObject();
  for (const auto &parameter : o.m_instanceParameters)
    hasObjectIndices |= parameter.second.holdsObject();
  if (hasObjectIndices)
    throw std::runtime_error("Cannot assign object indices across Scenes");
}

LayerNodeData &LayerNodeData::operator=(const LayerNodeData &o)
{
  checkAssignmentOwner(o);
  noteEdit();
  if (this == &o)
    return *this;
  decObjectUseCount();
  m_name = o.m_name;
  m_enabled = o.m_enabled;
  m_value = o.m_value;
  m_defaultValue = o.m_defaultValue;
  m_srt = o.m_srt;
  m_instanceParameters = o.m_instanceParameters;
  if (!m_attached) {
    m_layerOwner = o.m_layerOwner;
    m_sceneOwner = o.m_sceneOwner;
  }
  incObjectUseCount();
  return *this;
}

LayerNodeData &LayerNodeData::operator=(LayerNodeData &&o)
{
  checkAssignmentOwner(o);
  noteEdit();
  if (this == &o)
    return *this;
  decObjectUseCount();
  m_name = std::move(o.m_name);
  m_enabled = std::move(o.m_enabled);
  m_value = std::move(o.m_value);
  m_defaultValue = std::move(o.m_defaultValue);
  m_srt = std::move(o.m_srt);
  m_instanceParameters = std::move(o.m_instanceParameters);
  if (!m_attached) {
    m_layerOwner = o.m_layerOwner;
    m_sceneOwner = o.m_sceneOwner;
  }
  // The source reference is transferred, not released and reacquired.
  o.m_value.reset();
  o.noteEdit();
  return *this;
}

LayerNodeData::~LayerNodeData()
{
  decObjectUseCount();
}

const Layer *LayerNodeData::layer() const
{
  auto owner = m_layerOwner.lock();
  return owner ? owner->layer : nullptr;
}

Layer *LayerNodeData::layer()
{
  auto owner = m_layerOwner.lock();
  return owner ? owner->layer : nullptr;
}

bool LayerNodeData::hasDefault() const
{
  return m_defaultValue;
}

bool LayerNodeData::isDefaultValue() const
{
  return m_value == m_defaultValue;
}

void LayerNodeData::setToDefaultValue()
{
  if (hasDefault()) {
    noteEdit();
    decObjectUseCount();
    m_value = m_defaultValue;
    incObjectUseCount();
    if (isTransform())
      setAsTransform(getTransform()); // ensure srt matrix is up to date
  }
}

void LayerNodeData::setCurrentValueAsDefault()
{
  if (isTransform())
    m_defaultValue = m_value;
}

anari::DataType LayerNodeData::type() const
{
  return m_value.type();
}

bool LayerNodeData::isObject() const
{
  return anari::isObject(type());
}

bool LayerNodeData::isTransform() const
{
  return type() == ANARI_FLOAT32_MAT4;
}

bool LayerNodeData::isEmpty() const
{
  return !m_value;
}

bool LayerNodeData::isEnabled() const
{
  return m_enabled;
}

void LayerNodeData::setAsObject(Object *o)
{
  if (o)
    setAsObject(o->type(), o->index());
  else {
    vsr::core::logWarning(
        "LayerNodeData::setAsObject() called with null object,"
        " setting to empty");
    setEmpty();
  }
}

void LayerNodeData::setAsObject(anari::DataType type, size_t index)
{
  noteEdit();
  decObjectUseCount();
  m_value = Any(type, index);
  incObjectUseCount();
}

void LayerNodeData::setAsTransform(const math::mat4 &m)
{
  noteEdit();
  decObjectUseCount();
  m_value = m;
  if (!hasDefault())
    m_defaultValue = m_value;

  auto &sc = m_srt[0];
  auto &azelrot = m_srt[1];
  auto &tl = m_srt[2];
  math::mat4 rot;
  math::decomposeMatrix(m, sc, rot, tl);
  azelrot = math::normalizeDegrees(math::degrees(math::matrixToAzElRoll(rot)));
}

void LayerNodeData::setAsTransform(
    const math::mat4 &m, const math::mat4 &defaultM)
{
  m_defaultValue = defaultM;
  setAsTransform(m);
}

void LayerNodeData::setAsTransform(const math::mat3 &srt)
{
  noteEdit();
  decObjectUseCount();
  m_srt = srt;
  auto &sc = srt[0];
  auto &azelrot = srt[1];
  auto &tl = srt[2];

  auto rot = math::IDENTITY_MAT4;
  rot = math::mul(rot,
      math::rotation_matrix(math::rotation_quat(
          math::float3(0.f, 1.f, 0.f), math::radians(azelrot.x))));
  rot = math::mul(rot,
      math::rotation_matrix(math::rotation_quat(
          math::float3(1.f, 0.f, 0.f), math::radians(azelrot.y))));
  rot = math::mul(rot,
      math::rotation_matrix(math::rotation_quat(
          math::float3(0.f, 0.f, 1.f), math::radians(azelrot.z))));

  m_value = math::mul(
      math::translation_matrix(tl), math::mul(rot, math::scaling_matrix(sc)));
  if (!hasDefault())
    m_defaultValue = m_value;
}

void LayerNodeData::setTransformState(
    const math::mat4 &matrix, const math::mat3 &srt)
{
  noteEdit();
  decObjectUseCount();
  m_value = matrix;
  m_srt = srt;
}

void LayerNodeData::setAsTransformArray(Array *a)
{
  setAsObject(a);
}

void LayerNodeData::setEmpty()
{
  noteEdit();
  decObjectUseCount();
  m_value.reset();
  m_srt[0] = math::float3(1.f, 1.f, 1.f);
  m_srt[1] = math::float3(0.f, 0.f, 0.f);
  m_srt[2] = math::float3(0.f, 0.f, 0.f);
  clearInstanceParameters();
  m_name.clear();
}

void LayerNodeData::setEnabled(bool e)
{
  noteEdit(false);
  m_enabled = e;
}

std::uint64_t LayerNodeData::editVersion() const
{
  return m_editVersion;
}

std::uint64_t LayerNodeData::transformVersion() const
{
  return m_transformVersion;
}

void LayerNodeData::noteDependencyChange()
{
  if (!m_attached)
    return;
  auto owner = m_layerOwner.lock();
  auto scene = owner ? owner->scene.lock() : nullptr;
  if (owner && owner->layer && owner->dependencyAttached && scene)
    scene->noteChange();
}

void LayerNodeData::noteEdit(bool transformWrite)
{
  noteDependencyChange();
  // One ordered clock across all payloads makes the latest event on a chain
  // sufficient; unrelated writes never change a chain member's stamp.
  static std::atomic<std::uint64_t> clock{0};
  if (transformWrite) {
    m_transformVersion = clock.fetch_add(1, std::memory_order_relaxed) + 1;
    if (m_transformVersion == 0)
      std::terminate();
  }
  // Never let an ancient token become current again on wraparound.
  if (++m_editVersion == 0)
    std::terminate();
}

Object *LayerNodeData::getObject() const
{
  auto owner = m_sceneOwner.lock();
  auto *scene = owner ? owner->scene : nullptr;
  return isObject() && scene ? scene->getObject(m_value) : nullptr;
}

size_t LayerNodeData::getObjectIndex() const
{
  return m_value.getAsObjectIndex();
}

math::mat4 LayerNodeData::getTransform() const
{
  return isTransform() ? m_value.getAs<math::mat4>() : math::IDENTITY_MAT4;
}

math::mat3 LayerNodeData::getTransformSRT() const
{
  return isTransform() ? m_srt
                       : math::mat3{math::float3(1.f, 1.f, 1.f),
                             math::float3(0.f, 0.f, 0.f),
                             math::float3(0.f, 0.f, 0.f)};
}

Array *LayerNodeData::getTransformArray() const
{
  auto *obj = getObject();
  if (obj && obj->type() == ANARI_ARRAY1D) {
    auto *a = (Array *)obj;
    if (a->elementType() == ANARI_FLOAT32_MAT4)
      return a;
  }
  return nullptr;
}

std::string &LayerNodeData::name()
{
  return m_name;
}

const std::string &LayerNodeData::name() const
{
  return m_name;
}

Any LayerNodeData::getValueRaw() const
{
  return m_value;
}

void LayerNodeData::setValueRaw(const Any &v, bool ignoreObjectUsageCounts)
{
  noteEdit();
  if (!ignoreObjectUsageCounts)
    setEmpty();
  m_value = v;
  setCurrentValueAsDefault();
  if (!ignoreObjectUsageCounts)
    incObjectUseCount();
}

const InstanceParameterMap &LayerNodeData::getInstanceParameters() const
{
  return m_instanceParameters;
}

void LayerNodeData::setInstanceParameter(const std::string &name, Any v)
{
  noteDependencyChange();
  m_instanceParameters.set(name, v);
}

void LayerNodeData::clearInstanceParameters()
{
  noteDependencyChange();
  m_instanceParameters.clear();
}

void LayerNodeData::incObjectUseCount()
{
  if (auto *o = getObject(); o)
    o->incUseCount(Object::UseKind::LAYER);
}

void LayerNodeData::decObjectUseCount()
{
  if (auto *o = getObject(); o)
    o->decUseCount(Object::UseKind::LAYER);
}

} // namespace vsr::scene

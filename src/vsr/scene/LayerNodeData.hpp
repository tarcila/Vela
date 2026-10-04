// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/Any.hpp"
#include "vsr/core/FlatMap.hpp"
#include "vsr/core/Forest.hpp"
#include "vsr/core/VSRMath.hpp"
// vsr_scene
#include "vsr/scene/SceneDependencyState.hpp"

namespace vsr::scene {

using namespace vsr::core;

struct Array;
struct Object;
struct Layer;
using InstanceParameterMap = FlatMap<std::string, Any>;

/*
 * Payload stored in each ForestNode of a Layer tree; holds a polymorphic Any
 * value (transform matrix, Object reference, or empty), an enable flag,
 * per-instance parameters, and optional default-value tracking.
 *
 * Example:
 *   LayerNodeData nd(layer, mat4Transform, "root");
 *   nd.setAsObject(surfacePtr);
 *   nd.setEnabled(true);
 */
struct LayerNodeData
{
  LayerNodeData() = default;
  LayerNodeData(Layer *layer, const char *n = "");
  LayerNodeData(Layer *layer, const math::mat4 &m, const char *n = "");
  LayerNodeData(Layer *layer, const math::mat3 &m, const char *n = "");
  LayerNodeData(Layer *layer, Object *o, const char *n = "");
  template <typename T>
  LayerNodeData(Layer *layer, ObjectPoolRef<T> obj, const char *n = "");

  LayerNodeData(const LayerNodeData &o);
  LayerNodeData(LayerNodeData &&o);
  LayerNodeData &operator=(const LayerNodeData &o);
  LayerNodeData &operator=(LayerNodeData &&o);

  ~LayerNodeData();

  const Layer *layer() const;
  Layer *layer();

  bool hasDefault() const;
  bool isDefaultValue() const;
  void setToDefaultValue();
  void setCurrentValueAsDefault();

  anari::DataType type() const;
  bool isObject() const;
  bool isTransform() const;
  bool isEmpty() const;
  bool isEnabled() const;

  // Local editable write history, independent of layer notifications and names.
  // Copies and genuine move-outs are detached values; assignments write the
  // receiving node. Forest storage relocation preserves attachment/history,
  // while node lifetime distinguishes clones.
  std::uint64_t editVersion() const;
  // Globally ordered payload/transform history, excluding enabled-only writes.
  std::uint64_t transformVersion() const;

  void setAsObject(Object *o);
  void setAsObject(anari::DataType type, size_t index);
  void setAsTransform(const math::mat4 &m);
  void setAsTransform(const math::mat4 &m, const math::mat4 &defaultM);
  void setAsTransform(const math::mat3 &srt);
  // Apply a prepared exact matrix/cache pair without decomposition or changing
  // the stored default baseline. Advances transform and edit write history.
  void setTransformState(const math::mat4 &matrix, const math::mat3 &srt);
  void setAsTransformArray(Array *a);
  void setEmpty();

  void setEnabled(bool enabled);

  Object *getObject() const;
  size_t getObjectIndex() const;
  math::mat4 getTransform() const;
  math::mat3 getTransformSRT() const;
  Array *getTransformArray() const;

  std::string &name();
  const std::string &name() const;

  //////////////////////////////////////////////////////////////////
  // Warning: these operate on the raw Any value, no type checking!
  Any getValueRaw() const;
  void setValueRaw(const Any &v, bool ignoreObjectUsageCounts = false);
  //////////////////////////////////////////////////////////////////

  const InstanceParameterMap &getInstanceParameters() const;
  void setInstanceParameter(const std::string &name, Any v);
  void clearInstanceParameters();

 private:
  friend struct Layer;
  friend struct vsr::core::ForestStorageTraits<LayerNodeData>;
  struct StorageRelocationTag
  {
  };
  LayerNodeData(LayerNodeData &&o, StorageRelocationTag) noexcept;
  void relocateAssign(LayerNodeData &&o) noexcept;
  void bind(Layer *layer);
  void noteDependencyChange();
  void checkAssignmentOwner(const LayerNodeData &o) const;
  void noteEdit(bool transformWrite = true);
  void incObjectUseCount();
  void decObjectUseCount();

  // Data //

  std::string m_name;
  bool m_enabled{true};
  std::uint64_t m_editVersion{1};
  std::uint64_t m_transformVersion{0};
  Any m_value;
  Any m_defaultValue;
  math::mat3 m_srt; // scale, azelrot, translation
  InstanceParameterMap m_instanceParameters;
  std::weak_ptr<detail::LayerOwnerState> m_layerOwner;
  std::weak_ptr<detail::SceneDependencyState> m_sceneOwner;
  bool m_attached{false};
};

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T>
inline LayerNodeData::LayerNodeData(
    Layer *layer, ObjectPoolRef<T> obj, const char *n)
    : LayerNodeData(layer, obj.data(), n)
{}

} // namespace vsr::scene

namespace vsr::core {

template <>
struct ForestStorageTraits<scene::LayerNodeData>
{
  static scene::LayerNodeData relocate(scene::LayerNodeData &&value) noexcept;
  static void relocateAssign(scene::LayerNodeData &destination,
      scene::LayerNodeData &&source) noexcept;
};

inline scene::LayerNodeData ForestStorageTraits<scene::LayerNodeData>::relocate(
    scene::LayerNodeData &&value) noexcept
{
  return scene::LayerNodeData(
      std::move(value), scene::LayerNodeData::StorageRelocationTag{});
}

inline void ForestStorageTraits<scene::LayerNodeData>::relocateAssign(
    scene::LayerNodeData &destination, scene::LayerNodeData &&source) noexcept
{
  destination.relocateAssign(std::move(source));
}

} // namespace vsr::core

namespace vsr::scene {
using LayerNode = ForestNode<LayerNodeData>;
using LayerNodeRef = LayerNode::Ref;
} // namespace vsr::scene

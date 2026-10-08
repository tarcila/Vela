// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scene
#include "vsr/scene/Scene.hpp"
// std
#include <cassert>
#include <optional>
#include <utility>
#include <vector>

namespace vsr::scene::detail {

/* Internal owning-thread compound publication. Prepare a detached parameter
 * map, including presence and all Parameter metadata, without callbacks or
 * use-count changes. Pins protect all referenced Arrays across the nonthrowing
 * swap; final currency/authority guards belong to the owning transaction. */
class PreparedMeshBinding
{
 public:
  explicit PreparedMeshBinding(const Geometry &geometry);
  VSR_NOT_COPYABLE(PreparedMeshBinding)
  VSR_NOT_MOVEABLE(PreparedMeshBinding)
  static bool available(const Geometry &geometry);
  bool isCurrent(const Geometry &geometry) const;
  const Parameter *prior(Token name) const;
  const std::vector<const Parameter *> &parameters() const;
  void bind(Token name, const Array *array, bool enabled);
  void set(Token name, const std::optional<Parameter> &parameter);
  void prepareNotifications();
  void commit(Geometry &geometry) noexcept;

 private:
  Object::ParameterMap m_before;
  Object::ParameterMap m_parameters;
  std::vector<const Parameter *> m_notifications;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline PreparedMeshBinding::PreparedMeshBinding(const Geometry &geometry)
    : m_before(geometry.m_parameters), m_parameters(geometry.m_parameters)
{}

inline bool PreparedMeshBinding::available(const Geometry &geometry)
{
  return !geometry.m_inParameterBatch;
}

inline bool PreparedMeshBinding::isCurrent(const Geometry &geometry) const
{
  if (!available(geometry) || geometry.m_parameters.size() != m_before.size())
    return false;
  const auto same = [](const Any &a, const Any &b) {
    // Any deliberately treats invalid values as unequal, whereas two absent
    // Parameter bounds are the same prepared state.
    return (!a.valid() && !b.valid()) || a == b;
  };
  for (const auto &entry : m_before) {
    const auto *now = geometry.parameter(entry.first);
    const auto &before = entry.second;
    // Some UI metadata setters deliberately do not advance parameter clocks.
    // A whole-map swap must not overwrite such an intervening edit either.
    if (!now || !same(now->value(), before.value())
        || !same(now->min(), before.min()) || !same(now->max(), before.max())
        || now->description() != before.description()
        || now->usage() != before.usage()
        || now->isEnabled() != before.isEnabled()
        || now->stringSelection() != before.stringSelection()
        || now->stringValues() != before.stringValues())
      return false;
  }
  return true;
}

inline void PreparedMeshBinding::bind(
    Token name, const Array *array, bool enabled)
{
  if (!array) {
    if (const auto index = m_parameters.indexOfKey(name))
      m_parameters.erase(*index); // no allocating stable_partition
    return;
  }
  auto *parameter = m_parameters.at(name);
  if (!parameter) {
    m_parameters.set(name, Parameter(nullptr, name));
    parameter = m_parameters.at(name);
  }
  parameter->m_value = Any(array->type(), array->index());
  parameter->m_enabled = enabled;
}

inline void PreparedMeshBinding::set(
    Token name, const std::optional<Parameter> &parameter)
{
  if (parameter)
    m_parameters.set(name, *parameter);
  else if (const auto index = m_parameters.indexOfKey(name))
    m_parameters.erase(*index);
}

inline const Parameter *PreparedMeshBinding::prior(Token name) const
{
  return m_parameters.at(name);
}

inline void PreparedMeshBinding::prepareNotifications()
{
  m_notifications.reserve(m_parameters.size());
}

inline const std::vector<const Parameter *> &PreparedMeshBinding::parameters()
    const
{
  return m_notifications;
}

inline void PreparedMeshBinding::commit(Geometry &geometry) noexcept
{
  assert(m_notifications.capacity() >= m_parameters.size());
  // All map/control allocation, numeric values and referenced pool slots are
  // prepared. Direct count bookkeeping deliberately avoids zero-use callbacks.
  for (const auto &entry : geometry.m_parameters)
    if (anari::isArray(entry.second.value().type())) {
      const auto index = entry.second.value().getAsObjectIndex();
      --geometry.scene()->getObject<Array>(index)->m_useCounts.parameter;
    }
  for (const auto &entry : m_parameters)
    if (anari::isArray(entry.second.value().type())) {
      const auto index = entry.second.value().getAsObjectIndex();
      ++geometry.scene()->getObject<Array>(index)->m_useCounts.parameter;
    }
  std::swap(geometry.m_parameters, m_parameters);
  for (auto &entry : geometry.m_parameters) {
    entry.second.setObserver(&geometry);
    m_notifications.push_back(&entry.second);
  }
  ++geometry.m_versions.parameter;
}

} // namespace vsr::scene::detail

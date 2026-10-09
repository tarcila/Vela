// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/scene/Parameter.hpp"
#include "vsr/scene/Object.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"

namespace vsr::scene {

Parameter::Parameter(ParameterObserver *object, Token name) : m_name(name)
{
  setObserver(object);
}

Parameter::Parameter(const Parameter &p)
    : m_name(p.m_name),
      m_description(p.m_description),
      m_usageHint(p.m_usageHint),
      m_value(p.m_value),
      m_min(p.m_min),
      m_max(p.m_max),
      m_stringValues(p.m_stringValues),
      m_stringSelection(p.m_stringSelection),
      m_enabled(p.m_enabled)
{}

Parameter::Parameter(Parameter &&p)
    : Parameter(static_cast<const Parameter &>(p))
{}

Parameter &Parameter::operator=(const Parameter &p)
{
  if (this == &p)
    return *this;
  Parameter prepared(p); // Allocate before spending history or changing a slot.
  auto owner = metadataChanging();
  auto oldValue = std::move(m_value);
  if (!observer())
    m_name = prepared.m_name;
  m_description = std::move(prepared.m_description);
  m_usageHint = prepared.m_usageHint;
  m_value = std::move(prepared.m_value);
  m_min = std::move(prepared.m_min);
  m_max = std::move(prepared.m_max);
  m_stringValues = std::move(prepared.m_stringValues);
  m_stringSelection = prepared.m_stringSelection;
  m_enabled = prepared.m_enabled;
  if (auto *o = observer())
    o->parameterChanged(this, oldValue);
  metadataChanged(owner);
  return *this;
}

Parameter &Parameter::operator=(Parameter &&p)
{
  return operator=(static_cast<const Parameter &>(p));
}

void Parameter::remove()
{
  if (auto *o = observer())
    o->removeParameter(this);
}

Token Parameter::name() const
{
  return m_name;
}

const std::string &Parameter::description() const
{
  return m_description;
}

bool Parameter::isEnabled() const
{
  return m_enabled;
}

Parameter &Parameter::setDescription(const char *d)
{
  std::string prepared(d ? d : "");
  auto owner = metadataChanging();
  m_description = std::move(prepared);
  metadataChanged(owner);
  return *this;
}

Parameter &Parameter::setValue(const Any &newValue)
{
  auto oldValue = m_value;
  m_value = newValue;
  if (!selectStringValue()) {
    logWarning(
        "'%s' is not a valid value for parameter '%s', valid values are:",
        m_value.getCStr(),
        m_name.c_str());
    for (const auto &val : m_stringValues)
      logWarning("  '%s'", val.c_str());
  }
  if (auto *o = observer())
    o->parameterChanged(this, oldValue);
  return *this;
}

Parameter &Parameter::setPreparedValue(Any &&newValue)
{
  auto oldValue = std::move(m_value);
  m_value = std::move(newValue);
  selectStringValue();
  if (auto *o = observer())
    o->parameterChanged(this, oldValue);
  return *this;
}

bool Parameter::selectStringValue()
{
  if (m_value.type() != ANARI_STRING || m_stringValues.empty())
    return true;
  const auto it = std::find(
      m_stringValues.begin(), m_stringValues.end(), m_value.getStringView());
  if (it == m_stringValues.end())
    return false;
  m_stringSelection =
      static_cast<int>(std::distance(m_stringValues.begin(), it));
  return true;
}

Parameter &Parameter::setMin(const Any &newMin)
{
  Any prepared(newMin);
  auto owner = metadataChanging();
  m_min = std::move(prepared);
  metadataChanged(owner);
  return *this;
}

Parameter &Parameter::setMax(const Any &newMax)
{
  Any prepared(newMax);
  auto owner = metadataChanging();
  m_max = std::move(prepared);
  metadataChanged(owner);
  return *this;
}

Parameter &Parameter::setStringValues(const std::vector<std::string> &sv)
{
  auto prepared = sv;
  auto owner = metadataChanging();
  m_stringValues = std::move(prepared);
  metadataChanged(owner);
  return *this;
}

Parameter &Parameter::setStringSelection(int s)
{
  auto owner = metadataChanging();
  m_stringSelection = s;
  metadataChanged(owner);
  return *this;
}

Parameter &Parameter::setUsage(ParameterUsageHint u)
{
  auto owner = metadataChanging();
  m_usageHint = u;
  metadataChanged(owner);
  return *this;
}

Parameter &Parameter::setEnabled(bool enabled)
{
  auto owner = metadataChanging();
  m_enabled = enabled;
  if (auto *o = observer())
    o->parameterChanged(this, m_value);
  metadataChanged(owner);
  return *this;
}

const Any &Parameter::value() const
{
  return m_value;
}

ParameterUsageHint Parameter::usage() const
{
  return m_usageHint;
}

const Any &Parameter::min() const
{
  return m_min;
}

const Any &Parameter::max() const
{
  return m_max;
}

bool Parameter::hasMin() const
{
  return min().type() == value().type();
}

bool Parameter::hasMax() const
{
  return max().type() == value().type();
}

const std::vector<std::string> &Parameter::stringValues() const
{
  return m_stringValues;
}

int Parameter::stringSelection() const
{
  return m_stringSelection;
}

void Parameter::setToAttribute()
{
  setStringValues({"attribute0",
      "attribute1",
      "attribute2",
      "attribute3",
      "color",
      "worldPosition",
      "worldNormal",
      "objectPosition",
      "objectNormal"});
  setStringSelection(0);
  setValue("attribute0");
}

void Parameter::relocateFrom(Parameter &&p) noexcept
{
  m_name = p.m_name;
  m_description = std::move(p.m_description);
  m_usageHint = p.m_usageHint;
  m_value = std::move(p.m_value);
  m_min = std::move(p.m_min);
  m_max = std::move(p.m_max);
  m_stringValues = std::move(p.m_stringValues);
  m_stringSelection = p.m_stringSelection;
  m_enabled = p.m_enabled;
  m_observer = p.m_observer;
  m_owner = std::move(p.m_owner);
}

void Parameter::setObserver(ParameterObserver *o)
{
  m_owner.reset();
  m_observer = o;
  if (auto *object = dynamic_cast<Object *>(o)) {
    m_owner = object->m_parameterOwner;
    m_observer = nullptr;
  }
}

ParameterObserver *Parameter::observer() const
{
  const auto lifetime = m_owner.lock();
  auto *object = lifetime ? lifetime->object : nullptr;
  if (object && object->parameter(m_name) == this)
    return object;
  return m_observer;
}

std::shared_ptr<ParameterOwnerLifetime> Parameter::metadataChanging()
{
  auto owner = m_owner.lock();
  auto *object = owner ? owner->object : nullptr;
  if (!object || object->parameter(m_name) != this)
    return {};
  object->advanceParameterMetadata();
  return owner;
}

void Parameter::metadataChanged(
    const std::shared_ptr<ParameterOwnerLifetime> &owner)
{
  // A value observer may relocate or retire the Object before this signal.
  auto *object = owner ? owner->object : nullptr;
  if (object && object->m_updateDelegate)
    object->m_updateDelegate->signalObjectMetadataChanged(object);
}

} // namespace vsr::scene

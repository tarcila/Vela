// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/scene/UpdateDelegate.hpp"
// std
#include <algorithm>
#include <exception>

namespace vsr::scene {

namespace {

// Committed writes must reach identity tracking even if an earlier consumer
// fails. Keep the original exception without allocating a callback wrapper.
template <typename Notify>
void notifyAllThenRethrow(
    const std::vector<std::shared_ptr<BaseUpdateDelegate>> &delegates,
    const Notify &notify)
{
  std::exception_ptr failure;
  for (const auto &d : delegates) {
    try {
      notify(*d);
    } catch (...) {
      if (!failure)
        failure = std::current_exception();
    }
  }
  if (failure)
    std::rethrow_exception(failure);
}

} // namespace

void BaseUpdateDelegate::signalArrayMapCancelled(const Array *a)
{
  signalArrayUnmapped(a);
}

size_t MultiUpdateDelegate::size() const
{
  return m_delegates.size();
}

void MultiUpdateDelegate::clear()
{
  m_delegates.clear();
}

void MultiUpdateDelegate::erase(const BaseUpdateDelegate *d)
{
  m_delegates.erase(std::remove_if(m_delegates.begin(),
                        m_delegates.end(),
                        [&](auto &ud) { return ud.get() == d; }),
      m_delegates.end());
}

const BaseUpdateDelegate *MultiUpdateDelegate::get(size_t i) const
{
  return m_delegates[i].get();
}

BaseUpdateDelegate *MultiUpdateDelegate::get(size_t i)
{
  return m_delegates[i].get();
}

const BaseUpdateDelegate *MultiUpdateDelegate::operator[](size_t i) const
{
  return get(i);
}

BaseUpdateDelegate *MultiUpdateDelegate::operator[](size_t i)
{
  return get(i);
}

void MultiUpdateDelegate::signalObjectAdded(const Object *o)
{
  for (auto &d : m_delegates)
    d->signalObjectAdded(o);
}

void MultiUpdateDelegate::signalParameterUpdated(
    const Object *o, const Parameter *p)
{
  for (auto &d : m_delegates)
    d->signalParameterUpdated(o, p);
}

void MultiUpdateDelegate::signalParameterRemoved(
    const Object *o, const Parameter *p)
{
  for (auto &d : m_delegates)
    d->signalParameterRemoved(o, p);
}

void MultiUpdateDelegate::signalParameterBatchUpdated(
    const Object *o, const std::vector<const Parameter *> &ps)
{
  notifyAllThenRethrow(m_delegates,
      [&](BaseUpdateDelegate &d) { d.signalParameterBatchUpdated(o, ps); });
}

void MultiUpdateDelegate::signalArrayMapped(const Array *a)
{
  for (auto &d : m_delegates)
    d->signalArrayMapped(a);
}

void MultiUpdateDelegate::signalArrayUnmapped(const Array *a)
{
  for (auto &d : m_delegates)
    d->signalArrayUnmapped(a);
}

void MultiUpdateDelegate::signalObjectParameterUseCountZero(const Object *obj)
{
  for (auto &d : m_delegates)
    d->signalObjectParameterUseCountZero(obj);
}

void MultiUpdateDelegate::signalObjectLayerUseCountZero(const Object *obj)
{
  for (auto &d : m_delegates)
    d->signalObjectLayerUseCountZero(obj);
}

void MultiUpdateDelegate::signalObjectRemoved(const Object *o)
{
  for (auto &d : m_delegates)
    d->signalObjectRemoved(o);
}

void MultiUpdateDelegate::signalRemoveAllObjects()
{
  for (auto &d : m_delegates)
    d->signalRemoveAllObjects();
}

void MultiUpdateDelegate::signalLayerAdded(const Layer *l)
{
  for (auto &d : m_delegates)
    d->signalLayerAdded(l);
}

void MultiUpdateDelegate::signalLayerStructureUpdated(const Layer *l)
{
  notifyAllThenRethrow(m_delegates,
      [&](BaseUpdateDelegate &d) { d.signalLayerStructureUpdated(l); });
}

void MultiUpdateDelegate::signalLayerTransformUpdated(const Layer *l)
{
  for (auto &d : m_delegates)
    d->signalLayerTransformUpdated(l);
}

void MultiUpdateDelegate::signalLayerRemoved(const Layer *l)
{
  for (auto &d : m_delegates)
    d->signalLayerRemoved(l);
}

void MultiUpdateDelegate::signalActiveLayersChanged()
{
  for (auto &d : m_delegates)
    d->signalActiveLayersChanged();
}

void MultiUpdateDelegate::signalObjectFilteringChanged()
{
  for (auto &d : m_delegates)
    d->signalObjectFilteringChanged();
}

void MultiUpdateDelegate::signalInvalidateCachedObjects()
{
  for (auto &d : m_delegates)
    d->signalInvalidateCachedObjects();
}

void MultiUpdateDelegate::signalUpdateBatchBegin()
{
  for (auto &d : m_delegates)
    d->signalUpdateBatchBegin();
}

void MultiUpdateDelegate::signalUpdateBatchEnd()
{
  notifyAllThenRethrow(
      m_delegates, [](BaseUpdateDelegate &d) { d.signalUpdateBatchEnd(); });
}

void MultiUpdateDelegate::signalMetadataUpdated(
    const Object *o, const char *name)
{
  for (auto &d : m_delegates)
    d->signalMetadataUpdated(o, name);
}

void MultiUpdateDelegate::signalMetadataBatchUpdated(
    const Object *o, const std::vector<std::string> &names)
{
  for (auto &d : m_delegates)
    d->signalMetadataBatchUpdated(o, names);
}

void MultiUpdateDelegate::signalObjectMetadataChanged(const Object *o)
{
  for (auto &d : m_delegates)
    d->signalObjectMetadataChanged(o);
}

} // namespace vsr::scene

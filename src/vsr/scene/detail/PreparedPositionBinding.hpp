// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scene
#include "vsr/scene/Scene.hpp"
// std
#include <cassert>

namespace vsr::scene::detail {

/* Internal owning-thread primitive. No callbacks run during preparation or
 * commit. Detached preparation cannot escape through Scene enumeration or
 * reentrant host reservation callbacks. Reserve/identify the publication slot
 * after fallible host reservations, then recheck relocation and the final
 * guard.
 */
class PreparedPositionBinding
{
 public:
  PreparedPositionBinding(
      Scene &scene, anari::DataType type, std::size_t count, const void *bytes);
  ~PreparedPositionBinding();
  VSR_NOT_COPYABLE(PreparedPositionBinding)
  VSR_NOT_MOVEABLE(PreparedPositionBinding)
  Array *array() const;
  void prepareSlot(std::size_t offset = 0, std::size_t total = 1);
  bool slotAvailable() const;
  void publish() noexcept;
  static void retain(Scene &scene,
      std::shared_ptr<const void> owner,
      std::shared_ptr<const void> lease,
      std::vector<std::weak_ptr<ArrayLifetime>> arrays);
  static void drain(Scene &scene);

 private:
  Scene *m_scene{nullptr};
  Array m_array;
  std::weak_ptr<ArrayLifetime> m_lifetime;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline PreparedPositionBinding::PreparedPositionBinding(
    Scene &scene, anari::DataType type, std::size_t count, const void *bytes)
    : m_scene(&scene), m_array(type, count), m_lifetime(m_array.lifetime())
{
  if (!scene.m_imageWrites || !scene.m_imageWrites->isOwningThread())
    throw std::runtime_error(
        "Mesh Array preparation requires owning-thread tracking");
  if (!m_array.data() || !m_array.supportsHostWriteTracking()
      || !m_array.trackImageWrites(scene.m_imageWrites))
    throw std::bad_alloc();
  m_array.setData(bytes);
  if (!m_array.dataWriteSequence() || *m_array.dataWriteSequence() == 0)
    throw std::bad_alloc();
}

inline PreparedPositionBinding::~PreparedPositionBinding() = default;

inline Array *PreparedPositionBinding::array() const
{
  const auto lifetime = m_lifetime.lock();
  return lifetime ? lifetime->array : nullptr;
}

inline void PreparedPositionBinding::prepareSlot(
    std::size_t offset, std::size_t total)
{
  if (!array())
    throw std::runtime_error("Cannot prepare retired mesh Array");
  // capacity() is the logical slot extent, unchanged by reserve(). Reserve
  // backing storage for the whole append group without enumerable slots.
  m_scene->m_db.array.reserve(m_scene->m_db.array.capacity() + total);
  m_array.m_scene = m_scene;
  m_array.m_index = m_scene->m_db.array.capacity() + offset;
  m_array.setUpdateDelegate(&m_scene->m_updateDelegate);
}

inline bool PreparedPositionBinding::slotAvailable() const
{
  // Any intervening slot growth invalidates the prepared append index.
  return array() && m_scene->m_db.array.capacity() == m_array.index();
}

inline void PreparedPositionBinding::publish() noexcept
{
  // Final owning-thread guards establish a live lifetime and reserved append
  // slot. Array's move transfers that lifetime to its published pool location;
  // no callbacks or fallible work intervene here.
  assert(slotAvailable());
  m_scene->m_db.array.insert_at(m_array.index(), std::move(m_array));
  assert(array());
}

inline void PreparedPositionBinding::retain(Scene &scene,
    std::shared_ptr<const void> owner,
    std::shared_ptr<const void> lease,
    std::vector<std::weak_ptr<ArrayLifetime>> arrays)
{
  scene.drainRetainedArrays();
  scene.m_retainedArrays.reserve(scene.m_retainedArrays.size() + 1);
  for (const auto &weak : arrays) {
    const auto lifetime = weak.lock();
    if (!lifetime || !lifetime->array)
      throw std::runtime_error("Cannot retain retired Array");
  }
  scene.m_retainedArrays.push_back(
      {owner, std::move(lease), std::move(arrays)});
  for (const auto &weak : scene.m_retainedArrays.back().arrays)
    weak.lock()->array->incUseCount(Object::UseKind::INTERNAL);
}

inline void PreparedPositionBinding::drain(Scene &scene)
{
  scene.drainRetainedArrays();
}

} // namespace vsr::scene::detail

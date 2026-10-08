// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scene
#include "vsr/scene/Scene.hpp"
// std
#include <cstring>
#include <exception>
#include <stdexcept>
#include <vector>

namespace vsr::scene::detail {

/* Internal host Image publication boundary. No writable pointer escapes: only
 * commit() can write samples. Notification leases survive delegate erasure;
 * the relocation-safe Array identity becomes null on retirement. Cancellation
 * closes only reached downstream maps and never publishes a completed token.
 * Sequence reservations remain spent, including on callback failure. */
class PreparedImageWrite
{
 public:
  explicit PreparedImageWrite(Array &array);
  ~PreparedImageWrite();
  VSR_NOT_COPYABLE(PreparedImageWrite)
  VSR_NOT_MOVEABLE(PreparedImageWrite)
  Array *array() const;
  void start();
  void commit(void *before, const void *after, std::size_t bytes) noexcept;
  bool close() noexcept;

 private:
  std::shared_ptr<ArrayLifetime> m_lifetime;
  std::vector<std::shared_ptr<BaseUpdateDelegate>> m_delegates;
  std::size_t m_reached{0};
  std::uint64_t m_reserved{0};
  bool m_open{false};
  bool m_committed{false};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline PreparedImageWrite::PreparedImageWrite(Array &array)
    : m_lifetime(array.lifetime().lock())
{
  if (!array.scene() || !array.isImage() || array.isMapped()
      || !array.m_writeSequence)
    throw std::runtime_error("PreparedImageWrite - unavailable Image");
  const auto &delegates = array.scene()->updateDelegate().m_delegates;
  // Leases fit within the prepared Image control/scratch allowance, rather
  // than admitting arbitrarily large notification bookkeeping after approval.
  if (delegates.size() > 4096)
    throw std::runtime_error("PreparedImageWrite - notification limit");
  m_delegates = delegates;
}

inline PreparedImageWrite::~PreparedImageWrite()
{
  close();
}

inline Array *PreparedImageWrite::array() const
{
  return m_lifetime ? m_lifetime->array : nullptr;
}

inline void PreparedImageWrite::start()
{
  auto *a = array();
  if (!a || m_open || a->isMapped())
    throw std::runtime_error("PreparedImageWrite - unavailable Image");
  const auto reserved = a->m_writeSequence->reserve();
  if (!reserved)
    throw std::runtime_error("PreparedImageWrite - write admission refused");
  m_reserved = *reserved;
  a->m_reservedWrite = m_reserved;
  a->m_mapped = true;
  m_open = true;
  for (const auto &delegate : m_delegates) {
    a = array();
    if (!a)
      throw std::runtime_error("PreparedImageWrite - retired Image");
    ++m_reached; // even a throwing consumer may have acquired a device map
    delegate->signalArrayMapped(a);
  }
}

inline void PreparedImageWrite::commit(
    void *before, const void *after, std::size_t bytes) noexcept
{
  auto *a = array();
  assert(a && m_open && a->m_mapped && a->m_reservedWrite == m_reserved);
  assert(bytes == a->size() * a->elementSize());
  std::memcpy(before, a->m_data, bytes);
  std::memcpy(a->m_data, after, bytes);
  a->m_completedWrite = m_reserved; // truth precedes any fallible notification
  m_committed = true;
}

inline bool PreparedImageWrite::close() noexcept
{
  if (!m_open)
    return true;
  m_open = false;
  if (auto *a = array()) {
    a->m_mapped = false;
    a->m_reservedWrite = 0;
  }
  bool ok = true;
  for (std::size_t i = 0; i < m_reached; ++i) {
    auto *a = array();
    if (!a)
      break; // retirement notification already releases downstream handles
    try {
      if (m_committed)
        m_delegates[i]->signalArrayUnmapped(a);
      else
        m_delegates[i]->signalArrayMapCancelled(a);
    } catch (...) {
      ok = false; // host transaction cleanup must reach every leased consumer
    }
  }
  return ok;
}

} // namespace vsr::scene::detail

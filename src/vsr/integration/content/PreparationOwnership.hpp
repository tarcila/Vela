// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/operations/Reservation.hpp"
// std
#include <memory>
#include <new>

namespace vsr::integration::content {

/* A scoped preparation reservation, distinct from CapacityAllocator's literal
 * allocation-lifetime charges. Scene reserve() may leave persistent live
 * backing after abort; adoption is NOT physical reclamation. The caller keeps
 * this guard until all private plans and scratch have been destroyed. It does
 * not turn all live Scene memory into a permanent FrameBudget promise. */
struct PreparationOwnership
{
  void acquire(size_t bytes, const operations::Reservation &reserve);
  void releaseAfterPrivateDestruction();
  size_t peakBound{0};
  size_t liveBacking{0};

 private:
  std::shared_ptr<const void> m_lease;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline void PreparationOwnership::acquire(
    size_t bytes, const operations::Reservation &reserve)
{
  auto lease = reserve(bytes);
  if (!lease)
    throw std::bad_alloc();
  peakBound = bytes;
  m_lease = std::move(lease);
}

inline void PreparationOwnership::releaseAfterPrivateDestruction()
{
  m_lease.reset();
}

} // namespace vsr::integration::content

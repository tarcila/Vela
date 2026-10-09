// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstddef>
#include <string>

namespace vsr::core {

/* Allocation-free admission arithmetic, not an allocator or a total-RAM
 * promise. Counts requested backing (including string terminators), with
 * simultaneous old/new storage. Unsupported container implementations must
 * refuse rather than assuming their growth policy. */
struct StorageProfile
{
  explicit StorageProfile(size_t ceiling);
  bool add(size_t count, size_t elementBytes = 1);
  bool string(const std::string &value);
  bool valid() const;
  size_t bytes() const;

 private:
  size_t m_ceiling;
  size_t m_bytes{0};
  bool m_valid{true};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline StorageProfile::StorageProfile(size_t ceiling) : m_ceiling(ceiling) {}

inline bool StorageProfile::add(size_t count, size_t elementBytes)
{
  if (!m_valid
      || (elementBytes && count > (m_ceiling - m_bytes) / elementBytes))
    return m_valid = false;
  m_bytes += count * elementBytes;
  return true;
}

inline bool StorageProfile::string(const std::string &value)
{
  // Counting inline string capacity too is conservative and makes this query
  // independent of SSO layout. A cleared string can still own large backing.
  if (value.capacity() >= m_ceiling)
    return m_valid = false;
  return add(value.capacity() + 1);
}

inline bool StorageProfile::valid() const
{
  return m_valid;
}

inline size_t StorageProfile::bytes() const
{
  return m_bytes;
}

} // namespace vsr::core

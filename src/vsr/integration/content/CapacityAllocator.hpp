// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/operations/Reservation.hpp"
// vsr_core
#include "vsr/core/TypeMacros.hpp"
// std
#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vsr::integration::content {

/* Allocation authority, not Undo policy. Every allocator request reserves its
 * actual backing (including the in-allocation owner header) before allocation.
 * Reallocation holds both old and new charges; copies acquire new charges.
 * The header follows the allocation, even after its initiating Job has died.
 * A finite arena also shares the host's existing physical reservation provider.
 */
class CapacityArena : public std::enable_shared_from_this<CapacityArena>
{
 public:
  using Reserve = std::function<std::shared_ptr<const void>(size_t)>;
  struct Charge
  {
    Charge() = default;
    Charge(Charge &&other) noexcept;
    ~Charge();
    VSR_NOT_COPYABLE(Charge)
    std::shared_ptr<CapacityArena> arena;
    std::shared_ptr<const void> physical;
    size_t bytes{0};
    bool allocated{false};
  };

  static std::shared_ptr<CapacityArena> create(
      size_t limit, operations::Reservation reserve);
  CapacityArena(size_t limit, operations::Reservation reserve);
  // Legacy worker tests/services install one immutable callback target.
  CapacityArena(size_t limit, Reserve reserve);
  VSR_NOT_COPYABLE(CapacityArena)
  VSR_NOT_MOVEABLE(CapacityArena)
  size_t backingBytes() const;
  size_t allocatorPeak() const;
  size_t limit() const;
  Charge reserve(size_t bytes);
  void allocated(Charge &charge);

 private:
  mutable std::mutex m_mutex;
  const size_t m_limit;
  operations::Reservation m_reserve;
  std::shared_ptr<const Reserve> m_legacyReserve;
  size_t m_reserved{0};
  size_t m_backing{0};
  size_t m_peak{0};
};

template <typename T>
class CapacityAllocator
{
 public:
  using value_type = T;
  using propagate_on_container_copy_assignment = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap = std::true_type;
  CapacityAllocator() = default;
  explicit CapacityAllocator(std::shared_ptr<CapacityArena> arena)
      : m_arena(std::move(arena))
  {}
  // Root controls have no parent arena: reserve their literal allocation
  // directly from the finite host authority, including last-weak deallocation.
  explicit CapacityAllocator(operations::Reservation authority)
      : m_authority(std::move(authority))
  {}
  // Keep a known, precharged result profile through last-weak deallocation.
  explicit CapacityAllocator(std::shared_ptr<const void> lease)
      : m_profileLease(std::move(lease))
  {}
  template <typename U>
  CapacityAllocator(const CapacityAllocator<U> &other)
      : m_arena(other.arena()),
        m_authority(other.authority()),
        m_profileLease(other.profileLease())
  {}
  std::shared_ptr<CapacityArena> arena() const;
  operations::Reservation authority() const;
  std::shared_ptr<const void> profileLease() const;
  T *allocate(size_t count);
  void deallocate(T *pointer, size_t count) noexcept;
  template <typename U>
  bool operator==(const CapacityAllocator<U> &other) const;
  template <typename U>
  bool operator!=(const CapacityAllocator<U> &other) const;

 private:
  static constexpr size_t UNIT = sizeof(std::max_align_t);
  static constexpr size_t HEADER_UNITS =
      (sizeof(CapacityArena::Charge) + UNIT - 1) / UNIT;
  std::shared_ptr<CapacityArena> m_arena;
  operations::Reservation m_authority;
  std::shared_ptr<const void> m_profileLease;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline CapacityArena::Charge::Charge(Charge &&other) noexcept
    : arena(std::move(other.arena)),
      physical(std::move(other.physical)),
      bytes(std::exchange(other.bytes, 0)),
      allocated(std::exchange(other.allocated, false))
{}

inline CapacityArena::Charge::~Charge()
{
  if (arena && bytes) {
    std::lock_guard<std::mutex> lock(arena->m_mutex);
    arena->m_reserved -= bytes;
    if (allocated)
      arena->m_backing -= bytes;
  }
}

inline std::shared_ptr<CapacityArena> CapacityArena::create(
    size_t limit, operations::Reservation reserve)
{
  if (!limit || !reserve)
    throw std::bad_alloc();
  return std::allocate_shared<CapacityArena>(
      CapacityAllocator<CapacityArena>(reserve), limit, reserve);
}

inline CapacityArena::CapacityArena(
    size_t limit, operations::Reservation reserve)
    : m_limit(limit), m_reserve(std::move(reserve))
{
  if (!m_limit || !m_reserve)
    throw std::runtime_error("Missing bounded physical allocation authority");
}

inline CapacityArena::CapacityArena(size_t limit, Reserve reserve)
    : m_limit(limit),
      m_legacyReserve(std::make_shared<const Reserve>(std::move(reserve)))
{
  if (!m_limit || !*m_legacyReserve)
    throw std::runtime_error("Missing bounded physical allocation authority");
}

inline size_t CapacityArena::backingBytes() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_backing;
}

inline size_t CapacityArena::allocatorPeak() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_peak;
}

inline size_t CapacityArena::limit() const
{
  return m_limit;
}

inline CapacityArena::Charge CapacityArena::reserve(size_t bytes)
{
  Charge charge;
  charge.arena = shared_from_this();
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!bytes || bytes > m_limit - m_reserved)
      throw std::bad_alloc();
    m_reserved += bytes;
    charge.bytes = bytes;
  }
  charge.physical = m_reserve ? m_reserve(bytes) : (*m_legacyReserve)(bytes);
  if (!charge.physical)
    throw std::bad_alloc();
  return charge;
}

inline void CapacityArena::allocated(Charge &charge)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_backing += charge.bytes;
  m_peak = std::max(m_peak, m_backing);
  charge.allocated = true;
}

template <typename T>
inline std::shared_ptr<CapacityArena> CapacityAllocator<T>::arena() const
{
  return m_arena;
}

template <typename T>
inline operations::Reservation CapacityAllocator<T>::authority() const
{
  return m_authority;
}

template <typename T>
inline std::shared_ptr<const void> CapacityAllocator<T>::profileLease() const
{
  return m_profileLease;
}

template <typename T>
inline T *CapacityAllocator<T>::allocate(size_t count)
{
  static_assert(alignof(T) <= alignof(std::max_align_t),
      "Overaligned backing requires an aligned allocator");
  constexpr size_t MAX = std::numeric_limits<size_t>::max();
  if (count > (MAX - HEADER_UNITS * UNIT - (UNIT - 1)) / sizeof(T))
    throw std::bad_alloc();
  const auto units = HEADER_UNITS + (count * sizeof(T) + UNIT - 1) / UNIT;
  auto charge =
      m_arena ? m_arena->reserve(units * UNIT) : CapacityArena::Charge{};
  if (!m_arena && (m_authority || m_profileLease)) {
    charge.physical = m_authority ? m_authority(units * UNIT) : m_profileLease;
    if (!charge.physical)
      throw std::bad_alloc();
  }
  auto allocator = std::allocator<std::max_align_t>{};
  auto *storage = allocator.allocate(units);
  if (m_arena)
    m_arena->allocated(charge);
  auto headerAllocator = std::allocator<CapacityArena::Charge>{};
  std::allocator_traits<decltype(headerAllocator)>::construct(headerAllocator,
      reinterpret_cast<CapacityArena::Charge *>(storage),
      std::move(charge));
  return reinterpret_cast<T *>(storage + HEADER_UNITS);
}

template <typename T>
inline void CapacityAllocator<T>::deallocate(T *pointer, size_t count) noexcept
{
  auto *storage = reinterpret_cast<std::max_align_t *>(pointer) - HEADER_UNITS;
  auto *header = reinterpret_cast<CapacityArena::Charge *>(storage);
  // Destroy/deallocate physical backing BEFORE releasing either accounting
  // owner. No raw Job/provider reference survives in this allocation.
  auto charge = std::move(*header);
  auto headerAllocator = std::allocator<CapacityArena::Charge>{};
  std::allocator_traits<decltype(headerAllocator)>::destroy(
      headerAllocator, header);
  const auto units = HEADER_UNITS + (count * sizeof(T) + UNIT - 1) / UNIT;
  std::allocator<std::max_align_t>{}.deallocate(storage, units);
}

template <typename T>
template <typename U>
inline bool CapacityAllocator<T>::operator==(
    const CapacityAllocator<U> &other) const
{
  return m_arena == other.arena() && m_authority == other.authority()
      && m_profileLease == other.profileLease();
}

template <typename T>
template <typename U>
inline bool CapacityAllocator<T>::operator!=(
    const CapacityAllocator<U> &other) const
{
  return !(*this == other);
}

} // namespace vsr::integration::content

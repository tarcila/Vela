// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/operations/Reservation.hpp"
// std
#include <functional>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace vsr::integration::operations {

/* A bounded payload's reservation follows its owner, not its producer.
 * Place this member before the covered storage so destruction frees storage
 * first. Value copies acquire independent coverage before copying payloads.
 */
struct ResultOwnership
{
  // Production bounded results bind only a shared host authority and a byte
  // count. Legacy services may install a callable once; value copies share its
  // immutable target instead of cloning unknown captures before reservation.
  class Reserve
  {
   public:
    Reserve() = default;
    Reserve(Reservation authority, size_t bytes);
    template <typename F,
        typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, Reserve>>>
    Reserve(F &&callback);
    explicit operator bool() const;
    std::shared_ptr<const void> operator()() const;
    Reserve forBytes(size_t bytes) const;

   private:
    Reservation m_authority;
    size_t m_bytes{0};
    std::shared_ptr<const std::function<std::shared_ptr<const void>()>>
        m_legacy;
  };
  ResultOwnership() = default;
  explicit ResultOwnership(Reserve reserve);
  ResultOwnership(const ResultOwnership &other);
  ResultOwnership &operator=(const ResultOwnership &other);
  ResultOwnership(ResultOwnership &&) noexcept = default;
  ResultOwnership &operator=(ResultOwnership &&) noexcept = default;

  Reserve reserve;
  std::shared_ptr<const void> lease;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline ResultOwnership::Reserve::Reserve(Reservation authority, size_t bytes)
    : m_authority(std::move(authority)), m_bytes(bytes)
{}

template <typename F, typename>
inline ResultOwnership::Reserve::Reserve(F &&callback)
{
  std::function<std::shared_ptr<const void>()> function(
      std::forward<F>(callback));
  if (function)
    m_legacy = std::make_shared<const decltype(function)>(std::move(function));
}

inline ResultOwnership::Reserve::operator bool() const
{
  return bool(m_authority) || bool(m_legacy);
}

inline std::shared_ptr<const void> ResultOwnership::Reserve::operator()() const
{
  return m_authority ? m_authority(m_bytes)
                     : (m_legacy ? (*m_legacy)() : nullptr);
}

inline ResultOwnership::Reserve ResultOwnership::Reserve::forBytes(
    size_t bytes) const
{
  return m_authority ? Reserve(m_authority, bytes) : *this;
}

inline ResultOwnership::ResultOwnership(Reserve acquire)
    : reserve(std::move(acquire)), lease(reserve ? reserve() : nullptr)
{
  if (reserve && !lease)
    throw std::bad_alloc();
}

inline ResultOwnership::ResultOwnership(const ResultOwnership &other)
    : ResultOwnership(other.reserve)
{}

inline ResultOwnership &ResultOwnership::operator=(const ResultOwnership &other)
{
  if (this != &other) {
    ResultOwnership copy(other);
    *this = std::move(copy);
  }
  return *this;
}

} // namespace vsr::integration::operations

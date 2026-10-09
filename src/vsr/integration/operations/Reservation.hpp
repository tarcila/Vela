// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstddef>
#include <memory>
#include <utility>

namespace vsr::integration::operations {

/* An already owned host allocation authority, not a copied callable. The
 * production shape is one shared service plus a noncapturing dispatch pointer.
 * Copies retain the original service/charge; they cannot clone opaque captures.
 * The host must keep the service's own backing covered for its full lifetime.
 */
class Reservation
{
 public:
  Reservation() = default;
  template <typename Service>
  static Reservation from(std::shared_ptr<Service> service);
  explicit operator bool() const;
  bool operator==(const Reservation &other) const;
  std::shared_ptr<const void> operator()(size_t bytes) const;

 private:
  std::shared_ptr<const void> m_owner;
  std::shared_ptr<const void> (*m_reserve)(const void *, size_t){nullptr};
};

// A host-owned observer uses the same noncapturing dispatch shape. It is not
// an arbitrary std::function target adopted or cloned by a worker Job.
class SharedNotification
{
 public:
  SharedNotification() = default;
  template <typename Service>
  static SharedNotification from(std::shared_ptr<Service> service);
  explicit operator bool() const;
  void operator()() const;

 private:
  std::shared_ptr<const void> m_owner;
  void (*m_notify)(const void *){nullptr};
};

// Inlined definitions ////////////////////////////////////////////////////////

template <typename Service>
inline Reservation Reservation::from(std::shared_ptr<Service> service)
{
  Reservation result;
  result.m_owner = std::move(service);
  result.m_reserve = [](const void *owner, size_t bytes) {
    return const_cast<Service *>(static_cast<const Service *>(owner))
        ->reserve(bytes);
  };
  return result;
}

inline Reservation::operator bool() const
{
  return m_owner && m_reserve;
}

inline bool Reservation::operator==(const Reservation &other) const
{
  return m_owner == other.m_owner && m_reserve == other.m_reserve;
}

inline std::shared_ptr<const void> Reservation::operator()(size_t bytes) const
{
  return *this ? m_reserve(m_owner.get(), bytes) : nullptr;
}

template <typename Service>
inline SharedNotification SharedNotification::from(
    std::shared_ptr<Service> service)
{
  SharedNotification result;
  result.m_owner = std::move(service);
  result.m_notify = [](const void *owner) {
    static_cast<const Service *>(owner)->observe();
  };
  return result;
}

inline SharedNotification::operator bool() const
{
  return m_owner && m_notify;
}

inline void SharedNotification::operator()() const
{
  if (*this)
    m_notify(m_owner.get());
}

} // namespace vsr::integration::operations

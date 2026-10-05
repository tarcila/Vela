// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/TypeMacros.hpp"
// vsr_rendering
#include "vsr/rendering/pipeline/ImagePipeline.h"
// std
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <vector>

namespace vsr::rendering {

// Shared by all sources, retained consumers, encoders and transport drains in
// one host. A reservation follows ownership, not a cache slot. Rendering/Scene
// allocations themselves are outside this observation-owned budget.
class FrameBudget : public std::enable_shared_from_this<FrameBudget>
{
 public:
  explicit FrameBudget(std::size_t limit = 256u << 20);
  VSR_NOT_COPYABLE(FrameBudget)
  VSR_NOT_MOVEABLE(FrameBudget)
  std::shared_ptr<const void> reserve(std::size_t bytes);
  std::size_t used() const;
  std::size_t limit() const;

 private:
  struct Lease;
  mutable std::mutex m_mutex;
  std::size_t m_used{0};
  const std::size_t m_limit;
};

struct CpuFrameSnapshot
{
  CpuFrameSnapshot() = default;
  VSR_NOT_COPYABLE(CpuFrameSnapshot)
  VSR_NOT_MOVEABLE(CpuFrameSnapshot)
  std::uint64_t presentation{0};
  std::uint64_t production{0};
  double submittedAt{0.};
  std::uint32_t width{0}, height{0};
  bool bottomLeft{false};
  bool premultiplied{false};
  std::vector<std::uint8_t> rgba;
  std::shared_ptr<const void> reservation;
};

// An owning-thread producer at the successful display-submission seam. The
// submit function is the actual host display path, not upload/readback alone.
// CPU buffers must be final, synchronous RGBA8 output; CUDA-managed/device
// storage is deliberately not read. No Scene, integration or UI ownership.
class CpuFrameObservation
{
 public:
  using Submit =
      std::function<bool(const std::uint32_t *, std::uint32_t, std::uint32_t)>;
  explicit CpuFrameObservation(std::shared_ptr<FrameBudget> budget);
  VSR_NOT_COPYABLE(CpuFrameObservation)
  VSR_NOT_MOVEABLE(CpuFrameObservation)
  bool present(const ImagePipeline &pipeline,
      const Submit &submit,
      const std::function<bool()> &observe,
      std::uint32_t maxAxis,
      std::uint64_t maxPixels,
      bool bottomLeft = false,
      bool premultiplied = false);
  std::shared_ptr<const CpuFrameSnapshot> latest() const;
  const char *reason() const;
  std::uint64_t presentation() const;
  void retire();

 private:
  std::shared_ptr<FrameBudget> m_budget;
  std::shared_ptr<const CpuFrameSnapshot> m_latest;
  std::uint64_t m_presentation{0};
  const char *m_reason{"cold_cache"};
};

// Inlined definitions ////////////////////////////////////////////////////////

struct FrameBudget::Lease
{
  std::shared_ptr<FrameBudget> owner;
  std::size_t bytes;
  ~Lease();
};

inline FrameBudget::Lease::~Lease()
{
  std::lock_guard<std::mutex> lock(owner->m_mutex);
  owner->m_used -= bytes;
}

inline FrameBudget::FrameBudget(std::size_t limit) : m_limit(limit)
{
  if (limit < sizeof(FrameBudget) + 256 || limit > (256u << 20))
    throw std::runtime_error("invalid frame budget");
  // Includes the budget object, shared control block and allocation headers.
  m_used = sizeof(FrameBudget) + 256;
}

inline std::shared_ptr<const void> FrameBudget::reserve(std::size_t bytes)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  // Every lease also owns a control block and allocator bookkeeping. Subtract
  // before adding so even SIZE_MAX requests cannot wrap the checked counter.
  constexpr std::size_t LEASE_OVERHEAD = sizeof(Lease) + 256;
  if (!bytes || LEASE_OVERHEAD > m_limit - m_used
      || bytes > m_limit - m_used - LEASE_OVERHEAD)
    return {};
  auto owner = weak_from_this().lock();
  if (!owner)
    return {};
  auto lease = std::make_shared<Lease>();
  lease->owner = std::move(owner);
  lease->bytes = bytes + LEASE_OVERHEAD;
  // No counter change until allocation succeeds. Exceptions propagate to the
  // application adapter, consistent with the rendering library contract.
  m_used += lease->bytes;
  return lease;
}

inline std::size_t FrameBudget::used() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_used;
}

inline std::size_t FrameBudget::limit() const
{
  return m_limit;
}

inline CpuFrameObservation::CpuFrameObservation(
    std::shared_ptr<FrameBudget> budget)
    : m_budget(std::move(budget))
{
  if (!m_budget)
    throw std::runtime_error("missing frame budget");
}

inline bool CpuFrameObservation::present(const ImagePipeline &pipeline,
    const Submit &submit,
    const std::function<bool()> &observe,
    std::uint32_t maxAxis,
    std::uint64_t maxPixels,
    bool bottomLeft,
    bool premultiplied)
{
  const auto size = pipeline.dimensions();
  const auto production = pipeline.production();
  // Do not dereference GPU data, including when observation is inactive.
  if (pipeline.storage() != ImageStorage::HOST) {
    retire();
    m_reason = "unsupported_storage";
    return false;
  }
  if (!size.x || !size.y || !submit || !pipeline.getColorBuffer()
      || !submit(pipeline.getColorBuffer(), size.x, size.y))
    return false;
  const auto submittedAt = std::chrono::duration<double>(
      std::chrono::steady_clock::now().time_since_epoch())
                               .count();
  if (m_presentation == std::numeric_limits<std::uint64_t>::max()) {
    retire();
    m_reason = "identity_exhausted";
    return true;
  }
  ++m_presentation;
  // A newly submitted but unacquired frame ALWAYS retires the older copy.
  m_latest.reset();
  m_reason = "cold_cache";
  if (!observe || !observe())
    return true;
  const auto after = pipeline.dimensions();
  if (pipeline.production() != production || after.x != size.x
      || after.y != size.y) {
    m_reason = "source_reused_before_acquisition";
    return true;
  }
  const std::uint64_t pixels = std::uint64_t(size.x) * size.y;
  if (size.x > maxAxis || size.y > maxAxis || pixels > maxPixels
      || pixels > (std::numeric_limits<std::size_t>::max()
                      - sizeof(CpuFrameSnapshot) - 8192)
              / 4) {
    m_reason = "oversize_source";
    return true;
  }
  auto lease = m_budget->reserve(
      std::size_t(pixels) * 4 + sizeof(CpuFrameSnapshot) + 8192);
  if (!lease) {
    m_reason = "resource_exhausted";
    return true;
  }
  auto snapshot = std::make_shared<CpuFrameSnapshot>();
  snapshot->reservation = std::move(lease);
  snapshot->presentation = m_presentation;
  snapshot->production = pipeline.production();
  snapshot->submittedAt = submittedAt;
  snapshot->width = size.x;
  snapshot->height = size.y;
  snapshot->bottomLeft = bottomLeft;
  snapshot->premultiplied = premultiplied;
  snapshot->rgba.resize(std::size_t(pixels) * 4);
  std::memcpy(
      snapshot->rgba.data(), pipeline.getColorBuffer(), snapshot->rgba.size());
  m_latest = std::move(snapshot);
  m_reason = "available";
  return true;
}

inline std::shared_ptr<const CpuFrameSnapshot> CpuFrameObservation::latest()
    const
{
  return m_latest;
}

inline const char *CpuFrameObservation::reason() const
{
  return m_reason;
}

inline std::uint64_t CpuFrameObservation::presentation() const
{
  return m_presentation;
}

inline void CpuFrameObservation::retire()
{
  m_latest.reset();
  m_reason = "retired_source";
}

} // namespace vsr::rendering

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/content/CapacityAllocator.hpp"
// vsr_core
#include "vsr/core/TypeMacros.hpp"
// std
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace vsr::integration::content {

/* Host-owned read authority. Approval pins a directory; it neither changes
 * its permissions nor requires a shared asset directory to be private. Every
 * path component is opened beneath the pin without symlink resolution. Agent
 * admission must only select existing approvals, never call approve(). */
class AssetRoots
{
 public:
  static constexpr size_t MAX_ROOTS = 16;
  static constexpr size_t MAX_PATH_BYTES = 4096;
  static constexpr size_t MAX_INPUT_BYTES = 64u << 20;
  struct Root;
  using Bytes = std::vector<std::uint8_t, CapacityAllocator<std::uint8_t>>;

  class Snapshot
  {
   public:
    explicit Snapshot(std::shared_ptr<CapacityArena> arena = {},
        CapacityArena::Charge control = {});
    ~Snapshot();
    VSR_NOT_COPYABLE(Snapshot)
    VSR_NOT_MOVEABLE(Snapshot)
    const Bytes &bytes() const;
    bool isCurrent(const std::function<bool()> &cancelled = {}) const;
    // Final owner guard after worker byte validation: no payload reads.
    bool metadataCurrent() const;

   private:
    friend class AssetRoots;
    CapacityArena::Charge m_control;
    std::shared_ptr<Root> m_root;
    int m_file{-1};
    std::string m_relative;
    Bytes m_bytes;
    struct Currency;
    std::unique_ptr<Currency> m_currency;
  };

  AssetRoots() = default;
  ~AssetRoots();
  VSR_NOT_COPYABLE(AssetRoots)
  VSR_NOT_MOVEABLE(AssetRoots)
  void setCapacityArena(std::shared_ptr<CapacityArena> arena);
  std::optional<size_t> approve(const std::string &absoluteDirectory);
  bool revoke(size_t root);
  size_t size() const;
  // Cooperative native-I/O boundary: regular files only; snapshots are private
  // immutable bytes. Currency/revocation must be rechecked at final safe point.
  std::shared_ptr<Snapshot> read(size_t root,
      const std::string &relative,
      size_t byteLimit = MAX_INPUT_BYTES,
      const std::function<bool()> &cancelled = {},
      std::shared_ptr<CapacityArena> arena = {}) const;

 private:
  using Roots = std::vector<std::shared_ptr<Root>,
      CapacityAllocator<std::shared_ptr<Root>>>;
  mutable std::mutex m_mutex;
  std::shared_ptr<CapacityArena> m_arena;
  Roots m_roots;
};

} // namespace vsr::integration::content

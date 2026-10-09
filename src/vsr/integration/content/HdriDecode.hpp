// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/content/AssetRoots.hpp"
// vsr_core
#include "vsr/core/VSRMath.hpp"
// std
#include <cstdint>
#include <functional>
#include <vector>

namespace vsr::integration::content {

struct HdriDecodeLimits
{
  size_t inputBytes{64u << 20};
  size_t pixels{16u << 20};
  size_t workingBytes{512u << 20};
  size_t dimension{16384};
};

enum class HdriDecodeError
{
  NONE,
  UNSUPPORTED,
  MALFORMED,
  LIMIT,
  CANCELLED
};

struct DecodedHdri
{
  size_t width{0};
  size_t height{0};
  // Linear RGB FLOAT32, bottom row first, for Light radiance, not Sampler
  // Images. The admitted profile is Radiance RGBE, -Y +X, flat or scanline
  // RLE. Old repeat-marker RGBE and other orientations are not advertised.
  using Radiance =
      std::vector<core::math::float3, CapacityAllocator<core::math::float3>>;
  Radiance radiance;
};

struct HdriDecodeResult
{
  HdriDecodeError error{HdriDecodeError::NONE};
  DecodedHdri image;
};

// Decode immutable private bytes only: no filename reopening or decoder I/O.
// Working bound includes source capacity, output, one RGBE scanline and a
// future Scene Array copy; it is distinct from retained removal metadata.
// Cancellation is cooperative between bounded rows, not a hard I/O timeout.
HdriDecodeResult decodeHdri(const std::vector<std::uint8_t> &bytes,
    const HdriDecodeLimits &limits = {},
    const std::function<bool()> &cancelled = {},
    std::shared_ptr<CapacityArena> arena = {});
HdriDecodeResult decodeHdri(const AssetRoots::Bytes &bytes,
    const HdriDecodeLimits &limits = {},
    const std::function<bool()> &cancelled = {},
    std::shared_ptr<CapacityArena> arena = {});

} // namespace vsr::integration::content

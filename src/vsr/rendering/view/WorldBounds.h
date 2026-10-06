// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace vsr::rendering {

enum class WorldBoundsState
{
  AVAILABLE,
  EMPTY,
  PENDING,
  UNAVAILABLE
};

enum class WorldBoundsReason
{
  NONE,
  MISSING_BINDING,
  UNQUALIFIED_SOURCE,
  INACTIVE_SOURCE,
  RETIRED_SOURCE,
  SOURCE_TRANSITION,
  PROPERTY_UNAVAILABLE,
  INVALID_BOUNDS,
  INVALID_METADATA
};

// Opaque public identity only, never a view name, camera or address. Host
// adapters bound each string to 128 bytes before copying. Generation changes
// whenever the world/device or source lifetime changes. No Scene coverage
// stamp.
struct WorldBoundsSource
{
  std::string identity;
  std::uint64_t generation{0};
  std::string deviceIdentifier;
  std::string deviceSubtype;
};

struct WorldBoundsObservation
{
  WorldBoundsState state{WorldBoundsState::UNAVAILABLE};
  WorldBoundsReason reason{WorldBoundsReason::MISSING_BINDING};
  WorldBoundsSource source;
  std::array<double, 3> min{};
  std::array<double, 3> max{};
};

// Explicit external driver seam, owning service thread only. observe may query
// the existing ANARI world (ANARI_WAIT is NOT interruptible). The host must
// qualify finite servicing latency before binding it; no render/populate/reset.
// isCurrent only checks cached source identity/lifetime, never queries a
// driver. EMPTY requires a provider-proved convention, not arbitrary nonfinite
// output.
struct WorldBoundsProvider
{
  std::function<WorldBoundsObservation()> observe;
  std::function<bool(const WorldBoundsSource &)> isCurrent;
};

} // namespace vsr::rendering

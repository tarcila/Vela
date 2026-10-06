// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_rendering
#include "vsr/rendering/view/WorldBounds.h"
// std
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>

namespace vsr::integration::inspection::bounds {

// Fixed host limits, not client overrides. Missing/value-only observations do
// not perform or report scans; these ceilings also govern future bounds work.
struct Limits
{
  std::size_t sliceSteps{256};
  std::size_t sliceVertexValues{4096};
  std::size_t totalNodes{4096};
  std::size_t totalVertexValues{1048576};
  double deadlineSeconds{5.0};
  std::size_t scratchBytes{8u << 20};

  constexpr bool valid() const;
};

struct Args
{
  std::optional<bool> includeDisabled;
  // Any supplied node target rejects, including an empty/malformed handle.
  std::optional<std::string> nodeHandle;
};

constexpr std::size_t MAX_RESULT_BYTES = 2048;
// Covers bounded JSON construction copies as well as the final result.
constexpr std::size_t TEMPORARY_BYTES = 4 * MAX_RESULT_BYTES;

// Validate bounded provider-owned values before JSON construction/retention.
const char *stateName(rendering::WorldBoundsState state);
const char *reasonName(rendering::WorldBoundsReason reason);
bool validSource(const rendering::WorldBoundsSource &source);
const char *validate(const rendering::WorldBoundsObservation &observation);

// Inlined definitions ////////////////////////////////////////////////////////

inline const char *stateName(rendering::WorldBoundsState state)
{
  using S = rendering::WorldBoundsState;
  switch (state) {
  case S::AVAILABLE:
    return "available";
  case S::EMPTY:
    return "empty";
  case S::PENDING:
    return "pending";
  case S::UNAVAILABLE:
    return "unavailable";
  }
  return nullptr;
}

inline const char *reasonName(rendering::WorldBoundsReason reason)
{
  using R = rendering::WorldBoundsReason;
  switch (reason) {
  case R::NONE:
    return nullptr;
  case R::MISSING_BINDING:
    return "missing_binding";
  case R::UNQUALIFIED_SOURCE:
    return "unqualified_source";
  case R::INACTIVE_SOURCE:
    return "inactive_source";
  case R::RETIRED_SOURCE:
    return "retired_source";
  case R::SOURCE_TRANSITION:
    return "source_transition";
  case R::PROPERTY_UNAVAILABLE:
    return "property_unavailable";
  case R::INVALID_BOUNDS:
    return "invalid_bounds";
  case R::INVALID_METADATA:
    return "invalid_metadata";
  }
  return "invalid_metadata";
}

inline bool validSource(const rendering::WorldBoundsSource &source)
{
  auto token = [](const std::string &s) {
    if (s.empty() || s.size() > 128)
      return false;
    for (unsigned char c : s)
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
              || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'
              || c == ':' || c == '@'))
        return false;
    return true;
  };
  return source.generation && token(source.identity)
      && token(source.deviceIdentifier) && token(source.deviceSubtype);
}

inline const char *validate(const rendering::WorldBoundsObservation &o)
{
  using S = rendering::WorldBoundsState;
  using R = rendering::WorldBoundsReason;
  if (!stateName(o.state))
    return "invalid_metadata";
  if ((o.state == S::AVAILABLE || o.state == S::EMPTY)
      && (o.reason != R::NONE || !validSource(o.source)))
    return "invalid_metadata";
  if (o.state == S::PENDING && o.reason != R::SOURCE_TRANSITION)
    return "invalid_metadata";
  if (o.state == S::UNAVAILABLE
      && (o.reason == R::NONE || o.reason > R::INVALID_METADATA))
    return "invalid_metadata";
  if (!o.source.identity.empty() && !validSource(o.source))
    return "invalid_metadata";
  if (o.state == S::AVAILABLE)
    for (std::size_t i = 0; i < 3; ++i)
      if (!std::isfinite(o.min[i]) || !std::isfinite(o.max[i])
          || o.min[i] > o.max[i])
        return "invalid_bounds";
  return nullptr;
}

inline constexpr bool Limits::valid() const
{
  return sliceSteps > 0 && sliceSteps <= totalNodes && sliceVertexValues > 0
      && sliceVertexValues <= totalVertexValues && deadlineSeconds > 0
      && deadlineSeconds <= 5.0 && scratchBytes > 0
      && scratchBytes <= (8u << 20);
}

} // namespace vsr::integration::inspection::bounds

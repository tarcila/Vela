// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
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

// Inlined definitions ////////////////////////////////////////////////////////

inline constexpr bool Limits::valid() const
{
  return sliceSteps > 0 && sliceSteps <= totalNodes && sliceVertexValues > 0
      && sliceVertexValues <= totalVertexValues && deadlineSeconds > 0
      && deadlineSeconds <= 5.0 && scratchBytes > 0
      && scratchBytes <= (8u << 20);
}

} // namespace vsr::integration::inspection::bounds

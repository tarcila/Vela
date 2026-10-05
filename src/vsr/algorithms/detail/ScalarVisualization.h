// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_algorithms
#include "vsr/algorithms/math/device_macros.h"
// std
#include <cstdint>
#include <limits>

namespace vsr::algorithms::detail {

struct ScalarRange
{
  double min{std::numeric_limits<double>::max()};
  double max{-std::numeric_limits<double>::max()};
};

VSR_HOST_DEVICE_FCN inline bool finiteScalar(double value)
{
  return value <= std::numeric_limits<double>::max()
      && value >= -std::numeric_limits<double>::max();
}

struct MergeScalarRange
{
  VSR_HOST_DEVICE_FCN ScalarRange operator()(
      ScalarRange a, ScalarRange b) const;
};

VSR_HOST_DEVICE_FCN inline ScalarRange MergeScalarRange::operator()(
    ScalarRange a, ScalarRange b) const
{
  return {a.min < b.min ? a.min : b.min, a.max > b.max ? a.max : b.max};
}

template <typename T>
struct SampleScalarRange
{
  VSR_HOST_DEVICE_FCN ScalarRange operator()(T sample) const;
};

template <typename T>
VSR_HOST_DEVICE_FCN inline ScalarRange SampleScalarRange<T>::operator()(
    T sample) const
{
  const double value = double(sample);
  return finiteScalar(value) ? ScalarRange{value, value} : ScalarRange{};
}

VSR_HOST_DEVICE_FCN inline uint32_t scalarPixel(double value, ScalarRange range)
{
  double gray = 0.;
  if (finiteScalar(value) && range.min <= range.max) {
    // A constant finite frame is mid-gray, not background.
    gray = range.min == range.max
        ? 0.5
        : (value - range.min) / (range.max - range.min);
    gray = gray < 0. ? 0. : gray > 1. ? 1. : gray;
  }
  const auto byte = uint32_t(gray * 255. + 0.5);
  return 0xff000000u | byte | (byte << 8) | (byte << 16);
}

} // namespace vsr::algorithms::detail

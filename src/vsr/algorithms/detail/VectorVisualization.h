// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_algorithms
#include "ScalarVisualization.h"
// std
#include <cmath>

namespace vsr::algorithms::detail {

struct VectorSample
{
  const void *data;
  int components;
  int component; // Negative selects Euclidean magnitude.
  bool bytes{false};
  bool integers{false};

  VSR_HOST_DEVICE_FCN double value(size_t i, int c) const;
  VSR_HOST_DEVICE_FCN double operator()(size_t i) const;
};

struct VectorColor
{
  VectorSample sample;
  bool normal;
  bool srgb;

  VSR_HOST_DEVICE_FCN uint32_t operator()(size_t i) const;
};

struct VectorRange
{
  VectorSample sample;
  VSR_HOST_DEVICE_FCN ScalarRange operator()(size_t i) const;
};

// Inlined definitions ////////////////////////////////////////////////////////

VSR_HOST_DEVICE_FCN inline double VectorSample::value(size_t i, int c) const
{
  const size_t offset = i * components + c;
  return bytes   ? double(static_cast<const uint8_t *>(data)[offset]) / 255.
      : integers ? double(static_cast<const uint32_t *>(data)[offset])
                 : double(static_cast<const float *>(data)[offset]);
}

VSR_HOST_DEVICE_FCN inline double VectorSample::operator()(size_t i) const
{
  if (component >= 0)
    return value(i, component);
  double sum = 0.;
  for (int c = 0; c < components; ++c) {
    const double v = value(i, c);
    if (!finiteScalar(v))
      return std::numeric_limits<double>::infinity();
    sum += v * v;
  }
  return sqrt(sum);
}

VSR_HOST_DEVICE_FCN inline uint32_t VectorColor::operator()(size_t i) const
{
  uint32_t pixel = sample.components == 3 ? 0xff000000u : 0u;
  for (int c = 0; c < sample.components; ++c) {
    double v = sample.value(i, c);
    if (!finiteScalar(v))
      return 0xff000000u;
    if (normal && c < 3)
      v = (v + 1.) * 0.5;
    v = v < 0. ? 0. : v > 1. ? 1. : v;
    // IEC 61966-2-1; alpha is always linear, normals are diagnostic.
    if (!normal && !srgb && c < 3)
      v = v <= 0.0031308 ? 12.92 * v : 1.055 * pow(v, 1. / 2.4) - 0.055;
    const uint32_t byte = uint32_t(v * 255. + 0.5);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    pixel |= byte << (24 - 8 * c);
#else
    pixel |= byte << (8 * c);
#endif
  }
  return pixel;
}

VSR_HOST_DEVICE_FCN inline ScalarRange VectorRange::operator()(size_t i) const
{
  return SampleScalarRange<double>{}(sample(i));
}

} // namespace vsr::algorithms::detail

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_algorithms
#include "vsr/algorithms/math/device_macros.h"
// anari
#include <anari/anari.h>
// std
#include <cstddef>

namespace vsr::algorithms::detail {

struct ChannelPixelLayout
{
  int components{0};
  bool bytes{false};
  bool srgb{false};
  size_t size{0};
  bool signed16{false};
};

VSR_HOST_DEVICE_FCN inline ChannelPixelLayout channelPixelLayout(
    ANARIDataType type)
{
  switch (type) {
  case ANARI_FLOAT32:
  case ANARI_UINT32:
    return {1, false, false, 4};
  case ANARI_FLOAT32_VEC2:
    return {2, false, false, 8};
  case ANARI_FLOAT32_VEC3:
    return {3, false, false, 12};
  case ANARI_FIXED16_VEC3:
    return {3, false, false, 6, true};
  case ANARI_FLOAT32_VEC4:
    return {4, false, false, 16};
  case ANARI_UFIXED8_VEC3:
    return {3, true, false, 3};
  case ANARI_UFIXED8_VEC4:
    return {4, true, false, 4};
  case ANARI_UFIXED8_RGB_SRGB:
    return {3, true, true, 3};
  case ANARI_UFIXED8_RGBA_SRGB:
    return {4, true, true, 4};
  default:
    return {};
  }
}

} // namespace vsr::algorithms::detail

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include <anari/anari.h>
// std
#include <cstdint>

namespace vsr::algorithms::cpu {

enum class ChannelVisualization
{
  GRAYSCALE,
  COLOR,
  NORMAL,
  COMPONENT_X,
  COMPONENT_Y,
  COMPONENT_Z,
  COMPONENT_W,
  MAGNITUDE
};

void visualizeChannel(const void *data,
    ANARIDataType type,
    uint32_t *color,
    uint32_t count,
    ChannelVisualization mode,
    bool autoRange,
    double minimum,
    double maximum);

} // namespace vsr::algorithms::cpu

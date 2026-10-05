// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_algorithms
#include "vsr/algorithms/cpu/visualizeChannel.hpp"
// cuda
#include <cuda_runtime_api.h>

namespace vsr::algorithms::cuda {

using cpu::ChannelVisualization;

void visualizeChannel(cudaStream_t stream,
    const void *data,
    ANARIDataType type,
    uint32_t *color,
    uint32_t count,
    ChannelVisualization mode,
    bool autoRange,
    double minimum,
    double maximum);
void visualizeChannel(const void *data,
    ANARIDataType type,
    uint32_t *color,
    uint32_t count,
    ChannelVisualization mode,
    bool autoRange,
    double minimum,
    double maximum);

} // namespace vsr::algorithms::cuda

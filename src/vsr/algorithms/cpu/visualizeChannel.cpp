// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "visualizeChannel.hpp"
// vsr_algorithms
#include "detail/parallel_for.h"
#include "detail/parallel_reduce.h"
#include "vsr/algorithms/detail/ChannelPixelLayout.h"
#include "vsr/algorithms/detail/VectorVisualization.h"

namespace vsr::algorithms::cpu {

void visualizeChannel(const void *data,
    ANARIDataType type,
    uint32_t *color,
    uint32_t count,
    ChannelVisualization mode,
    bool autoRange,
    double minimum,
    double maximum)
{
  const auto layout = algorithms::detail::channelPixelLayout(type);
  const int component = mode == ChannelVisualization::MAGNITUDE ? -1
      : mode == ChannelVisualization::GRAYSCALE
      ? 0
      : int(mode) - int(ChannelVisualization::COMPONENT_X);
  const algorithms::detail::VectorSample sample{data,
      layout.components,
      component,
      layout.bytes,
      type == ANARI_UINT32,
      layout.signed16};
  if (mode == ChannelVisualization::COLOR
      || mode == ChannelVisualization::NORMAL) {
    const algorithms::detail::VectorColor convert{
        sample, mode == ChannelVisualization::NORMAL, layout.srgb};
    detail::parallel_for(0, count, [&](uint32_t i) { color[i] = convert(i); });
    return;
  }
  auto range = autoRange ? algorithms::detail::ScalarRange{}
                         : algorithms::detail::ScalarRange{minimum, maximum};
  if (autoRange)
    range = detail::parallel_reduce(0,
        count,
        range,
        algorithms::detail::VectorRange{sample},
        algorithms::detail::MergeScalarRange{});
  detail::parallel_for(0, count, [&](uint32_t i) {
    color[i] = algorithms::detail::scalarPixel(sample(i), range);
  });
}

} // namespace vsr::algorithms::cpu

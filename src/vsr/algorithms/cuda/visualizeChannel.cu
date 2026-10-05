// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "visualizeChannel.hpp"
// vsr_algorithms
#include "vsr/algorithms/detail/ChannelPixelLayout.h"
#include "vsr/algorithms/detail/VectorVisualization.h"
// thrust
#include <thrust/execution_policy.h>
#include <thrust/for_each.h>
#include <thrust/iterator/counting_iterator.h>
#include <thrust/transform_reduce.h>

namespace vsr::algorithms::cuda {

void visualizeChannel(cudaStream_t stream,
    const void *data,
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
  const auto begin = thrust::make_counting_iterator(uint32_t(0));
  const auto policy = thrust::cuda::par.on(stream);
  if (mode == ChannelVisualization::COLOR
      || mode == ChannelVisualization::NORMAL) {
    const algorithms::detail::VectorColor convert{
        sample, mode == ChannelVisualization::NORMAL, layout.srgb};
    thrust::for_each(
        policy, begin, begin + count, [=] VSR_DEVICE_FCN(uint32_t i) {
          color[i] = convert(i);
        });
    return;
  }
  auto range = autoRange ? algorithms::detail::ScalarRange{}
                         : algorithms::detail::ScalarRange{minimum, maximum};
  if (autoRange)
    range = thrust::transform_reduce(policy,
        begin,
        begin + count,
        algorithms::detail::VectorRange{sample},
        range,
        algorithms::detail::MergeScalarRange{});
  thrust::for_each(
      policy, begin, begin + count, [=] VSR_DEVICE_FCN(uint32_t i) {
        color[i] = algorithms::detail::scalarPixel(sample(i), range);
      });
}

void visualizeChannel(const void *data,
    ANARIDataType type,
    uint32_t *color,
    uint32_t count,
    ChannelVisualization mode,
    bool autoRange,
    double minimum,
    double maximum)
{
  visualizeChannel(
      0, data, type, color, count, mode, autoRange, minimum, maximum);
}

} // namespace vsr::algorithms::cuda

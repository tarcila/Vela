// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ChannelVisualizationPass.h"
// vsr_algorithms
#include "vsr/algorithms/cpu/visualizeAOV.hpp"
#include "vsr/algorithms/cpu/visualizeChannel.hpp"
#ifdef VSR_ALGORITHMS_HAS_CUDA
#include "vsr/algorithms/cuda/visualizeAOV.hpp"
#include "vsr/algorithms/cuda/visualizeChannel.hpp"
#endif
// std
#include <algorithm>
#include <stdexcept>

namespace vsr::rendering {
namespace {
#ifdef ENABLE_CUDA
void synchronize(detail::ComputeStream stream)
{
  const auto result = cudaStreamSynchronize(stream);
  if (result != cudaSuccess)
    throw std::runtime_error(cudaGetErrorString(result));
}
#endif

vsr::algorithms::cpu::ChannelVisualization visualizationMode(
    const std::string &mode)
{
  using Mode = vsr::algorithms::cpu::ChannelVisualization;
  if (mode == "color")
    return Mode::COLOR;
  if (mode == "normal")
    return Mode::NORMAL;
  if (mode == "magnitude")
    return Mode::MAGNITUDE;
  if (mode.compare(0, 10, "component-") == 0)
    return static_cast<Mode>(
        int(Mode::COMPONENT_X) + std::string("xyzw").find(mode.back()));
  return Mode::GRAYSCALE;
}
} // namespace

const char *ChannelVisualizationPass::name() const
{
  return "Channel Visualization";
}

const FrameChannelSelection &ChannelVisualizationPass::selection() const
{
  return m_selection;
}

FrameChannelStatus ChannelVisualizationPass::status() const
{
  return isEnabled() ? m_status : FrameChannelStatus::PENDING;
}

const std::string &ChannelVisualizationPass::error() const
{
  return m_error;
}

bool ChannelVisualizationPass::setSelection(const FrameChannelSelection &s)
{
  m_selection = s;
  m_error.clear();
  m_status = FrameChannelStatus::PENDING;
  const bool color =
      s.deviceName == "channel.color" && s.visualization == "color";
  m_selectionValid = validChannelRange(s)
      && channelTypeSupportsVisualization(
          s.pixelType, s.deviceName, s.visualization)
      && (color || s.visualization == "grayscale" || s.visualization == "color"
          || s.visualization == "normal" || s.visualization == "magnitude"
          || s.visualization == "id-colors" || s.visualization == "edges"
          || s.visualization.compare(0, 10, "component-") == 0);
  if (!m_selectionValid) {
    m_status = FrameChannelStatus::FAILED;
    m_error = "Frame Channel '" + s.deviceName + "': "
        + (!validChannelRange(s)
                ? "Fixed range requires finite minimum < maximum"
                : "incompatible or unimplemented visualization/type; consult the channel listing");
  }
  return m_selectionValid;
}

void ChannelVisualizationPass::setUseCUDA(bool enabled)
{
  m_useCUDA = enabled;
}

std::vector<FrameChannelRequest>
ChannelVisualizationPass::requiredNamedChannels() const
{
  if (!isEnabled() || !m_selectionValid
      || (m_selection.deviceName == "channel.color"
          && m_selection.visualization == "color"))
    return {};
  return {{m_selection.deviceName, m_selection.pixelType}};
}

void ChannelVisualizationPass::rememberDisplay(const ImageBuffers &b)
{
#ifdef ENABLE_CUDA
  synchronize(b.stream);
#endif
  const auto size = dimensions();
  m_completedDisplay.assign(b.color, b.color + size_t(size.x) * size.y);
}

void ChannelVisualizationPass::updateSize()
{
  m_completedDisplay.clear();
  if (m_selectionValid)
    m_status = FrameChannelStatus::PENDING;
}

void ChannelVisualizationPass::render(ImageBuffers &b, FrameState &)
{
  const bool beauty = m_selectionValid
      && m_selection.deviceName == "channel.color"
      && m_selection.visualization == "color";
#ifdef ENABLE_CUDA
  if (!m_useCUDA || !m_selectionValid)
    synchronize(b.stream);
#endif
  const auto size = dimensions();
  const size_t count = size_t(size.x) * size.y;
  const auto result = std::find_if(b.namedChannels.begin(),
      b.namedChannels.end(),
      [&](const auto &c) { return c.deviceName == m_selection.deviceName; });
  if (beauty) {
    m_status = b.sourceStatus;
    m_error = b.sourceError;
  } else if (m_selectionValid) {
    m_error.clear();
    m_status = result == b.namedChannels.end() ? FrameChannelStatus::PENDING
                                               : result->status;
    if (m_status == FrameChannelStatus::VALID
        && (!result->data || result->pixelType != m_selection.pixelType
            || result->width != size.x || result->height != size.y)) {
      m_status = FrameChannelStatus::FAILED;
      m_error = "invalid mapped data for Frame Channel '"
          + m_selection.deviceName + "'";
    } else if (m_status == FrameChannelStatus::FAILED)
      m_error = result->error;
  }
  if (m_status != FrameChannelStatus::VALID) {
#ifdef ENABLE_CUDA
    synchronize(b.stream);
#endif
    // Retain a completed display across a pending selection, but never claim
    // that its pixels were produced by the newly requested channel.
    if (m_status == FrameChannelStatus::PENDING
        && m_completedDisplay.size() == count)
      std::copy(m_completedDisplay.begin(), m_completedDisplay.end(), b.color);
    else
      std::fill(b.color, b.color + count, 0xff000000u);
    return;
  }
  if (beauty) {
    rememberDisplay(b);
    return;
  }
  const auto stream = m_useCUDA ? b.stream : detail::ComputeStream{};
  if (m_selection.visualization == "id-colors"
      || m_selection.visualization == "edges") {
    const auto *ids = static_cast<const uint32_t *>(result->data);
    const bool edges = m_selection.visualization == "edges";
#ifdef VSR_ALGORITHMS_HAS_CUDA
    if (stream) {
      if (edges)
        vsr::algorithms::cuda::visualizeEdges(
            stream, ids, b.color, m_selection.invertEdges, size.x, size.y);
      else
        vsr::algorithms::cuda::visualizeId(
            stream, ids, b.color, size.x, size.y);
      rememberDisplay(b);
      return;
    }
#endif
    if (edges)
      vsr::algorithms::cpu::visualizeEdges(
          ids, b.color, m_selection.invertEdges, size.x, size.y);
    else
      vsr::algorithms::cpu::visualizeId(ids, b.color, size.x, size.y);
    rememberDisplay(b);
    return;
  }
  const auto mode = visualizationMode(m_selection.visualization);
  const bool autoRange = m_selection.rangePolicy == ChannelRangePolicy::AUTO;
#ifdef VSR_ALGORITHMS_HAS_CUDA
  if (stream) {
    vsr::algorithms::cuda::visualizeChannel(stream,
        result->data,
        m_selection.pixelType,
        b.color,
        uint32_t(count),
        mode,
        autoRange,
        m_selection.rangeMin,
        m_selection.rangeMax);
    rememberDisplay(b);
    return;
  }
#endif
  vsr::algorithms::cpu::visualizeChannel(result->data,
      m_selection.pixelType,
      b.color,
      uint32_t(count),
      mode,
      autoRange,
      m_selection.rangeMin,
      m_selection.rangeMax);
  rememberDisplay(b);
}

} // namespace vsr::rendering

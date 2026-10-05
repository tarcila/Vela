// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_rendering
#include "ImagePass.h"

namespace vsr::rendering {

/* Named scalar conversion, placed after beauty transforms and before overlays.
 * Color is a no-op: existing HDR/exposure/output passes remain authoritative.
 * Only VALID status describes successful visualization of the selection.
 */
struct ChannelVisualizationPass : ImagePass
{
  const char *name() const override;
  const FrameChannelSelection &selection() const;
  FrameChannelStatus status() const;
  const std::string &error() const;
  std::vector<FrameChannelRequest> requiredNamedChannels() const override;

  bool setSelection(const FrameChannelSelection &selection);
  // CPU execution remains available in a CUDA-configured pipeline.
  void setUseCUDA(bool enabled);

 protected:
  void updateSize() override;
  void render(ImageBuffers &buffers, FrameState &frame) override;

 private:
  void rememberDisplay(const ImageBuffers &buffers);

  std::vector<uint32_t> m_completedDisplay;
  FrameChannelSelection m_selection;
  FrameChannelStatus m_status{FrameChannelStatus::PENDING};
  std::string m_error;
  bool m_selectionValid{true};
  bool m_useCUDA{true};
};

} // namespace vsr::rendering

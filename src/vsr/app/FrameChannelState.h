// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/DataTree.hpp"
// vsr_rendering
#include "vsr/rendering/pipeline/FrameChannelCatalog.h"

namespace vsr::app {

// Callers choose the owning subtree: offline Application Dump settings and
// per-application UI State remain separate. No catalog index or pixel type is
// persisted. A failed load leaves the caller's selection unchanged; interactive
// callers may explain the error and fall back, noninteractive callers reject
// it.
struct LegacyFrameChannelKeys
{
  const char *mode{"aovType"};
  const char *minimum{"depthMin"};
  const char *maximum{"depthMax"};
  const char *invert{"edgeInvert"};
};

void saveFrameChannelSelection(vsr::core::DataNode &node,
    const vsr::rendering::FrameChannelSelection &selection);
// Offline loading may defer numeric range checks until overrides are merged;
// malformed fields and unknown policies are always rejected.
bool loadFrameChannelSelection(const vsr::core::DataNode *named,
    const vsr::core::DataNode *legacy,
    vsr::rendering::FrameChannelSelection &selection,
    std::string &error,
    LegacyFrameChannelKeys keys = {},
    bool validateRange = true);

} // namespace vsr::app

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_rendering
#include "vsr/rendering/pipeline/FrameChannelCatalog.h"
// std
#include <optional>

namespace vsr::app {

struct FrameChannelOptions
{
  std::optional<std::string> channel;
  std::optional<std::string> visualization;
  std::optional<vsr::rendering::ChannelRangePolicy> range;
  std::optional<float> minimum;
  std::optional<float> maximum;
};

enum class FrameChannelOptionResult
{
  UNRECOGNIZED,
  PARSED,
  ERROR
};
FrameChannelOptionResult parseFrameChannelOption(int argc,
    const char **argv,
    int &index,
    FrameChannelOptions &options,
    std::string &error);
// Saved intent is strict unless a channel override requires a compatible
// choice/default. Offline disables saved visualization precedence to resolve
// semantic/type defaults. Explicit overrides never silently fall back.
bool applyFrameChannelOptions(const FrameChannelOptions &options,
    const vsr::rendering::FrameChannelCatalog &catalog,
    vsr::rendering::FrameChannelSelection &selection,
    std::string &error,
    bool preserveSavedVisualization = true);
// Parser failures can precede --channel; scan the invocation for context.
std::string frameChannelOptionError(
    int argc, const char **argv, const std::string &error);
// Interactive adapters report invalid saved intent or a completed map failure
// and return to Color. CLI adapters continue to use strict options above.
bool resolveInteractiveFrameChannelSelection(
    const vsr::rendering::FrameChannelCatalog &catalog,
    vsr::rendering::FrameChannelSelection &selection,
    std::string &error,
    const std::string &productionError = {});
bool frameChannelShowsBeauty(
    const vsr::rendering::FrameChannelSelection &selection);
bool frameChannelUsesRange(
    const vsr::rendering::FrameChannelSelection &selection);
const char *frameChannelOptionsHelp();

} // namespace vsr::app

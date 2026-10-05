// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_app
#include "FrameChannelOptions.h"
// std
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace vsr::app {

FrameChannelOptionResult parseFrameChannelOption(int argc,
    const char **argv,
    int &index,
    FrameChannelOptions &o,
    std::string &error)
{
  const std::string arg = argv[index];
  if (arg != "--channel" && arg != "--visualization" && arg != "--range"
      && arg != "--range-min" && arg != "--range-max")
    return FrameChannelOptionResult::UNRECOGNIZED;
  if (index + 1 >= argc
      || std::string_view(argv[index + 1]).compare(0, 2, "--") == 0) {
    error = arg + " requires an argument";
    return FrameChannelOptionResult::ERROR;
  }
  const std::string value = argv[++index];
  if (arg == "--channel")
    o.channel = value;
  else if (arg == "--visualization")
    o.visualization = value;
  else if (arg == "--range") {
    if (value == "auto")
      o.range = vsr::rendering::ChannelRangePolicy::AUTO;
    else if (value == "fixed")
      o.range = vsr::rendering::ChannelRangePolicy::FIXED;
    else {
      error = "--range requires auto or fixed";
      return FrameChannelOptionResult::ERROR;
    }
  } else {
    char *end = nullptr;
    errno = 0;
    const float bound = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0' || errno == ERANGE
        || !std::isfinite(bound)) {
      error = arg + " requires a finite numeric bound";
      return FrameChannelOptionResult::ERROR;
    }
    if (arg == "--range-min")
      o.minimum = bound;
    else
      o.maximum = bound;
  }
  return FrameChannelOptionResult::PARSED;
}

bool applyFrameChannelOptions(const FrameChannelOptions &o,
    const vsr::rendering::FrameChannelCatalog &catalog,
    vsr::rendering::FrameChannelSelection &selection,
    std::string &error,
    bool preserveSavedVisualization)
{
  using namespace vsr::rendering;
  auto next = selection;
  std::string name = next.deviceName;
  if (name.compare(0, 8, "channel.") == 0)
    name.erase(0, 8);
  if (o.channel)
    name = *o.channel;
  const auto *descriptor = catalog.find(name);
  if (!o.channel && descriptor && descriptor->deviceName != next.deviceName) {
    error = "saved Frame Channel identity '" + next.deviceName
        + "' is unavailable; consult the channel listing";
    return false;
  }
  bool strict = preserveSavedVisualization || o.visualization.has_value();
  if (preserveSavedVisualization && o.channel && descriptor
      && descriptor->deviceName != next.deviceName && !o.visualization) {
    strict = std::find(descriptor->visualizations.begin(),
                 descriptor->visualizations.end(),
                 next.visualization)
        != descriptor->visualizations.end();
    if (!strict) {
      next.rangePolicy = ChannelRangePolicy::AUTO;
      next.rangeMin = 0.f;
      next.rangeMax = 1.f;
      next.invertEdges = false;
    }
  }
  if (o.visualization)
    next.visualization = *o.visualization;
  if (o.range)
    next.rangePolicy = *o.range;
  if (o.minimum.has_value() != o.maximum.has_value()
      || (next.rangePolicy == ChannelRangePolicy::AUTO
          && (o.minimum || o.maximum))
      || (o.range == ChannelRangePolicy::FIXED && (!o.minimum || !o.maximum))) {
    error =
        "--range fixed requires both --range-min and --range-max; auto accepts no bounds";
    return false;
  }
  if (o.minimum)
    next.rangeMin = *o.minimum;
  if (o.maximum)
    next.rangeMax = *o.maximum;
  if (!resolveFrameChannelSelection(catalog, name, next, error, strict))
    return false;
  selection = std::move(next);
  return true;
}

bool resolveInteractiveFrameChannelSelection(
    const vsr::rendering::FrameChannelCatalog &catalog,
    vsr::rendering::FrameChannelSelection &selection,
    std::string &error,
    const std::string &productionError)
{
  if (productionError.empty()
      && applyFrameChannelOptions({}, catalog, selection, error))
    return true;
  if (!productionError.empty())
    error = productionError;
  const auto requested = selection.deviceName;
  for (const auto &channel : catalog.channels) {
    if (channel.deviceName == requested && !channel.unavailableReason.empty())
      error += ": " + channel.unavailableReason;
  }
  error += "; Frame Channel '" + requested + "' returned to Color";
  selection = {};
  std::string fallbackError;
  if (!applyFrameChannelOptions({}, catalog, selection, fallbackError))
    error += "; " + fallbackError;
  return false;
}

bool frameChannelShowsBeauty(
    const vsr::rendering::FrameChannelSelection &selection)
{
  return selection.deviceName == "channel.color"
      && selection.visualization == "color";
}

bool frameChannelUsesRange(
    const vsr::rendering::FrameChannelSelection &selection)
{
  return selection.visualization == "grayscale"
      || selection.visualization == "magnitude"
      || selection.visualization.compare(0, 10, "component-") == 0;
}

std::string frameChannelOptionError(
    int argc, const char **argv, const std::string &error)
{
  std::string channel = "<saved/default>";
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string_view(argv[i]) == "--channel")
      channel = argv[++i];
  }
  return "Frame Channel '" + channel + "': " + error
      + "; use --list-channels for available channels and visualizations";
}

const char *frameChannelOptionsHelp()
{
  return "  --channel <name>           Exact listed presentation name\n"
         "  --visualization <mode>     Shared compatible mode\n"
         "  --range <auto|fixed>       Numeric range policy\n"
         "  --range-min <number>       Fixed minimum; requires --range-max\n"
         "  --range-max <number>       Fixed maximum; finite minimum < maximum\n"
         "  Modes: color, grayscale, normal, id-colors, edges, component-x,\n"
         "         component-y, component-z, component-w, magnitude.\n"
         "  Only listed compatible modes are accepted. Edges uses objectId.\n";
}

} // namespace vsr::app

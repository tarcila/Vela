// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "FrameChannelCatalog.h"
// vsr_rendering
#include "passes/AnariSceneRenderPass.h"
// vsr_algorithms
#include "vsr/algorithms/detail/ChannelPixelLayout.h"
// std
#include <algorithm>
#include <cmath>
#include <ostream>

namespace vsr::rendering {
namespace {
std::string presentationName(const std::string &name)
{
  return name.compare(0, 8, "channel.") == 0 ? name.substr(8) : name;
}

void appendUnique(std::vector<std::string> &values, const std::string &value)
{
  if (std::find(values.begin(), values.end(), value) == values.end())
    values.push_back(value);
}

int visualizationComponents(ANARIDataType type)
{
  return vsr::algorithms::detail::channelPixelLayout(type).components;
}

void describeCompatibility(FrameChannelDescriptor &channel)
{
  for (auto type : channel.pixelTypes) {
    const int components = visualizationComponents(type);
    if (components == 1)
      appendUnique(channel.visualizations, "grayscale");
    if (components > 1) {
      if (components >= 3)
        appendUnique(channel.visualizations, "color");
      for (int i = 0; i < components; ++i)
        appendUnique(
            channel.visualizations, std::string("component-") + "xyzw"[i]);
      appendUnique(channel.visualizations, "magnitude");
    }
    if (type == ANARI_FLOAT32_VEC3 && channel.deviceName == "channel.normal")
      appendUnique(channel.visualizations, "normal");
    if (type == ANARI_UINT32
        && (channel.deviceName == "channel.objectId"
            || channel.deviceName == "channel.primitiveId"
            || channel.deviceName == "channel.instanceId")) {
      appendUnique(channel.visualizations, "id-colors");
      if (channel.deviceName == "channel.objectId")
        appendUnique(channel.visualizations, "edges");
    }
  }
  if (channel.pixelTypes.empty())
    channel.unavailableReason =
        "pixel-type metadata unknown; mapping not verified";
  else if (channel.visualizations.empty())
    channel.unavailableReason =
        "advertised pixel types have no supported visualization";
}
} // namespace

bool validChannelRange(const FrameChannelSelection &s)
{
  return s.rangePolicy == ChannelRangePolicy::AUTO
      || (std::isfinite(s.rangeMin) && std::isfinite(s.rangeMax)
          && s.rangeMin < s.rangeMax);
}

bool channelTypeSupportsVisualization(
    ANARIDataType type, std::string_view name, std::string_view mode)
{
  FrameChannelDescriptor descriptor;
  descriptor.deviceName = std::string(name);
  descriptor.pixelTypes = {type};
  describeCompatibility(descriptor);
  return std::find(descriptor.visualizations.begin(),
             descriptor.visualizations.end(),
             mode)
      != descriptor.visualizations.end();
}

bool resolveFrameChannelSelection(const FrameChannelCatalog &catalog,
    std::string_view name,
    FrameChannelSelection &s,
    std::string &error,
    bool explicitVisualization)
{
  const auto *c = catalog.find(name);
  error.clear();
  if (!c || c->visualizations.empty()) {
    error = "unavailable Frame Channel '" + std::string(name)
        + "'; consult the channel listing";
    return false;
  }
  auto next = s;
  next.deviceName = c->deviceName;
  const auto compatible = [&](std::string_view mode) {
    return std::find(c->visualizations.begin(), c->visualizations.end(), mode)
        != c->visualizations.end();
  };
  if (!compatible(next.visualization)
      || (!explicitVisualization && s.deviceName != c->deviceName)) {
    if (explicitVisualization && !compatible(next.visualization)) {
      error = "incompatible visualization for Frame Channel '"
          + std::string(name) + "'";
      return false;
    }
    next.visualization = c->visualizations.front();
    if (c->deviceName == "channel.normal" && compatible("normal"))
      next.visualization = "normal";
    if ((c->deviceName == "channel.objectId"
            || c->deviceName == "channel.primitiveId"
            || c->deviceName == "channel.instanceId")
        && compatible("id-colors"))
      next.visualization = "id-colors";
  }
  next.pixelType = ANARI_UNKNOWN;
  for (auto type : c->pixelTypes) {
    if (channelTypeSupportsVisualization(
            type, c->deviceName, next.visualization)) {
      next.pixelType = type;
      break;
    }
  }
  if ((c->deviceName == "channel.normal" || c->deviceName == "channel.albedo")
      && std::find(
             c->pixelTypes.begin(), c->pixelTypes.end(), ANARI_FLOAT32_VEC3)
          != c->pixelTypes.end()
      && channelTypeSupportsVisualization(
          ANARI_FLOAT32_VEC3, c->deviceName, next.visualization))
    next.pixelType = ANARI_FLOAT32_VEC3;
  if (!validChannelRange(next)) {
    error = "Fixed range requires finite minimum < maximum";
    return false;
  }
  s = std::move(next);
  return true;
}

bool FrameChannelCatalog::usable() const
{
  return std::any_of(channels.begin(), channels.end(), [](const auto &c) {
    return !c.ambiguous && !c.visualizations.empty();
  });
}

const FrameChannelDescriptor *FrameChannelCatalog::find(
    std::string_view name) const
{
  for (const auto &channel : channels) {
    if (channel.presentationName == name && !channel.ambiguous)
      return &channel;
  }
  return nullptr;
}

FrameChannelCatalog discoverFrameChannels(anari::Device device)
{
  FrameChannelCatalog result;
  if (!device) {
    result.diagnostics.push_back("cannot discover channels: device is null");
    return result;
  }
  const auto *parameters =
      static_cast<const ANARIParameter *>(anariGetObjectInfo(
          device, ANARI_FRAME, nullptr, "parameter", ANARI_PARAMETER_LIST));
  if (!parameters)
    result.diagnostics.push_back(
        "Frame metadata unavailable; using standard fallback");
  if (parameters) {
    for (auto p = parameters; p->name; ++p) {
      if (p->type != ANARI_DATA_TYPE)
        continue;
      const std::string rawName = p->name;
      if (rawName.empty() || presentationName(rawName).empty()) {
        result.diagnostics.push_back("malformed empty Frame Channel name");
        continue;
      }
      auto existing = std::find_if(result.channels.begin(),
          result.channels.end(),
          [&](const auto &c) { return c.deviceName == rawName; });
      if (existing == result.channels.end()) {
        FrameChannelDescriptor channel;
        channel.deviceName = rawName;
        channel.presentationName = presentationName(rawName);
        channel.advertised = true;
        result.channels.push_back(std::move(channel));
        existing = result.channels.end() - 1;
      }
      const auto *types =
          static_cast<const ANARIDataType *>(anariGetParameterInfo(device,
              ANARI_FRAME,
              nullptr,
              p->name,
              p->type,
              "value",
              ANARI_DATA_TYPE_LIST));
      if (types) {
        for (; *types != ANARI_UNKNOWN; ++types) {
          if (std::find(existing->pixelTypes.begin(),
                  existing->pixelTypes.end(),
                  *types)
              == existing->pixelTypes.end())
            existing->pixelTypes.push_back(*types);
        }
      }
    }
  }
  const auto fallback = [&](const char *name,
                            std::vector<ANARIDataType> types,
                            const char *extension = nullptr) {
    if (extension && !deviceSupportsExtension(device, extension))
      return;
    if (std::any_of(result.channels.begin(),
            result.channels.end(),
            [&](const auto &c) { return c.deviceName == name; }))
      return;
    FrameChannelDescriptor channel;
    channel.deviceName = name;
    channel.presentationName = presentationName(name);
    channel.pixelTypes = std::move(types);
    result.channels.push_back(std::move(channel));
  };
  fallback("channel.color",
      {ANARI_UFIXED8_RGBA_SRGB, ANARI_UFIXED8_VEC4, ANARI_FLOAT32_VEC4});
  fallback("channel.depth", {ANARI_FLOAT32});
  fallback(
      "channel.objectId", {ANARI_UINT32}, "ANARI_KHR_FRAME_CHANNEL_OBJECT_ID");
  fallback("channel.primitiveId",
      {ANARI_UINT32},
      "ANARI_KHR_FRAME_CHANNEL_PRIMITIVE_ID");
  fallback("channel.instanceId",
      {ANARI_UINT32},
      "ANARI_KHR_FRAME_CHANNEL_INSTANCE_ID");
  fallback(
      "channel.normal", {ANARI_FLOAT32_VEC3}, "ANARI_KHR_FRAME_CHANNEL_NORMAL");
  fallback(
      "channel.albedo", {ANARI_FLOAT32_VEC3}, "ANARI_KHR_FRAME_CHANNEL_ALBEDO");
  for (auto &channel : result.channels) {
    describeCompatibility(channel);
    for (const auto &other : result.channels) {
      if (other.deviceName != channel.deviceName
          && other.presentationName == channel.presentationName) {
        channel.ambiguous = true;
        channel.unavailableReason =
            "ambiguous presentation name; use corrected device metadata";
      }
    }
    if (channel.ambiguous)
      result.diagnostics.push_back(
          "ambiguous Frame Channel: " + channel.deviceName);
  }
  return result;
}

void printFrameChannels(std::ostream &out, const FrameChannelCatalog &catalog)
{
  out << "Frame Channels (type compatibility only; rendering/mapping not verified):\n";
  for (const auto &channel : catalog.channels) {
    out << "  " << channel.presentationName
        << " [device name: " << channel.deviceName << "; "
        << (channel.advertised ? "advertised" : "standard fallback")
        << "]\n    types:";
    if (channel.pixelTypes.empty())
      out << " unknown";
    for (auto type : channel.pixelTypes) {
      out << ' ' << anari::toString(type);
      if (visualizationComponents(type) == 0)
        out << " (unsupported visualization type)";
    }
    out << "\n    compatible visualizations:";
    if (channel.visualizations.empty() || channel.ambiguous)
      out << " unavailable";
    else
      for (const auto &mode : channel.visualizations)
        out << ' ' << mode;
    if (!channel.unavailableReason.empty())
      out << "\n    " << channel.unavailableReason;
    out << '\n';
  }
  for (const auto &diagnostic : catalog.diagnostics)
    out << "  note: " << diagnostic << '\n';
}
} // namespace vsr::rendering

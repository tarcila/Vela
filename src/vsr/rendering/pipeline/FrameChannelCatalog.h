// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include <anari/anari_cpp.hpp>
// std
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace vsr::rendering {

/* Discovery describes advertised capability, never proof of a successful map.
 * Visualization names describe type compatibility; production is validated by
 * the source when a frame is mapped. No frame buffers are allocated here.
 */
struct FrameChannelDescriptor
{
  std::string deviceName;
  std::string presentationName;
  std::vector<ANARIDataType> pixelTypes;
  std::vector<std::string> visualizations;
  std::string unavailableReason;
  bool advertised{false};
  bool mappingVerified{false};
  bool ambiguous{false};
};

struct FrameChannelCatalog
{
  std::vector<FrameChannelDescriptor> channels;
  std::vector<std::string> diagnostics;

  bool usable() const;
  const FrameChannelDescriptor *find(std::string_view presentationName) const;
};

enum class ChannelRangePolicy
{
  AUTO,
  FIXED
};

// Shared named state; storage is resolved internally, never a menu index.
struct FrameChannelSelection
{
  std::string deviceName{"channel.color"};
  std::string visualization{"color"};
  ChannelRangePolicy rangePolicy{ChannelRangePolicy::AUTO};
  float rangeMin{0.f};
  float rangeMax{1.f};
  bool invertEdges{false};
  ANARIDataType pixelType{ANARI_UFIXED8_RGBA_SRGB};
};

bool validChannelRange(const FrameChannelSelection &selection);
bool channelTypeSupportsVisualization(ANARIDataType type,
    std::string_view deviceName,
    std::string_view visualization);
bool resolveFrameChannelSelection(const FrameChannelCatalog &catalog,
    std::string_view presentationName,
    FrameChannelSelection &selection,
    std::string &error,
    bool explicitVisualization = false);

FrameChannelCatalog discoverFrameChannels(anari::Device device);
void printFrameChannels(std::ostream &out, const FrameChannelCatalog &catalog);

} // namespace vsr::rendering

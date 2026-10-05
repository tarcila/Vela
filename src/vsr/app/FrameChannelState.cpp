// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "FrameChannelState.h"
#include "vsr/rendering/pipeline/passes/VisualizeAOVPass.h"

namespace vsr::app {

void saveFrameChannelSelection(
    vsr::core::DataNode &node, const vsr::rendering::FrameChannelSelection &s)
{
  node.reset();
  node["deviceName"] = s.deviceName;
  node["visualization"] = s.visualization;
  node["rangePolicy"] =
      s.rangePolicy == vsr::rendering::ChannelRangePolicy::AUTO ? "auto"
                                                                : "fixed";
  node["rangeMin"] = s.rangeMin;
  node["rangeMax"] = s.rangeMax;
  node["invertEdges"] = s.invertEdges;
}

bool loadFrameChannelSelection(const vsr::core::DataNode *named,
    const vsr::core::DataNode *legacy,
    vsr::rendering::FrameChannelSelection &selection,
    std::string &error,
    LegacyFrameChannelKeys keys,
    bool validateRange)
{
  using namespace vsr::rendering;
  FrameChannelSelection next;
  error.clear();
  const auto read = [&](const vsr::core::DataNode *node,
                        const char *key,
                        ANARIDataType type,
                        void *value,
                        bool required = false) {
    const auto *field = node ? node->child(key) : nullptr;
    if (!field && !required)
      return true;
    if (field && field->getValue(type, value))
      return true;
    error = std::string("invalid saved Frame Channel field '") + key + "'";
    return false;
  };
  if (named) {
    std::string policy{"auto"};
    if (!read(named, "deviceName", ANARI_STRING, &next.deviceName, true)
        || !read(
            named, "visualization", ANARI_STRING, &next.visualization, true)
        || !read(named, "rangePolicy", ANARI_STRING, &policy)
        || !read(named, "rangeMin", ANARI_FLOAT32, &next.rangeMin)
        || !read(named, "rangeMax", ANARI_FLOAT32, &next.rangeMax)
        || !read(named, "invertEdges", ANARI_BOOL, &next.invertEdges))
      return false;
    if (next.deviceName.empty() || next.visualization.empty()) {
      error = "saved Frame Channel name and visualization must not be empty";
      return false;
    }
    if (policy != "auto" && policy != "fixed") {
      error = "invalid saved Frame Channel range policy '" + policy + "'";
      return false;
    }
    next.rangePolicy =
        policy == "auto" ? ChannelRangePolicy::AUTO : ChannelRangePolicy::FIXED;
  } else if (legacy) {
    int mode = int(AOVType::NONE);
    if (!read(legacy, keys.mode, ANARI_INT32, &mode)
        || !read(legacy, keys.minimum, ANARI_FLOAT32, &next.rangeMin)
        || !read(legacy, keys.maximum, ANARI_FLOAT32, &next.rangeMax)
        || !read(legacy, keys.invert, ANARI_BOOL, &next.invertEdges))
      return false;
    switch (static_cast<AOVType>(mode)) {
    case AOVType::NONE:
      break;
    case AOVType::DEPTH:
      next.deviceName = "channel.depth";
      next.visualization = "grayscale";
      if (legacy->child(keys.minimum) || legacy->child(keys.maximum))
        next.rangePolicy = ChannelRangePolicy::FIXED;
      break;
    case AOVType::ALBEDO:
      next.deviceName = "channel.albedo";
      break;
    case AOVType::NORMAL:
      next.deviceName = "channel.normal";
      next.visualization = "normal";
      break;
    case AOVType::EDGES:
      next.deviceName = "channel.objectId";
      next.visualization = "edges";
      break;
    case AOVType::OBJECT_ID:
      next.deviceName = "channel.objectId";
      next.visualization = "id-colors";
      break;
    case AOVType::PRIMITIVE_ID:
      next.deviceName = "channel.primitiveId";
      next.visualization = "id-colors";
      break;
    case AOVType::INSTANCE_ID:
      next.deviceName = "channel.instanceId";
      next.visualization = "id-colors";
      break;
    default:
      error = "unknown saved legacy AOV value " + std::to_string(mode);
      return false;
    }
  }
  if (validateRange && !validChannelRange(next)) {
    error = "saved Fixed range requires finite minimum < maximum";
    return false;
  }
  selection = std::move(next);
  return true;
}

} // namespace vsr::app

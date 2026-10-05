// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/image/ImageNormalization.hpp"
// std
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace vsr::integration {

constexpr std::uint64_t DEFAULT_FRAME_CACHE_GOAL_BYTES = 8u << 20;

// Read-only declarations. No value of this policy can expose capture or qualify
// a backend. Units are declarations, not observed samples or quality promises.
struct CapturePolicy
{
  std::uint32_t maxAxis{2048};
  std::uint64_t maxPixels{2073600};
  std::uint32_t maxQualityCount{1024};
  std::uint32_t deadlineSeconds{120};
  std::uint64_t maxPngBytes{8388608};
  std::uint64_t maxResponseBytes{12582912};
  std::uint32_t perViewCapacity{1};
  std::uint32_t globalActiveCapacity{1};
  std::uint64_t hostBudgetBytes{268435456};
  std::uint64_t deviceBudgetBytes{268435456};
  std::vector<std::string> qualityUnits;
};

inline std::string capturePolicyJson(const CapturePolicy &p,
    bool framesExposed = false,
    std::uint64_t frameHostBudgetBytes = 0)
{
  std::string units = "[";
  for (const auto &unit : p.qualityUnits) {
    if (units.size() > 1)
      units += ",";
    units += "\"" + unit + "\"";
  }
  return "{\"exposed\":false,\"reason\":\"unqualified_binding\","
      "\"presented_frames_exposed\":" + std::string(framesExposed ? "true" : "false")
      + ",\"frame_observation_policy\":{\"storage\":\"synchronous_host_cpu\","
        "\"source_status\":\"" + std::string(framesExposed ? "host_qualified" : "unexposed")
      + "\",\"encoder_capacity\":1,\"retained_result_capacity\":64,\"host_budget_bytes\":"
      + std::to_string(frameHostBudgetBytes) + "},\"frame_cache_goal_bytes\":"
      + std::to_string(DEFAULT_FRAME_CACHE_GOAL_BYTES)
      + ",\"quality_units\":" + units + "],\"quality_support\":\"host_declared_only\","
      "\"max_axis\":" + std::to_string(p.maxAxis)
      + ",\"max_pixels\":" + std::to_string(p.maxPixels)
      + ",\"max_quality_count\":" + std::to_string(p.maxQualityCount)
      + ",\"deadline_seconds\":" + std::to_string(p.deadlineSeconds)
      + ",\"max_png_bytes\":" + std::to_string(p.maxPngBytes)
      + ",\"max_response_bytes\":" + std::to_string(p.maxResponseBytes)
      + ",\"per_view_capacity\":" + std::to_string(p.perViewCapacity)
      + ",\"global_active_capacity\":" + std::to_string(p.globalActiveCapacity)
      + ",\"host_budget_bytes\":" + std::to_string(p.hostBudgetBytes)
      + ",\"device_budget_bytes\":" + std::to_string(p.deviceBudgetBytes) + "}";
}

inline bool validCapturePolicy(const CapturePolicy &p)
{
  // Current increment can narrow, not silently raise, transport/resource caps.
  if (!p.maxAxis || p.maxAxis > 2048 || !p.maxPixels
      || p.maxPixels > std::uint64_t(p.maxAxis) * p.maxAxis
      || p.maxPixels > 2073600 || !p.maxQualityCount || p.maxQualityCount > 1024
      || !p.deadlineSeconds || p.deadlineSeconds > 120 || !p.perViewCapacity
      || p.perViewCapacity > p.globalActiveCapacity
      || p.globalActiveCapacity > 64 || p.maxPngBytes > 8388608
      || p.maxResponseBytes > 12582912 || p.hostBudgetBytes > 268435456
      || p.deviceBudgetBytes > 268435456 || p.qualityUnits.size() > 2)
    return false;
  std::set<std::string> units;
  for (const auto &unit : p.qualityUnits)
    if ((unit != "samples" && unit != "completed_frames")
        || !units.insert(unit).second)
      return false;
  // Include row/filter overhead for every admissible aspect ratio. The
  // authoritative encoder bound is monotone in height at a fixed width.
  for (std::uint32_t width = 1; width <= p.maxAxis && width <= p.maxPixels;
      ++width) {
    const auto height = std::min<std::uint64_t>(p.maxAxis, p.maxPixels / width);
    const auto png = image::pngRgba8OutputBound(width, height);
    if (!png || *png > p.maxPngBytes)
      return false;
  }
  // Reserve the bounded provenance in structured content and escaped text,
  // plus the enclosing status/MCP JSON. PNG/base64 arithmetic stays checked
  // under the fixed caps above; this is not an assumed compression estimate.
  const auto response = ((p.maxPngBytes + 2) / 3) * 4 + 3 * (64u << 10) + 4096;
  const auto native = p.maxPixels * 4;
  return response <= p.maxResponseBytes
      && (native + p.maxPngBytes) * p.globalActiveCapacity <= p.hostBudgetBytes
      && native * p.globalActiveCapacity <= p.deviceBudgetBytes;
}

struct CapturePolicyObservation
{
  CapturePolicy policy;
  std::uint64_t revision{1};
  bool presentedFramesExposed{false};
  std::uint64_t frameHostBudgetBytes{0};
};

} // namespace vsr::integration

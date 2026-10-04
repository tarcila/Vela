// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/Integration.hpp"
// std
#include <charconv>
#include <cmath>
#include <set>
#include <string>

namespace vsr::integration::inspection::viewinfo {

constexpr std::size_t MAX_BINDING_BYTES = 64u << 10;

inline std::string policyJson()
{
  return "{\"exposed\":false,\"reason\":\"unqualified_binding\","
         "\"quality_units\":[],\"max_axis\":2048,\"max_pixels\":2073600,"
         "\"max_quality_count\":1024,\"deadline_seconds\":120,"
         "\"max_png_bytes\":8388608,\"max_response_bytes\":12582912,"
         "\"per_view_capacity\":1,\"global_active_capacity\":1,"
         "\"host_budget_bytes\":268435456,\"device_budget_bytes\":268435456}";
}

inline std::string number(double value)
{
  char buffer[64];
  const auto converted = std::to_chars(
      buffer, buffer + sizeof(buffer), value, std::chars_format::general, 17);
  return std::string(buffer, converted.ptr);
}

// Validate before serialization or retention; never truncate complete settings.
inline const char *validate(const EffectiveViewBinding &binding)
{
  std::size_t bytes = 2048;
  auto text = [&](const std::string &value) {
    if (value.size() > 4096 || value.size() * 6 > MAX_BINDING_BYTES - bytes)
      return false;
    bytes += value.size() * 6;
    return true;
  };
  auto settings = [&](const std::vector<EffectiveSetting> &values) {
    if (values.size() > 128)
      return false;
    std::set<std::string> names;
    for (const auto &value : values) {
      if (value.name.empty() || !text(value.name) || !text(value.text)
          || !names.insert(value.name).second || value.numbers.size() > 64
          || value.numbers.size() * 32 + 64 > MAX_BINDING_BYTES - bytes)
        return false;
      bytes += value.numbers.size() * 32 + 64;
      for (double n : value.numbers)
        if (!std::isfinite(n))
          return false;
    }
    return true;
  };
  if (!text(binding.deviceIdentifier) || !text(binding.rendererSubtype)
      || !text(binding.rendererConfigurationRevision)
      || !text(binding.pipelineSource)
      || !text(binding.pipelineConfigurationRevision)
      || !text(binding.camera.projection) || !settings(binding.rendererSettings)
      || !settings(binding.pipelineSettings)
      || !settings(binding.camera.projectionSettings))
    return "representation_unavailable";
  if (!binding.width || !binding.height || binding.deviceIdentifier.empty()
      || binding.rendererSubtype.empty()
      || binding.rendererConfigurationRevision.empty()
      || binding.pipelineSource.empty()
      || binding.pipelineConfigurationRevision.empty()
      || binding.camera.projection.empty())
    return "incomplete_binding";
  const auto &camera = binding.camera;
  for (std::size_t i = 0; i < 3; ++i)
    if (!std::isfinite(camera.position[i])
        || !std::isfinite(camera.direction[i]) || !std::isfinite(camera.up[i]))
      return "invalid_camera";
  double directionLength = 0., upLength = 0., dot = 0.;
  for (std::size_t i = 0; i < 3; ++i) {
    directionLength += camera.direction[i] * camera.direction[i];
    upLength += camera.up[i] * camera.up[i];
    dot += camera.direction[i] * camera.up[i];
  }
  const double basis = directionLength * upLength - dot * dot;
  if (!std::isfinite(basis) || basis <= 1e-12 * directionLength * upLength
      || directionLength == 0. || upLength == 0.)
    return "invalid_camera";
  if (camera.projection == "perspective") {
    if (!std::isfinite(camera.fovyDegrees) || camera.fovyDegrees <= 0.
        || camera.fovyDegrees >= 180.)
      return "invalid_camera";
  } else if (camera.projectionSettings.empty())
    return "incomplete_projection";
  return nullptr;
}

// `quote` is the existing inspection JSON string encoder.
template <typename Quote>
inline std::string bindingJson(const EffectiveViewBinding &binding, Quote quote)
{
  auto vector = [](const std::array<double, 3> &v) {
    return "[" + number(v[0]) + "," + number(v[1]) + "," + number(v[2]) + "]";
  };
  auto settings = [&](const std::vector<EffectiveSetting> &values) {
    std::string result = "[";
    for (const auto &value : values) {
      if (result.size() > 1)
        result += ",";
      result += "{\"name\":" + quote(value.name) + ",\"numbers\":[";
      bool first = true;
      for (double n : value.numbers) {
        if (!first)
          result += ",";
        first = false;
        result += number(n);
      }
      result += "],\"text\":" + quote(value.text) + "}";
    }
    return result + "]";
  };
  const auto &camera = binding.camera;
  std::string result =
      "\"state\":\"available\",\"camera\":{\"coordinate_space\":\"world\","
      "\"projection\":"
      + quote(camera.projection) + ",\"position\":" + vector(camera.position)
      + ",\"direction\":" + vector(camera.direction)
      + ",\"up\":" + vector(camera.up)
      + ",\"aspect\":" + number(double(binding.width) / binding.height);
  if (camera.projection == "perspective")
    result += ",\"fovy_deg\":" + number(camera.fovyDegrees);
  result += ",\"projection_settings\":" + settings(camera.projectionSettings)
      + "},\"width\":" + std::to_string(binding.width)
      + ",\"height\":" + std::to_string(binding.height)
      + ",\"device_identifier\":" + quote(binding.deviceIdentifier)
      + ",\"renderer_subtype\":" + quote(binding.rendererSubtype)
      + ",\"renderer_settings\":" + settings(binding.rendererSettings)
      + ",\"renderer_configuration_revision\":"
      + quote(binding.rendererConfigurationRevision)
      + ",\"pipeline_source\":" + quote(binding.pipelineSource)
      + ",\"pipeline_settings\":" + settings(binding.pipelineSettings)
      + ",\"pipeline_configuration_revision\":"
      + quote(binding.pipelineConfigurationRevision)
      + ",\"device_private_state\":{\"state\":\"unknown\",\"reason\":\"not_observed\"}";
  return result;
}

} // namespace vsr::integration::inspection::viewinfo

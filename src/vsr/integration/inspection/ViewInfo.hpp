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

inline std::string jsonString(const std::string &value)
{
  constexpr char HEX[] = "0123456789abcdef";
  std::string result = "\"";
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') {
      result += '\\';
      result += char(c);
    } else if (c < 32) {
      result += "\\u00";
      result += HEX[c >> 4];
      result += HEX[c & 15];
    } else
      result += char(c);
  }
  return result + "\"";
}

inline const char *roleName(ViewRole role)
{
  switch (role) {
  case ViewRole::GENERIC:
    return "generic";
  case ViewRole::HEADLESS:
    return "headless";
  case ViewRole::INTERACTIVE_VIEWPORT:
    return "interactive_viewport";
  case ViewRole::INDEPENDENT_AGENT:
    return "independent_agent";
  default:
    return "unknown";
  }
}

template <typename Quote>
inline std::string identityJson(const ViewDescription &view, Quote quote)
{
  return "\"view_identity\":" + quote(view.identity) + ",\"name\":"
      + quote(view.name) + ",\"role\":" + quote(roleName(view.role))
      + ",\"origin\":" + quote(roleName(view.role));
}

inline std::string cameraControlJson(
    const Integration &integration, ViewId view, bool renderAllowed)
{
  const auto description = integration.viewDescription(view);
  const bool interactive =
      description && description->role == ViewRole::INTERACTIVE_VIEWPORT;
  const bool supported = integration.cameraControlSupported(view);
  const bool enabled = supported
      && (interactive ? integration.agentCameraControlEnabled()
                      : renderAllowed);
  return std::string("\"camera_control\":{\"supported\":")
      + (supported ? "true" : "false")
      + ",\"enabled\":" + (enabled ? "true" : "false") + "}";
}

inline std::string policyJson()
{
  return capturePolicyJson(CapturePolicy{});
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
  if (!text(binding.displayCoordinateUnits) || !text(binding.deviceSubtype)
      || !text(binding.outputColor) || !text(binding.rendererSettingsReason)
      || !text(binding.pipelineSettingsReason)
      || !text(binding.unavailableReason) || !text(binding.deviceIdentifier)
      || !text(binding.rendererSubtype)
      || !text(binding.rendererConfigurationRevision)
      || !text(binding.pipelineSource)
      || !text(binding.pipelineConfigurationRevision)
      || !text(binding.camera.projection) || !settings(binding.rendererSettings)
      || !settings(binding.pipelineSettings)
      || !settings(binding.camera.projectionSettings))
    return "representation_unavailable";
  if (!binding.unavailableReason.empty()) {
    for (const auto *reason : {"device_transition",
             "missing_bound_source",
             "incomplete_camera_pose",
             "incomplete_projection",
             "unsupported_projection",
             "inactive_viewport",
             "unsupported_custom_frame_parameters"})
      if (binding.unavailableReason == reason)
        return reason;
    return "incomplete_binding";
  }
  if (!std::isfinite(binding.displayWidth)
      || !std::isfinite(binding.displayHeight) || binding.displayWidth < 0.
      || binding.displayHeight < 0.
      || !std::isfinite(binding.displayFramebufferScaleX)
      || !std::isfinite(binding.displayFramebufferScaleY)
      || binding.displayFramebufferScaleX < 0.
      || binding.displayFramebufferScaleY < 0.
      || !std::isfinite(binding.camera.aspect) || binding.camera.aspect < 0.)
    return "incomplete_binding";
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
inline std::string bindingJson(
    const EffectiveViewBinding &binding, Quote quote, bool rendered = false)
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
  std::string result = "\"state\":\"available\",\"provenance_kind\":"
      + quote(rendered ? "rendered" : "current_effective");
  if (!rendered)
    result +=
        ",\"rendered_configuration\":{\"state\":\"unknown\",\"reason\":\"not_observed\"}";
  result += ",\"camera\":{\"coordinate_space\":\"world\",\"projection\":"
      + quote(camera.projection) + ",\"position\":" + vector(camera.position)
      + ",\"direction\":" + vector(camera.direction)
      + ",\"up\":" + vector(camera.up)
      + ",\"pose_convention\":\"position_eye_direction_forward_up_world\""
      + ",\"fov_convention\":\"vertical_degrees\""
      + ",\"additional_settings\":{\"state\":\"unknown\",\"reason\":\"not_fully_observed\"}"
      + ",\"aspect\":"
      + number(camera.aspect > 0. ? camera.aspect
                                  : double(binding.width) / binding.height);
  if (camera.projection == "perspective")
    result += ",\"fovy_deg\":" + number(camera.fovyDegrees);
  result += ",\"projection_settings\":" + settings(camera.projectionSettings)
      + "},\"width\":" + std::to_string(binding.width)
      + ",\"height\":" + std::to_string(binding.height)
      + ",\"dimensions_kind\":\"effective_native_buffer\",\"display\":"
      + (binding.displayWidth > 0. && binding.displayHeight > 0.
              ? "{\"width\":" + number(binding.displayWidth) + ",\"height\":"
                  + number(binding.displayHeight) + ",\"scale_x\":"
                  + number(binding.displayWidth / binding.width)
                  + ",\"scale_y\":"
                  + number(binding.displayHeight / binding.height)
                  + ",\"coordinate_units\":"
                  + (binding.displayCoordinateUnits.empty()
                          ? "{\"state\":\"unknown\",\"reason\":\"not_observed\"}"
                          : quote(binding.displayCoordinateUnits))
                  + ",\"scale_convention\":\"display_units_per_native_pixel\""
                  + ",\"framebuffer_scale\":"
                  + (binding.displayFramebufferScaleX > 0.
                              && binding.displayFramebufferScaleY > 0.
                          ? "[" + number(binding.displayFramebufferScaleX) + ","
                              + number(binding.displayFramebufferScaleY) + "]"
                          : "{\"state\":\"unknown\",\"reason\":\"not_observed\"}")
                  + "}"
              : "{\"state\":\"unknown\",\"reason\":\"not_displayed\"}")
      + ",\"device_identifier\":" + quote(binding.deviceIdentifier)
      + ",\"device_subtype\":"
      + (binding.deviceSubtype.empty()
              ? "{\"state\":\"unknown\",\"reason\":\"not_observed\"}"
              : quote(binding.deviceSubtype))
      + ",\"renderer_subtype\":" + quote(binding.rendererSubtype)
      + ",\"renderer_settings\":" + settings(binding.rendererSettings)
      + ",\"renderer_settings_state\":"
      + quote(
          binding.rendererSettingsReason.empty() ? "complete" : "incomplete")
      + ",\"renderer_settings_reason\":" + quote(binding.rendererSettingsReason)
      + ",\"renderer_quality\":{\"state\":\"unknown\",\"reason\":\"samples_not_observed\"}"
      + ",\"renderer_configuration_revision\":"
      + quote(binding.rendererConfigurationRevision) + ",\"pipeline_source\":"
      + quote(binding.pipelineSource) + ",\"pipeline_settings\":"
      + settings(binding.pipelineSettings) + ",\"pipeline_settings_state\":"
      + quote(
          binding.pipelineSettingsReason.empty() ? "complete" : "incomplete")
      + ",\"pipeline_settings_reason\":" + quote(binding.pipelineSettingsReason)
      + ",\"output_color\":"
      + (binding.outputColor.empty()
              ? "{\"state\":\"unknown\",\"reason\":\"not_observed\"}"
              : quote(binding.outputColor))
      + ",\"pipeline_configuration_revision\":"
      + quote(binding.pipelineConfigurationRevision)
      + ",\"device_private_state\":{\"state\":\"unknown\",\"reason\":\"not_observed\"}";
  auto reference = [&](const char *name, const identity::ObjectHandle &handle) {
    if (!handle.valid())
      return;
    auto hex = [](std::uint64_t value) {
      char buffer[16];
      const auto converted =
          std::to_chars(buffer, buffer + sizeof(buffer), value, 16);
      return std::string(16 - (converted.ptr - buffer), '0')
          + std::string(buffer, converted.ptr);
    };
    result += std::string(",\"") + name
        + "\":" + quote(hex(handle.domain) + hex(handle.serial));
  };
  reference("camera_handle", binding.cameraHandle);
  reference("renderer_handle", binding.rendererHandle);
  if (!binding.cameraHandle.valid())
    result +=
        ",\"camera_reference\":{\"state\":\"unknown\",\"reason\":\"host_private_camera\"}";
  if (!binding.rendererHandle.valid())
    result +=
        ",\"renderer_reference\":{\"state\":\"unknown\",\"reason\":\"host_private_renderer\"}";
  return result;
}

} // namespace vsr::integration::inspection::viewinfo

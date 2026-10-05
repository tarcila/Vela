// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace vsr::rendering {

struct EffectiveSetting
{
  std::string name;
  std::vector<double> numbers;
  std::string text;
};

struct EffectiveCamera
{
  std::string projection{"perspective"};
  std::array<double, 3> position{0., 0., 0.};
  std::array<double, 3> direction{0., 0., -1.};
  std::array<double, 3> up{0., 1., 0.};
  double fovyDegrees{60.};
  // Zero means the source uses native-buffer aspect; otherwise explicit.
  double aspect{0.};
  std::vector<EffectiveSetting> projectionSettings;
};

// Public value-only host state, independent of integration wire identities.
// Not a pixel-producing receipt: asynchronous images can represent older state.
struct EffectiveView
{
  EffectiveCamera camera;
  // Host-local instance receipt, never serialized as an address or identity.
  // Same Device Identifier does not make replacement storage the same source.
  std::uint64_t sourceInstanceGeneration{0};
  std::uint32_t width{0};
  std::uint32_t height{0};
  double displayWidth{0.};
  double displayHeight{0.};
  double displayFramebufferScaleX{0.};
  double displayFramebufferScaleY{0.};
  std::string displayCoordinateUnits;
  std::string deviceIdentifier;
  std::string deviceSubtype;
  std::string rendererSubtype;
  std::vector<EffectiveSetting> rendererSettings;
  std::string rendererConfigurationRevision;
  std::string rendererSettingsReason;
  std::string pipelineSource;
  std::vector<EffectiveSetting> pipelineSettings;
  std::string pipelineConfigurationRevision;
  std::string pipelineSettingsReason;
  std::string outputColor;
  std::string unavailableReason;
};

} // namespace vsr::rendering

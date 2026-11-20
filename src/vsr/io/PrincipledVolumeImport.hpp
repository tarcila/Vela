// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/scene/Scene.hpp"
// std
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <variant>

namespace vsr::io {

struct PrincipledVolumeFieldSource
{
  std::filesystem::path file;
};

using PrincipledVolumeValue =
    std::variant<float, vsr::math::float3, bool, uint32_t>;

struct PrincipledVolumeImportOptions
{
  std::string name;
  std::optional<vsr::math::box3> bounds;
  std::map<std::string, PrincipledVolumeFieldSource> fields;
  std::map<std::string, PrincipledVolumeValue> parameters;
};

// Numeric scalars and comma-separated RGB vectors infer :value; other inputs
// infer :field where supported. Explicit semantics resolve numeric filenames.
// All errors throw std::runtime_error; failed assignments do not mutate
// options.
void applyPrincipledVolumeAssignment(
    PrincipledVolumeImportOptions &, const std::string &assignment);
void validatePrincipledVolumeOptions(const PrincipledVolumeImportOptions &);
PrincipledVolumeImportOptions readPrincipledVolumeDescription(
    const std::filesystem::path &);
void writePrincipledVolumeDescription(
    const PrincipledVolumeImportOptions &, const std::filesystem::path &);

vsr::scene::VolumeRef import_principledVolume(vsr::scene::Scene &,
    const PrincipledVolumeImportOptions &,
    vsr::scene::LayerNodeRef location = {});

} // namespace vsr::io

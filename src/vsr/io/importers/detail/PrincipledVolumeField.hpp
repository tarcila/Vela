// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "vsr/scene/objects/SpatialField.hpp"
#include <optional>

namespace vsr::io::detail {
struct PrincipledVolumeFieldInfo
{
  std::optional<vsr::math::box3> bounds;
  bool rgb{false};
};
PrincipledVolumeFieldInfo inspectPrincipledVolumeField(
    const vsr::scene::SpatialField &);
} // namespace vsr::io::detail

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CameraView.h"
#include "Manipulator.hpp"
// vsr_scene
#include "vsr/scene/objects/Camera.hpp"
// std
#include <optional>

namespace vsr::rendering {

void updateCameraObject(vsr::scene::Camera &c,
    const Manipulator &m,
    bool includeManipulatorMetadata = true);

void updateManipulatorFromCamera(Manipulator &m, const vsr::scene::Camera &c);

// The projection updateCameraObject() gives 'c' for 'm'; nothing for camera
// subtypes other than perspective and orthographic.
std::optional<CameraView> makeCameraView(
    const vsr::scene::Camera &c, const Manipulator &m);

} // namespace vsr::rendering

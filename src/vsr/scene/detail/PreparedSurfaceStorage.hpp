// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scene
#include "vsr/scene/Scene.hpp"

namespace vsr::scene::detail {

/* Reserve all parallel pool/Forest backing before private Surface insertion.
 * Ordinary insert may grow values before its slot markers; failure between
 * those allocations is not a destructible partial graph. This narrow seam
 * changes capacity only and is not live publication or a storage profile. */
class PreparedSurfaceStorage
{
 public:
  static void reserve(Scene &scene, size_t arrays);
};

// Inlined definitions ////////////////////////////////////////////////////////

inline void PreparedSurfaceStorage::reserve(Scene &scene, size_t arrays)
{
  if (!arrays || arrays > 64 || scene.m_db.array.size()
      || scene.m_db.geometry.size() || scene.m_db.surface.size()
      || scene.m_db.material.size() || scene.numberOfLayers())
    throw std::runtime_error("Surface storage requires empty private Scene");
  scene.m_db.array.reserve(arrays);
  scene.m_db.geometry.reserve(1);
  scene.m_db.material.reserve(1);
  scene.m_db.surface.reserve(1);
  scene.defaultLayer()->m_tree.reserve(3);
}

} // namespace vsr::scene::detail

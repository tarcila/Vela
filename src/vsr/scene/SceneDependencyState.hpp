// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstdint>
#include <exception>
#include <memory>

namespace vsr::scene {

struct Scene;
struct Layer;

namespace detail {

// Resolution and dependency state do not keep either owner alive. Revocation
// precedes pool destruction; weak references cannot revive on address reuse.
struct SceneDependencyState
{
  Scene *scene{nullptr};
  std::uint64_t revision{1};

  void noteChange();
};

struct LayerOwnerState
{
  Layer *layer{nullptr};
  std::weak_ptr<SceneDependencyState> scene;
  // Removal revokes dependency attachment, not live object resolution.
  bool dependencyAttached{true};
};

inline void SceneDependencyState::noteChange()
{
  if (scene && ++revision == 0)
    std::terminate();
}

} // namespace detail
} // namespace vsr::scene

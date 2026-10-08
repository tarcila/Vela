// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/mesh/MeshBundle.hpp"
// std
#include <cstring>

namespace vsr::integration::mesh {

/* Ephemeral owning-thread validation only. This pointer-bearing plan must not
 * escape a safe point or enter a worker, result, replay or restoration owner.
 * It is not a binding transaction and does not authorize a Scene write. */
struct PositionReplacement
{
  CapturePlan dependencies;
  std::size_t payload{FIELD_CAP};
  bool changed{false};
};

const char *preparePositions(const scene::Geometry &geometry,
    const Bundle &bundle,
    PositionReplacement &result);

// Inlined definitions ////////////////////////////////////////////////////////

inline const char *preparePositions(const scene::Geometry &geometry,
    const Bundle &bundle,
    PositionReplacement &result)
{
  PositionReplacement staged;
  const Payload *positions = nullptr;
  for (const auto &field : bundle.fields) {
    const auto *current = geometry.parameter(field.name.c_str());
    if (field.action == Action::REMOVE
        || (field.action == Action::REPLACE && field.name != "vertex.position")
        || !current)
      return "field_action_unsupported";
    if (current->isEnabled() != field.enabled)
      return "binding_state_change_unsupported";
    if (field.action != Action::REPLACE)
      continue;
    if (field.payload >= bundle.arrays.size())
      return "invalid_payload_reference";
    positions = &bundle.arrays[field.payload];
    staged.payload = field.payload;
    const auto *old =
        geometry.parameterValueAsObject<scene::Array>("vertex.position");
    if (!old)
      return "missing_positions";
    if (old->size() != positions->count)
      return "count_change_unsupported";
    if (positions->offset > bundle.bytes.size()
        || positions->byteLength > bundle.bytes.size() - positions->offset)
      return "invalid_payload_length";
  }
  PositionData replacement;
  if (positions)
    replacement = {bundle.data(*positions),
        positions->elementType,
        positions->count,
        positions->byteLength};
  if (const auto *reason = plan(
          geometry, staged.dependencies, positions ? &replacement : nullptr))
    return reason;
  if (positions) {
    const auto *old =
        geometry.parameterValueAsObject<scene::Array>("vertex.position");
    staged.changed = old->elementType() != positions->elementType
        || old->size() * old->elementSize() != positions->byteLength
        || std::memcmp(
            old->data(), bundle.data(*positions), positions->byteLength);
  }
  result = staged;
  return nullptr;
}

} // namespace vsr::integration::mesh

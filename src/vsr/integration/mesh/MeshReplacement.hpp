// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/mesh/MeshBundle.hpp"
// vsr_scene
#include "vsr/scene/detail/PreparedMeshBinding.hpp"
// std
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace vsr::integration::mesh {

/* Safe-point-only final-state plan; workers and restoration never own these
 * pointers. Payload groups encode explicit aliases, not byte deduplication. */
struct MeshReplacement
{
  struct Binding
  {
    std::string name;
    bool enabled{true};
    const scene::Array *retained{nullptr};
    std::size_t payload{FIELD_CAP};
  };
  CapturePlan dependencies;
  std::vector<Binding> fields;
  std::array<const scene::Array *, FIELD_CAP> reuse{};
  std::array<bool, FIELD_CAP> needed{};
  std::size_t newBytes{0};
  std::size_t newCount{0};
  bool changed{false};
};

inline bool equivalent(
    const scene::Array &array, const Bundle &bundle, const Payload &payload)
{
  return array.elementType() == payload.elementType
      && array.size() == payload.count
      && array.size() * array.elementSize() == payload.byteLength
      && std::memcmp(array.data(), bundle.data(payload), payload.byteLength)
      == 0;
}

// New-graph planning may start only from an empty detached triangle, never
// bypass an existing target's dependency inventory.
inline const char *prepareMesh(const scene::Geometry &geometry,
    const Bundle &bundle,
    MeshReplacement &result,
    bool newGeometry = false)
{
  MeshReplacement staged;
  if (!scene::detail::PreparedMeshBinding::available(geometry))
    return "parameter_batch_active";
  if (!boundedParameters(geometry))
    return "mesh_parameter_limit";
  if (newGeometry) {
    if (geometry.scene() || geometry.numParameters()
        || geometry.subtype() != scene::tokens::geometry::triangle)
      return "invalid_new_geometry";
  } else if (const auto *reason = plan(geometry, staged.dependencies, false))
    return reason;
  for (std::size_t i = 0; i < staged.dependencies.fieldCount; ++i) {
    const auto *field = staged.dependencies.fields[i];
    staged.fields.push_back({field->name().str(),
        field->isEnabled(),
        staged.dependencies.arrays[staged.dependencies.aliases[i]],
        FIELD_CAP});
  }
  std::array<const scene::Array *, FIELD_CAP> anchors{};
  for (const auto &field : bundle.fields) {
    auto found = std::find_if(staged.fields.begin(),
        staged.fields.end(),
        [&](const auto &binding) { return binding.name == field.name; });
    if (field.action == Action::REMOVE) {
      if (field.name == "vertex.position")
        return "missing_positions";
      if (found != staged.fields.end())
        staged.fields.erase(found);
      continue;
    }
    if (field.payload >= bundle.arrays.size())
      return "invalid_payload_reference";
    if (field.action == Action::PRESERVE) {
      if (found == staged.fields.end() || found->enabled != field.enabled)
        return "invalid_preserve_action";
      if (!equivalent(*found->retained, bundle, bundle.arrays[field.payload]))
        return "preserve_payload_mismatch";
      if (anchors[field.payload] && anchors[field.payload] != found->retained)
        return "alias_conflict";
      anchors[field.payload] = found->retained;
      found->payload = field.payload;
    } else {
      if (found == staged.fields.end()) {
        staged.fields.push_back(
            {field.name, field.enabled, nullptr, field.payload});
      } else {
        found->retained = nullptr;
        found->enabled = field.enabled;
        found->payload = field.payload;
      }
      staged.needed[field.payload] = true;
    }
  }
  const auto ordinary =
      geometry.numParameters() - staged.dependencies.fieldCount;
  if (staged.fields.size() + ordinary > FIELD_CAP)
    return "resource_limit";
  std::size_t added = 0;
  for (const auto &field : staged.fields)
    if (!geometry.parameter(field.name.c_str()))
      ++added;
  // A successful import must leave a target that fits immediate restoration's
  // copy bound. Conservatively include added controls before any publication.
  if (!boundedParameters(
          geometry, added * (sizeof(scene::Parameter) + sizeof(scene::Token))))
    return "mesh_parameter_limit";
  // Distinct preserve IDs must not claim an implicit split of one live Array.
  for (std::size_t a = 0; a < bundle.arrays.size(); ++a)
    for (std::size_t b = a + 1; b < bundle.arrays.size(); ++b)
      if (anchors[a] && anchors[a] == anchors[b])
        return "alias_conflict";
  for (std::size_t p = 0; p < bundle.arrays.size(); ++p) {
    if (!staged.needed[p])
      continue;
    auto *candidate = anchors[p];
    if (!candidate) {
      bool same = true;
      for (const auto &binding : staged.fields) {
        if (binding.payload != p)
          continue;
        const auto *old =
            geometry.parameterValueAsObject<scene::Array>(binding.name.c_str());
        if (!old || (candidate && old != candidate))
          same = false;
        if (!candidate)
          candidate = old;
      }
      // A byte-equivalent split must still split; omission retains its alias.
      for (const auto &binding : staged.fields)
        if (binding.payload != p && candidate
            && geometry.parameterValueAsObject<scene::Array>(
                   binding.name.c_str())
                == candidate)
          same = false;
      if (!same || !candidate
          || !equivalent(*candidate, bundle, bundle.arrays[p]))
        candidate = nullptr;
    } else if (!equivalent(*candidate, bundle, bundle.arrays[p]))
      return "alias_conflict";
    staged.reuse[p] = candidate;
    if (!candidate) {
      if (bundle.arrays[p].byteLength > PAYLOAD_CAP - staged.newBytes)
        return "resource_limit";
      staged.newBytes += bundle.arrays[p].byteLength;
      ++staged.newCount;
    }
  }
  const auto data = [&](const MeshReplacement::Binding &field) {
    if (field.retained)
      return PositionData{field.retained->data(),
          field.retained->elementType(),
          field.retained->size(),
          field.retained->size() * field.retained->elementSize()};
    const auto &payload = bundle.arrays[field.payload];
    return PositionData{bundle.data(payload),
        payload.elementType,
        payload.count,
        payload.byteLength};
  };
  const auto find = [&](const char *name) -> const MeshReplacement::Binding * {
    for (const auto &binding : staged.fields)
      if (binding.name == name)
        return &binding;
    return nullptr;
  };
  const auto *position = find("vertex.position"),
             *index = find("primitive.index");
  if (!position || !position->enabled)
    return "missing_positions";
  const auto positions = data(*position);
  if (!positions.count
      || !detail::fieldType(position->name, positions.elementType))
    return "unsupported_element_type";
  const bool indexed = index && index->enabled;
  if (!indexed && positions.count % 3)
    return "invalid_triangle_count";
  const auto triangles = indexed ? data(*index).count : positions.count / 3;
  if (triangles > PAYLOAD_CAP / 3)
    return "resource_limit";
  if (index) {
    const auto indices = data(*index);
    if (!detail::fieldType(index->name, indices.elementType))
      return "unsupported_element_type";
    for (std::size_t i = 0; i < indices.count * 3; ++i) {
      std::uint64_t value = 0;
      const auto width = representation(indices.elementType).scalarBytes;
      std::memcpy(&value,
          static_cast<const std::uint8_t *>(indices.data) + i * width,
          width);
      if (value >= positions.count)
        return "index_out_of_range";
    }
  }
  std::size_t bytes = 0;
  for (const auto &field : staged.fields) {
    const auto array = data(field);
    const auto repr = representation(array.elementType);
    if (!repr.base || !detail::fieldType(field.name, array.elementType))
      return "unsupported_element_type";
    const auto width = repr.components * repr.scalarBytes;
    if (!array.data || !array.count || array.count > PAYLOAD_CAP / width
        || array.byteLength != array.count * width)
      return "invalid_payload_length";
    const std::string_view associated = association(field.name);
    const auto count = associated == "vertex" ? positions.count
        : associated == "corner"              ? triangles * 3
                                              : triangles;
    if (array.count != count)
      return "attribute_count_mismatch";
    if (const auto *reason = finiteValues(array))
      return reason;
    // Conservative final working bound, accounting aliases only once.
    bool first = true;
    for (const auto &previous : staged.fields) {
      if (&previous == &field)
        break;
      if ((field.retained && previous.retained == field.retained)
          || (!field.retained && !previous.retained
              && previous.payload == field.payload))
        first = false;
    }
    if (first) {
      if (array.byteLength > PAYLOAD_CAP - bytes)
        return "resource_limit";
      bytes += array.byteLength;
    }
    const auto *old = geometry.parameter(field.name.c_str());
    const auto *oldArray =
        geometry.parameterValueAsObject<scene::Array>(field.name.c_str());
    const auto *finalArray =
        field.retained ? field.retained : staged.reuse[field.payload];
    if (!old || old->isEnabled() != field.enabled || oldArray != finalArray)
      staged.changed = true;
  }
  if (staged.fields.size() != staged.dependencies.fieldCount)
    staged.changed = true;
  // Removing one field and adding another with equal counts is still a change.
  for (std::size_t i = 0; i < staged.dependencies.fieldCount; ++i)
    if (!find(staged.dependencies.fields[i]->name().c_str()))
      staged.changed = true;
  result = std::move(staged);
  return nullptr;
}

} // namespace vsr::integration::mesh

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scene
#include "vsr/scene/Scene.hpp"

namespace vsr::integration::content {

/* Owning-thread, allocation-free authoritative reference scan. Counts remain
 * necessary for app/animation pins, but ordinary raw setters may omit counts.
 * A bounded scan failure never authorizes deletion. Slots, including holes,
 * and all parameters (including disabled values) spend the work budget. */
inline const char *scopedUsage(const scene::Scene &s,
    const scene::Object *light = nullptr,
    const scene::Object *radiance = nullptr,
    scene::LayerNodeRef ownedNode = {},
    size_t additionalSlots = 0,
    size_t additionalValues = 0)
{
  constexpr size_t MAX_SLOTS = 32768;
  constexpr size_t MAX_VALUES = 131072;
  if (additionalSlots > MAX_SLOTS || additionalValues > MAX_VALUES)
    return "reference_scan_limit";
  size_t slots = additionalSlots, values = additionalValues;
  const char *failure = nullptr;
  const auto references = [&](const core::Any &value) {
    if (!value.holdsObject())
      return false;
    const auto *object = s.getObject(value);
    return object && (object == light || object == radiance);
  };
  const auto scan = [&](const auto &pool) {
    if (failure)
      return;
    if (pool.capacity() > MAX_SLOTS - slots) {
      failure = "reference_scan_limit";
      return;
    }
    slots += pool.capacity();
    for (size_t i = 0; i < pool.capacity() && !failure; ++i) {
      const auto object = pool.at(i);
      if (!object)
        continue;
      const auto *array = dynamic_cast<const scene::Array *>(object.data());
      if (array && anari::isObject(array->elementType())) {
        if (array->size() > MAX_VALUES - values || !array->isHost()
            || array->isMapped() || (array->size() && !array->data())) {
          failure = "reference_scan_limit";
          break;
        }
        values += array->size();
        const auto *indices = static_cast<const size_t *>(array->data());
        for (size_t element = 0; element < array->size(); ++element)
          if (references(core::Any(array->elementType(), indices[element]))) {
            failure = "outside_use";
            break;
          }
      }
      if (object->numParameters() > (MAX_VALUES - values) / 3) {
        failure = "reference_scan_limit";
        break;
      }
      values += object->numParameters() * 3;
      for (size_t p = 0; p < object->numParameters(); ++p) {
        const auto &parameter = object->parameterAt(p);
        const auto &value = parameter.value();
        if (references(parameter.min()) || references(parameter.max())
            || (references(value)
                && !(object.data() == light
                    && std::string_view(object->parameterNameAt(p))
                        == "radiance"
                    && s.getObject(value) == radiance))) {
          failure = "outside_use";
          break;
        }
      }
    }
  };
  const auto &db = s.objectDB();
  scan(db.array);
  scan(db.surface);
  scan(db.geometry);
  scan(db.material);
  scan(db.sampler);
  scan(db.volume);
  scan(db.field);
  scan(db.light);
  scan(db.camera);
  scan(db.renderer);
  if (s.numberOfLayers() > MAX_SLOTS - slots)
    return "reference_scan_limit";
  slots += s.numberOfLayers();
  for (const auto &entry : s.layers()) {
    const auto *layer = entry.second.ptr.get();
    if (failure)
      break;
    if (layer->capacity() > MAX_SLOTS - slots)
      return "reference_scan_limit";
    slots += layer->capacity();
    for (size_t i = 0; i < layer->capacity(); ++i) {
      const auto node = layer->at(i);
      if (!node)
        continue;
      if (MAX_VALUES - values < 2)
        return "reference_scan_limit";
      values += 2;
      if ((references(node->value().valueRaw()) && node != ownedNode)
          || references(node->value().defaultValueRaw()))
        return "outside_use";
      const auto &parameters = node->value().getInstanceParameters();
      if (parameters.size() > MAX_VALUES - values)
        return "reference_scan_limit";
      values += parameters.size();
      for (const auto &parameter : parameters)
        if (references(parameter.second))
          return "outside_use";
    }
  }
  return failure;
}

} // namespace vsr::integration::content

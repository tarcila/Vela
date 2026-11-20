// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "PrincipledVolumeField.hpp"
#include "vsr/scene/Scene.hpp"
#include <nanovdb/NanoVDB.h>
#include <cmath>
#include <stdexcept>

namespace vsr::io::detail {
namespace {
using namespace vsr::scene;
using namespace vsr::math;

bool isRGB(anari::DataType type)
{
  if (type == ANARI_FLOAT32_VEC3)
    return true;
  switch (type) {
  case ANARI_FLOAT32:
  case ANARI_FLOAT64:
  case ANARI_FIXED8:
  case ANARI_FIXED16:
  case ANARI_FIXED32:
  case ANARI_UFIXED8:
  case ANARI_UFIXED16:
  case ANARI_UFIXED32:
    return false;
  default:
    throw std::runtime_error(
        "principled volume: unsupported field sample type");
  }
}

bool valid(const box3 &b)
{
  for (int i = 0; i < 3; ++i)
    if (!std::isfinite(b.lower[i]) || !std::isfinite(b.upper[i])
        || b.lower[i] >= b.upper[i])
      return false;
  return true;
}

void extend(box3 &b, const float3 &p)
{
  for (int i = 0; i < 3; ++i) {
    b.lower[i] = std::min(b.lower[i], p[i]);
    b.upper[i] = std::max(b.upper[i], p[i]);
  }
}
} // namespace

PrincipledVolumeFieldInfo inspectPrincipledVolumeField(const SpatialField &f)
{
  PrincipledVolumeFieldInfo info;
  auto subtype = f.subtype();
  auto *data = f.parameterValueAsObject<Array>("data");
  box3 b(float3(INFINITY), float3(-INFINITY));
  bool cell = false;
  if (auto *p = f.parameter("dataCentering"))
    cell = p->value().getString() == std::string("cell");
  if (subtype == tokens::spatial_field::nanovdb
      || subtype == tokens::spatial_field::nanovdbRectilinear) {
    if (!data || !data->isHost() || data->size() < sizeof(nanovdb::GridData))
      throw std::runtime_error("principled volume: missing NanoVDB data");
    auto *grid = static_cast<const nanovdb::GridData *>(data->data());
    if (grid->mGridSize > data->size()
        || grid->mGridSize
            < sizeof(nanovdb::GridData) + sizeof(nanovdb::TreeData))
      throw std::runtime_error("principled volume: invalid NanoVDB data size");
    switch (grid->mGridType) {
    case nanovdb::GridType::Vec3f:
      info.rgb = true;
      break;
    case nanovdb::GridType::Float:
    case nanovdb::GridType::Fp4:
    case nanovdb::GridType::Fp8:
    case nanovdb::GridType::Fp16:
    case nanovdb::GridType::FpN:
      break;
    default:
      throw std::runtime_error(
          "principled volume: unsupported NanoVDB sample type");
    }
    const auto &index = grid->indexBBox();
    for (int c = 0; c < 8; ++c) {
      nanovdb::Vec3d p;
      for (int i = 0; i < 3; ++i)
        p[i] = (c & (1 << i)) ? double(index.max()[i]) + (cell ? 1.0 : 0.0)
                              : index.min()[i];
      auto world = grid->mMap.applyMap(p);
      extend(b, float3(world[0], world[1], world[2]));
    }
  } else {
    if (subtype == tokens::spatial_field::unstructured) {
      data = f.parameterValueAsObject<Array>("vertex.data");
      if (!data)
        data = f.parameterValueAsObject<Array>("cell.data");
    }
    if (!data || !data->isHost())
      throw std::runtime_error("principled volume: missing host field samples");
    info.rgb = isRGB(data->elementType());
    if (subtype == tokens::spatial_field::structuredRegular) {
      auto origin = f.parameterValueAs<float3>("origin").value_or(float3(0.f));
      auto spacing = f.parameterValueAs<float3>("spacing").value_or(float3(1));
      b.lower = origin;
      b.upper = origin;
      for (int i = 0; i < 3; ++i) {
        if (!data->dim(i))
          return info;
        b.upper[i] += spacing[i] * (data->dim(i) - (cell ? 0 : 1));
        if (b.upper[i] < b.lower[i])
          std::swap(b.upper[i], b.lower[i]);
      }
    } else if (subtype == tokens::spatial_field::unstructured) {
      auto *positions = f.parameterValueAsObject<Array>("vertex.position");
      if (positions && positions->isHost()
          && positions->elementType() == ANARI_FLOAT32_VEC3)
        for (size_t i = 0; i < positions->size(); ++i)
          extend(b, positions->dataAs<float3>()[i]);
    } else if (subtype == tokens::spatial_field::amr) {
      // FLASH supplies origin/dimensions/levels without a standardized physical
      // scale in the imported object. Explicit bounds are required in that
      // case.
      return info;
    }
  }
  if (subtype == tokens::spatial_field::structuredRectilinear
      || subtype == tokens::spatial_field::nanovdbRectilinear) {
    int i = 0;
    for (auto name : {"coordsX", "coordsY", "coordsZ"}) {
      auto *axis = f.parameterValueAsObject<Array>(name);
      if (!axis || !axis->isHost() || axis->elementType() != ANARI_FLOAT32
          || axis->size() < 2)
        return info;
      b.lower[i] = axis->dataAs<float>()[0];
      b.upper[i] = axis->dataAs<float>()[axis->size() - 1];
      if (b.upper[i] < b.lower[i])
        std::swap(b.upper[i], b.lower[i]);
      ++i;
    }
  }
  if (auto roi = f.parameterValueAs<box3>("roi")) {
    for (int i = 0; i < 3; ++i) {
      b.lower[i] = std::max(b.lower[i], roi->lower[i]);
      b.upper[i] = std::min(b.upper[i], roi->upper[i]);
    }
  }
  if (valid(b))
    info.bounds = b;
  return info;
}
} // namespace vsr::io::detail

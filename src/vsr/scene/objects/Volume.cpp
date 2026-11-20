// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/scene/objects/Volume.hpp"
#include "vsr/core/ColorMapUtil.hpp"
#include "vsr/core/Logging.hpp"
#include "vsr/scene/Scene.hpp"

namespace vsr::scene {

Volume::Volume(Token stype) : Object(ANARI_VOLUME, stype)
{
  addParameter("visible").setValue(true).setDescription(
      "whether the volume is visible in the scene");
  if (stype == tokens::volume::principled) {
    addParameter("density").setValue(1.f).setMin(0.f).setDescription(
        "total extinction scale; multiplies density.field, "
        "zero outside its domain; zero disables extinction "
        "and thermal emission");
    addParameter("scattering")
        .setValue(float3(1.f))
        .setUsage(ParameterUsageHint::COLOR)
        .setDescription(
            "linear-RGB single-scattering albedo in [0, 1]; "
            "multiplies a scalar or RGB scattering.field, "
            "using the constant outside its domain");
    addParameter("transmissionColor")
        .setValue(float3(0.36787944f))
        .setUsage(ParameterUsageHint::COLOR);
    addParameter("transmissionDistance")
        .setValue(1.f)
        .setDescription(
            "world-space distance for transmissionColor at density "
            "one; must be positive when extinction is nonzero");
    addParameter("anisotropy").setValue(0.f).setMin(-1.f).setMax(1.f);
    addParameter("secondaryAnisotropy").setValue(0.f).setMin(-1.f).setMax(1.f);
    addParameter("secondaryWeight").setValue(0.f).setMin(0.f).setMax(1.f);
    addParameter("emission")
        .setValue(float3(0.f))
        .setUsage(ParameterUsageHint::COLOR)
        .setDescription(
            "linear-RGB radiance per world-space length, independent "
            "of density; multiplies a scalar or RGB emission.field, "
            "using the constant outside its domain");
    addParameter("temperature")
        .setValue(1.f)
        .setDescription(
            "kelvin; multiplies temperature.field, using the "
            "constant outside its domain; nonpositive is cold");
    addParameter("blackbodyIntensity")
        .setValue(0.f)
        .setMin(0.f)
        .setDescription(
            "thermal radiance scale; emission also requires nonzero "
            "absorption (non-white effective scattering albedo)");
    addParameter("blackbodyTint")
        .setValue(float3(1.f))
        .setUsage(ParameterUsageHint::COLOR);
  } else if (stype == tokens::volume::transferFunction1D) {
    addParameter("color")
        .setValue(float3{1.f})
        .setUsage(ParameterUsageHint::COLOR)
        .setDescription("transfer function color");
    addParameter("opacity").setValue(1.f).setDescription(
        "transfer function opacity");
    addParameter("unitDistance")
        .setValue(1.f)
        .setDescription(
            "distance after which a 'opacity' fraction of light traveling "
            "through the volume is absorbed")
        .setMin(0.f);
    float2 defaultValueRange{0.f, 1.f};
    addParameter("valueRange")
        .setValue({ANARI_FLOAT32_BOX1, &defaultValueRange})
        .setDescription("transfer function value range");

    float2 defaultOpacityControlPoints[2]{
        float2(0.f),
        float2(1.f),
    };
    setMetadataArray("opacityControlPoints",
        ANARI_FLOAT32_VEC2,
        defaultOpacityControlPoints,
        2);
  }
}

void Volume::ensureColorArray()
{
  if (subtype() != tokens::volume::transferFunction1D)
    return;

  if (parameterValueAsObject<Array>("color") != nullptr)
    return;

  auto *s = scene();
  if (s == nullptr)
    return;

  const auto samples = core::makeDefaultColorMap();
  auto array = s->createArray(ANARI_FLOAT32_VEC4, samples.size());
  if (!array)
    return;

  array->setData(samples);
  setParameterObject("color", *array);
}

ObjectPoolRef<Volume> Volume::self() const
{
  return scene() ? scene()->getObject<Volume>(index())
                 : ObjectPoolRef<Volume>{};
}

anari::Object Volume::makeANARIObject(anari::Device d) const
{
  if (subtype() == tokens::volume::principled) {
    const auto *types = anariGetObjectSubtypes(d, ANARI_VOLUME);
    bool supported = false;
    if (types)
      for (auto p = types; *p; ++p)
        supported |= subtype() == *p;
    if (!supported) {
      vsr::core::logError(
          "[Volume] device does not support principled; no fallback is available");
      return nullptr;
    }
  }
  return anari::newObject<anari::Volume>(d, subtype().c_str());
}

namespace tokens::volume {

const Token principled = "principled";
const Token gaussianSplat = "gaussianSplat";
const Token structuredRegular = "structuredRegular";
const Token structuredRectilinear = "structuredRectilinear";
const Token transferFunction1D = "transferFunction1D";

} // namespace tokens::volume

} // namespace vsr::scene

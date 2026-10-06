// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_integration
#include "vsr/integration/values/EditableValues.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// catch
#include "catch.hpp"
// std
#include <array>
#include <cfenv>
#include <cmath>
#include <cstring>
#include <limits>

using namespace vsr::integration;

namespace {

struct RoundingGuard
{
  int original{std::fegetround()};
  ~RoundingGuard();
};

RoundingGuard::~RoundingGuard()
{
  std::fesetround(original);
}

} // namespace

TEST_CASE(
    "Matrix component conversion rejects nonfinite and pre-round bound violations",
    "[integration][object-matrix][matrix-numeric]")
{
  vsr::scene::Scene scene;
  auto sampler = scene.createObject<vsr::scene::Sampler>("image2D");
  const float zero[4] = {};
  sampler->setParameter("outTransform", ANARI_FLOAT32_MAT2, zero);
  auto *parameter = sampler->parameter("outTransform");
  auto input = JsonValue::array(
      {JsonValue::numbers({1, 2}), JsonValue::numbers({3, 4})});
  for (double invalid : {std::numeric_limits<double>::infinity(),
           -std::numeric_limits<double>::infinity(),
           std::numeric_limits<double>::quiet_NaN()}) {
    for (std::size_t r = 0; r < 2; ++r)
      for (std::size_t c = 0; c < 2; ++c) {
        auto bad = input;
        bad.elements[r].elements[c].number = invalid;
        CHECK(prepareEdit(*sampler, "outTransform", bad).error
            == ValueError::NON_FINITE);
      }
  }
  const auto max = double(std::numeric_limits<float>::max());
  for (double invalid :
      {std::nextafter(max, std::numeric_limits<double>::infinity()),
          std::nextafter(-max, -std::numeric_limits<double>::infinity())}) {
    auto bad = input;
    bad.elements[1].elements[1].number = invalid;
    CHECK(prepareEdit(*sampler, "outTransform", bad).error
        == ValueError::OUT_OF_DOMAIN);
  }
  const float lower[4] = {1, 0, 0, 0};
  const float upper[4] = {1, 10, 10, 10};
  parameter->setMin(vsr::core::Any(ANARI_FLOAT32_MAT2, lower))
      .setMax(vsr::core::Any(ANARI_FLOAT32_MAT2, upper));
  REQUIRE(prepareEdit(*sampler, "outTransform", input).ok());
  for (double invalid : {std::nextafter(1., 0.), std::nextafter(1., 2.)}) {
    auto bad = input;
    bad.elements[0].elements[0].number = invalid;
    // Both would round back into the domain; input still must reject first.
    CHECK(prepareEdit(*sampler, "outTransform", bad).error
        == ValueError::OUT_OF_DOMAIN);
  }
  parameter->setUsage(vsr::scene::ParameterUsageHint::COLOR);
  CHECK(prepareEdit(*sampler, "outTransform", input).error
      == ValueError::OUT_OF_DOMAIN);
  CHECK(std::memcmp(parameter->value().data(), zero, sizeof(zero)) == 0);
}

TEST_CASE(
    "Matrix ties-to-even and subnormal conversion ignore ambient rounding mode",
    "[integration][object-matrix][matrix-numeric]")
{
  vsr::scene::Scene scene;
  auto sampler = scene.createObject<vsr::scene::Sampler>("image2D");
  const float zero[4] = {};
  sampler->setParameter("outTransform", ANARI_FLOAT32_MAT2, zero);
  RoundingGuard guard;
  const auto originalMode = guard.original;
  const auto input = JsonValue::array({JsonValue::numbers(
                                           {1. + std::ldexp(1., -24),
                                               1. + 3. * std::ldexp(1., -24)}),
      JsonValue::numbers({std::ldexp(1., -150), 3. * std::ldexp(1., -150)})});
  for (int mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    REQUIRE(std::fesetround(mode) == 0);
    const auto result = prepareEdit(*sampler, "outTransform", input);
    std::fesetround(originalMode);
    REQUIRE(result.ok());
    float packed[4];
    std::memcpy(packed, result.value.data(), sizeof(packed));
    CHECK(packed[0] == 1.f);
    CHECK(packed[1] == 0.f);
    CHECK(packed[2] == 1.0000002384185791015625f);
    CHECK(packed[3] == std::ldexp(1.f, -148));
  }
}

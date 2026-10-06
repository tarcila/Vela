// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/integration/values/EditableValues.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <cfenv>
#include <cmath>
#include <cstring>
#include <limits>

using namespace vsr::integration;
using vsr::core::math::float2;
using vsr::core::math::float3;
namespace tokens = vsr::scene::tokens;

namespace {

constexpr double FLT_MAX_D = double(std::numeric_limits<float>::max());

std::uint32_t bitsOf(float f)
{
  std::uint32_t b = 0;
  std::memcpy(&b, &f, sizeof(b));
  return b;
}

bool isPositiveZero(float f)
{
  return bitsOf(f) == 0u;
}

// Field-agnostic scalar checks run against every field's domain.
void checkCommonScalarRules(Domain d)
{
  CHECK(convertScalar(JsonValue::fromNumber(0.1), d).value == 0.1f);
  CHECK(convertScalar(JsonValue::fromNumber(1.0), d).value == 1.f);
  CHECK(convertScalar(JsonValue::fromNumber(0.0), d).ok());
  CHECK(convertScalar(JsonValue::fromNumber(d.min), d).ok());
  CHECK(convertScalar(JsonValue::fromNumber(d.max), d).ok());
  CHECK(convertScalar(JsonValue::fromNumber(std::nextafter(d.max, 1e300)), d)
            .error
      == ValueError::OUT_OF_DOMAIN);
  CHECK(convertScalar(JsonValue::fromNumber(-1e-300), d).error
      == ValueError::OUT_OF_DOMAIN);

  auto negZero = convertScalar(JsonValue::fromNumber(-0.0), d);
  REQUIRE(negZero.ok());
  CHECK(isPositiveZero(negZero.value));
  auto underflow = convertScalar(JsonValue::fromNumber(1e-46), d);
  REQUIRE(underflow.ok());
  CHECK(isPositiveZero(underflow.value));
  CHECK(convertScalar(JsonValue::fromNumber(-1e-46), d).error
      == ValueError::OUT_OF_DOMAIN);

  for (auto k : {JsonKind::BOOLEAN,
           JsonKind::STRING,
           JsonKind::NULL_VALUE,
           JsonKind::ARRAY,
           JsonKind::OBJECT,
           JsonKind::MALFORMED}) {
    CHECK(convertScalar(JsonValue::ofKind(k), d).error
        == ValueError::INCOMPATIBLE_TYPE);
  }
  const double inf = std::numeric_limits<double>::infinity();
  for (double nf : {inf, -inf, std::numeric_limits<double>::quiet_NaN()})
    CHECK(convertScalar(JsonValue::fromNumber(nf), d).error
        == ValueError::NON_FINITE);
}

struct Fixture
{
  vsr::scene::Scene scene;
  vsr::scene::MaterialRef matte;
  vsr::scene::MaterialRef pbr;
  vsr::scene::LightRef directional;
  vsr::scene::LightRef point;

  Fixture()
  {
    matte = scene.createObject<vsr::scene::Material>(tokens::material::matte);
    pbr = scene.createObject<vsr::scene::Material>(
        tokens::material::physicallyBased);
    directional =
        scene.createObject<vsr::scene::Light>(tokens::light::directional);
    point = scene.createObject<vsr::scene::Light>(tokens::light::point);
  }
};

} // namespace

SCENARIO(
    "Numeric conversion is validated in binary64 then rounded to "
    "binary32",
    "[integration]")
{
  GIVEN("every editable field's domain")
  {
    for (const auto domain :
        {Domain{0, 1}, Domain{0, FLT_MAX_D}, Domain{0, 360}})
      checkCommonScalarRules(domain);
  }

  GIVEN("the unit-interval domains")
  {
    const Domain unit = {0, 1};
    THEN("opacity 1.0000000000000002 rejects instead of rounding to 1")
    {
      CHECK(convertScalar(JsonValue::fromNumber(1.0000000000000002), unit).error
          == ValueError::OUT_OF_DOMAIN);
    }
    THEN("the double-rounding fixture rejects above 1 for color")
    {
      CHECK(convertScalar(JsonValue::fromNumber(1.00000005960464477539), unit)
                .error
          == ValueError::OUT_OF_DOMAIN);
    }
  }

  GIVEN("the irradiance domain")
  {
    const Domain irr = {0, FLT_MAX_D};
    THEN("1e39 rejects")
    {
      CHECK(convertScalar(JsonValue::fromNumber(1e39), irr).error
          == ValueError::OUT_OF_DOMAIN);
    }
    THEN("the largest binary32 accepts, and anything above it rejects")
    {
      CHECK(convertScalar(JsonValue::fromNumber(FLT_MAX_D), irr).value
          == std::numeric_limits<float>::max());
      // Would round to FLT_MAX, but validation precedes rounding.
      CHECK(convertScalar(
                JsonValue::fromNumber(std::nextafter(FLT_MAX_D, 1e300)), irr)
                .error
          == ValueError::OUT_OF_DOMAIN);
    }
    THEN("integer-looking values accept")
    {
      CHECK(convertScalar(JsonValue::fromNumber(5.0), irr).value == 5.f);
    }
    THEN("the double-rounding fixture stores exactly 1.0")
    {
      // 1 + 2^-24 + 2^-80: binary64 parse gives 1 + 2^-24, a binary32 tie.
      const double parsed =
          1.00000005960464477539062582718061255302767487140869206996285356581211090087890625;
      REQUIRE(parsed == 1.0 + std::ldexp(1.0, -24));
      auto r = convertScalar(JsonValue::fromNumber(parsed), irr);
      REQUIRE(r.ok());
      CHECK(bitsOf(r.value) == bitsOf(1.f));
    }
  }
}

SCENARIO("Binary32 rounding is ties-to-even regardless of ambient mode",
    "[integration]")
{
  const double tieDown = 1.0 + std::ldexp(1.0, -24); // even neighbor is 1
  const double tieUp = 1.0 + 3 * std::ldexp(1.0, -24); // even is 1 + 2^-22
  const double subTie = 1.5 * std::ldexp(1.0, -149); // even is 2^-148
  const double halfMin = std::ldexp(1.0, -150); // even is +0
  const double aboveHalfMin = std::nextafter(halfMin, 1.0);

  auto check = [&]() {
    CHECK(roundToBinary32(tieDown) == 1.f);
    CHECK(roundToBinary32(-tieDown) == -1.f);
    CHECK(double(roundToBinary32(tieUp)) == 1.0 + std::ldexp(1.0, -22));
    CHECK(double(roundToBinary32(subTie)) == std::ldexp(1.0, -148));
    CHECK(isPositiveZero(roundToBinary32(halfMin)));
    CHECK(double(roundToBinary32(aboveHalfMin)) == std::ldexp(1.0, -149));
    CHECK(double(roundToBinary32(std::ldexp(1.0, -149)))
        == std::ldexp(1.0, -149));
    CHECK(roundToBinary32(0.1) == 0.1f);
    CHECK(isPositiveZero(roundToBinary32(-0.0)));
    CHECK(isPositiveZero(roundToBinary32(std::ldexp(1.0, -1074))));
    // Rounding up across a binade and into the smallest normal.
    CHECK(double(roundToBinary32(std::nextafter(2.0, 0.0))) == 2.0);
    CHECK(double(roundToBinary32(std::nextafter(0x1p-126, 0.0))) == 0x1p-126);
  };

  check();
  for (int mode : {FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO}) {
    const int saved = std::fegetround();
    REQUIRE(std::fesetround(mode) == 0);
    check();
    std::fesetround(saved);
  }
}

SCENARIO(
    "Object edits validate the introspected type and metadata", "[integration]")
{
  Fixture fx;

  GIVEN("the listed fields")
  {
    THEN("each maps to an existing parameter with the expected type")
    {
      auto opacity =
          prepareEdit(*fx.matte, "opacity", JsonValue::fromNumber(1));
      REQUIRE(opacity.ok());
      CHECK(opacity.value.get<float>() == 1.f);

      auto mColor =
          prepareEdit(*fx.matte, "color", JsonValue::numbers({0.1, 0, 1}));
      REQUIRE(mColor.ok());
      CHECK(mColor.value.get<float3>() == float3(0.1f, 0.f, 1.f));

      auto lColor = prepareEdit(
          *fx.directional, "color", JsonValue::numbers({1, 0.5, 0}));
      REQUIRE(lColor.ok());

      auto irr = prepareEdit(
          *fx.directional, "irradiance", JsonValue::fromNumber(1e-46));
      REQUIRE(irr.ok());
      CHECK(isPositiveZero(irr.value.get<float>()));

      auto dir = prepareEdit(
          *fx.directional, "direction", JsonValue::numbers({360, 0.1}));
      REQUIRE(dir.ok());
      CHECK(dir.value.type() == ANARI_FLOAT32_VEC2);
    }
  }

  GIVEN("unsupported parameters, subtypes and types")
  {
    const auto before = fx.matte->lastParameterChange();
    const auto opacityBefore = fx.matte->parameterValueAs<float>("opacity");

    THEN("they reject without touching the object")
    {
      CHECK(prepareEdit(*fx.matte, "alphaMode", JsonValue::fromNumber(0)).error
          == ValueError::INCOMPATIBLE_TYPE);
      CHECK(prepareEdit(*fx.matte, "nope", JsonValue::fromNumber(0)).error
          == ValueError::UNSUPPORTED_PARAMETER);
      CHECK(prepareEdit(*fx.pbr, "opacity", JsonValue::fromNumber(0.5)).ok());
      CHECK(
          prepareEdit(*fx.point, "color", JsonValue::numbers({1, 1, 1})).ok());
      CHECK(prepareEdit(*fx.directional, "visible", JsonValue::fromNumber(1))
                .error
          == ValueError::INCOMPATIBLE_TYPE);
      CHECK(prepareEdit(*fx.directional, "opacity", JsonValue::fromNumber(1))
                .error
          == ValueError::UNSUPPORTED_PARAMETER);

      // Wrong JSON shape for the field.
      CHECK(prepareEdit(*fx.matte, "opacity", JsonValue::numbers({1})).error
          == ValueError::INCOMPATIBLE_TYPE);
      CHECK(prepareEdit(*fx.matte, "color", JsonValue::fromNumber(1)).error
          == ValueError::INCOMPATIBLE_TYPE);
      CHECK(prepareEdit(*fx.matte, "color", JsonValue::numbers({1, 1})).error
          == ValueError::INCOMPATIBLE_TYPE);
      CHECK(prepareEdit(
                *fx.directional, "direction", JsonValue::numbers({0, 0, 0}))
                .error
          == ValueError::INCOMPATIBLE_TYPE);
      auto mixed = JsonValue::numbers({0.5, 0.5});
      mixed.elements.push_back(JsonValue::ofKind(JsonKind::BOOLEAN));
      CHECK(prepareEdit(*fx.matte, "color", mixed).error
          == ValueError::INCOMPATIBLE_TYPE);
      CHECK(
          prepareEdit(*fx.matte, "color", JsonValue::numbers({0.5, 2, 0})).error
          == ValueError::OUT_OF_DOMAIN);

      CHECK(fx.matte->lastParameterChange() == before);
      CHECK(fx.matte->parameterValueAs<float>("opacity") == opacityBefore);
    }

    THEN("a parameter whose stored type differs rejects as incompatible")
    {
      fx.matte->setParameter("opacity", float2(0.f, 0.f));
      CHECK(prepareEdit(*fx.matte, "opacity", JsonValue::fromNumber(0.5)).error
          == ValueError::INCOMPATIBLE_TYPE);
      fx.matte->removeParameter("color");
      CHECK(prepareEdit(*fx.matte, "color", JsonValue::numbers({0, 0, 0})).error
          == ValueError::UNSUPPORTED_PARAMETER);
    }
  }
}

TEST_CASE("Generic numeric validation uses stored types and component bounds",
    "[integration][generic-edit]")
{
  Fixture fx;
  auto &object = *fx.pbr;
  struct IntegerCase
  {
    anari::DataType type;
    double lower;
    double upper;
  };
  const IntegerCase integers[] = {{ANARI_INT8, -128, 127},
      {ANARI_UINT8, 0, 255},
      {ANARI_INT16, -32768, 32767},
      {ANARI_UINT16, 0, 65535},
      {ANARI_INT32, -2147483648., 2147483647.},
      {ANARI_UINT32, 0, 4294967295.},
      {ANARI_INT64, -9007199254740991., 9007199254740991.},
      {ANARI_UINT64, 0, 9007199254740991.}};
  const std::uint64_t zero = 0;
  for (const auto &test : integers) {
    INFO(anari::toString(test.type));
    object.setParameter("custom", test.type, &zero);
    CHECK(
        prepareEdit(object, "custom", JsonValue::fromNumber(test.lower)).ok());
    const auto upper =
        prepareEdit(object, "custom", JsonValue::fromNumber(test.upper));
    REQUIRE(upper.ok());
    CHECK(upper.value.type() == test.type);
    CHECK(prepareEdit(object, "custom", JsonValue::fromNumber(test.lower - 1))
              .error
        == ValueError::OUT_OF_DOMAIN);
    CHECK(prepareEdit(object, "custom", JsonValue::fromNumber(test.upper + 1))
              .error
        == ValueError::OUT_OF_DOMAIN);
    CHECK(prepareEdit(object, "custom", JsonValue::fromNumber(0.5)).error
        == ValueError::INCOMPATIBLE_TYPE);
  }
  const double components[] = {0, 0, 0, 0};
  const double lower[] = {-1, 2, 3, 4};
  const double upper[] = {1, 3, 4, 5};
  object.setParameter("vector", ANARI_FLOAT64_VEC4, components);
  object.parameter("vector")
      ->setMin(vsr::core::Any(ANARI_FLOAT64_VEC4, lower))
      .setMax(vsr::core::Any(ANARI_FLOAT64_VEC4, upper));
  CHECK(prepareEdit(object, "vector", JsonValue::numbers({-1, 2, 4, 5})).ok());
  CHECK(prepareEdit(object, "vector", JsonValue::numbers({0, 1, 3, 4})).error
      == ValueError::OUT_OF_DOMAIN);
  CHECK(prepareEdit(object, "vector", JsonValue::numbers({0, 2, 3})).error
      == ValueError::INCOMPATIBLE_TYPE);
  object.setParameter("double", 0.0);
  const double exact = 1.0000000000000002;
  const auto prepared =
      prepareEdit(object, "double", JsonValue::fromNumber(exact));
  REQUIRE(prepared.ok());
  CHECK(prepared.value.get<double>() == exact);
  CHECK(prepareEdit(object,
            "double",
            JsonValue::fromNumber(std::numeric_limits<double>::infinity()))
            .error
      == ValueError::NON_FINITE);
  CHECK(prepareEdit(object, "roughness", JsonValue::fromNumber(exact)).error
      == ValueError::OUT_OF_DOMAIN);
  auto text = JsonValue::ofKind(JsonKind::STRING);
  text.text = "opaque";
  CHECK(prepareEdit(object, "alphaMode", text).ok());
  text.text = "invalid";
  CHECK(prepareEdit(object, "alphaMode", text).error
      == ValueError::OUT_OF_DOMAIN);
  object.setParameter("unbounded", "old");
  text.text = std::string("a\0b", 3);
  CHECK(prepareEdit(object, "unbounded", text).error
      == ValueError::OUT_OF_DOMAIN);
}

SCENARIO("Direction angles are closed [0,360] and never canonicalized",
    "[integration]")
{
  Fixture fx;

  THEN("aliasing angle pairs are stored verbatim")
  {
    auto at360 = prepareEdit(
        *fx.directional, "direction", JsonValue::numbers({360, 360}));
    REQUIRE(at360.ok());
    CHECK(at360.value.get<float2>() == float2(360.f, 360.f));
    auto at0 =
        prepareEdit(*fx.directional, "direction", JsonValue::numbers({0, 0}));
    REQUIRE(at0.ok());
    CHECK(at0.value.get<float2>() != at360.value.get<float2>());

    CHECK(prepareEdit(*fx.directional,
              "direction",
              JsonValue::numbers({360.00000000000006, 0}))
              .error
        == ValueError::OUT_OF_DOMAIN);
    CHECK(prepareEdit(*fx.directional, "direction", JsonValue::numbers({-1, 0}))
              .error
        == ValueError::OUT_OF_DOMAIN);
  }

  THEN("the direction formula matches VSR's axis convention")
  {
    auto near = [](float3 a, float3 b) {
      return std::fabs(a.x - b.x) < 1e-6f && std::fabs(a.y - b.y) < 1e-6f
          && std::fabs(a.z - b.z) < 1e-6f;
    };
    CHECK(near(directionFromAngles({0.f, 0.f}), {0.f, 0.f, 1.f}));
    CHECK(near(directionFromAngles({90.f, 0.f}), {1.f, 0.f, 0.f}));
    CHECK(near(directionFromAngles({180.f, 0.f}), {0.f, 0.f, -1.f}));
    CHECK(near(directionFromAngles({270.f, 0.f}), {-1.f, 0.f, 0.f}));
    CHECK(near(directionFromAngles({0.f, 90.f}), {0.f, 1.f, 0.f}));
    CHECK(near(directionFromAngles({0.f, 270.f}), {0.f, -1.f, 0.f}));
    CHECK(near(directionFromAngles({360.f, 0.f}), {0.f, 0.f, 1.f}));
    // Small positive steps rotate toward +X (azimuth) and +Y (elevation).
    CHECK(directionFromAngles({1.f, 0.f}).x > 0.f);
    CHECK(directionFromAngles({0.f, 1.f}).y > 0.f);
  }
}

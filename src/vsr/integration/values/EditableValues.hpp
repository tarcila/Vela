// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scene
#include "vsr/core/VSRMath.hpp"
#include "vsr/scene/Object.hpp"
#include "vsr/scene/objects/Light.hpp"
#include "vsr/scene/objects/Material.hpp"
// std
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vsr::integration {

using core::Token;

/*
 * The v1 agent edit surface and its deterministic numeric conversion.
 *
 * Input is an already-parsed JSON value: a kind tag plus, for numbers, the
 * parser's correctly rounded binary64. Validation happens entirely on the
 * binary64 value (domain and binary32 range) before a single deterministic
 * round-to-nearest-ties-to-even step to binary32. Nothing here mutates a
 * scene: prepareEdit() inspects a const Object and returns the value to store.
 *
 * Example:
 *   auto e = prepareEdit(*light, "irradiance", JsonValue::number(2.5));
 *   if (e.ok()) light->setParameter("irradiance", e.value.get<float>());
 */

enum class JsonKind
{
  NUMBER,
  BOOLEAN,
  STRING,
  NULL_VALUE,
  ARRAY,
  OBJECT,
  MALFORMED
};

struct JsonValue
{
  JsonKind kind{JsonKind::MALFORMED};
  double number{0.0}; // meaningful only for NUMBER
  std::vector<JsonValue> elements; // meaningful only for ARRAY
  bool boolean{false}; // meaningful only for BOOLEAN
  std::string text; // meaningful only for STRING
  // Meaningful only for OBJECT; sorted by key, keys unique.
  std::vector<std::pair<std::string, JsonValue>> members;

  static JsonValue fromNumber(double v);
  static JsonValue ofKind(JsonKind k);
  static JsonValue array(std::vector<JsonValue> e);
  static JsonValue numbers(std::initializer_list<double> v);
};

enum class EditableField
{
  MATTE_COLOR,
  MATTE_OPACITY,
  DIRECTIONAL_COLOR,
  DIRECTIONAL_IRRADIANCE,
  DIRECTIONAL_DIRECTION
};

enum class ValueError
{
  NONE,
  UNSUPPORTED_PARAMETER, // not on the edit surface for this object/subtype
  INCOMPATIBLE_TYPE, // wrong JSON kind/arity, or stored parameter type differs
  NON_FINITE,
  OUT_OF_DOMAIN, // includes exceeding the largest finite binary32
  UNSUPPORTED_NODE_KIND,
  NON_AFFINE
};

struct Domain
{
  double min{0.0};
  double max{0.0};
};

struct FieldSpec
{
  EditableField field;
  anari::DataType objectType;
  const Token *subtype;
  const char *parameter;
  anari::DataType storedType;
  size_t components;
  Domain domain;
};

struct ScalarResult
{
  ValueError error{ValueError::NONE};
  float value{0.f};

  bool ok() const;
};

struct PreparedEdit
{
  ValueError error{ValueError::NONE};
  std::optional<EditableField> field;
  core::Any value; // FLOAT32, FLOAT32_VEC2 or FLOAT32_VEC3 when ok()

  bool ok() const;
};

// Deterministic binary64 -> binary32, round-to-nearest ties-to-even, computed
// on the bit patterns so the ambient floating-point mode is irrelevant.
// Precondition: finite and |v| <= FLT_MAX. Zero results are +0.
float roundToBinary32(double v);

// Validate one binary64 against a domain, then round.
ScalarResult convertScalar(const JsonValue &v, Domain domain);

// The edit surface. Returns nullptr when (object, parameter) is not editable.
const std::vector<FieldSpec> &editSurface();
const FieldSpec *findEditableField(
    anari::DataType objectType, Token subtype, Token parameter);

// Validates target and value without mutating the object.
PreparedEdit prepareEdit(
    const scene::Object &obj, Token parameter, const JsonValue &value);

// VSR's azimuth/elevation (degrees) convention; no wrapping/canonicalization.
core::math::float3 directionFromAngles(core::math::float2 azimuthElevation);

// Inlined definitions ////////////////////////////////////////////////////////

inline JsonValue JsonValue::fromNumber(double v)
{
  JsonValue j;
  j.kind = JsonKind::NUMBER;
  j.number = v;
  return j;
}

inline JsonValue JsonValue::ofKind(JsonKind k)
{
  JsonValue j;
  j.kind = k;
  return j;
}

inline JsonValue JsonValue::array(std::vector<JsonValue> e)
{
  JsonValue j;
  j.kind = JsonKind::ARRAY;
  j.elements = std::move(e);
  return j;
}

inline JsonValue JsonValue::numbers(std::initializer_list<double> v)
{
  std::vector<JsonValue> e;
  for (double d : v)
    e.push_back(fromNumber(d));
  return array(std::move(e));
}

inline bool ScalarResult::ok() const
{
  return error == ValueError::NONE;
}

inline bool PreparedEdit::ok() const
{
  return error == ValueError::NONE;
}

inline float roundToBinary32(double v)
{
  std::uint64_t bits = 0;
  std::memcpy(&bits, &v, sizeof(bits));
  const std::uint32_t sign = std::uint32_t(bits >> 63);
  const int biased = int((bits >> 52) & 0x7ff);
  const std::uint64_t mantissa = bits & ((std::uint64_t(1) << 52) - 1);

  if (biased == 0 && mantissa == 0)
    return 0.f; // normalizes -0 to +0

  // value = sig * 2^exp2
  std::uint64_t sig = biased ? (mantissa | (std::uint64_t(1) << 52)) : mantissa;
  const int exp2 = (biased ? biased : 1) - 1075;
  int bitLength = 0;
  for (std::uint64_t s = sig; s; s >>= 1)
    bitLength++;
  const int lead = exp2 + bitLength - 1; // floor(log2(|value|))

  // Binary32 quantum: 2^(lead-23) for normals, 2^-149 for subnormals.
  const bool normal = lead >= -126;
  const int quantumExp = normal ? lead - 23 : -149;
  const int shift = quantumExp - exp2; // >= 29 for every binary64 input

  std::uint64_t q = 0;
  if (shift < 64) {
    q = sig >> shift;
    const std::uint64_t rem = sig & ((std::uint64_t(1) << shift) - 1);
    const std::uint64_t half = std::uint64_t(1) << (shift - 1);
    if (rem > half || (rem == half && (q & 1)))
      q++;
  }
  if (q == 0)
    return 0.f; // positive underflow (or sign-less zero) stores +0

  std::uint32_t out = 0;
  if (normal) {
    int e = lead + 127;
    if (q == (std::uint64_t(1) << 24)) {
      q >>= 1;
      e++;
    }
    out = (std::uint32_t(e) << 23) | (std::uint32_t(q) & 0x7fffffu);
  } else {
    // q <= 2^23; q == 2^23 is exactly the smallest normal's encoding.
    out = std::uint32_t(q);
  }
  out |= sign << 31;

  float f = 0.f;
  std::memcpy(&f, &out, sizeof(f));
  return f;
}

inline ScalarResult convertScalar(const JsonValue &v, Domain domain)
{
  ScalarResult r;
  if (v.kind != JsonKind::NUMBER) {
    r.error = ValueError::INCOMPATIBLE_TYPE;
    return r;
  }
  const double d = v.number;
  if (!std::isfinite(d)) {
    r.error = ValueError::NON_FINITE;
    return r;
  }
  const double maxFinite = double(std::numeric_limits<float>::max());
  if (d < domain.min || d > domain.max || std::fabs(d) > maxFinite) {
    r.error = ValueError::OUT_OF_DOMAIN;
    return r;
  }
  r.value = roundToBinary32(d);
  if (!std::isfinite(r.value) || double(r.value) < domain.min
      || double(r.value) > domain.max) {
    r.error = ValueError::OUT_OF_DOMAIN; // unreachable for closed domains
  }
  return r;
}

inline const std::vector<FieldSpec> &editSurface()
{
  constexpr double MAX_F = double(std::numeric_limits<float>::max());
  static const std::vector<FieldSpec> surface = {
      {EditableField::MATTE_COLOR,
          ANARI_MATERIAL,
          &scene::tokens::material::matte,
          "color",
          ANARI_FLOAT32_VEC3,
          3,
          {0.0, 1.0}},
      {EditableField::MATTE_OPACITY,
          ANARI_MATERIAL,
          &scene::tokens::material::matte,
          "opacity",
          ANARI_FLOAT32,
          1,
          {0.0, 1.0}},
      {EditableField::DIRECTIONAL_COLOR,
          ANARI_LIGHT,
          &scene::tokens::light::directional,
          "color",
          ANARI_FLOAT32_VEC3,
          3,
          {0.0, 1.0}},
      {EditableField::DIRECTIONAL_IRRADIANCE,
          ANARI_LIGHT,
          &scene::tokens::light::directional,
          "irradiance",
          ANARI_FLOAT32,
          1,
          {0.0, MAX_F}},
      {EditableField::DIRECTIONAL_DIRECTION,
          ANARI_LIGHT,
          &scene::tokens::light::directional,
          "direction",
          ANARI_FLOAT32_VEC2,
          2,
          {0.0, 360.0}},
  };
  return surface;
}

inline const FieldSpec *findEditableField(
    anari::DataType objectType, Token subtype, Token parameter)
{
  for (const auto &f : editSurface()) {
    if (f.objectType == objectType && *f.subtype == subtype
        && parameter == Token(f.parameter))
      return &f;
  }
  return nullptr;
}

inline PreparedEdit prepareEdit(
    const scene::Object &obj, Token parameter, const JsonValue &value)
{
  PreparedEdit r;
  const FieldSpec *spec =
      findEditableField(obj.type(), obj.subtype(), parameter);
  const scene::Parameter *p = spec ? obj.parameter(parameter) : nullptr;
  if (!spec || !p) {
    r.error = ValueError::UNSUPPORTED_PARAMETER;
    return r;
  }
  r.field = spec->field;
  if (p->value().type() != spec->storedType) {
    r.error = ValueError::INCOMPATIBLE_TYPE;
    return r;
  }

  float out[3] = {};
  if (spec->components == 1) {
    auto s = convertScalar(value, spec->domain);
    if (!s.ok()) {
      r.error = s.error;
      return r;
    }
    out[0] = s.value;
  } else {
    if (value.kind != JsonKind::ARRAY
        || value.elements.size() != spec->components) {
      r.error = ValueError::INCOMPATIBLE_TYPE;
      return r;
    }
    for (size_t i = 0; i < spec->components; i++) {
      auto s = convertScalar(value.elements[i], spec->domain);
      if (!s.ok()) {
        r.error = s.error;
        return r;
      }
      out[i] = s.value;
    }
  }

  switch (spec->components) {
  case 1:
    r.value = core::Any(out[0]);
    break;
  case 2:
    r.value = core::Any(core::math::float2(out[0], out[1]));
    break;
  default:
    r.value = core::Any(core::math::float3(out[0], out[1], out[2]));
    break;
  }
  return r;
}

inline core::math::float3 directionFromAngles(
    core::math::float2 azimuthElevation)
{
  return core::math::azelToDir(azimuthElevation);
}

} // namespace vsr::integration

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scene
#include "vsr/core/VSRMath.hpp"
#include "vsr/scene/Object.hpp"
// std
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace vsr::integration {

using core::Token;

/*
 * Introspection-driven agent edits and deterministic numeric conversion.
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

enum class ValueError
{
  NONE,
  UNSUPPORTED_PARAMETER, // missing parameter or unsupported stored type
  INCOMPATIBLE_TYPE, // wrong JSON kind/arity, or stored parameter type differs
  NON_FINITE,
  OUT_OF_DOMAIN, // includes exceeding the largest finite binary32
  UNSUPPORTED_NODE_KIND,
  NON_AFFINE,
  UNFAITHFUL_SRT,
  SINGULAR_PARENT
};

struct Domain
{
  double min{0.0};
  double max{0.0};
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
  core::Any value; // preserves the introspected stored type when ok()

  bool ok() const;
};

// Deterministic binary64 -> binary32, round-to-nearest ties-to-even, computed
// on the bit patterns so the ambient floating-point mode is irrelevant.
// Precondition: finite and |v| <= FLT_MAX. Zero results are +0.
float roundToBinary32(double v);

// Validate one binary64 against a domain, then round.
ScalarResult convertScalar(const JsonValue &v, Domain domain);

// References, double matrices and opaque storage are deliberately read-only.
bool isEditableParameter(const scene::Parameter &parameter);

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

namespace value_detail {

template <typename T>
inline T component(const core::Any &value, std::size_t index)
{
  T result;
  std::memcpy(&result,
      static_cast<const unsigned char *>(value.data()) + index * sizeof(T),
      sizeof(T));
  return result;
}

template <typename T>
inline PreparedEdit prepareNumeric(const scene::Parameter &p,
    const JsonValue &value,
    std::size_t rows = 0,
    std::size_t columns = 0)
{
  PreparedEdit result;
  const auto count = anari::componentsOf(p.value().type());
  if (rows) {
    if (value.kind != JsonKind::ARRAY || value.elements.size() != rows) {
      result.error = ValueError::INCOMPATIBLE_TYPE;
      return result;
    }
    for (const auto &row : value.elements) {
      if (row.kind != JsonKind::ARRAY || row.elements.size() != columns) {
        result.error = ValueError::INCOMPATIBLE_TYPE;
        return result;
      }
    }
  } else if ((count == 1 && value.kind != JsonKind::NUMBER)
      || (count != 1
          && (value.kind != JsonKind::ARRAY
              || value.elements.size() != count))) {
    result.error = ValueError::INCOMPATIBLE_TYPE;
    return result;
  }
  T out[16] = {}; // largest supported value is FLOAT32_MAT4
  for (std::size_t i = 0; i < count; ++i) {
    // ANARI matrix names are rows x columns; metadata uses packed indexing.
    const auto &input = rows ? value.elements[i % rows].elements[i / rows]
        : count == 1         ? value
                             : value.elements[i];
    if (input.kind != JsonKind::NUMBER) {
      result.error = ValueError::INCOMPATIBLE_TYPE;
      return result;
    }
    const double number = input.number;
    if (!std::isfinite(number)) {
      result.error = ValueError::NON_FINITE;
      return result;
    }
    if constexpr (std::is_integral_v<T>) {
      if (std::trunc(number) != number) {
        result.error = ValueError::INCOMPATIBLE_TYPE;
        return result;
      }
      // Above this interval JSON binary64 cannot faithfully carry integers.
      if (std::fabs(number) > 9007199254740991.0
          || number < double(std::numeric_limits<T>::lowest())
          || number > double(std::numeric_limits<T>::max())) {
        result.error = ValueError::OUT_OF_DOMAIN;
        return result;
      }
    }
    const bool color = (p.usage() & scene::ParameterUsageHint::COLOR) != 0;
    const double lower = p.hasMin() ? double(component<T>(p.min(), i))
                                    : double(std::numeric_limits<T>::lowest());
    const double upper = p.hasMax() ? double(component<T>(p.max(), i))
                                    : double(std::numeric_limits<T>::max());
    if (!std::isfinite(lower) || !std::isfinite(upper) || lower > upper
        || number < lower || number > upper
        || (color && (number < 0 || number > 1))) {
      result.error = ValueError::OUT_OF_DOMAIN;
      return result;
    }
    if constexpr (std::is_same_v<T, float>) {
      const auto converted = convertScalar(input, {lower, upper});
      if (!converted.ok()) {
        result.error = converted.error;
        return result;
      }
      out[i] = converted.value;
    } else {
      out[i] = static_cast<T>(number);
    }
    // Integer bounds are compared exactly after the safe, range-checked cast.
    if ((p.hasMin() && out[i] < component<T>(p.min(), i))
        || (p.hasMax() && out[i] > component<T>(p.max(), i))) {
      result.error = ValueError::OUT_OF_DOMAIN;
      return result;
    }
  }
  result.value = core::Any(p.value().type(), out);
  return result;
}

} // namespace value_detail

inline bool isEditableParameter(const scene::Parameter &p)
{
  const auto type = p.value().type();
  if (type == ANARI_BOOL || type == ANARI_STRING || type == ANARI_FLOAT32_MAT2
      || type == ANARI_FLOAT32_MAT3 || type == ANARI_FLOAT32_MAT4
      || type == ANARI_FLOAT32_MAT2x3 || type == ANARI_FLOAT32_MAT3x4)
    return true;
#define VSR_EDIT_NUMERIC(BASE)                                                 \
  if (type == BASE || type == BASE##_VEC2 || type == BASE##_VEC3               \
      || type == BASE##_VEC4)                                                  \
  return true
  VSR_EDIT_NUMERIC(ANARI_INT8);
  VSR_EDIT_NUMERIC(ANARI_UINT8);
  VSR_EDIT_NUMERIC(ANARI_INT16);
  VSR_EDIT_NUMERIC(ANARI_UINT16);
  VSR_EDIT_NUMERIC(ANARI_INT32);
  VSR_EDIT_NUMERIC(ANARI_UINT32);
  VSR_EDIT_NUMERIC(ANARI_INT64);
  VSR_EDIT_NUMERIC(ANARI_UINT64);
  VSR_EDIT_NUMERIC(ANARI_FLOAT32);
  VSR_EDIT_NUMERIC(ANARI_FLOAT64);
#undef VSR_EDIT_NUMERIC
  return false;
}

inline PreparedEdit prepareEdit(
    const scene::Object &obj, Token parameter, const JsonValue &value)
{
  PreparedEdit result;
  const auto *p = obj.parameter(parameter);
  if (!p || !isEditableParameter(*p)) {
    result.error = ValueError::UNSUPPORTED_PARAMETER;
    return result;
  }
  const auto type = p->value().type();
  if (type == ANARI_BOOL) {
    if (value.kind != JsonKind::BOOLEAN)
      result.error = ValueError::INCOMPATIBLE_TYPE;
    else
      result.value = core::Any(value.boolean);
  } else if (type == ANARI_STRING) {
    if (value.kind != JsonKind::STRING)
      result.error = ValueError::INCOMPATIBLE_TYPE;
    else if (value.text.find('\0') != std::string::npos
        || (!p->stringValues().empty()
            && std::find(p->stringValues().begin(),
                   p->stringValues().end(),
                   value.text)
                == p->stringValues().end()))
      result.error = ValueError::OUT_OF_DOMAIN;
    else
      result.value = core::Any(value.text);
  }
#define VSR_PREPARE_NUMERIC(BASE, CPP)                                         \
  else if (type == BASE || type == BASE##_VEC2 || type == BASE##_VEC3          \
      || type == BASE##_VEC4) return value_detail::prepareNumeric<CPP>(*p,     \
      value)
  VSR_PREPARE_NUMERIC(ANARI_INT8, std::int8_t);
  VSR_PREPARE_NUMERIC(ANARI_UINT8, std::uint8_t);
  VSR_PREPARE_NUMERIC(ANARI_INT16, std::int16_t);
  VSR_PREPARE_NUMERIC(ANARI_UINT16, std::uint16_t);
  VSR_PREPARE_NUMERIC(ANARI_INT32, std::int32_t);
  VSR_PREPARE_NUMERIC(ANARI_UINT32, std::uint32_t);
  VSR_PREPARE_NUMERIC(ANARI_INT64, std::int64_t);
  VSR_PREPARE_NUMERIC(ANARI_UINT64, std::uint64_t);
  VSR_PREPARE_NUMERIC(ANARI_FLOAT32, float);
  VSR_PREPARE_NUMERIC(ANARI_FLOAT64, double);
#undef VSR_PREPARE_NUMERIC
  else if (type
      == ANARI_FLOAT32_MAT2) return value_detail::prepareNumeric<float>(*p,
      value,
      2,
      2);
  else if (type
      == ANARI_FLOAT32_MAT3) return value_detail::prepareNumeric<float>(*p,
      value,
      3,
      3);
  else if (type
      == ANARI_FLOAT32_MAT4) return value_detail::prepareNumeric<float>(*p,
      value,
      4,
      4);
  else if (type
      == ANARI_FLOAT32_MAT2x3) return value_detail::prepareNumeric<float>(*p,
      value,
      2,
      3);
  else if (type
      == ANARI_FLOAT32_MAT3x4) return value_detail::prepareNumeric<float>(*p,
      value,
      3,
      4);
  return result;
}

inline core::math::float3 directionFromAngles(
    core::math::float2 azimuthElevation)
{
  return core::math::azelToDir(azimuthElevation);
}

} // namespace vsr::integration

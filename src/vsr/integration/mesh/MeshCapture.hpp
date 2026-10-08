// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/identity/Identity.hpp"
#include "vsr/integration/mcp/Json.hpp"
// std
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory_resource> // IWYU pragma: keep (pmr vector allocator)
#include <string>
#include <string_view>
#include <vector>

namespace vsr::integration::mesh {

/* Restricted revision-1 profile and envelope: docs/mesh-exchange.md.
 * Write tracking is broader than this profile; it never authorizes export. */
constexpr std::size_t FIELD_CAP = 64;
constexpr std::size_t HEADER_CAP = 65536;
constexpr std::size_t PAYLOAD_CAP = 64u << 20;

struct Representation
{
  const char *base{nullptr};
  unsigned components{0};
  unsigned scalarBytes{0};
  bool floating{false};
};

inline Representation representation(anari::DataType type)
{
  struct Scalar
  {
    anari::DataType type;
    const char *name;
    unsigned bytes;
    bool floating;
  };
  // ANARI scalar/vector families have four consecutive enum values.
  const Scalar scalars[] = {
#define VSR_MESH_SCALAR(T, B, F)                                               \
  [] {                                                                         \
    static_assert(ANARI_##T##_VEC2 == ANARI_##T + 1                            \
            && ANARI_##T##_VEC3 == ANARI_##T + 2                               \
            && ANARI_##T##_VEC4 == ANARI_##T + 3 && ANARI_##T >= ANARI_INT8    \
            && ANARI_##T##_VEC4 <= ANARI_FLOAT64_VEC4,                         \
        "Mesh scalar/vector families must fit host write tracking");           \
    return Scalar{ANARI_##T, "ANARI_" #T, B, F};                               \
  }()
      VSR_MESH_SCALAR(INT8, 1, false),
      VSR_MESH_SCALAR(UINT8, 1, false),
      VSR_MESH_SCALAR(INT16, 2, false),
      VSR_MESH_SCALAR(UINT16, 2, false),
      VSR_MESH_SCALAR(INT32, 4, false),
      VSR_MESH_SCALAR(UINT32, 4, false),
      VSR_MESH_SCALAR(INT64, 8, false),
      VSR_MESH_SCALAR(UINT64, 8, false),
      VSR_MESH_SCALAR(FIXED8, 1, false),
      VSR_MESH_SCALAR(UFIXED8, 1, false),
      VSR_MESH_SCALAR(FIXED16, 2, false),
      VSR_MESH_SCALAR(UFIXED16, 2, false),
      VSR_MESH_SCALAR(FIXED32, 4, false),
      VSR_MESH_SCALAR(UFIXED32, 4, false),
      VSR_MESH_SCALAR(FLOAT32, 4, true),
      VSR_MESH_SCALAR(FLOAT64, 8, true),
#undef VSR_MESH_SCALAR
  };
  for (const auto &scalar : scalars)
    if (type >= scalar.type && type <= scalar.type + 3)
      return {scalar.name,
          unsigned(type - scalar.type + 1),
          scalar.bytes,
          scalar.floating};
  return {};
}

inline const char *association(std::string_view name)
{
  if (name == "primitive.index")
    return "index";
  const auto dot = name.find('.');
  if (dot == std::string_view::npos)
    return nullptr;
  const auto prefix = name.substr(0, dot), suffix = name.substr(dot + 1);
  const char *fieldAssociation = prefix == "vertex" ? "vertex"
      : prefix == "primitive"                       ? "primitive"
      : prefix == "faceVarying"                     ? "corner"
                                                    : nullptr;
  if (!fieldAssociation)
    return nullptr;
  if (suffix == "position")
    return prefix == "vertex" ? fieldAssociation : nullptr;
  if (suffix == "normal" || suffix == "tangent" || suffix == "color"
      || suffix == "attribute0" || suffix == "attribute1"
      || suffix == "attribute2" || suffix == "attribute3")
    return fieldAssociation;
  return nullptr;
}

/* Ephemeral safe-point plan. Never retain this pointer-bearing value in a
 * worker/job; only the encoded owned bytes cross that boundary. */
struct CapturePlan
{
  std::array<const scene::Array *, FIELD_CAP> arrays{};
  std::array<const scene::Parameter *, FIELD_CAP> fields{};
  std::array<std::size_t, FIELD_CAP> aliases{};
  std::size_t arrayCount{0};
  std::size_t fieldCount{0};
  std::size_t payloadBytes{0};
};

struct PositionData
{
  const void *data{nullptr};
  anari::DataType elementType{ANARI_UNKNOWN};
  std::size_t count{0};
  std::size_t byteLength{0};
};

inline const char *finiteValues(
    const PositionData &array, const std::function<bool()> &abandoned = {})
{
  const auto repr = representation(array.elementType);
  if (repr.floating)
    for (std::size_t c = 0; c < array.count * repr.components; ++c) {
      if (c % 4096 == 0 && abandoned && abandoned())
        return "cancelled";
      const auto *bytes = static_cast<const std::uint8_t *>(array.data);
      double value;
      if (repr.scalarBytes == 4) {
        float sample;
        std::memcpy(&sample, bytes + c * 4, 4);
        value = sample;
      } else
        std::memcpy(&value, bytes + c * 8, 8);
      if (!std::isfinite(value))
        return "nonfinite_mesh_value";
    }
  return nullptr;
}

// A positions-only, count-preserving proposal has the same triangle/index
// and attribute cardinalities as the current bindings. Validate those in full,
// substituting only the declared position samples in the finite-value check.
inline const char *plan(const scene::Geometry &geometry,
    CapturePlan &result,
    const PositionData *replacement = nullptr)
{
  const std::uint16_t endian = 1;
  if (*reinterpret_cast<const unsigned char *>(&endian) != 1)
    return "unsupported_byte_order";
  if (geometry.subtype() != scene::tokens::geometry::triangle)
    return "unsupported_geometry";
  if (geometry.numParameters() > FIELD_CAP)
    return "resource_limit";
  for (std::size_t i = 0; i < geometry.numParameters(); ++i) {
    const auto &field = geometry.parameterAt(i);
    const auto type = field.value().type();
    const auto *associated = association(field.name().str());
    if (!anari::isObject(type)) {
      if (associated)
        return "unsupported_field_binding";
      continue;
    }
    if (!associated || !anari::isArray(type))
      return "unsupported_dependency";
    const auto *array =
        geometry.parameterValueAsObject<scene::Array>(field.name());
    if (!array || !array->isHost() || !array->data() || !array->size())
      return "unsupported_storage";
    if (array->isMapped())
      return "array_mapped";
    if (array->type() != ANARI_ARRAY1D)
      return "unsupported_layout";
    const auto repr = representation(array->elementType());
    if (!repr.base
        || array->elementSize() != repr.scalarBytes * repr.components)
      return "unsupported_element_type";
    if (array->numParameters() > FIELD_CAP)
      return "resource_limit";
    for (std::size_t p = 0; p < array->numParameters(); ++p)
      if (anari::isObject(array->parameterAt(p).value().type()))
        return "unsupported_dependency";
    const std::string name = field.name().str();
    const auto suffix = name.substr(name.find('.') + 1);
    if ((suffix == "position" && (!repr.floating || repr.components != 3))
        || (suffix == "index" && array->elementType() != ANARI_UINT32_VEC3
            && array->elementType() != ANARI_UINT64_VEC3)
        || (suffix == "normal" && repr.components != 3)
        || (suffix == "tangent" && repr.components != 4)
        || (suffix == "color" && repr.components != 3 && repr.components != 4))
      return "unsupported_element_type";
    std::size_t alias = 0;
    while (alias < result.arrayCount && result.arrays[alias] != array)
      ++alias;
    if (alias == result.arrayCount) {
      if (array->size()
          > (PAYLOAD_CAP - result.payloadBytes) / array->elementSize())
        return "resource_limit";
      result.payloadBytes += array->size() * array->elementSize();
      result.arrays[result.arrayCount++] = array;
    }
    result.fields[result.fieldCount] = &field;
    result.aliases[result.fieldCount++] = alias;
  }
  const auto *positionField = geometry.parameter("vertex.position");
  const auto *positions =
      geometry.parameterValueAsObject<scene::Array>("vertex.position");
  if (!positions || !positionField || !positionField->isEnabled())
    return "missing_positions";
  const auto *indexField = geometry.parameter("primitive.index");
  const auto *indices =
      geometry.parameterValueAsObject<scene::Array>("primitive.index");
  const bool indexed = indices && indexField->isEnabled();
  if (!indexed && positions->size() % 3)
    return "invalid_triangle_count";
  const auto triangles = indexed ? indices->size() : positions->size() / 3;
  if (indices) {
    for (std::size_t i = 0; i < indices->size() * 3; ++i) {
      const auto value = indices->elementType() == ANARI_UINT32_VEC3
          ? static_cast<const std::uint32_t *>(indices->data())[i]
          : static_cast<const std::uint64_t *>(indices->data())[i];
      if (value >= positions->size())
        return "index_out_of_range";
    }
  }
  for (std::size_t i = 0; i < result.fieldCount; ++i) {
    const auto *field = result.fields[i];
    const auto *array = result.arrays[result.aliases[i]];
    const std::string_view associated = association(field->name().str());
    const auto count = associated == "vertex" ? positions->size()
        : associated == "corner"              ? triangles * 3
                                              : triangles;
    if (array->size() != count)
      return "attribute_count_mismatch";
  }
  bool retainedPositions = false;
  for (std::size_t i = 0; i < result.fieldCount; ++i)
    if (result.arrays[result.aliases[i]] == positions
        && result.fields[i]->name() != "vertex.position")
      retainedPositions = true;
  if (replacement) {
    const auto repr = representation(replacement->elementType);
    if (!replacement->data || !repr.floating || repr.components != 3)
      return "unsupported_element_type";
    if (replacement->count != positions->size())
      return "count_change_unsupported";
    const auto elementBytes = repr.components * repr.scalarBytes;
    if (replacement->count > PAYLOAD_CAP / elementBytes
        || replacement->byteLength != replacement->count * elementBytes)
      return "invalid_payload_length";
    const auto retainedBytes = result.payloadBytes
        - (retainedPositions ? 0
                             : positions->size() * positions->elementSize());
    if (retainedBytes > PAYLOAD_CAP - replacement->byteLength)
      return "resource_limit";
    if (const auto *reason = finiteValues(*replacement))
      return reason;
  }
  // Validate shared storage once. A split position alias leaves the old data
  // in every preserved field and therefore still needs validation there.
  for (std::size_t i = 0; i < result.arrayCount; ++i) {
    const auto *array = result.arrays[i];
    if (replacement && array == positions && !retainedPositions)
      continue;
    if (const auto *reason = finiteValues({array->data(),
            array->elementType(),
            array->size(),
            array->size() * array->elementSize()}))
      return reason;
  }
  return nullptr;
}

inline const char *capture(const scene::Geometry &geometry,
    identity::SceneIdentity &ids,
    const CapturePlan &plan,
    std::pmr::vector<std::uint8_t> &bytes,
    std::string &receipt)
{
  const auto handle = ids.handleOf(&geometry);
  const auto version = ids.parameterVersion(handle);
  if (!version)
    return "invalid_handle";
  const auto targetVersion = "mesh:" + std::to_string(handle.launch.activation)
      + "." + std::to_string(handle.launch.nonce) + "."
      + std::to_string(handle.domain) + "." + std::to_string(handle.serial)
      + "." + std::to_string(version->version);
  std::string fields = "{", arrays = "{", dependencies = "[";
  for (std::size_t i = 0; i < plan.fieldCount; ++i) {
    const auto *field = plan.fields[i];
    if (i)
      fields += ",";
    fields += mcp::json::quote(field->name().str())
        + ":{\"action\":\"preserve\",\"payload\":\"a"
        + std::to_string(plan.aliases[i]) + "\",\"enabled\":"
        + (field->isEnabled() ? "true" : "false") + ",\"association\":"
        + mcp::json::quote(association(field->name().str())) + "}";
  }
  for (std::size_t i = 0; i < plan.arrayCount; ++i) {
    const auto *array = plan.arrays[i];
    const auto arrayHandle = ids.handleOf(array);
    const auto dataVersion = ids.arrayDataVersion(arrayHandle);
    if (!dataVersion)
      return "host_write_contract_unavailable";
    const auto repr = representation(array->elementType());
    const auto elementType = std::string(repr.base)
        + (repr.components == 1 ? ""
                                : "_VEC" + std::to_string(repr.components));
    if (i) {
      arrays += ",";
      dependencies += ",";
    }
    arrays += "\"a" + std::to_string(i)
        + "\":{\"element_type\":" + mcp::json::quote(elementType)
        + ",\"count\":" + std::to_string(array->size()) + ",\"byte_length\":"
        + std::to_string(array->size() * array->elementSize()) + "}";
    char text[33];
    std::snprintf(text,
        sizeof(text),
        "%016llx%016llx",
        (unsigned long long)arrayHandle.domain,
        (unsigned long long)arrayHandle.serial);
    dependencies += "{\"payload\":\"a" + std::to_string(i)
        + "\",\"handle\":" + mcp::json::quote(text)
        + ",\"data_version\":" + mcp::json::quote(*dataVersion) + "}";
  }
  receipt = "\"target_version\":" + mcp::json::quote(targetVersion)
      + ",\"dependencies\":" + dependencies + "]";
  const auto header = "{\"revision\":1,\"byte_order\":\"little\",\"fields\":"
      + fields + "},\"arrays\":" + arrays + "},\"source\":{" + receipt + "}}";
  if (header.size() > HEADER_CAP)
    return "resource_limit";
  bytes.reserve(12 + header.size() + plan.payloadBytes);
  bytes.insert(bytes.end(), {'V', 'M', 'E', 'S', 'H', '0', '1', '\n'});
  for (int i = 0; i < 4; ++i)
    bytes.push_back(std::uint8_t(header.size() >> (8 * i)));
  bytes.insert(bytes.end(), header.begin(), header.end());
  for (std::size_t i = 0; i < plan.arrayCount; ++i) {
    const auto *array = plan.arrays[i];
    const auto *data = static_cast<const std::uint8_t *>(array->data());
    bytes.insert(
        bytes.end(), data, data + array->size() * array->elementSize());
  }
  return nullptr;
}

} // namespace vsr::integration::mesh

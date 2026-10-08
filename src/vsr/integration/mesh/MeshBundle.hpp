// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/mesh/MeshCapture.hpp"
// std
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

namespace vsr::integration::mesh {

enum class Action
{
  PRESERVE,
  REPLACE,
  REMOVE
};

struct Payload
{
  std::string id;
  anari::DataType elementType{ANARI_UNKNOWN};
  std::size_t count{0};
  std::size_t offset{0};
  std::size_t byteLength{0};
};

struct Field
{
  std::string name;
  Action action{Action::PRESERVE};
  bool enabled{true};
  std::size_t payload{FIELD_CAP};
};

/* Owned, untrusted working data, never Scene references. The caller reserves
 * physical/transient ownership before reading bytes or invoking the decoder.
 * No descriptor, source token or bundle-local payload ID confers authority. */
struct Bundle
{
  explicit Bundle(
      std::pmr::memory_resource *allocator = std::pmr::new_delete_resource());
  VSR_NOT_COPYABLE(Bundle)
  VSR_DEFAULT_MOVEABLE(Bundle)
  const std::uint8_t *data(const Payload &payload) const;

  std::pmr::vector<std::uint8_t> bytes;
  std::vector<Payload> arrays;
  std::vector<Field> fields;
};

const char *decodeBundle(std::pmr::vector<std::uint8_t> &&bytes,
    Bundle &result,
    const std::function<bool()> &abandoned = {});

// Inlined definitions ////////////////////////////////////////////////////////

inline Bundle::Bundle(std::pmr::memory_resource *allocator) : bytes(allocator)
{}

inline const std::uint8_t *Bundle::data(const Payload &payload) const
{
  return bytes.data() + payload.offset;
}

namespace detail {
using mcp::json::Value;

constexpr std::size_t METADATA_ITEM_CAP = 4096;

// Bound the DOM's allocation count as well as serialized header bytes. This
// over-counts containers/member separators, so it is conservative. Strings do
// not contribute punctuation; malformed strings still fail the strict parser.
inline bool boundedMetadata(std::string_view input)
{
  std::size_t items = 0;
  bool quoted = false;
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (quoted) {
      if (input[i] == '\\')
        ++i;
      else if (input[i] == '"')
        quoted = false;
    } else if (input[i] == '"')
      quoted = true;
    else if (input[i] == '{' || input[i] == '[' || input[i] == ':'
        || input[i] == ',') {
      if (++items > METADATA_ITEM_CAP)
        return false;
    }
  }
  return true;
}

inline bool members(
    const Value &value, std::initializer_list<std::string_view> allowed)
{
  if (!value.isObject())
    return false;
  for (const auto &member : value.o)
    if (std::find(allowed.begin(), allowed.end(), member.first)
        == allowed.end())
      return false;
  return true;
}

inline bool text(const Value *value, std::string_view expected)
{
  return value && value->isString() && value->s == expected;
}

inline bool size(const Value *value, std::size_t cap, std::size_t &result)
{
  if (!value || !value->isNumber() || value->n < 0 || value->n > double(cap)
      || std::floor(value->n) != value->n)
    return false;
  result = std::size_t(value->n);
  return true;
}

inline bool utf8(std::string_view input)
{
  for (std::size_t i = 0; i < input.size();) {
    const auto lead = std::uint8_t(input[i++]);
    if (lead < 128)
      continue;
    const unsigned following = lead >= 0xc2 && lead <= 0xdf ? 1
        : lead >= 0xe0 && lead <= 0xef                      ? 2
        : lead >= 0xf0 && lead <= 0xf4                      ? 3
                                                            : 0;
    if (!following || following > input.size() - i)
      return false;
    std::uint32_t cp = lead & ((1u << (6 - following)) - 1);
    for (unsigned j = 0; j < following; ++j) {
      const auto next = std::uint8_t(input[i++]);
      if ((next & 0xc0) != 0x80)
        return false;
      cp = (cp << 6) | (next & 0x3f);
    }
    if (cp < (following == 1     ? 0x80u
                : following == 2 ? 0x800u
                                 : 0x10000u)
        || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
      return false;
  }
  return true;
}

/* JSON objects are semantically unordered, but the carrier's raw payloads
 * follow lexical declaration order. This pass only walks an already strictly
 * parsed header; it neither interprets values nor accepts invalid grammar. */
inline std::vector<std::string> arrayOrder(std::string_view header)
{
  std::vector<std::string> order;
  unsigned depth = 0;
  bool arrays = false;
  for (std::size_t i = 0; i < header.size(); ++i) {
    const auto c = header[i];
    if (c == '"') {
      const auto start = i++;
      while (i < header.size() && header[i] != '"') {
        if (header[i] == '\\')
          ++i;
        ++i;
      }
      auto next = i + 1;
      while (next < header.size()
          && (header[next] == ' ' || header[next] == '\n'
              || header[next] == '\r' || header[next] == '\t'))
        ++next;
      if (next < header.size() && header[next] == ':'
          && (depth == 1 || (arrays && depth == 2))) {
        Value key;
        if (!mcp::json::parse(header.substr(start, i + 1 - start), key))
          return {};
        if (depth == 1)
          arrays = key.s == "arrays";
        else
          order.push_back(std::move(key.s));
      }
    } else if (c == '{' || c == '[')
      ++depth;
    else if (c == '}' || c == ']')
      --depth;
  }
  return order;
}

inline anari::DataType elementType(std::string_view name)
{
  for (int t = ANARI_INT8; t <= ANARI_FLOAT64_VEC4; ++t) {
    const auto type = anari::DataType(t);
    const auto repr = representation(type);
    if (!repr.base)
      continue;
    const auto spelling = std::string(repr.base)
        + (repr.components == 1 ? ""
                                : "_VEC" + std::to_string(repr.components));
    if (name == spelling)
      return type;
  }
  return ANARI_UNKNOWN;
}

inline bool fieldType(std::string_view name, anari::DataType type)
{
  const auto repr = representation(type);
  const auto suffix = name.substr(name.find('.') + 1);
  return repr.base
      && (suffix != "position" || (repr.floating && repr.components == 3))
      && (suffix != "index" || type == ANARI_UINT32_VEC3
          || type == ANARI_UINT64_VEC3)
      && (suffix != "normal" || repr.components == 3)
      && (suffix != "tangent" || repr.components == 4)
      && (suffix != "color" || repr.components == 3 || repr.components == 4);
}
} // namespace detail

inline const char *decodeBundle(std::pmr::vector<std::uint8_t> &&bytes,
    Bundle &result,
    const std::function<bool()> &abandoned)
{
  const std::uint16_t endian = 1;
  if (*reinterpret_cast<const std::uint8_t *>(&endian) != 1)
    return "unsupported_byte_order";
  if (bytes.size() < 12 || bytes.size() > 12 + HEADER_CAP + PAYLOAD_CAP
      || std::memcmp(bytes.data(), "VMESH01\n", 8))
    return "invalid_mesh_bundle";
  std::uint32_t headerBytes = 0;
  for (int i = 0; i < 4; ++i)
    headerBytes |= std::uint32_t(bytes[8 + i]) << (8 * i);
  if (!headerBytes || headerBytes > HEADER_CAP
      || headerBytes > bytes.size() - 12)
    return "invalid_mesh_bundle";
  const std::string_view header(
      reinterpret_cast<const char *>(bytes.data() + 12), headerBytes);
  if (abandoned && abandoned())
    return "cancelled";
  if (!detail::boundedMetadata(header))
    return "mesh_metadata_limit";
  mcp::json::Value root;
  if (!detail::utf8(header) || !mcp::json::parse(header, root)
      || !detail::members(
          root, {"revision", "byte_order", "fields", "arrays", "source"}))
    return "invalid_mesh_bundle";
  std::size_t revision = 0;
  if (!detail::size(root.get("revision"), 1, revision) || revision != 1)
    return "unsupported_mesh_revision";
  if (!detail::text(root.get("byte_order"), "little"))
    return "unsupported_byte_order";
  if (const auto *source = root.get("source"); source && !source->isObject())
    return "invalid_mesh_bundle";
  const auto *fields = root.get("fields"), *arrays = root.get("arrays");
  if (!fields || !fields->isObject() || fields->o.size() > FIELD_CAP || !arrays
      || !arrays->isObject() || arrays->o.size() > FIELD_CAP)
    return "invalid_mesh_bundle";
  const auto order = detail::arrayOrder(header);
  if (order.size() != arrays->o.size())
    return "invalid_mesh_bundle";
  Bundle staged(bytes.get_allocator().resource());
  staged.arrays.reserve(order.size());
  staged.fields.reserve(fields->o.size());
  std::size_t offset = 12 + headerBytes, total = 0;
  for (const auto &id : order) {
    if (abandoned && abandoned())
      return "cancelled";
    const auto *value = arrays->get(id);
    if (!value
        || !detail::members(*value, {"element_type", "count", "byte_length"})
        || value->o.size() != 3)
      return "invalid_mesh_bundle";
    const auto *typeName = value->get("element_type");
    if (!typeName || !typeName->isString())
      return "unsupported_element_type";
    const auto type = detail::elementType(typeName->s);
    const auto repr = representation(type);
    if (!repr.base)
      return "unsupported_element_type";
    const auto elementBytes = repr.components * repr.scalarBytes;
    std::size_t count = 0, byteLength = 0;
    if (!detail::size(value->get("count"), PAYLOAD_CAP / elementBytes, count)
        || !count
        || !detail::size(value->get("byte_length"), PAYLOAD_CAP, byteLength)
        || count * elementBytes != byteLength
        || byteLength > PAYLOAD_CAP - total
        || byteLength > bytes.size() - offset)
      return "invalid_payload_length";
    // Payload offsets need not be naturally aligned. The shared profile
    // validator reads samples without typed-pointer aliasing.
    if (const auto *reason = finiteValues(
            {bytes.data() + offset, type, count, byteLength}, abandoned))
      return reason;
    staged.arrays.push_back({id, type, count, offset, byteLength});
    offset += byteLength;
    total += byteLength;
  }
  if (offset != bytes.size())
    return "invalid_payload_length";
  for (const auto &[name, value] : fields->o) {
    const auto *associated = association(name);
    if (!associated)
      return "unsupported_field";
    if (!detail::members(value, {"action", "payload", "enabled", "association"})
        || !detail::text(value.get("association"), associated))
      return "invalid_mesh_bundle";
    const auto *enabled = value.get("enabled");
    if (!enabled || enabled->type != mcp::json::Value::Type::BOOL)
      return "invalid_mesh_bundle";
    Action action;
    if (detail::text(value.get("action"), "preserve"))
      action = Action::PRESERVE;
    else if (detail::text(value.get("action"), "replace"))
      action = Action::REPLACE;
    else if (detail::text(value.get("action"), "remove"))
      action = Action::REMOVE;
    else
      return "unsupported_field_action";
    if (action == Action::REMOVE) {
      if (name == "vertex.position" || value.get("payload"))
        return "invalid_mesh_bundle";
      staged.fields.push_back({name, action, enabled->b, FIELD_CAP});
      continue;
    }
    const auto *payload = value.get("payload");
    if (!payload || !payload->isString())
      return "invalid_mesh_bundle";
    const auto found = std::find_if(staged.arrays.begin(),
        staged.arrays.end(),
        [&](const Payload &array) { return array.id == payload->s; });
    if (found == staged.arrays.end())
      return "invalid_payload_reference";
    if (!detail::fieldType(name, found->elementType))
      return "unsupported_element_type";
    staged.fields.push_back(
        {name, action, enabled->b, std::size_t(found - staged.arrays.begin())});
  }
  staged.bytes = std::move(bytes);
  result = std::move(staged);
  return nullptr;
}

} // namespace vsr::integration::mesh

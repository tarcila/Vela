// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <array>
#include <cstddef>

namespace vsr::integration::inspection {

// Only syntax vocabulary belongs here. Never attach request values or live
// identity observations to these pre-admission diagnostics.
enum class DiagnosticField
{
  ARGUMENTS,
  REQUEST_ID,
  SCENE_ID,
  KIND,
  PAGE_SIZE,
  CURSOR,
  NAME,
  NAME_CONTAINS,
  HANDLE,
  OBJECT_TYPE,
  OBJECT_SUBTYPE,
  NODE_KIND,
  LAYER_HANDLE,
  PARENT_HANDLE
};
enum class DiagnosticReason
{
  TYPE,
  REQUIRED,
  RANGE,
  UNSUPPORTED,
  INAPPLICABLE,
  LENGTH,
  HANDLE_SYNTAX,
  HANDLE_KIND,
  CURSOR
};

inline const char *diagnosticName(DiagnosticField field)
{
  constexpr const char *names[] = {"arguments",
      "request_id",
      "scene_id",
      "kind",
      "page_size",
      "cursor",
      "name",
      "name_contains",
      "handle",
      "object_type",
      "object_subtype",
      "node_kind",
      "layer_handle",
      "parent_handle"};
  return names[std::size_t(field)];
}
inline const char *diagnosticName(DiagnosticReason reason)
{
  constexpr const char *names[] = {"invalid_type",
      "required",
      "out_of_range",
      "unsupported_value",
      "inapplicable_filter",
      "too_long",
      "invalid_handle_syntax",
      "invalid_handle_kind",
      "invalid_cursor"};
  return names[std::size_t(reason)];
}

struct Diagnostic
{
  DiagnosticField field;
  DiagnosticReason reason;
};
struct RequestDiagnostics
{
  static constexpr std::size_t MAX_ENTRIES = 16;
  static constexpr std::size_t MAX_BYTES = 4096;
  std::array<Diagnostic, MAX_ENTRIES> entries{};
  std::size_t count{0};
  void add(DiagnosticField field, DiagnosticReason reason);
};
inline void RequestDiagnostics::add(
    DiagnosticField field, DiagnosticReason reason)
{
  if (count < entries.size())
    entries[count++] = {field, reason};
}

} // namespace vsr::integration::inspection

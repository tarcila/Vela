// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Header-only: the vsr_integration library globs only top-level sources (see
// ticket 19).

#include "vsr/integration/mcp/tools/ToolSupport.hpp"
#include "vsr/integration/restore/BatchRestore.hpp"
// std
#include <vector>

namespace vsr::integration::mcp::tools {

/*
 * vsr_edit and vsr_restore: scene mutations admitted as one operation per
 * call (spec §10, §11). Both return the admission acknowledgement; the
 * terminal status (poll vsr_operation_status) carries `committed` and, on
 * commit, `result.batch_ref` (edit) or `result.target_batch_ref` (restore).
 *
 * Wire shapes:
 *   edit:    {request_id, scene_id, edits: [{handle, version, parameter,
 *            value}]}; `handle` and `version` are copied from a vsr_inspect
 *            item (handle = 32 hex: domain then serial, current launch).
 *   restore: {request_id, scene_id, target_batch_ref: {launch_id, batch_id}}
 *
 * The headless services own every rule; this layer only parses arguments.
 */

struct EditDeps
{
  identity::IdentityAuthority &ids; // launch stamp for wire handles
  edit::EditService &edits;
  restore::RestoreService &restores;
};

namespace detail {

inline int hexDigit(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}

// 32 lowercase hex: domain (16) then serial (16), as vsr_inspect emits.
inline std::optional<identity::ObjectHandle> handleArg(
    const json::Value *v, const identity::LaunchStamp &launch)
{
  if (!v || !v->isString() || v->s.size() != 32)
    return std::nullopt;
  std::uint64_t parts[2] = {0, 0};
  for (std::size_t i = 0; i < 32; i++) {
    const int d = hexDigit(v->s[i]);
    if (d < 0)
      return std::nullopt;
    parts[i / 16] = (parts[i / 16] << 4) | std::uint64_t(d);
  }
  identity::ObjectHandle h;
  h.launch = launch;
  h.domain = parts[0];
  h.serial = parts[1];
  return h;
}

// Carries every JSON kind with its full content so request canonicalization
// sees the whole value; the edit surface rejects non-number kinds with its own
// ValueError.
inline JsonValue editValue(const json::Value &v, int depth = 0)
{
  using T = json::Value::Type;
  if (depth > 4 && (v.type == T::ARRAY || v.type == T::OBJECT))
    return JsonValue::ofKind(JsonKind::MALFORMED);
  switch (v.type) {
  case T::NUMBER:
    return JsonValue::fromNumber(v.n);
  case T::ARRAY: {
    std::vector<JsonValue> e;
    for (const auto &x : v.a)
      e.push_back(editValue(x, depth + 1));
    return JsonValue::array(std::move(e));
  }
  case T::BOOL: {
    auto j = JsonValue::ofKind(JsonKind::BOOLEAN);
    j.boolean = v.b;
    return j;
  }
  case T::STRING: {
    auto j = JsonValue::ofKind(JsonKind::STRING);
    j.text = v.s;
    return j;
  }
  case T::NUL:
    return JsonValue::ofKind(JsonKind::NULL_VALUE);
  case T::OBJECT: {
    auto j = JsonValue::ofKind(JsonKind::OBJECT);
    for (const auto &[k, c] : v.o) // std::map: sorted, unique keys
      j.members.emplace_back(k, editValue(c, depth + 1));
    return j;
  }
  }
  return JsonValue::ofKind(JsonKind::MALFORMED);
}

inline std::optional<edit::EditBatch> editBatchArg(
    const json::Value &args, const identity::LaunchStamp &launch)
{
  const auto scene = targetIdArg(args, "scene_id");
  const auto *list = args.get("edits");
  if (!scene || !list || list->type != json::Value::Type::ARRAY)
    return std::nullopt;
  edit::EditBatch batch;
  batch.scene = *scene;
  for (const auto &e : list->a) {
    std::optional<std::int64_t> version;
    const auto handle = handleArg(e.get("handle"), launch);
    const auto *param = e.get("parameter");
    const auto *value = e.get("value");
    if (!e.isObject() || !handle || !optionalIntArg(e, "version", version)
        || !version || *version < 0 || !param || !param->isString()
        || !value)
      return std::nullopt;
    edit::EditEntry entry;
    entry.expected.object = *handle;
    entry.expected.version = std::uint64_t(*version);
    entry.parameter = param->s;
    entry.value = editValue(*value);
    batch.edits.push_back(std::move(entry));
  }
  return batch;
}

inline std::optional<restore::RestoreRequest> restoreArg(
    const json::Value &args)
{
  const auto scene = targetIdArg(args, "scene_id");
  const auto *t = args.get("target_batch_ref");
  if (!scene || !t || !t->isObject())
    return std::nullopt;
  const auto *launch = t->get("launch_id");
  const auto *batch = t->get("batch_id");
  if (!launch || !launch->isString() || !batch || !batch->isString())
    return std::nullopt;
  restore::RestoreRequest r;
  r.scene = *scene;
  r.targetBatchRef = edit::BatchRef{launch->s, batch->s};
  return r;
}

inline ToolResult ack(
    const operations::OperationRef &ref, const operations::Admission &a)
{
  if (a.code != Outcome::OK)
    return rejected(a.code);
  return statusResult(ref, a.status, a.replayed);
}

} // namespace detail

inline void addEditTools(std::vector<Tool> &tools, EditDeps deps)
{
  Tool editTool;
  editTool.name = "vsr_edit";
  editTool.description =
      "Atomically apply up to 64 parameter edits to one scene; returns a "
      "batch_ref on commit.";
  editTool.inputSchemaJson = std::string("{\"type\":\"object\",\"properties\":{")
      + requestIdSchema()
      + ",\"scene_id\":{\"type\":\"integer\",\"minimum\":0},"
        "\"edits\":{\"type\":\"array\",\"minItems\":1,\"maxItems\":64,"
        "\"items\":{\"type\":\"object\",\"properties\":{"
        "\"handle\":{\"type\":\"string\"},"
        "\"version\":{\"type\":\"integer\",\"minimum\":0},"
        "\"parameter\":{\"type\":\"string\"},\"value\":{}},"
        "\"required\":[\"handle\",\"version\",\"parameter\",\"value\"]}}},"
        "\"required\":[\"request_id\",\"scene_id\",\"edits\"]}";
  editTool.handler = [deps](const session::Credentials &c,
                     const json::Value &args) {
    const auto id = requestIdArg(args.get("request_id"));
    auto batch = detail::editBatchArg(args, deps.ids.launch());
    if (!id || !batch)
      return rejected(Outcome::INVALID_REQUEST);
    const operations::OperationRef ref{c.sessionId, *id};
    return detail::ack(ref, deps.edits.submit(c, ref, std::move(*batch)));
  };
  tools.push_back(std::move(editTool));

  Tool restoreTool;
  restoreTool.name = "vsr_restore";
  restoreTool.description =
      "One-way restore of the batch named by target_batch_ref.";
  restoreTool.inputSchemaJson =
      std::string("{\"type\":\"object\",\"properties\":{") + requestIdSchema()
      + ",\"scene_id\":{\"type\":\"integer\",\"minimum\":0},"
        "\"target_batch_ref\":{\"type\":\"object\",\"properties\":{"
        "\"launch_id\":{\"type\":\"string\"},"
        "\"batch_id\":{\"type\":\"string\"}},"
        "\"required\":[\"launch_id\",\"batch_id\"]}},"
        "\"required\":[\"request_id\",\"scene_id\",\"target_batch_ref\"]}";
  restoreTool.handler = [deps](const session::Credentials &c,
                        const json::Value &args) {
    const auto id = requestIdArg(args.get("request_id"));
    auto req = detail::restoreArg(args);
    if (!id || !req)
      return rejected(Outcome::INVALID_REQUEST);
    const operations::OperationRef ref{c.sessionId, *id};
    return detail::ack(ref, deps.restores.submit(c, ref, std::move(*req)));
  };
  tools.push_back(std::move(restoreTool));
}

} // namespace vsr::integration::mcp::tools

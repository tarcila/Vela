// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Header-only: the vsr_integration library globs only top-level sources (see
// ticket 19).

#include "vsr/integration/mcp/McpServer.hpp"
// std
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace vsr::integration::mcp::tools {

/*
 * Shared argument parsing and payload placement for MCP tools. Every tool
 * family (discovery/inspection, lookup/cancel, edit/restore, render/resync)
 * builds its mcp::Tool entries on these helpers so the carrier mapping stays
 * in one place; see ../README.md for the contract.
 *
 * Placement:
 *  - A rejected call (nothing admitted, or a failed lookup/cancel) returns a
 *    JSON-RPC error; error.data.code carries the frozen code.
 *  - Otherwise result.structuredContent carries the operation status object
 *    and result.content one text item with the same JSON. isError is true
 *    exactly when the reported operation is terminal with a non-ok outcome.
 */

using operations::Outcome;

// JSON integer-valued request ID in [1, 2^53-1]; nothing else.
inline std::optional<std::uint64_t> requestIdArg(const json::Value *v)
{
  if (!v || !v->isNumber())
    return std::nullopt;
  return operations::requestIdFromNumber(v->n);
}

// Optional integer argument; `present` false when omitted. Returns false for
// a present non-integer (callers reject rather than coerce).
inline bool optionalIntArg(
    const json::Value &args, const char *key, std::optional<std::int64_t> &out)
{
  out.reset();
  const auto *v = args.get(key);
  if (!v)
    return true;
  if (!v->isNumber() || !std::isfinite(v->n) || std::floor(v->n) != v->n
      || v->n < -9007199254740991.0 || v->n > 9007199254740991.0)
    return false;
  out = std::int64_t(v->n);
  return true;
}

// Optional string argument; false for a present non-string.
inline bool optionalStringArg(
    const json::Value &args, const char *key, std::string &out)
{
  out.clear();
  const auto *v = args.get(key);
  if (!v)
    return true;
  if (!v->isString())
    return false;
  out = v->s;
  return true;
}

// Registered target id (non-negative 32-bit integer).
inline std::optional<std::uint32_t> targetIdArg(
    const json::Value &args, const char *key)
{
  std::optional<std::int64_t> v;
  if (!optionalIntArg(args, key, v) || !v || *v < 0 || *v > 0xFFFFFFFEll)
    return std::nullopt;
  return std::uint32_t(*v);
}

// Wire `operation_ref`: {"session_id": string, "request_id": number}.
inline std::optional<operations::OperationRef> operationRefArg(
    const json::Value &args)
{
  const auto *ref = args.get("operation_ref");
  if (!ref || !ref->isObject())
    return std::nullopt;
  const auto *sid = ref->get("session_id");
  const auto id = requestIdArg(ref->get("request_id"));
  if (!sid || !sid->isString() || !id)
    return std::nullopt;
  return operations::OperationRef{sid->s, *id};
}

inline std::string operationRefJson(const operations::OperationRef &ref)
{
  return "{\"session_id\":" + json::quote(ref.sessionId)
      + ",\"request_id\":" + std::to_string(ref.requestId) + "}";
}

inline ToolResult rejected(Outcome code)
{
  ToolResult r;
  r.code = code == Outcome::OK ? Outcome::UNKNOWN : code;
  return r;
}

// Status object shared by admission acknowledgements, lookup and cancel:
//   operation_ref, kind, state ("pending" | "terminal"), progress (frozen
//   non-terminal name, while a slot is held), outcome (frozen code, once
//   terminal), committed, result_visible, result (engine JSON, when
//   visible), replayed (admissions only).
inline ToolResult statusResult(const operations::OperationRef &ref,
    const operations::Status &s,
    std::optional<bool> replayed = std::nullopt)
{
  std::string j = "{\"operation_ref\":" + operationRefJson(ref)
      + ",\"kind\":" + json::quote(s.kind) + ",\"state\":"
      + (s.terminal ? "\"terminal\"" : "\"pending\"");
  if (s.progress)
    j += ",\"progress\":" + json::quote(operations::progressName(*s.progress));
  if (s.terminal)
    j += ",\"outcome\":" + json::quote(operations::outcomeName(*s.terminal));
  j += std::string(",\"committed\":") + (s.committed ? "true" : "false");
  j += std::string(",\"result_visible\":")
      + (s.resultVisible ? "true" : "false");
  if (s.resultVisible && !s.result.empty())
    j += ",\"result\":" + s.result; // engine results are JSON objects
  if (replayed)
    j += std::string(",\"replayed\":") + (*replayed ? "true" : "false");
  j += "}";

  ToolResult r;
  r.structuredJson = j;
  r.contentJson = "[{\"type\":\"text\",\"text\":" + json::quote(j) + "}]";
  r.isError = s.terminal && *s.terminal != Outcome::OK;
  return r;
}

// Submits `request` under the caller's session and `request_id`, returning
// the admission acknowledgement (or the pre-admission rejection code).
inline ToolResult submitAndAck(operations::OperationEngine &engine,
    const session::Credentials &creds,
    const json::Value &args,
    operations::Request request)
{
  const auto id = requestIdArg(args.get("request_id"));
  if (!id)
    return rejected(Outcome::INVALID_REQUEST);
  const operations::OperationRef ref{creds.sessionId, *id};
  const auto a = engine.submit(creds, ref, std::move(request));
  if (a.code != Outcome::OK)
    return rejected(a.code);
  return statusResult(ref, a.status, a.replayed);
}

inline const char *requestIdSchema()
{
  return "\"request_id\":{\"type\":\"integer\",\"minimum\":1,"
         "\"maximum\":9007199254740991}";
}

} // namespace vsr::integration::mcp::tools

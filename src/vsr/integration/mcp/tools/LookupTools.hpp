// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Header-only: the vsr_integration library globs only top-level sources (see
// ticket 19).

#include "vsr/integration/mcp/tools/ToolSupport.hpp"
// std
#include <vector>

namespace vsr::integration::mcp::tools {

/*
 * vsr_operation_status and vsr_cancel: control-path calls addressed by a
 * complete operation_ref (spec §9). They consume no scene slot and no
 * request ID. Status may name an earlier session's reference from the same
 * launch (needs current read on every covered target); cancel only ever
 * applies to the caller's own session.
 */

inline void addLookupTools(
    std::vector<Tool> &tools, operations::OperationEngine &engine)
{
  const std::string schema =
      "{\"type\":\"object\",\"properties\":{\"operation_ref\":{"
      "\"type\":\"object\",\"properties\":{"
      "\"session_id\":{\"type\":\"string\"},"
      "\"request_id\":{\"type\":\"integer\",\"minimum\":1,"
      "\"maximum\":9007199254740991}},"
      "\"required\":[\"session_id\",\"request_id\"]}},"
      "\"required\":[\"operation_ref\"]}";

  Tool status;
  status.name = "vsr_operation_status";
  status.description =
      "Status and, with read permission, result of an operation_ref.";
  status.inputSchemaJson = schema;
  status.handler = [&engine](const session::Credentials &c,
                       const json::Value &args) {
    const auto ref = operationRefArg(args);
    if (!ref)
      return rejected(Outcome::INVALID_REQUEST);
    const auto s = engine.status(c, *ref);
    if (s.code != Outcome::OK)
      return rejected(s.code);
    return statusResult(*ref, s);
  };
  tools.push_back(std::move(status));

  Tool cancel;
  cancel.name = "vsr_cancel";
  cancel.description = "Cancel the caller's own operation_ref.";
  cancel.inputSchemaJson = schema;
  cancel.handler = [&engine](const session::Credentials &c,
                       const json::Value &args) {
    const auto ref = operationRefArg(args);
    if (!ref)
      return rejected(Outcome::INVALID_REQUEST);
    const auto s = engine.cancel(c, *ref);
    if (s.code != Outcome::OK)
      return rejected(s.code);
    return statusResult(*ref, s);
  };
  tools.push_back(std::move(cancel));
}

} // namespace vsr::integration::mcp::tools

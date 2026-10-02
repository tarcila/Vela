// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Header-only: the vsr_integration library globs only top-level sources (see
// ticket 19).

#include "vsr/integration/inspection/Inspection.hpp"
#include "vsr/integration/mcp/tools/ToolSupport.hpp"
// std
#include <vector>

namespace vsr::integration::mcp::tools {

/*
 * vsr_discover and vsr_inspect: one paged, admitted operation per call
 * (spec §7). Each call carries a client-chosen request_id; the page arrives
 * in structuredContent.result once the operation is terminal (poll
 * vsr_operation_status with the returned operation_ref).
 */

struct QueryDeps
{
  operations::OperationEngine &engine;
  inspection::Inspector &inspector;
};

namespace detail {

inline const char *pageSchemaProps()
{
  return "\"page_size\":{\"type\":\"integer\",\"minimum\":1},"
         "\"cursor\":{\"type\":\"string\"}";
}

inline bool pageArgs(const json::Value &args, inspection::PageArgs &out)
{
  return optionalIntArg(args, "page_size", out.pageSize)
      && optionalStringArg(args, "cursor", out.cursor);
}

} // namespace detail

inline void addQueryTools(std::vector<Tool> &tools, QueryDeps deps)
{
  Tool discover;
  discover.name = "vsr_discover";
  discover.description =
      "List readable exposed scenes and views with current capabilities.";
  discover.inputSchemaJson = std::string("{\"type\":\"object\",\"properties\":{")
      + requestIdSchema() + "," + detail::pageSchemaProps()
      + "},\"required\":[\"request_id\"]}";
  discover.handler = [deps](const session::Credentials &c,
                         const json::Value &args) {
    inspection::PageArgs page;
    if (!detail::pageArgs(args, page))
      return rejected(Outcome::INVALID_REQUEST);
    auto built = deps.inspector.makeDiscovery(c.sessionId, page);
    if (built.code != Outcome::OK)
      return rejected(built.code);
    return submitAndAck(deps.engine, c, args, std::move(built.request));
  };
  tools.push_back(std::move(discover));

  Tool inspect;
  inspect.name = "vsr_inspect";
  inspect.description = "Page through objects and parameters of one scene.";
  inspect.inputSchemaJson = std::string("{\"type\":\"object\",\"properties\":{")
      + requestIdSchema() + ",\"scene_id\":{\"type\":\"integer\",\"minimum\":0},"
      + detail::pageSchemaProps()
      + "},\"required\":[\"request_id\",\"scene_id\"]}";
  inspect.handler = [deps](const session::Credentials &c,
                        const json::Value &args) {
    inspection::PageArgs page;
    const auto scene = targetIdArg(args, "scene_id");
    if (!scene || !detail::pageArgs(args, page))
      return rejected(Outcome::INVALID_REQUEST);
    auto built = deps.inspector.makeInspection(c.sessionId, *scene, page);
    if (built.code != Outcome::OK)
      return rejected(built.code);
    return submitAndAck(deps.engine, c, args, std::move(built.request));
  };
  tools.push_back(std::move(inspect));
}

} // namespace vsr::integration::mcp::tools

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

inline std::string pageSchemaProps(const inspection::PageLimits &limits)
{
  return "\"page_size\":{\"type\":\"integer\",\"minimum\":1,\"default\":"
      + std::to_string(limits.defaultItems) + ",\"maximum\":"
      + std::to_string(limits.maxItems) + "},\"cursor\":{\"type\":\"string\"}";
}

inline std::string filterSchemaProps(const inspection::PageLimits &limits)
{
  const std::string text = "{\"type\":\"string\",\"maxLength\":"
      + std::to_string(limits.displayString) + "}";
  std::string props = "\"kind\":{\"type\":\"string\",\"enum\":[\"objects\",\"layers\"],"
                     "\"default\":\"objects\"},\"name\":"
      + text + ",\"name_contains\":" + text + ",\"object_subtype\":" + text
      + ",\"handle\":{\"type\":\"string\",\"pattern\":\"^[0-9a-f]{32}$\"},"
        "\"object_type\":{\"type\":\"string\",\"enum\":[";
  bool first = true;
  for (auto type : {ANARI_ARRAY,
           ANARI_ARRAY1D,
           ANARI_ARRAY2D,
           ANARI_ARRAY3D,
           ANARI_SURFACE,
           ANARI_GEOMETRY,
           ANARI_MATERIAL,
           ANARI_SAMPLER,
           ANARI_VOLUME,
           ANARI_SPATIAL_FIELD,
           ANARI_LIGHT,
           ANARI_CAMERA,
           ANARI_RENDERER}) {
    if (!first)
      props += ",";
    first = false;
    props += json::quote(anari::toString(type));
  }
  return props + "]}";
}

inline bool filterArgs(const json::Value &args, inspection::PageArgs &out)
{
  if (const auto *kind = args.get("kind")) {
    if (!kind->isString())
      return false;
    out.kind = kind->s;
  }
  for (const char *key : {"node_kind", "layer_handle", "parent_handle"})
    if (args.get(key))
      return false;
  auto text = [&](const char *key, std::optional<std::string> &value) {
    const auto *v = args.get(key);
    if (!v)
      return true;
    if (!v->isString())
      return false;
    value = v->s;
    return true;
  };
  return text("name", out.filters.name)
      && text("name_contains", out.filters.nameContains)
      && text("handle", out.filters.handle)
      && text("object_type", out.filters.objectType)
      && text("object_subtype", out.filters.objectSubtype);
}

inline bool pageArgs(const json::Value &args, inspection::PageArgs &out)
{
  return optionalIntArg(args, "page_size", out.pageSize)
      && optionalStringArg(args, "cursor", out.cursor);
}

} // namespace detail

inline void addQueryTools(std::vector<Tool> &tools, QueryDeps deps)
{
  const auto &limits = deps.inspector.limits();
  const std::string paging = " Default page size "
      + std::to_string(limits.defaultItems) + ", maximum "
      + std::to_string(limits.maxItems)
      + "; supported inspection kinds: objects, layers.";
  Tool discover;
  discover.name = "vsr_discover";
  discover.description =
      "List readable exposed scenes and views with current capabilities."
      + paging;
  discover.inputSchemaJson =
      std::string("{\"type\":\"object\",\"properties\":{") + requestIdSchema()
      + "," + detail::pageSchemaProps(limits)
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
  inspect.description =
      "Page through scene objects/parameters (default) or layers with live "
      "root handles. Layer filters: exact stored name, exact layer handle, "
      "and literal case-sensitive name_contains intersect. Object/node-only "
      "filters reject for layers; general nodes are unsupported. Object filters: "
      "optional exact name, handle, ANARI object_type and "
      "object_subtype filters intersect with literal, "
      "case-sensitive name_contains (not regex/glob). "
      "Filter strings are limited to "
      + std::to_string(limits.displayString) + " UTF-8 bytes." + paging;
  inspect.inputSchemaJson = std::string("{\"type\":\"object\",\"properties\":{")
      + requestIdSchema()
      + ",\"scene_id\":{\"type\":\"integer\",\"minimum\":0},"
      + detail::pageSchemaProps(limits) + ","
      + detail::filterSchemaProps(limits)
      + "},\"required\":[\"request_id\",\"scene_id\"]}";
  inspect.handler = [deps](const session::Credentials &c,
                        const json::Value &args) {
    inspection::PageArgs page;
    const auto scene = targetIdArg(args, "scene_id");
    if (!scene || !detail::pageArgs(args, page)
        || !detail::filterArgs(args, page))
      return rejected(Outcome::INVALID_REQUEST);
    auto built = deps.inspector.makeInspection(c.sessionId, *scene, page);
    if (built.code != Outcome::OK)
      return rejected(built.code);
    return submitAndAck(deps.engine, c, args, std::move(built.request));
  };
  tools.push_back(std::move(inspect));
}

} // namespace vsr::integration::mcp::tools

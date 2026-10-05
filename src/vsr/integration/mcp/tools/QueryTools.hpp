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
  std::string props = "\"kind\":{\"type\":\"string\",\"enum\":[\"objects\",\"layers\",\"nodes\"],"
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
  return props + "]},\"node_kind\":{\"type\":\"string\",\"enum\":[\"empty\",\"transform\",\"object\",\"transform_array\"]},"
      "\"layer_handle\":{\"type\":\"string\",\"pattern\":\"^[0-9a-f]{32}$\"},"
      "\"parent_handle\":{\"type\":\"string\",\"pattern\":\"^[0-9a-f]{32}$\"}";
}

inline inspection::RequestDiagnostics argTypes(
    const json::Value &args, bool inspect)
{
  using F = inspection::DiagnosticField;
  using R = inspection::DiagnosticReason;
  inspection::RequestDiagnostics errors;
  if (!args.isObject()) {
    errors.add(F::ARGUMENTS, R::TYPE);
    return errors;
  }
  const auto idReason = [&](const char *key) {
    const auto *v = args.get(key);
    return !v ? R::REQUIRED : !v->isNumber() ? R::TYPE : R::RANGE;
  };
  if (!requestIdArg(args.get("request_id")))
    errors.add(F::REQUEST_ID, idReason("request_id"));
  if (inspect && !targetIdArg(args, "scene_id"))
    errors.add(F::SCENE_ID, idReason("scene_id"));
  std::optional<std::int64_t> size;
  if (!optionalIntArg(args, "page_size", size))
    errors.add(F::PAGE_SIZE, R::TYPE);
  const F strings[] = {F::CURSOR,
      F::KIND,
      F::NAME,
      F::NAME_CONTAINS,
      F::HANDLE,
      F::OBJECT_TYPE,
      F::OBJECT_SUBTYPE,
      F::NODE_KIND,
      F::LAYER_HANDLE,
      F::PARENT_HANDLE};
  for (const auto field : strings) {
    if (!inspect && field != F::CURSOR)
      continue; // legacy discovery ignores unknown properties
    const auto *value = args.get(inspection::diagnosticName(field));
    if (value && !value->isString())
      errors.add(field, R::TYPE);
  }
  return errors;
}

inline ToolResult queryRejected(
    inspection::RequestDiagnostics errors, const std::string &kind)
{
  auto result = rejected(Outcome::INVALID_REQUEST);
  result.diagnostics = errors;
  result.diagnosticKind = kind;
  return result;
}

inline bool filterArgs(const json::Value &args, inspection::PageArgs &out)
{
  if (const auto *kind = args.get("kind")) {
    if (!kind->isString())
      return false;
    out.kind = kind->s;
  }
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
      && text("object_subtype", out.filters.objectSubtype)
      && text("node_kind", out.filters.nodeKind)
      && text("layer_handle", out.filters.layerHandle)
      && text("parent_handle", out.filters.parentHandle);
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
      + "; supported inspection kinds: objects, layers, nodes.";
  Tool discover;
  discover.name = "vsr_discover";
  discover.description =
      "List readable exposed scenes and views with current capabilities, "
      "bounded names, roles and current effective bindings. Roles are descriptive "
      "and grant neither image access nor backend qualification."
      + paging;
  discover.inputSchemaJson =
      std::string("{\"type\":\"object\",\"properties\":{") + requestIdSchema()
      + "," + detail::pageSchemaProps(limits)
      + "},\"required\":[\"request_id\"]}";
  discover.handler = [deps](const session::Credentials &c,
                         const json::Value &args) {
    inspection::PageArgs page;
    auto errors = detail::argTypes(args, false);
    if (errors.count)
      return detail::queryRejected(errors, page.kind);
    detail::pageArgs(args, page);
    auto built = deps.inspector.makeDiscovery(c.sessionId, page);
    if (built.code != Outcome::OK)
      return built.diagnostics.count
          ? detail::queryRejected(built.diagnostics, page.kind)
          : rejected(built.code);
    return submitAndAck(deps.engine, c, args, std::move(built.request));
  };
  tools.push_back(std::move(discover));

  Tool inspect;
  inspect.name = "vsr_inspect";
  inspect.description =
      "Minimal examples (IDs illustrative; use a discovered scene_id and fresh "
      "request_id per operation): {\"request_id\":1,\"scene_id\":0,\"kind\":\"objects\"}; "
      "{\"request_id\":2,\"scene_id\":0,\"kind\":\"layers\"}; "
      "{\"request_id\":3,\"scene_id\":0,\"kind\":\"nodes\",\"name\":\"RootDelta\"}. "
      "Omit unused filters; name=\"\" means the exact empty stored name, not no "
      "filter. Empty name_contains matches all names; omitted/empty cursor starts "
      "the first page. Filters intersect: handle plus name selects that same "
      "node only, not its children; parent_handle selects direct children. "
      "Use handles and node_edit_version/structural_version/ancestry_version "
      "from fresh inspection, never zero/invented placeholders. Copy node edit "
      "versions verbatim into version; world edits also require ancestry_version. "
      "World position is the node origin (local [0,0,0] transformed by its world "
      "matrix), not geometry centre or bounds centre. Transform inheritance "
      "example, identity linear transforms: RootDelta local x=0 has child BodyTop "
      "local x=1, so origins are 0 and 1. Moving only RootDelta by +2 in local x "
      "moves both origins once to 2 and 3; BodyTop local x stays 1. Applying +2 "
      "separately to both local positions puts BodyTop at world x=5 (double "
      "movement). For two selected sibling roots, move those roots independently; "
      "do not choose their unselected common parent or move unselected siblings. "
      "These names are illustrative, not inferred assemblies. "
      "Page through scene objects/parameters (default) or layers with live "
      "root handles. Layer filters: exact stored name, exact layer handle, "
      "and literal case-sensitive name_contains intersect. Object/node-only "
      "filters reject for layers. Nodes expose full row-major local/world "
      "matrices with column-vector mathematics, local position, and faithful "
      "degree-based azimuth(Y), elevation(X), roll(Z), composed Y then X then Z, "
      "and scale. Unavailable world/SRT observations carry explicit reasons; "
      "edit affordances describe representability, not write permission. "
      "Node filters: name, name_contains, handle, node_kind, layer_handle and "
      "parent_handle intersect. Object filters: "
      "optional exact name, handle, ANARI object_type and "
      "object_subtype filters intersect with literal, "
      "case-sensitive name_contains (not regex/glob). "
      "Filter strings are limited to "
      + std::to_string(limits.displayString)
      + " UTF-8 bytes. Stored names are matched resumably, not against previews; "
        "name_work counts byte comparisons (including substring fallback), with "
      + std::to_string(limits.nameWork) + " per operation and "
      + std::to_string(limits.sliceNameWork)
      + " per safe-point slice. Work exhaustion returns a continuation." + paging;
  inspect.inputSchemaJson = std::string("{\"type\":\"object\",\"properties\":{")
      + requestIdSchema()
      + ",\"scene_id\":{\"type\":\"integer\",\"minimum\":0},"
      + detail::pageSchemaProps(limits) + ","
      + detail::filterSchemaProps(limits)
      + "},\"required\":[\"request_id\",\"scene_id\"]}";
  inspect.handler = [deps](const session::Credentials &c,
                        const json::Value &args) {
    inspection::PageArgs page;
    auto errors = detail::argTypes(args, true);
    if (errors.count) {
      const auto *kind = args.get("kind");
      return detail::queryRejected(errors,
          !kind                  ? page.kind
              : kind->isString() ? kind->s
                                 : "unknown");
    }
    const auto scene = targetIdArg(args, "scene_id");
    detail::pageArgs(args, page);
    detail::filterArgs(args, page);
    auto built = deps.inspector.makeInspection(c.sessionId, *scene, page);
    if (built.code != Outcome::OK)
      return built.diagnostics.count
          ? detail::queryRejected(built.diagnostics, page.kind)
          : rejected(built.code);
    return submitAndAck(deps.engine, c, args, std::move(built.request));
  };
  tools.push_back(std::move(inspect));

  Tool bounds;
  bounds.name = "vsr_bounds";
  bounds.description =
      "Observe default-visible scene bounds at a safe point: active layers "
      "and effectively enabled contributors only. Missing host binding is "
      "unavailable/missing_binding, not empty geometry. No scans, renderer "
      "setup or provider work. include_disabled=true and any node_handle "
      "are unsupported. Fixed work ceilings: 256 node/geometry steps and "
      "4096 vertex values per slice; 4096 nodes and 1048576 vertex values "
      "per operation; deadline 5 seconds including queue time; shared "
      "temporary scratch at most 8 MiB. Value-only results claim no scans.";
  bounds.inputSchemaJson =
      std::string("{\"type\":\"object\",\"additionalProperties\":false,\"properties\":{")
      + requestIdSchema()
      + ",\"scene_id\":{\"type\":\"integer\",\"minimum\":0},"
        "\"include_disabled\":{\"type\":\"boolean\",\"enum\":[false],\"default\":false}},"
        "\"required\":[\"request_id\",\"scene_id\"]}";
  bounds.handler = [deps](
                       const session::Credentials &c, const json::Value &args) {
    for (const auto &property : args.o)
      if (property.first != "request_id" && property.first != "scene_id"
          && property.first != "include_disabled")
        return rejected(Outcome::INVALID_REQUEST);
    const auto scene = targetIdArg(args, "scene_id");
    inspection::bounds::Args selection;
    if (!scene || args.get("node_handle"))
      return rejected(Outcome::INVALID_REQUEST);
    if (const auto *include = args.get("include_disabled")) {
      if (include->type != json::Value::Type::BOOL)
        return rejected(Outcome::INVALID_REQUEST);
      selection.includeDisabled = include->b;
    }
    auto built = deps.inspector.makeBounds(c.sessionId, *scene, selection);
    if (built.code != Outcome::OK)
      return rejected(built.code);
    return submitAndAck(deps.engine, c, args, std::move(built.request));
  };
  tools.push_back(std::move(bounds));

  Tool viewInfo;
  viewInfo.name = "vsr_view_info";
  viewInfo.description =
      "Read the registered host's actual bound world-space camera/renderer, "
      "Device Identifier/subtype, native and displayed dimensions/scale, "
      "Image Pipeline and color settings at an owning-thread safe point. "
      "Current effective configuration is not an image-producing receipt. "
      "Perspective FOV is vertical degrees; unknown/incomplete fields carry reasons. "
      "Interactive viewports and independent previews have distinct role/origin. "
      "Metadata creates no render, readback, device fallback or camera mutation. "
      "Capture remains unexposed/unqualified. Host-declared quality units "
      "do not establish observed samples or a supported runtime quality contract. "
      "Returned validated policy is authoritative; defaults: axis 2048, pixels "
      "2073600, quality maximum 1024, deadline 120 seconds, PNG 8 MiB, response "
      "12 MiB, per-view/global capacity 1, host/device budgets 256 MiB each. "
      "Roles are descriptive and grant no capture authority.";
  viewInfo.inputSchemaJson =
      std::string("{\"type\":\"object\",\"properties\":{") + requestIdSchema()
      + ",\"view_id\":{\"type\":\"integer\",\"minimum\":0}},"
        "\"required\":[\"request_id\",\"view_id\"]}";
  viewInfo.handler = [deps](const session::Credentials &c,
                         const json::Value &args) {
    const auto view = targetIdArg(args, "view_id");
    if (!view)
      return rejected(Outcome::INVALID_REQUEST);
    auto built = deps.inspector.makeViewInfo(c.sessionId, *view);
    if (built.code != Outcome::OK)
      return rejected(built.code);
    return submitAndAck(deps.engine, c, args, std::move(built.request));
  };
  tools.push_back(std::move(viewInfo));
}

} // namespace vsr::integration::mcp::tools

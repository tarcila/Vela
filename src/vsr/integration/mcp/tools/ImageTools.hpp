// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/image/ImageExchange.hpp"
#include "vsr/integration/mcp/tools/EditTools.hpp"

namespace vsr::integration::mcp::tools {

struct ImageDeps
{
  ImageDeps(
      identity::IdentityAuthority &identity, image::ImageExchange &exchange)
      : identities(&identity), images(&exchange)
  {}
  identity::IdentityAuthority *identities{nullptr};
  image::ImageExchange *images{nullptr};
};

inline void addMeshExportTool(std::vector<Tool> &tools, ImageDeps deps)
{
  Tool tool;
  tool.name = "vsr_mesh_export";
  tool.description =
      "Export one exposed triangle Geometry to a lossless confined .vmesh "
      "working bundle. Requires current Scene READ, not EDIT. Poll mesh_export "
      "status for file_name, target_version and dependency evidence. Use the "
      "documented vsr_mesh Python/NumPy helper to inspect/read/write typed "
      "Arrays. Unsupported dependencies refuse the whole export. No live "
      "Scene mutation, GPU transfer or restoration retention. Replay returns "
      "the historical receipt and never rewrites files. See docs/mesh-exchange.md "
      "for profile, position-only Import and finite limits.";
  tool.inputSchemaJson = std::string("{\"type\":\"object\",\"properties\":{")
      + requestIdSchema()
      + ",\"scene_id\":{\"type\":\"integer\",\"minimum\":0},"
        "\"handle\":{\"type\":\"string\"}},"
        "\"required\":[\"request_id\",\"scene_id\",\"handle\"],"
        "\"additionalProperties\":false}";
  tool.handler = [deps](
                     const session::Credentials &c, const json::Value &args) {
    const auto id = requestIdArg(args.get("request_id"));
    const auto scene = targetIdArg(args, "scene_id");
    const auto handle =
        detail::handleArg(args.get("handle"), deps.identities->launch());
    for (const auto &argument : args.o)
      if (argument.first != "request_id" && argument.first != "scene_id"
          && argument.first != "handle")
        return rejected(operations::Outcome::INVALID_REQUEST);
    if (!id || !scene || !handle)
      return rejected(operations::Outcome::INVALID_REQUEST);
    const operations::OperationRef ref{c.sessionId, *id};
    return detail::ack(
        ref, deps.images->submitMeshExport(c, ref, *scene, *handle));
  };
  tools.push_back(std::move(tool));
}

inline void addMeshImportTool(std::vector<Tool> &tools, ImageDeps deps)
{
  if (!deps.images->importAvailable())
    return;
  Tool tool;
  tool.name = "vsr_mesh_import";
  tool.description =
      "Replace count-preserving positions on one exposed triangle Geometry from a confined .vmesh. Requires current READ+EDIT and enabled agent edits. Copy current target_version and complete dependencies from Mesh Export; descriptive bundle source is not authority and export provenance is not required. Other fields must preserve; no count, state or optional field changes yet. Poll mesh_import for committed, current evidence and normal batch_ref for vsr_restore. Old shared Arrays are never overwritten. See docs/mesh-exchange.md.";
  tool.inputSchemaJson = std::string("{\"type\":\"object\",\"properties\":{")
      + requestIdSchema()
      + ",\"scene_id\":{\"type\":\"integer\",\"minimum\":0},\"handle\":{\"type\":\"string\"},"
        "\"target_version\":{\"type\":\"string\"},\"dependencies\":{\"type\":\"array\",\"maxItems\":64},"
        "\"file_name\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":128}},"
        "\"required\":[\"request_id\",\"scene_id\",\"handle\",\"target_version\",\"dependencies\",\"file_name\"],\"additionalProperties\":false}";
  tool.handler = [deps](
                     const session::Credentials &c, const json::Value &args) {
    for (const auto &argument : args.o)
      if (argument.first != "request_id" && argument.first != "scene_id"
          && argument.first != "handle" && argument.first != "target_version"
          && argument.first != "dependencies" && argument.first != "file_name")
        return rejected(operations::Outcome::INVALID_REQUEST);
    const auto id = requestIdArg(args.get("request_id"));
    const auto scene = targetIdArg(args, "scene_id");
    const auto handle =
        detail::handleArg(args.get("handle"), deps.identities->launch());
    const auto *version = args.get("target_version"),
               *dependencies = args.get("dependencies"),
               *file = args.get("file_name");
    if (!id || !scene || !handle || !version || !version->isString()
        || !dependencies || dependencies->type != json::Value::Type::ARRAY
        || !file || !file->isString())
      return rejected(operations::Outcome::INVALID_REQUEST);
    const operations::OperationRef ref{c.sessionId, *id};
    return detail::ack(ref,
        deps.images->submitMeshImport(c,
            ref,
            *scene,
            *handle,
            version->s,
            json::write(*dependencies),
            file->s));
  };
  tools.push_back(std::move(tool));
}

inline void addImageTools(std::vector<Tool> &tools, ImageDeps deps)
{
  addMeshExportTool(tools, deps);
  addMeshImportTool(tools, deps);
  Tool tool;
  tool.name = "vsr_image_export";
  tool.description =
      "Export an existing Image Array as a lossless raw-sample PNG working copy "
      "in the private launch image_exchange_directory. Requires Scene READ, "
      "not EDIT. Poll image_export status for file_name and captured data_version. "
      "No gamma conversion, channel expansion or live texel mutation. A replay "
      "returns the original receipt, not proof of current file bytes. Files "
      "have no TTL; remove unneeded PNGs explicitly. PNG cleanup and undo "
      "retention are separate.";
  if (!deps.images->importAvailable())
    tool.description += " Image Import is unavailable on this host.";
  tool.inputSchemaJson = std::string("{\"type\":\"object\",\"properties\":{")
      + requestIdSchema()
      + ",\"scene_id\":{\"type\":\"integer\",\"minimum\":0},"
        "\"handle\":{\"type\":\"string\"}},"
        "\"required\":[\"request_id\",\"scene_id\",\"handle\"],"
        "\"additionalProperties\":false}";
  tool.handler = [deps](
                     const session::Credentials &c, const json::Value &args) {
    const auto id = requestIdArg(args.get("request_id"));
    const auto scene = targetIdArg(args, "scene_id");
    const auto handle =
        detail::handleArg(args.get("handle"), deps.identities->launch());
    for (const auto &argument : args.o)
      if (argument.first != "request_id" && argument.first != "scene_id"
          && argument.first != "handle")
        return rejected(operations::Outcome::INVALID_REQUEST);
    if (!id || !scene || !handle)
      return rejected(operations::Outcome::INVALID_REQUEST);
    const operations::OperationRef ref{c.sessionId, *id};
    const auto admission = deps.images->submitExport(c, ref, *scene, *handle);
    if (admission.code != operations::Outcome::OK)
      return rejected(admission.code);
    return statusResult(ref, admission.status, admission.replayed);
  };
  tools.push_back(std::move(tool));
  if (!deps.images->importAvailable())
    return;
  Tool import;
  import.name = "vsr_image_import";
  import.description =
      "Replace an existing shared Image Array in place with raw PNG samples "
      "from a confined launch file_name. Requires Scene READ+EDIT and agent "
      "edits enabled. Supply the inspected equality-only data_version. Shape, "
      "8-bit channel count and straight alpha must match; no conversion, "
      "resizing or prior-export provenance. Poll image_import status for fresh "
      "data_version, scene_revision and normal batch_ref for vsr_restore. "
      "Recovery is conditional and capacity-based, not permanent. PNG removal "
      "does not release the undo snapshot; restore/eviction does not remove PNGs.";
  import.inputSchemaJson = std::string("{\"type\":\"object\",\"properties\":{")
      + requestIdSchema()
      + ",\"scene_id\":{\"type\":\"integer\",\"minimum\":0},"
        "\"handle\":{\"type\":\"string\"},\"data_version\":{\"type\":\"string\"},"
        "\"file_name\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":128}},"
        "\"required\":[\"request_id\",\"scene_id\",\"handle\",\"data_version\",\"file_name\"],"
        "\"additionalProperties\":false}";
  import.handler = [deps](
                       const session::Credentials &c, const json::Value &args) {
    const auto id = requestIdArg(args.get("request_id"));
    const auto scene = targetIdArg(args, "scene_id");
    const auto handle =
        detail::handleArg(args.get("handle"), deps.identities->launch());
    const auto *version = args.get("data_version"),
               *file = args.get("file_name");
    for (const auto &argument : args.o)
      if (argument.first != "request_id" && argument.first != "scene_id"
          && argument.first != "handle" && argument.first != "data_version"
          && argument.first != "file_name")
        return rejected(operations::Outcome::INVALID_REQUEST);
    if (!id || !scene || !handle || !version || !version->isString() || !file
        || !file->isString())
      return rejected(operations::Outcome::INVALID_REQUEST);
    const operations::OperationRef ref{c.sessionId, *id};
    return detail::ack(ref,
        deps.images->submitImport(
            c, ref, *scene, *handle, version->s, file->s));
  };
  tools.push_back(std::move(import));
}

} // namespace vsr::integration::mcp::tools

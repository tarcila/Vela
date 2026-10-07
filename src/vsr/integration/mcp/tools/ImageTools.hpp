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

inline void addImageTools(std::vector<Tool> &tools, ImageDeps deps)
{
  Tool tool;
  tool.name = "vsr_image_export";
  tool.description =
      "Export an existing Image Array as a lossless raw-sample PNG working copy "
      "in the private launch image_exchange_directory. Requires Scene READ, "
      "not EDIT. Poll image_export status for file_name and captured data_version. "
      "No gamma conversion, channel expansion or live texel mutation. A replay "
      "returns the original receipt, not proof of current file bytes. Files "
      "have no TTL; remove unneeded PNGs explicitly. PNG cleanup and undo "
      "retention are separate. Image Import is not implemented.";
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
}

} // namespace vsr::integration::mcp::tools

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/content/ContentCreation.hpp"
#include "vsr/integration/mcp/tools/EditTools.hpp"

namespace vsr::integration::mcp::tools {

inline void addContentTools(std::vector<Tool> &tools,
    identity::IdentityAuthority &identities,
    content::ContentCreation &creation,
    operations::OperationEngine &engine)
{
  Tool options;
  options.name = "vsr_content_options";
  options.description =
      "Discover safely implemented Content Creation source profile, effective defaults, finite limits and host-configured asset root IDs. Radiance RGBE -Y +X HDRI only (flat or modern scanline RLE); no Surface, Volume or Asset Import capability. Roots are configured by the host, never granted by an agent. Requires Scene READ. Poll content_options status.";
  options.inputSchemaJson = std::string("{\"type\":\"object\",\"properties\":{")
      + requestIdSchema()
      + ",\"scene_id\":{\"type\":\"integer\",\"minimum\":0}},\"required\":[\"request_id\",\"scene_id\"],\"additionalProperties\":false}";
  options.handler = [&creation, &engine](const session::Credentials &c,
                        const json::Value &args) {
    const auto id = requestIdArg(args.get("request_id"));
    const auto scene = targetIdArg(args, "scene_id");
    if (!id || !scene || args.o.size() != 2)
      return rejected(operations::Outcome::INVALID_REQUEST);
    operations::Request request;
    request.kind = "content_options";
    request.target = session::Target::scene(*scene);
    request.canonicalArguments = std::to_string(*scene);
    request.executor = [&creation](operations::StepContext &) {
      operations::Step result;
      result.result = creation.metadata();
      return result;
    };
    const operations::OperationRef ref{c.sessionId, *id};
    return detail::ack(ref, engine.submit(c, ref, std::move(request)));
  };
  tools.push_back(std::move(options));
  Tool tool;
  tool.name = "vsr_create_hdri";
  tool.description =
      "Create one new HDRI Light and FLOAT32 radiance under a named Content Root from a confined host-approved Radiance RGBE -Y +X file (flat or modern scanline RLE). Requires Scene READ+EDIT and enabled agent edits. No existing Light, camera or data is replaced. Destination defaults to the default Layer; select Layer/parent handles or an explicitly new Layer. transform is 16 column-major affine numbers. Poll create_hdri for committed, primary handles, effective_settings and normal batch_ref. Restore conditionally removes unchanged Owned Content; edited data/root/parameters, children/reparenting/outside users conflict without writes. Shared 4 GiB soft/8 GiB single-entry FIFO policy; above-hard metadata charge requires the existing trusted one-operation Activity approval and a fresh request_id with approval_request_id. No agent root grants, network input, EXR or 8-bit substitution. Cancellation is cooperative and held cleanup remains visible. Call vsr_content_options for exact profile/bounds.";
  tool.inputSchemaJson = std::string("{\"type\":\"object\",\"properties\":{") + requestIdSchema()
      + ",\"scene_id\":{\"type\":\"integer\",\"minimum\":0},\"asset_root\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":15},"
        "\"file_name\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":4096},\"root_name\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":256},"
        "\"new_layer\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":256},\"layer\":{\"type\":\"string\"},\"parent\":{\"type\":\"string\"},"
        "\"scale\":{\"type\":\"number\",\"minimum\":0,\"default\":1},\"up\":{\"type\":\"array\",\"minItems\":3,\"maxItems\":3,\"items\":{\"type\":\"number\"},\"default\":[0,1,0]},"
        "\"direction\":{\"type\":\"array\",\"minItems\":3,\"maxItems\":3,\"items\":{\"type\":\"number\"},\"default\":[1,0,0]},\"transform\":{\"type\":\"array\",\"minItems\":16,\"maxItems\":16,\"items\":{\"type\":\"number\"}},"
        "\"approval_request_id\":{\"type\":\"integer\",\"minimum\":1}},\"required\":[\"request_id\",\"scene_id\",\"asset_root\",\"file_name\"],\"additionalProperties\":false}";
  tool.handler = [&creation, &identities](
                     const session::Credentials &c, const json::Value &args) {
    const auto id = requestIdArg(args.get("request_id"));
    const auto scene = targetIdArg(args, "scene_id");
    const auto root = targetIdArg(args, "asset_root");
    const auto *file = args.get("file_name");
    if (!id || !scene || !root || *root >= 16 || !file || !file->isString())
      return rejected(operations::Outcome::INVALID_REQUEST);
    for (const auto &arg : args.o)
      if (arg.first != "request_id" && arg.first != "scene_id"
          && arg.first != "asset_root" && arg.first != "file_name"
          && arg.first != "root_name" && arg.first != "new_layer"
          && arg.first != "layer" && arg.first != "parent"
          && arg.first != "scale" && arg.first != "up"
          && arg.first != "direction" && arg.first != "transform"
          && arg.first != "approval_request_id")
        return rejected(operations::Outcome::INVALID_REQUEST);
    content::HdriRequest request;
    request.scene = *scene;
    request.assetRoot = *root;
    request.fileName = file->s;
    for (const auto *key : {"root_name", "new_layer"})
      if (const auto *value = args.get(key)) {
        if (!value->isString() || value->s.empty())
          return rejected(operations::Outcome::INVALID_REQUEST);
        (std::string_view(key) == "root_name" ? request.rootName
                                              : request.newLayer) = value->s;
      }
    if (const auto *value = args.get("layer")) {
      const auto h = detail::handleArg(value, identities.launch());
      if (!h)
        return rejected(operations::Outcome::INVALID_REQUEST);
      request.layer = identity::LayerHandle{h->launch, h->domain, h->serial};
    }
    if (const auto *value = args.get("parent")) {
      const auto h = detail::handleArg(value, identities.launch());
      if (!h)
        return rejected(operations::Outcome::INVALID_REQUEST);
      request.parent = identity::NodeHandle{h->launch, h->domain, h->serial};
    }
    const auto finiteFloat = [](const json::Value &value, float &out) {
      if (!value.isNumber() || !std::isfinite(value.n)
          || std::abs(value.n) > std::numeric_limits<float>::max())
        return false;
      out = float(value.n);
      return true;
    };
    if (const auto *value = args.get("scale"))
      if (!finiteFloat(*value, request.settings.scale)
          || request.settings.scale < 0)
        return rejected(operations::Outcome::INVALID_REQUEST);
    for (const auto *key : {"up", "direction"})
      if (const auto *value = args.get(key)) {
        auto &v = std::string_view(key) == "up" ? request.settings.up
                                                : request.settings.direction;
        if (value->type != json::Value::Type::ARRAY || value->a.size() != 3)
          return rejected(operations::Outcome::INVALID_REQUEST);
        for (size_t i = 0; i < 3; ++i)
          if (!finiteFloat(value->a[i], v[i]))
            return rejected(operations::Outcome::INVALID_REQUEST);
      }
    if (const auto *value = args.get("transform")) {
      if (value->type != json::Value::Type::ARRAY || value->a.size() != 16)
        return rejected(operations::Outcome::INVALID_REQUEST);
      for (size_t i = 0; i < 16; ++i)
        if (!finiteFloat(value->a[i], request.settings.transform[i / 4][i % 4]))
          return rejected(operations::Outcome::INVALID_REQUEST);
    }
    if (const auto *value = args.get("approval_request_id")) {
      request.approvalRequestId = requestIdArg(value);
      if (!request.approvalRequestId)
        return rejected(operations::Outcome::INVALID_REQUEST);
    }
    const operations::OperationRef ref{c.sessionId, *id};
    return detail::ack(ref, creation.submit(c, ref, std::move(request)));
  };
  tools.push_back(std::move(tool));
}

} // namespace vsr::integration::mcp::tools

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/frame/FrameObservation.hpp"
#include "vsr/integration/mcp/tools/RenderTools.hpp"

namespace vsr::integration::mcp::tools {

struct FrameDeps
{
  FrameDeps(frame::FrameService &frames, operations::OperationEngine &engine);
  frame::FrameService *frames{nullptr};
  operations::OperationEngine *engine{nullptr};
};

inline FrameDeps::FrameDeps(
    frame::FrameService &service, operations::OperationEngine &operations)
    : frames(&service), engine(&operations)
{}

inline void addFrameTools(std::vector<Tool> &tools, FrameDeps deps)
{
  Tool observe;
  observe.name = "vsr_view_frame";
  observe.description =
      "Observe the explicitly named view's most recently successfully presented "
      "pipeline frame at its owning-thread observation point, never a new "
      "render. The SAME first operation stays pending during cold preparation "
      "until natural presentation, within its original deadline. Preparation "
      "is not observation: latest is pinned once after readiness, not at "
      "admission or delivery. Unavailable does not imply empty geometry. "
      "Rendered and current revisions are separate; stale displayed images are "
      "historical. Only explicitly exposed qualified sources support images. "
      "Example: {request_id:1,view_id:0}. Fetch via vsr_view_frame_image; replay "
      "keeps the same historical frame. No UI, HUD, gizmos or desktop pixels.";
  observe.inputSchemaJson = std::string("{\"type\":\"object\",\"properties\":{")
      + requestIdSchema() + ",\"view_id\":{\"type\":\"integer\",\"minimum\":0}},"
        "\"required\":[\"request_id\",\"view_id\"]}";
  observe.handler = [deps](const session::Credentials &c,
                        const json::Value &args) {
    const auto id = requestIdArg(args.get("request_id"));
    const auto view = targetIdArg(args, "view_id");
    if (!id || !view || args.get("width") || args.get("height"))
      return rejected(Outcome::INVALID_REQUEST);
    const operations::OperationRef ref{c.sessionId, *id};
    const auto admitted = deps.frames->submit(c, ref, *view);
    if (admitted.code != Outcome::OK)
      return rejected(admitted.code);
    auto result = statusResult(ref, admitted.status, admitted.replayed);
    // Pending/minimal acknowledgements have no protected result-copy lease.
    if (!result.resourceLease)
      result.resourceLease = deps.frames->reserveDelivery(16384);
    if (!result.resourceLease)
      return rejected(Outcome::BUSY);
    result.maxResponseBytes = 12582912;
    return result;
  };
  tools.push_back(std::move(observe));

  Tool image;
  image.name = "vsr_view_frame_image";
  image.description =
      "Retrieve a retained request-bound presented frame as image/png content. "
      "Requires current Scene and view read rights even for historical refs. "
      "Never accepts legacy render refs as frame images, never renders or saves "
      "files. Image retention is finite; expired/missing images are unavailable.";
  image.inputSchemaJson =
      "{\"type\":\"object\",\"properties\":{\"operation_ref\":{"
      "\"type\":\"object\",\"properties\":{\"session_id\":{\"type\":\"string\"},"
      "\"request_id\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":9007199254740991}},"
      "\"required\":[\"session_id\",\"request_id\"]}},\"required\":[\"operation_ref\"]}";
  image.handler = [deps](
                      const session::Credentials &c, const json::Value &args) {
    const auto ref = operationRefArg(args);
    if (!ref)
      return rejected(Outcome::INVALID_REQUEST);
    auto metadataLease = deps.frames->reserveDelivery(
        3 * (inspection::viewinfo::MAX_BINDING_BYTES + 4096) + 4096);
    if (!metadataLease)
      return rejected(Outcome::BUSY);
    const auto status = deps.engine->status(c, *ref);
    if (status.code != Outcome::OK)
      return rejected(status.code);
    if (status.kind != "view_frame")
      return rejected(Outcome::INVALID_REQUEST);
    auto result = statusResult(*ref, status);
    result.resourceLease = metadataLease;
    result.maxResponseBytes = 12582912;
    if (!status.resultVisible || status.terminal != Outcome::OK)
      return result;
    auto frame = deps.frames->result(c, *ref);
    if (!frame) {
      result.contentJson.pop_back();
      result.contentJson +=
          ",{\"type\":\"text\",\"text\":\"frame_image_unavailable_or_expired\"}]";
      return result;
    }
    const auto base64Bytes = (frame->png.size() + 2) / 3 * 4;
    const auto completeBound = base64Bytes + result.contentJson.size()
        + result.structuredJson.size() + 4096;
    auto lease = deps.frames->reserveDelivery(completeBound);
    if (!lease)
      return rejected(Outcome::BUSY);
    result.resourceLease = std::move(lease);
    result.contentJson.pop_back();
    result.contentJson +=
        ",{\"type\":\"image\",\"mimeType\":\"image/png\",\"data\":\""
        + detail::base64(frame->png) + "\"}]";
    // Publication repeats current visibility after encoding/serialization.
    const auto current = deps.engine->status(c, *ref);
    if (!current.resultVisible)
      return statusResult(*ref, current);
    return result;
  };
  tools.push_back(std::move(image));
}

} // namespace vsr::integration::mcp::tools

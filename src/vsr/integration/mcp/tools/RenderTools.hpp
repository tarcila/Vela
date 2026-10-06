// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Header-only: the vsr_integration library globs only top-level sources (see
// ticket 19).

#include "vsr/integration/mcp/tools/ToolSupport.hpp"
#include "vsr/integration/render/RevisionRender.hpp"
#include "vsr/integration/sync/SceneSync.hpp"
// std
#include <vector>

namespace vsr::integration::mcp::tools {

/*
 * vsr_render, vsr_render_image and vsr_resynchronize (spec §10, §12).
 *
 * vsr_render: {request_id, view_id, scene_revision, view_revision}. The
 * revision tokens are the "activation.nonce.domain.value" strings a render
 * result carries. Dimensions are view-owned: a request naming `width` or
 * `height` is an envelope error. Returns the admission acknowledgement; an
 * identical retry replays the record and, once it is terminal ok and
 * readable, also carries the image.
 *
 * vsr_render_image: {operation_ref}. The status object plus, for a readable
 * completed render whose image is still retained, one MCP image content item
 * (`image/png`, base64). The image is never a file path.
 *
 * vsr_select_device: {request_id, view_id, device_identifier?}. Agent
 * override of a view's render device among the backend's candidates (see
 * RenderService::submitSelectDevice).
 *
 * vsr_set_view_camera: {request_id, view_id, position, direction, up,
 * fovy_deg?}. Agent pose of an independent view's own camera (see
 * RenderService::submitSetCamera).
 *
 * vsr_resynchronize: {request_id, scene_id}. The terminal result is the
 * headless {"ready": bool, "state": "<sync state>"} (ticket 67).
 */

struct RenderDeps
{
  render::RenderService &renders;
  sync::SyncService &sync;
  operations::OperationEngine &engine;
};

namespace detail {

inline std::string base64(const std::vector<std::uint8_t> &in)
{
  static const char *t =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((in.size() + 2) / 3 * 4);
  std::size_t i = 0;
  for (; i + 2 < in.size(); i += 3) {
    const std::uint32_t v = (std::uint32_t(in[i]) << 16)
        | (std::uint32_t(in[i + 1]) << 8) | in[i + 2];
    out += t[(v >> 18) & 63];
    out += t[(v >> 12) & 63];
    out += t[(v >> 6) & 63];
    out += t[v & 63];
  }
  if (i < in.size()) {
    std::uint32_t v = std::uint32_t(in[i]) << 16;
    if (i + 1 < in.size())
      v |= std::uint32_t(in[i + 1]) << 8;
    out += t[(v >> 18) & 63];
    out += t[(v >> 12) & 63];
    out += i + 1 < in.size() ? t[(v >> 6) & 63] : '=';
    out += '=';
  }
  return out;
}

// "activation.nonce.domain.value", four decimal unsigned 64-bit fields.
template <typename R>
inline std::optional<R> revisionArg(const json::Value *v)
{
  if (!v || !v->isString())
    return std::nullopt;
  std::uint64_t f[4] = {0, 0, 0, 0};
  std::size_t k = 0, digits = 0;
  for (char c : v->s) {
    if (c == '.') {
      if (digits == 0 || ++k > 3)
        return std::nullopt;
      digits = 0;
      continue;
    }
    if (c < '0' || c > '9' || ++digits > 20)
      return std::nullopt;
    const std::uint64_t d = std::uint64_t(c - '0');
    if (f[k] > (~std::uint64_t(0) - d) / 10)
      return std::nullopt;
    f[k] = f[k] * 10 + d;
  }
  if (k != 3 || digits == 0)
    return std::nullopt;
  R r;
  r.launch.activation = f[0];
  r.launch.nonce = f[1];
  r.domain = f[2];
  r.value = f[3];
  return r;
}

// Adds the retained PNG to `r` when the record is a readable, completed
// render whose image survived the retained-image budget.
inline void attachImage(ToolResult &r,
    const render::RenderService &renders,
    const operations::OperationRef &ref,
    const operations::Status &s)
{
  if (s.kind != "render" || !s.terminal || *s.terminal != Outcome::OK
      || !s.resultVisible)
    return;
  const auto res = renders.result(ref);
  if (!res || res->outcome != Outcome::OK || res->png.empty()
      || res->imageEvicted)
    return;
  r.contentJson.pop_back(); // "]"
  r.contentJson += ",{\"type\":\"image\",\"mimeType\":\"image/png\",\"data\":\""
      + base64(res->png) + "\"}]";
}

} // namespace detail

inline void addRenderTools(std::vector<Tool> &tools, RenderDeps deps)
{
  Tool renderTool;
  renderTool.name = "vsr_render";
  renderTool.description =
      "Render a view pinned to scene/view revision tokens at the view's own "
      "dimensions; the image is returned by vsr_render_image. This legacy "
      "independent render is not observation of a presented viewport frame. "
      "The receipt preserves source role/origin and image-producing configuration "
      "only when the backend explicitly attests it; otherwise it is unknown.";
  renderTool.inputSchemaJson =
      std::string("{\"type\":\"object\",\"properties\":{") + requestIdSchema()
      + ",\"view_id\":{\"type\":\"integer\",\"minimum\":0},"
        "\"scene_revision\":{\"type\":\"string\"},"
        "\"view_revision\":{\"type\":\"string\"}},"
        "\"required\":[\"request_id\",\"view_id\",\"scene_revision\","
        "\"view_revision\"]}";
  renderTool.handler = [deps](const session::Credentials &c,
                           const json::Value &args) {
    const auto id = requestIdArg(args.get("request_id"));
    const auto view = targetIdArg(args, "view_id");
    const auto sr = detail::revisionArg<identity::SceneRevision>(
        args.get("scene_revision"));
    const auto vr =
        detail::revisionArg<identity::ViewRevision>(args.get("view_revision"));
    // View-owned dimensions: no per-request override (spec §12).
    if (!id || !view || !sr || !vr || args.get("width") || args.get("height"))
      return rejected(Outcome::INVALID_REQUEST);
    const operations::OperationRef ref{c.sessionId, *id};
    const auto a = deps.renders.submit(c, ref, {*view, *sr, *vr});
    if (a.code != Outcome::OK)
      return rejected(a.code);
    auto r = statusResult(ref, a.status, a.replayed);
    detail::attachImage(r, deps.renders, ref, a.status);
    return r;
  };
  tools.push_back(std::move(renderTool));

  Tool imageTool;
  imageTool.name = "vsr_render_image";
  imageTool.description =
      "Status of a render operation_ref and, once completed and readable, its "
      "PNG image content.";
  imageTool.inputSchemaJson =
      "{\"type\":\"object\",\"properties\":{\"operation_ref\":{"
      "\"type\":\"object\",\"properties\":{"
      "\"session_id\":{\"type\":\"string\"},"
      "\"request_id\":{\"type\":\"integer\",\"minimum\":1,"
      "\"maximum\":9007199254740991}},"
      "\"required\":[\"session_id\",\"request_id\"]}},"
      "\"required\":[\"operation_ref\"]}";
  imageTool.handler = [deps](const session::Credentials &c,
                          const json::Value &args) {
    const auto ref = operationRefArg(args);
    if (!ref)
      return rejected(Outcome::INVALID_REQUEST);
    const auto s = deps.engine.status(c, *ref);
    if (s.code != Outcome::OK)
      return rejected(s.code);
    auto r = statusResult(*ref, s);
    detail::attachImage(r, deps.renders, *ref, s);
    return r;
  };
  tools.push_back(std::move(imageTool));

  Tool selectTool;
  selectTool.name = "vsr_select_device";
  selectTool.description =
      "Override the render device of a render-capable view. device_identifier "
      "must be one of the view's host-listed candidates ([subtype@]library); "
      "omit it to only report the current device and candidates. A switch "
      "advances the view revision, so re-read it before vsr_render. The "
      "terminal result is {applied, device_identifier, "
      "previous_device_identifier, candidates}; a failed switch is ok with "
      "applied:false and keeps the previous device.";
  selectTool.inputSchemaJson =
      std::string("{\"type\":\"object\",\"properties\":{") + requestIdSchema()
      + ",\"view_id\":{\"type\":\"integer\",\"minimum\":0},"
        "\"device_identifier\":{\"type\":\"string\",\"minLength\":1}},"
        "\"required\":[\"request_id\",\"view_id\"]}";
  selectTool.handler = [deps](const session::Credentials &c,
                           const json::Value &args) {
    const auto id = requestIdArg(args.get("request_id"));
    const auto view = targetIdArg(args, "view_id");
    std::string device;
    if (!id || !view || !optionalStringArg(args, "device_identifier", device)
        || (args.get("device_identifier") && device.empty()))
      return rejected(Outcome::INVALID_REQUEST);
    const operations::OperationRef ref{c.sessionId, *id};
    const auto a = deps.renders.submitSelectDevice(c, ref, {*view, device});
    if (a.code != Outcome::OK)
      return rejected(a.code);
    return statusResult(ref, a.status, a.replayed);
  };
  tools.push_back(std::move(selectTool));

  Tool cameraTool;
  cameraTool.name = "vsr_set_view_camera";
  cameraTool.description =
      "Pose exactly the addressed view: an independent camera needs RENDER "
      "and READ; a supported interactive viewport needs view and Scene READ "
      "and Allow agent camera control (off each launch), not EDIT or RENDER. "
      "Interactive moves replace that view's Before agent recovery slot. "
      "Use world-space position, "
      "direction (forward) and up, with optional vertical fovy_deg in (0,180). "
      "direction/up need not be normalized but must be non-zero and not "
      "parallel. Advances the view revision, so re-read it (vsr_view_info) "
      "before vsr_render. Views without camera control reject "
      "invalid_request/unsupported_view_camera. Disabled viewport control "
      "rejects camera_control_disabled. Receipts describe effective state, "
      "not presentation; verify via the addressed viewport's presented frame.";
  const std::string vec3 =
      "{\"type\":\"array\",\"items\":{\"type\":\"number\"},"
      "\"minItems\":3,\"maxItems\":3}";
  cameraTool.inputSchemaJson =
      std::string("{\"type\":\"object\",\"properties\":{") + requestIdSchema()
      + ",\"view_id\":{\"type\":\"integer\",\"minimum\":0},"
        "\"position\":"
      + vec3 + ",\"direction\":" + vec3 + ",\"up\":" + vec3
      + ",\"fovy_deg\":{\"type\":\"number\",\"exclusiveMinimum\":0,"
        "\"exclusiveMaximum\":180}},"
        "\"required\":[\"request_id\",\"view_id\",\"position\","
        "\"direction\",\"up\"],\"additionalProperties\":false}";
  cameraTool.handler = [deps](const session::Credentials &c,
                           const json::Value &args) {
    const auto id = requestIdArg(args.get("request_id"));
    const auto view = targetIdArg(args, "view_id");
    for (const auto &argument : args.o)
      if (argument.first != "request_id" && argument.first != "view_id"
          && argument.first != "position" && argument.first != "direction"
          && argument.first != "up" && argument.first != "fovy_deg")
        return rejected(Outcome::INVALID_REQUEST);
    auto vecArg = [&](const char *key, std::array<double, 3> &out) {
      const auto *v = args.get(key);
      if (!v || v->type != json::Value::Type::ARRAY || v->a.size() != 3)
        return false;
      for (int i = 0; i < 3; ++i) {
        if (!v->a[i].isNumber())
          return false;
        out[i] = v->a[i].n;
      }
      return true;
    };
    render::CameraRequest request;
    if (!id || !view || !vecArg("position", request.position)
        || !vecArg("direction", request.direction) || !vecArg("up", request.up))
      return rejected(Outcome::INVALID_REQUEST);
    if (const auto *f = args.get("fovy_deg")) {
      if (!f->isNumber() || !(f->n > 0.) || !(f->n < 180.))
        return rejected(Outcome::INVALID_REQUEST);
      request.fovyDegrees = f->n;
    }
    request.view = *view;
    const operations::OperationRef ref{c.sessionId, *id};
    const auto a = deps.renders.submitSetCamera(c, ref, request);
    if (a.code != Outcome::OK) {
      auto result = rejected(a.code);
      if (a.code == Outcome::INVALID_REQUEST
          && render::validCameraPose(request))
        result.semanticReason = "unsupported_view_camera";
      return result;
    }
    return statusResult(ref, a.status, a.replayed);
  };
  tools.push_back(std::move(cameraTool));

  Tool resync;
  resync.name = "vsr_resynchronize";
  resync.description =
      "Rebuild one scene's downstream rendering state; the result reports "
      "ready and the scene's sync state.";
  resync.inputSchemaJson =
      std::string("{\"type\":\"object\",\"properties\":{") + requestIdSchema()
      + ",\"scene_id\":{\"type\":\"integer\",\"minimum\":0}},"
        "\"required\":[\"request_id\",\"scene_id\"]}";
  resync.handler = [deps](
                       const session::Credentials &c, const json::Value &args) {
    const auto id = requestIdArg(args.get("request_id"));
    const auto scene = targetIdArg(args, "scene_id");
    if (!id || !scene)
      return rejected(Outcome::INVALID_REQUEST);
    const operations::OperationRef ref{c.sessionId, *id};
    const auto a = deps.sync.submitResynchronize(c, ref, *scene);
    if (a.code != Outcome::OK)
      return rejected(a.code);
    return statusResult(ref, a.status, a.replayed);
  };
  tools.push_back(std::move(resync));
}

} // namespace vsr::integration::mcp::tools

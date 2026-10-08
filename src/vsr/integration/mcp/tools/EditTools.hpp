// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Header-only: the vsr_integration library globs only top-level sources (see
// ticket 19).

#include "vsr/integration/edit/Translation.hpp"
#include "vsr/integration/mcp/tools/ToolSupport.hpp"
#include "vsr/integration/restore/BatchRestore.hpp"
// std
#include <vector>

namespace vsr::integration::mcp::tools {

/*
 * vsr_edit and vsr_restore: scene mutations admitted as one operation per
 * call (spec §10, §11). Both return the admission acknowledgement; the
 * terminal status (poll vsr_operation_status) carries `committed` and, on
 * commit, `result.batch_ref` (edit) or `result.target_batch_ref` (restore).
 *
 * Wire shapes:
 *   edit:    {request_id, scene_id, edits: [{handle, version, parameter,
 *            value}]}; `handle` and `version` are copied from a vsr_inspect
 *            item (handle = 32 hex: domain then serial, current launch).
 *   restore: {request_id, scene_id, target_batch_ref: {launch_id, batch_id}}
 *
 * The headless services own every rule; this layer only parses arguments.
 */

struct EditDeps
{
  identity::IdentityAuthority &ids; // launch stamp for wire handles
  edit::EditService &edits;
  restore::RestoreService &restores;
  edit::TranslationService *translations{nullptr}; // explicitly qualified host
};

namespace detail {

inline int hexDigit(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}

// 32 lowercase hex: domain (16) then serial (16), as vsr_inspect emits.
inline std::optional<identity::ObjectHandle> handleArg(
    const json::Value *v, const identity::LaunchStamp &launch)
{
  if (!v || !v->isString() || v->s.size() != 32)
    return std::nullopt;
  std::uint64_t parts[2] = {0, 0};
  for (std::size_t i = 0; i < 32; i++) {
    const int d = hexDigit(v->s[i]);
    if (d < 0)
      return std::nullopt;
    parts[i / 16] = (parts[i / 16] << 4) | std::uint64_t(d);
  }
  identity::ObjectHandle h;
  h.launch = launch;
  h.domain = parts[0];
  h.serial = parts[1];
  return h;
}

// Carries every JSON kind with its full content so request canonicalization
// sees the whole value; the edit surface rejects non-number kinds with its own
// ValueError.
inline JsonValue editValue(const json::Value &v, int depth = 0)
{
  using T = json::Value::Type;
  if (depth > 4 && (v.type == T::ARRAY || v.type == T::OBJECT))
    return JsonValue::ofKind(JsonKind::MALFORMED);
  switch (v.type) {
  case T::NUMBER:
    return JsonValue::fromNumber(v.n);
  case T::ARRAY: {
    std::vector<JsonValue> e;
    for (const auto &x : v.a)
      e.push_back(editValue(x, depth + 1));
    return JsonValue::array(std::move(e));
  }
  case T::BOOL: {
    auto j = JsonValue::ofKind(JsonKind::BOOLEAN);
    j.boolean = v.b;
    return j;
  }
  case T::STRING: {
    auto j = JsonValue::ofKind(JsonKind::STRING);
    j.text = v.s;
    return j;
  }
  case T::NUL:
    return JsonValue::ofKind(JsonKind::NULL_VALUE);
  case T::OBJECT: {
    auto j = JsonValue::ofKind(JsonKind::OBJECT);
    for (const auto &[k, c] : v.o) // std::map: sorted, unique keys
      j.members.emplace_back(k, editValue(c, depth + 1));
    return j;
  }
  }
  return JsonValue::ofKind(JsonKind::MALFORMED);
}

inline std::optional<std::uint64_t> tokenNumber(const std::string &s)
{
  if (s.empty() || s.size() > 20 || (s.size() > 1 && s[0] == '0'))
    return {};
  std::uint64_t n = 0;
  for (char c : s) {
    if (c < '0' || c > '9' || n > (UINT64_MAX - (c - '0')) / 10)
      return {};
    n = n * 10 + (c - '0');
  }
  return n ? std::optional<std::uint64_t>(n) : std::nullopt;
}

inline std::optional<edit::EditBatch> editBatchArg(
    const json::Value &args, const identity::LaunchStamp &launch)
{
  const auto scene = targetIdArg(args, "scene_id");
  const auto *list = args.get("edits");
  if (!scene || !list || list->type != json::Value::Type::ARRAY)
    return std::nullopt;
  edit::EditBatch batch;
  batch.scene = *scene;
  for (const auto &e : list->a) {
    std::optional<std::int64_t> version;
    const auto handle = handleArg(e.get("handle"), launch);
    const auto *param = e.get("parameter");
    const auto *value = e.get("value");
    if (!e.isObject() || !handle || !param || !param->isString() || !value)
      return std::nullopt;
    edit::EditEntry entry;
    const auto *kind = e.get("target_kind");
    if (kind
        && (!kind->isString() || (kind->s != "node" && kind->s != "object")))
      return {};
    if (kind && kind->s == "node") {
      const auto *v = e.get("version");
      const auto *d = e.get("structural_version");
      const auto prefix = "ne:" + e.get("handle")->s + ":";
      const auto dp = "ns:" + e.get("handle")->s + ":";
      if (!v || !v->isString() || v->s.compare(0, prefix.size(), prefix) || !d
          || !d->isString() || d->s.compare(0, dp.size(), dp)
          || d->s.size() < dp.size() + 34 || d->s[dp.size() + 32] != ':')
        return {};
      auto nv = tokenNumber(v->s.substr(prefix.size()));
      auto ds = tokenNumber(d->s.substr(dp.size() + 33));
      json::Value lv;
      lv.type = json::Value::Type::STRING;
      lv.s = d->s.substr(dp.size(), 32);
      auto lh = handleArg(&lv, launch);
      if (!nv || !ds || !lh)
        return {};
      const auto *space = e.get("space");
      if (space) {
        if (param->s != "position" || !space->isString()
            || (space->s != "local" && space->s != "world"))
          return {};
        entry.space = space->s;
      }
      entry.targetKind = edit::TargetKind::NODE;
      entry.nodeExpected = {{launch, handle->domain, handle->serial}, *nv};
      entry.nodeDependencies = {
          entry.nodeExpected.node, {launch, lh->domain, lh->serial}, 0, *ds, 0};
      if (entry.space == "world") {
        const auto *ancestry = e.get("ancestry_version");
        const auto ap = "na:" + e.get("handle")->s + ":" + lv.s + ":"
            + std::to_string(*ds) + ":";
        if (!ancestry || !ancestry->isString()
            || ancestry->s.compare(0, ap.size(), ap))
          return {};
        const auto tail = ancestry->s.substr(ap.size());
        auto av =
            tail == "0" ? std::optional<std::uint64_t>(0) : tokenNumber(tail);
        if (!av)
          return {};
        entry.nodeDependencies.transformVersion = *av;
        entry.hasAncestryVersion = true;
      }
    } else {
      if (e.get("space"))
        return {}; // space is node positioning policy, never object value data
      if (!optionalIntArg(e, "version", version) || !version || *version < 0)
        return {};
      entry.expected.object = *handle;
      entry.expected.version = std::uint64_t(*version);
    }
    entry.parameter = param->s;
    entry.value = editValue(*value);
    batch.edits.push_back(std::move(entry));
  }
  return batch;
}

inline std::optional<restore::RestoreRequest> restoreArg(
    const json::Value &args)
{
  const auto scene = targetIdArg(args, "scene_id");
  const auto *t = args.get("target_batch_ref");
  if (!scene || !t || !t->isObject())
    return std::nullopt;
  for (const auto &argument : args.o)
    if (argument.first != "request_id" && argument.first != "scene_id"
        && argument.first != "target_batch_ref")
      return std::nullopt;
  for (const auto &argument : t->o)
    if (argument.first != "launch_id" && argument.first != "batch_id")
      return std::nullopt;
  const auto *launch = t->get("launch_id");
  const auto *batch = t->get("batch_id");
  if (!launch || !launch->isString() || !batch || !batch->isString())
    return std::nullopt;
  restore::RestoreRequest r;
  r.scene = *scene;
  r.targetBatchRef = edit::BatchRef{launch->s, batch->s};
  return r;
}

inline ToolResult ack(
    const operations::OperationRef &ref, const operations::Admission &a)
{
  if (a.code != Outcome::OK)
    return rejected(a.code);
  return statusResult(ref, a.status, a.replayed);
}

} // namespace detail

inline void addEditTools(std::vector<Tool> &tools, EditDeps deps)
{
  Tool editTool;
  editTool.name = "vsr_edit";
  editTool.description =
      "Copy node_edit_version into version and structural_version verbatim for "
      "every node entry; world position additionally requires ancestry_version. "
      "Object entries keep integer versions and edit only existing parameters, "
      "validated by stored type and introspected metadata, not a material/light "
      "allowlist. Supported stored types: BOOL as JSON Boolean; STRING without "
      "embedded NUL, enforcing string_values choices when present; signed/unsigned "
      "8-, 16-, 32-, or 64-bit integer scalars and vec2/3/4 as integral JSON "
      "numbers within both type range and +/-(2^53-1); float32/float64 scalars "
      "and vec2/3/4 as finite JSON numbers within type range. Vectors require "
      "exactly the stored component count. Matching min/max metadata applies "
      "per component; COLOR usage retains [0,1]. FLOAT32_MAT2, MAT3, MAT4, "
      "MAT2x3 and MAT3x4 take exact nested row arrays of 2x2, 3x3, 4x4, "
      "2x3 and 3x4 numbers respectively (rows x columns), as inspection emits. "
      "Row r / column c maps to packed index c*R+r. Components validate finite "
      "binary64 input, binary32 range and matching-type finite ordered bounds "
      "before ties-to-even conversion and again afterward. Differently typed "
      "bounds are ignored. Object matrices may be non-affine and accept no "
      "space argument; node transforms retain their affine/SRT rules. Matrices "
      "use ordinary object versions and exact conditional restore, not a new "
      "matrix receipt. Object references, double matrices and all other types "
      "are unsupported; parameters cannot be created. "
      "Inspection parameter entries expose editable Boolean, typed min/max "
      "and string_values when present; editable describes value support, not "
      "authority. Parameter enabled is preserved and does not restrict value "
      "editing. The legacy values mirror covers generic supported float32 "
      "scalar/vec2/vec3/vec4 values, not exclusive edit authority. Readable "
      "parameters are not automatically writable. Stored camera parameter edits "
      "do not control the viewport or agent capture camera. "
      "World position means the node's local origin, requires ancestry_version from inspection, and converts through finite invertible proposed final ancestors; singular parents reject without fallback. "
      "Inheritance example with identity linear transforms: RootDelta local x=0 "
      "and child BodyTop local x=1 have world origins 0 and 1. Moving only the "
      "parent to local x=2 moves both once to world x=2 and x=3, keeping child "
      "local x=1. Also adding +2 to the child's local position moves its origin "
      "twice, to x=5. Selected sibling roots must be edited independently, not "
      "promoted to an unselected common parent. Names are illustrative; inspect "
      "actual ancestry rather than guessing an assembly from names. "
      "Atomically apply up to 64 object parameter or explicitly exposed node enabled/local or world position/local rotation/scale/transform edits to one scene. Position and scale are three finite float32-representable numbers. Rotation is an object with named azimuth/elevation/roll degree components, composed about Y then X then Z as in VSR; supplied angles are retained, not wrapped. Rotation/scale require a faithful current cached SRT reconstruction (per-linear-component tolerance 1e-5 times max(1,abs(component))); no shear repair. Signed and zero scales are supported when faithful. Space defaults to local; no world rotation/scale/matrix edits. Only scalar-transform nodes accept transform fields. Different fields group deterministically with one expected version; repeated fields reject. Transform replaces a scalar node's complete local affine matrix (four rows of four finite numbers, column-vector math, last-column translation, exact last row [0,0,0,1]). Shear, reflection and singular affine matrices are supported. Transform cannot combine with position/rotation/scale on the same node but may combine with Boolean enabled. Node entries require target_kind=node, opaque version and structural_version from inspection. Returns a "
      "batch_ref on commit.";
  editTool.inputSchemaJson = std::string("{\"type\":\"object\",\"properties\":{")
      + requestIdSchema()
      + ",\"scene_id\":{\"type\":\"integer\",\"minimum\":0},"
        "\"edits\":{\"type\":\"array\",\"minItems\":1,\"maxItems\":64,"
        "\"items\":{\"type\":\"object\",\"properties\":{"
        "\"handle\":{\"type\":\"string\"},"
        "\"target_kind\":{\"enum\":[\"object\",\"node\"]},"
        "\"structural_version\":{\"type\":\"string\"},"
        "\"ancestry_version\":{\"type\":\"string\"},"
        "\"space\":{\"enum\":[\"local\",\"world\"]},"
        "\"version\":{\"type\":[\"integer\",\"string\"]},"
        "\"parameter\":{\"type\":\"string\"},\"value\":{\"anyOf\":["
        "{\"type\":\"number\"},{\"type\":\"boolean\"},{\"type\":\"string\"},"
        "{\"type\":\"array\",\"items\":{\"type\":\"number\"}},"
        "{\"type\":\"array\",\"items\":{\"type\":\"array\",\"items\":{\"type\":\"number\"}}},"
        "{\"type\":\"object\",\"properties\":{\"azimuth\":{\"type\":\"number\"},"
        "\"elevation\":{\"type\":\"number\"},\"roll\":{\"type\":\"number\"}},"
        "\"required\":[\"azimuth\",\"elevation\",\"roll\"]}]}},"
        "\"required\":[\"handle\",\"version\",\"parameter\",\"value\"]}}},"
        "\"required\":[\"request_id\",\"scene_id\",\"edits\"]}";
  editTool.handler = [deps](const session::Credentials &c,
                         const json::Value &args) {
    const auto id = requestIdArg(args.get("request_id"));
    auto batch = detail::editBatchArg(args, deps.ids.launch());
    if (!id || !batch)
      return rejected(Outcome::INVALID_REQUEST);
    const operations::OperationRef ref{c.sessionId, *id};
    return detail::ack(ref, deps.edits.submit(c, ref, std::move(*batch)));
  };
  tools.push_back(std::move(editTool));

  Tool restoreTool;
  restoreTool.name = "vsr_restore";
  restoreTool.description =
      "Recovery shares the host's publication-FIFO soft budget (4 GiB launch default) across parameter/Node/Image/Mesh batches, with a separate disclosed 32-batch bound and 16 MiB/1024 Activity metadata bounds. Normal entries replace the oldest complete recovery only on successful publication; failed preparation, no-op and replay do not evict. Physical owners can outlive eligibility. Above-soft recovery at most the effective per-entry hard cap (8 GiB launch default) is retained alone; the next normal change uses soft again. Above-hard recovery refuses with required bytes and effective limits; bounded operator approval is not implemented and no agent flag bypasses refusal. Conditionally restore the exact prior changed state of the batch named by target_batch_ref, not a whole layer. Image batches restore exact prior samples in the same Array and advance data_version; supply no caller data_version. The addressed scene_id must match the retained Image batch's original Scene registration. Later target or required ancestor writes conflict, including change-back; unrelated edits and reset defaults survive. Success consumes eligibility without redo; restore conflicts permanently consume eligibility. Sync failure can still mean committed: inspect committed independently of outcome. Committed receipts contain image_versions (empty for parameter/node batches), ordinary versions and node_versions; there is no matrix_versions field.";
  restoreTool.inputSchemaJson =
      std::string("{\"type\":\"object\",\"properties\":{") + requestIdSchema()
      + ",\"scene_id\":{\"type\":\"integer\",\"minimum\":0},"
        "\"target_batch_ref\":{\"type\":\"object\",\"properties\":{"
        "\"launch_id\":{\"type\":\"string\"},"
        "\"batch_id\":{\"type\":\"string\"}},"
        "\"required\":[\"launch_id\",\"batch_id\"],\"additionalProperties\":false}},"
        "\"required\":[\"request_id\",\"scene_id\",\"target_batch_ref\"],"
        "\"additionalProperties\":false}";
  restoreTool.handler = [deps](const session::Credentials &c,
                            const json::Value &args) {
    const auto id = requestIdArg(args.get("request_id"));
    auto req = detail::restoreArg(args);
    if (!id || !req)
      return rejected(Outcome::INVALID_REQUEST);
    const operations::OperationRef ref{c.sessionId, *id};
    return detail::ack(ref, deps.restores.submit(c, ref, std::move(*req)));
  };
  tools.push_back(std::move(restoreTool));

  if (!deps.translations || !deps.translations->valid())
    return;
  Tool preview;
  preview.name = "vsr_translate_preview";
  const auto &limits = deps.translations->limits();
  preview.description =
      "Preview a finite world-space delta of an explicit selection without "
      "any Scene write, batch or restore eligibility. Inspect fresh node handles "
      "and copy node_edit_version, structural_version and ancestry_version "
      "verbatim for EVERY selection entry, including inherited descendants. "
      "Identical duplicates collapse; conflicting expectations reject. Only "
      "topmost SELECTED scalar-transform roots are prepared; selecting parent "
      "and child moves both origins once, leaving child local state unchanged. "
      "Sibling roots stay independent: no common-parent/piece guessing or "
      "wrappers. World origin is not bounds/geometry centre. Linear transforms "
      "are preserved; required singular parents reject without pseudoinverse. "
      "Poll vsr_operation_status for exact prepared matrices/origins, dependencies "
      "and preview_ref. Unselected descendants inherit and are NOT enumerated. "
      "Prepared-reference lookup is separate from historical request replay; "
      "expiry/eviction never revives a plan. This tool does not commit. "
      "Limits: selection="
      + std::to_string(limits.maxSelection)
      + ", per-session plans=" + std::to_string(limits.perSession)
      + ", global plans=" + std::to_string(limits.globalCount)
      + ", aggregate owned bytes=" + std::to_string(limits.aggregateBytes)
      + ", fixed owner charge="
      + std::to_string(edit::TranslationService::INFRASTRUCTURE_BYTES)
      + ", attached sources<=" + std::to_string(limits.globalCount)
      + ", preparation charge="
      + std::to_string(edit::TranslationService::PREPARATION_BYTES)
      + ", each status-copy charge="
      + std::to_string(2 * edit::TranslationService::RESULT_BYTES)
      + ", ancestry steps=" + std::to_string(limits.ancestrySteps)
      + ", selected-effect entries=" + std::to_string(limits.effectEntries)
      + ", lifetime seconds=" + std::to_string(limits.lifetimeSeconds)
      + ", deadline seconds=" + std::to_string(limits.deadlineSeconds) + ".";
  preview.inputSchemaJson = std::string("{\"type\":\"object\",\"properties\":{")
      + requestIdSchema() + ",\"scene_id\":{\"type\":\"integer\",\"minimum\":0},"
        "\"world_delta\":{\"type\":\"array\",\"minItems\":3,\"maxItems\":3,"
        "\"items\":{\"type\":\"number\"}},\"selection\":{\"type\":\"array\","
        "\"minItems\":1,\"maxItems\":" + std::to_string(limits.maxSelection)
      + ",\"items\":{\"type\":\"object\",\"properties\":{"
        "\"handle\":{\"type\":\"string\"},\"node_edit_version\":{\"type\":\"string\"},"
        "\"structural_version\":{\"type\":\"string\"},\"ancestry_version\":{\"type\":\"string\"}},"
        "\"required\":[\"handle\",\"node_edit_version\",\"structural_version\",\"ancestry_version\"]}}},"
        "\"required\":[\"request_id\",\"scene_id\",\"selection\",\"world_delta\"]}";
  preview.handler = [deps](const session::Credentials &c,
                        const json::Value &args) {
    const auto id = requestIdArg(args.get("request_id"));
    const auto scene = targetIdArg(args, "scene_id");
    const auto *selection = args.get("selection");
    const auto *delta = args.get("world_delta");
    if (!id || !scene || !selection
        || selection->type != json::Value::Type::ARRAY || selection->a.empty()
        || selection->a.size() > deps.translations->limits().maxSelection
        || !delta || delta->type != json::Value::Type::ARRAY
        || delta->a.size() != 3)
      return rejected(Outcome::INVALID_REQUEST);
    edit::TranslationRequest request;
    request.scene = *scene;
    for (int i = 0; i < 3; ++i) {
      if (!delta->a[i].isNumber() || !std::isfinite(delta->a[i].n))
        return rejected(Outcome::INVALID_REQUEST);
      request.worldDelta[i] = delta->a[i].n;
    }
    // Reuse the exact existing node wire-token parser; do not invent an
    // alternate interpretation of version/domain/ancestry strings.
    std::string wire =
        "{\"scene_id\":" + std::to_string(*scene) + ",\"edits\":[";
    for (const auto &item : selection->a) {
      if (!item.isObject())
        return rejected(Outcome::INVALID_REQUEST);
      for (const auto *field : {"handle",
               "node_edit_version",
               "structural_version",
               "ancestry_version"}) {
        const auto *v = item.get(field);
        if (!v || !v->isString() || v->s.size() > 256)
          return rejected(Outcome::INVALID_REQUEST);
      }
      if (wire.back() != '[')
        wire += ',';
      wire +=
          "{\"target_kind\":\"node\",\"parameter\":\"position\",\"space\":\"world\",\"value\":[0,0,0],"
          "\"handle\":"
          + json::quote(item.get("handle")->s)
          + ",\"version\":" + json::quote(item.get("node_edit_version")->s)
          + ",\"structural_version\":"
          + json::quote(item.get("structural_version")->s)
          + ",\"ancestry_version\":"
          + json::quote(item.get("ancestry_version")->s) + "}";
    }
    wire += "]}";
    json::Value parsed;
    if (!json::parse(wire, parsed))
      return rejected(Outcome::INVALID_REQUEST);
    auto batch = detail::editBatchArg(parsed, deps.ids.launch());
    if (!batch)
      return rejected(Outcome::INVALID_REQUEST);
    for (const auto &entry : batch->edits)
      request.selection.push_back({entry.nodeExpected, entry.nodeDependencies});
    const operations::OperationRef ref{c.sessionId, *id};
    return detail::ack(
        ref, deps.translations->submit(c, ref, std::move(request)));
  };
  tools.push_back(std::move(preview));

  Tool commit;
  commit.name = "vsr_translate_commit";
  commit.description =
      "Atomically commit the exact retained selection preview, without new "
      "translation inputs or recomputation. Copy the entire preview_ref from "
      "vsr_translate_preview and supply a new request_id. Its scene_id is "
      "routing metadata, not authority; launch/session/Scene/dependencies and "
      "current read/edit/exposure are rechecked before any write. Stale, "
      "expired, foreign or consumed plans write nothing. Successful commit "
      "consumes the preview once; the same admitted request replays historical "
      "metadata without mutation, while a different request cannot reuse it. "
      "Poll vsr_operation_status for committed, outcome, exact versions, "
      "Scene revisions and the normal batch_ref for conditional vsr_restore. "
      "Later target/ancestor writes conflict even after change-back. "
      "Cancellation before commit writes nothing; late cancellation or sync "
      "failure may still be committed, never implicit rollback. Limits and "
      "the finite deadline are the same as vsr_translate_preview.";
  commit.inputSchemaJson = std::string("{\"type\":\"object\",\"properties\":{")
      + requestIdSchema()
      + ",\"preview_ref\":{\"type\":\"object\",\"properties\":{"
        "\"launch_id\":{\"type\":\"string\"},\"preview_id\":{\"type\":\"string\"},"
        "\"scene_id\":{\"type\":\"integer\",\"minimum\":0}},"
        "\"required\":[\"launch_id\",\"preview_id\",\"scene_id\"]}},"
        "\"required\":[\"request_id\",\"preview_ref\"]}";
  commit.handler = [deps](
                       const session::Credentials &c, const json::Value &args) {
    const auto id = requestIdArg(args.get("request_id"));
    const auto *ref = args.get("preview_ref");
    if (!id || !ref || !ref->isObject())
      return rejected(Outcome::INVALID_REQUEST);
    const auto scene = targetIdArg(*ref, "scene_id");
    const auto *launch = ref->get("launch_id");
    const auto *previewId = ref->get("preview_id");
    if (!scene || !launch || !launch->isString() || !previewId
        || !previewId->isString() || !edit::translation_detail::token(launch->s)
        || !edit::translation_detail::token(previewId->s))
      return rejected(Outcome::INVALID_REQUEST);
    const operations::OperationRef operation{c.sessionId, *id};
    return detail::ack(operation,
        deps.translations->submitCommit(
            c, operation, {launch->s, previewId->s, *scene}));
  };
  tools.push_back(std::move(commit));
}

} // namespace vsr::integration::mcp::tools

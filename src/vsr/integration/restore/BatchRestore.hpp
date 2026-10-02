// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Header-only: the vsr_integration library globs only top-level sources (see
// ticket 19).

// vsr_integration
#include "vsr/integration/edit/AgentEdit.hpp"
// std
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace vsr::integration::restore {

/*
 * One-way Batch Restoration (spec §11): return an eligible Agent Edit Batch's
 * parameters to their recorded prior values, completely or not at all.
 *
 * A restoration explicitly names its batch with `target_batch_ref`; external
 * work carries its own Operation Reference, local work uses host-local
 * correlation and host authority. Both go through the OperationEngine, so
 * they share admission, safe-point permission checks and serialization with
 * edits. At the safe point every affected object's current parameter version
 * must still equal the batch's recorded post-commit version; otherwise the
 * restore rejects with `conflict` and nothing mutates. Versions only advance,
 * so a conflict is permanent: the batch becomes CONFLICTING (its payload is
 * released, never consumed) and later attempts report `conflict` again. A
 * commit consumes eligibility even if the normal-synchronization hook then
 * fails. With an ActivitySink installed, the log is authoritative: a batch it
 * no longer marks eligible (expired or evicted) reports `outcome_unavailable`,
 * and commit/conflict update its Eligibility to RESTORED/CONFLICTING.
 * A restoration is never itself restorable: there is no redo.
 *
 * Threading: submit*() and queries are thread-safe; execution happens on the
 * servicing thread. The service must outlive every service() call that may
 * still run one of its steps.
 */

using edit::BatchRef;
using edit::EditService;
using identity::ParameterVersionToken;
using identity::SceneIdentity;
using identity::SceneRevision;
using operations::Admission;
using operations::LocalAdmission;
using operations::LocalRef;
using operations::OperationEngine;
using operations::OperationRef;
using operations::Outcome;

struct RestoreRequest
{
  // The scene the batch belongs to; the operation's registered target.
  SceneId scene{INVALID_SCENE_ID};
  BatchRef targetBatchRef;
};

struct RestoreResult
{
  Outcome outcome{Outcome::OK};
  bool committed{false};
  BatchRef targetBatchRef;
  std::optional<SceneRevision> sceneRevision; // resulting, on commit
  std::vector<ParameterVersionToken> versions; // resulting, per object
  std::vector<edit::NodeVersion> nodeVersions;
  // outcome_unavailable because the log/store expired the batch; unknown
  // and wrong-launch references leave this false (addendum 04 §7).
  bool expired{false};
};

// Test seam for preparation failures; return true to fail preparation.
using RestoreFaultInjector = std::function<bool()>;

class RestoreService
{
 public:
  RestoreService(OperationEngine &engine, EditService &edits);
  ~RestoreService();
  RestoreService(const RestoreService &) = delete;
  RestoreService &operator=(const RestoreService &) = delete;

  // Host: associate a registered scene with its identity observer.
  void attachScene(SceneId scene, SceneIdentity &identity);
  void setFaultInjector(RestoreFaultInjector injector);

  // External restoration under the current session.
  Admission submit(const operations::Credentials &credentials,
      const OperationRef &ref,
      RestoreRequest request);
  // Host-local Restore under host authority.
  LocalAdmission submitLocal(RestoreRequest request);

  std::optional<RestoreResult> result(const OperationRef &ref) const;
  std::optional<RestoreResult> resultLocal(LocalRef ref) const;

  // True while the batch is retained, not yet restored and not conflicting.
  bool eligible(const BatchRef &ref) const;
  bool restored(const BatchRef &ref) const;
  bool conflicting(const BatchRef &ref) const;

  static std::string canonicalArguments(const RestoreRequest &request);

 private:
  using Slot = std::shared_ptr<std::optional<RestoreResult>>;

  static bool validEnvelope(const RestoreRequest &request);
  operations::Request makeRequest(RestoreRequest request, Slot slot);
  RestoreResult commit(
      const RestoreRequest &request, operations::StepContext &ctx);
  void markConflicting(const BatchRef &ref, operations::StepContext &ctx);

  OperationEngine &m_engine;
  EditService &m_edits;
  RestoreFaultInjector m_fault;
  std::shared_ptr<bool> m_alive;

  mutable std::mutex m_mutex;
  std::map<SceneId, SceneIdentity *> m_identities;
  // Consumed / conflicting batch IDs (launch-qualified); payloads released.
  std::set<std::string> m_consumed;
  std::set<std::string> m_conflicting;
  std::map<std::pair<std::string, std::uint64_t>, Slot> m_results;
  std::map<std::uint64_t, Slot> m_localResults;
};

///////////////////////////////////////////////////////////////////////////////
// Inlined definitions ////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

namespace detail {

inline std::string consumedKey(const BatchRef &ref)
{
  return ref.launchId + "/" + ref.batchId;
}

} // namespace detail

inline RestoreService::RestoreService(
    OperationEngine &engine, EditService &edits)
    : m_engine(engine), m_edits(edits), m_alive(std::make_shared<bool>(true))
{}

inline RestoreService::~RestoreService()
{
  m_alive.reset();
}

inline void RestoreService::attachScene(SceneId scene, SceneIdentity &identity)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_identities[scene] = &identity;
}

inline void RestoreService::setFaultInjector(RestoreFaultInjector injector)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_fault = std::move(injector);
}

inline bool RestoreService::restored(const BatchRef &ref) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_consumed.count(detail::consumedKey(ref)) != 0;
}

inline bool RestoreService::conflicting(const BatchRef &ref) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_conflicting.count(detail::consumedKey(ref)) != 0;
}

inline bool RestoreService::eligible(const BatchRef &ref) const
{
  return !restored(ref) && !conflicting(ref)
      && m_edits.restorations().find(ref).has_value();
}

inline bool RestoreService::validEnvelope(const RestoreRequest &request)
{
  return request.scene != INVALID_SCENE_ID
      && !request.targetBatchRef.launchId.empty()
      && !request.targetBatchRef.batchId.empty();
}

inline std::string RestoreService::canonicalArguments(
    const RestoreRequest &request)
{
  return "restore;scene=" + std::to_string(request.scene)
      + ";launch=" + request.targetBatchRef.launchId
      + ";batch=" + request.targetBatchRef.batchId + ";";
}

inline operations::Request RestoreService::makeRequest(
    RestoreRequest request, Slot slot)
{
  operations::Request r;
  r.kind = "restore";
  r.target = session::Target::scene(request.scene);
  r.capability = session::Capability::EDIT;
  r.canonicalArguments = canonicalArguments(request);
  std::weak_ptr<bool> alive = m_alive;
  auto shared = std::make_shared<RestoreRequest>(std::move(request));
  auto committed = std::make_shared<std::optional<RestoreResult>>();
  r.executor = [this, alive, shared, slot, committed](
                   operations::StepContext &ctx) {
    operations::Step step;
    if (!alive.lock())
      return step;
    if (!*committed) {
      if (ctx.abandoned)
        return step; // never mutate for abandoned work
      RestoreResult res = commit(*shared, ctx);
      if (!res.committed) {
        step.outcome = res.outcome;
        if (res.expired)
          step.result = "{\"reason\":\"expired\"}";
        *slot = std::move(res);
        return step;
      }
      *committed = std::move(res);
      step.committed = true;
      step.status = operations::Step::Status::SYNCHRONIZING;
      return step;
    }
    RestoreResult res = std::move(**committed);
    res.outcome = m_edits.synchronizeCommitted(shared->scene);
    step.outcome = res.outcome;
    step.committed = true;
    step.result = "{\"target_batch_ref\":"
        + edit::detail::batchRefJson(res.targetBatchRef) + ","
        + edit::detail::commitDetailJson(
            true, res.sceneRevision, res.versions, res.nodeVersions)
        + "}";
    *slot = std::move(res);
    return step;
  };
  return r;
}

inline Admission RestoreService::submit(
    const operations::Credentials &credentials,
    const OperationRef &ref,
    RestoreRequest request)
{
  if (!validEnvelope(request)) {
    Admission a;
    a.code = Outcome::INVALID_REQUEST;
    return a;
  }
  auto slot = std::make_shared<std::optional<RestoreResult>>();
  auto a =
      m_engine.submit(credentials, ref, makeRequest(std::move(request), slot));
  if (a.code == Outcome::OK && !a.replayed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_results[{ref.sessionId, ref.requestId}] = slot;
  }
  return a;
}

inline LocalAdmission RestoreService::submitLocal(RestoreRequest request)
{
  if (!validEnvelope(request)) {
    LocalAdmission a;
    a.code = Outcome::INVALID_REQUEST;
    return a;
  }
  auto slot = std::make_shared<std::optional<RestoreResult>>();
  auto a = m_engine.submitLocal(makeRequest(std::move(request), slot));
  if (a.code == Outcome::OK) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_localResults[a.ref.id] = slot;
  }
  return a;
}

inline std::optional<RestoreResult> RestoreService::result(
    const OperationRef &ref) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_results.find({ref.sessionId, ref.requestId});
  if (it == m_results.end())
    return std::nullopt;
  return *it->second;
}

inline std::optional<RestoreResult> RestoreService::resultLocal(
    LocalRef ref) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_localResults.find(ref.id);
  if (it == m_localResults.end())
    return std::nullopt;
  return *it->second;
}

inline void RestoreService::markConflicting(
    const BatchRef &ref, operations::StepContext &ctx)
{
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_conflicting.insert(detail::consumedKey(ref));
  }
  m_edits.restorations().release(ref);
  if (ctx.markConflicting)
    ctx.markConflicting(ref.launchId, ref.batchId);
}

inline RestoreResult RestoreService::commit(
    const RestoreRequest &request, operations::StepContext &ctx)
{
  scene::Scene &scene = ctx.scene;
  RestoreResult res;
  res.targetBatchRef = request.targetBatchRef;
  SceneIdentity *ident = nullptr;
  RestoreFaultInjector fault;
  bool consumed = false;
  bool conflicted = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_identities.find(request.scene);
    if (it != m_identities.end())
      ident = it->second;
    fault = m_fault;
    consumed = m_consumed.count(detail::consumedKey(request.targetBatchRef));
    conflicted =
        m_conflicting.count(detail::consumedKey(request.targetBatchRef));
  }
  if (!ident || ident->scene() != &scene) {
    res.outcome = Outcome::INVALID_REQUEST;
    return res;
  }

  // 1. Select exactly the named record; never fall back to another one. //

  if (consumed) {
    res.outcome = Outcome::ALREADY_RESTORED;
    return res;
  }
  if (conflicted) {
    res.outcome = Outcome::CONFLICT;
    return res;
  }
  bool logExpired = false;
  if (ctx.restorationRetained
      && !ctx.restorationRetained(
          request.targetBatchRef.launchId, request.targetBatchRef.batchId)) {
    // The activity log expired or evicted it: keep the store in step.
    logExpired = m_edits.restorations().find(request.targetBatchRef)
        || m_edits.restorations().expired(request.targetBatchRef);
    m_edits.restorations().expireUnless([&](const BatchRef &r) {
      return ctx.restorationRetained(r.launchId, r.batchId);
    });
  }
  auto record = m_edits.restorations().find(request.targetBatchRef);
  if (!record || record->scene != request.scene) {
    // Unknown, expired, wrong-launch or foreign-scene reference. Only a
    // batch this launch issued and then expired carries a reason.
    res.outcome = Outcome::OUTCOME_UNAVAILABLE;
    res.expired = !record
        && request.targetBatchRef.launchId == m_edits.launchId()
        && (logExpired
            || m_edits.restorations().expired(request.targetBatchRef));
    return res;
  }

  // 2. Eligibility: every affected object is unchanged since the commit. //

  for (auto &v : record->resultingVersions) {
    if (!ident->isCurrent(v)) {
      markConflicting(request.targetBatchRef, ctx);
      res.outcome = Outcome::CONFLICT;
      return res;
    }
  }

  std::size_t remaining = 4096;
  std::vector<identity::NodeDependencyWalk> dependencies;
  std::vector<std::vector<scene::LayerNodeRef>> ancestors;
  try {
    ancestors.resize(record->nodeChanges.size());
    dependencies.reserve(record->nodeChanges.size());
  } catch (const std::bad_alloc &) {
    res.outcome = Outcome::BUSY;
    return res;
  }
  for (const auto &c : record->nodeChanges) {
    identity::NodeDependencyWalk current;
    Outcome status;
    try {
      status = edit::detail::nodeDependencies(*ident,
          c.resulting.expected.node,
          current,
          remaining,
          &ancestors[dependencies.size()]);
    } catch (const std::bad_alloc &) {
      res.outcome = Outcome::BUSY;
      return res;
    }
    if (status == Outcome::BUSY) {
      res.outcome = status;
      return res;
    }
    if (status != Outcome::OK || !ident->isCurrent(c.resulting.expected)
        || !current.matchesStructure(c.resulting.dependencies)
        || (c.requiresAncestry
            && !current.matchesAncestry(c.resulting.dependencies))) {
      markConflicting(request.targetBatchRef, ctx);
      res.outcome = Outcome::CONFLICT;
      return res;
    }
    dependencies.push_back(current);
  }

  // 3. Prepare in bounded temporary memory; nothing mutates on failure. //

  struct Planned
  {
    scene::Object *object;
    const edit::Change *change;
  };
  std::vector<Planned> plan;
  std::vector<scene::Object *> objects;
  std::vector<const scene::Layer *> layers;
  std::vector<scene::LayerNodeRef> nodes;
  try {
    if (fault && fault())
      throw std::bad_alloc();
    nodes.reserve(record->nodeChanges.size());
    layers.reserve(record->nodeChanges.size());
    res.nodeVersions.reserve(record->nodeChanges.size());
    for (const auto &c : record->nodeChanges) {
      auto n = ident->resolve(c.resulting.expected.node);
      nodes.push_back(n);
      const auto *layer = n->value().layer();
      if (std::find(layers.begin(), layers.end(), layer) == layers.end())
        layers.push_back(layer);
    }
    plan.reserve(record->changes.size());
    objects.reserve(record->resultingVersions.size());
    for (auto &c : record->changes) {
      auto *o = ident->resolve(c.object);
      if (!o || !c.before.valid() || !o->parameter(core::Token(c.parameter))) {
        markConflicting(request.targetBatchRef, ctx);
        res.outcome = Outcome::CONFLICT; // missing/incompatible reference
        return res;
      }
      plan.push_back({o, &c});
      if (std::find(objects.begin(), objects.end(), o) == objects.end())
        objects.push_back(o);
    }
    res.versions.reserve(objects.size());
  } catch (const std::bad_alloc &) {
    res.outcome = Outcome::BUSY;
    return res;
  }

  // 4. Commit: store every prior value, then flush signals. //

  scene.beginUpdateBatch();
  for (auto *o : objects)
    o->beginParameterBatch();
  for (auto &p : plan) {
    p.object->setParameter(core::Token(p.change->parameter),
        p.change->before.type(),
        p.change->before.data());
  }
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const auto &c = record->nodeChanges[i];
    if (c.changedTransform)
      nodes[i]->value().setTransformState(c.beforeMatrix, c.beforeSRT);
    if (c.changedEnabled)
      nodes[i]->value().setEnabled(c.beforeEnabled);
  }
  for (auto *o : objects)
    o->endParameterBatch();
  for (const auto *layer : layers)
    scene.signalLayerStructureChanged(layer);
  scene.endUpdateBatch();

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_consumed.insert(detail::consumedKey(request.targetBatchRef));
  }
  m_edits.restorations().release(request.targetBatchRef);
  if (ctx.markRestored)
    ctx.markRestored(
        request.targetBatchRef.launchId, request.targetBatchRef.batchId);
  for (auto *o : objects) {
    if (auto v = ident->parameterVersion(ident->handleOf(o)))
      res.versions.push_back(*v);
  }
  for (std::size_t i = 0; i < record->nodeChanges.size(); ++i) {
    auto v = record->nodeChanges[i].resulting;
    v.expected = *ident->nodeEditVersion(v.expected.node);
    v.dependencies = dependencies[i];
    v.dependencies.transformVersion = 0;
    for (const auto &a : ancestors[i])
      v.dependencies.transformVersion = std::max(
          v.dependencies.transformVersion, a->value().transformVersion());
    res.nodeVersions.push_back(v);
  }
  res.committed = true;
  res.sceneRevision = ident->sceneRevision();
  m_edits.syncStates().beginHook(request.scene);
  return res;
}

} // namespace vsr::integration::restore

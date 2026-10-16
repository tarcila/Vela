// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Header-only: the vsr_integration library globs only top-level sources (see
// ticket 19).

// vsr_integration
#include "vsr/integration/content/AllocationProfile.hpp"
#include "vsr/integration/content/CapacityAllocator.hpp"
#include "vsr/integration/edit/AgentEdit.hpp"
#include "vsr/integration/mesh/MeshCommit.hpp"
// vsr_scene
#include "vsr/scene/detail/PreparedMeshBinding.hpp"
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
#include <utility>
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

struct ImageVersion
{
  identity::ObjectHandle handle;
  std::string dataVersion;
};

struct RestoreResult
{
  operations::ResultOwnership ownership;
  std::shared_ptr<const void> wireLease;
  Outcome outcome{Outcome::OK};
  bool committed{false};
  BatchRef targetBatchRef;
  std::optional<SceneRevision> sceneRevision; // resulting, on commit
  std::vector<ParameterVersionToken> versions; // resulting, per object
  std::vector<edit::NodeVersion> nodeVersions;
  std::vector<ImageVersion> imageVersions;
  std::string reason;
  // Image-only wire receipt storage is prepared before map(), then moved
  // into the terminal Step without allocating after the committed write.
  std::string imageReceipt;
  // outcome_unavailable because the log/store expired the batch; unknown
  // and wrong-launch references leave this false (addendum 04 §7).
  bool expired{false};
};

// Test seam for preparation failures; return true to fail preparation.
using RestoreFaultInjector = std::function<bool()>;

class RestoreService
{
 public:
  RestoreService(OperationEngine &engine,
      EditService &edits,
      operations::ResultOwnership::Reserve reserveResult = {});
  ~RestoreService();
  RestoreService(const RestoreService &) = delete;
  RestoreService &operator=(const RestoreService &) = delete;

  // Host: associate a registered scene with its identity observer.
  void attachScene(SceneId scene, SceneIdentity &identity);
  void setFaultInjector(RestoreFaultInjector injector);
  bool owns(const OperationEngine &engine, const EditService &edits) const;
  bool resultOwnershipAvailable() const;
  std::weak_ptr<const void> lifetime() const;

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
  struct ResultSlot
  {
    operations::ResultOwnership ownership;
    RestoreRequest request;
    std::optional<RestoreResult> result;
    bool hookPending{false};
    bool completed{false};
  };
  using Slot = std::shared_ptr<ResultSlot>;
  struct SceneBinding
  {
    SceneIdentity *identity{nullptr};
    std::weak_ptr<const void> lifetime;
  };

  static bool validEnvelope(const RestoreRequest &request);
  operations::Request makeRequest(RestoreRequest request, Slot slot);
  RestoreResult commit(const RestoreRequest &request,
      operations::StepContext &ctx,
      bool &hookPending);
  void markConflicting(const BatchRef &ref, operations::StepContext &ctx);

  OperationEngine &m_engine;
  EditService &m_edits;
  operations::ResultOwnership::Reserve m_reserveResult;
  std::shared_ptr<const RestoreFaultInjector> m_fault;
  std::shared_ptr<bool> m_alive;

  mutable std::mutex m_mutex;
  std::map<SceneId, SceneBinding> m_identities;
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

inline RestoreService::RestoreService(OperationEngine &engine,
    EditService &edits,
    operations::ResultOwnership::Reserve reserveResult)
    : m_engine(engine),
      m_edits(edits),
      m_reserveResult(std::move(reserveResult)),
      m_alive(std::make_shared<bool>(true))
{}

inline RestoreService::~RestoreService()
{
  m_alive.reset();
}

inline bool RestoreService::owns(
    const OperationEngine &engine, const EditService &edits) const
{
  return &m_engine == &engine && &m_edits == &edits;
}

inline bool RestoreService::resultOwnershipAvailable() const
{
  return bool(m_reserveResult);
}

inline std::weak_ptr<const void> RestoreService::lifetime() const
{
  return m_alive;
}

inline void RestoreService::attachScene(SceneId scene, SceneIdentity &identity)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_identities[scene] = {&identity, identity.lifetime()};
}

inline void RestoreService::setFaultInjector(RestoreFaultInjector injector)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_fault = injector
      ? std::make_shared<const RestoreFaultInjector>(std::move(injector))
      : nullptr;
}

inline bool RestoreService::restored(const BatchRef &ref) const
{
  if (m_edits.restorations().imageState(ref)
      == edit::RestorationStore::ImageState::RESTORED)
    return true;
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_consumed.count(detail::consumedKey(ref)) != 0;
}

inline bool RestoreService::conflicting(const BatchRef &ref) const
{
  if (m_edits.restorations().imageState(ref)
      == edit::RestorationStore::ImageState::CONFLICTING)
    return true;
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
  r.resourceLease = slot->ownership.lease;
  r.reserveStatus = slot->ownership.reserve;
  r.target = session::Target::scene(request.scene);
  r.capability = session::Capability::EDIT;
  r.canonicalArguments = canonicalArguments(request);
  std::weak_ptr<bool> alive = m_alive;
  slot->request = std::move(request);
  const auto failure = [this, alive, slot] {
    if (!alive.lock())
      return;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (slot->result)
        slot->result->outcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
      slot->completed = true;
    }
    if (std::exchange(slot->hookPending, false))
      m_edits.syncStates().finishHook(slot->request.scene, false);
    else
      m_edits.syncStates().markFailed(slot->request.scene);
  };
  const auto execute = [this, alive, slot](operations::StepContext &ctx) {
    operations::Step step;
    if (!alive.lock())
      return step;
    if (!slot->result) {
      if (ctx.abandoned)
        return step; // never mutate for abandoned work
      RestoreResult res = commit(slot->request, ctx, slot->hookPending);
      if (!res.committed) {
        step.outcome = res.outcome;
        if (res.expired)
          step.result = "{\"reason\":\"expired\"}";
        else if (!res.reason.empty())
          step.result = "{\"reason\":\"" + res.reason + "\"}";
        slot->result = std::move(res);
        slot->completed = true;
        return step;
      }
      slot->result = std::move(res);
      step.committed = true;
      step.status = operations::Step::Status::SYNCHRONIZING;
      return step;
    }
    auto &res = *slot->result;
    Outcome syncOutcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
    // The normal hook owns completion even when it throws. A late engine
    // failure must not consume somebody else's outstanding hook.
    slot->hookPending = false;
    try {
      syncOutcome = m_edits.synchronizeCommitted(slot->request.scene);
    } catch (...) {
      // Commit/eligibility are already final; host notification failure is
      // not a noncommitting preparation rejection.
    }
    if (res.outcome != Outcome::APPLIED_WITH_SYNC_FAILURE)
      res.outcome = syncOutcome;
    step.outcome = res.outcome;
    step.committed = true;
    if (!res.imageReceipt.empty()) {
      step.resourceLease = std::move(res.wireLease);
      step.reserveStatus = res.ownership.reserve;
      step.result = std::move(res.imageReceipt);
      slot->completed = true;
      return step;
    }
    step.result = "{\"target_batch_ref\":"
        + edit::detail::batchRefJson(res.targetBatchRef) + ","
        + edit::detail::commitDetailJson(
            true, res.sceneRevision, res.versions, res.nodeVersions)
        + ",\"image_versions\":[";
    for (const auto &image : res.imageVersions) {
      if (step.result.back() != '[')
        step.result += ',';
      step.result += "{\"handle\":\"" + edit::detail::hex64(image.handle.domain)
          + edit::detail::hex64(image.handle.serial) + "\",\"data_version\":\""
          + image.dataVersion + "\"}";
    }
    step.result += "]";
    if (res.nodeVersions.empty())
      step.result += ",\"node_versions\":[]";
    step.result += ",\"restored\":true}";
    slot->completed = true;
    return step;
  };
  // The existing 64 KiB slot profile covers this known control allocation,
  // request and callback targets, separately from RestoreResult's reservation.
  // Qualify actual backing before constructing std::functions; their captures
  // share the original charge, never clone an opaque host closure.
  core::StorageProfile callbacks(64u << 10);
  if (!callbacks.add(sizeof(failure) + alignof(decltype(failure)))
      || !callbacks.add(2, sizeof(execute) + alignof(decltype(execute)))
      || !callbacks.add(r.canonicalArguments.capacity() + 1)
      || !callbacks.add(2, slot->request.targetBatchRef.launchId.capacity() + 1)
      || !callbacks.add(2, slot->request.targetBatchRef.batchId.capacity() + 1)
      || !callbacks.add(
          content::allocation_profile::sharedControlBytes<ResultSlot,
              content::CapacityAllocator<ResultSlot>>()
          + sizeof(content::CapacityArena::Charge)
          + 2 * alignof(std::max_align_t)))
    throw std::bad_alloc();
  r.onSynchronizationFailure = failure;
  r.executor = execute;
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
  operations::ResultOwnership ownership;
  try {
    ownership = operations::ResultOwnership(m_reserveResult);
  } catch (const std::bad_alloc &) {
    Admission busy;
    busy.code = Outcome::BUSY;
    return busy;
  }
  auto slot = std::allocate_shared<ResultSlot>(
      content::CapacityAllocator<ResultSlot>(ownership.lease));
  slot->ownership = std::move(ownership);
  auto a =
      m_engine.submit(credentials, ref, makeRequest(std::move(request), slot));
  if (a.code == Outcome::OK && !a.replayed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Completed Image result slots retain their own charge; bound history
    // independently of Engine/Activity retention and never prune active slots.
    while (m_results.size() >= 1024) {
      auto old = std::find_if(
          m_results.begin(), m_results.end(), [](const auto &entry) {
            return entry.second->completed || entry.second.use_count() == 1;
          });
      if (old == m_results.end())
        break;
      m_results.erase(old);
    }
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
  operations::ResultOwnership ownership;
  try {
    ownership = operations::ResultOwnership(m_reserveResult);
  } catch (const std::bad_alloc &) {
    LocalAdmission busy;
    busy.code = Outcome::BUSY;
    return busy;
  }
  auto slot = std::allocate_shared<ResultSlot>(
      content::CapacityAllocator<ResultSlot>(ownership.lease));
  slot->ownership = std::move(ownership);
  auto a = m_engine.submitLocal(makeRequest(std::move(request), slot));
  if (a.code == Outcome::OK) {
    std::lock_guard<std::mutex> lock(m_mutex);
    while (m_localResults.size() >= 1024) {
      auto old = std::find_if(
          m_localResults.begin(), m_localResults.end(), [](const auto &entry) {
            return entry.second->completed || entry.second.use_count() == 1;
          });
      if (old == m_localResults.end())
        break;
      m_localResults.erase(old);
    }
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
  return it->second->completed ? it->second->result : std::nullopt;
}

inline std::optional<RestoreResult> RestoreService::resultLocal(
    LocalRef ref) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_localResults.find(ref.id);
  if (it == m_localResults.end())
    return std::nullopt;
  return it->second->completed ? it->second->result : std::nullopt;
}

inline void RestoreService::markConflicting(
    const BatchRef &ref, operations::StepContext &ctx)
{
  if (!m_edits.restorations().setImageState(
          ref, edit::RestorationStore::ImageState::CONFLICTING)) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_conflicting.insert(detail::consumedKey(ref));
  }
  m_edits.restorations().release(ref);
  if (ctx.markConflicting)
    ctx.markConflicting(ref.launchId, ref.batchId);
}

inline RestoreResult RestoreService::commit(const RestoreRequest &request,
    operations::StepContext &ctx,
    bool &hookPending)
{
  scene::Scene &scene = ctx.scene;
  RestoreResult res;
  try {
    res.ownership = operations::ResultOwnership(m_reserveResult);
    // A later beginHook must increment the prepared entry without allocating,
    // so setting hookPending immediately before it cannot claim a missing hook.
    m_edits.syncStates().prepareScene(request.scene);
  } catch (const std::bad_alloc &) {
    res.outcome = Outcome::BUSY;
    return res;
  }
  res.targetBatchRef = request.targetBatchRef;
  SceneIdentity *ident = nullptr;
  std::shared_ptr<const RestoreFaultInjector> fault;
  bool consumed = false;
  bool conflicted = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_identities.find(request.scene);
    if (it != m_identities.end() && !it->second.lifetime.expired())
      ident = it->second.identity;
    fault = m_fault;
    consumed = m_consumed.count(detail::consumedKey(request.targetBatchRef));
    conflicted =
        m_conflicting.count(detail::consumedKey(request.targetBatchRef));
  }
  // Image scope precedes consumed/conflicting/target checks, after current
  // authority. Retirement cannot transfer this registration's recovery.
  const auto imageScope =
      m_edits.restorations().imageScope(request.targetBatchRef);
  if (imageScope && ctx.restorationKnown
      && !ctx.restorationKnown(
          request.targetBatchRef.launchId, request.targetBatchRef.batchId)) {
    m_edits.restorations().expireUnless([&](const BatchRef &ref) {
      return !ctx.restorationRetained
          || ctx.restorationRetained(ref.launchId, ref.batchId);
    });
    res.expired = m_edits.restorations().expired(request.targetBatchRef);
    m_edits.restorations().forgetImageScope(request.targetBatchRef);
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      const auto key = detail::consumedKey(request.targetBatchRef);
      m_consumed.erase(key);
      m_conflicting.erase(key);
    }
    res.outcome = Outcome::OUTCOME_UNAVAILABLE;
    return res;
  }
  if (imageScope && *imageScope != request.scene) {
    res.outcome = Outcome::OUTCOME_UNAVAILABLE;
    res.reason = "foreign_scene";
    return res;
  }
  // 1. Select exactly the named record; never fall back to another one. //
  const auto imageState =
      m_edits.restorations().imageState(request.targetBatchRef);
  consumed =
      consumed || imageState == edit::RestorationStore::ImageState::RESTORED;
  conflicted = conflicted
      || imageState == edit::RestorationStore::ImageState::CONFLICTING;

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

  if (record->hdri) {
    const auto &snapshot = record->hdri;
    const auto check = [&]() {
      return ident ? snapshot->evidence.check(*ident) : "owner_retired";
    };
    const auto reject = [&](const char *reason) {
      if (std::strcmp(reason, "array_mapped") == 0
          || std::strcmp(reason, "write_contract_unavailable") == 0) {
        res.outcome = Outcome::BUSY;
        res.reason = reason;
      } else {
        markConflicting(request.targetBatchRef, ctx);
        res.outcome = Outcome::CONFLICT;
      }
    };
    if (const auto *reason = check()) {
      reject(reason);
      return res;
    }
    std::shared_ptr<const void> tombstoneLease;
    std::unique_ptr<content::PreparedHdriRemoval> transaction;
    try {
      if (fault && (*fault)())
        throw std::bad_alloc();
      auto *array = dynamic_cast<scene::Array *>(
          ident->resolve(snapshot->evidence.handles.radiance));
      core::StorageProfile removal(64u << 20);
      if (!snapshot->reserveRemoval || !array
          || !scene::detail::PreparedHdriContent::profileStorage(scene,
              ident->resolve(snapshot->evidence.handles.contentRoot),
              array->size(),
              false,
              removal)
          || !removal.add(sizeof(content::PreparedHdriRemoval)
              + sizeof(scene::detail::PreparedHdriRemoval)))
        throw std::bad_alloc();
      // Declared before transaction: coverage survives notifications, their
      // exception paths, and destruction of the removed radiance tombstone.
      // The live Scene reserve backing in this conservative profile is not
      // reclaimed by dropping this temporary ownership reservation.
      tombstoneLease = snapshot->reserveRemoval(removal.bytes());
      if (!tombstoneLease)
        throw std::bad_alloc();
      res.ownership = operations::ResultOwnership(snapshot->reserveResult);
      res.wireLease = snapshot->reserveResult();
      if (!res.wireLease)
        throw std::bad_alloc();
      res.imageReceipt = "{\"target_batch_ref\":"
          + edit::detail::batchRefJson(request.targetBatchRef)
          + ",\"committed\":true,\"restored\":true,\"removed_content\":true,"
            "\"versions\":[],\"node_versions\":[],\"image_versions\":[]}";
      res.imageReceipt.reserve(res.imageReceipt.size() + 256);
      if (!ctx.prepareActivityResult
          || !ctx.prepareActivityResult(
              res.imageReceipt.capacity(), snapshot->reserveResult))
        throw std::bad_alloc();
      m_edits.syncStates().prepareScene(request.scene);
      transaction = std::make_unique<content::PreparedHdriRemoval>(
          *ident, snapshot->evidence);
      if (!ctx.prepareCommit || !ctx.markCommitted)
        throw std::bad_alloc();
      ctx.prepareCommit(res.imageReceipt, res.imageReceipt.capacity());
    } catch (const std::bad_alloc &) {
      res.outcome = Outcome::BUSY;
      res.reason = "resource_limit";
      return res;
    }
    if (!ctx.beforeWrite) {
      res.outcome = Outcome::FORBIDDEN;
      return res;
    }
    res.outcome = ctx.beforeWrite();
    if (res.outcome != Outcome::OK)
      return res;
    if (const auto *reason = check()) {
      reject(reason);
      return res;
    }
    if (!transaction->isCurrent() || !transaction->commit()) {
      res.outcome = Outcome::BUSY;
      res.reason = "prepared_state_changed";
      return res;
    }
    res.committed = true;
    ctx.markCommitted();
    m_edits.restorations().setImageState(
        request.targetBatchRef, edit::RestorationStore::ImageState::RESTORED);
    m_edits.restorations().release(request.targetBatchRef);
    try {
      if (ctx.markRestored)
        ctx.markRestored(
            request.targetBatchRef.launchId, request.targetBatchRef.batchId);
    } catch (...) {
      res.outcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
    }
    res.sceneRevision = ident->sceneRevision();
    char revision[256];
    const auto &value = *res.sceneRevision;
    std::snprintf(revision,
        sizeof(revision),
        ",\"scene_revision\":\"%llu.%llu.%llu.%llu\"}",
        (unsigned long long)value.launch.activation,
        (unsigned long long)value.launch.nonce,
        (unsigned long long)value.domain,
        (unsigned long long)value.value);
    res.imageReceipt.pop_back();
    res.imageReceipt.append(revision);
    try {
      transaction->notify();
    } catch (...) {
      res.outcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
    }
    try {
      hookPending = true;
      m_edits.syncStates().beginHook(request.scene);
    } catch (...) {
      res.outcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
    }
    return res;
  }

  if (record->mesh) {
    const auto &snapshot = record->mesh;
    auto *geometry = ident && ident->scene() == &scene
        ? dynamic_cast<scene::Geometry *>(ident->resolve(snapshot->target))
        : nullptr;
    const auto check = [&]() { return mesh::restorable(*snapshot, geometry); };
    if (!scene.imageWriteSequence()
        || !scene.imageWriteSequence()->isEnabled()) {
      res.outcome = Outcome::BUSY;
      res.reason = "host_write_contract_unavailable";
      return res;
    }
    const auto *reason = ident ? check() : "conflict";
    if (reason) {
      if (std::strcmp(reason, "array_mapped") == 0) {
        res.outcome = Outcome::BUSY;
        res.reason = reason;
      } else {
        markConflicting(request.targetBatchRef, ctx);
        res.outcome = Outcome::CONFLICT;
      }
      return res;
    }
    std::unique_ptr<scene::detail::PreparedMeshBinding> transaction;
    std::vector<scene::Array *> oldArrays;
    std::vector<std::pair<std::weak_ptr<scene::ArrayLifetime>, std::size_t>>
        preparedIndices;
    try {
      if (fault && (*fault)())
        throw std::bad_alloc();
      res.ownership = operations::ResultOwnership(snapshot->reserveResult);
      res.wireLease = snapshot->reserveResult();
      if (!res.wireLease)
        throw std::bad_alloc();
      res.imageReceipt = "{\"target_batch_ref\":"
          + edit::detail::batchRefJson(request.targetBatchRef)
          + ",\"committed\":true,\"restored\":true,\"versions\":[],\"node_versions\":[],\"target_version\":"
          + mcp::json::quote(
              mesh::targetVersion(*geometry, snapshot->target, true));
      res.imageReceipt.reserve(res.imageReceipt.size() + 256);
      if (!ctx.prepareActivityResult
          || !ctx.prepareActivityResult(
              res.imageReceipt.size() + 256, snapshot->reserveResult))
        throw std::bad_alloc();
      m_edits.syncStates().prepareScene(request.scene);
      // Prepare presence, complete Parameter metadata and current prior indices
      // before the final host callback; restoration never rewrites Array bytes.
      geometry =
          dynamic_cast<scene::Geometry *>(ident->resolve(snapshot->target));
      if ((reason = check())) {
        res.outcome = std::strcmp(reason, "array_mapped") == 0
            ? Outcome::BUSY
            : Outcome::CONFLICT;
        if (res.outcome == Outcome::CONFLICT)
          markConflicting(request.targetBatchRef, ctx);
        return res;
      }
      // Removed prior Parameters and unrelated later UI metadata can each fit
      // separately while their prepared union exceeds the charged map bound.
      // Check all recovered string/control storage before making any copy.
      std::size_t recoveredBytes = 0;
      for (const auto &binding : snapshot->bindings)
        if (binding.prior && !geometry->parameter(binding.name)
            && !mesh::addParameterCopyBytes(*binding.prior, recoveredBytes))
          throw std::bad_alloc();
      if (!mesh::boundedParameters(*geometry, recoveredBytes)
          || !scene::detail::PreparedMeshBinding::available(*geometry))
        throw std::bad_alloc();
      transaction =
          std::make_unique<scene::detail::PreparedMeshBinding>(*geometry);
      oldArrays.reserve(snapshot->dependencies.size());
      preparedIndices.reserve(snapshot->dependencies.size());
      for (const auto &dependency : snapshot->dependencies) {
        auto *array = dependency.lifetime.lock()->array;
        oldArrays.push_back(array);
        preparedIndices.push_back({dependency.lifetime, array->index()});
      }
      for (const auto &binding : snapshot->bindings) {
        // Restore missing old Parameters exactly; for still-present bindings,
        // preserve unrelated UI metadata edited since import.
        if (!binding.prior || !geometry->parameter(binding.name))
          transaction->set(binding.name, binding.prior);
        if (binding.prior)
          transaction->bind(binding.name,
              binding.before.lock()->array,
              binding.prior->isEnabled());
      }
      transaction->prepareNotifications();
    } catch (const std::bad_alloc &) {
      res.outcome = Outcome::BUSY;
      res.reason = "resource_limit";
      return res;
    }
    if (!ctx.beforeWrite) {
      res.outcome = Outcome::FORBIDDEN;
      return res;
    }
    res.outcome = ctx.beforeWrite();
    if (res.outcome != Outcome::OK)
      return res;
    geometry =
        dynamic_cast<scene::Geometry *>(ident->resolve(snapshot->target));
    if ((reason = check())) {
      if (std::strcmp(reason, "array_mapped") == 0) {
        res.outcome = Outcome::BUSY;
        res.reason = reason;
      } else {
        markConflicting(request.targetBatchRef, ctx);
        res.outcome = Outcome::CONFLICT;
      }
      return res;
    }
    if (!transaction->isCurrent(*geometry)) {
      res.outcome = Outcome::BUSY;
      res.reason = "prepared_state_changed";
      return res;
    }
    for (std::size_t i = 0; i < preparedIndices.size(); ++i) {
      auto *array = preparedIndices[i].first.lock()->array;
      if (array->index() != preparedIndices[i].second) {
        res.outcome = Outcome::BUSY;
        res.reason = "dependency_relocated";
        return res;
      }
      oldArrays[i] = array;
    }
    transaction->commit(*geometry);
    res.committed = true;
    mesh::MeshCommitAccess::committed(*ident, *geometry);
    m_edits.restorations().setImageState(
        request.targetBatchRef, edit::RestorationStore::ImageState::RESTORED);
    m_edits.restorations().release(request.targetBatchRef);
    if (ctx.markRestored)
      ctx.markRestored(
          request.targetBatchRef.launchId, request.targetBatchRef.batchId);
    if (mesh::notify(
            scene, *geometry, *transaction, snapshot->bindings, {}, oldArrays))
      res.outcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
    res.sceneRevision = ident->sceneRevision();
    const auto &revision = *res.sceneRevision;
    char suffix[256];
    std::snprintf(suffix,
        sizeof(suffix),
        ",\"scene_revision\":\"%llu.%llu.%llu.%llu\"}",
        (unsigned long long)revision.launch.activation,
        (unsigned long long)revision.launch.nonce,
        (unsigned long long)revision.domain,
        (unsigned long long)revision.value);
    res.imageReceipt.append(suffix);
    try {
      hookPending = true;
      m_edits.syncStates().beginHook(request.scene);
    } catch (...) {
      res.outcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
    }
    return res;
  }

  if (record->image) {
    const auto &snapshot = record->image;
    auto *array = ident && ident->scene() == &scene
        ? dynamic_cast<scene::Array *>(ident->resolve(snapshot->handle))
        : nullptr;
    if (snapshot->registration.expired() || snapshot->scene != &scene || !array
        || !array->isImage()) {
      markConflicting(request.targetBatchRef, ctx);
      res.outcome = Outcome::CONFLICT;
      return res;
    }
    if (array->isMapped()) {
      res.outcome = Outcome::BUSY;
      res.reason = "image_mapped";
      return res;
    }
    if (array->dataWriteSequence() != snapshot->completedWrite
        || array->size() * array->elementSize() != snapshot->bytes.size()) {
      markConflicting(request.targetBatchRef, ctx);
      res.outcome = Outcome::CONFLICT;
      return res;
    }
    try {
      if (fault && (*fault)())
        throw std::bad_alloc();
      res.ownership = operations::ResultOwnership(snapshot->reserveResult);
      res.wireLease = snapshot->reserveResult();
      if (!res.wireLease)
        throw std::bad_alloc();
      res.imageVersions.reserve(1);
      res.imageVersions.push_back({snapshot->handle, {}});
      res.imageVersions[0].dataVersion.reserve(256);
      res.imageReceipt = "{\"target_batch_ref\":"
          + edit::detail::batchRefJson(request.targetBatchRef)
          + ",\"committed\":true,\"versions\":[],\"node_versions\":[],"
            "\"image_versions\":[{\"handle\":\""
          + edit::detail::hex64(snapshot->handle.domain)
          + edit::detail::hex64(snapshot->handle.serial)
          + "\",\"data_version\":\"";
      res.imageReceipt.reserve(res.imageReceipt.size() + 512);
      if (!ctx.prepareActivityResult
          || !ctx.prepareActivityResult(
              res.imageReceipt.size() + 512, snapshot->reserveResult))
        throw std::bad_alloc();
      m_edits.syncStates().prepareScene(request.scene);
    } catch (const std::bad_alloc &) {
      res.outcome = Outcome::BUSY;
      res.reason = "resource_limit";
      return res;
    }
    if (ctx.beforeWrite) {
      res.outcome = ctx.beforeWrite();
      if (res.outcome != Outcome::OK)
        return res;
    }
    void *destination = nullptr;
    try {
      destination = array->map();
    } catch (...) {
      // map() reserves before notification, but exposes no write pointer if
      // that notification fails. The value stays burned, not completed.
      res.outcome = Outcome::BUSY;
      res.reason = "resource_limit";
      return res;
    }
    if (!destination) {
      res.outcome = Outcome::BUSY;
      res.reason = "resource_limit";
      return res;
    }
    std::memcpy(destination, snapshot->bytes.data(), snapshot->bytes.size());
    res.committed = true;
    try {
      array->unmap();
    } catch (...) {
      res.outcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
    }
    m_edits.restorations().setImageState(
        request.targetBatchRef, edit::RestorationStore::ImageState::RESTORED);
    m_edits.restorations().release(request.targetBatchRef);
    if (ctx.markRestored)
      ctx.markRestored(
          request.targetBatchRef.launchId, request.targetBatchRef.batchId);
    const auto &handle = snapshot->handle;
    char token[192];
    std::snprintf(token,
        sizeof(token),
        "image:%llu.%llu.%llu.%llu.%llu",
        (unsigned long long)handle.launch.activation,
        (unsigned long long)handle.launch.nonce,
        (unsigned long long)handle.domain,
        (unsigned long long)handle.serial,
        (unsigned long long)*array->dataWriteSequence());
    res.imageVersions[0].dataVersion.assign(token);
    res.sceneRevision = ident->sceneRevision();
    res.imageReceipt.append(token);
    const auto &revision = *res.sceneRevision;
    char suffix[256];
    std::snprintf(suffix,
        sizeof(suffix),
        "\"}],\"scene_revision\":\"%llu.%llu.%llu.%llu\",\"restored\":true}",
        (unsigned long long)revision.launch.activation,
        (unsigned long long)revision.launch.nonce,
        (unsigned long long)revision.domain,
        (unsigned long long)revision.value);
    res.imageReceipt.append(suffix);
    try {
      hookPending = true;
      m_edits.syncStates().beginHook(request.scene);
    } catch (...) {
      res.outcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
    }
    return res;
  }

  if (!ident || ident->scene() != &scene) {
    res.outcome = Outcome::INVALID_REQUEST;
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
    scene::Parameter *parameter;
    core::Any value;
  };
  std::vector<Planned> plan;
  std::vector<scene::Object *> objects;
  std::vector<const scene::Layer *> layers;
  std::vector<scene::LayerNodeRef> nodes;
  try {
    if (fault && (*fault)())
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
      auto *parameter = o ? o->parameter(core::Token(c.parameter)) : nullptr;
      if (!parameter || !c.before.valid()) {
        markConflicting(request.targetBatchRef, ctx);
        res.outcome = Outcome::CONFLICT; // missing/incompatible reference
        return res;
      }
      plan.push_back({o, parameter, c.before});
      if (std::find(objects.begin(), objects.end(), o) == objects.end())
        objects.push_back(o);
    }
    for (auto *object : objects)
      object->reserveParameterBatch(plan.size());
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
    p.parameter->setPreparedValue(std::move(p.value));
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
    // Every restored ancestor is now final. Revisit the bounded validated
    // chain without allocating, rather than returning pre-restore history.
    v.dependencies = *ident->beginNodeDependencies(v.expected.node);
    while (v.dependencies.ancestorSlot)
      ident->advanceNodeDependencies(v.dependencies);
    res.nodeVersions.push_back(v);
  }
  res.committed = true;
  res.sceneRevision = ident->sceneRevision();
  hookPending = true;
  m_edits.syncStates().beginHook(request.scene);
  return res;
}

} // namespace vsr::integration::restore

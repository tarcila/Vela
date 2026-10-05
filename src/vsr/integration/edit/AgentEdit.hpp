// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Header-only: the vsr_integration library globs only top-level sources (see
// ticket 19).

// vsr_integration
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/identity/Identity.hpp"
#include "vsr/integration/operations/Operations.hpp"
#include "vsr/integration/session/SessionAuthority.hpp"
#include "vsr/integration/sync/SceneSync.hpp"
#include "vsr/integration/values/EditableValues.hpp"
// std
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace vsr::integration::edit {

/*
 * Agent Edit Batches (spec §10): one explicitly submitted group of up to 64
 * edits that commits in full or leaves the scene unchanged.
 *
 * An edit is scene work admitted through the OperationEngine. At the host
 * safe point it validates every target, value and expected parameter version
 * together, prepares all allocations, the Batch Reference and the restoration
 * record, and only then mutates. All parameter values are stored before any
 * update signal is flushed, so observers only ever see the complete batch.
 * The host's normal-synchronization hook then runs exactly once; terminal
 * success needs its synchronous acknowledgement.
 *
 * Threading: submit*() and queries are thread-safe; execution happens on the
 * servicing thread. The service must outlive every service() call that may
 * still run one of its steps, and every SceneIdentity it was given.
 */

using identity::ObjectHandle;
using identity::ParameterVersionToken;
using identity::SceneIdentity;
using identity::SceneRevision;
using operations::Admission;
using operations::LocalAdmission;
using operations::LocalRef;
using operations::OperationEngine;
using operations::OperationRef;
using operations::Outcome;

constexpr std::size_t MAX_BATCH_EDITS = 64;

enum class TargetKind
{
  OBJECT,
  NODE
};

struct NodeVersion
{
  identity::NodeEditVersionToken expected;
  identity::NodeDependencyWalk dependencies;
};

struct EditEntry
{
  // Names the object and the version the client inspected.
  ParameterVersionToken expected;
  std::string parameter;
  JsonValue value;
  TargetKind targetKind{TargetKind::OBJECT};
  identity::NodeEditVersionToken nodeExpected;
  identity::NodeDependencyWalk nodeDependencies;
  std::string space{"local"}; // position only
  bool hasAncestryVersion{false};
};

struct EditBatch
{
  SceneId scene{INVALID_SCENE_ID};
  std::vector<EditEntry> edits;
};

// Wire `batch_ref`: {launch_id, batch_id}.
struct BatchRef
{
  std::string launchId;
  std::string batchId; // 32 lowercase hex, unique within the launch

  friend bool operator==(const BatchRef &a, const BatchRef &b);
};

// Scene synchronization state lives in the sync module.
using sync::SyncState;
using sync::syncStateName;

struct EditResult
{
  Outcome outcome{Outcome::OK};
  bool committed{false};
  std::optional<BatchRef> batchRef; // set on every commit
  std::optional<SceneRevision> sceneRevision; // resulting, on commit
  std::vector<ParameterVersionToken> versions; // resulting, per edited object
  std::vector<NodeVersion> nodeVersions;
  // On a value rejection: the offending entry and its error.
  std::optional<std::size_t> failedEntry;
  ValueError valueError{ValueError::NONE};
};

// Wire detail shared by edit and restore results (addendum 04 §6, §8).
namespace detail {

inline const char *valueErrorName(ValueError e)
{
  switch (e) {
  case ValueError::UNSUPPORTED_PARAMETER:
    return "unsupported_parameter";
  case ValueError::INCOMPATIBLE_TYPE:
    return "incompatible_type";
  case ValueError::NON_FINITE:
    return "non_finite";
  case ValueError::OUT_OF_DOMAIN:
    return "out_of_domain";
  case ValueError::UNSUPPORTED_NODE_KIND:
    return "unsupported_node_kind";
  case ValueError::UNFAITHFUL_SRT:
    return "unfaithful_srt";
  case ValueError::NON_AFFINE:
    return "non_affine";
  case ValueError::SINGULAR_PARENT:
    return "singular_parent";
  case ValueError::NONE:
    break;
  }
  return "none";
}

inline std::string hex64(std::uint64_t v)
{
  static const char *d = "0123456789abcdef";
  std::string s(16, '0');
  for (int i = 15; i >= 0; i--, v >>= 4)
    s[i] = d[v & 15];
  return s;
}

inline std::string batchRefJson(const BatchRef &r)
{
  return "{\"launch_id\":\"" + r.launchId + "\",\"batch_id\":\"" + r.batchId
      + "\"}";
}

// `"committed":…,"scene_revision":"a.n.d.v","versions":[{handle,version}]`
// members (no braces). `handle` matches vsr_inspect's 32-hex form.
inline std::string commitDetailJson(bool committed,
    const std::optional<SceneRevision> &revision,
    const std::vector<ParameterVersionToken> &versions,
    const std::vector<NodeVersion> &nodes = {})
{
  std::string j =
      std::string("\"committed\":") + (committed ? "true" : "false");
  if (revision)
    j += ",\"scene_revision\":\"" + std::to_string(revision->launch.activation)
        + "." + std::to_string(revision->launch.nonce) + "."
        + std::to_string(revision->domain) + "."
        + std::to_string(revision->value) + "\"";
  if (committed) {
    j += ",\"versions\":[";
    for (std::size_t i = 0; i < versions.size(); i++)
      j += std::string(i ? "," : "") + "{\"handle\":\""
          + hex64(versions[i].object.domain) + hex64(versions[i].object.serial)
          + "\",\"version\":" + std::to_string(versions[i].version) + "}";
    j += "]";
    if (!nodes.empty()) {
      j += ",\"node_versions\":[";
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto &n = nodes[i];
        const auto h =
            hex64(n.expected.node.domain) + hex64(n.expected.node.serial);
        const auto l = hex64(n.dependencies.layer.domain)
            + hex64(n.dependencies.layer.serial);
        j += std::string(i ? "," : "") + "{\"handle\":\"" + h
            + "\",\"version\":\"ne:" + h + ":"
            + std::to_string(n.expected.version)
            + "\",\"structural_version\":\"ns:" + h + ":" + l + ":"
            + std::to_string(n.dependencies.structuralVersion)
            + "\",\"ancestry_version\":\"na:" + h + ":" + l + ":"
            + std::to_string(n.dependencies.structuralVersion) + ":"
            + std::to_string(n.dependencies.transformVersion) + "\"}";
      }
      j += "]";
    }
  }
  return j;
}

inline ValueError prepareSRTField(const scene::LayerNodeData &data,
    const std::string &field,
    const JsonValue &value,
    core::math::mat3 &proposed)
{
  if (!data.isTransform())
    return ValueError::UNSUPPORTED_NODE_KIND;
  const auto cached = data.getTransformSRT();
  for (int c = 0; c < 3; ++c)
    for (int r = 0; r < 3; ++r)
      if (!std::isfinite(cached[c][r]))
        return ValueError::NON_FINITE;
  const scene::LayerNodeData rebuilt(nullptr, cached);
  const auto reconstruction = rebuilt.getTransform();
  const auto original = data.getTransform();
  for (int c = 0; c < 3; ++c)
    for (int r = 0; r < 3; ++r) {
      const double component = original[c][r];
      if (!std::isfinite(component) || !std::isfinite(reconstruction[c][r])
          || std::abs(component - double(reconstruction[c][r]))
              > 1e-5 * std::max(1., std::abs(component)))
        return ValueError::UNFAITHFUL_SRT;
    }
  const bool rotation = field == "rotation";
  if (rotation ? value.kind != JsonKind::OBJECT || value.members.size() != 3
               : value.kind != JsonKind::ARRAY || value.elements.size() != 3)
    return ValueError::INCOMPATIBLE_TYPE;
  const char *names[] = {"azimuth", "elevation", "roll"};
  const double limit = std::numeric_limits<float>::max();
  for (int i = 0; i < 3; ++i) {
    const JsonValue *component = nullptr;
    if (rotation) {
      for (const auto &member : value.members)
        if (member.first == names[i]) {
          if (component)
            return ValueError::INCOMPATIBLE_TYPE;
          component = &member.second;
        }
      if (!component)
        return ValueError::INCOMPATIBLE_TYPE;
    } else
      component = &value.elements[i];
    const auto converted = convertScalar(*component, {-limit, limit});
    if (!converted.ok())
      return converted.error;
    proposed[rotation ? 1 : 0][i] = converted.value;
  }
  return ValueError::NONE;
}

inline ValueError prepareMatrix(const JsonValue &value, core::math::mat4 &out)
{
  if (value.kind != JsonKind::ARRAY || value.elements.size() != 4)
    return ValueError::INCOMPATIBLE_TYPE;
  for (std::size_t row = 0; row < 4; ++row) {
    const auto &r = value.elements[row];
    if (r.kind != JsonKind::ARRAY || r.elements.size() != 4)
      return ValueError::INCOMPATIBLE_TYPE;
    for (std::size_t col = 0; col < 4; ++col) {
      const auto &v = r.elements[col];
      const auto converted = convertScalar(v,
          {-double(std::numeric_limits<float>::max()),
              double(std::numeric_limits<float>::max())});
      if (!converted.ok())
        return converted.error;
      if (row == 3 && v.number != (col == 3 ? 1.0 : 0.0))
        return ValueError::NON_AFFINE;
      out[col][row] = converted.value;
    }
  }
  return ValueError::NONE;
}

inline ValueError preparePositionField(const scene::LayerNodeData &data,
    const JsonValue &value,
    core::math::float3 &position)
{
  if (!data.isTransform())
    return ValueError::UNSUPPORTED_NODE_KIND;
  if (value.kind != JsonKind::ARRAY || value.elements.size() != 3)
    return ValueError::INCOMPATIBLE_TYPE;
  const double limit = std::numeric_limits<float>::max();
  for (int c = 0; c < 3; ++c) {
    const auto component = convertScalar(value.elements[c], {-limit, limit});
    if (!component.ok())
      return component.error;
    position[c] = component.value;
  }
  return ValueError::NONE;
}

inline ValueError prepareTransformField(const scene::LayerNodeData &data,
    const JsonValue &value,
    core::math::mat4 &matrix,
    core::math::mat3 &srt)
{
  if (!data.isTransform())
    return ValueError::UNSUPPORTED_NODE_KIND;
  const auto error = prepareMatrix(value, matrix);
  if (error != ValueError::NONE)
    return error;
  // Decompose only the cache, never reconstruct the authoritative matrix.
  const scene::LayerNodeData prepared(nullptr, matrix);
  srt = prepared.getTransformSRT();
  return ValueError::NONE;
}

using Affine = std::array<std::array<double, 4>, 4>;

inline Affine identityAffine()
{
  Affine result{};
  for (int i = 0; i < 4; ++i)
    result[i][i] = 1.;
  return result;
}

inline ValueError appendAffine(Affine &parent, const core::math::mat4 &matrix)
{
  Affine product{};
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      if (!std::isfinite(matrix[col][row]))
        return ValueError::NON_FINITE;
      if (row == 3 && matrix[col][row] != (col == 3 ? 1.f : 0.f))
        return ValueError::NON_AFFINE;
      for (int k = 0; k < 4; ++k)
        product[row][col] += parent[row][k] * double(matrix[col][k]);
      if (!std::isfinite(product[row][col]))
        return ValueError::NON_FINITE;
      if (std::abs(product[row][col]) > std::numeric_limits<float>::max())
        return ValueError::OUT_OF_DOMAIN;
    }
  }
  parent = product;
  return ValueError::NONE;
}

// One bounded walk for the whole request; no retained ancestor snapshots.
inline Outcome nodeDependencies(SceneIdentity &ids,
    const identity::NodeHandle &h,
    identity::NodeDependencyWalk &out,
    std::size_t &remaining,
    std::vector<scene::LayerNodeRef> *ancestors = nullptr)
{
  auto walk = ids.beginNodeDependencies(h);
  if (!walk)
    return Outcome::CONFLICT;
  while (walk->ancestorSlot) {
    if (!remaining)
      return Outcome::BUSY;
    --remaining;
    if (ancestors)
      ancestors->push_back(
          ids.resolve(walk->layer)->at(walk->ancestorSlot - 1));
    if (!ids.advanceNodeDependencies(*walk))
      return Outcome::CONFLICT;
  }
  out = *walk;
  return Outcome::OK;
}

} // namespace detail

// Ephemeral safe-point preparation. References never enter retained previews.
struct Planned
{
  scene::Object *object;
  const EditEntry *entry;
  core::Any value;
};

struct PlannedNode
{
  scene::LayerNodeRef node;
  const EditEntry *entry;
  identity::NodeDependencyWalk dependencies;
  bool changedEnabled{false};
  bool enabled{true};
  bool changedTransform{false};
  bool changedSRT{false};
  core::math::mat4 matrix{core::math::IDENTITY_MAT4};
  core::math::mat3 srt{};
  std::vector<scene::LayerNodeRef> ancestors;
  const EditEntry *worldEntry{nullptr};
  std::size_t worldIndex{0};
  bool worldPrepared{false};
};

struct PreparedBatch
{
  SceneIdentity *identity{nullptr};
  std::vector<PlannedNode> nodes;
  std::vector<const scene::Layer *> layers;
  std::vector<Planned> objects;
};

// One stored parameter change, enough to restore it (ticket 09).
struct Change
{
  ObjectHandle object;
  std::string parameter;
  core::Any before;
  core::Any after;
};

struct NodeChange
{
  NodeVersion resulting;
  bool beforeEnabled{true};
  bool changedEnabled{false};
  bool changedTransform{false};
  bool requiresAncestry{false};
  core::math::mat4 beforeMatrix{core::math::IDENTITY_MAT4};
  core::math::mat3 beforeSRT{};
};

struct RestorationRecord
{
  BatchRef ref;
  SceneId scene{INVALID_SCENE_ID};
  std::optional<OperationRef> provenance; // external work
  std::optional<LocalRef> localProvenance; // host-local work
  std::vector<Change> changes;
  std::vector<ParameterVersionToken> resultingVersions;
  std::vector<NodeChange> nodeChanges;
};

// Bounded restorable-batch store (spec §5: 32 by default). Publishing past
// capacity visibly expires the oldest record; reserved storage for active
// work is never evicted.
class RestorationStore
{
 public:
  explicit RestorationStore(std::size_t capacity = 32);

  // Reserve storage for one record before mutation; false only when active
  // reservations alone fill the store.
  bool reserve();
  void cancelReservation();
  void publish(RestorationRecord record); // consumes one reservation

  std::optional<RestorationRecord> find(const BatchRef &ref) const;
  // True once the record was evicted by the retention budget.
  bool expired(const BatchRef &ref) const;
  // Restoration commit releases the record's restorable position/payload.
  void release(const BatchRef &ref);
  // Visibly expires every record `retained` rejects; keeps this store in
  // step with an authoritative retention budget (the ActivityLog).
  void expireUnless(const std::function<bool(const BatchRef &)> &retained);
  std::size_t size() const;
  std::size_t capacity() const;

 private:
  mutable std::mutex m_mutex;
  std::size_t m_capacity;
  std::size_t m_reserved{0};
  std::map<std::string, RestorationRecord> m_records;
  std::vector<std::string> m_order; // oldest first
  std::set<std::string> m_expired;
  std::vector<std::string> m_expiredOrder; // bounded like activity entries

  void expireLocked(std::string id); // by value: may alias m_order
};

// Bytes charged to the activity budget for a restoration record's payload.
std::size_t restorationBytes(const RestorationRecord &record);

// Test seam for preparation failures; return true to fail that stage.
enum class FaultStage
{
  PREPARE,
  ALLOCATE,
  RESERVE_RESTORATION
};
using FaultInjector = std::function<bool(FaultStage)>;

class EditService
{
 public:
  EditService(Integration &integration,
      OperationEngine &engine,
      std::string launchId,
      std::size_t restorationCapacity = 32);
  ~EditService();
  EditService(const EditService &) = delete;
  EditService &operator=(const EditService &) = delete;

  // Host: associate a registered scene with its identity observer.
  void attachScene(SceneId scene, SceneIdentity &identity);
  void setFaultInjector(FaultInjector injector);

  // External edit; envelope errors reject before admission.
  Admission submit(const operations::Credentials &credentials,
      const OperationRef &ref,
      EditBatch batch);
  // Host-local edit under host authority.
  LocalAdmission submitLocal(EditBatch batch);

  std::optional<EditResult> result(const OperationRef &ref) const;
  std::optional<EditResult> resultLocal(LocalRef ref) const;

  SyncState syncState(SceneId scene) const;
  // Shared with sync::SyncService for Resynchronize and view confirmation.
  sync::SyncStates &syncStates();
  // Runs the normal-synchronization hook once for a commit made outside
  // this service (Batch Restoration) after syncStates().beginHook().
  Outcome synchronizeCommitted(SceneId scene);
  RestorationStore &restorations();
  const std::string &launchId() const;
  std::weak_ptr<const void> lifetime() const;

  static std::string canonicalArguments(const EditBatch &batch);

  // Shared by low-level commit and selection preview. Safe-point only;
  // caller owns the batch and keeps its entries alive through preparation.
  EditResult prepare(const EditBatch &batch,
      scene::Scene &scene,
      PreparedBatch &prepared,
      std::size_t &remaining);
  // Exact safe-point plan already validated by the selection service. Shares
  // the low-level allocation, atomic write, receipt and restoration phase.
  EditResult commitPrepared(SceneId scene,
      PreparedBatch &prepared,
      operations::StepContext &ctx,
      const std::function<Outcome()> &beforeWrite = {});

 private:
  using Slot = std::shared_ptr<std::optional<EditResult>>;

  static bool validEnvelope(const EditBatch &batch);
  operations::Request makeRequest(EditBatch batch, Slot slot);
  EditResult commit(const EditBatch &batch, operations::StepContext &ctx);
  Outcome synchronize(SceneId scene);
  bool fault(FaultStage stage) const;
  std::optional<std::string> newBatchId();

  Integration &m_integration;
  OperationEngine &m_engine;
  std::string m_launchId;
  RestorationStore m_store;
  FaultInjector m_fault;
  std::shared_ptr<bool> m_alive;

  mutable std::mutex m_mutex;
  std::map<SceneId, SceneIdentity *> m_identities;
  sync::SyncStates m_sync;
  std::set<std::string> m_issued; // every batch_id ever issued
  std::map<std::pair<std::string, std::uint64_t>, Slot> m_results;
  std::map<std::uint64_t, Slot> m_localResults;
};

///////////////////////////////////////////////////////////////////////////////
// Inlined definitions ////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

inline bool operator==(const BatchRef &a, const BatchRef &b)
{
  return a.launchId == b.launchId && a.batchId == b.batchId;
}

// RestorationStore //

inline RestorationStore::RestorationStore(std::size_t capacity)
    : m_capacity(capacity)
{}

inline bool RestorationStore::reserve()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_reserved >= m_capacity)
    return false;
  ++m_reserved;
  return true;
}

inline void RestorationStore::cancelReservation()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_reserved)
    --m_reserved;
}

inline void RestorationStore::publish(RestorationRecord record)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_reserved)
    --m_reserved;
  auto id = record.ref.batchId;
  while (!m_order.empty() && m_records.size() + m_reserved >= m_capacity)
    expireLocked(m_order.front());
  m_order.push_back(id);
  m_records[id] = std::move(record);
}

inline void RestorationStore::expireLocked(std::string id)
{
  for (auto o = m_order.begin(); o != m_order.end(); ++o)
    if (*o == id) {
      m_order.erase(o);
      break;
    }
  if (m_records.erase(id) && m_expired.insert(id).second) {
    m_expiredOrder.push_back(id);
    if (m_expiredOrder.size() > 1024) {
      m_expired.erase(m_expiredOrder.front());
      m_expiredOrder.erase(m_expiredOrder.begin());
    }
  }
}

inline void RestorationStore::expireUnless(
    const std::function<bool(const BatchRef &)> &retained)
{
  std::vector<BatchRef> refs;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &[id, r] : m_records)
      refs.push_back(r.ref);
  }
  // Evaluated unlocked: `retained` may take other locks.
  std::vector<std::string> gone;
  for (auto &ref : refs)
    if (!retained(ref))
      gone.push_back(ref.batchId);
  std::lock_guard<std::mutex> lock(m_mutex);
  for (auto &id : gone)
    expireLocked(id);
}

inline std::size_t restorationBytes(const RestorationRecord &record)
{
  auto anyBytes = [](const core::Any &a) {
    return a.type() == ANARI_STRING ? a.getString().size() : std::size_t(0);
  };
  std::size_t n = sizeof(RestorationRecord) + record.ref.launchId.size()
      + record.ref.batchId.size()
      + record.resultingVersions.capacity() * sizeof(ParameterVersionToken);
  n += record.nodeChanges.capacity() * sizeof(NodeChange);
  for (auto &c : record.changes)
    n += sizeof(Change) + c.parameter.size() + anyBytes(c.before)
        + anyBytes(c.after);
  return n;
}

inline void RestorationStore::release(const BatchRef &ref)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_records.find(ref.batchId);
  if (it == m_records.end() || it->second.ref.launchId != ref.launchId)
    return;
  m_records.erase(it);
  for (auto o = m_order.begin(); o != m_order.end(); ++o)
    if (*o == ref.batchId) {
      m_order.erase(o);
      break;
    }
}

inline bool RestorationStore::expired(const BatchRef &ref) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_expired.count(ref.batchId) != 0;
}

inline std::optional<RestorationRecord> RestorationStore::find(
    const BatchRef &ref) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_records.find(ref.batchId);
  if (it == m_records.end() || it->second.ref.launchId != ref.launchId)
    return std::nullopt;
  return it->second;
}

inline std::size_t RestorationStore::size() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_records.size();
}

inline std::size_t RestorationStore::capacity() const
{
  return m_capacity;
}

// EditService //

inline EditService::EditService(Integration &integration,
    OperationEngine &engine,
    std::string launchId,
    std::size_t restorationCapacity)
    : m_integration(integration),
      m_engine(engine),
      m_launchId(std::move(launchId)),
      m_store(restorationCapacity),
      m_alive(std::make_shared<bool>(true))
{}

inline EditService::~EditService()
{
  m_alive.reset();
}

inline void EditService::attachScene(SceneId scene, SceneIdentity &identity)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_identities[scene] = &identity;
}

inline void EditService::setFaultInjector(FaultInjector injector)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_fault = std::move(injector);
}

inline bool EditService::fault(FaultStage stage) const
{
  FaultInjector f;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    f = m_fault;
  }
  return f && f(stage);
}

inline SyncState EditService::syncState(SceneId scene) const
{
  return m_sync.state(scene);
}

inline sync::SyncStates &EditService::syncStates()
{
  return m_sync;
}

inline RestorationStore &EditService::restorations()
{
  return m_store;
}

inline const std::string &EditService::launchId() const
{
  return m_launchId;
}

inline bool EditService::validEnvelope(const EditBatch &batch)
{
  if (batch.edits.empty() || batch.edits.size() > MAX_BATCH_EDITS)
    return false;
  std::set<std::tuple<TargetKind, std::uint64_t, std::uint64_t, std::string>>
      seen;
  std::map<std::pair<std::uint64_t, std::uint64_t>,
      identity::NodeEditVersionToken>
      expectedNodes;
  for (auto &e : batch.edits) {
    if (e.parameter.empty())
      return false;
    const auto domain = e.targetKind == TargetKind::NODE
        ? e.nodeExpected.node.domain
        : e.expected.object.domain;
    const auto serial = e.targetKind == TargetKind::NODE
        ? e.nodeExpected.node.serial
        : e.expected.object.serial;
    if (e.targetKind == TargetKind::NODE) {
      if (!e.nodeExpected.node.valid() || !e.nodeExpected.version
          || e.nodeDependencies.node != e.nodeExpected.node
          || !e.nodeDependencies.layer.valid()
          || e.nodeDependencies.ancestorSlot
          || !e.nodeDependencies.structuralVersion
          || (e.parameter != "enabled" && e.parameter != "position"
              && e.parameter != "transform" && e.parameter != "rotation"
              && e.parameter != "scale")
          || (e.space != "local"
              && (e.parameter != "position" || e.space != "world"
                  || !e.hasAncestryVersion))
          || (e.parameter == "enabled" && e.value.kind != JsonKind::BOOLEAN))
        return false;
      auto inserted =
          expectedNodes.emplace(std::make_pair(domain, serial), e.nodeExpected);
      if (!inserted.second
          && inserted.first->second.version != e.nodeExpected.version)
        return false;
      if (e.parameter == "transform") {
        for (const auto *field : {"position", "rotation", "scale"})
          if (seen.count({TargetKind::NODE, domain, serial, field}))
            return false;
      } else if (e.parameter != "enabled"
          && seen.count({TargetKind::NODE, domain, serial, "transform"}))
        return false;
    } else if (e.targetKind != TargetKind::OBJECT || !e.expected.object.valid())
      return false;
    if (!seen.insert({e.targetKind, domain, serial, e.parameter}).second)
      return false;
  }
  return true;
}

namespace detail {

// Length-prefixed so string content can never collide with the framing.
inline void appendText(std::string &out, const std::string &t)
{
  out += std::to_string(t.size());
  out += ':';
  out += t;
}

// Encodes the full content of every JSON kind: numbers by their binary64
// bits, strings and object keys length-prefixed, objects in sorted key order.
inline void appendValue(std::string &out, const JsonValue &v)
{
  char buf[40];
  switch (v.kind) {
  case JsonKind::NUMBER: {
    // Authoritative binary64 input; -0 normalizes to 0.
    const double d = v.number == 0.0 ? 0.0 : v.number;
    std::uint64_t bits;
    std::memcpy(&bits, &d, sizeof(bits));
    std::snprintf(buf, sizeof(buf), "n%016llx", (unsigned long long)bits);
    out += buf;
    break;
  }
  case JsonKind::ARRAY:
    out += '[';
    for (auto &e : v.elements) {
      appendValue(out, e);
      out += ',';
    }
    out += ']';
    break;
  case JsonKind::BOOLEAN:
    out += v.boolean ? "t" : "f";
    break;
  case JsonKind::NULL_VALUE:
    out += 'z';
    break;
  case JsonKind::STRING:
    out += 's';
    appendText(out, v.text);
    break;
  case JsonKind::OBJECT: {
    std::vector<const std::pair<std::string, JsonValue> *> sorted;
    for (auto &m : v.members)
      sorted.push_back(&m);
    std::sort(sorted.begin(), sorted.end(), [](auto *a, auto *b) {
      return a->first < b->first;
    });
    out += '{';
    for (auto *m : sorted) {
      appendText(out, m->first);
      out += '=';
      appendValue(out, m->second);
      out += ',';
    }
    out += '}';
    break;
  }
  default:
    std::snprintf(buf, sizeof(buf), "k%d", int(v.kind));
    out += buf;
    break;
  }
}

} // namespace detail

inline std::string EditService::canonicalArguments(const EditBatch &batch)
{
  std::string s = "edit;scene=" + std::to_string(batch.scene) + ";";
  for (auto &e : batch.edits) {
    if (e.targetKind == TargetKind::NODE) {
      const auto &h = e.nodeExpected.node;
      const auto &d = e.nodeDependencies;
      s += "node:" + std::to_string(h.launch.activation) + "/"
          + std::to_string(h.launch.nonce) + "/" + std::to_string(h.domain)
          + "/" + std::to_string(h.serial) + "@"
          + std::to_string(e.nodeExpected.version) + ":"
          + std::to_string(d.layer.domain) + "/"
          + std::to_string(d.layer.serial) + "/"
          + std::to_string(d.structuralVersion) + ":"
          + (e.space == "world" ? std::to_string(d.transformVersion) : "") + ":"
          + e.parameter + "=";
      if (e.parameter == "position")
        detail::appendText(s, e.space);
      detail::appendValue(s, e.value);
      s += ";";
      continue;
    }
    const auto &h = e.expected.object;
    s += std::to_string(h.launch.activation) + "/"
        + std::to_string(h.launch.nonce) + "/" + std::to_string(h.domain) + "/"
        + std::to_string(h.serial) + "@" + std::to_string(e.expected.version)
        + ":" + e.parameter + "=";
    detail::appendValue(s, e.value);
    s += ";";
  }
  return s;
}

inline std::optional<std::string> EditService::newBatchId()
{
  for (int attempt = 0; attempt < 8; attempt++) {
    std::uint8_t bytes[16];
    if (!session::secureRandom(bytes, sizeof(bytes)))
      return std::nullopt;
    auto id = session::toLowerHex(bytes, sizeof(bytes));
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_issued.insert(id).second)
      return id;
  }
  return std::nullopt;
}

inline operations::Request EditService::makeRequest(EditBatch batch, Slot slot)
{
  operations::Request r;
  r.kind = "edit";
  r.target = session::Target::scene(batch.scene);
  r.capability = session::Capability::EDIT;
  r.canonicalArguments = canonicalArguments(batch);
  std::weak_ptr<bool> alive = m_alive;
  auto shared = std::make_shared<EditBatch>(std::move(batch));
  auto committed = std::make_shared<std::optional<EditResult>>();
  r.executor = [this, alive, shared, slot, committed](
                   operations::StepContext &ctx) {
    operations::Step step;
    if (!alive.lock())
      return step;
    if (!*committed) {
      if (ctx.abandoned)
        return step; // never mutate for abandoned work
      EditResult res = commit(*shared, ctx);
      if (!res.committed) {
        step.outcome = res.outcome;
        if (res.failedEntry)
          step.result = "{\"committed\":false,\"failed_entry\":"
              + std::to_string(*res.failedEntry) + ",\"value_error\":\""
              + detail::valueErrorName(res.valueError) + "\"}";
        *slot = std::move(res);
        return step;
      }
      // Committed; the hook runs at the next safe point.
      *committed = std::move(res);
      step.committed = true;
      step.status = operations::Step::Status::SYNCHRONIZING;
      return step;
    }
    EditResult res = std::move(**committed);
    res.outcome = synchronize(shared->scene);
    step.outcome = res.outcome;
    step.committed = true;
    step.result = "{\"batch_ref\":" + detail::batchRefJson(*res.batchRef) + ","
        + detail::commitDetailJson(
            true, res.sceneRevision, res.versions, res.nodeVersions)
        + "}";
    *slot = std::move(res);
    return step;
  };
  return r;
}

inline Admission EditService::submit(const operations::Credentials &credentials,
    const OperationRef &ref,
    EditBatch batch)
{
  if (!validEnvelope(batch)) {
    // Replays of an admitted ID still go through the engine's comparison.
    Admission a;
    a.code = Outcome::INVALID_REQUEST;
    return a;
  }
  auto slot = std::make_shared<std::optional<EditResult>>();
  auto a =
      m_engine.submit(credentials, ref, makeRequest(std::move(batch), slot));
  if (a.code == Outcome::OK && !a.replayed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_results[{ref.sessionId, ref.requestId}] = slot;
  }
  return a;
}

inline LocalAdmission EditService::submitLocal(EditBatch batch)
{
  if (!validEnvelope(batch)) {
    LocalAdmission a;
    a.code = Outcome::INVALID_REQUEST;
    return a;
  }
  auto slot = std::make_shared<std::optional<EditResult>>();
  // Execution happens at a later safe point, after this map insertion.
  auto a = m_engine.submitLocal(makeRequest(std::move(batch), slot));
  if (a.code == Outcome::OK) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_localResults[a.ref.id] = slot;
  }
  return a;
}

inline std::optional<EditResult> EditService::result(
    const OperationRef &ref) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_results.find({ref.sessionId, ref.requestId});
  if (it == m_results.end())
    return std::nullopt;
  return *it->second;
}

inline std::optional<EditResult> EditService::resultLocal(LocalRef ref) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_localResults.find(ref.id);
  if (it == m_localResults.end())
    return std::nullopt;
  return *it->second;
}

inline EditResult EditService::prepare(const EditBatch &batch,
    scene::Scene &scene,
    PreparedBatch &prepared,
    std::size_t &remaining)
{
  EditResult res;
  SceneIdentity *ident = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_identities.find(batch.scene);
    if (it != m_identities.end())
      ident = it->second;
  }
  if (!ident || ident->scene() != &scene) {
    res.outcome = Outcome::INVALID_REQUEST;
    return res;
  }

  // 1. Validate every entry together; nothing mutates on failure. //

  prepared.identity = ident;
  auto &nodes = prepared.nodes;
  auto &layers = prepared.layers;
  auto &plan = prepared.objects;
  bool stale = false;
  std::size_t index = 0;
  try {
    nodes.reserve(batch.edits.size());
    layers.reserve(batch.edits.size());
    plan.reserve(batch.edits.size());
    for (auto &e : batch.edits) {
      if (e.targetKind == TargetKind::NODE) {
        // Envelope validation checks shape; live exposure/authority may
        // change after admission and must still reject before any mutation.
        if (!m_integration.nodeEditsExposed(batch.scene)) {
          res.outcome = Outcome::FORBIDDEN;
          return res;
        }
        identity::NodeDependencyWalk d;
        std::vector<scene::LayerNodeRef> ancestors;
        const auto status = detail::nodeDependencies(
            *ident, e.nodeExpected.node, d, remaining, &ancestors);
        if (status == Outcome::BUSY) {
          res.outcome = status;
          return res;
        }
        auto node = ident->resolve(e.nodeExpected.node);
        if (status != Outcome::OK || !ident->isCurrent(e.nodeExpected)
            || !d.matchesStructure(e.nodeDependencies)
            || (e.space == "world" && !d.matchesAncestry(e.nodeDependencies)))
          stale = true;
        else {
          auto it = std::find_if(nodes.begin(),
              nodes.end(),
              [&](const PlannedNode &p) { return p.node == node; });
          if (it == nodes.end()) {
            nodes.push_back({node, &e, d});
            it = std::prev(nodes.end());
            if (node->value().isTransform()) {
              it->matrix = node->value().getTransform();
              it->srt = node->value().getTransformSRT();
            }
            it->ancestors = std::move(ancestors);
          }
          if (e.parameter == "enabled") {
            it->changedEnabled = true;
            it->enabled = e.value.boolean;
          } else if (e.parameter == "position") {
            core::math::float3 position;
            const auto error =
                detail::preparePositionField(node->value(), e.value, position);
            if (error != ValueError::NONE) {
              res.outcome = Outcome::INVALID_REQUEST;
              res.failedEntry = index;
              res.valueError = error;
              return res;
            }
            it->changedTransform = true;
            it->matrix[3].x = position.x;
            it->matrix[3].y = position.y;
            it->matrix[3].z = position.z;
            it->srt[2] = position;
            if (e.space == "world") {
              it->worldEntry = &e;
              it->worldIndex = index;
            }
          } else if (e.parameter == "rotation" || e.parameter == "scale") {
            res.valueError = detail::prepareSRTField(
                node->value(), e.parameter, e.value, it->srt);
            if (res.valueError != ValueError::NONE) {
              res.outcome = Outcome::INVALID_REQUEST;
              res.failedEntry = index;
              return res;
            }
            it->changedTransform = true;
            it->changedSRT = true;
          } else {
            res.valueError = detail::prepareTransformField(
                node->value(), e.value, it->matrix, it->srt);
            if (res.valueError != ValueError::NONE) {
              res.outcome = Outcome::INVALID_REQUEST;
              res.failedEntry = index;
              return res;
            }
            it->changedTransform = true;
          }
          const auto *layer = node->value().layer();
          if (std::find(layers.begin(), layers.end(), layer) == layers.end())
            layers.push_back(layer);
        }
        ++index;
        continue;
      }
      auto *o = ident->resolve(e.expected.object);
      if (!o || !ident->isCurrent(e.expected)) {
        stale = true; // removed, replaced or changed since inspection
      } else {
        auto p = prepareEdit(*o, core::Token(e.parameter), e.value);
        if (!p.ok()) {
          res.outcome = Outcome::INVALID_REQUEST;
          res.failedEntry = index;
          res.valueError = p.error;
          return res;
        }
        plan.push_back({o, &e, p.value});
      }
      ++index;
    }
  } catch (const std::bad_alloc &) {
    res.outcome = Outcome::BUSY;
    return res;
  }
  if (stale) {
    res.outcome = Outcome::CONFLICT;
    return res;
  }

  // Finalize every grouped local linear state before world conversion, even
  // when a child appears before its proposed parent in the request.
  for (auto &p : nodes) {
    if (!p.changedSRT)
      continue;
    const scene::LayerNodeData rebuilt(nullptr, p.srt);
    const auto matrix = rebuilt.getTransform();
    for (int c = 0; c < 3; ++c)
      for (int r = 0; r < 3; ++r) {
        if (!std::isfinite(matrix[c][r])) {
          res.outcome = Outcome::INVALID_REQUEST;
          res.valueError = ValueError::NON_FINITE;
          return res;
        }
        p.matrix[c][r] = matrix[c][r];
      }
  }

  // Resolve world requests parent-first, using complete proposed states.
  // Saved chains are bounded by the same 4096-step preparation budget.
  auto prepareWorld = [&](auto &&self, PlannedNode &p) -> ValueError {
    if (!p.worldEntry || p.worldPrepared)
      return ValueError::NONE;
    auto parent = detail::identityAffine();
    for (auto a = p.ancestors.rbegin(); a != p.ancestors.rend(); ++a) {
      auto proposed = std::find_if(nodes.begin(),
          nodes.end(),
          [&](const PlannedNode &n) { return n.node == *a; });
      if (proposed != nodes.end()) {
        auto error = self(self, *proposed);
        if (error != ValueError::NONE)
          return error;
      }
      const auto &data = (*a)->value();
      if (anari::isArray(data.type()))
        return ValueError::UNSUPPORTED_NODE_KIND;
      if (!data.isTransform())
        continue;
      const auto matrix = proposed != nodes.end() && proposed->changedTransform
          ? proposed->matrix
          : data.getTransform();
      const auto error = detail::appendAffine(parent, matrix);
      if (error != ValueError::NONE)
        return error;
    }
    // Pivoted elimination solves the affine point conversion; no repair or
    // pseudoinverse is permitted. Keep binary64 request coordinates here.
    double system[3][4];
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < 3; ++col)
        system[row][col] = parent[row][col];
      system[row][3] =
          p.worldEntry->value.elements[row].number - parent[row][3];
    }
    for (int col = 0; col < 3; ++col) {
      int pivot = col;
      for (int row = col + 1; row < 3; ++row)
        if (std::abs(system[row][col]) > std::abs(system[pivot][col]))
          pivot = row;
      if (system[pivot][col] == 0)
        return ValueError::SINGULAR_PARENT;
      for (int k = 0; k < 4; ++k)
        std::swap(system[col][k], system[pivot][k]);
      const double divisor = system[col][col];
      for (int k = col; k < 4; ++k)
        system[col][k] /= divisor;
      for (int row = 0; row < 3; ++row) {
        if (row == col)
          continue;
        const double factor = system[row][col];
        for (int k = col; k < 4; ++k)
          system[row][k] -= factor * system[col][k];
      }
    }
    const double limit = std::numeric_limits<float>::max();
    for (int row = 0; row < 3; ++row) {
      auto value =
          convertScalar(JsonValue::fromNumber(system[row][3]), {-limit, limit});
      if (!value.ok())
        return value.error;
      p.matrix[3][row] = value.value;
      p.srt[2][row] = value.value;
    }
    p.worldPrepared = true;
    return ValueError::NONE;
  };
  for (auto &p : nodes) {
    auto error = prepareWorld(prepareWorld, p);
    if (error != ValueError::NONE) {
      res.outcome = Outcome::INVALID_REQUEST;
      res.failedEntry = p.worldIndex;
      res.valueError = error;
      return res;
    }
  }

  return res;
}

inline EditResult EditService::commit(
    const EditBatch &batch, operations::StepContext &ctx)
{
  PreparedBatch prepared;
  std::size_t remaining = 4096;
  auto res = prepare(batch, ctx.scene, prepared, remaining);
  if (res.outcome != Outcome::OK)
    return res;
  return commitPrepared(batch.scene, prepared, ctx);
}

inline EditResult EditService::commitPrepared(SceneId sceneId,
    PreparedBatch &prepared,
    operations::StepContext &ctx,
    const std::function<Outcome()> &beforeWrite)
{
  EditResult res;
  auto &scene = ctx.scene;
  auto *ident = prepared.identity;
  auto &nodes = prepared.nodes;
  auto &layers = prepared.layers;
  auto &plan = prepared.objects;

  // 2. Prepare allocations, Batch Reference and restoration storage. //

  RestorationRecord record;
  bool reserved = false;
  bool charged = false; // reserved in the activity budget
  std::vector<scene::Object *> objects;
  try {
    if (fault(FaultStage::PREPARE))
      throw std::bad_alloc();
    if (fault(FaultStage::ALLOCATE))
      throw std::bad_alloc();
    record.scene = sceneId;
    record.nodeChanges.reserve(nodes.size());
    res.nodeVersions.reserve(nodes.size());
    res.versions.reserve(plan.size());
    for (auto &p : nodes) {
      NodeChange c;
      c.resulting = {p.entry->nodeExpected, p.dependencies};
      c.beforeEnabled = p.node->value().isEnabled();
      c.changedEnabled = p.changedEnabled;
      c.changedTransform = p.changedTransform;
      c.requiresAncestry = p.worldEntry != nullptr;
      if (c.changedTransform) {
        c.beforeMatrix = p.node->value().getTransform();
        c.beforeSRT = p.node->value().getTransformSRT();
      }
      record.nodeChanges.push_back(c);
    }
    record.changes.reserve(plan.size());
    objects.reserve(plan.size());
    for (auto &p : plan) {
      Change c;
      c.object = p.entry->expected.object;
      c.parameter = p.entry->parameter;
      c.before = p.object->parameter(core::Token(p.entry->parameter))->value();
      c.after = p.value;
      record.changes.push_back(std::move(c));
      if (std::find(objects.begin(), objects.end(), p.object) == objects.end())
        objects.push_back(p.object);
    }
    record.resultingVersions.reserve(objects.size());
    auto id = newBatchId();
    if (!id)
      throw std::bad_alloc();
    record.ref = BatchRef{m_launchId, *id};
    if (fault(FaultStage::RESERVE_RESTORATION) || !m_store.reserve())
      throw std::bad_alloc();
    reserved = true;
    // Payload bytes plus one restorable-batch slot in the same budget as
    // operation records (spec §5); busy before commit when it cannot fit.
    if (ctx.reserveRestoration
        && !ctx.reserveRestoration(
            restorationBytes(record), record.ref.launchId, record.ref.batchId))
      throw std::bad_alloc();
    charged = true;
  } catch (const std::bad_alloc &) {
    if (charged && ctx.cancelRestoration)
      ctx.cancelRestoration();
    if (reserved)
      m_store.cancelReservation();
    // Admitted ID stays consumed; no commit and no batch.
    res.outcome = Outcome::BUSY;
    return res;
  }

  // The selection path rechecks authority/lifetime/deadline after allocation,
  // at the last no-write boundary. Denial releases every unpublished receipt.
  if (beforeWrite) {
    Outcome outcome = Outcome::OK;
    try {
      outcome = beforeWrite();
    } catch (const std::bad_alloc &) {
      outcome = Outcome::BUSY;
    }
    if (outcome != Outcome::OK) {
      if (charged && ctx.cancelRestoration)
        ctx.cancelRestoration();
      if (reserved)
        m_store.cancelReservation();
      res.outcome = outcome;
      return res;
    }
  }

  // 3. Commit: store every value, then flush signals. //

  scene.beginUpdateBatch();
  for (auto *o : objects)
    o->beginParameterBatch();
  for (auto &p : plan) {
    p.object->setParameter(
        core::Token(p.entry->parameter), p.value.type(), p.value.data());
  }
  for (auto &p : nodes) {
    if (p.changedTransform)
      p.node->value().setTransformState(p.matrix, p.srt);
    if (p.changedEnabled)
      p.node->value().setEnabled(p.enabled);
  }
  for (auto *o : objects)
    o->endParameterBatch();
  for (const auto *layer : layers)
    scene.signalLayerStructureChanged(layer);
  scene.endUpdateBatch();

  for (auto *o : objects) {
    auto v = ident->parameterVersion(ident->handleOf(o));
    if (v)
      record.resultingVersions.push_back(*v);
  }
  for (auto &c : record.nodeChanges) {
    c.resulting.expected = *ident->nodeEditVersion(c.resulting.expected.node);
    // Publish dependencies only after every node write. In particular a child
    // must observe its parent's final transform history, in either entry order.
    // This allocation-free walk revisits the already validated bounded chain;
    // edits cannot change topology.
    auto dependencies =
        *ident->beginNodeDependencies(c.resulting.expected.node);
    while (dependencies.ancestorSlot)
      ident->advanceNodeDependencies(dependencies);
    c.resulting.dependencies = dependencies;
    res.nodeVersions.push_back(c.resulting);
  }
  res.committed = true;
  res.batchRef = record.ref;
  res.sceneRevision = ident->sceneRevision();
  res.versions = record.resultingVersions;
  m_store.publish(std::move(record));
  if (ctx.publishRestoration)
    ctx.publishRestoration();
  if (ctx.restorationRetained) {
    // The activity log decides which batches stay restorable.
    m_store.expireUnless([&ctx](const BatchRef &r) {
      return ctx.restorationRetained(r.launchId, r.batchId);
    });
  }
  m_sync.beginHook(sceneId);
  return res;
}

inline std::weak_ptr<const void> EditService::lifetime() const
{
  return m_alive;
}

inline Outcome EditService::synchronizeCommitted(SceneId scene)
{
  return synchronize(scene);
}

inline Outcome EditService::synchronize(SceneId sceneId)
{
  // Ordinary synchronization: the host hook runs exactly once per commit.
  bool confirmed = false;
  if (auto hook = m_integration.normalSyncHook(sceneId)) {
    try {
      confirmed = hook(sceneId);
    } catch (...) {
      confirmed = false;
    }
  }
  // An earlier failure is only cleared by explicit Resynchronize.
  const bool ready = m_sync.finishHook(sceneId, confirmed);
  return ready ? Outcome::OK : Outcome::APPLIED_WITH_SYNC_FAILURE;
}

} // namespace vsr::integration::edit

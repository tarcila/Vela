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
#include <memory_resource>
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
  std::optional<std::uint64_t> approvalRequestId;
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
  std::string receipt; // storage and fallback prepared before Scene writes
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

// Capacity is reserved from the validated entry count before any Scene write.
// Formatting uses stack scratch, never temporary allocating strings.
inline void writeCommitReceipt(EditResult &result)
{
  auto &out = result.receipt;
  out.clear();
  out += "{\"batch_ref\":{\"launch_id\":\"";
  out += result.batchRef->launchId;
  out += "\",\"batch_id\":\"";
  out += result.batchRef->batchId;
  out += "\"},\"committed\":true";
  char buffer[512];
  const auto &revision = *result.sceneRevision;
  std::snprintf(buffer,
      sizeof(buffer),
      ",\"scene_revision\":\"%llu.%llu.%llu.%llu\",\"versions\":[",
      (unsigned long long)revision.launch.activation,
      (unsigned long long)revision.launch.nonce,
      (unsigned long long)revision.domain,
      (unsigned long long)revision.value);
  out += buffer;
  bool first = true;
  for (const auto &v : result.versions) {
    std::snprintf(buffer,
        sizeof(buffer),
        "%s{\"handle\":\"%016llx%016llx\",\"version\":%llu}",
        first ? "" : ",",
        (unsigned long long)v.object.domain,
        (unsigned long long)v.object.serial,
        (unsigned long long)v.version);
    out += buffer;
    first = false;
  }
  out += "]";
  if (!result.nodeVersions.empty()) {
    out += ",\"node_versions\":[";
    first = true;
    for (const auto &v : result.nodeVersions) {
      const auto &node = v.expected.node;
      const auto &d = v.dependencies;
      char handle[33], layer[33];
      std::snprintf(handle,
          sizeof(handle),
          "%016llx%016llx",
          (unsigned long long)node.domain,
          (unsigned long long)node.serial);
      std::snprintf(layer,
          sizeof(layer),
          "%016llx%016llx",
          (unsigned long long)d.layer.domain,
          (unsigned long long)d.layer.serial);
      std::snprintf(buffer,
          sizeof(buffer),
          "%s{\"handle\":\"%s\",\"version\":\"ne:%s:%llu\","
          "\"structural_version\":\"ns:%s:%s:%llu\","
          "\"ancestry_version\":\"na:%s:%s:%llu:%llu\"}",
          first ? "" : ",",
          handle,
          handle,
          (unsigned long long)v.expected.version,
          handle,
          layer,
          (unsigned long long)d.structuralVersion,
          handle,
          layer,
          (unsigned long long)d.structuralVersion,
          (unsigned long long)d.transformVersion);
      out += buffer;
      first = false;
    }
    out += "]";
  }
  out += "}";
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
  scene::Parameter *parameter;
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

struct ImageSnapshot
{
  explicit ImageSnapshot(
      std::pmr::memory_resource *allocator = std::pmr::new_delete_resource())
      : bytes(allocator)
  {}
  VSR_NOT_COPYABLE(ImageSnapshot)
  VSR_NOT_MOVEABLE(ImageSnapshot)
  // Control/store allocations have a lifetime independent of texels.
  operations::ResultOwnership controlOwnership;
  operations::ResultOwnership::Reserve reserveResult;
  std::shared_ptr<const void> scopeLease;
  // One shared physical allocation; copying records never duplicates texels.
  std::shared_ptr<const void> physicalLease;
  std::shared_ptr<const void> sessionLease;
  std::pmr::vector<std::uint8_t> bytes;
  ObjectHandle handle;
  std::uint64_t completedWrite{0};
  std::string batchOrder;
  std::string batchScope;
  std::weak_ptr<const void> registration;
  scene::Scene *scene{nullptr};
};

struct MeshSnapshot : ImageSnapshot
{
  struct Dependency
  {
    std::weak_ptr<scene::ArrayLifetime> lifetime;
    std::uint64_t write{0};
  };
  struct Binding
  {
    core::Token name;
    std::optional<scene::Parameter> prior;
    std::weak_ptr<scene::ArrayLifetime> before;
    std::weak_ptr<scene::ArrayLifetime> after;
    bool present{false};
    bool enabled{true};
  };
  ObjectHandle target;
  scene::ObjectVersion targetVersion{0};
  std::vector<Binding> bindings;
  std::vector<Dependency> dependencies;
  std::shared_ptr<const void> pinOwner;
  std::size_t retainedBytes{0};
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
  std::size_t publicationBytes{0}; // prepared receipt/version ownership
  std::shared_ptr<ImageSnapshot> image;
  // Mesh reuses Image's prepared bounded store/scope/receipt ownership.
  std::shared_ptr<MeshSnapshot> mesh;
};

// Active reservations are bounded (32 by default). Qualified hosts use the
// ActivityLog's sole publication FIFO authority; this store prunes only entries
// it makes ineligible. Standalone legacy hosts retain the bounded fallback.
class RestorationStore
{
 public:
  explicit RestorationStore(std::size_t capacity = 32);

  // Reserve storage for one record before mutation; false only when active
  // reservations alone fill the store.
  bool reserve();
  void cancelReservation();
  // Allocate a generic publication node/order storage before Scene writes.
  bool prepareRecord(const BatchRef &ref);
  void cancelRecord(const BatchRef &ref);
  void publish(RestorationRecord record, bool sharedRetention = false);
  // Allocate all store/control state before an Image's write boundary.
  bool prepareImage(RestorationRecord record);
  void publishImage(const BatchRef &ref);
  void cancelImage(const BatchRef &ref);
  std::optional<SceneId> imageScope(const BatchRef &ref) const;
  void forgetImageScope(const BatchRef &ref);
  // Bounded Image-only one-way history shares the scope's control lease.
  enum class ImageState
  {
    ELIGIBLE,
    RESTORED,
    CONFLICTING
  };
  std::optional<ImageState> imageState(const BatchRef &ref) const;
  bool setImageState(const BatchRef &ref, ImageState state);

  std::optional<RestorationRecord> find(const BatchRef &ref) const;
  // True once the record was evicted by the retention budget.
  bool expired(const BatchRef &ref) const;
  // Restoration commit releases the record's restorable position/payload.
  void release(const BatchRef &ref);
  // Visibly expires every record `retained` rejects; keeps this store in
  // step with an authoritative retention budget (the ActivityLog).
  template <typename F>
  void expireUnless(F &&retained);
  std::size_t size() const;
  std::size_t capacity() const;

 private:
  mutable std::mutex m_mutex;
  std::size_t m_capacity;
  std::size_t m_reserved{0};
  std::map<std::string, RestorationRecord> m_records;
  std::map<std::string, RestorationRecord> m_pendingImages;
  struct ImageScope
  {
    std::shared_ptr<const void> lease;
    SceneId scene;
    ImageState state{ImageState::ELIGIBLE};
  };
  std::map<std::string, ImageScope> m_imageScopes;
  std::vector<std::string> m_imageScopeOrder;
  std::vector<std::string> m_order; // oldest first
  struct Expired
  {
    std::string id;
    std::shared_ptr<const void> imageLease;
  };
  std::vector<Expired> m_expired; // 1024 allocation-free tombstones

  void expireLocked(const std::string &id); // may alias m_order
};

// Bytes charged to the activity budget for a restoration record's payload.
std::size_t restorationBytes(const RestorationRecord &record);

// Test seam for preparation failures; return true to fail that stage.
enum class FaultStage
{
  PREPARE,
  ALLOCATE,
  RESERVE_RESTORATION,
  LAST_PREWRITE,
  RECEIPT,
  SYNC
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
  // Shared edit/import namespace. Issued identifiers are never recycled.
  std::optional<std::string> newBatchId();

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

  Integration &m_integration;
  OperationEngine &m_engine;
  std::string m_launchId;
  RestorationStore m_store;
  FaultInjector m_fault;
  std::shared_ptr<bool> m_alive;

  mutable std::mutex m_mutex;
  std::map<SceneId, SceneIdentity *> m_identities;
  sync::SyncStates m_sync;
  std::uint64_t m_nextBatchId{0}; // shared edit/import namespace, never reused
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
{
  m_expired.reserve(1024);
}

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

inline bool RestorationStore::prepareRecord(const BatchRef &ref)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_order.reserve(m_order.size() + m_reserved + 1);
  RestorationRecord pending;
  pending.ref = ref;
  return m_pendingImages.emplace(ref.batchId, std::move(pending)).second;
}

inline void RestorationStore::cancelRecord(const BatchRef &ref)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_pendingImages.erase(ref.batchId);
}

inline void RestorationStore::publish(
    RestorationRecord record, bool sharedRetention)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_reserved)
    --m_reserved;
  if (!sharedRetention)
    while (!m_order.empty() && m_records.size() + m_reserved >= m_capacity)
      expireLocked(m_order.front());
  const auto pending = m_pendingImages.find(record.ref.batchId);
  if (pending != m_pendingImages.end()) {
    // The prepared map node and vector capacity make publication nonallocating.
    m_order.push_back(std::move(pending->second.ref.batchId));
    pending->second = std::move(record);
    m_records.insert(m_pendingImages.extract(pending));
    return;
  }
  // Legacy standalone hosts without an Activity sink retain the bounded store.
  auto id = record.ref.batchId;
  while (!m_order.empty() && m_records.size() + m_reserved >= m_capacity)
    expireLocked(m_order.front());
  m_order.push_back(id);
  m_records[id] = std::move(record);
}

inline bool RestorationStore::prepareImage(RestorationRecord record)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!record.image || m_reserved >= m_capacity)
    return false;
  const auto id = record.ref.batchId;
  const auto scopeKey = record.ref.launchId + "/" + id;
  // Allocations here may fail; no write has occurred. Rollback the partial
  // control state before propagating an allocation failure to admission.
  try {
    m_order.reserve(m_order.size() + m_capacity + 1);
    m_imageScopeOrder.reserve(m_imageScopeOrder.size() + m_capacity + 1);
    m_imageScopes.emplace(
        scopeKey, ImageScope{record.image->scopeLease, record.scene});
    record.image->batchOrder = id;
    record.image->batchScope = scopeKey;
    m_pendingImages.emplace(id, std::move(record));
  } catch (...) {
    m_pendingImages.erase(id);
    m_imageScopes.erase(scopeKey);
    throw;
  }
  ++m_reserved;
  return true;
}

inline void RestorationStore::publishImage(const BatchRef &ref)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  const auto it = m_pendingImages.find(ref.batchId);
  if (it == m_pendingImages.end())
    return;
  --m_reserved;
  m_order.push_back(std::move(it->second.image->batchOrder));
  m_imageScopeOrder.push_back(std::move(it->second.image->batchScope));
  m_records.insert(m_pendingImages.extract(it));
  // Historical scope metadata also ages by successful publication. Current
  // records carry their authoritative scope even under larger host limits.
  while (m_imageScopeOrder.size() > 1024) {
    m_imageScopes.erase(m_imageScopeOrder.front());
    m_imageScopeOrder.erase(m_imageScopeOrder.begin());
  }
}

inline void RestorationStore::cancelImage(const BatchRef &ref)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  const auto it = m_pendingImages.find(ref.batchId);
  if (it != m_pendingImages.end()) {
    auto scopeKey = std::move(it->second.image->batchScope);
    m_pendingImages.erase(it);
    --m_reserved;
    m_imageScopes.erase(scopeKey);
    m_imageScopeOrder.erase(
        std::remove(
            m_imageScopeOrder.begin(), m_imageScopeOrder.end(), scopeKey),
        m_imageScopeOrder.end());
  }
}

inline std::optional<SceneId> RestorationStore::imageScope(
    const BatchRef &ref) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  const auto record = m_records.find(ref.batchId);
  if (record != m_records.end() && record->second.ref.launchId == ref.launchId
      && record->second.image)
    return record->second.scene;
  const auto it = m_imageScopes.find(ref.launchId + "/" + ref.batchId);
  return it == m_imageScopes.end() ? std::optional<SceneId>{}
                                   : std::optional<SceneId>{it->second.scene};
}

inline void RestorationStore::forgetImageScope(const BatchRef &ref)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  const auto scopeKey = ref.launchId + "/" + ref.batchId;
  m_imageScopes.erase(scopeKey);
  m_imageScopeOrder.erase(
      std::remove(m_imageScopeOrder.begin(), m_imageScopeOrder.end(), scopeKey),
      m_imageScopeOrder.end());
}

inline std::optional<RestorationStore::ImageState> RestorationStore::imageState(
    const BatchRef &ref) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  const auto it = m_imageScopes.find(ref.launchId + "/" + ref.batchId);
  return it == m_imageScopes.end()
      ? std::optional<ImageState>{}
      : std::optional<ImageState>{it->second.state};
}

inline bool RestorationStore::setImageState(
    const BatchRef &ref, ImageState state)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  const auto it = m_imageScopes.find(ref.launchId + "/" + ref.batchId);
  if (it == m_imageScopes.end())
    return false;
  it->second.state = state;
  return true;
}

inline void RestorationStore::expireLocked(const std::string &id)
{
  const auto record = m_records.find(id);
  if (record == m_records.end())
    return;
  Expired expired;
  if (record->second.image) {
    // Match the already-owned scope key without constructing launch/id.
    const auto &launch = record->second.ref.launchId;
    for (auto scope = m_imageScopes.begin(); scope != m_imageScopes.end();
        ++scope) {
      const auto &key = scope->first;
      if (key.size() == launch.size() + 1 + id.size()
          && key.compare(0, launch.size(), launch) == 0
          && key[launch.size()] == '/'
          && key.compare(launch.size() + 1, id.size(), id) == 0) {
        expired.imageLease = std::move(scope->second.lease);
        m_imageScopeOrder.erase(
            std::remove(
                m_imageScopeOrder.begin(), m_imageScopeOrder.end(), key),
            m_imageScopeOrder.end());
        m_imageScopes.erase(scope);
        break;
      }
    }
  }
  // Move independent ownership before erasing a possibly aliased order key.
  expired.id = std::move(record->second.ref.batchId);
  m_order.erase(
      std::remove(m_order.begin(), m_order.end(), expired.id), m_order.end());
  m_records.erase(record);
  if (m_expired.size() == 1024)
    m_expired.erase(m_expired.begin());
  m_expired.push_back(std::move(expired));
}

template <typename F>
inline void RestorationStore::expireUnless(F &&retained)
{
  // Store -> engine -> ActivityLog. Engine/sink callbacks never acquire the
  // store lock; executors run outside the engine lock. No snapshot allocations
  // or temporary Batch Reference copies are needed at publication.
  std::lock_guard<std::mutex> lock(m_mutex);
  for (std::size_t i = 0; i < m_order.size();) {
    const auto record = m_records.find(m_order[i]);
    if (record != m_records.end() && !retained(record->second.ref))
      expireLocked(m_order[i]);
    else
      ++i;
  }
}

inline std::size_t restorationBytes(const RestorationRecord &record)
{
  auto anyBytes = [](const core::Any &a) {
    return a.type() == ANARI_STRING ? a.getStringView().size() : std::size_t(0);
  };
  std::size_t n = sizeof(RestorationRecord) + record.ref.launchId.size()
      + record.ref.batchId.size()
      + record.resultingVersions.capacity() * sizeof(ParameterVersionToken);
  n += record.nodeChanges.capacity() * sizeof(NodeChange);
  // Map node, order key and allocation/control bookkeeping.
  n += 4096 + record.publicationBytes;
  if (record.image)
    n += record.image->bytes.size();
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
  return std::any_of(m_expired.begin(), m_expired.end(), [&](const Expired &e) {
    return e.id == ref.batchId;
  });
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
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_nextBatchId == UINT64_MAX)
    return std::nullopt;
  // Launch qualification supplies uniqueness across launches; a bounded
  // counter supplies exact uniqueness within the shared namespace without
  // retaining an unbounded journal of every issued Image batch.
  return detail::hex64(0) + detail::hex64(++m_nextBatchId);
}

inline operations::Request EditService::makeRequest(EditBatch batch, Slot slot)
{
  operations::Request r;
  r.kind = "edit";
  r.target = session::Target::scene(batch.scene);
  r.capability = session::Capability::EDIT;
  r.canonicalArguments = canonicalArguments(batch);
  r.approvalRequestId = batch.approvalRequestId;
  r.boundSceneRead = true;
  std::weak_ptr<bool> alive = m_alive;
  auto shared = std::make_shared<EditBatch>(std::move(batch));
  // One prepared owner serves both the pending committed state and public
  // result lookup; a failed synchronization dispatch cannot lose its receipt.
  auto committed = slot;
  r.onSynchronizationFailure = [this, alive, slot, scene = shared->scene] {
    if (alive.lock()) {
      m_sync.finishHook(scene, false);
      std::lock_guard<std::mutex> lock(m_mutex);
      if (*slot)
        (*slot)->outcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
    }
  };
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
        step.result = std::move(res.receipt);
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
    const auto synchronized = synchronize(shared->scene);
    if (res.outcome == Outcome::OK)
      res.outcome = synchronized;
    step.outcome = res.outcome;
    step.committed = true;
    step.result = std::move(res.receipt);
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
        plan.push_back({o,
            &e,
            std::move(p.value),
            o->parameter(core::Token(e.parameter))});
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

  // Compare the fully validated final state, including latent SRT state.
  // An equivalent operation has no publication or recovery position.
  plan.erase(
      std::remove_if(plan.begin(),
          plan.end(),
          [](const Planned &p) { return p.parameter->value() == p.value; }),
      plan.end());
  for (auto &p : nodes) {
    p.changedEnabled =
        p.changedEnabled && p.enabled != p.node->value().isEnabled();
    p.changedTransform = p.changedTransform
        && (p.matrix != p.node->value().getTransform()
            || p.srt != p.node->value().getTransformSRT());
  }
  nodes.erase(std::remove_if(nodes.begin(),
                  nodes.end(),
                  [](const PlannedNode &p) {
                    return !p.changedEnabled && !p.changedTransform;
                  }),
      nodes.end());
  if (plan.empty() && nodes.empty())
    return res;
  layers.clear();
  for (const auto &p : nodes) {
    const auto *layer = p.node->value().layer();
    if (std::find(layers.begin(), layers.end(), layer) == layers.end())
      layers.push_back(layer);
  }

  // 2. Prepare allocations, Batch Reference and restoration storage. //

  RestorationRecord record;
  bool reserved = false;
  bool charged = false; // reserved in the activity budget
  bool publicationPrepared = false;
  std::size_t requiredRecoveryBytes = 0;
  // At most 64 entries; includes decimal uint64 maxima and all fixed syntax.
  const std::size_t receiptCapacity =
      m_launchId.size() + 1024 + plan.size() * 192 + nodes.size() * 512;
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
      c.before = p.parameter->value();
      c.after = p.value;
      record.changes.push_back(std::move(c));
      if (std::find(objects.begin(), objects.end(), p.object) == objects.end())
        objects.push_back(p.object);
    }
    for (auto *object : objects)
      object->reserveParameterBatch(plan.size());
    record.resultingVersions.reserve(objects.size());
    auto id = newBatchId();
    if (!id)
      throw std::bad_alloc();
    record.ref = BatchRef{m_launchId, *id};
    if (fault(FaultStage::RESERVE_RESTORATION) || !m_store.reserve())
      throw std::bad_alloc();
    reserved = true;
    // Preparation reserves the individual recovery charge, not free space in
    // old history. Only successful publication may replace eligible history.
    record.publicationBytes = 2 * receiptCapacity
        + res.versions.capacity() * sizeof(ParameterVersionToken)
        + res.nodeVersions.capacity() * sizeof(NodeVersion);
    requiredRecoveryBytes = restorationBytes(record);
    if (ctx.restorationPolicy
        && (ctx.recoveryApprovalRequested
            || requiredRecoveryBytes > ctx.restorationPolicy->hardBytes)) {
      // Exact converted types/values and final local Node state complement
      // the engine's target/version/ancestry-bound semantic arguments.
      std::size_t bound = 16384;
      for (const auto &change : record.changes) {
        const auto bytes = change.after.type() == ANARI_STRING
            ? std::strlen(static_cast<const char *>(change.after.data()))
            : anari::sizeOf(change.after.type());
        if (bytes > SIZE_MAX - bound)
          throw std::bad_alloc();
        bound += bytes;
      }
      const auto coverage = ctx.reserveRecoveryMemory
          ? ctx.reserveRecoveryMemory(bound)
          : nullptr;
      if (!coverage)
        throw std::bad_alloc();
      std::string canonical;
      canonical.reserve(bound);
      for (const auto &change : record.changes) {
        canonical += std::to_string(change.after.type()) + ":";
        const auto bytes = change.after.type() == ANARI_STRING
            ? std::strlen(static_cast<const char *>(change.after.data()))
            : anari::sizeOf(change.after.type());
        canonical += std::to_string(bytes) + ":";
        canonical.append(static_cast<const char *>(change.after.data()), bytes);
      }
      static_assert(sizeof(core::math::mat4) == 16 * sizeof(float));
      static_assert(sizeof(core::math::mat3) == 9 * sizeof(float));
      for (const auto &p : nodes) {
        canonical += p.changedEnabled ? "enabled:" : "unchanged:";
        canonical += p.enabled ? "1" : "0";
        canonical += p.changedTransform ? "transform:" : "unchanged:";
        if (p.changedTransform) {
          canonical.append(
              reinterpret_cast<const char *>(&p.matrix), sizeof(p.matrix));
          canonical.append(
              reinterpret_cast<const char *>(&p.srt), sizeof(p.srt));
        }
      }
      if (!ctx.authorizeRecovery
          || !ctx.authorizeRecovery(requiredRecoveryBytes,
              canonical,
              ident->lifetime(),
              ctx.reserveRecoveryMemory,
              "prepared_edit"))
        throw std::bad_alloc();
    }
    if (ctx.reserveRestoration
        && !ctx.reserveRestoration(
            requiredRecoveryBytes, record.ref.launchId, record.ref.batchId))
      throw std::bad_alloc();
    charged = true;
    if (!m_store.prepareRecord(record.ref))
      throw std::bad_alloc();
    publicationPrepared = true;
    res.batchRef = record.ref;
    // This minimal receipt survives failure of richer post-write construction.
    res.receipt = "{\"batch_ref\":" + detail::batchRefJson(record.ref)
        + ",\"committed\":true}";
    res.receipt.reserve(receiptCapacity);
    if (ctx.prepareCommit)
      ctx.prepareCommit(res.receipt, receiptCapacity);
    m_sync.prepareScene(sceneId);
  } catch (const std::bad_alloc &) {
    if (publicationPrepared)
      m_store.cancelRecord(record.ref);
    if (charged && ctx.cancelRestoration)
      ctx.cancelRestoration();
    if (reserved)
      m_store.cancelReservation();
    // Admitted ID stays consumed; no commit and no batch.
    res.batchRef.reset();
    res.receipt.clear();
    if (ctx.restorationPolicy
        && requiredRecoveryBytes > ctx.restorationPolicy->hardBytes)
      res.receipt = ctx.restorationPolicy->refusal(
          requiredRecoveryBytes, ctx.approvalOffered);
    res.outcome = Outcome::BUSY;
    return res;
  }

  // The selection path rechecks authority/lifetime/deadline after allocation,
  // at the last no-write boundary. Denial releases every unpublished receipt.
  {
    Outcome outcome = Outcome::OK;
    try {
      if (fault(FaultStage::LAST_PREWRITE))
        throw std::bad_alloc();
      if (beforeWrite)
        outcome = beforeWrite();
      else if (ctx.beforeWrite)
        outcome = ctx.beforeWrite();
      // Preparation callbacks may revoke authority or change a dependency.
      // Revalidate tokens before touching ephemeral parameter/node pointers.
      if (outcome == Outcome::OK) {
        for (const auto &p : plan)
          if (!ident->isCurrent(p.entry->expected))
            outcome = Outcome::CONFLICT;
        std::size_t remaining = 4096;
        for (const auto &p : nodes) {
          if (!ident->isCurrent(p.entry->nodeExpected)) {
            outcome = Outcome::CONFLICT;
            break;
          }
          auto walk = ident->beginNodeDependencies(p.entry->nodeExpected.node);
          if (!walk) {
            outcome = Outcome::CONFLICT;
            break;
          }
          while (walk->ancestorSlot && remaining) {
            --remaining;
            if (!ident->advanceNodeDependencies(*walk))
              break;
          }
          if (walk->ancestorSlot || !walk->matchesStructure(p.dependencies)
              || (p.worldEntry && !walk->matchesAncestry(p.dependencies)))
            outcome = Outcome::CONFLICT;
        }
      }
      if (outcome == Outcome::OK) {
        scene.beginUpdateBatch();
        if (ctx.consumeRecoveryApproval && !ctx.consumeRecoveryApproval()) {
          scene.endUpdateBatch();
          outcome = Outcome::FORBIDDEN;
        }
      }
    } catch (...) {
      outcome = Outcome::BUSY;
    }
    if (outcome != Outcome::OK) {
      if (publicationPrepared)
        m_store.cancelRecord(record.ref);
      res.batchRef.reset();
      res.receipt.clear();
      if (charged && ctx.cancelRestoration)
        ctx.cancelRestoration();
      if (reserved)
        m_store.cancelReservation();
      res.outcome = outcome;
      return res;
    }
  }

  // 3. Commit: store every value, then flush signals. //

  for (auto *o : objects)
    o->beginParameterBatch();
  for (auto &p : plan) {
    p.parameter->setPreparedValue(std::move(p.value));
  }
  for (auto &p : nodes) {
    if (p.changedTransform)
      p.node->value().setTransformState(p.matrix, p.srt);
    if (p.changedEnabled)
      p.node->value().setEnabled(p.enabled);
  }
  res.committed = true;
  if (ctx.markCommitted)
    ctx.markCommitted();
  // Downstream notification failure cannot turn completed Scene writes into a
  // rejection or prevent other objects/identity observers from seeing them.
  for (auto *o : objects) {
    try {
      o->endParameterBatch();
    } catch (...) {
      res.outcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
    }
  }
  for (const auto *layer : layers) {
    try {
      scene.signalLayerStructureChanged(layer);
    } catch (...) {
      res.outcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
    }
  }
  try {
    scene.endUpdateBatch();
  } catch (...) {
    res.outcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
  }

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
  res.sceneRevision = ident->sceneRevision();
  res.versions = record.resultingVersions;
  m_store.publish(std::move(record), ctx.sharedRetention);
  if (ctx.publishRestoration)
    ctx.publishRestoration();
  if (ctx.restorationRetained) {
    // The activity log decides which batches stay restorable.
    m_store.expireUnless([&ctx](const BatchRef &r) {
      return ctx.restorationRetained(r.launchId, r.batchId);
    });
  }
  // Full receipt is optional after commit; the already-owned fallback and
  // exact recovery record remain usable even if allocation fails here.
  try {
    if (fault(FaultStage::RECEIPT))
      throw std::bad_alloc();
    detail::writeCommitReceipt(res);
  } catch (const std::bad_alloc &) {
    res.outcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
  }
  m_sync.beginHook(sceneId);
  if (res.outcome != Outcome::OK)
    m_sync.markNeedsSync(sceneId);
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
  try {
    if (fault(FaultStage::SYNC))
      throw std::bad_alloc();
    if (auto hook = m_integration.normalSyncHook(sceneId))
      confirmed = hook(sceneId);
  } catch (...) {
    confirmed = false;
  }
  // An earlier failure is only cleared by explicit Resynchronize.
  const bool ready = m_sync.finishHook(sceneId, confirmed);
  return ready ? Outcome::OK : Outcome::APPLIED_WITH_SYNC_FAILURE;
}

} // namespace vsr::integration::edit

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/activity/Activity.hpp"
#include "vsr/integration/content/AllocationProfile.hpp"
#include "vsr/integration/content/AssetRoots.hpp"
#include "vsr/integration/content/HdriDecode.hpp"
#include "vsr/integration/content/PreparationOwnership.hpp"
#include "vsr/integration/edit/AgentEdit.hpp"
#include "vsr/integration/mcp/Json.hpp"
// std
#include <atomic>
#include <future>

namespace vsr::integration::content {

inline std::string hdriNumber(double number)
{
  mcp::json::Value value;
  value.type = mcp::json::Value::Type::NUMBER;
  value.n = number;
  return mcp::json::write(value);
}

struct HdriRequest
{
  SceneId scene{INVALID_SCENE_ID};
  size_t assetRoot{0};
  std::string fileName;
  std::string rootName{"HDRI Content"};
  std::string newLayer;
  std::optional<identity::LayerHandle> layer;
  std::optional<identity::NodeHandle> parent;
  scene::detail::HdriSettings settings;
  std::optional<std::uint64_t> approvalRequestId;
};

/* Host-owned creator. Every actual file read and byte comparison runs on the
 * worker, never the Scene thread. The owner performs bounded descriptor/stat
 * currency and scoped destination guards. Roots and services outlive jobs;
 * shutdown cancels and joins workers before releasing their physical leases.
 * Recovery is metadata-only in the ordinary shared store, not a radiance copy.
 */
class ContentCreation
{
 public:
  using Reserve = operations::Reservation;
  ContentCreation(Integration &integration,
      operations::OperationEngine &engine,
      edit::EditService &edits,
      activity::ActivityLog &activity,
      Reserve reserve);
  ~ContentCreation();
  VSR_NOT_COPYABLE(ContentCreation)
  AssetRoots &assetRoots();
  void attachScene(SceneId scene, identity::SceneIdentity &identity);
  void pump();
  // Trusted host telemetry at a worker checkpoint. Install only while idle;
  // callbacks run off-thread and must not access the live Scene.
  void setWorkerObserver(operations::SharedNotification observer);
  operations::Admission submit(const session::Credentials &credentials,
      const operations::OperationRef &ref,
      HdriRequest request);
  std::string metadata() const;
  // Bounded host-root discovery: ids/kinds/labels only, never paths.
  std::string rootProfiles() const;

 private:
  struct Source
  {
    identity::SceneIdentity *identity{nullptr};
    std::weak_ptr<const void> lifetime;
  };
  struct Job;
  struct State
  {
    explicit State(std::shared_ptr<CapacityArena> arena);
    using Sources = std::map<SceneId,
        Source,
        std::less<SceneId>,
        CapacityAllocator<std::pair<const SceneId, Source>>>;
    using Jobs =
        std::vector<std::weak_ptr<Job>, CapacityAllocator<std::weak_ptr<Job>>>;
    std::atomic<bool> stopping{false};
    AssetRoots roots;
    Reserve reserve;
    operations::SharedNotification workerObserver;
    edit::EditService *edits{nullptr};
    std::weak_ptr<const void> editsLife;
    Sources sources;
    std::mutex mutex;
    Jobs jobs;
  };
  struct Job
  {
    ~Job();
    std::shared_ptr<const void> control;
    HdriRequest request;
    std::string settingsJson;
    PreparationOwnership initialPreparation;
    PreparationOwnership tokenPreparation;
    PreparationOwnership preparation;
    std::shared_ptr<State> state;
    std::shared_ptr<CapacityArena> workerArena;
    std::shared_ptr<const void> asyncControl;
    std::future<void> worker;
    std::function<void()> wake;
    std::shared_ptr<AssetRoots::Snapshot> source;
    HdriDecodeResult decoded;
    std::vector<std::pair<identity::NodeHandle, identity::NodeEditVersionToken>>
        ancestry;
    std::vector<std::uint64_t> topology;
    std::optional<identity::NodeHandle> destination;
    std::weak_ptr<const void> registration;
    std::string receipt;
    edit::BatchRef batch;
    bool started{false};
    bool sourceCurrent{false};
    bool workerResourceFailed{false};
    bool preparedStore{false};
    bool committed{false};
    bool notificationFailed{false};
    bool hookPending{false};
  };
  std::shared_ptr<State> m_state;
  Integration *m_integration{nullptr};
  operations::OperationEngine *m_engine{nullptr};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline ContentCreation::State::State(std::shared_ptr<CapacityArena> arena)
    : sources(std::less<SceneId>{}, Sources::allocator_type(arena)),
      jobs(Jobs::allocator_type(std::move(arena)))
{}

inline ContentCreation::Job::~Job()
{
  if (worker.valid())
    worker.wait();
  if (preparedStore && !state->editsLife.expired())
    state->edits->restorations().cancelImage(batch);
}

inline ContentCreation::ContentCreation(Integration &integration,
    operations::OperationEngine &engine,
    edit::EditService &edits,
    activity::ActivityLog &activity,
    Reserve reserve)
    : m_integration(&integration), m_engine(&engine)
{
  if (!reserve || !activity.limits().valid())
    throw std::runtime_error(
        "Content creation requires shared retention and physical ownership");
  auto arena = CapacityArena::create(4u << 20, reserve);
  m_state = std::allocate_shared<State>(CapacityAllocator<State>(arena), arena);
  // Literal map-node and vector allocations, not a full-capacity allowance.
  // Their allocator headers retain coverage through the last backing owner.
  m_state->jobs.reserve(32);
  m_state->roots.setCapacityArena(CapacityArena::create(1u << 20, reserve));
  m_state->reserve = std::move(reserve);
  m_state->edits = &edits;
  m_state->editsLife = edits.lifetime();
}

inline ContentCreation::~ContentCreation()
{
  m_state->stopping.store(true);
  std::lock_guard<std::mutex> lock(m_state->mutex);
  for (const auto &weak : m_state->jobs)
    if (const auto job = weak.lock())
      if (job->worker.valid())
        job->worker.wait();
}

inline void ContentCreation::pump()
{
  std::lock_guard<std::mutex> lock(m_state->mutex);
  auto &jobs = m_state->jobs;
  jobs.erase(std::remove_if(jobs.begin(),
                 jobs.end(),
                 [](const auto &weak) { return weak.expired(); }),
      jobs.end());
  for (const auto &weak : jobs)
    if (const auto job = weak.lock())
      if (job->worker.valid()
          && job->worker.wait_for(std::chrono::seconds(0))
              == std::future_status::ready
          && job->wake)
        job->wake();
}

inline void ContentCreation::setWorkerObserver(
    operations::SharedNotification observer)
{
  std::lock_guard<std::mutex> lock(m_state->mutex);
  for (const auto &job : m_state->jobs)
    if (!job.expired())
      throw std::runtime_error("Worker observer requires an idle creator");
  m_state->workerObserver = std::move(observer);
}

inline AssetRoots &ContentCreation::assetRoots()
{
  return m_state->roots;
}

inline void ContentCreation::attachScene(
    SceneId scene, identity::SceneIdentity &identity)
{
  std::lock_guard<std::mutex> lock(m_state->mutex);
  if (!m_state->sources.count(scene) && m_state->sources.size() == 32)
    throw std::runtime_error("Content scene registration limit");
  m_state->sources[scene] = {&identity, identity.lifetime()};
}

inline std::string ContentCreation::rootProfiles() const
{
  // Bounded operator discovery: at most 16 ids, each with its trusted kind and
  // a <=64-byte host label. No filesystem path, credential or grant appears,
  // and selecting an id never widens access beyond its pinned directory fd.
  std::string out = "[";
  for (const auto &profile : m_state->roots.profiles()) {
    if (out.size() > 1)
      out += ',';
    out += "{\"id\":" + std::to_string(profile.id) + ",\"kind\":\""
        + (profile.kind == AssetRoots::Kind::LAUNCH_PRIVATE ? "launch_private"
                                                            : "host_startup")
        + "\",\"label\":" + mcp::json::quote(profile.label)
        + ",\"approved\":" + (profile.approved ? "true" : "false") + "}";
  }
  return out + "]";
}

inline std::string ContentCreation::metadata() const
{
  if (!allocation_profile::AVAILABLE)
    return "{\"kinds\":[],\"reason\":\"hdri_allocation_profile_unavailable\"}";
  return "{\"kinds\":[\"hdri\"],\"source_profile\":\"radiance_rgbe_minus_y_plus_x\","
      "\"color\":\"linear_float32_rgb\",\"rows\":\"bottom_up_light_radiance\","
      "\"defaults\":{\"scale\":1,\"up\":[0,1,0],\"direction\":[1,0,0],\"root_name\":\"HDRI Content\"},"
      "\"limits\":{\"input_bytes\":4194304,\"pixels\":1048576,\"dimension\":4096,"
      "\"processing_bytes_per_job\":171966464,\"control_profile_bytes\":1048576,\"store_record_bound\":4096,\"store_scope_bound\":1024,\"worker_backing_bytes\":33554432,\"initial_profile_bytes\":67108864,\"token_profile_bytes\":4194304,\"preparation_profile_bytes\":67108864,\"metadata_bytes_per_owner\":65536,\"registry_buckets\":65536,\"concurrent_jobs\":32,\"registered_scenes\":32,\"result_bytes\":16384,\"reference_scan_slots\":32768,"
      "\"reference_scan_values\":131072,\"ancestry\":1024,\"deadline_seconds\":30},"
      "\"asset_roots\":" + std::to_string(m_state->roots.size())
      + ",\"asset_root_profiles\":" + rootProfiles()
      + ",\"root_authority\":\"trusted_host_startup_and_launch_private_only; no_agent_grant\""
        ",\"cancellation\":\"cooperative_io_chunks_and_decode_rows; slot_and_leases_held_until_worker_exit\","
        "\"recovery\":\"normal_conditional_restore_metadata_only_shared_fifo\"}";
}

inline operations::Admission ContentCreation::submit(
    const session::Credentials &credentials,
    const operations::OperationRef &ref,
    HdriRequest request)
{
  using operations::Outcome;
  using operations::Step;
  operations::Admission rejected;
  rejected.code = Outcome::INVALID_REQUEST;
  if (!allocation_profile::AVAILABLE)
    return rejected;
  if (request.fileName.empty() || request.fileName.size() > 4096
      || request.rootName.empty() || request.rootName.size() > 256
      || request.rootName.find('\0') != std::string::npos
      || request.newLayer.size() > 256
      || request.newLayer.find('\0') != std::string::npos
      || (!request.newLayer.empty() && (request.parent || request.layer)))
    return rejected;
  auto state = m_state;
  // Qualify retained input capacity too: C++ callers can clear a huge string.
  // quote() grows at most sixfold; libstdc++ concatenation doubles capacity.
  // Eight simultaneous canonical/receipt construction owners include overlap.
  core::StorageProfile controls(1u << 20);
  // The failure target owns exactly one shared Job pointer, not an opaque
  // host callback. Its backing and overlap stay under the Job control lease.
  if (!controls.add(sizeof(Job) + sizeof(operations::Request)
          + sizeof(std::shared_ptr<Job>) + 2 * sizeof(std::max_align_t))
      || !controls.string(request.fileName)
      || !controls.string(request.rootName)
      || !controls.string(request.newLayer) || !controls.string(ref.sessionId)
      || ref.sessionId.size() > 256
      || !controls.add(8
          * (6
                  * (request.fileName.size() + request.rootName.size()
                      + request.newLayer.size())
              + 4096))
      || !controls.add(32,
          sizeof(std::function<void()>)
              + sizeof(std::pair<std::string, std::uint64_t>)
              + sizeof(std::string) + 2 * 257
              + 4 * sizeof(std::shared_ptr<const void>))) {
    rejected.code = Outcome::BUSY;
    return rejected;
  }
  auto control = state->reserve(controls.bytes());
  if (!control) {
    rejected.code = Outcome::BUSY;
    return rejected;
  }
  auto job = std::allocate_shared<Job>(CapacityAllocator<Job>(state->reserve));
  job->state = state;
  job->control = std::move(control);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    auto &jobs = state->jobs;
    jobs.erase(std::remove_if(jobs.begin(),
                   jobs.end(),
                   [](const auto &weak) { return weak.expired(); }),
        jobs.end());
    if (jobs.size() == 32 || state->stopping.load()) {
      rejected.code = Outcome::BUSY;
      return rejected;
    }
    jobs.push_back(job);
  }
  const auto settingsJson = [&] {
    std::string text = "{\"scale\":" + hdriNumber(request.settings.scale);
    const auto vector = [](const math::float3 &v) {
      return "[" + hdriNumber(v.x) + "," + hdriNumber(v.y) + ","
          + hdriNumber(v.z) + "]";
    };
    text += ",\"up\":" + vector(request.settings.up) + ",\"direction\":"
        + vector(request.settings.direction) + ",\"transform\":[";
    for (size_t c = 0; c < 4; ++c)
      for (size_t r = 0; r < 4; ++r) {
        if (c || r)
          text += ',';
        text += hdriNumber(request.settings.transform[c][r]);
      }
    return text + "]}";
  }();
  operations::Request operation;
  operation.kind = "create_hdri";
  operation.target = session::Target::scene(request.scene);
  operation.capability = session::Capability::EDIT;
  operation.boundSceneRead = true;
  operation.deadlineSeconds = 30.;
  operation.approvalRequestId = request.approvalRequestId;
  operation.resourceLease = job->control;
  // One bounded Status/Activity string plus doubling capacity; MCP separately
  // profiles both encoding levels and its wire/parser overlap before copying.
  constexpr size_t RESULT_BACKING =
      2 * (16384 + 1) + sizeof(operations::Status) + sizeof(activity::Entry);
  operation.reserveStatus =
      operations::ResultOwnership::Reserve(state->reserve, RESULT_BACKING);
  operation.canonicalArguments = std::to_string(request.scene) + ":"
      + std::to_string(request.assetRoot) + ":"
      + mcp::json::quote(request.fileName) + ":"
      + mcp::json::quote(request.rootName) + ":"
      + mcp::json::quote(request.newLayer) + ":" + settingsJson;
  if (request.layer)
    operation.canonicalArguments +=
        ":layer:" + std::to_string(request.layer->domain) + ":"
        + std::to_string(request.layer->serial);
  if (request.parent)
    operation.canonicalArguments +=
        ":parent:" + std::to_string(request.parent->domain) + ":"
        + std::to_string(request.parent->serial);
  job->request = std::move(request);
  job->settingsJson = settingsJson;
  operation.onSynchronizationFailure = [job] {
    if (job->state->editsLife.expired())
      return;
    auto &sync = job->state->edits->syncStates();
    if (job->hookPending) {
      job->hookPending = false;
      sync.finishHook(job->request.scene, false);
    } else {
      sync.markFailed(job->request.scene);
    }
  };
  operation.executor = [state, job, integration = m_integration](
                           operations::StepContext &ctx) {
    const auto &request = job->request;
    const auto &settingsJson = job->settingsJson;
    const auto fail = [&](Outcome outcome, const char *reason) {
      if (job->preparedStore && !state->editsLife.expired()) {
        state->edits->restorations().cancelImage(job->batch);
        job->preparedStore = false;
      }
      if (ctx.cancelRestoration)
        ctx.cancelRestoration();
      Step result;
      result.outcome = outcome;
      result.result =
          "{\"committed\":false,\"reason\":" + mcp::json::quote(reason) + "}";
      return result;
    };
    if (job->committed) {
      // The prior safe point's detached publication owners have now died.
      job->preparation.releaseAfterPrivateDestruction();
      Step result;
      result.committed = true;
      result.resourceLease = job->control;
      result.reserveStatus =
          operations::ResultOwnership::Reserve(state->reserve, RESULT_BACKING);
      result.result = std::move(job->receipt);
      // Transfer this hook to synchronizeCommitted before invoking consumers;
      // a later engine failure must not consume a different outstanding hook.
      job->hookPending = false;
      try {
        result.outcome = state->editsLife.expired()
            ? Outcome::APPLIED_WITH_SYNC_FAILURE
            : state->edits->synchronizeCommitted(request.scene);
      } catch (...) {
        result.outcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
      }
      if (job->notificationFailed)
        result.outcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
      return result;
    }
    try {
      if (job->started
          && job->worker.wait_for(std::chrono::seconds(0))
              != std::future_status::ready) {
        Step pending;
        pending.status = Step::Status::BACKEND_PENDING;
        return pending;
      }
      if (state->stopping.load() || ctx.abandoned || ctx.isAbandoned())
        return fail(Outcome::CANCELLED, "cancelled");
      if (const auto *reason =
              scene::detail::validateHdriSettings(request.settings))
        return fail(Outcome::INVALID_REQUEST, reason);
      Source source;
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto found = state->sources.find(request.scene);
        if (found != state->sources.end())
          source = found->second;
      }
      if (source.lifetime.expired() || state->editsLife.expired()
          || !ctx.sharedRetention)
        return fail(Outcome::BUSY, "host_retired_or_unqualified");
      auto *ids = source.identity;
      if (ids->scene() != &ctx.scene
          || !integration->imageTrackingAvailable(request.scene))
        return fail(Outcome::INVALID_REQUEST, "write_contract_unavailable");
      if (!job->started) {
        // Leave room for the complete owned closure in the removal scan.
        if (const auto *reason =
                scopedUsage(ctx.scene, nullptr, nullptr, {}, 6, 64))
          return fail(Outcome::BUSY, reason);

        scene::LayerNodeRef parent;
        if (request.parent)
          parent = ids->resolve(*request.parent);
        else if (request.layer) {
          auto *layer = ids->resolve(*request.layer);
          if (layer)
            parent = layer->root();
        } else if (request.newLayer.empty() && ctx.scene.numberOfLayers())
          parent = ctx.scene.layer(0)->root();
        if ((request.parent || request.layer) && !parent)
          return fail(Outcome::INVALID_REQUEST, "invalid_destination");
        if (request.layer && parent
            && ids->resolve(*request.layer) != parent->value().layer())
          return fail(Outcome::INVALID_REQUEST, "destination_layer_mismatch");
        size_t ancestry = 0;
        if (parent)
          for (auto node = parent;; node = node->parent()) {
            if (++ancestry > 1024)
              return fail(Outcome::BUSY, "ancestry_limit");
            if (node->isRoot())
              break;
          }
        core::StorageProfile initialProfile(64u << 20);
        if (!PreparedHdriPublication::profileInitial(*ids,
                parent,
                !request.newLayer.empty() || !ctx.scene.numberOfLayers(),
                ancestry,
                initialProfile))
          return fail(Outcome::BUSY, "initial_profile_limit");
        job->initialPreparation.acquire(initialProfile.bytes(), state->reserve);
        job->ancestry.reserve(ancestry);
        job->topology.reserve(ancestry);
        if (parent) {
          job->destination = ids->handleOf(parent);
          for (auto node = parent;; node = node->parent()) {
            if (job->ancestry.size() == 1024)
              return fail(Outcome::BUSY, "ancestry_limit");
            const auto handle = ids->handleOf(node);
            const auto version = ids->nodeEditVersion(handle);
            if (!version)
              return fail(Outcome::CONFLICT, "destination_retired");
            job->ancestry.emplace_back(handle, *version);
            job->topology.push_back(node->topologyVersion());
            if (node->isRoot())
              break;
          }
        }
        // Worker payloads are individually charged at allocation capacity.
        // The owner-side profile is checked again after the worker completes;
        // source and decoded ownership remain individually lifetime-charged.
        job->workerArena = CapacityArena::create(32u << 20, state->reserve);
        job->registration = source.lifetime;
        const auto cancelled = [state, abandoned = ctx.isAbandoned] {
          return state->stopping.load() || abandoned();
        };
        job->wake = ctx.wake;
        auto task = [weak = std::weak_ptr<Job>(job),
                        cancelled,
                        wake = ctx.wake] {
          const auto job = weak.lock();
          if (!job)
            return;
          const auto &request = job->request;
          try {
            job->source = job->state->roots.read(request.assetRoot,
                request.fileName,
                4u << 20,
                cancelled,
                job->workerArena);
            if (job->source) {
              HdriDecodeLimits limits;
              limits.inputBytes = 4u << 20;
              limits.pixels = 1u << 20;
              limits.dimension = 4096;
              limits.workingBytes = 32u << 20;
              job->decoded = decodeHdri(
                  job->source->bytes(), limits, cancelled, job->workerArena);
              // Installed only while idle; State is immutable for the
              // duration of this worker. Never clone the host's target.
              if (job->state->workerObserver)
                job->state->workerObserver();
              job->sourceCurrent = job->source->isCurrent(cancelled);
            }
          } catch (...) {
            job->workerResourceFailed = true;
            job->decoded.error = HdriDecodeError::LIMIT;
          }
          wake();
        };
        // libstdc++ async's actual state and result allocation types. The
        // thread trampoline owns one member-function pointer plus a state
        // pointer and a virtual base; max_align_t rounding is conservative.
        constexpr size_t ASYNC_BYTES =
            allocation_profile::asyncBytes<decltype(task)>();
        job->asyncControl = state->reserve(ASYNC_BYTES);
        if (!job->asyncControl)
          return fail(Outcome::BUSY, "async_control_limit");
        job->worker = std::async(std::launch::async, std::move(task));
        job->started = true;
        Step pending;
        pending.status = Step::Status::BACKEND_PENDING;
        return pending;
      }
      job->worker.get();
      if (job->workerResourceFailed)
        return fail(Outcome::BUSY, "worker_backing_limit");
      if (!job->source || !job->sourceCurrent)
        return fail(Outcome::CONFLICT, "source_rejected_or_changed");
      if (job->decoded.error != HdriDecodeError::NONE)
        return fail(Outcome::INVALID_REQUEST, "decode_rejected");
      const auto destinationCurrent = [&] {
        if (job->registration.expired()
            || job->registration.lock() != source.lifetime.lock())
          return false;
        for (size_t i = 0; i < job->ancestry.size(); ++i) {
          const auto &entry = job->ancestry[i];
          const auto node = ids->resolve(entry.first);
          if (!node || !ids->isCurrent(entry.second)
              || node->topologyVersion() != job->topology[i])
            return false;
        }
        return true;
      };
      if (!destinationCurrent() || !job->source->metadataCurrent())
        return fail(Outcome::CONFLICT, "source_or_destination_changed");
      if (const auto *reason =
              scopedUsage(ctx.scene, nullptr, nullptr, {}, 6, 64))
        return fail(Outcome::BUSY, reason);
      scene::LayerNodeRef parent = job->destination
          ? ids->resolve(*job->destination)
          : scene::LayerNodeRef{};
      const auto &image = job->decoded.image;
      const bool ownLayer =
          !request.newLayer.empty() || !ctx.scene.numberOfLayers();
      core::StorageProfile profile(64u << 20);
      if (!PreparedHdriPublication::profileStorage(
              *ids, parent, image.width * image.height, ownLayer, profile))
        return fail(Outcome::BUSY, "preparation_profile_limit");
      job->preparation.acquire(profile.bytes(), state->reserve);
      // Reserve old/new overlap before any detached graph or pool growth.
      auto snapshot = std::allocate_shared<edit::HdriSnapshot>(
          CapacityAllocator<edit::HdriSnapshot>(state->reserve));
      snapshot->registration = source.lifetime;
      snapshot->scene = &ctx.scene;
      snapshot->reserveResult =
          operations::ResultOwnership::Reserve(state->reserve, RESULT_BACKING);
      snapshot->reserveRemoval = state->reserve;
      const auto batchId = state->edits->newBatchId();
      if (!batchId)
        return fail(Outcome::BUSY, "batch_identity_limit");
      job->batch = {state->edits->launchId(), *batchId};
      // One pending/live tree node (node-handle transfer), one scope node,
      // copied keys/order strings and the expired ID inheriting the scope
      // lease.
      const auto scopeBytes =
          edit::RestorationStore::hdriScopeBytes(job->batch);
      if (!scopeBytes)
        return fail(Outcome::BUSY, "scope_profile_limit");
      snapshot->scopeLease = state->reserve(scopeBytes);
      if (!snapshot->scopeLease)
        return fail(Outcome::BUSY, "scope_ownership_limit");
      snapshot->retainedBytes =
          allocation_profile::sharedControlBytes<edit::HdriSnapshot,
              CapacityAllocator<edit::HdriSnapshot>>()
          + sizeof(CapacityArena::Charge) + 2 * sizeof(std::max_align_t)
          + scopeBytes;
      core::StorageProfile recordProfile(64u << 10);
      if (!recordProfile.add(sizeof(edit::RestorationRecord))
          || !recordProfile.string(job->batch.launchId)
          || !recordProfile.string(job->batch.batchId))
        return fail(Outcome::BUSY, "record_profile_limit");
      edit::RestorationRecord record;
      record.ownership =
          operations::ResultOwnership(operations::ResultOwnership::Reserve(
              state->reserve, recordProfile.bytes()));
      record.ref = job->batch;
      record.scene = request.scene;
      // Two distinct retained strings: the engine's prepared fallback/result
      // and Activity's reserved receipt. Their physical charges already live
      // on those owners; policy admission counts them, not another lease.
      record.publicationBytes = 2 * (16384 + 1);
      record.image = snapshot;
      record.hdri = snapshot;
      const auto retained = edit::restorationBytes(record);
      const auto policy = *ctx.restorationPolicy;
      const std::string_view canonical(
          reinterpret_cast<const char *>(job->source->bytes().data()),
          job->source->bytes().size());
      if (!ctx.authorizeRecovery(retained,
              canonical,
              source.lifetime,
              state->reserve,
              "radiance_rgbe")) {
        Step result;
        result.outcome = Outcome::BUSY;
        result.result = policy.refusal(retained, ctx.approvalOffered);
        return result;
      }
      const auto layerToken = request.newLayer.empty()
          ? std::optional<scene::Token>{scene::Token{}}
          : scene::Token::internBounded(request.newLayer, [&](size_t bytes) {
              job->tokenPreparation.acquire(bytes, state->reserve);
              return true;
            });
      if (!layerToken)
        return fail(Outcome::BUSY, "token_profile_limit");
      PreparedHdriPublication prepared(*ids,
          parent,
          request.rootName.c_str(),
          image.width,
          image.height,
          image.radiance.data(),
          request.settings,
          *layerToken);
      job->preparation.liveBacking =
          image.width * image.height * sizeof(math::float3);
      job->receipt = "{\"committed\":true,\"kind\":\"hdri\",\"primary\":"
          + prepared.inventoryReceipt()
          + ",\"effective_settings\":" + settingsJson
          + ",\"root_name\":" + mcp::json::quote(request.rootName)
          + ",\"width\":" + std::to_string(image.width)
          + ",\"height\":" + std::to_string(image.height) + ",\"owned_layer\":"
          + (ownLayer ? "true" : "false") + ",\"worker_allocator_peak_bytes\":"
          + std::to_string(job->workerArena->allocatorPeak())
          + ",\"preparation_peak_bound_bytes\":"
          + std::to_string(job->preparation.peakBound)
          + ",\"initial_preparation_peak_bound_bytes\":"
          + std::to_string(job->initialPreparation.peakBound)
          + ",\"live_payload_adopted_bytes\":"
          + std::to_string(job->preparation.liveBacking)
          + ",\"live_scene_backing_after_prepare_bound_bytes\":"
          + std::to_string(prepared.liveBackingBytes())
          + ",\"borrowed_resources\":[],\"batch_ref\":"
          + edit::detail::batchRefJson(job->batch)
          + ",\"restoration_policy\":" + policy.receipt(retained) + "}";
      if (job->receipt.size() > 16384
          || !ctx.prepareActivityResult(16384, snapshot->reserveResult))
        return fail(Outcome::BUSY, "result_ownership_limit");
      ctx.prepareCommit(job->receipt, 16384);
      if (!ctx.reserveRestoration(
              retained, job->batch.launchId, job->batch.batchId)) {
        Step result;
        result.outcome = Outcome::BUSY;
        result.result = policy.refusal(retained);
        return result;
      }
      if (!state->edits->restorations().prepareImage(std::move(record)))
        return fail(Outcome::BUSY, "store_limit");
      job->preparedStore = true;
      state->edits->syncStates().prepareScene(request.scene);
      const auto authority = ctx.beforeWrite();
      if (authority != Outcome::OK)
        return fail(authority, "authority_withdrawn");
      if (!destinationCurrent() || !job->source->metadataCurrent()
          || !prepared.isCurrent()
          || !integration->imageTrackingAvailable(request.scene))
        return fail(Outcome::CONFLICT, "final_guard_changed");
      if (ctx.consumeRecoveryApproval && !ctx.consumeRecoveryApproval())
        return fail(Outcome::FORBIDDEN, "approval_invalidated");
      if (!prepared.commit())
        return fail(Outcome::CONFLICT, "destination_changed");
      job->committed = true;
      ctx.markCommitted();
      // Commit has adopted the complete graph and IDs; sealing is allocation-
      // free. Store publication precedes every notification, even a failure.
      if (!snapshot->evidence.seal(*ids, prepared.handles(), ownLayer))
        job->notificationFailed = true;
      state->edits->restorations().publishImage(job->batch);
      job->preparedStore = false;
      ctx.publishRestoration();
      state->edits->restorations().expireUnless(
          [&](const edit::BatchRef &batch) {
            return ctx.restorationRetained(batch.launchId, batch.batchId);
          });
      try {
        prepared.notify();
      } catch (...) {
        job->notificationFailed = true;
      }
      job->source.reset();
      job->decoded.image.radiance.clear();
      DecodedHdri::Radiance().swap(job->decoded.image.radiance);
      job->hookPending = true;
      state->edits->syncStates().beginHook(request.scene);
      Step result;
      result.committed = true;
      result.status = Step::Status::SYNCHRONIZING;
      return result;
    } catch (...) {
      if (job->committed) {
        job->notificationFailed = true;
        Step result;
        result.committed = true;
        result.status = Step::Status::SYNCHRONIZING;
        return result;
      }
      return fail(Outcome::BUSY, "preparation_resource_limit");
    }
  };
  return m_engine->submit(credentials, ref, std::move(operation));
}

} // namespace vsr::integration::content

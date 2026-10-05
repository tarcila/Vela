// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Header-only: the vsr_integration library globs only top-level sources (see
// ticket 19).

// vsr_integration
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/activity/Activity.hpp"
#include "vsr/integration/identity/Identity.hpp"
#include "vsr/integration/image/ImageNormalization.hpp"
#include "vsr/integration/inspection/ViewInfo.hpp"
#include "vsr/integration/operations/Operations.hpp"
#include "vsr/integration/sync/SceneSync.hpp"
// std
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace vsr::integration::render {

/*
 * Revision-pinned rendering of a registered view (spec §12, deadlines §5).
 *
 * A render names a view plus the scene/view revision tokens the client
 * observed. At the safe point both must still be current, the view's
 * dimensions valid (1..1024 per axis, owned by the view, never clamped) and
 * its scene synchronization-ready; otherwise the request ends superseded /
 * invalid_request / sync_required without backend work. The same token and
 * readiness checks repeat when the backend finishes, so a relevant change
 * mid-render prevents success.
 *
 * The host supplies one RenderBackend per view and calls pump() from its
 * loop so in-flight backend work is re-examined at a later safe point.
 * Cancelled/timed-out work never succeeds from a late image; its backend
 * resources (and scene slot) stay held until RenderBackend::release()
 * reports safe release. While any render holds the view's backend resources
 * new renders of that view reject busy before admission (one frame per
 * view; ticket 91 records this assumption).
 *
 * Threading: registerView() before any render; setDimensions(), pump(),
 * submit*() and result*() are thread-safe. Backends are only driven on the
 * servicing thread.
 */

using identity::SceneRevision;
using identity::ViewRevision;
using operations::LocalAdmission;
using operations::LocalRef;
using operations::OperationRef;
using operations::Outcome;

constexpr std::uint32_t MAX_RENDER_DIMENSION = 1024;
constexpr double RENDER_DEADLINE_SECONDS = 30.0;

inline bool validDimensions(std::uint32_t w, std::uint32_t h)
{
  return w >= 1 && h >= 1 && w <= MAX_RENDER_DIMENSION
      && h <= MAX_RENDER_DIMENSION;
}

enum class BackendState
{
  PENDING,
  DONE,
  FAULT // an actual backend-reported fault
};

// Host-owned rendering resources of one view. Called on the servicing thread.
struct RenderBackend
{
  virtual ~RenderBackend() = default;
  // Starts one frame at exactly these dimensions; false on failure.
  virtual bool begin(std::uint32_t width, std::uint32_t height) = 0;
  // Non-blocking.
  virtual BackendState poll() = 0;
  // Valid after DONE until release().
  virtual image::SourceImage image() = 0;
  // Optional value-only attestation of the configuration that produced image().
  // Never infer this from a current UI selection. Default is explicitly
  // unknown.
  virtual std::optional<EffectiveViewBinding> renderedBinding() const;
  // Releases frame resources; false while release is not yet safe (retried
  // at later safe points, indefinitely if necessary).
  virtual bool release() = 0;

  // Optional device selection. A backend that supports it lists the Device
  // Identifiers an agent may select (host-chosen; nothing else is accepted),
  // reports the current one, and switches on selectDevice(). A failed switch
  // must keep the current device bound. Called on the servicing thread only
  // while no render holds the backend's resources.
  virtual std::vector<std::string> deviceCandidates() const;
  virtual std::string deviceIdentifier() const;
  virtual bool selectDevice(const std::string &deviceIdentifier);

  // Optional camera control of the backend's own (non-scene) camera. A
  // backend that supports it returns true from cameraControllable() and
  // applies validated poses in setCamera(); fovyDegrees <= 0 keeps the
  // current field of view. Called on the servicing thread only while no
  // render holds the backend's resources.
  virtual bool cameraControllable() const;
  virtual bool setCamera(const std::array<double, 3> &position,
      const std::array<double, 3> &direction,
      const std::array<double, 3> &up,
      double fovyDegrees);
};

inline std::optional<EffectiveViewBinding> RenderBackend::renderedBinding()
    const
{
  return {};
}

inline std::vector<std::string> RenderBackend::deviceCandidates() const
{
  return {};
}

inline std::string RenderBackend::deviceIdentifier() const
{
  return {};
}

inline bool RenderBackend::selectDevice(const std::string &)
{
  return false;
}

inline bool RenderBackend::cameraControllable() const
{
  return false;
}

inline bool RenderBackend::setCamera(const std::array<double, 3> &,
    const std::array<double, 3> &,
    const std::array<double, 3> &,
    double)
{
  return false;
}

struct RenderRequest
{
  ViewId view{INVALID_VIEW_ID};
  SceneRevision sceneRevision;
  ViewRevision viewRevision;
};

struct DeviceSelectionRequest
{
  ViewId view{INVALID_VIEW_ID};
  std::string deviceIdentifier;
};

// World-space eye pose. `direction` and `up` need not be normalized but must
// be non-zero and not parallel; fovyDegrees <= 0 keeps the current value,
// otherwise it must lie in (0, 180).
struct CameraRequest
{
  ViewId view{INVALID_VIEW_ID};
  std::array<double, 3> position{0., 0., 0.};
  std::array<double, 3> direction{0., 0., -1.};
  std::array<double, 3> up{0., 1., 0.};
  double fovyDegrees{0.};
};

// True when `request`'s pose is finite and well-formed (see CameraRequest).
bool validCameraPose(const CameraRequest &request);

struct RenderResult
{
  Outcome outcome{Outcome::OK};
  // Present on success only.
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::vector<std::uint8_t> png;
  bool imageEvicted{false};
  SceneRevision sceneRevision; // exactly the matched tokens
  ViewRevision viewRevision;
  std::uint64_t capture{0}; // unique per fresh capture; replays keep it
};

class RenderService
{
 public:
  RenderService(Integration &integration,
      operations::OperationEngine &engine,
      const sync::SyncService &sync,
      activity::ActivityLog &log);
  ~RenderService();
  RenderService(const RenderService &) = delete;
  RenderService &operator=(const RenderService &) = delete;

  // Host: a registered view, its scene identity, revision tracker and
  // backend. All must outlive this service.
  bool attachView(ViewId view,
      identity::SceneIdentity &scene,
      identity::ViewRevisionTracker &tracker,
      RenderBackend &backend);
  // Host: view-owned dimensions (also advance the view revision).
  void setDimensions(ViewId view, std::uint32_t width, std::uint32_t height);

  // Invalid dimensions and views with resources held by abandoned work
  // reject before admission (invalid_request / busy).
  operations::Admission submit(const operations::Credentials &credentials,
      const OperationRef &ref,
      RenderRequest request);
  LocalAdmission submitLocal(RenderRequest request);

  // Agent override of the view's render device. Rejects invalid_request
  // before admission when the view's backend offers no selection or the
  // identifier is not one of its candidates, and busy while a render holds
  // the view. At the safe point the backend switches; on success the view
  // revision advances (pending renders end superseded). A switch that
  // fails keeps the previous device and is still terminal ok, with
  // `applied:false` (as a failed resynchronization is).
  operations::Admission submitSelectDevice(
      const operations::Credentials &credentials,
      const OperationRef &ref,
      DeviceSelectionRequest request);

  // Agent pose of a view's own camera. Rejects invalid_request before
  // admission when the view's backend offers no camera control or the pose
  // is malformed, and busy while a render holds the view. At the safe point
  // the backend applies it and the view revision advances (pending renders
  // end superseded). Scene cameras and interactive viewports are untouched.
  operations::Admission submitSetCamera(
      const operations::Credentials &credentials,
      const OperationRef &ref,
      CameraRequest request);

  std::optional<RenderResult> result(const OperationRef &ref) const;
  std::optional<RenderResult> resultLocal(LocalRef ref) const;

  // Host loop: re-examines in-flight backend work at the next safe point.
  void pump();
  // Backend begin() calls so far (fresh-capture evidence for tests/metrics).
  std::uint64_t backendStarts() const;

 private:
  struct Job
  {
    bool started{false};
    bool viewHeld{false};
    std::string provenance;
    // Guard/fault terminated the request but release was not yet safe.
    std::optional<Outcome> pendingOutcome;
    std::function<void()> wake;
    std::optional<RenderResult> result;
    std::optional<RenderResult> pendingResult; // until safe release
    std::optional<OperationRef> ref;
    std::optional<LocalRef> localRef;
    std::optional<activity::ActivityLog::Id> logId;
  };
  using JobPtr = std::shared_ptr<Job>;

  struct View
  {
    identity::SceneIdentity *scene{nullptr};
    identity::ViewRevisionTracker *tracker{nullptr};
    RenderBackend *backend{nullptr};
    std::uint32_t width{0};
    std::uint32_t height{0};
    Job *holder{nullptr}; // job holding backend resources
    bool holderAbandoned{false};
  };

  std::optional<Outcome> preAdmission(const RenderRequest &request) const;
  std::optional<Outcome> preAdmission(
      const DeviceSelectionRequest &request) const;
  operations::Step selectDevice(const DeviceSelectionRequest &request);
  std::optional<Outcome> preAdmission(const CameraRequest &request) const;
  operations::Step setCamera(const CameraRequest &request);
  operations::Request makeRequest(RenderRequest request, JobPtr job);
  operations::Step step(
      const RenderRequest &request, Job &job, operations::StepContext &ctx);
  operations::Step finishJob(Job &job, Outcome outcome);
  void record(Job &job, ViewId view);
  void completeSuccess(Job &job, ViewId view, operations::Step &s);

  Integration &m_integration;
  operations::OperationEngine &m_engine;
  const sync::SyncService &m_sync;
  activity::ActivityLog &m_log;
  std::shared_ptr<bool> m_alive;

  mutable std::mutex m_mutex;
  std::map<ViewId, View> m_views;
  std::map<std::pair<std::string, std::uint64_t>, JobPtr> m_results;
  std::map<std::uint64_t, JobPtr> m_localResults;
  std::vector<std::weak_ptr<Job>> m_inFlight;
  std::uint64_t m_starts{0};
  std::uint64_t m_captures{0};
};

///////////////////////////////////////////////////////////////////////////////
// Inlined definitions ////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

namespace detail {

template <typename R>
inline std::string revisionText(const R &r)
{
  return std::to_string(r.launch.activation) + "."
      + std::to_string(r.launch.nonce) + "." + std::to_string(r.domain) + "."
      + std::to_string(r.value);
}

} // namespace detail

inline RenderService::RenderService(Integration &integration,
    operations::OperationEngine &engine,
    const sync::SyncService &sync,
    activity::ActivityLog &log)
    : m_integration(integration),
      m_engine(engine),
      m_sync(sync),
      m_log(log),
      m_alive(std::make_shared<bool>(true))
{}

inline RenderService::~RenderService()
{
  m_alive.reset();
}

inline bool RenderService::attachView(ViewId view,
    identity::SceneIdentity &scene,
    identity::ViewRevisionTracker &tracker,
    RenderBackend &backend)
{
  if (!m_integration.isRegisteredView(view))
    return false;
  std::lock_guard<std::mutex> lock(m_mutex);
  auto &v = m_views[view];
  v.scene = &scene;
  v.tracker = &tracker;
  v.backend = &backend;
  return true;
}

inline void RenderService::setDimensions(
    ViewId view, std::uint32_t width, std::uint32_t height)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_views.find(view);
  if (it == m_views.end())
    return;
  it->second.width = width;
  it->second.height = height;
  it->second.tracker->setDimensions(width, height);
}

inline std::optional<Outcome> RenderService::preAdmission(
    const RenderRequest &request) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_views.find(request.view);
  if (it == m_views.end())
    return Outcome::INVALID_REQUEST;
  if (!validDimensions(it->second.width, it->second.height))
    return Outcome::INVALID_REQUEST; // never clamped or resized
  if (it->second.holder)
    return Outcome::BUSY; // necessary view resources held (see ticket 91)
  return std::nullopt;
}

inline operations::Request RenderService::makeRequest(
    RenderRequest request, JobPtr job)
{
  operations::Request r;
  r.kind = "render";
  r.target = session::Target::view(request.view);
  r.capability = session::Capability::RENDER;
  r.canonicalArguments = "render;view=" + std::to_string(request.view)
      + ";scene=" + detail::revisionText(request.sceneRevision)
      + ";view_revision=" + detail::revisionText(request.viewRevision);
  r.deadlineSeconds = RENDER_DEADLINE_SECONDS;
  std::weak_ptr<bool> alive = m_alive;
  r.executor = [this, alive, request, job](operations::StepContext &ctx) {
    if (!alive.lock())
      return operations::Step{};
    return step(request, *job, ctx);
  };
  return r;
}

inline operations::Step RenderService::finishJob(Job &job, Outcome outcome)
{
  operations::Step s;
  s.status = operations::Step::Status::COMPLETE;
  s.outcome = outcome;
  if (!job.result)
    job.result = RenderResult{};
  job.result->outcome = outcome;
  return s;
}

inline operations::Step RenderService::step(
    const RenderRequest &request, Job &job, operations::StepContext &ctx)
{
  using S = operations::Step::Status;
  View *v = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_views.find(request.view);
    if (it != m_views.end())
      v = &it->second;
  }

  // Abandoned work only releases resources; never produces an image.
  if (ctx.abandoned) {
    if (job.viewHeld && v) {
      {
        std::lock_guard<std::mutex> lock(m_mutex);
        v->holderAbandoned = true;
      }
      if (!v->backend->release()) {
        operations::Step s;
        s.status = S::BACKEND_PENDING;
        return s;
      }
      std::lock_guard<std::mutex> lock(m_mutex);
      v->holder = nullptr;
      v->holderAbandoned = false;
      job.viewHeld = false;
    }
    return operations::Step{};
  }

  if (!v)
    return finishJob(job, Outcome::INVALID_REQUEST);

  auto tokensMatch = [&] {
    return v->scene->sceneRevision() == request.sceneRevision
        && v->tracker->revision() == request.viewRevision;
  };
  auto releaseOrHold = [&](Outcome outcome) {
    if (!v->backend->release()) {
      // Guard terminated the request; resources stay held until safe.
      std::lock_guard<std::mutex> lock(m_mutex);
      v->holderAbandoned = true;
      job.pendingOutcome = outcome;
      operations::Step s;
      s.status = S::BACKEND_PENDING;
      return s;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    v->holder = nullptr;
    v->holderAbandoned = false;
    job.viewHeld = false;
    return finishJob(job, outcome);
  };

  job.wake = ctx.wake;
  if (job.pendingOutcome) {
    const Outcome o = *job.pendingOutcome;
    job.pendingOutcome.reset();
    if (o == Outcome::OK) {
      // Success waits on safe release too; not reported before it.
      operations::Step s = releaseOrHold(Outcome::OK);
      if (s.status != S::COMPLETE) {
        job.pendingOutcome = Outcome::OK;
        return s;
      }
      // The delay may have outlived the completion guards: re-check them.
      if (!tokensMatch() || !m_sync.viewReady(request.view)) {
        job.pendingResult.reset();
        return finishJob(
            job, tokensMatch() ? Outcome::SYNC_REQUIRED : Outcome::SUPERSEDED);
      }
      completeSuccess(job, request.view, s);
      return s;
    }
    return releaseOrHold(o);
  }

  if (!job.started) {
    if (!tokensMatch())
      return finishJob(job, Outcome::SUPERSEDED);
    std::uint32_t w, h;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      w = v->width;
      h = v->height;
      if (v->holder)
        return finishJob(job, Outcome::BUSY);
    }
    if (!validDimensions(w, h))
      return finishJob(job, Outcome::INVALID_REQUEST);
    if (!m_sync.viewReady(request.view))
      return finishJob(job, Outcome::SYNC_REQUIRED);
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      v->holder = &job;
      v->holderAbandoned = false;
      job.viewHeld = true;
      job.started = true;
      job.wake = ctx.wake;
      ++m_starts;
    }
    if (!v->backend->begin(w, h)) {
      operations::Step s = releaseOrHold(Outcome::UNKNOWN);
      s.backendFault = true;
      return s;
    }
    operations::Step s;
    s.status = S::BACKEND_PENDING;
    return s;
  }

  switch (v->backend->poll()) {
  case BackendState::PENDING: {
    operations::Step s;
    s.status = S::BACKEND_PENDING;
    return s;
  }
  case BackendState::FAULT: {
    operations::Step s = releaseOrHold(Outcome::UNKNOWN);
    s.backendFault = true;
    return s;
  }
  case BackendState::DONE:
    break;
  }

  // Completion guards: relevant changes and readiness loss prevent success.
  if (!tokensMatch())
    return releaseOrHold(Outcome::SUPERSEDED);
  if (!m_sync.viewReady(request.view))
    return releaseOrHold(Outcome::SYNC_REQUIRED);

  auto normalized = image::normalizeToPng(v->backend->image());
  if (!normalized.ok())
    return releaseOrHold(Outcome::UNKNOWN);
  RenderResult r;
  r.width = normalized.image.width;
  r.height = normalized.image.height;
  r.png = std::move(normalized.image.png);
  r.sceneRevision = request.sceneRevision;
  r.viewRevision = request.viewRevision;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    r.capture = ++m_captures;
  }
  auto description = m_integration.viewDescription(request.view);
  job.provenance = ",\"view_id\":" + std::to_string(request.view)
      + ",\"image_kind\":\"independent_render\"";
  if (description)
    job.provenance += ","
        + inspection::viewinfo::identityJson(
            *description, inspection::viewinfo::jsonString);
  auto rendered = v->backend->renderedBinding();
  const bool attested = rendered && !inspection::viewinfo::validate(*rendered)
      && rendered->width == r.width && rendered->height == r.height;
  job.provenance += ",\"rendered_configuration\":{"
      + (attested
              ? inspection::viewinfo::bindingJson(
                    *rendered, inspection::viewinfo::jsonString, true)
              : std::string(
                    "\"state\":\"unknown\",\"reason\":\"legacy_backend_not_attested\""))
      + "}";
  job.pendingResult = std::move(r);
  operations::Step s = releaseOrHold(Outcome::OK);
  if (s.status == S::COMPLETE)
    completeSuccess(job, request.view, s);
  return s;
}

inline void RenderService::completeSuccess(
    Job &job, ViewId view, operations::Step &s)
{
  job.result = std::move(job.pendingResult);
  job.pendingResult.reset();
  job.result->outcome = Outcome::OK;
  record(job, view);
  s.result = "{\"width\":" + std::to_string(job.result->width) + ",\"height\":"
      + std::to_string(job.result->height) + ",\"scene_revision\":\""
      + detail::revisionText(job.result->sceneRevision)
      + "\",\"view_revision\":\""
      + detail::revisionText(job.result->viewRevision) + "\",\"capture\":"
      + std::to_string(job.result->capture) + job.provenance + "}";
}

inline void RenderService::record(Job &job, ViewId view)
{
  // Completed image stored through the activity model and its image budget.
  activity::Entry e;
  e.ref = job.ref;
  e.localRef = job.localRef;
  e.kind = activity::OperationKind::RENDER;
  e.targets = activity::coveredTargets(activity::OperationKind::RENDER,
      session::Target::view(view),
      m_integration);
  e.terminal = Outcome::OK;
  e.image = job.result->png;
  job.logId = m_log.append(std::move(e));
  if (!job.logId) {
    job.result->imageEvicted = true;
    return;
  }
  auto stored = m_log.get(*job.logId);
  job.result->imageEvicted = !stored || stored->imageEvicted;
}

inline operations::Admission RenderService::submit(
    const operations::Credentials &credentials,
    const OperationRef &ref,
    RenderRequest request)
{
  bool known = false;
  {
    // Identical retries replay without new work, even when now busy.
    std::lock_guard<std::mutex> lock(m_mutex);
    known = m_results.count({ref.sessionId, ref.requestId}) > 0;
  }
  if (!known) {
    if (auto rejected = preAdmission(request)) {
      operations::Admission a;
      a.code = *rejected;
      return a;
    }
  }
  auto job = std::make_shared<Job>();
  job->ref = ref;
  auto a = m_engine.submit(credentials, ref, makeRequest(request, job));
  if (a.code == Outcome::OK && !a.replayed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_results[{ref.sessionId, ref.requestId}] = job;
    m_inFlight.push_back(job);
  }
  return a;
}

inline LocalAdmission RenderService::submitLocal(RenderRequest request)
{
  if (auto rejected = preAdmission(request)) {
    LocalAdmission a;
    a.code = *rejected;
    return a;
  }
  auto job = std::make_shared<Job>();
  auto a = m_engine.submitLocal(makeRequest(request, job));
  if (a.code == Outcome::OK) {
    std::lock_guard<std::mutex> lock(m_mutex);
    job->localRef = a.ref;
    m_localResults[a.ref.id] = job;
    m_inFlight.push_back(job);
  }
  return a;
}

inline std::optional<RenderResult> RenderService::result(
    const OperationRef &ref) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_results.find({ref.sessionId, ref.requestId});
  if (it == m_results.end())
    return std::nullopt;
  return it->second->result;
}

inline std::optional<RenderResult> RenderService::resultLocal(
    LocalRef ref) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_localResults.find(ref.id);
  if (it == m_localResults.end())
    return std::nullopt;
  return it->second->result;
}

inline void RenderService::pump()
{
  std::vector<std::function<void()>> wakes;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::weak_ptr<Job>> keep;
    for (auto &w : m_inFlight) {
      auto j = w.lock();
      if (!j)
        continue;
      if (j->wake)
        wakes.push_back(j->wake);
      keep.push_back(w);
    }
    m_inFlight.swap(keep);
  }
  for (auto &w : wakes)
    w();
}

inline std::uint64_t RenderService::backendStarts() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_starts;
}

inline std::optional<Outcome> RenderService::preAdmission(
    const DeviceSelectionRequest &request) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_views.find(request.view);
  if (it == m_views.end())
    return Outcome::INVALID_REQUEST;
  const auto candidates = it->second.backend->deviceCandidates();
  if (candidates.empty())
    return Outcome::INVALID_REQUEST; // backend offers no selection
  if (!request.deviceIdentifier.empty()
      && std::find(candidates.begin(),
             candidates.end(),
             request.deviceIdentifier)
          == candidates.end())
    return Outcome::INVALID_REQUEST;
  if (it->second.holder)
    return Outcome::BUSY;
  return std::nullopt;
}

inline operations::Step RenderService::selectDevice(
    const DeviceSelectionRequest &request)
{
  operations::Step s;
  s.status = operations::Step::Status::COMPLETE;
  View *v = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_views.find(request.view);
    if (it == m_views.end()) {
      s.outcome = Outcome::INVALID_REQUEST;
      return s;
    }
    if (it->second.holder) {
      s.outcome = Outcome::BUSY;
      return s;
    }
    v = &it->second;
  }

  const auto previous = v->backend->deviceIdentifier();
  bool applied = false;
  if (!request.deviceIdentifier.empty()
      && request.deviceIdentifier != previous) {
    applied = v->backend->selectDevice(request.deviceIdentifier);
    if (applied) {
      std::lock_guard<std::mutex> lock(m_mutex);
      v->tracker->notePipelineChanged();
      s.committed = true;
    }
  }

  using inspection::viewinfo::jsonString;
  std::string candidates;
  for (const auto &c : v->backend->deviceCandidates())
    candidates += (candidates.empty() ? "" : ",") + jsonString(c);
  s.result = std::string("{\"applied\":") + (applied ? "true" : "false")
      + ",\"device_identifier\":"
      + jsonString(v->backend->deviceIdentifier())
      + ",\"previous_device_identifier\":" + jsonString(previous)
      + ",\"candidates\":[" + candidates + "]}";
  return s;
}

inline operations::Admission RenderService::submitSelectDevice(
    const operations::Credentials &credentials,
    const OperationRef &ref,
    DeviceSelectionRequest request)
{
  if (auto rejected = preAdmission(request)) {
    operations::Admission a;
    a.code = *rejected;
    return a;
  }
  operations::Request r;
  r.kind = "select_device";
  r.target = session::Target::view(request.view);
  r.capability = session::Capability::RENDER;
  r.canonicalArguments = "select_device;view=" + std::to_string(request.view)
      + ";device=" + request.deviceIdentifier;
  r.deadlineSeconds = RENDER_DEADLINE_SECONDS;
  std::weak_ptr<bool> alive = m_alive;
  r.executor = [this, alive, request](operations::StepContext &ctx) {
    if (!alive.lock() || ctx.abandoned)
      return operations::Step{};
    return selectDevice(request);
  };
  return m_engine.submit(credentials, ref, std::move(r));
}


inline bool validCameraPose(const CameraRequest &request)
{
  auto finite = [](const std::array<double, 3> &v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
  };
  auto length2 = [](const std::array<double, 3> &v) {
    return v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
  };
  if (!finite(request.position) || !finite(request.direction)
      || !finite(request.up) || !std::isfinite(request.fovyDegrees))
    return false;
  const double d2 = length2(request.direction);
  const double u2 = length2(request.up);
  if (!(d2 > 0.) || !(u2 > 0.))
    return false;
  const auto &d = request.direction;
  const auto &u = request.up;
  const std::array<double, 3> cross{d[1] * u[2] - d[2] * u[1],
      d[2] * u[0] - d[0] * u[2],
      d[0] * u[1] - d[1] * u[0]};
  // Reject (near-)parallel direction/up: sin^2(angle) below 1e-12.
  if (!(length2(cross) > 1e-12 * d2 * u2))
    return false;
  return request.fovyDegrees <= 0.
      || (request.fovyDegrees > 0. && request.fovyDegrees < 180.);
}

inline std::optional<Outcome> RenderService::preAdmission(
    const CameraRequest &request) const
{
  if (!validCameraPose(request))
    return Outcome::INVALID_REQUEST;
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_views.find(request.view);
  if (it == m_views.end() || !it->second.backend->cameraControllable())
    return Outcome::INVALID_REQUEST;
  if (it->second.holder)
    return Outcome::BUSY;
  return std::nullopt;
}

inline operations::Step RenderService::setCamera(const CameraRequest &request)
{
  operations::Step s;
  s.status = operations::Step::Status::COMPLETE;
  View *v = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_views.find(request.view);
    if (it == m_views.end()) {
      s.outcome = Outcome::INVALID_REQUEST;
      return s;
    }
    if (it->second.holder) {
      s.outcome = Outcome::BUSY;
      return s;
    }
    v = &it->second;
  }
  const bool applied = v->backend->setCamera(
      request.position, request.direction, request.up, request.fovyDegrees);
  if (applied) {
    std::lock_guard<std::mutex> lock(m_mutex);
    v->tracker->notePipelineChanged();
    s.committed = true;
  } else
    s.outcome = Outcome::UNKNOWN;
  s.result = std::string("{\"applied\":") + (applied ? "true" : "false")
      + ",\"view_revision\":\"" + detail::revisionText(v->tracker->revision())
      + "\"}";
  return s;
}

inline operations::Admission RenderService::submitSetCamera(
    const operations::Credentials &credentials,
    const OperationRef &ref,
    CameraRequest request)
{
  if (auto rejected = preAdmission(request)) {
    operations::Admission a;
    a.code = *rejected;
    return a;
  }
  auto num = [](double x) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.17g", x);
    return std::string(buf);
  };
  auto vec = [&](const std::array<double, 3> &v) {
    return num(v[0]) + "," + num(v[1]) + "," + num(v[2]);
  };
  operations::Request r;
  r.kind = "set_view_camera";
  r.target = session::Target::view(request.view);
  r.capability = session::Capability::RENDER;
  r.canonicalArguments = "set_view_camera;view=" + std::to_string(request.view)
      + ";position=" + vec(request.position) + ";direction="
      + vec(request.direction) + ";up=" + vec(request.up)
      + ";fovy=" + num(request.fovyDegrees);
  r.deadlineSeconds = RENDER_DEADLINE_SECONDS;
  std::weak_ptr<bool> alive = m_alive;
  r.executor = [this, alive, request](operations::StepContext &ctx) {
    if (!alive.lock() || ctx.abandoned)
      return operations::Step{};
    return setCamera(request);
  };
  return m_engine.submit(credentials, ref, std::move(r));
}

} // namespace vsr::integration::render

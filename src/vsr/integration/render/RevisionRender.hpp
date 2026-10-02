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
#include "vsr/integration/operations/Operations.hpp"
#include "vsr/integration/sync/SceneSync.hpp"
// std
#include <cstdint>
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
  // Releases frame resources; false while release is not yet safe (retried
  // at later safe points, indefinitely if necessary).
  virtual bool release() = 0;
};

struct RenderRequest
{
  ViewId view{INVALID_VIEW_ID};
  SceneRevision sceneRevision;
  ViewRevision viewRevision;
};

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
      + detail::revisionText(job.result->viewRevision)
      + "\",\"capture\":" + std::to_string(job.result->capture) + "}";
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

} // namespace vsr::integration::render

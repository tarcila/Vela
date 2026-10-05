// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/inspection/ViewInfo.hpp"
#include "vsr/integration/operations/Operations.hpp"
// vsr_rendering
#include "vsr/rendering/view/FrameObservation.h"
// POSIX (qualified fixed-stack CPU worker; other hosts remain unexposed)
#if defined(__linux__)
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
// std
#include <condition_variable>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>

namespace vsr::integration::frame {

enum class SourceQualification
{
  SYNCHRONOUS_CPU,
  VIEWER_CPU_READY,
  VIEWER_UNQUALIFIED
};

struct Production
{
  Production() = default;
  VSR_NOT_COPYABLE(Production)
  VSR_DEFAULT_MOVEABLE(Production)
  ViewId view{INVALID_VIEW_ID};
  std::uint64_t nextProduction{0};
  std::uint64_t viewerEpoch{0};
  identity::SceneRevision sceneRevision;
  identity::ViewRevision viewRevision;
  identity::CapturePolicyRevision policyRevision;
  EffectiveViewBinding binding;
  std::string configuration;
  image::RowOrigin origin{image::RowOrigin::TOP_LEFT};
  image::AlphaMode alpha{image::AlphaMode::STRAIGHT};
  std::shared_ptr<const void> reservation;
};

// Immutable acquisition made at the natural CPU-ready SDL upload. Its bytes
// are not a presented frame until the checked display submission is reported.
struct ViewerAcquisition
{
  std::shared_ptr<const Production> production;
  std::shared_ptr<const rendering::CpuFrameSnapshot> snapshot;
  std::shared_ptr<const void> reservation;
  std::uint64_t epoch{0};
};

struct EncodedFrame
{
  EncodedFrame() = default;
  VSR_NOT_COPYABLE(EncodedFrame)
  VSR_NOT_MOVEABLE(EncodedFrame)
  std::uint32_t width{0}, height{0};
  std::vector<std::uint8_t> png;
  std::shared_ptr<const void> reservation;
};

// Qualification is an explicit host declaration, never implied by view role.
// SYNCHRONOUS_CPU covers final HOST ImagePipeline producers; VIEWER_CPU_READY
// additionally requires real render-start stamps and the natural completed
// host/managed-host SDL transfer. It does not qualify CUDA-GL interop/readback.
// Owning-thread producers publish immutable results using one shared budget.
class FrameService
{
 public:
  FrameService(Integration &integration,
      session::SessionAuthority &authority,
      operations::OperationEngine &engine,
      session::MonotonicClock clock = session::steadyClock(),
      std::shared_ptr<rendering::FrameBudget> budget =
          std::make_shared<rendering::FrameBudget>());
  ~FrameService();
  VSR_NOT_COPYABLE(FrameService)
  VSR_NOT_MOVEABLE(FrameService)
  bool attachView(ViewId view,
      identity::SceneIdentity &scene,
      identity::ViewRevisionTracker &tracker,
      SourceQualification qualification = SourceQualification::SYNCHRONOUS_CPU);
  bool setExposed(ViewId view, bool exposed);
  std::optional<Production> beginProduction(ViewId view,
      const rendering::ImagePipeline &pipeline,
      image::RowOrigin origin = image::RowOrigin::TOP_LEFT,
      image::AlphaMode alpha = image::AlphaMode::STRAIGHT);
  bool present(ViewId view,
      const rendering::ImagePipeline &pipeline,
      std::optional<Production> production,
      const rendering::CpuFrameObservation::Submit &submit);
  // Owning viewer thread only; never services queued Scene/operation work.
  using ViewerBinding = std::function<EffectiveViewBinding()>;
  std::shared_ptr<const Production> beginViewerProduction(
      ViewId view, const ViewerBinding &binding);
  std::shared_ptr<const ViewerAcquisition> acquireViewerPixels(ViewId view,
      std::shared_ptr<const Production> production,
      const ViewerBinding &output,
      const std::uint32_t *pixels,
      std::uint32_t width,
      std::uint32_t height,
      std::string_view transferPath = {});
  void presentViewerPixels(ViewId view,
      std::shared_ptr<const ViewerAcquisition> acquired,
      std::uint32_t width,
      std::uint32_t height,
      double displayWidth,
      double displayHeight,
      double submittedAt,
      double framebufferScaleX = 1.,
      double framebufferScaleY = 1.);
  void retireViewer(ViewId view);
  void detachViewer(ViewId view);
  std::shared_ptr<const void> reserveViewerConnection();
  operations::Admission submit(const operations::Credentials &credentials,
      const operations::OperationRef &ref,
      ViewId view);
  std::shared_ptr<const EncodedFrame> result(
      const operations::Credentials &credentials,
      const operations::OperationRef &ref) const;
  std::shared_ptr<const void> reserveDelivery(std::size_t responseBytes) const;
  std::size_t ownedBytes() const;
  void pump(); // owning-thread resource expiry/withdrawal, no rendering
 private:
  struct View
  {
    std::shared_ptr<const void> infrastructure;
    identity::SceneIdentity *scene{nullptr};
    identity::ViewRevisionTracker *tracker{nullptr};
    std::unique_ptr<rendering::CpuFrameObservation> source;
    bool exposed{false};
    SourceQualification qualification{SourceQualification::SYNCHRONOUS_CPU};
    const char *qualificationReason{"viewer_acquisition_unqualified"};
    std::string observer;
    std::uint64_t generation{0};
    std::shared_ptr<const Production> receipt;
    std::uint32_t presentedWidth{0}, presentedHeight{0};
    std::shared_ptr<const rendering::CpuFrameSnapshot> viewerSnapshot;
    std::uint64_t viewerProduction{0}, viewerPresentation{0}, viewerEpoch{0};
    double viewerSubmittedAt{0.};
    const char *unavailable{"cold_cache"};
  };
  struct Job
  {
    std::shared_ptr<const void> infrastructure;
    ViewId view{INVALID_VIEW_ID};
    bool started{false};
    bool done{false};
    bool denied{false};
    bool observing{false}, activating{false};
    std::uint64_t sourceGeneration{0}, sourceEpoch{0}, sourceInstance{0};
    std::string device, subtype;
    std::function<bool()> isAbandoned;
    std::shared_ptr<const EncodedFrame> result;
    std::string payload;
    std::shared_ptr<const void> metadataLease;
    std::future<std::shared_ptr<const EncodedFrame>> encoded;
    std::function<void()> wake;
    double expires{0.};
  };
  struct Work
  {
    std::shared_ptr<Job> job;
    std::shared_ptr<const rendering::CpuFrameSnapshot> snapshot;
    std::shared_ptr<EncodedFrame> result;
    std::shared_ptr<const void> scratch;
    std::function<void()> wake;
    std::packaged_task<std::shared_ptr<const EncodedFrame>()> task;
  };
  using Key = std::pair<std::string, std::uint64_t>;
  bool authorized(const std::string &session, ViewId view);
  bool active(ViewId view, View &source);
  operations::Step observe(ViewId view,
      const std::string &session,
      const std::shared_ptr<Job> &job,
      operations::StepContext &ctx);
  void encodeLoop();
  Integration *m_integration{nullptr};
  session::SessionAuthority *m_authority{nullptr};
  operations::OperationEngine *m_engine{nullptr};
  session::MonotonicClock m_clock;
  const std::thread::id m_ownerThread{std::this_thread::get_id()};
  std::shared_ptr<rendering::FrameBudget> m_budget;
  std::shared_ptr<const void> m_serviceLease;
  std::shared_ptr<const void> m_stackLease;
  std::shared_ptr<bool> m_alive;
  std::map<ViewId, View> m_views; // host-only, never read by transport
  std::uint64_t m_nextSource{0};
  mutable std::mutex m_mutex;
  std::map<Key, std::shared_ptr<Job>> m_jobs;
  std::mutex m_workerMutex;
  std::condition_variable m_ready;
  std::unique_ptr<Work> m_work;
  bool m_encoding{false};
  bool m_stopping{false};
  // The stack is explicitly mapped, guarded, and unmapped after join. Using
  // std::thread/default pthread stacks would leave libc's stack cache outside
  // the ownership lifetime. No pool or extra queue is created.
  static constexpr std::size_t STACK_BYTES = 2u << 20;
  static constexpr std::size_t GUARD_BYTES = 64u << 10;
#if defined(__linux__)
  pthread_t m_worker{};
#endif
  void *m_stack{nullptr};
  bool m_workerStarted{false};
};

// Inlined definitions ////////////////////////////////////////////////////////

namespace detail {
template <typename Revision>
inline std::string revision(const Revision &r)
{
  return std::to_string(r.launch.activation) + "."
      + std::to_string(r.launch.nonce) + "." + std::to_string(r.domain) + "."
      + std::to_string(r.value);
}
inline std::string quote(const std::string &s)
{
  return inspection::viewinfo::jsonString(s);
}
} // namespace detail

inline FrameService::FrameService(Integration &integration,
    session::SessionAuthority &authority,
    operations::OperationEngine &engine,
    session::MonotonicClock clock,
    std::shared_ptr<rendering::FrameBudget> budget)
    : m_integration(&integration),
      m_authority(&authority),
      m_engine(&engine),
      m_clock(std::move(clock)),
      m_budget(std::move(budget))
{
  if (!m_budget)
    throw std::runtime_error("missing frame budget");
  // Fixed service state, mutex/CV, alive control, 64-entry pump wake vector,
  // libc thread/TLS/tcache bookkeeping and bounded-result callback storage in
  // the existing engine. Views/jobs have separate lifetime leases.
  m_serviceLease = m_budget->reserve(sizeof(FrameService) + (1u << 20));
  if (!m_serviceLease)
    throw std::runtime_error("frame infrastructure budget exhausted");
  try {
    m_alive = std::make_shared<bool>(true);
  } catch (const std::bad_alloc &) {
    return;
  }
#if defined(__linux__)
  const auto page = ::sysconf(_SC_PAGESIZE);
  if (page <= 0 || GUARD_BYTES % page || STACK_BYTES % page)
    return;
  m_stackLease = m_budget->reserve(STACK_BYTES + GUARD_BYTES + (128u << 10));
  if (!m_stackLease)
    return;
  m_stack = ::mmap(nullptr,
      STACK_BYTES + GUARD_BYTES,
      PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS,
      -1,
      0);
  if (m_stack == MAP_FAILED) {
    m_stack = nullptr;
    m_stackLease.reset();
    return;
  }
  pthread_attr_t attr;
  bool initialized = ::pthread_attr_init(&attr) == 0;
  if (initialized && ::mprotect(m_stack, GUARD_BYTES, PROT_NONE) == 0
      && ::pthread_attr_setguardsize(&attr, 0) == 0
      && ::pthread_attr_setstack(
             &attr, static_cast<char *>(m_stack) + GUARD_BYTES, STACK_BYTES)
          == 0) {
    m_workerStarted = ::pthread_create(
                          &m_worker,
                          &attr,
                          [](void *self) -> void * {
                            static_cast<FrameService *>(self)->encodeLoop();
                            return nullptr;
                          },
                          this)
        == 0;
  }
  if (initialized)
    ::pthread_attr_destroy(&attr);
  if (!m_workerStarted) {
    ::munmap(m_stack, STACK_BYTES + GUARD_BYTES);
    m_stack = nullptr;
    m_stackLease.reset();
  }
#endif
}

inline FrameService::~FrameService()
{
  m_alive.reset();
  {
    std::lock_guard<std::mutex> lock(m_workerMutex);
    m_stopping = true;
  }
  m_ready.notify_one();
#if defined(__linux__)
  if (m_workerStarted)
    ::pthread_join(m_worker, nullptr); // bounded CPU encoder, no driver wait
  if (m_stack)
    ::munmap(m_stack, STACK_BYTES + GUARD_BYTES);
#endif
  m_stackLease.reset();
  for (const auto &entry : m_views)
    m_integration->updateFrameSourceExposure(
        entry.first, false, m_budget->limit());
}

inline bool FrameService::attachView(ViewId view,
    identity::SceneIdentity &scene,
    identity::ViewRevisionTracker &tracker,
    SourceQualification qualification)
{
  auto policy = m_integration->capturePolicy(view);
  if (!m_workerStarted || !policy
      || m_budget->limit() > policy->policy.hostBudgetBytes
      || !m_integration->isRegisteredView(view) || m_views.size() >= 64
      || m_views.count(view)
      || m_nextSource == std::numeric_limits<std::uint64_t>::max())
    return false;
  auto lease = m_budget->reserve(sizeof(View) + (256u << 10));
  if (!lease)
    return false;
  try {
    View v;
    v.infrastructure = std::move(lease);
    v.scene = &scene;
    v.tracker = &tracker;
    v.generation = ++m_nextSource;
    v.qualification = qualification;
    v.source = std::make_unique<rendering::CpuFrameObservation>(m_budget);
    m_views.emplace(view, std::move(v));
    return true;
  } catch (const std::bad_alloc &) {
    return false;
  }
}

inline bool FrameService::setExposed(ViewId view, bool exposed)
{
  auto it = m_views.find(view);
  if (it == m_views.end() || !m_integration->isRegisteredView(view)
      || (exposed
          && (it->second.qualification
                  == SourceQualification::VIEWER_UNQUALIFIED
              || it->second.viewerEpoch
                  == std::numeric_limits<std::uint64_t>::max())))
    return false;
  if (!m_integration->updateFrameSourceExposure(
          view, exposed, m_budget->limit()))
    return false;
  it->second.exposed = exposed;
  if (!exposed) {
    it->second.unavailable = "cold_cache";
    it->second.observer.clear();
    it->second.receipt.reset();
    it->second.source->retire();
    it->second.viewerSnapshot.reset();
    if (it->second.viewerEpoch != std::numeric_limits<std::uint64_t>::max())
      ++it->second.viewerEpoch;
  }
  return true;
}

inline bool FrameService::authorized(const std::string &sessionId, ViewId view)
{
  auto scene = m_integration->sceneOfView(view);
  return scene
      && m_authority->authorizeExecution(
             sessionId, session::Target::view(view), session::Capability::READ)
      == session::Code::OK
      && m_authority->authorizeExecution(sessionId,
             session::Target::scene(*scene),
             session::Capability::READ)
      == session::Code::OK;
}

inline bool FrameService::active(ViewId view, View &v)
{
  const auto policy = m_integration->capturePolicy(view);
  bool yes = false;
  if (policy && policy->presentedFramesExposed && v.exposed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto &entry : m_jobs) {
      const auto &job = *entry.second;
      if (job.view == view && job.observing && !job.denied
          && job.sourceGeneration == v.generation
          && job.sourceEpoch == v.viewerEpoch && m_clock() < job.expires
          && (!job.activating || !job.isAbandoned || !job.isAbandoned())
          && authorized(entry.first.first, view)) {
        v.observer = entry.first.first;
        yes = true;
        break;
      }
    }
  }
  if (!yes) {
    if (!v.observer.empty()
        && v.viewerEpoch != std::numeric_limits<std::uint64_t>::max())
      ++v.viewerEpoch;
    v.source->retire();
    v.viewerSnapshot.reset();
    v.receipt.reset();
    v.observer.clear();
    // Retiring an observer must not turn an acquisition/profile failure into
    // cold preparation. Only ordinary availability is made cold by inactivity.
    const std::string_view reason = v.unavailable;
    if (reason == "available" || reason == "awaiting_presentation"
        || reason == "unmatched_render_start" || reason == "retired_source")
      v.unavailable = "cold_cache";
  }
  return yes;
}

inline std::optional<Production> FrameService::beginProduction(ViewId view,
    const rendering::ImagePipeline &pipeline,
    image::RowOrigin origin,
    image::AlphaMode alpha)
try {
  if (!m_integration->inServiceCall())
    return {};
  auto it = m_views.find(view);
  if (it == m_views.end())
    return {};
  auto &v = it->second;
  if (!active(view, v))
    return {};
  if (pipeline.production() == std::numeric_limits<std::uint64_t>::max())
    return {};
  // Reserve before observing/copying bounded bindings and JSON, not afterwards.
  // Covers receipt/control, nested vectors/strings and transient serialization.
  auto lease = m_budget->reserve(
      8 * inspection::viewinfo::MAX_BINDING_BYTES + sizeof(Production) + 4096);
  if (!lease)
    return {};
  auto binding = m_integration->observeViewBinding(view);
  auto policy = m_integration->capturePolicy(view);
  if (!binding || inspection::viewinfo::validate(*binding) || !policy
      || binding->outputColor != "rgba8_srgb")
    return {};
  auto validReference = [&](const identity::ObjectHandle &h,
                            anari::DataType type) {
    auto *object = v.scene->resolve(h);
    return !h.valid() || (object && object->type() == type);
  };
  if (!validReference(binding->cameraHandle, ANARI_CAMERA)
      || !validReference(binding->rendererHandle, ANARI_RENDERER))
    return {};
  const auto size = pipeline.dimensions();
  if (size.x != binding->width || size.y != binding->height)
    return {};
  auto configuration =
      inspection::viewinfo::bindingJson(*binding, detail::quote, true);
  v.tracker->observeConfiguration(
      inspection::viewinfo::bindingJson(*binding, detail::quote));
  v.tracker->setCamera(binding->cameraHandle);
  v.tracker->setRenderer(binding->rendererHandle);
  v.tracker->setDimensions(binding->width, binding->height);
  v.tracker->observeCapturePolicy(policy->revision);
  Production p;
  p.view = view;
  p.nextProduction = pipeline.production() + 1;
  p.sceneRevision = v.scene->sceneRevision();
  p.viewRevision = v.tracker->revision();
  p.policyRevision = v.tracker->capturePolicyRevision();
  p.binding = std::move(*binding);
  p.configuration = std::move(configuration);
  p.origin = origin;
  p.alpha = alpha;
  p.reservation = std::move(lease);
  return p;
} catch (const std::bad_alloc &) {
  return {}; // natural rendering/presentation can still proceed without a copy
}

inline std::shared_ptr<const Production> FrameService::beginViewerProduction(
    ViewId view, const ViewerBinding &observeBinding)
try {
  if (std::this_thread::get_id() != m_ownerThread || !observeBinding)
    return {};
  auto it = m_views.find(view);
  if (it == m_views.end())
    return {};
  auto &v = it->second;
  if (v.qualification != SourceQualification::VIEWER_CPU_READY
      || !active(view, v)
      || v.viewerProduction == std::numeric_limits<std::uint64_t>::max())
    return {};
  auto lease = m_budget->reserve(
      8 * inspection::viewinfo::MAX_BINDING_BYTES + sizeof(Production) + 4096);
  if (!lease)
    return {};
  auto binding = observeBinding();
  auto policy = m_integration->capturePolicy(view);
  if (!policy || inspection::viewinfo::validate(binding))
    return {};
  auto validReference = [&](const identity::ObjectHandle &h,
                            anari::DataType type) {
    auto *object = v.scene->resolve(h);
    return !h.valid() || (object && object->type() == type);
  };
  if (!validReference(binding.cameraHandle, ANARI_CAMERA)
      || !validReference(binding.rendererHandle, ANARI_RENDERER))
    return {};
  auto p = std::make_shared<Production>();
  p->reservation = std::move(lease);
  v.tracker->observeConfiguration(
      inspection::viewinfo::bindingJson(binding, detail::quote));
  v.tracker->setCamera(binding.cameraHandle);
  v.tracker->setRenderer(binding.rendererHandle);
  v.tracker->setDimensions(binding.width, binding.height);
  v.tracker->observeCapturePolicy(policy->revision);
  p->view = view;
  p->nextProduction = ++v.viewerProduction;
  p->viewerEpoch = v.viewerEpoch;
  p->sceneRevision = v.scene->sceneRevision();
  p->viewRevision = v.tracker->revision();
  p->policyRevision = v.tracker->capturePolicyRevision();
  p->binding = std::move(binding);
  p->origin = image::RowOrigin::BOTTOM_LEFT;
  return p;
} catch (const std::bad_alloc &) {
  return {};
}

inline std::shared_ptr<const ViewerAcquisition>
FrameService::acquireViewerPixels(ViewId view,
    std::shared_ptr<const Production> production,
    const ViewerBinding &observeOutput,
    const std::uint32_t *pixels,
    std::uint32_t width,
    std::uint32_t height,
    std::string_view transferPath)
try {
  if (std::this_thread::get_id() != m_ownerThread)
    return {};
  auto it = m_views.find(view);
  if (it == m_views.end())
    return {};
  auto &v = it->second;
  if (v.qualification != SourceQualification::VIEWER_CPU_READY
      || !active(view, v))
    return {};
  if (!pixels
      || (transferPath != "host_sdl_upload"
          && transferPath != "managed_host_sdl_upload")) {
    // Profile SDL_CPU_READY_V1 accepts only the normal completed SDL upload.
    // A non-null pointer/declaration is not proof of transfer support. Reject
    // before binding/snapshot allocation or dereferencing any source bytes.
    setExposed(view, false);
    v.qualification = SourceQualification::VIEWER_UNQUALIFIED;
    v.qualificationReason = !pixels ? "interop_acquisition_unqualified"
                                    : "unsupported_upload_profile";
    return {};
  }
  v.unavailable = "unmatched_render_start";
  if (!production || production->view != view
      || production->viewerEpoch != v.viewerEpoch || !observeOutput)
    return {};
  auto policy = m_integration->capturePolicy(view);
  const std::uint64_t count = std::uint64_t(width) * height;
  if (!policy || !width || !height || width > policy->policy.maxAxis
      || height > policy->policy.maxAxis || count > policy->policy.maxPixels
      || count > (std::numeric_limits<std::size_t>::max()
                     - sizeof(rendering::CpuFrameSnapshot) - 8192)
              / 4) {
    v.unavailable = "oversize_source";
    return {};
  }
  auto metadata = m_budget->reserve(8 * inspection::viewinfo::MAX_BINDING_BYTES
      + sizeof(ViewerAcquisition) + sizeof(Production) + 8192);
  auto bytes = m_budget->reserve(
      std::size_t(count) * 4 + sizeof(rendering::CpuFrameSnapshot) + 8192);
  if (!metadata || !bytes) {
    v.unavailable = "resource_exhausted";
    return {};
  }
  auto output = observeOutput();
  // The snapshot is the exact RGBA8 sRGB buffer uploaded for display, after
  // every in-pipeline effect (AOV, tone mapping, exposure, gamma, Outlines).
  // Those settings change what the viewport shows, not how these bytes are
  // read, so any combination is captured as presented; the result reports
  // them from this upload. Only an unknown source or pixel encoding is
  // unavailable.
  const bool supported = output.pipelineSource == "viewer_image_pipeline"
      && output.sourceInstanceGeneration != 0
      && output.outputColor == "rgba8_srgb";
  if (!supported) {
    retireViewer(view);
    v.unavailable = "unsupported_output_profile";
    return {};
  }
  if (inspection::viewinfo::validate(output) || output.width != width
      || output.height != height || production->binding.width != width
      || production->binding.height != height
      || output.deviceIdentifier != production->binding.deviceIdentifier
      || output.deviceSubtype != production->binding.deviceSubtype
      || output.sourceInstanceGeneration
          != production->binding.sourceInstanceGeneration)
    return {};
  auto p = std::make_shared<Production>();
  p->view = production->view;
  p->nextProduction = production->nextProduction;
  p->viewerEpoch = production->viewerEpoch;
  p->sceneRevision = production->sceneRevision;
  p->viewRevision = production->viewRevision;
  p->policyRevision = production->policyRevision;
  p->origin = production->origin;
  p->alpha = production->alpha;
  p->binding = production->binding;
  // ANARI camera/renderer/Scene come from actual render start. Final pipeline
  // effects come from this upload, never from a later request's UI selections.
  p->binding.pipelineSource = std::move(output.pipelineSource);
  p->binding.pipelineSettings = std::move(output.pipelineSettings);
  p->binding.pipelineConfigurationRevision =
      std::move(output.pipelineConfigurationRevision);
  p->binding.pipelineSettingsReason = std::move(output.pipelineSettingsReason);
  p->binding.outputColor = std::move(output.outputColor);
  p->reservation = metadata;
  auto snapshot = std::make_shared<rendering::CpuFrameSnapshot>();
  snapshot->reservation = std::move(bytes);
  snapshot->production = p->nextProduction;
  snapshot->width = width;
  snapshot->height = height;
  snapshot->bottomLeft = true;
  snapshot->rgba.resize(std::size_t(count) * 4);
  std::memcpy(snapshot->rgba.data(), pixels, snapshot->rgba.size());
  auto acquired = std::make_shared<ViewerAcquisition>();
  acquired->reservation = std::move(metadata);
  acquired->snapshot = std::move(snapshot);
  acquired->production = std::move(p);
  acquired->epoch = v.viewerEpoch;
  v.unavailable = "awaiting_presentation";
  return acquired;
} catch (const std::bad_alloc &) {
  return {};
}

inline void FrameService::presentViewerPixels(ViewId view,
    std::shared_ptr<const ViewerAcquisition> acquired,
    std::uint32_t width,
    std::uint32_t height,
    double displayWidth,
    double displayHeight,
    double submittedAt,
    double framebufferScaleX,
    double framebufferScaleY)
try {
  if (std::this_thread::get_id() != m_ownerThread)
    return;
  auto it = m_views.find(view);
  if (it == m_views.end())
    return;
  auto &v = it->second;
  // Clear on EVERY successful newer submission, even if its acquisition was
  // missing/busy. A finished old copy must never be substituted as latest.
  v.viewerSnapshot.reset();
  v.receipt.reset();
  v.presentedWidth = width;
  v.presentedHeight = height;
  if (v.viewerPresentation == std::numeric_limits<std::uint64_t>::max()) {
    v.unavailable = "identity_exhausted";
    return;
  }
  ++v.viewerPresentation;
  v.viewerSubmittedAt = submittedAt;
  if (framebufferScaleX != 1. || framebufferScaleY != 1.) {
    // High-DPI/renderer-scale variants have not passed the real display oracle
    // for SDL_CPU_READY_V1. Reject at presentation time, even for an older
    // otherwise-qualified upload; upload-time dimensions cannot certify this.
    v.unavailable = "unsupported_display_scale_profile";
    return;
  }
  if (v.qualification != SourceQualification::VIEWER_CPU_READY
      || !active(view, v) || !acquired || acquired->epoch != v.viewerEpoch)
    return;
  auto lease = m_budget->reserve(
      8 * inspection::viewinfo::MAX_BINDING_BYTES + sizeof(Production) + 8192);
  if (!lease) {
    v.unavailable = "resource_exhausted";
    return;
  }
  const auto &start = *acquired->production;
  const auto &snapshot = *acquired->snapshot;
  if (start.view != view || snapshot.width != width
      || snapshot.height != height)
    return;
  auto p = std::make_shared<Production>();
  p->view = view;
  p->nextProduction = start.nextProduction;
  p->viewerEpoch = start.viewerEpoch;
  p->sceneRevision = start.sceneRevision;
  p->viewRevision = start.viewRevision;
  p->policyRevision = start.policyRevision;
  p->origin = start.origin;
  p->alpha = start.alpha;
  p->binding = start.binding;
  // Display geometry belongs to this checked quad, including draws of an old
  // upload after a window resize. Its native pixels remain unchanged.
  p->binding.displayWidth = displayWidth;
  p->binding.displayHeight = displayHeight;
  p->binding.displayCoordinateUnits = "sdl_renderer_units";
  p->binding.displayFramebufferScaleX = framebufferScaleX;
  p->binding.displayFramebufferScaleY = framebufferScaleY;
  p->configuration =
      inspection::viewinfo::bindingJson(p->binding, detail::quote, true);
  p->reservation = std::move(lease);
  v.receipt = std::move(p);
  v.viewerSnapshot = acquired->snapshot;
  v.unavailable = "available";
} catch (const std::bad_alloc &) {
  // Latest was already retired. Natural presentation is never undone.
}

inline std::shared_ptr<const void> FrameService::reserveViewerConnection()
{
  return std::this_thread::get_id() == m_ownerThread
      ? m_budget->reserve(16u << 10)
      : std::shared_ptr<const void>{};
}

inline void FrameService::retireViewer(ViewId view)
{
  if (std::this_thread::get_id() != m_ownerThread)
    return;
  auto it = m_views.find(view);
  if (it == m_views.end())
    return;
  auto &v = it->second;
  v.viewerSnapshot.reset();
  v.receipt.reset();
  v.unavailable = "retired_source";
  if (v.viewerEpoch != std::numeric_limits<std::uint64_t>::max())
    ++v.viewerEpoch;
  else
    setExposed(view, false);
}

inline void FrameService::detachViewer(ViewId view)
{
  if (std::this_thread::get_id() != m_ownerThread)
    return;
  retireViewer(view);
  m_views.erase(view);
}

inline bool FrameService::present(ViewId view,
    const rendering::ImagePipeline &pipeline,
    std::optional<Production> production,
    const rendering::CpuFrameObservation::Submit &submit)
{
  if (!m_integration->inServiceCall())
    return false;
  auto it = m_views.find(view);
  if (it == m_views.end())
    return false;
  auto &v = it->second;
  auto policy = m_integration->capturePolicy(view);
  if (!policy)
    return false;
  const auto size = pipeline.dimensions();
  const bool matched = production && production->view == view
      && production->nextProduction == pipeline.production()
      && production->binding.width == size.x
      && production->binding.height == size.y;
  const bool observing = active(view, v);
  const auto before = v.source->presentation();
  bool submitted = false;
  try {
    submitted = v.source->present(
        pipeline,
        submit,
        [&] { return matched && active(view, v); },
        policy->policy.maxAxis,
        policy->policy.maxPixels,
        production && production->origin == image::RowOrigin::BOTTOM_LEFT,
        production && production->alpha == image::AlphaMode::PREMULTIPLIED);
  } catch (const std::bad_alloc &) {
    // Application-facing presentation boundary: allocation failure after a
    // successful display must not stop natural viewing or revive an old copy.
    submitted = before != v.source->presentation();
    v.source->retire();
    v.receipt.reset();
    v.unavailable = "resource_exhausted";
    if (submitted) {
      v.presentedWidth = size.x;
      v.presentedHeight = size.y;
    }
    return submitted;
  }
  if (submitted) {
    v.presentedWidth = size.x;
    v.presentedHeight = size.y;
    v.receipt.reset();
    const bool stillObserving = observing && active(view, v);
    v.unavailable = !stillObserving
        ? "cold_cache"
        : (matched ? v.source->reason() : "unmatched_production");
    if (stillObserving && matched && v.source->latest()) {
      try {
        v.receipt = std::make_shared<const Production>(std::move(*production));
      } catch (const std::bad_alloc &) {
        v.source->retire();
        v.unavailable = "resource_exhausted";
      }
    }
  }
  if (!submitted && pipeline.storage() != rendering::ImageStorage::HOST) {
    v.receipt.reset();
    v.unavailable = "unsupported_storage";
  }
  return submitted;
}

inline operations::Step FrameService::observe(ViewId view,
    const std::string &sessionId,
    const std::shared_ptr<Job> &jobPtr,
    operations::StepContext &ctx)
{
  using operations::Outcome;
  using operations::Step;
  auto &job = *jobPtr;
  const bool readable = !ctx.abandoned && authorized(sessionId, view);
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (job.started) {
      job.denied = job.denied || !readable;
      if (!job.done)
        return {Step::Status::BACKEND_PENDING};
      if (job.denied) {
        job.encoded = {};
        job.result.reset();
        job.payload.clear();
        job.metadataLease.reset();
        return {Step::Status::COMPLETE,
            ctx.abandoned ? Outcome::CANCELLED : Outcome::FORBIDDEN};
      }
      // Packaged work transports exceptional allocation failures back to the
      // host's application boundary instead of terminating the worker thread.
      try {
        job.result = job.encoded.get();
      } catch (const std::bad_alloc &) {
        job.result.reset();
      }
      if (!job.result) {
        Step failed{Step::Status::COMPLETE,
            Outcome::OK,
            "{\"state\":\"unavailable\",\"reason\":\"encoding_failed\"}"};
        failed.resourceLease = std::move(job.metadataLease);
        failed.reserveStatus = [budget = m_budget] {
          return budget->reserve(
              8 * (3 * inspection::viewinfo::MAX_BINDING_BYTES + 16384));
        };
        return failed;
      }
      Step completed{
          Step::Status::COMPLETE, Outcome::OK, std::move(job.payload)};
      completed.resourceLease = std::move(job.metadataLease);
      completed.reserveStatus = [budget = m_budget] {
        return budget->reserve(
            8 * (3 * inspection::viewinfo::MAX_BINDING_BYTES + 16384));
      };
      return completed;
    }
  }
  if (!readable) {
    std::lock_guard<std::mutex> lock(m_mutex);
    job.denied = true;
    job.observing = job.activating = false;
    job.wake = {};
    job.isAbandoned = {};
    return {Step::Status::COMPLETE,
        ctx.abandoned ? Outcome::CANCELLED : Outcome::FORBIDDEN};
  }
  auto unavailable = [&](const char *reason) {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (job.activating)
        job.observing = false;
      job.activating = false;
      job.wake = {};
      job.isAbandoned = {};
    }
    auto lease = m_budget->reserve(8192);
    if (!lease)
      return Step{Step::Status::COMPLETE, Outcome::BUSY};
    const auto found = m_views.find(view);
    const auto width =
        found == m_views.end() ? 0u : found->second.presentedWidth;
    const auto height =
        found == m_views.end() ? 0u : found->second.presentedHeight;
    const auto declared = m_integration->capturePolicy(view);
    const auto caps = declared ? declared->policy : CapturePolicy{};
    Step s{Step::Status::COMPLETE,
        Outcome::OK,
        "{\"state\":\"unavailable\",\"reason\":" + detail::quote(reason)
            + ",\"geometry_state\":\"not_inferred\",\"native_width\":"
            + std::to_string(width)
            + ",\"native_height\":" + std::to_string(height)
            + ",\"limits\":{\"max_axis\":" + std::to_string(caps.maxAxis)
            + ",\"max_pixels\":" + std::to_string(caps.maxPixels)
            + ",\"max_png_bytes\":" + std::to_string(caps.maxPngBytes)
            + ",\"max_response_bytes\":" + std::to_string(caps.maxResponseBytes)
            + ",\"proxy_ceiling_bytes\":16777216,\"host_budget_bytes\":"
            + std::to_string(m_budget->limit()) + "}}"};
    s.resourceLease = std::move(lease);
    s.reserveStatus = [budget = m_budget] { return budget->reserve(16384); };
    return s;
  };
  auto it = m_views.find(view);
  if (it == m_views.end())
    return unavailable("missing_source");
  auto &v = it->second;
  if (v.qualification == SourceQualification::VIEWER_UNQUALIFIED)
    return unavailable(v.qualificationReason);
  if (!v.exposed)
    return unavailable("unexposed_source");
  auto policy = m_integration->capturePolicy(view);
  if (!policy || !policy->presentedFramesExposed)
    return unavailable("unexposed_source");
  auto observationLease =
      m_budget->reserve(8 * inspection::viewinfo::MAX_BINDING_BYTES + 4096);
  if (!observationLease)
    return unavailable("resource_exhausted");
  auto current = m_integration->observeViewBinding(view);
  if (!policy || !current || inspection::viewinfo::validate(*current)) {
    v.source->retire();
    v.viewerSnapshot.reset();
    v.receipt.reset();
    retireViewer(view);
    return unavailable("inactive_binding");
  }
  if (job.activating) {
    if (m_clock() >= job.expires) {
      std::lock_guard<std::mutex> lock(m_mutex);
      job.observing = job.activating = false;
      job.wake = {};
      job.isAbandoned = {};
      return {Step::Status::COMPLETE, Outcome::TIMEOUT};
    }
    // Preparation owns the original source, not whichever view/device happens
    // to be registered when a later host wake arrives.
    if (job.sourceGeneration != v.generation || job.sourceEpoch != v.viewerEpoch
        || job.sourceInstance != current->sourceInstanceGeneration
        || job.device != current->deviceIdentifier
        || job.subtype != current->deviceSubtype)
      return unavailable("source_changed");
  } else {
    active(view, v);
    std::lock_guard<std::mutex> lock(m_mutex);
    job.sourceGeneration = v.generation;
    job.sourceEpoch = v.viewerEpoch;
    job.sourceInstance = current->sourceInstanceGeneration;
    job.device = current->deviceIdentifier;
    job.subtype = current->deviceSubtype;
    job.expires =
        std::min(job.expires, m_clock() + policy->policy.deadlineSeconds);
    job.observing = true;
  }
  const bool viewer = v.qualification == SourceQualification::VIEWER_CPU_READY;
  const auto snapshot = viewer ? v.viewerSnapshot : v.source->latest();
  const auto receipt = v.receipt;
  if (!snapshot || !receipt) {
    const std::string_view reason = v.unavailable;
    if (reason == "cold_cache"
        || (job.activating
            && (reason == "unmatched_render_start"
                || reason == "awaiting_presentation"))) {
      std::lock_guard<std::mutex> lock(m_mutex);
      job.activating = true;
      job.wake = ctx.wake;
      job.isAbandoned = ctx.isAbandoned;
      return {Step::Status::BACKEND_PENDING};
    }
    return unavailable(v.unavailable);
  }
  const auto currentJson =
      inspection::viewinfo::bindingJson(*current, detail::quote);
  v.tracker->observeConfiguration(currentJson);
  v.tracker->setCamera(current->cameraHandle);
  v.tracker->setRenderer(current->rendererHandle);
  v.tracker->setDimensions(current->width, current->height);
  v.tracker->observeCapturePolicy(policy->revision);
  // This is the owning-thread observation point. No later presentation can
  // replace this snapshot or its receipt while encoding/delivery continues.
  const auto observedAt = session::steadyClock()();
  const auto presentation =
      viewer ? v.viewerPresentation : (snapshot ? snapshot->presentation : 0);
  const auto submittedAt =
      viewer ? v.viewerSubmittedAt : (snapshot ? snapshot->submittedAt : 0.);
  const auto currentScene = v.scene->sceneRevision();
  const auto currentView = v.tracker->revision();
  if (receipt->binding.deviceIdentifier != current->deviceIdentifier
      || receipt->binding.deviceSubtype != current->deviceSubtype
      || receipt->binding.sourceInstanceGeneration
          != current->sourceInstanceGeneration) {
    v.source->retire();
    v.viewerSnapshot.reset();
    v.receipt.reset();
    retireViewer(view);
    v.unavailable = "device_changed";
    return unavailable("device_changed");
  }
  if (snapshot->width > policy->policy.maxAxis
      || snapshot->height > policy->policy.maxAxis
      || std::uint64_t(snapshot->width) * snapshot->height
          > policy->policy.maxPixels)
    return unavailable("oversize_source");
  const auto pngBound =
      image::pngRgba8OutputBound(snapshot->width, snapshot->height);
  if (!pngBound || *pngBound > policy->policy.maxPngBytes)
    return unavailable("oversize_source");
  const auto responseBound = ((*pngBound + 2) / 3) * 4
      + 3 * (inspection::viewinfo::MAX_BINDING_BYTES + 4096) + 4096;
  if (responseBound > policy->policy.maxResponseBytes)
    return unavailable("response_limit");
  // normalize: one RGBA array; filter rows; stb output growth/reallocation;
  // final PNG and fallback; hash table 16384 lists (quality=8, capacity <=32).
  // PngEncode.cpp proves compressed output <=2*filtered+8 and stretchy
  // capacity <4*output+8; simultaneous old/new realloc capacities <=12*filtered
  // plus normalization/filter arrays. 16 filtered arrays + 8MiB covers these
  // peaks and all 16384 hash lists (capacity <=30 pointers, quality fixed at8).
  // Additional to the pinned snapshot, metadata and retained PNG reservations.
  auto scratch = m_budget->reserve(
      16 * (snapshot->rgba.size() + snapshot->height) + (8u << 20));
  auto retained = m_budget->reserve(*pngBound + sizeof(EncodedFrame) + 8192);
  if (!scratch || !retained)
    return unavailable("resource_exhausted");
  auto metadataLease =
      m_budget->reserve(2 * inspection::viewinfo::MAX_BINDING_BYTES + 8192);
  if (!metadataLease)
    return unavailable("resource_exhausted");
  auto encoded = std::make_shared<EncodedFrame>();
  encoded->width = snapshot->width;
  encoded->height = snapshot->height;
  encoded->reservation = std::move(retained);
  const auto description = m_integration->viewDescription(view);
  if (!description)
    return {Step::Status::COMPLETE, Outcome::SUPERSEDED};
  const bool stale = receipt->sceneRevision != currentScene
      || receipt->viewRevision != currentView;
  const auto result = "{\"state\":\"available\",\"image_kind\":\"presented_frame\",\"view_id\":"
      + std::to_string(view) + "," + inspection::viewinfo::identityJson(*description, detail::quote)
      + ",\"frame_identity\":" + detail::quote(description->identity + ":" + std::to_string(presentation))
      + ",\"presentation_id\":" + std::to_string(presentation)
      + ",\"production_id\":" + std::to_string(snapshot->production)
      + (viewer ? ",\"source_frame_id\":" + detail::quote(description->identity
                        + ":render:" + std::to_string(receipt->viewerEpoch)
                        + ":" + std::to_string(snapshot->production))
              + ",\"rendered_view_revision_basis\":\"anari_render_start\""
                ",\"pipeline_configuration_basis\":\"final_cpu_ready_upload\""
                ",\"display_configuration_basis\":\"checked_sdl_submission\""
                : std::string{})
      + ",\"observed_presentation_id\":" + std::to_string(presentation)
      + ",\"submitted_monotonic_seconds\":" + inspection::viewinfo::number(submittedAt)
      + ",\"observed_monotonic_seconds\":" + inspection::viewinfo::number(observedAt)
      + ",\"width\":" + std::to_string(encoded->width) + ",\"height\":" + std::to_string(encoded->height)
      + ",\"stale\":" + (stale ? "true" : "false")
      + ",\"rendered_scene_revision\":" + detail::quote(detail::revision(receipt->sceneRevision))
      + ",\"current_scene_revision\":" + detail::quote(detail::revision(currentScene))
      + ",\"rendered_view_revision\":" + detail::quote(detail::revision(receipt->viewRevision))
      + ",\"current_view_revision\":" + detail::quote(detail::revision(currentView))
      + ",\"rendered_policy_revision\":" + detail::quote(detail::revision(receipt->policyRevision))
      + ",\"current_policy_revision\":" + detail::quote(detail::revision(v.tracker->capturePolicyRevision()))
      + ",\"rendered_configuration\":{" + receipt->configuration + "}"
      + ",\"image_contract\":{\"color\":\"srgb_rgba8\",\"alpha\":\"straight\",\"origin\":\"top_left\","
        "\"pipeline_effects\":\"included\",\"ui_hud_gizmos\":\"excluded\",\"monitor_compositor\":\"outside_contract\"}}";
  if (result.size() > inspection::viewinfo::MAX_BINDING_BYTES + 4096)
    return unavailable("representation_limit");
  auto work = std::make_unique<Work>();
  work->job = jobPtr;
  work->snapshot = snapshot;
  work->result = std::move(encoded);
  work->scratch = std::move(scratch);
  work->wake = ctx.wake;
  work->task = std::packaged_task<std::shared_ptr<const EncodedFrame>()>(
      [ptr = work.get()] {
        image::SourceImage src;
        src.width = ptr->snapshot->width;
        src.height = ptr->snapshot->height;
        src.encoding = image::ChannelEncoding::SRGB_UINT8;
        src.origin = ptr->snapshot->bottomLeft ? image::RowOrigin::BOTTOM_LEFT
                                               : image::RowOrigin::TOP_LEFT;
        src.alpha = ptr->snapshot->premultiplied
            ? image::AlphaMode::PREMULTIPLIED
            : image::AlphaMode::STRAIGHT;
        src.pixels = ptr->snapshot->rgba.data();
        auto normalized = image::normalizeToPng(src);
        const auto bound = image::pngRgba8OutputBound(src.width, src.height);
        if (!normalized.ok() || !bound || normalized.image.png.size() > *bound)
          return std::shared_ptr<const EncodedFrame>{};
        ptr->result->png = std::move(normalized.image.png);
        return std::shared_ptr<const EncodedFrame>(std::move(ptr->result));
      });
  {
    std::lock_guard<std::mutex> workerLock(m_workerMutex);
    if (m_encoding || m_stopping)
      return unavailable("encoder_busy");
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      job.encoded = work->task.get_future();
      job.started = true;
      job.activating = false;
      job.isAbandoned = {};
      job.wake = ctx.wake;
      job.payload = result;
      job.metadataLease = std::move(metadataLease);
      // Keep the admission/preparation lifetime; waking never buys more time.
    }
    m_encoding = true;
    m_work = std::move(work);
  }
  m_ready.notify_one();
  return {Step::Status::BACKEND_PENDING};
}

inline void FrameService::encodeLoop()
{
  for (;;) {
    std::unique_ptr<Work> work;
    {
      std::unique_lock<std::mutex> lock(m_workerMutex);
      m_ready.wait(lock, [&] { return m_work || m_stopping; });
      if (!m_work && m_stopping)
        return;
      work = std::move(m_work);
    }
    work->task();
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      work->job->done = true;
    }
    {
      std::lock_guard<std::mutex> lock(m_workerMutex);
      m_encoding = false;
    }
    // The engine owns this wake contract; it ignores terminal/retired refs.
    // Only immutable pixels are accessed above, never Scene/UI/current state.
    try {
      work->wake();
    } catch (const std::bad_alloc &) {
      // pump() retries completion at the next owning-thread safe point.
    }
  }
}

inline operations::Admission FrameService::submit(
    const operations::Credentials &credentials,
    const operations::OperationRef &ref,
    ViewId view)
{
  const auto scene = m_integration->sceneOfView(view);
  if (!scene)
    return {operations::Outcome::INVALID_REQUEST};
  for (auto target :
      {session::Target::view(view), session::Target::scene(*scene)}) {
    const auto code =
        m_authority->authorize(credentials, target, session::Capability::READ);
    if (code != session::Code::OK)
      return {operations::fromSessionCode(code)};
  }
  if (ref.sessionId != credentials.sessionId
      || !operations::validRequestId(ref.requestId))
    return {operations::Outcome::INVALID_REQUEST};
  std::shared_ptr<Job> job;
  bool inserted = false;
  const Key key{ref.sessionId, ref.requestId};
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_jobs.begin(); it != m_jobs.end();) {
      // Activation has no worker to deliver a late cleanup wake. Its pump
      // must retain that callback until expiry/cancellation is safely drained.
      if (it->second->expires > 0. && m_clock() >= it->second->expires
          && !it->second->activating
          && (!it->second->started || it->second->done))
        it = m_jobs.erase(it);
      else
        ++it;
    }
    auto it = m_jobs.find(key);
    if (it != m_jobs.end())
      job = it->second;
    else {
      if (m_jobs.size() >= 64)
        return {operations::Outcome::BUSY};
      // Job/map node, key/session, executor copies in OperationEngine, Work,
      // future/task shared state and wake closures. Lease follows the Job held
      // by the executor/worker too, not merely this lookup-table entry.
      auto lease = m_budget->reserve(sizeof(Job) + (16u << 10));
      if (!lease)
        return {operations::Outcome::BUSY};
      try {
        job = std::make_shared<Job>();
        job->infrastructure = std::move(lease);
        job->view = view;
        job->expires = m_clock() + 120.;
        m_jobs.emplace(key, job);
      } catch (const std::bad_alloc &) {
        return {operations::Outcome::BUSY};
      }
      inserted = true;
    }
  }
  operations::Request request;
  request.kind = "view_frame";
  request.target = session::Target::view(view);
  request.capability = session::Capability::READ;
  request.canonicalArguments = std::to_string(view);
  request.deadlineSeconds = 120.;
  std::weak_ptr<bool> alive = m_alive;
  request.executor = [this, alive, job, view, sessionId = ref.sessionId](
                         operations::StepContext &ctx) {
    if (!alive.lock())
      return operations::Step{operations::Step::Status::COMPLETE,
          operations::Outcome::OUTCOME_UNAVAILABLE};
    return observe(view, sessionId, job, ctx);
  };
  auto admitted = m_engine->submit(credentials, ref, std::move(request));
  if (inserted
      && (admitted.code != operations::Outcome::OK || admitted.replayed)) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_jobs.erase(key);
  }
  return admitted;
}

inline std::shared_ptr<const EncodedFrame> FrameService::result(
    const operations::Credentials &credentials,
    const operations::OperationRef &ref) const
{
  const auto status = m_engine->status(credentials, ref);
  if (status.code != operations::Outcome::OK || status.kind != "view_frame"
      || status.terminal != operations::Outcome::OK || !status.resultVisible)
    return {};
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_jobs.find({ref.sessionId, ref.requestId});
  if (it == m_jobs.end() || m_clock() >= it->second->expires)
    return {};
  return it->second->result;
}

inline std::shared_ptr<const void> FrameService::reserveDelivery(
    std::size_t responseBytes) const
{
  if (responseBytes > 12582912)
    return {};
  // Base64, content, structured/text duplication, JSON-RPC envelope and HTTP
  // serialization coexist. Held through send timeout/drain, not just handler.
  return m_budget->reserve(responseBytes * 8 + 4096);
}

inline std::size_t FrameService::ownedBytes() const
{
  return m_budget->used();
}

inline void FrameService::pump()
{
  if (!m_integration->inServiceCall())
    return;
  for (auto it = m_views.begin(); it != m_views.end();) {
    if (!m_integration->isRegisteredView(it->first))
      it = m_views.erase(it);
    else {
      active(it->first, it->second);
      ++it;
    }
  }
  std::vector<std::function<void()>> wakes;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_jobs.begin(); it != m_jobs.end();) {
      auto &job = *it->second;
      job.denied = job.denied || !authorized(it->first.first, job.view);
      if (job.activating && job.wake) {
        const auto source = m_views.find(job.view);
        bool ready = job.denied || m_clock() >= job.expires
            || (job.isAbandoned && job.isAbandoned())
            || source == m_views.end();
        if (!ready) {
          const auto &v = source->second;
          const std::string_view reason = v.unavailable;
          const auto binding = m_integration->observeViewBinding(job.view);
          ready = !v.exposed || v.generation != job.sourceGeneration
              || v.viewerEpoch != job.sourceEpoch || !binding
              || inspection::viewinfo::validate(*binding)
              || binding->sourceInstanceGeneration != job.sourceInstance
              || binding->deviceIdentifier != job.device
              || binding->deviceSubtype != job.subtype
              || (v.receipt && (v.viewerSnapshot || v.source->latest()))
              || (reason != "cold_cache" && reason != "unmatched_render_start"
                  && reason != "awaiting_presentation");
        }
        if (ready)
          wakes.push_back(job.wake);
      } else if (job.started && job.done && job.wake)
        wakes.push_back(job.wake);
      // A captured/queued wake is not executor cleanup. Cold preparation has
      // no worker wake: retain its charged Job until observe() clears
      // activation, so a rejected dispatch can retry on a later host pump.
      if ((job.denied || m_clock() >= job.expires) && !job.activating
          && (!job.started || job.done))
        it = m_jobs.erase(it);
      else
        ++it;
    }
  }
  // Handles completion racing the engine's transition to BACKEND_PENDING.
  // Hosts call pump once per owning-thread loop, as for RenderService.
  for (const auto &wake : wakes)
    wake();
}

} // namespace vsr::integration::frame

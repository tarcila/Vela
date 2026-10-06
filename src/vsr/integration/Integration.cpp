// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Integration.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <cmath>
#include <utility>

namespace vsr::integration {

namespace {

bool validDuration(double seconds)
{
  return std::isfinite(seconds) && seconds > 0.0;
}

} // namespace

Integration::Integration(std::size_t queueCapacity)
    : m_queueCapacity(queueCapacity ? queueCapacity : DEFAULT_QUEUE_CAPACITY)
{}

Integration::~Integration() = default;

// Queries ////////////////////////////////////////////////////////////////////

bool Integration::active() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_active;
}

bool Integration::isRegisteredScene(SceneId id) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return id < m_scenes.size();
}

bool Integration::isRegisteredView(ViewId id) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return id < m_views.size() && !m_views[id].removed;
}

std::vector<ViewId> Integration::viewsOfScene(SceneId id) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<ViewId> views;
  for (std::size_t i = 0; i < m_views.size(); i++) {
    if (!m_views[i].removed && m_views[i].registration.scene == id)
      views.push_back(ViewId(i));
  }
  return views;
}

std::optional<SceneId> Integration::sceneOfView(ViewId id) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (id >= m_views.size() || m_views[id].removed)
    return {};
  return m_views[id].registration.scene;
}

SceneGrants Integration::sceneGrants(SceneId id) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_active || id >= m_scenes.size())
    return {};
  return m_scenes[id].registration.grants;
}

bool Integration::nodeEditsExposed(SceneId id) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_active && id < m_scenes.size() && m_scenes[id].registration.nodeEdits
      && m_scenes[id].registration.grants.read
      && m_scenes[id].registration.grants.edit;
}

ViewGrants Integration::viewGrants(ViewId id) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_active || id >= m_views.size() || m_views[id].removed)
    return {};
  return m_views[id].registration.grants;
}

SyncHook Integration::normalSyncHook(SceneId id) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_active || id >= m_scenes.size())
    return {};
  return m_scenes[id].registration.normalSyncHook;
}

SyncHook Integration::recoveryCallback(SceneId id) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_active || id >= m_scenes.size())
    return {};
  return m_scenes[id].registration.recoveryCallback;
}

SessionTiming Integration::timing() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_timing;
}

std::size_t Integration::queueCapacity() const
{
  return m_queueCapacity;
}

bool Integration::inServiceCall() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_servicingThread == std::this_thread::get_id();
}

std::optional<EffectiveViewBinding> Integration::observeViewBinding(
    ViewId id) const
{
  BindingObserver observer;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_active || m_servicingThread != std::this_thread::get_id()
        || id >= m_views.size() || m_views[id].removed)
      return {};
    observer = m_views[id].registration.observeBinding;
  }
  return observer ? observer() : std::optional<EffectiveViewBinding>{};
}

rendering::WorldBoundsProvider Integration::worldBoundsProvider(
    SceneId id) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_active || m_servicingThread != std::this_thread::get_id()
      || id >= m_scenes.size())
    return {};
  return m_scenes[id].registration.worldBounds;
}

std::optional<ViewDescription> Integration::viewDescription(ViewId id) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (id >= m_views.size() || m_views[id].removed)
    return {};
  const auto &v = m_views[id].registration;
  const auto launch = m_viewIdentities.launch();
  return ViewDescription{v.name,
      v.role,
      "v:" + std::to_string(launch.activation) + "."
          + std::to_string(launch.nonce) + ":" + std::to_string(id)};
}

std::optional<CapturePolicyObservation> Integration::capturePolicy(
    ViewId id) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (id >= m_views.size() || m_views[id].removed)
    return {};
  return CapturePolicyObservation{m_views[id].registration.capturePolicy,
      m_views[id].policyRevision,
      m_views[id].framesExposed,
      m_views[id].frameHostBudgetBytes};
}

bool Integration::updateFrameSourceExposure(
    ViewId id, bool exposed, std::uint64_t hostBudgetBytes)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (id >= m_views.size() || m_views[id].removed || !hostBudgetBytes
      || hostBudgetBytes
          > m_views[id].registration.capturePolicy.hostBudgetBytes)
    return false;
  auto &view = m_views[id];
  if (view.framesExposed != exposed
      || view.frameHostBudgetBytes != hostBudgetBytes) {
    if (view.policyRevision == std::numeric_limits<std::uint64_t>::max())
      return false;
    view.framesExposed = exposed;
    view.frameHostBudgetBytes = hostBudgetBytes;
    ++view.policyRevision;
  }
  return true;
}

bool Integration::updateCapturePolicy(ViewId id, const CapturePolicy &policy)
{
  if (!validCapturePolicy(policy))
    return false;
  std::lock_guard<std::mutex> lock(m_mutex);
  if (id >= m_views.size() || m_views[id].removed
      || (m_active && m_servicingThread != std::this_thread::get_id()))
    return false;
  auto &view = m_views[id];
  if (capturePolicyJson(view.registration.capturePolicy)
      != capturePolicyJson(policy)) {
    if (view.policyRevision == std::numeric_limits<std::uint64_t>::max())
      return false;
    view.registration.capturePolicy = policy;
    if (view.frameHostBudgetBytes > policy.hostBudgetBytes)
      view.framesExposed = false;
    ++view.policyRevision;
  }
  return true;
}

bool Integration::sceneSyncConfirmsView(ViewId id) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return id < m_views.size() && !m_views[id].removed
      && m_views[id].registration.sceneSyncConfirmsBinding;
}

std::size_t Integration::viewSlotCount() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_views.size();
}

// Registration ///////////////////////////////////////////////////////////////

std::optional<SceneId> Integration::registerScene(
    SceneRegistration registration)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_active || registration.scene == nullptr)
    return {};
  m_scenes.push_back({std::move(registration)});
  return SceneId(m_scenes.size() - 1);
}

std::optional<ViewId> Integration::registerView(ViewRegistration registration)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (registration.scene >= m_scenes.size() || registration.name.size() > 256
      || !validCapturePolicy(registration.capturePolicy))
    return {};
  if (m_active) {
    // Post-activation: host scene work at a safe point only.
    if (m_servicingThread != std::this_thread::get_id())
      return {};
    const auto &g = registration.grants;
    if (g.render && !g.read)
      return {};
  }
  m_views.push_back({std::move(registration)});
  return ViewId(m_views.size() - 1);
}

bool Integration::unregisterView(ViewId id)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_active || m_servicingThread != std::this_thread::get_id()
      || id >= m_views.size() || m_views[id].removed)
    return false;
  m_views[id].removed = true;
  return true;
}

// Activation /////////////////////////////////////////////////////////////////

ActivationResult Integration::validate(const SessionTiming &timing) const
{
  if (m_active)
    return {ActivationError::ALREADY_ACTIVE, {}};
  if (!validDuration(timing.leaseSeconds))
    return {ActivationError::INVALID_LEASE, {}};
  if (!validDuration(timing.renewalSeconds))
    return {ActivationError::INVALID_RENEWAL, {}};
  if (!(timing.renewalSeconds < timing.leaseSeconds))
    return {ActivationError::RENEWAL_NOT_SHORTER_THAN_LEASE, {}};

  for (const auto &s : m_scenes) {
    const auto &r = s.registration;
    if ((r.grants.edit || r.grants.resynchronize) && !r.grants.read)
      return {ActivationError::ACTION_WITHOUT_READ, r.name};
    if (r.grants.edit && !r.normalSyncHook)
      return {ActivationError::MISSING_SYNC_HOOK, r.name};
    if (r.grants.resynchronize && !r.recoveryCallback)
      return {ActivationError::MISSING_RECOVERY_CALLBACK, r.name};
  }

  for (const auto &v : m_views) {
    const auto &r = v.registration;
    if (r.grants.render && !r.grants.read)
      return {ActivationError::ACTION_WITHOUT_READ, r.name};
  }

  return {};
}

ActivationResult Integration::activate(SessionTiming timing)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto result = validate(timing);
  if (result.ok()) {
    m_timing = timing;
    m_active = true;
  }
  return result;
}

// Execution //////////////////////////////////////////////////////////////////

bool Integration::submit(SceneId target, SceneWork work)
{
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_active || target >= m_scenes.size() || !work
        || m_queue.size() >= m_queueCapacity)
      return false;
    m_queue.push_back({target, std::move(work), {}});
  }
  m_workAvailable.notify_all();
  return true;
}

bool Integration::submitSessionWork(SessionWork work)
{
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_active || !work || m_queue.size() >= m_queueCapacity)
      return false;
    m_queue.push_back({INVALID_SCENE_ID, {}, std::move(work)});
  }
  m_workAvailable.notify_all();
  return true;
}

std::size_t Integration::service()
{
  std::deque<PendingWork> batch;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Exclusive and non-blocking: a nested or concurrent call runs nothing.
    if (m_servicingThread != std::thread::id())
      return 0;
    batch.swap(m_queue);
    m_servicingThread = std::this_thread::get_id();
  }

  for (auto &item : batch) {
    try {
      if (item.sessionWork) {
        item.sessionWork();
      } else {
        // Registrations are immutable after activation, so the pointer is
        // stable.
        auto *scene = m_scenes[item.target].registration.scene;
        item.work(*scene);
      }
    } catch (...) {
      // The exception text may carry request data; never log it.
      core::logError("[integration] submitted work threw; item skipped");
    }
  }

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_servicingThread = std::thread::id();
  }
  // Wake a service loop that deferred to this call.
  m_workAvailable.notify_all();
  return batch.size();
}

void Integration::runServiceLoop()
{
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_workAvailable.wait(lock, [&] {
        const bool idle = m_servicingThread == std::thread::id();
        return (m_stopRequested && m_queue.empty())
            || (!m_queue.empty() && idle);
      });
      if (m_queue.empty()) {
        m_stopRequested = false;
        return;
      }
    }
    service();
  }
}

void Integration::requestStop()
{
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stopRequested = true;
  }
  m_workAvailable.notify_all();
}

} // namespace vsr::integration

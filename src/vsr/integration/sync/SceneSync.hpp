// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Header-only: the vsr_integration library globs only top-level sources (see
// ticket 19).

// vsr_integration
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/operations/Operations.hpp"
// std
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace vsr::integration::sync {

/*
 * Scene synchronization states, explicit Resynchronize and confirmed view
 * registration (spec §10).
 *
 * Health is scene-scoped: any failure blocks every registered view of that
 * scene; other scenes are unaffected. A view renders only when it is
 * registered, confirmed by the host, and its scene is `ready` (viewReady(),
 * the predicate render guards use).
 *
 * Threading: queries and submit*() are thread-safe. Hooks, the recovery
 * callback and view (un)registration run only on the servicing thread at
 * safe points, one at a time, so the view set never changes while a hook
 * runs.
 */

// Frozen scene synchronization state names.
enum class SyncState
{
  READY,
  NEEDS_SYNC,
  SYNCHRONIZING,
  FAILED
};

const char *syncStateName(SyncState s);

using TransitionListener =
    std::function<void(SceneId, SyncState from, SyncState to)>;

// Per-scene state shared by ordinary synchronization (edit/restore) and
// explicit recovery.
class SyncStates
{
 public:
  SyncState state(SceneId scene) const;
  bool sceneReady(SceneId scene) const;
  bool viewConfirmed(ViewId view) const;
  void setTransitionListener(TransitionListener listener);

  // Allocate control state before a mutation's no-fail boundary.
  void prepareScene(SceneId scene);
  // Ordinary synchronization after an edit/restore commit.
  void beginHook(SceneId scene);
  // Returns true when the operation may report synchronization-ready success:
  // the hook confirmed and no earlier failure/needs_sync remains uncleared.
  bool finishHook(SceneId scene, bool confirmed);

  // Explicit recovery.
  void beginRecovery(SceneId scene);
  void finishRecovery(
      SceneId scene, bool confirmed, const std::vector<ViewId> &views);

  // View confirmation.
  void markViewPending(ViewId view);
  void confirmView(ViewId view);
  void forgetView(ViewId view);
  void markNeedsSync(SceneId scene);
  // A committed failure after its normal hook completed owns no pending hook.
  void markFailed(SceneId scene);

 private:
  struct Scene
  {
    bool failed{false};
    bool needsSync{false};
    bool recovering{false};
    std::size_t pendingHooks{0};
  };

  static SyncState compute(const Scene &s);
  template <typename F>
  bool mutate(SceneId scene, F &&f);

  mutable std::mutex m_mutex;
  std::map<SceneId, Scene> m_scenes;
  std::set<ViewId> m_unconfirmed;
  std::shared_ptr<const TransitionListener> m_listener;
};

struct ResyncResult
{
  bool ready{false}; // host confirmed every registered view
  SyncState state{SyncState::FAILED}; // state after recovery
  std::vector<ViewId> views; // the views the confirmation covered
};

// What happens when the host cannot confirm a newly registered view.
enum class UnconfirmedView
{
  FAIL_REGISTRATION, // the view is removed again
  NEEDS_SYNC // the view stays, the scene becomes needs_sync
};

// Returns true when the host confirms the new view's downstream state
// matches the current scene.
using ViewConfirmation = std::function<bool(ViewId)>;
using RegistrationDone = std::function<void(std::optional<ViewId>)>;

class SyncService
{
 public:
  SyncService(Integration &integration,
      operations::OperationEngine &engine,
      SyncStates &states);
  ~SyncService();
  SyncService(const SyncService &) = delete;
  SyncService &operator=(const SyncService &) = delete;

  SyncStates &states();
  SyncState state(SceneId scene) const;
  // Render-guard predicate: registered, confirmed, and its scene is ready.
  bool viewReady(ViewId view) const;

  // Resynchronize: read + resynchronize on the scene; one scene-work slot.
  operations::Admission submitResynchronize(
      const operations::Credentials &credentials,
      const operations::OperationRef &ref,
      SceneId scene);
  operations::LocalAdmission submitLocalResynchronize(SceneId scene);
  std::optional<ResyncResult> result(const operations::OperationRef &ref) const;
  std::optional<ResyncResult> resultLocal(operations::LocalRef ref) const;

  // Host view-set changes after activation; queued host scene work that runs
  // at a later safe point (never inside a running hook). Returns false when
  // the integration rejects the submission.
  bool requestRegisterView(ViewRegistration registration,
      ViewConfirmation confirm,
      UnconfirmedView onUnconfirmed,
      RegistrationDone done = {});
  bool requestUnregisterView(ViewId view, std::function<void(bool)> done = {});

 private:
  using Slot = std::shared_ptr<std::optional<ResyncResult>>;
  operations::Request makeRequest(SceneId scene, Slot slot);
  ResyncResult recover(SceneId scene);

  Integration &m_integration;
  operations::OperationEngine &m_engine;
  SyncStates &m_states;
  std::shared_ptr<bool> m_alive;

  mutable std::mutex m_mutex;
  std::map<std::pair<std::string, std::uint64_t>, Slot> m_results;
  std::map<std::uint64_t, Slot> m_localResults;
};

///////////////////////////////////////////////////////////////////////////////
// Inlined definitions ////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

inline const char *syncStateName(SyncState s)
{
  switch (s) {
  case SyncState::READY:
    return "ready";
  case SyncState::NEEDS_SYNC:
    return "needs_sync";
  case SyncState::SYNCHRONIZING:
    return "synchronizing";
  case SyncState::FAILED:
    return "failed";
  }
  return "failed";
}

// SyncStates //

inline SyncState SyncStates::compute(const Scene &s)
{
  if (s.recovering)
    return SyncState::SYNCHRONIZING;
  if (s.failed)
    return SyncState::FAILED;
  if (s.needsSync)
    return SyncState::NEEDS_SYNC;
  if (s.pendingHooks)
    return SyncState::SYNCHRONIZING;
  return SyncState::READY;
}

template <typename F>
inline bool SyncStates::mutate(SceneId scene, F &&f)
{
  std::shared_ptr<const TransitionListener> listener;
  SyncState from, to;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto &s = m_scenes[scene];
    from = compute(s);
    f(s);
    to = compute(s);
    listener = m_listener;
  }
  if (from != to && listener) {
    try {
      (*listener)(scene, from, to);
    } catch (...) {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_scenes.at(scene).failed = true;
      return false;
    }
  }
  return true;
}

inline void SyncStates::prepareScene(SceneId scene)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_scenes.try_emplace(scene);
}

inline SyncState SyncStates::state(SceneId scene) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_scenes.find(scene);
  return it == m_scenes.end() ? SyncState::READY : compute(it->second);
}

inline bool SyncStates::sceneReady(SceneId scene) const
{
  return state(scene) == SyncState::READY;
}

inline bool SyncStates::viewConfirmed(ViewId view) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_unconfirmed.count(view) == 0;
}

inline void SyncStates::setTransitionListener(TransitionListener listener)
{
  auto prepared = listener
      ? std::make_shared<const TransitionListener>(std::move(listener))
      : std::shared_ptr<const TransitionListener>{};
  std::lock_guard<std::mutex> lock(m_mutex);
  m_listener = std::move(prepared);
}

inline void SyncStates::beginHook(SceneId scene)
{
  mutate(scene, [](Scene &s) { s.pendingHooks++; });
}

inline bool SyncStates::finishHook(SceneId scene, bool confirmed)
{
  bool ready = false;
  const bool notified = mutate(scene, [&](Scene &s) {
    if (s.pendingHooks)
      s.pendingHooks--;
    // Only explicit Resynchronize clears an earlier failure/needs_sync.
    ready = confirmed && !s.failed && !s.needsSync;
    if (!confirmed)
      s.failed = true;
  });
  return ready && notified;
}

inline void SyncStates::beginRecovery(SceneId scene)
{
  mutate(scene, [](Scene &s) { s.recovering = true; });
}

inline void SyncStates::finishRecovery(
    SceneId scene, bool confirmed, const std::vector<ViewId> &views)
{
  if (confirmed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto v : views)
      m_unconfirmed.erase(v);
  }
  mutate(scene, [&](Scene &s) {
    s.recovering = false;
    s.failed = !confirmed;
    if (confirmed)
      s.needsSync = false;
  });
}

inline void SyncStates::markViewPending(ViewId view)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_unconfirmed.insert(view);
}

inline void SyncStates::confirmView(ViewId view)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_unconfirmed.erase(view);
}

inline void SyncStates::forgetView(ViewId view)
{
  // Ids are never reused; a removed view simply stops being registered.
  confirmView(view);
}

inline void SyncStates::markNeedsSync(SceneId scene)
{
  mutate(scene, [](Scene &s) { s.needsSync = true; });
}

inline void SyncStates::markFailed(SceneId scene)
{
  mutate(scene, [](Scene &s) { s.failed = true; });
}

// SyncService //

inline SyncService::SyncService(Integration &integration,
    operations::OperationEngine &engine,
    SyncStates &states)
    : m_integration(integration),
      m_engine(engine),
      m_states(states),
      m_alive(std::make_shared<bool>(true))
{}

inline SyncService::~SyncService()
{
  m_alive.reset();
}

inline SyncStates &SyncService::states()
{
  return m_states;
}

inline SyncState SyncService::state(SceneId scene) const
{
  return m_states.state(scene);
}

inline bool SyncService::viewReady(ViewId view) const
{
  auto scene = m_integration.sceneOfView(view);
  return scene && m_integration.sceneSyncConfirmsView(view)
      && m_states.viewConfirmed(view) && m_states.sceneReady(*scene);
}

inline ResyncResult SyncService::recover(SceneId scene)
{
  ResyncResult r;
  // Fixed for the callback: registration changes are serialized safe-point
  // work and cannot run concurrently.
  for (auto view : m_integration.viewsOfScene(scene))
    if (m_integration.sceneSyncConfirmsView(view))
      r.views.push_back(view);
  m_states.beginRecovery(scene);
  bool confirmed = false;
  if (auto cb = m_integration.recoveryCallback(scene)) {
    try {
      confirmed = cb(scene);
    } catch (...) {
      confirmed = false;
    }
  }
  m_states.finishRecovery(scene, confirmed, r.views);
  r.ready = confirmed;
  r.state = m_states.state(scene);
  return r;
}

inline operations::Request SyncService::makeRequest(SceneId scene, Slot slot)
{
  operations::Request r;
  r.kind = "resynchronize";
  r.target = session::Target::scene(scene);
  r.capability = session::Capability::RESYNCHRONIZE;
  r.canonicalArguments = "resynchronize;scene=" + std::to_string(scene);
  std::weak_ptr<bool> alive = m_alive;
  r.executor = [this, alive, scene, slot](operations::StepContext &ctx) {
    operations::Step step;
    if (!alive.lock() || ctx.abandoned)
      return step;
    // Never replays edits; one attempt, no automatic retry.
    auto res = recover(scene);
    step.outcome = operations::Outcome::OK;
    step.result = std::string("{\"ready\":") + (res.ready ? "true" : "false")
        + ",\"state\":\"" + syncStateName(res.state) + "\"}";
    *slot = std::move(res);
    return step;
  };
  return r;
}

inline operations::Admission SyncService::submitResynchronize(
    const operations::Credentials &credentials,
    const operations::OperationRef &ref,
    SceneId scene)
{
  auto slot = std::make_shared<std::optional<ResyncResult>>();
  auto a = m_engine.submit(credentials, ref, makeRequest(scene, slot));
  if (a.code == operations::Outcome::OK && !a.replayed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_results[{ref.sessionId, ref.requestId}] = slot;
  }
  return a;
}

inline operations::LocalAdmission SyncService::submitLocalResynchronize(
    SceneId scene)
{
  auto slot = std::make_shared<std::optional<ResyncResult>>();
  auto a = m_engine.submitLocal(makeRequest(scene, slot));
  if (a.code == operations::Outcome::OK) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_localResults[a.ref.id] = slot;
  }
  return a;
}

inline std::optional<ResyncResult> SyncService::result(
    const operations::OperationRef &ref) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_results.find({ref.sessionId, ref.requestId});
  if (it == m_results.end())
    return std::nullopt;
  return *it->second;
}

inline std::optional<ResyncResult> SyncService::resultLocal(
    operations::LocalRef ref) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_localResults.find(ref.id);
  if (it == m_localResults.end())
    return std::nullopt;
  return *it->second;
}

inline bool SyncService::requestRegisterView(ViewRegistration registration,
    ViewConfirmation confirm,
    UnconfirmedView onUnconfirmed,
    RegistrationDone done)
{
  const SceneId scene = registration.scene;
  std::weak_ptr<bool> alive = m_alive;
  auto reg = std::make_shared<ViewRegistration>(std::move(registration));
  // Ordinary host scene work: serialized at a safe point, no scene slot.
  return m_integration.submit(
      scene, [this, alive, reg, confirm, onUnconfirmed, done](scene::Scene &) {
        if (!alive.lock())
          return;
        auto id = m_integration.registerView(*reg);
        if (id) {
          // Not renderable until the host confirms it.
          m_states.markViewPending(*id);
          bool ok = false;
          if (confirm) {
            try {
              ok = confirm(*id);
            } catch (...) {
              ok = false;
            }
          }
          if (ok) {
            m_states.confirmView(*id);
          } else if (onUnconfirmed == UnconfirmedView::FAIL_REGISTRATION) {
            m_integration.unregisterView(*id);
            m_states.forgetView(*id);
            id.reset();
          } else {
            m_states.markNeedsSync(reg->scene);
          }
        }
        if (done)
          done(id);
      });
}

inline bool SyncService::requestUnregisterView(
    ViewId view, std::function<void(bool)> done)
{
  auto scene = m_integration.sceneOfView(view);
  if (!scene)
    return false;
  std::weak_ptr<bool> alive = m_alive;
  return m_integration.submit(
      *scene, [this, alive, view, done](scene::Scene &) {
        if (!alive.lock())
          return;
        const bool ok = m_integration.unregisterView(view);
        if (ok)
          m_states.forgetView(view);
        if (done)
          done(ok);
      });
}

} // namespace vsr::integration::sync

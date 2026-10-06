// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/identity/Identity.hpp"
#include "vsr/integration/inspection/CapturePolicy.hpp"
// vsr_rendering
#include "vsr/rendering/view/EffectiveView.h"
#include "vsr/rendering/view/WorldBounds.h"
// std
#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace vsr::scene {
struct Scene;
} // namespace vsr::scene

namespace vsr::integration {

/*
 * Headless, transport-independent agent-integration core.
 *
 * The host registers the scenes and views it chooses to expose, together with
 * a per-target capability ceiling, then explicitly activates the integration.
 * Nothing is controllable before activation. Work submitted from any thread is
 * queued and only runs when the host services it at a safe execution point
 * (service() or runServiceLoop()), on the servicing thread.
 */

using SceneId = std::uint32_t;
using ViewId = std::uint32_t;

constexpr SceneId INVALID_SCENE_ID = std::numeric_limits<SceneId>::max();
constexpr ViewId INVALID_VIEW_ID = std::numeric_limits<ViewId>::max();

// Omitted grants are denied, including read.
struct SceneGrants
{
  bool read{false};
  bool edit{false};
  bool resynchronize{false};
};

struct ViewGrants
{
  bool read{false};
  bool render{false};
};

// Returns true when the host confirms downstream state is ready to render.
using SyncHook = std::function<bool(SceneId)>;

struct SceneRegistration
{
  std::string name;
  scene::Scene *scene{nullptr};
  SceneGrants grants;
  SyncHook normalSyncHook; // required when grants.edit
  SyncHook recoveryCallback; // required when grants.resynchronize
  bool nodeEdits{false}; // explicit exposure, in addition to the edit grant
  rendering::WorldBoundsProvider worldBounds;
};

// Value-only, owning-thread snapshot of the actual host binding. The adapter
// must only copy cached state: no device calls, renderer/readback allocations,
// waits or mutations. Owned value copies are bounded before retention. Settings
// include every effective public setting, including quality defaults. No
// handles, addresses, credentials or arbitrary diagnostics in text fields. Each
// view needs a distinct ViewRevisionTracker supplied through
// Inspector::setViewRevisions. The host must notify it on every effective
// change (including change-back between observations), using the camera,
// renderer, dimensions and notePipelineChanged contracts. Observations also
// detect changed returned values, but cannot reconstruct unreported history.
// Bound references assert actual source bindings, not convenient unrelated
// Scene objects.
using EffectiveSetting = rendering::EffectiveSetting;
using EffectiveCamera = rendering::EffectiveCamera;

struct EffectiveViewBinding : rendering::EffectiveView
{
  identity::ObjectHandle cameraHandle;
  identity::ObjectHandle rendererHandle;
};

using BindingObserver = std::function<std::optional<EffectiveViewBinding>()>;

enum class ViewRole
{
  UNKNOWN,
  GENERIC,
  HEADLESS,
  INTERACTIVE_VIEWPORT,
  INDEPENDENT_AGENT
};

struct ViewDescription
{
  std::string name;
  ViewRole role{ViewRole::UNKNOWN};
  std::string identity;
};

struct ViewRegistration
{
  std::string name;
  SceneId scene{INVALID_SCENE_ID};
  ViewGrants grants;
  BindingObserver observeBinding;
  ViewRole role{ViewRole::UNKNOWN};
  CapturePolicy capturePolicy;
  bool sceneSyncConfirmsBinding{true};
  bool cameraControlSupported{false};
};

struct SessionTiming
{
  double leaseSeconds{120.0};
  double renewalSeconds{30.0};
};

enum class ActivationError
{
  NONE,
  ALREADY_ACTIVE,
  ACTION_WITHOUT_READ,
  MISSING_SYNC_HOOK,
  MISSING_RECOVERY_CALLBACK,
  INVALID_LEASE,
  INVALID_RENEWAL,
  RENEWAL_NOT_SHORTER_THAN_LEASE
};

struct ActivationResult
{
  ActivationError error{ActivationError::NONE};
  std::string target; // offending target name, when applicable

  bool ok() const;
};

using SceneWork = std::function<void(scene::Scene &)>;
// Session-wide work that needs a safe point but no particular scene (e.g.
// discovery with zero registered scenes).
using SessionWork = std::function<void()>;

// Default bound on queued, not yet serviced work items.
constexpr std::size_t DEFAULT_QUEUE_CAPACITY = 1024;

class Integration
{
 public:
  // `queueCapacity` bounds submitted work not yet serviced; zero selects
  // DEFAULT_QUEUE_CAPACITY.
  explicit Integration(std::size_t queueCapacity = DEFAULT_QUEUE_CAPACITY);
  ~Integration();

  Integration(const Integration &) = delete;
  Integration &operator=(const Integration &) = delete;

  // Queries //

  bool active() const;
  bool isRegisteredScene(SceneId id) const;
  bool isRegisteredView(ViewId id) const;
  std::optional<SceneId> sceneOfView(ViewId id) const;
  // Effective ceilings; all-denied while inactive or for unregistered ids.
  SceneGrants sceneGrants(SceneId id) const;
  bool nodeEditsExposed(SceneId id) const;
  ViewGrants viewGrants(ViewId id) const;
  bool cameraControlSupported(ViewId id) const;
  bool updateCameraControlSupport(ViewId id, bool supported);
  bool agentCameraControlEnabled() const;
  // Launch-local host permission, independent of Scene EDIT. Never persist it.
  void setAgentCameraControlEnabled(bool enabled);
  // The registered normal-synchronization hook; empty while inactive or for
  // unregistered ids.
  SyncHook normalSyncHook(SceneId id) const;
  // The registered recovery callback; empty while inactive or for
  // unregistered ids.
  SyncHook recoveryCallback(SceneId id) const;
  // A copy taken under the lock; safe to call concurrently with activate().
  SessionTiming timing() const;
  std::size_t queueCapacity() const;
  // True only on the servicing thread while it runs submitted work.
  bool inServiceCall() const;
  // Invokes the explicit host adapter only from submitted safe-point work.
  std::optional<EffectiveViewBinding> observeViewBinding(ViewId id) const;
  rendering::WorldBoundsProvider worldBoundsProvider(SceneId id) const;
  std::optional<ViewDescription> viewDescription(ViewId id) const;
  std::size_t viewSlotCount() const;
  bool sceneSyncConfirmsView(ViewId id) const;
  std::optional<CapturePolicyObservation> capturePolicy(ViewId id) const;
  bool updateCapturePolicy(ViewId id, const CapturePolicy &policy);
  // Host declaration only; no grant/backend qualification follows from it.
  // FrameService owns qualification and the finite shared observation budget.
  bool updateFrameSourceExposure(
      ViewId id, bool exposed, std::uint64_t hostBudgetBytes);

  // Host registration (host thread, before activation) //

  std::optional<SceneId> registerScene(SceneRegistration registration);
  // Before activation from the host thread; after activation only as host
  // scene work on the servicing thread (see sync::SyncService), so the view
  // set never changes while a synchronization hook runs.
  std::optional<ViewId> registerView(ViewRegistration registration);
  // After activation, servicing thread only. Ids are never reused.
  bool unregisterView(ViewId id);
  std::vector<ViewId> viewsOfScene(SceneId id) const;

  // Activation //

  ActivationResult activate(SessionTiming timing = {});

  // Execution //

  // Thread-safe. Rejected while inactive, for an unregistered scene, or when
  // queueCapacity() items are already queued.
  bool submit(SceneId target, SceneWork work);
  // Thread-safe. As submit(), for work that needs no scene.
  bool submitSessionWork(SessionWork work);
  // Host safe point: runs all currently queued work on the calling thread and
  // returns the number of items run.
  //
  // Servicing is exclusive and never blocks: while one thread services, a
  // concurrent service() from another thread returns 0 without running work,
  // and a nested service() from inside running work returns 0. No lock is
  // held while work runs, so work may call any Integration method (and
  // engines layered on top may take their own locks) without inverting lock
  // order; the Integration mutex is always a leaf lock. inServiceCall() is
  // true exactly on the owning thread for the whole batch.
  //
  // Work that throws is caught per item and logged (without its message,
  // which may carry request data); the remaining items still run and the
  // servicing state is always reset.
  std::size_t service();
  // Blocking headless service loop; returns after requestStop().
  void runServiceLoop();
  // Thread-safe.
  void requestStop();

 private:
  struct SceneEntry
  {
    SceneRegistration registration;
  };

  struct ViewEntry
  {
    ViewRegistration registration;
    bool removed{false};
    std::uint64_t policyRevision{1};
    bool framesExposed{false};
    std::uint64_t frameHostBudgetBytes{0};
  };

  struct PendingWork
  {
    SceneId target{INVALID_SCENE_ID};
    SceneWork work;
    SessionWork sessionWork; // set instead of `work` for session work
  };

  ActivationResult validate(const SessionTiming &timing) const;

  identity::IdentityAuthority m_viewIdentities;
  mutable std::mutex m_mutex;
  std::condition_variable m_workAvailable;
  bool m_active{false};
  bool m_agentCameraControlEnabled{false};
  bool m_stopRequested{false};
  SessionTiming m_timing;
  std::vector<SceneEntry> m_scenes;
  std::vector<ViewEntry> m_views;
  std::deque<PendingWork> m_queue;
  std::size_t m_queueCapacity{DEFAULT_QUEUE_CAPACITY};
  std::thread::id m_servicingThread; // empty when nobody services
};

// Inlined definitions ////////////////////////////////////////////////////////

inline bool ActivationResult::ok() const
{
  return error == ActivationError::NONE;
}

} // namespace vsr::integration

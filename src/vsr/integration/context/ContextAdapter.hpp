// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Header-only: the vsr_integration library globs only top-level sources (see
// ticket 19). It is also templated on the context type so vsr_integration
// never links vsr_app and vsr_app never depends on the integration.

// vsr_core
#include "vsr/core/TypeMacros.hpp"
// vsr_integration
#include "vsr/integration/Integration.hpp"
// std
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace vsr::integration::context {

/*
 * Standard application-Context convenience layer (spec §1).
 *
 * - registerContextScene() registers the scene owned by a vsr::app::Context
 *   (any type exposing `ctx.vsr.scene`). It registers the scene only: views
 *   are registered separately by the host, and rendering support is never
 *   inferred from a Context. Context owns neither the Image Pipeline nor the
 *   RenderIndex, so the host supplies synchronization hooks itself.
 * - ViewerExecutionHook services queued integration work at the viewer's
 *   safe point (call once per frame, outside any scene iteration).
 * - HeadlessServiceLoop runs the blocking service loop for hosts without a
 *   graphical event loop and stops on a thread-safe/signal-safe flag.
 *
 * All three are thin wrappers over the public Integration API; a custom host
 * that calls registerScene()/service()/runServiceLoop() directly behaves
 * identically.
 */

struct ContextSceneOptions
{
  std::string name{"scene"};
  SceneGrants grants;
  SyncHook normalSyncHook; // required when grants.edit
  SyncHook recoveryCallback; // required when grants.resynchronize
  bool nodeEdits{false};
  bool serializedArrayWrites{false};
  rendering::WorldBoundsProvider worldBounds;
};

// Host thread, before activation. Same result as Integration::registerScene.
template <typename ContextT>
std::optional<SceneId> registerContextScene(
    Integration &integration, ContextT &ctx, ContextSceneOptions options = {});

// Viewer safe point: runs all currently queued integration work on the
// viewer (servicing) thread. Does nothing while the integration is inactive.
class ViewerExecutionHook
{
 public:
  explicit ViewerExecutionHook(Integration &integration);
  std::size_t operator()();

 private:
  Integration *m_integration{nullptr};
};

// Blocking headless service loop. run() returns once stop() was called or,
// when a stop flag is given, once that flag becomes true (e.g. set from a
// SIGINT handler, where requestStop() itself is not async-signal-safe).
class HeadlessServiceLoop
{
 public:
  explicit HeadlessServiceLoop(Integration &integration,
      const volatile std::sig_atomic_t *stopFlag = nullptr,
      std::chrono::milliseconds pollInterval = std::chrono::milliseconds(50));

  void run();
  void stop(); // thread-safe

 private:
  struct Watcher
  {
    Watcher() = default;
    ~Watcher();
    VSR_NOT_COPYABLE(Watcher)
    VSR_NOT_MOVEABLE(Watcher)

    std::atomic<bool> done{false};
    std::thread thread;
  };

  Integration *m_integration{nullptr};
  const volatile std::sig_atomic_t *m_stopFlag{nullptr};
  std::chrono::milliseconds m_pollInterval;
};

// Inlined definitions ////////////////////////////////////////////////////////

template <typename ContextT>
inline std::optional<SceneId> registerContextScene(
    Integration &integration, ContextT &ctx, ContextSceneOptions options)
{
  SceneRegistration r;
  r.name = std::move(options.name);
  r.scene = &ctx.vsr.scene;
  r.grants = options.grants;
  r.nodeEdits = options.nodeEdits;
  r.serializedArrayWrites = options.serializedArrayWrites;
  r.worldBounds = std::move(options.worldBounds);
  r.normalSyncHook = std::move(options.normalSyncHook);
  r.recoveryCallback = std::move(options.recoveryCallback);
  return integration.registerScene(std::move(r));
}

inline ViewerExecutionHook::ViewerExecutionHook(Integration &integration)
    : m_integration(&integration)
{}

inline std::size_t ViewerExecutionHook::operator()()
{
  if (!m_integration->active())
    return 0;
  return m_integration->service();
}

inline HeadlessServiceLoop::HeadlessServiceLoop(Integration &integration,
    const volatile std::sig_atomic_t *stopFlag,
    std::chrono::milliseconds pollInterval)
    : m_integration(&integration),
      m_stopFlag(stopFlag),
      m_pollInterval(pollInterval)
{}

inline HeadlessServiceLoop::Watcher::~Watcher()
{
  done.store(true);
  if (thread.joinable())
    thread.join();
}

inline void HeadlessServiceLoop::run()
{
  Watcher watcher;
  if (m_stopFlag) {
    watcher.thread = std::thread([&] {
      while (!watcher.done.load()) {
        if (*m_stopFlag) {
          m_integration->requestStop();
          return;
        }
        std::this_thread::sleep_for(m_pollInterval);
      }
    });
  }
  m_integration->runServiceLoop();
}

inline void HeadlessServiceLoop::stop()
{
  m_integration->requestStop();
}

} // namespace vsr::integration::context

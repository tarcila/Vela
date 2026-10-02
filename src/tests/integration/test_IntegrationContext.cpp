// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_core
#include "vsr/core/Logging.hpp"
#include "vsr/core/TypeMacros.hpp"
// vsr_integration
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/context/ContextAdapter.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// catch
#include "catch.hpp"
// std
#include <atomic>
#include <chrono>
#include <csignal>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace vsr::integration;

namespace {

// Same member shape as vsr::app::Context for the adapter (ctx.vsr.scene);
// vsrIntegrationTests does not link vsr_app. The real Context is exercised by
// vsrTutorialIntegrationHeadless.
struct FakeContext
{
  struct
  {
    vsr::scene::Scene scene;
  } vsr;
};

bool confirmReady(SceneId)
{
  return true;
}

struct Observation
{
  std::vector<bool> inService;
  std::vector<std::thread::id> threads;
  std::vector<const vsr::scene::Scene *> scenes;
};

// Drives the same client-side sequence against either host and records
// where scene access happened.
Observation drive(Integration &integration,
    SceneId scene,
    const std::function<std::size_t()> &safePoint)
{
  Observation obs;
  auto work = [&](vsr::scene::Scene &s) {
    obs.inService.push_back(integration.inServiceCall());
    obs.threads.push_back(std::this_thread::get_id());
    obs.scenes.push_back(&s);
  };
  // Submitted from another thread; nothing may touch the scene until the
  // host reaches its safe point.
  std::thread client([&] {
    REQUIRE(integration.submit(scene, work));
    REQUIRE(integration.submit(scene, work));
  });
  client.join();
  REQUIRE(obs.inService.empty());
  REQUIRE(safePoint() == 2);
  return obs;
}

constexpr auto kWaitDeadline = std::chrono::seconds(10);

// Polls `done` until it holds or the deadline expires; returns whether it held.
bool waitFor(const std::function<bool()> &done)
{
  const auto deadline = std::chrono::steady_clock::now() + kWaitDeadline;
  while (!done()) {
    if (std::chrono::steady_clock::now() >= deadline)
      return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

// Runs the loop on a host thread whose completion is observable with a
// deadline, so a hung loop fails the test instead of hanging the suite.
struct HostThread
{
  explicit HostThread(context::HeadlessServiceLoop &loop) : loop(loop)
  {
    auto done = std::make_shared<std::promise<void>>();
    finished = done->get_future();
    thread = std::thread([&loop, done] {
      loop.run();
      done->set_value();
    });
  }

  // Joins when the loop has returned in time. On timeout the loop is told
  // to stop so the thread can still be joined; only if that also hangs is
  // the thread detached. Either way the caller fails.
  bool joinWithin(std::chrono::steady_clock::duration timeout)
  {
    if (finished.wait_for(timeout) == std::future_status::ready) {
      thread.join();
      return true;
    }
    loop.stop();
    if (finished.wait_for(kWaitDeadline) == std::future_status::ready)
      thread.join();
    else
      thread.detach();
    return false;
  }

  // An early assertion failure still stops and joins the loop, so no host
  // thread outlives the scenario's loop and integration.
  ~HostThread()
  {
    if (thread.joinable())
      joinWithin(std::chrono::steady_clock::duration::zero());
  }

  context::HeadlessServiceLoop &loop;
  std::thread thread;
  std::future<void> finished;
};

/*
 * A host logging callback can throw while Integration reports failed work.
 */
struct ThrowingLogSink
{
  ThrowingLogSink();
  ~ThrowingLogSink();
  VSR_NOT_COPYABLE(ThrowingLogSink)
  VSR_NOT_MOVEABLE(ThrowingLogSink)
};

ThrowingLogSink::ThrowingLogSink()
{
  vsr::core::setLoggingCallback([](vsr::core::LogLevel, std::string) {
    throw std::runtime_error("host log failure");
  });
}

ThrowingLogSink::~ThrowingLogSink()
{
  vsr::core::setNoLogging();
}

} // namespace

SCENARIO("Context adapter registers only the Context scene", "[integration]")
{
  FakeContext ctx;
  Integration integration;

  context::ContextSceneOptions options;
  options.name = "main";
  options.grants.read = true;
  auto sceneId = context::registerContextScene(integration, ctx, options);
  REQUIRE(sceneId);

  THEN("no view and no render grant is inferred from the Context")
  {
    REQUIRE(integration.viewsOfScene(*sceneId).empty());
    REQUIRE(integration.activate().ok());
    REQUIRE(integration.sceneGrants(*sceneId).read);
    REQUIRE_FALSE(integration.sceneGrants(*sceneId).edit);
    REQUIRE(integration.viewsOfScene(*sceneId).empty());
  }

  THEN("views are registered separately with host-chosen grants")
  {
    ViewRegistration v;
    v.name = "main-view";
    v.scene = *sceneId;
    v.grants.read = true;
    auto viewId = integration.registerView(v);
    REQUIRE(viewId);
    REQUIRE(integration.activate().ok());
    REQUIRE(integration.viewGrants(*viewId).read);
    REQUIRE_FALSE(integration.viewGrants(*viewId).render);
  }

  THEN("activation validation is the same as for a custom host")
  {
    FakeContext other;
    Integration strict;
    context::ContextSceneOptions editing;
    editing.grants.read = true;
    editing.grants.edit = true; // no normal-synchronization hook
    REQUIRE(context::registerContextScene(strict, other, editing));
    auto result = strict.activate();
    REQUIRE(result.error == ActivationError::MISSING_SYNC_HOOK);
  }
}

SCENARIO("Context host and custom host behave the same", "[integration]")
{
  FakeContext ctx;
  Integration viaContext;
  context::ContextSceneOptions options;
  options.grants.read = true;
  options.grants.edit = true;
  options.normalSyncHook = confirmReady;
  auto ctxScene = context::registerContextScene(viaContext, ctx, options);
  REQUIRE(ctxScene);
  REQUIRE(viaContext.activate().ok());

  vsr::scene::Scene customScene;
  Integration custom;
  SceneRegistration r;
  r.name = "scene";
  r.scene = &customScene;
  r.grants.read = true;
  r.grants.edit = true;
  r.normalSyncHook = confirmReady;
  auto customId = custom.registerScene(r);
  REQUIRE(customId);
  REQUIRE(custom.activate().ok());

  THEN("ids and effective grants match")
  {
    REQUIRE(*ctxScene == *customId);
    REQUIRE(viaContext.sceneGrants(*ctxScene).read
        == custom.sceneGrants(*customId).read);
    REQUIRE(viaContext.sceneGrants(*ctxScene).edit
        == custom.sceneGrants(*customId).edit);
    REQUIRE(viaContext.sceneGrants(*ctxScene).resynchronize
        == custom.sceneGrants(*customId).resynchronize);
    REQUIRE(static_cast<bool>(viaContext.normalSyncHook(*ctxScene)));
  }

  THEN("work runs only at the safe point, on the servicing thread")
  {
    context::ViewerExecutionHook hook(viaContext);
    auto a = drive(viaContext, *ctxScene, [&] { return hook(); });
    auto b = drive(custom, *customId, [&] { return custom.service(); });

    REQUIRE(a.inService == std::vector<bool>{true, true});
    REQUIRE(a.inService == b.inService);
    for (auto id : a.threads)
      REQUIRE(id == std::this_thread::get_id());
    for (auto *s : a.scenes)
      REQUIRE(s == &ctx.vsr.scene);
    for (auto *s : b.scenes)
      REQUIRE(s == &customScene);
  }
}

SCENARIO("Viewer execution hook is inert while inactive", "[integration]")
{
  FakeContext ctx;
  Integration integration;
  context::ContextSceneOptions options;
  options.grants.read = true;
  auto sceneId = context::registerContextScene(integration, ctx, options);
  REQUIRE(sceneId);

  context::ViewerExecutionHook hook(integration);
  REQUIRE(hook() == 0);
  REQUIRE_FALSE(integration.submit(*sceneId, [](vsr::scene::Scene &) {}));
}

SCENARIO(
    "Headless service loop joins its watcher on exception", "[integration]")
{
  Integration integration;
  REQUIRE(integration.activate().ok());
  REQUIRE(integration.submitSessionWork(
      [] { throw std::runtime_error("work failure"); }));

  volatile std::sig_atomic_t stopFlag = 0;
  context::HeadlessServiceLoop loop(
      integration, &stopFlag, std::chrono::milliseconds(1));
  ThrowingLogSink logging;

  // Propagation instead of process termination proves the watcher was joined
  // before its owning thread object was destroyed during unwinding.
  REQUIRE_THROWS_WITH(loop.run(), "host log failure");
}

SCENARIO("Headless service loop runs work and stops", "[integration]")
{
  FakeContext ctx;
  Integration integration;
  context::ContextSceneOptions options;
  options.grants.read = true;
  auto sceneId = context::registerContextScene(integration, ctx, options);
  REQUIRE(sceneId);
  REQUIRE(integration.activate().ok());

  WHEN("stopped through the loop")
  {
    context::HeadlessServiceLoop loop(integration);
    std::atomic<bool> ranInService{false};
    std::thread::id workThread;
    HostThread host(loop);
    auto hostId = host.thread.get_id();

    REQUIRE(integration.submit(*sceneId, [&](vsr::scene::Scene &) {
      workThread = std::this_thread::get_id();
      ranInService = integration.inServiceCall();
      loop.stop();
    }));
    INFO("headless loop did not stop within the deadline after loop.stop()");
    REQUIRE(host.joinWithin(kWaitDeadline));
    REQUIRE(ranInService);
    REQUIRE(workThread == hostId);
  }

  WHEN("stopped through a signal-style flag")
  {
    volatile std::sig_atomic_t stopFlag = 0;
    context::HeadlessServiceLoop loop(
        integration, &stopFlag, std::chrono::milliseconds(1));
    std::atomic<int> ran{0};
    HostThread host(loop);
    REQUIRE(integration.submit(*sceneId, [&](vsr::scene::Scene &) { ran++; }));
    {
      INFO("submitted work did not run within the deadline");
      const bool ranInTime = waitFor([&] { return ran.load() != 0; });
      stopFlag = 1; // release the loop even when failing
      REQUIRE(ranInTime);
    }
    INFO("headless loop did not stop within the deadline after the flag");
    REQUIRE(host.joinWithin(kWaitDeadline));
    REQUIRE(ran == 1);
  }
}

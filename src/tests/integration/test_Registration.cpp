// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/core/Logging.hpp"
#include "vsr/integration/Integration.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace vsr::integration;

namespace {

bool confirmReady(SceneId)
{
  return true;
}

SceneRegistration readableScene(vsr::scene::Scene &scene)
{
  SceneRegistration r;
  r.name = "scene";
  r.scene = &scene;
  r.grants.read = true;
  return r;
}

} // namespace

SCENARIO("Integration exposes nothing until activated", "[integration]")
{
  vsr::scene::Scene scene;
  Integration integration;

  auto sceneId = integration.registerScene(readableScene(scene));
  REQUIRE(sceneId);

  THEN("an inactive integration grants nothing and accepts no work")
  {
    REQUIRE_FALSE(integration.active());
    REQUIRE_FALSE(integration.sceneGrants(*sceneId).read);
    REQUIRE_FALSE(integration.submit(*sceneId, [](vsr::scene::Scene &) {}));
    REQUIRE(integration.service() == 0);
  }

  WHEN("the host activates")
  {
    REQUIRE(integration.activate().ok());
    THEN("configured grants become the ceiling")
    {
      REQUIRE(integration.active());
      REQUIRE(integration.sceneGrants(*sceneId).read);
    }
    THEN("registration is closed once active")
    {
      REQUIRE_FALSE(integration.registerScene(readableScene(scene)));
    }
    THEN("activating twice is rejected")
    {
      REQUIRE(integration.activate().error == ActivationError::ALREADY_ACTIVE);
    }
  }
}

SCENARIO("Only registered state is addressable", "[integration]")
{
  vsr::scene::Scene scene;
  Integration integration;
  auto sceneId = integration.registerScene(readableScene(scene));
  REQUIRE(sceneId);

  ViewRegistration view;
  view.name = "view";
  view.scene = *sceneId;
  view.grants.read = true;
  auto viewId = integration.registerView(view);
  REQUIRE(viewId);
  REQUIRE(integration.sceneOfView(*viewId) == *sceneId);

  ViewRegistration orphan;
  orphan.name = "orphan";
  orphan.scene = *sceneId + 1;
  REQUIRE_FALSE(integration.registerView(orphan));

  SceneRegistration noScene;
  noScene.name = "null";
  REQUIRE_FALSE(integration.registerScene(noScene));

  REQUIRE(integration.activate().ok());
  REQUIRE_FALSE(integration.isRegisteredScene(*sceneId + 1));
  REQUIRE_FALSE(integration.isRegisteredView(*viewId + 1));
  REQUIRE_FALSE(integration.sceneGrants(*sceneId + 1).read);
  REQUIRE_FALSE(integration.viewGrants(*viewId + 1).read);
  REQUIRE_FALSE(integration.submit(*sceneId + 1, [](vsr::scene::Scene &) {}));
}

SCENARIO("Capability ceilings are explicit per target", "[integration]")
{
  vsr::scene::Scene scene;

  GIVEN("omitted grants")
  {
    Integration integration;
    SceneRegistration r;
    r.name = "bare";
    r.scene = &scene;
    auto id = integration.registerScene(r);
    ViewRegistration v;
    v.name = "bareView";
    v.scene = *id;
    auto viewId = integration.registerView(v);
    REQUIRE(integration.activate().ok());
    THEN("everything, including read, is denied")
    {
      auto g = integration.sceneGrants(*id);
      REQUIRE_FALSE(g.read);
      REQUIRE_FALSE(g.edit);
      REQUIRE_FALSE(g.resynchronize);
      auto vg = integration.viewGrants(*viewId);
      REQUIRE_FALSE(vg.read);
      REQUIRE_FALSE(vg.render);
    }
  }

  GIVEN("edit without read")
  {
    Integration integration;
    SceneRegistration r;
    r.name = "writeOnly";
    r.scene = &scene;
    r.grants.edit = true;
    r.normalSyncHook = confirmReady;
    integration.registerScene(r);
    auto result = integration.activate();
    REQUIRE(result.error == ActivationError::ACTION_WITHOUT_READ);
    REQUIRE(result.target == "writeOnly");
    REQUIRE_FALSE(integration.active());
  }

  GIVEN("resynchronize without read")
  {
    Integration integration;
    SceneRegistration r;
    r.name = "resyncOnly";
    r.scene = &scene;
    r.grants.resynchronize = true;
    r.recoveryCallback = confirmReady;
    integration.registerScene(r);
    REQUIRE(
        integration.activate().error == ActivationError::ACTION_WITHOUT_READ);
  }

  GIVEN("render without read on the view")
  {
    Integration integration;
    auto id = integration.registerScene(readableScene(scene));
    ViewRegistration v;
    v.name = "renderOnly";
    v.scene = *id;
    v.grants.render = true;
    integration.registerView(v);
    auto result = integration.activate();
    REQUIRE(result.error == ActivationError::ACTION_WITHOUT_READ);
    REQUIRE(result.target == "renderOnly");
  }
}

SCENARIO("Edit and resynchronize need their own host hooks", "[integration]")
{
  vsr::scene::Scene scene;

  auto withGrants = [&](bool edit, bool resync, bool syncHook, bool recovery) {
    Integration integration;
    auto r = readableScene(scene);
    r.grants.edit = edit;
    r.grants.resynchronize = resync;
    if (syncHook)
      r.normalSyncHook = confirmReady;
    if (recovery)
      r.recoveryCallback = confirmReady;
    integration.registerScene(r);
    return integration.activate().error;
  };

  REQUIRE(withGrants(true, false, false, false)
      == ActivationError::MISSING_SYNC_HOOK);
  REQUIRE(withGrants(false, true, false, false)
      == ActivationError::MISSING_RECOVERY_CALLBACK);
  // Neither capability requires the other's hook.
  REQUIRE(withGrants(true, false, true, false) == ActivationError::NONE);
  REQUIRE(withGrants(false, true, false, true) == ActivationError::NONE);
  REQUIRE(withGrants(true, true, true, false)
      == ActivationError::MISSING_RECOVERY_CALLBACK);
  REQUIRE(withGrants(true, true, false, true)
      == ActivationError::MISSING_SYNC_HOOK);
  REQUIRE(withGrants(true, true, true, true) == ActivationError::NONE);
}

SCENARIO(
    "Lease and renewal durations are validated, never clamped", "[integration]")
{
  vsr::scene::Scene scene;
  const double inf = std::numeric_limits<double>::infinity();
  const double nan = std::numeric_limits<double>::quiet_NaN();

  auto activateWith = [&](double lease, double renewal) {
    Integration integration;
    integration.registerScene(readableScene(scene));
    auto error = integration.activate({lease, renewal}).error;
    if (error != ActivationError::NONE)
      REQUIRE_FALSE(integration.active());
    return error;
  };

  REQUIRE(activateWith(0.0, 1.0) == ActivationError::INVALID_LEASE);
  REQUIRE(activateWith(-5.0, 1.0) == ActivationError::INVALID_LEASE);
  REQUIRE(activateWith(inf, 1.0) == ActivationError::INVALID_LEASE);
  REQUIRE(activateWith(nan, 1.0) == ActivationError::INVALID_LEASE);
  REQUIRE(activateWith(10.0, 0.0) == ActivationError::INVALID_RENEWAL);
  REQUIRE(activateWith(10.0, -1.0) == ActivationError::INVALID_RENEWAL);
  REQUIRE(activateWith(10.0, inf) == ActivationError::INVALID_RENEWAL);
  REQUIRE(activateWith(10.0, nan) == ActivationError::INVALID_RENEWAL);
  REQUIRE(activateWith(10.0, 10.0)
      == ActivationError::RENEWAL_NOT_SHORTER_THAN_LEASE);
  REQUIRE(activateWith(10.0, 20.0)
      == ActivationError::RENEWAL_NOT_SHORTER_THAN_LEASE);

  Integration integration;
  integration.registerScene(readableScene(scene));
  REQUIRE(integration.timing().leaseSeconds == 120.0);
  REQUIRE(integration.timing().renewalSeconds == 30.0);
  REQUIRE(integration.activate({10.0, 2.5}).ok());
  REQUIRE(integration.timing().leaseSeconds == 10.0);
  REQUIRE(integration.timing().renewalSeconds == 2.5);
}

SCENARIO(
    "Submitted work touches scenes only at host safe points", "[integration]")
{
  vsr::scene::Scene scene;
  Integration integration;
  auto sceneId = integration.registerScene(readableScene(scene));
  REQUIRE(integration.activate().ok());

  GIVEN("work submitted from another thread")
  {
    std::atomic<bool> ran{false};
    std::thread::id ranOn;
    bool insideService = false;
    vsr::scene::Scene *seen = nullptr;

    std::thread submitter([&] {
      REQUIRE(integration.submit(*sceneId, [&](vsr::scene::Scene &s) {
        ranOn = std::this_thread::get_id();
        insideService = integration.inServiceCall();
        seen = &s;
        ran = true;
      }));
    });
    submitter.join();

    THEN("nothing runs until the host services it")
    {
      REQUIRE_FALSE(ran);
      REQUIRE_FALSE(integration.inServiceCall());
      REQUIRE(integration.service() == 1);
      REQUIRE(ran);
      REQUIRE(ranOn == std::this_thread::get_id());
      REQUIRE(insideService);
      REQUIRE(seen == &scene);
      REQUIRE_FALSE(integration.inServiceCall());
    }
  }

  GIVEN("a blocking headless service loop")
  {
    std::thread::id loopThread;
    std::atomic<int> count{0};
    std::atomic<bool> allInside{true};
    std::atomic<bool> allOnLoop{true};

    std::thread host([&] { integration.runServiceLoop(); });
    loopThread = host.get_id();

    const int N = 16;
    for (int i = 0; i < N; i++) {
      REQUIRE(integration.submit(*sceneId, [&](vsr::scene::Scene &) {
        if (!integration.inServiceCall())
          allInside = false;
        if (std::this_thread::get_id() != loopThread)
          allOnLoop = false;
        count++;
      }));
    }
    integration.requestStop();
    host.join();

    THEN("all work ran on the servicing thread inside a service call")
    {
      REQUIRE(count == N);
      REQUIRE(allInside);
      REQUIRE(allOnLoop);
    }
  }
}

SCENARIO("Servicing is exclusive: concurrent and nested service() run nothing",
    "[integration]")
{
  vsr::scene::Scene scene;
  Integration integration;
  auto sceneId = integration.registerScene(readableScene(scene));
  REQUIRE(integration.activate().ok());

  GIVEN("a nested service() from inside running work")
  {
    std::size_t nested = 99;
    bool insideAfterNested = false;
    int laterRan = 0;
    REQUIRE(integration.submit(*sceneId, [&](vsr::scene::Scene &) {
      REQUIRE(integration.submit(
          *sceneId, [&](vsr::scene::Scene &) { laterRan++; }));
      nested = integration.service();
      insideAfterNested = integration.inServiceCall();
    }));
    REQUIRE(integration.service() == 1);
    THEN("the nested call returns 0 and servicing state is kept")
    {
      REQUIRE(nested == 0);
      REQUIRE(laterRan == 0);
      REQUIRE(insideAfterNested);
      REQUIRE_FALSE(integration.inServiceCall());
      REQUIRE(integration.service() == 1);
      REQUIRE(laterRan == 1);
    }
  }

  GIVEN("a second thread calling service() while one thread services")
  {
    std::mutex m;
    std::condition_variable cv;
    bool entered = false;
    bool release = false;
    std::atomic<int> concurrent{0};
    std::atomic<int> maxConcurrent{0};

    REQUIRE(integration.submit(*sceneId, [&](vsr::scene::Scene &) {
      const int c = ++concurrent;
      if (c > maxConcurrent)
        maxConcurrent = c;
      std::unique_lock<std::mutex> lock(m);
      entered = true;
      cv.notify_all();
      cv.wait(lock, [&] { return release; });
      --concurrent;
    }));

    std::size_t first = 0;
    std::thread host([&] { first = integration.service(); });
    {
      std::unique_lock<std::mutex> lock(m);
      cv.wait(lock, [&] { return entered; });
    }

    // Queued while the first batch runs: the other thread must not run it.
    std::atomic<int> laterRan{0};
    REQUIRE(integration.submit(*sceneId, [&](vsr::scene::Scene &) {
      const int c = ++concurrent;
      if (c > maxConcurrent)
        maxConcurrent = c;
      laterRan++;
      --concurrent;
    }));
    const std::size_t second = integration.service();
    const bool otherInside = integration.inServiceCall();
    {
      std::lock_guard<std::mutex> lock(m);
      release = true;
    }
    cv.notify_all();
    host.join();

    THEN("the concurrent call returns 0 without running work")
    {
      REQUIRE(first == 1);
      REQUIRE(second == 0);
      REQUIRE_FALSE(otherInside);
      REQUIRE(laterRan == 0);
      REQUIRE(integration.service() == 1);
      REQUIRE(laterRan == 1);
      REQUIRE(maxConcurrent == 1);
    }
  }

  GIVEN("many threads servicing concurrently")
  {
    std::atomic<int> concurrent{0};
    std::atomic<int> maxConcurrent{0};
    std::atomic<int> ran{0};
    std::atomic<bool> allInside{true};
    const int N = 400;
    std::atomic<bool> done{false};
    std::vector<std::thread> servicers;
    for (int t = 0; t < 4; t++)
      servicers.emplace_back([&] {
        while (!done)
          integration.service();
      });
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(30);
    for (int i = 0; i < N && std::chrono::steady_clock::now() < deadline;
         i++) {
      while (!integration.submit(*sceneId, [&](vsr::scene::Scene &) {
        const int c = ++concurrent;
        if (c > maxConcurrent)
          maxConcurrent = c;
        if (!integration.inServiceCall())
          allInside = false;
        std::this_thread::yield();
        ran++;
        --concurrent;
      })) {
        if (std::chrono::steady_clock::now() >= deadline)
          break;
        std::this_thread::yield();
      }
    }
    while (ran < N && std::chrono::steady_clock::now() < deadline)
      std::this_thread::yield();
    done = true;
    for (auto &t : servicers)
      t.join();
    INFO("work items run before the deadline: " << ran.load() << " of " << N);
    REQUIRE(ran == N);
    THEN("work never overlaps and always sees inServiceCall()")
    {
      REQUIRE(ran == N);
      REQUIRE(maxConcurrent == 1);
      REQUIRE(allInside);
      REQUIRE_FALSE(integration.inServiceCall());
    }
  }
}

SCENARIO("Throwing work is isolated per item", "[integration]")
{
  vsr::scene::Scene scene;
  Integration integration;
  auto sceneId = integration.registerScene(readableScene(scene));
  REQUIRE(integration.activate().ok());

  int ran = 0;
  REQUIRE(integration.submit(*sceneId, [&](vsr::scene::Scene &) { ran++; }));
  REQUIRE(integration.submit(*sceneId, [&](vsr::scene::Scene &) -> void {
    throw std::runtime_error("secret-token-must-not-be-logged");
  }));
  REQUIRE(integration.submit(*sceneId, [&](vsr::scene::Scene &) { throw 7; }));
  REQUIRE(integration.submit(*sceneId, [&](vsr::scene::Scene &) { ran++; }));

  std::vector<std::string> logged;
  std::size_t n = 0;
  {
    // Restores logging even if service() escapes an exception.
    struct Capture
    {
      explicit Capture(std::vector<std::string> &out)
      {
        vsr::core::setLoggingCallback([&out](vsr::core::LogLevel,
                                          std::string msg) {
          out.push_back(std::move(msg));
        });
      }
      ~Capture()
      {
        vsr::core::setNoLogging();
      }
    } capture(logged);
    n = integration.service();
  }

  REQUIRE(n == 4);
  REQUIRE(ran == 2);
  REQUIRE_FALSE(integration.inServiceCall());
  REQUIRE(logged.size() == 2);
  for (const auto &msg : logged)
    REQUIRE(msg.find("secret-token") == std::string::npos);

  // Servicing state was reset: later work still runs.
  REQUIRE(integration.submit(*sceneId, [&](vsr::scene::Scene &) {
    REQUIRE(integration.inServiceCall());
    ran++;
  }));
  REQUIRE(integration.service() == 1);
  REQUIRE(ran == 3);
}

SCENARIO("The work queue is bounded", "[integration]")
{
  vsr::scene::Scene scene;
  REQUIRE(Integration().queueCapacity() == DEFAULT_QUEUE_CAPACITY);
  REQUIRE(Integration(0).queueCapacity() == DEFAULT_QUEUE_CAPACITY);

  Integration integration(2);
  REQUIRE(integration.queueCapacity() == 2);
  auto sceneId = integration.registerScene(readableScene(scene));
  REQUIRE(integration.activate().ok());

  auto noop = [](vsr::scene::Scene &) {};
  REQUIRE(integration.submit(*sceneId, noop));
  REQUIRE(integration.submitSessionWork([] {}));
  REQUIRE_FALSE(integration.submit(*sceneId, noop));
  REQUIRE_FALSE(integration.submitSessionWork([] {}));
  REQUIRE(integration.service() == 2);
  REQUIRE(integration.submit(*sceneId, noop));
}

SCENARIO("Session work runs at a safe point without a registered scene",
    "[integration]")
{
  Integration integration;
  REQUIRE_FALSE(integration.submitSessionWork([] {}));
  REQUIRE(integration.activate().ok());
  bool inside = false;
  REQUIRE(integration.submitSessionWork(
      [&] { inside = integration.inServiceCall(); }));
  REQUIRE_FALSE(integration.submitSessionWork({}));
  REQUIRE(integration.service() == 1);
  REQUIRE(inside);
}

SCENARIO("timing() is safe to read while activating", "[integration]")
{
  vsr::scene::Scene scene;
  Integration integration;
  integration.registerScene(readableScene(scene));
  std::atomic<bool> stop{false};
  std::atomic<bool> torn{false};
  std::thread reader([&] {
    while (!stop) {
      const auto t = integration.timing();
      const bool before = t.leaseSeconds == 120.0 && t.renewalSeconds == 30.0;
      const bool after = t.leaseSeconds == 10.0 && t.renewalSeconds == 2.5;
      if (!before && !after)
        torn = true;
    }
  });
  REQUIRE(integration.activate({10.0, 2.5}).ok());
  stop = true;
  reader.join();
  REQUIRE_FALSE(torn);
  REQUIRE(integration.timing().leaseSeconds == 10.0);
}

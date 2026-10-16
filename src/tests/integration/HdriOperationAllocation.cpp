// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_integration
#include "vsr/integration/content/PreparedHdriPublication.hpp"
#include "vsr/integration/edit/AgentEdit.hpp"
#include "vsr/integration/operations/Operations.hpp"
// std
#include <atomic>
#include <cstdlib>
#include <future>
#include <iostream>
#include <new>

namespace {
thread_local int countdown = -1;
thread_local bool injected = false;
} // namespace

void *operator new(std::size_t size)
{
  if (countdown == 0) {
    countdown = -1;
    injected = true;
    throw std::bad_alloc();
  }
  if (countdown > 0)
    --countdown;
  if (auto *p = std::malloc(size ? size : 1))
    return p;
  throw std::bad_alloc();
}
void *operator new[](std::size_t n)
{
  return ::operator new(n);
}
void operator delete(void *p) noexcept
{
  std::free(p);
}
void operator delete[](void *p) noexcept
{
  std::free(p);
}
void operator delete(void *p, std::size_t) noexcept
{
  std::free(p);
}
void operator delete[](void *p, std::size_t) noexcept
{
  std::free(p);
}

int main()
{
  using namespace vsr::integration;
  using namespace operations;
  int failures = 0;
  int stranded = 0;
  for (int stage = 0; stage != 3; ++stage) {
    // Includes both shared controls, both std::function targets and the other
    // ordinary setup allocations. Arm at the engine clock, not queue setup.
    for (int allocation = 0; allocation != 32; ++allocation) {
      vsr::scene::Scene scene;
      Integration integration;
      SceneRegistration registration;
      registration.name = "private allocation control";
      registration.scene = &scene;
      registration.normalSyncHook = [](SceneId) { return true; };
      registration.grants = {true, true};
      const auto id = integration.registerScene(registration);
      if (!id || !integration.activate().ok())
        return 1;
      auto authority = session::SessionAuthority::create(integration);
      bool arm = false;
      Limits limits;
      limits.sceneSlots = 1;
      auto engine =
          OperationEngine::create(integration, *authority, limits, [&] {
            if (arm) {
              countdown = allocation;
              arm = false;
            }
            return 1000.;
          });
      identity::IdentityAuthority ids;
      identity::SceneIdentity identity(ids, scene);
      const vsr::math::float3 rgb[] = {{8, 4, 2}};
      content::PreparedHdriPublication prepared(
          identity, {}, "HDRI", 1, 1, rgb);
      edit::RestorationStore history;
      edit::RestorationRecord old;
      old.ref = {"launch", "old"};
      old.image = std::make_shared<edit::ImageSnapshot>();
      history.prepareImage(std::move(old));
      history.publishImage({"launch", "old"});
      edit::RestorationRecord addition;
      addition.ref = {"launch", "new"};
      addition.image = std::make_shared<edit::ImageSnapshot>();
      history.prepareImage(std::move(addition));
      bool committed = false;
      bool pending = false;
      bool cleanupDone = false;
      std::atomic<bool> releaseWorker{false};
      std::future<void> worker;
      std::function<void()> wake;
      Request request;
      request.kind = "create_hdri";
      request.target = session::Target::scene(*id);
      request.capability = session::Capability::EDIT;
      request.resourceLease = std::make_shared<int>(1);
      request.executor = [&](StepContext &ctx) {
        Step step;
        if (stage == 2 && !pending) {
          pending = true;
          wake = ctx.wake;
          worker = std::async(std::launch::async, [&, guard = ctx.isAbandoned] {
            while (!releaseWorker.load())
              std::this_thread::yield();
            (void)guard();
          });
          step.status = Step::Status::BACKEND_PENDING;
          return step;
        }
        if (stage == 2
            && worker.wait_for(std::chrono::seconds(0))
                != std::future_status::ready) {
          step.status = Step::Status::BACKEND_PENDING;
          return step;
        }
        if (ctx.abandoned) {
          cleanupDone = true;
          return step;
        }
        if (!committed) {
          ctx.prepareCommit("prepared HDRI receipt", 128);
          if (!prepared.commit())
            std::abort();
          committed = true;
          ctx.markCommitted();
          history.publishImage({"launch", "new"});
          step.committed = true;
          step.status = Step::Status::SYNCHRONIZING;
        } else {
          step.committed = true;
          step.result = "prepared HDRI receipt";
        }
        return step;
      };
      const auto admitted = engine->submitLocal(std::move(request));
      if (admitted.code != Outcome::OK)
        return 2;
      if (stage != 0) {
        integration.service();
        if (stage == 2)
          wake();
      }
      injected = false;
      arm = true;
      integration.service();
      countdown = -1;
      arm = false;
      const bool failed = injected;
      if (stage == 2) {
        const auto held = engine->statusLocal(admitted.ref);
        const bool heldUntilExit = held.heldSlots == 1;
        releaseWorker.store(true);
        worker.wait();
        if (!heldUntilExit)
          return 7;
      }
      for (int attempt = 0; attempt != 4; ++attempt) {
        if (wake)
          wake();
        integration.service();
      }
      const auto status = engine->statusLocal(admitted.ref);
      if (!status.terminal || status.heldSlots != 0) {
        std::cerr << "stranded stage=" << stage << " allocation=" << allocation
                  << '\n';
        ++stranded;
        continue;
      }
      if (failed) {
        ++failures;
        std::cout << "injected stage=" << stage << " allocation=" << allocation
                  << " committed=" << committed
                  << " terminal=" << outcomeName(*status.terminal)
                  << " held=" << status.heldSlots << '\n';
        if (status.committed != committed
            || (committed
                && (*status.terminal != Outcome::APPLIED_WITH_SYNC_FAILURE
                    || status.result != "prepared HDRI receipt"))
            || (!committed && scene.numberOfObjects(ANARI_LIGHT) != 0)
            || !history.find({"launch", "old"})
            || (committed && !history.find({"launch", "new"}))
            || (stage == 2 && !committed && !cleanupDone)) {
          std::cerr << "untruthful stage=" << stage
                    << " allocation=" << allocation << '\n';
          return 4;
        }
      }
      Request next;
      next.kind = "probe";
      next.target = session::Target::scene(*id);
      next.executor = [](StepContext &) { return Step{}; };
      if (engine->submitLocal(std::move(next)).code != Outcome::OK)
        return 5;
      integration.service();
    }
  }
  std::cout << "HDRI engine ordinary allocation failures: " << failures
            << "; first, postcommit and pending-worker slots released\n";
  return stranded ? 3 : failures < 12 ? 6 : 0;
}

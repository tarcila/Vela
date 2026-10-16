// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_integration
#include "vsr/integration/content/ContentCreation.hpp"
#include "vsr/integration/restore/BatchRestore.hpp"
#include "vsr/rendering/view/FrameObservation.h"
// std
#include <unistd.h>
#include <cstdlib>
#include <fstream>
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
void *operator new[](std::size_t size)
{
  return ::operator new(size);
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

// Arbitrary operation owners may reenter or throw during failure delivery.
// They must observe an already terminal record, once, with a reusable slot.
static bool failureDelivery(bool queueRefusal)
{
  using namespace vsr::integration;
  using namespace operations;
  vsr::scene::Scene scene;
  Integration integration(1);
  SceneRegistration registration;
  registration.name = "private failure-delivery control";
  registration.scene = &scene;
  registration.grants = {true, true};
  registration.normalSyncHook = [](SceneId) { return true; };
  const auto id = integration.registerScene(registration);
  if (!id || !integration.activate().ok())
    return false;
  auto authority = session::SessionAuthority::create(integration);
  bool arm = false;
  Limits limits;
  limits.sceneSlots = 1;
  auto engine = OperationEngine::create(integration, *authority, limits, [&] {
    if (arm) {
      countdown = 0;
      arm = false;
    }
    return 1000.;
  });
  LocalRef ref;
  int deliveries = 0;
  bool observedTerminal = false;
  Request request;
  request.kind = "failure delivery";
  request.target = session::Target::scene(*id);
  request.onSynchronizationFailure = [&] {
    ++deliveries;
    const auto status = engine->statusLocal(ref);
    observedTerminal = status.terminal == Outcome::APPLIED_WITH_SYNC_FAILURE
        && status.committed && status.heldSlots == 0;
    throw std::bad_alloc();
  };
  bool committed = false;
  request.executor = [&](StepContext &ctx) {
    Step step;
    if (!committed) {
      ctx.prepareCommit("committed receipt", 64);
      ctx.markCommitted();
      committed = true;
      if (queueRefusal)
        integration.submit(*id, [](vsr::scene::Scene &) {});
      step.committed = true;
      step.status = Step::Status::SYNCHRONIZING;
    }
    return step;
  };
  const auto admitted = engine->submitLocal(std::move(request));
  if (admitted.code != Outcome::OK)
    return false;
  ref = admitted.ref;
  integration.service();
  arm = !queueRefusal;
  integration.service();
  countdown = -1;
  arm = false;
  integration.service();
  const auto status = engine->statusLocal(ref);
  return observedTerminal && deliveries == 1
      && status.result == "committed receipt";
}

static bool editFailureControl(int mode)
{
  using namespace vsr::integration;
  using namespace operations;
  vsr::scene::Scene scene;
  auto material = scene.createObject<vsr::scene::Material>("matte");
  material->setParameter("opacity", 1.f);
  Integration integration;
  bool lateFailure = false;
  SceneRegistration registration;
  registration.name = "private ordinary edit synchronization";
  registration.scene = &scene;
  registration.grants = {true, true};
  registration.normalSyncHook = [&](SceneId) {
    lateFailure = mode != 0;
    if (mode == 2)
      throw std::bad_alloc();
    return true;
  };
  const auto id = integration.registerScene(registration);
  if (!id || !integration.activate().ok())
    return false;
  auto authority = session::SessionAuthority::create(integration);
  bool arm = false;
  auto engine = OperationEngine::create(integration, *authority, {}, [&] {
    if (lateFailure) {
      lateFailure = false;
      throw std::bad_alloc();
    }
    if (arm) {
      arm = false;
      countdown = 0;
    }
    return 1000.;
  });
  identity::IdentityAuthority ids;
  identity::SceneIdentity identity(ids, scene);
  edit::EditService edits(integration, *engine, authority->launchId());
  edits.attachScene(*id, identity);
  const auto version =
      identity.parameterVersion(identity.handleOf(material.data()));
  if (!version)
    return false;
  edit::EditBatch batch{
      *id, {{*version, "opacity", JsonValue::fromNumber(.5)}}};
  const auto admitted = edits.submitLocal(std::move(batch));
  if (admitted.code != Outcome::OK)
    return false;
  integration.service();
  edits.syncStates().beginHook(*id);
  arm = mode == 0;
  integration.service();
  countdown = -1;
  arm = false;
  const auto status = engine->statusLocal(admitted.ref);
  const auto result = edits.resultLocal(admitted.ref);
  if (status.terminal != Outcome::APPLIED_WITH_SYNC_FAILURE || status.heldSlots
      || !status.committed || !result || !result->committed
      || result->outcome != Outcome::APPLIED_WITH_SYNC_FAILURE
      || edits.syncState(*id) != sync::SyncState::FAILED)
    return false;
  edits.syncStates().finishRecovery(*id, true, {});
  if (edits.syncState(*id) != sync::SyncState::SYNCHRONIZING)
    return false;
  return edits.syncStates().finishHook(*id, true)
      && edits.syncState(*id) == sync::SyncState::READY;
}

int main()
{
  using namespace vsr::integration;
  using namespace operations;
  int faults = 0;
  for (int mode = 0; mode != 8; ++mode) {
    for (int allocation = 0; allocation != (mode == 0 ? 40 : 1); ++allocation) {
      char directory[] = "/tmp/vhs-XXXXXX";
      if (!mkdtemp(directory))
        return 1;
      const std::string file = std::string(directory) + "/input.hdr";
      {
        std::ofstream out(file, std::ios::binary);
        out << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n";
        const unsigned char pixel[] = {128, 64, 32, 132};
        out.write(reinterpret_cast<const char *>(pixel), 4);
      }
      vsr::scene::Scene scene;
      Integration integration(1);
      SceneRegistration registration;
      registration.name = "private production synchronization fault";
      registration.scene = &scene;
      registration.grants = {true, true, true};
      registration.serializedArrayWrites = true;
      registration.recoveryCallback = [](SceneId) { return true; };
      int hooks = 0;
      bool lateFailure = false;
      registration.normalSyncHook = [&](SceneId) {
        ++hooks;
        // Force a terminal setup failure after the normal hook ran (or
        // threw), so the failure callback must leave other hooks alone.
        lateFailure = mode == 6 || mode == 7;
        if (mode == 3 || mode == 5 || mode == 7)
          throw std::bad_alloc();
        return true;
      };
      const auto id = integration.registerScene(registration);
      if (!id)
        return 2;
      const auto view =
          integration.registerView({"private render guard", *id, {true, true}});
      if (!view || !integration.activate().ok())
        return 2;
      auto authority = session::SessionAuthority::create(integration);
      bool arm = false;
      Limits limits;
      limits.sceneSlots = 1;
      auto engine =
          OperationEngine::create(integration, *authority, limits, [&] {
            if (lateFailure) {
              lateFailure = false;
              throw std::bad_alloc();
            }
            if (arm) {
              countdown = allocation;
              arm = false;
            }
            return 1000.;
          });
      identity::IdentityAuthority ids;
      identity::SceneIdentity identity(ids, scene);
      edit::EditService edits(integration, *engine, authority->launchId());
      edits.attachScene(*id, identity);
      activity::ActivityLog activity;
      activity::EngineActivitySink sink(activity, integration);
      engine->setActivitySink(&sink);
      auto budget = std::make_shared<vsr::rendering::FrameBudget>(512u << 20);
      content::ContentCreation creator(
          integration, *engine, edits, activity, Reservation::from(budget));
      creator.attachScene(*id, identity);
      const auto root = creator.assetRoots().approve(directory);
      if (!root)
        return 3;
      const auto attachment = authority->attach(authority->launchCredential());
      session::Credentials credentials{authority->launchCredential(),
          attachment.sessionId,
          attachment.sessionSecret};
      const OperationRef ref{attachment.sessionId, 1};
      content::HdriRequest request;
      request.scene = *id;
      request.assetRoot = *root;
      request.fileName = "input.hdr";
      // Queue refusal happens after the real creator begins its hook. The
      // listener also reenters the engine on failure: delivery must not lock
      // it.
      edits.syncStates().setTransitionListener(
          [&](SceneId, sync::SyncState, sync::SyncState to) {
            if (mode == 1 && to == sync::SyncState::SYNCHRONIZING)
              integration.submit(*id, [](vsr::scene::Scene &) {});
            if (to == sync::SyncState::FAILED)
              (void)engine->status(credentials, ref);
          });
      if (creator.submit(credentials, ref, request).code != Outcome::OK)
        return 4;
      Status status;
      for (int attempt = 0; attempt != 10000; ++attempt) {
        creator.pump();
        integration.service();
        status = engine->status(credentials, ref);
        if (status.committed || status.terminal)
          break;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
      if (!status.committed)
        return 5;
      const bool unrelated =
          mode == 2 || mode == 4 || mode == 5 || mode == 6 || mode == 7;
      if (unrelated)
        edits.syncStates().beginHook(*id);
      injected = false;
      arm = mode == 0 || mode == 2;
      integration.service();
      countdown = -1;
      arm = false;
      const bool fault = injected;
      status = engine->status(credentials, ref);
      const bool failed = fault || mode == 1 || mode == 3 || mode == 5
          || mode == 6 || mode == 7;
      if (!status.terminal || status.heldSlots || !status.committed
          || (failed && *status.terminal != Outcome::APPLIED_WITH_SYNC_FAILURE)
          || (failed && edits.syncState(*id) != sync::SyncState::FAILED)) {
        std::cerr << "production hook stranded mode=" << mode
                  << " allocation=" << allocation
                  << " state=" << sync::syncStateName(edits.syncState(*id))
                  << " terminal="
                  << (status.terminal ? outcomeName(*status.terminal)
                                      : "pending")
                  << " held=" << status.heldSlots << '\n';
        return 6;
      }
      mcp::json::Value receipt;
      if (!mcp::json::parse(status.result, receipt) || !receipt.get("primary")
          || !receipt.get("batch_ref"))
        return 7;
      edit::BatchRef batch{receipt.get("batch_ref")->get("launch_id")->s,
          receipt.get("batch_ref")->get("batch_id")->s};
      const auto committedReceipt = status.result;
      const auto replay = creator.submit(credentials, ref, request);
      if (!edits.restorations().find(batch)
          || activity.eligibility(batch) != activity::Eligibility::ELIGIBLE
          || replay.code != Outcome::OK || !replay.replayed
          || replay.status.result != committedReceipt
          || scene.numberOfObjects(ANARI_LIGHT) != 1)
        return 8;
      // Explicit successful recovery must reveal any remaining normal hook.
      edits.syncStates().setTransitionListener({});
      sync::SyncService recovery(integration, *engine, edits.syncStates());
      const auto recovered = recovery.submitLocalResynchronize(*id);
      if (recovered.code != Outcome::OK)
        return 9;
      integration.service();
      if (!recovery.resultLocal(recovered.ref)
          || !recovery.resultLocal(recovered.ref)->ready)
        return 9;
      if (edits.syncState(*id)
          != (unrelated ? sync::SyncState::SYNCHRONIZING
                        : sync::SyncState::READY))
        return 9;
      if (unrelated && !edits.syncStates().finishHook(*id, true))
        return 10;
      if (!recovery.viewReady(*view))
        return 15;
      // Recovery restores render admission; ordinary exact Restore consumes
      // the preserved production record, rather than deleting a fake graph.
      restore::RestoreService restores(*engine,
          edits,
          ResultOwnership::Reserve(Reservation::from(budget), 65536));
      restores.attachScene(*id, identity);
      const auto restored = restores.submitLocal({*id, batch});
      if (restored.code != Outcome::OK)
        return 13;
      integration.service();
      integration.service();
      if (scene.numberOfObjects(ANARI_LIGHT) != 0
          || activity.eligibility(batch) != activity::Eligibility::RESTORED) {
        const auto result = engine->statusLocal(restored.ref);
        std::cerr << "restore mode=" << mode << " allocation=" << allocation
                  << " result=" << result.result << " outcome="
                  << (result.terminal ? outcomeName(*result.terminal)
                                      : "pending")
                  << '\n';
        return 14;
      }
      Request next;
      next.kind = "slot reuse";
      next.target = session::Target::scene(*id);
      next.executor = [](StepContext &) { return Step{}; };
      if (engine->submitLocal(std::move(next)).code != Outcome::OK)
        return 11;
      integration.service();
      if (fault)
        ++faults;
      std::cout << "production mode=" << mode << " allocation=" << allocation
                << " malloc_fault=" << fault
                << " committed_terminal=" << outcomeName(*status.terminal)
                << " exact_restore=true recovery_render_guard=true\n";
      std::remove(file.c_str());
      rmdir(directory);
    }
  }
  if (!failureDelivery(false) || !failureDelivery(true))
    return 16;
  for (int mode = 0; mode != 3; ++mode)
    if (!editFailureControl(mode))
      return 17;
  std::cout
      << "Real ContentCreation synchronization: " << faults
      << " ordinary allocation faults; queue refusal, normal and throwing hooks pass\n";
  return faults < 12 ? 12 : 0;
}

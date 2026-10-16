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

// Real public creation followed by the ordinary Restore driver. Only the
// servicing thread's next context setup is faulted, never the decoder worker.
static int restoreControl(int mode, int allocation, bool scalar, int &faults)
{
  using namespace vsr::integration;
  using namespace operations;
  char directory[] = "/tmp/vrs-XXXXXX";
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
  auto material = scene.createObject<vsr::scene::Material>("matte");
  material->setParameter("opacity", 1.f);
  Integration integration(1);
  bool restoring = false;
  bool lateFailure = false;
  int hooks = 0;
  SceneRegistration registration;
  registration.name = "private ordinary Restore fault";
  registration.scene = &scene;
  registration.grants = {true, true, true};
  registration.serializedArrayWrites = true;
  registration.recoveryCallback = [](SceneId) { return true; };
  registration.normalSyncHook = [&](SceneId) {
    if (restoring) {
      ++hooks;
      lateFailure = mode == 5 || mode == 6;
      if (mode == 4 || mode == 6)
        throw std::bad_alloc();
    }
    return true;
  };
  const auto id = integration.registerScene(registration);
  if (!id)
    return 2;
  const auto view =
      integration.registerView({"render guard", *id, {true, true}});
  if (!view || !integration.activate().ok())
    return 2;
  auto authority = session::SessionAuthority::create(integration);
  bool arm = false;
  Limits limits;
  limits.sceneSlots = 1;
  auto engine = OperationEngine::create(integration, *authority, limits, [&] {
    if (lateFailure) {
      lateFailure = false;
      throw std::bad_alloc();
    }
    if (arm) {
      arm = false;
      countdown = allocation;
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
  const OperationRef creationRef{attachment.sessionId, 1};
  if (scalar) {
    const auto version =
        identity.parameterVersion(identity.handleOf(material.data()));
    edit::EditBatch batch{
        *id, {{*version, "opacity", JsonValue::fromNumber(.5)}}};
    if (edits.submit(credentials, creationRef, std::move(batch)).code
        != Outcome::OK)
      return 4;
  } else {
    content::HdriRequest request;
    request.scene = *id;
    request.assetRoot = *root;
    request.fileName = "input.hdr";
    if (creator.submit(credentials, creationRef, request).code != Outcome::OK)
      return 4;
  }
  Status status;
  for (int attempt = 0; attempt != 10000; ++attempt) {
    creator.pump();
    integration.service();
    status = engine->status(credentials, creationRef);
    if (status.terminal)
      break;
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  mcp::json::Value receipt;
  if (status.terminal != Outcome::OK
      || !mcp::json::parse(status.result, receipt) || !receipt.get("batch_ref"))
    return 5;
  edit::BatchRef batch{receipt.get("batch_ref")->get("launch_id")->s,
      receipt.get("batch_ref")->get("batch_id")->s};
  restore::RestoreService restores(*engine,
      edits,
      ResultOwnership::Reserve(Reservation::from(budget), 65536));
  restores.attachScene(*id, identity);
  const OperationRef ref{attachment.sessionId, 2};
  bool reentered = false;
  edits.syncStates().setTransitionListener(
      [&](SceneId, sync::SyncState, sync::SyncState to) {
        if (mode == 1 && to == sync::SyncState::SYNCHRONIZING)
          integration.submit(*id, [](vsr::scene::Scene &) {});
        if (to == sync::SyncState::FAILED) {
          const auto terminal = engine->status(credentials, ref);
          // A hook itself may fail before engine terminalization. Queue/setup
          // failure delivery, however, must run outside the engine mutex.
          reentered = reentered || terminal.terminal.has_value();
          throw std::bad_alloc();
        }
      });
  restoring = true;
  const restore::RestoreRequest request{*id, batch};
  if (restores.submit(credentials, ref, request).code != Outcome::OK)
    return 6;
  integration.service();
  const bool unrelated = mode >= 2;
  if (unrelated)
    edits.syncStates().beginHook(*id);
  injected = false;
  arm = mode == 0 || mode == 2;
  integration.service();
  countdown = -1;
  arm = false;
  const bool fault = injected;
  status = engine->status(credentials, ref);
  const bool failed = fault || mode == 1 || mode == 4 || mode == 5 || mode == 6;
  if (!status.terminal || !status.committed || status.heldSlots
      || (failed && status.terminal != Outcome::APPLIED_WITH_SYNC_FAILURE)
      || (failed && edits.syncState(*id) != sync::SyncState::FAILED)) {
    std::cerr << "Restore hook stranded scalar=" << scalar << " mode=" << mode
              << " allocation=" << allocation
              << " state=" << sync::syncStateName(edits.syncState(*id))
              << " held=" << status.heldSlots << '\n';
    return 7;
  }
  const auto result = restores.result(ref);
  if (!result || !result->committed || result->outcome != *status.terminal
      || !(result->targetBatchRef == batch)
      || (!scalar
          && (!mcp::json::parse(status.result, receipt)
              || !receipt.get("target_batch_ref") || !receipt.get("restored")
              || !receipt.get("restored")->b
              || receipt.get("target_batch_ref")->get("launch_id")->s
                  != batch.launchId
              || receipt.get("target_batch_ref")->get("batch_id")->s
                  != batch.batchId
              || !receipt.get("removed_content")
              || !receipt.get("removed_content")->b))
      || (scalar ? material->parameterValueAs<float>("opacity") != 1.f
                 : scene.numberOfObjects(ANARI_LIGHT) != 0)
      || scene.numberOfObjects(ANARI_ARRAY) != 0
      || scene.defaultLayer()->size() != 1 || !restores.restored(batch)
      || restores.eligible(batch)
      || activity.eligibility(batch) != activity::Eligibility::RESTORED)
    return 8;
  const auto replay = restores.submit(credentials, ref, request);
  if (replay.code != Outcome::OK || !replay.replayed
      || replay.status.result != status.result
      || replay.status.terminal != status.terminal)
    return 9;
  if ((fault || mode == 1 || mode == 5) && !reentered)
    return 10;
  edits.syncStates().setTransitionListener({});
  sync::SyncService recovery(integration, *engine, edits.syncStates());
  const auto recovered = recovery.submitLocalResynchronize(*id);
  if (recovered.code != Outcome::OK)
    return 11;
  integration.service();
  if (!recovery.resultLocal(recovered.ref)
      || !recovery.resultLocal(recovered.ref)->ready
      || edits.syncState(*id)
          != (unrelated ? sync::SyncState::SYNCHRONIZING
                        : sync::SyncState::READY))
    return 12;
  if (unrelated && !edits.syncStates().finishHook(*id, true))
    return 13;
  if (!recovery.viewReady(*view))
    return 14;
  restoring = false;
  const auto consumed = restores.submitLocal(request);
  if (consumed.code != Outcome::OK)
    return 15;
  integration.service();
  if (engine->statusLocal(consumed.ref).terminal != Outcome::ALREADY_RESTORED)
    return 16;
  if (fault)
    ++faults;
  std::cout
      << "Restore scalar=" << scalar << " mode=" << mode
      << " allocation=" << allocation << " malloc_fault=" << fault
      << " terminal=" << outcomeName(*status.terminal)
      << " graph_absent=true consumed=true replay=true recovery_render_guard=true\n";
  std::remove(file.c_str());
  rmdir(directory);
  return 0;
}

int main()
{
  int faults = 0;
  for (bool scalar : {false, true})
    for (int mode = 0; mode != 7; ++mode)
      for (int allocation = 0; allocation != (mode == 0 ? 40 : 1); ++allocation)
        if (const auto result =
                restoreControl(mode, allocation, scalar, faults)) {
          std::cerr << "control exit=" << result << '\n';
          return result;
        }
  std::cout << "Real ordinary Restore: " << faults
            << " ordinary allocation faults\n";
  return faults < 12 ? 17 : 0;
}

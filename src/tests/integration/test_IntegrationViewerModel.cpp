// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// Smoke test of the ImGui-free view-model behind vsrViewer's activity window
// (ticket 17): edit-disable versus disconnect, restoration after the client
// exits, and synchronization recovery. Business rules live in the other
// integration suites; this only checks the viewer forwards and reports them.

// catch
#include "catch.hpp"
// vsr
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/activity/Activity.hpp"
#include "vsr/integration/edit/AgentEdit.hpp"
#include "vsr/integration/restore/BatchRestore.hpp"
#include "vsr/integration/sync/SceneSync.hpp"
#include "vsr/scene/Scene.hpp"
#include "vsr/ui/imgui/windows/IntegrationActivityModel.h"
// std
#include <memory>
#include <string>

using namespace vsr::integration;
using edit::BatchRef;
using edit::EditService;
using operations::Outcome;
using sync::SyncState;
using vsr::ui::imgui::IntegrationActivityModel;
namespace tokens = vsr::scene::tokens;

namespace {

struct Fixture
{
  vsr::scene::Scene scene;
  Integration integration;
  SceneId sceneId{INVALID_SCENE_ID};
  double now{1000.0};
  bool hookOk{true};
  std::unique_ptr<session::SessionAuthority> authority;
  std::unique_ptr<operations::OperationEngine> engine;
  std::unique_ptr<identity::IdentityAuthority> ids;
  std::unique_ptr<identity::SceneIdentity> identity;
  std::unique_ptr<EditService> edits;
  std::unique_ptr<restore::RestoreService> restores;
  std::unique_ptr<sync::SyncService> syncs;
  activity::ActivityLog log;
  std::unique_ptr<IntegrationActivityModel> model;
  operations::Credentials creds;
  vsr::scene::MaterialRef matte;

  Fixture()
  {
    matte = scene.createObject<vsr::scene::Material>(tokens::material::matte);
    matte->setParameter("opacity", 1.f);

    SceneRegistration r;
    r.name = "scene";
    r.scene = &scene;
    r.grants = {true, true, true};
    r.normalSyncHook = [this](SceneId) { return hookOk; };
    r.recoveryCallback = [this](SceneId) { return hookOk; };
    sceneId = *integration.registerScene(r);
    REQUIRE(integration.activate().ok());

    authority =
        session::SessionAuthority::create(integration, [this] { return now; });
    REQUIRE(authority);
    engine = operations::OperationEngine::create(
        integration, *authority, {}, [this] { return now; });
    REQUIRE(engine);
    ids = std::make_unique<identity::IdentityAuthority>();
    identity = std::make_unique<identity::SceneIdentity>(*ids, scene);
    edits = std::make_unique<EditService>(
        integration, *engine, authority->launchId());
    edits->attachScene(sceneId, *identity);
    restores = std::make_unique<restore::RestoreService>(*engine, *edits);
    restores->attachScene(sceneId, *identity);
    syncs = std::make_unique<sync::SyncService>(
        integration, *engine, edits->syncStates());

    vsr::ui::imgui::IntegrationServices s;
    s.integration = &integration;
    s.authority = authority.get();
    s.activity = &log;
    s.sync = syncs.get();
    s.restore = restores.get();
    s.scenes.emplace_back(sceneId, "scene");
    s.descriptorPath = "/run/user/1000/vsr/vsr-mcp-x.json";
    model = std::make_unique<IntegrationActivityModel>(std::move(s));

    auto a = authority->attach(authority->launchCredential());
    REQUIRE(a.code == session::Code::OK);
    creds = {authority->launchCredential(), a.sessionId, a.sessionSecret};
  }

  ~Fixture()
  {
    model.reset();
    syncs.reset();
    restores.reset();
    edits.reset();
    identity.reset();
  }

  void drain()
  {
    while (integration.service() > 0) {
    }
  }

  float opacity()
  {
    return *matte->parameterValueAs<float>("opacity");
  }

  // External edit, recorded in the activity log as ticket 80 will do.
  std::pair<Outcome, std::optional<BatchRef>> agentEdit(
      std::uint64_t id, double v)
  {
    auto token = *identity->parameterVersion(identity->handleOf(matte.data()));
    edit::EditBatch b{
        sceneId, {{token, "opacity", JsonValue::fromNumber(v)}}};
    auto a = edits->submit(creds, {creds.sessionId, id}, b);
    if (a.code != Outcome::OK)
      return {a.code, std::nullopt};
    drain();
    auto r = edits->result({creds.sessionId, id});
    REQUIRE(r);
    if (r->committed) {
      activity::Entry e;
      e.ref = operations::OperationRef{creds.sessionId, id};
      e.kind = activity::OperationKind::EDIT;
      e.targets = {session::Target::scene(sceneId)};
      e.terminal = r->outcome;
      e.committed = true;
      e.batchRef = r->batchRef;
      e.eligibility = activity::Eligibility::ELIGIBLE;
      e.clientSummary = activity::ClientSummary{"dim the material"};
      REQUIRE(log.append(e));
    }
    return {r->outcome, r->batchRef};
  }
};

} // namespace

SCENARIO("Viewer model reports connection and activity", "[integration]")
{
  Fixture fx;
  auto [code, batch] = fx.agentEdit(1, 0.5);
  REQUIRE(code == Outcome::OK);

  const auto &s = fx.model->refresh();
  REQUIRE(s.active);
  REQUIRE(s.sessionOwned);
  REQUIRE(s.descriptorPath == "/run/user/1000/vsr/vsr-mcp-x.json");
  REQUIRE(s.scenes.size() == 1);
  REQUIRE(s.scenes[0].sync == SyncState::READY);
  REQUIRE(s.scenes[0].effective.edit);
  REQUIRE(s.activity.size() == 1);
  REQUIRE(s.activity[0].entry.batchRef == batch);
  REQUIRE(s.activity[0].entry.clientSummary->text == "dim the material");
  REQUIRE(s.activity[0].reference.find("#1") != std::string::npos);
}

SCENARIO("Disable agent edits keeps the session; Disconnect ends it",
    "[integration]")
{
  Fixture fx;
  REQUIRE(fx.model->setAgentEditsDisabled(true));
  auto s = fx.model->refresh();
  REQUIRE(s.agentEditsDisabled);
  REQUIRE(s.sessionOwned);
  REQUIRE_FALSE(s.scenes[0].effective.edit);
  REQUIRE(s.scenes[0].effective.read);
  REQUIRE(fx.agentEdit(1, 0.5).first != Outcome::OK);
  REQUIRE(fx.opacity() == 1.f);
  // Local authority is not the external mask.
  REQUIRE(s.scenes[0].localEdit);

  REQUIRE(fx.model->disconnect());
  s = fx.model->refresh();
  REQUIRE_FALSE(s.sessionOwned);
  REQUIRE(s.agentEditsDisabled); // restriction outlives the session
  REQUIRE_FALSE(fx.model->disconnect());
}

SCENARIO("Local Restore works after the client exits", "[integration]")
{
  Fixture fx;
  auto [code, batch] = fx.agentEdit(1, 0.25);
  REQUIRE(code == Outcome::OK);
  REQUIRE(fx.opacity() == 0.25f);

  REQUIRE(fx.model->disconnect());
  REQUIRE(fx.model->setAgentEditsDisabled(true));
  auto a = fx.model->restore(fx.sceneId, *batch);
  REQUIRE(a.code == Outcome::OK);
  fx.drain();
  auto r = fx.restores->resultLocal(a.ref);
  REQUIRE(r);
  REQUIRE(r->committed);
  REQUIRE(fx.opacity() == 1.f);

  // A second local restore cannot consume the same eligibility again.
  auto b = fx.model->restore(fx.sceneId, *batch);
  if (b.code == Outcome::OK) {
    fx.drain();
    REQUIRE_FALSE(fx.restores->resultLocal(b.ref)->committed);
  }
}

SCENARIO("Resynchronize recovers a failed scene", "[integration]")
{
  Fixture fx;
  fx.hookOk = false;
  auto [code, batch] = fx.agentEdit(1, 0.5);
  REQUIRE(code == Outcome::APPLIED_WITH_SYNC_FAILURE);
  REQUIRE(fx.model->refresh().scenes[0].sync == SyncState::FAILED);

  fx.hookOk = true;
  auto a = fx.model->resynchronize(fx.sceneId);
  REQUIRE(a.code == Outcome::OK);
  fx.drain();
  REQUIRE(fx.syncs->resultLocal(a.ref));
  REQUIRE(fx.model->refresh().scenes[0].sync == SyncState::READY);
}

SCENARIO("Viewer model shows every retained record after an eviction burst",
    "[integration]")
{
  Fixture fx;
  vsr::integration::activity::Limits l;
  l.entries = 8;
  vsr::integration::activity::ActivityLog small(l);
  auto services = fx.model->services();
  services.activity = &small;
  fx.model->setServices(services);

  auto record = [&](std::uint64_t id) {
    activity::Entry e;
    e.ref = operations::OperationRef{fx.creds.sessionId, id};
    e.kind = activity::OperationKind::INSPECTION;
    e.targets = {session::Target::scene(fx.sceneId)};
    e.terminal = Outcome::OK;
    REQUIRE(small.append(e));
  };

  for (std::uint64_t i = 1; i <= 3; ++i)
    record(i);
  REQUIRE(fx.model->refresh().activity.size() == 3);

  // Far more than 32 records evicted between two refreshes.
  for (std::uint64_t i = 4; i <= 100; ++i)
    record(i);
  const auto &s = fx.model->refresh();
  REQUIRE(s.activity.size() == 8);
  for (std::size_t i = 0; i < s.activity.size(); ++i) {
    REQUIRE(s.activity[i].entry.ref->requestId == 93 + i);
    REQUIRE(s.activity[i].reference.find("#" + std::to_string(93 + i))
        != std::string::npos);
  }
  REQUIRE(s.usage.entries == 8);
}

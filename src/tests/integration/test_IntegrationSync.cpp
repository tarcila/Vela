// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/edit/AgentEdit.hpp"
#include "vsr/integration/sync/SceneSync.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

using namespace vsr::integration;
using namespace vsr::integration::sync;
using edit::EditBatch;
using edit::EditService;
using operations::Outcome;
namespace tokens = vsr::scene::tokens;

namespace {

// Scene A with two views, unrelated scene B with one view.
struct Fixture
{
  vsr::scene::Scene sceneA, sceneB;
  Integration integration;
  SceneId a{INVALID_SCENE_ID}, b{INVALID_SCENE_ID};
  ViewId a1{INVALID_VIEW_ID}, a2{INVALID_VIEW_ID}, b1{INVALID_VIEW_ID};
  double now{1000.0};
  int hookCalls{0}, recoveryCalls{0};
  std::function<bool(SceneId)> hook = [](SceneId) { return true; };
  std::function<bool(SceneId)> recovery = [](SceneId) { return true; };
  std::unique_ptr<session::SessionAuthority> authority;
  std::unique_ptr<operations::OperationEngine> engine;
  std::unique_ptr<identity::IdentityAuthority> ids;
  std::unique_ptr<identity::SceneIdentity> identityA;
  std::unique_ptr<EditService> edits;
  std::unique_ptr<SyncService> sync;
  operations::Credentials creds;
  std::string sessionId;
  vsr::scene::MaterialRef matte;
  std::vector<std::tuple<SceneId, SyncState, SyncState>> transitions;

  explicit Fixture(operations::Limits limits = {})
  {
    matte = sceneA.createObject<vsr::scene::Material>(tokens::material::matte);
    matte->setParameter("opacity", 1.f);

    for (auto *s : {&sceneA, &sceneB}) {
      SceneRegistration r;
      r.name = s == &sceneA ? "a" : "b";
      r.scene = s;
      r.grants = {true, true, true};
      r.normalSyncHook = [this](SceneId id) {
        ++hookCalls;
        return hook(id);
      };
      r.recoveryCallback = [this](SceneId id) {
        ++recoveryCalls;
        return recovery(id);
      };
      (s == &sceneA ? a : b) = *integration.registerScene(r);
    }
    a1 = *integration.registerView(view("a1", a));
    a2 = *integration.registerView(view("a2", a));
    b1 = *integration.registerView(view("b1", b));
    REQUIRE(integration.activate().ok());

    authority =
        session::SessionAuthority::create(integration, [this] { return now; });
    REQUIRE(authority);
    engine = operations::OperationEngine::create(
        integration, *authority, limits, [this] { return now; });
    REQUIRE(engine);
    ids = std::make_unique<identity::IdentityAuthority>();
    identityA = std::make_unique<identity::SceneIdentity>(*ids, sceneA);
    edits = std::make_unique<EditService>(
        integration, *engine, authority->launchId());
    edits->attachScene(a, *identityA);
    sync = std::make_unique<SyncService>(
        integration, *engine, edits->syncStates());
    sync->states().setTransitionListener(
        [this](SceneId s, SyncState from, SyncState to) {
          transitions.emplace_back(s, from, to);
        });

    auto at = authority->attach(authority->launchCredential());
    REQUIRE(at.code == session::Code::OK);
    sessionId = at.sessionId;
    creds = {authority->launchCredential(), at.sessionId, at.sessionSecret};
  }

  ~Fixture()
  {
    sync.reset();
    edits.reset();
    identityA.reset();
  }

  static ViewRegistration view(std::string name, SceneId scene)
  {
    ViewRegistration v;
    v.name = std::move(name);
    v.scene = scene;
    v.grants = {true, true};
    return v;
  }

  operations::OperationRef ref(std::uint64_t id)
  {
    return {sessionId, id};
  }

  void drain()
  {
    while (integration.service() > 0) {
    }
  }

  EditBatch opacity(double v)
  {
    auto token =
        *identityA->parameterVersion(identityA->handleOf(matte.data()));
    return {a, {{token, "opacity", JsonValue::fromNumber(v)}}};
  }

  edit::EditResult runEdit(std::uint64_t id, double v)
  {
    REQUIRE(edits->submit(creds, ref(id), opacity(v)).code == Outcome::OK);
    drain();
    return *edits->result(ref(id));
  }

  ResyncResult resync(std::uint64_t id, SceneId scene)
  {
    REQUIRE(
        sync->submitResynchronize(creds, ref(id), scene).code == Outcome::OK);
    drain();
    auto r = sync->result(ref(id));
    REQUIRE(r);
    return *r;
  }

  void failSceneA(std::uint64_t id)
  {
    hook = [](SceneId) { return false; };
    REQUIRE(runEdit(id, 0.25).outcome == Outcome::APPLIED_WITH_SYNC_FAILURE);
    hook = [](SceneId) { return true; };
  }
};

} // namespace

TEST_CASE("Scene synchronization states use the frozen names", "[integration]")
{
  REQUIRE(std::string(syncStateName(SyncState::READY)) == "ready");
  REQUIRE(std::string(syncStateName(SyncState::NEEDS_SYNC)) == "needs_sync");
  REQUIRE(
      std::string(syncStateName(SyncState::SYNCHRONIZING)) == "synchronizing");
  REQUIRE(std::string(syncStateName(SyncState::FAILED)) == "failed");
}

SCENARIO("A failure blocks every view of its scene, not unrelated scenes",
    "[integration]")
{
  Fixture fx;
  REQUIRE(fx.sync->viewReady(fx.a1));
  REQUIRE(fx.sync->viewReady(fx.a2));
  fx.failSceneA(1);

  REQUIRE(fx.sync->state(fx.a) == SyncState::FAILED);
  REQUIRE_FALSE(fx.sync->viewReady(fx.a1));
  REQUIRE_FALSE(fx.sync->viewReady(fx.a2)); // healthy sibling still blocked
  REQUIRE(fx.sync->state(fx.b) == SyncState::READY);
  REQUIRE(fx.sync->viewReady(fx.b1));
}

SCENARIO("Resynchronize recovers a failed scene without replaying edits",
    "[integration]")
{
  Fixture fx;
  fx.failSceneA(1);
  const int hooksBefore = fx.hookCalls;
  const float value = *fx.matte->parameterValueAs<float>("opacity");
  fx.transitions.clear();

  std::optional<SyncState> during;
  fx.recovery = [&](SceneId s) {
    during = fx.sync->state(s);
    return true;
  };
  auto r = fx.resync(2, fx.a);

  REQUIRE(fx.recoveryCalls == 1);
  REQUIRE(during == SyncState::SYNCHRONIZING);
  REQUIRE(r.ready);
  REQUIRE(r.state == SyncState::READY);
  REQUIRE(r.views == std::vector<ViewId>{fx.a1, fx.a2});
  REQUIRE(fx.sync->viewReady(fx.a1));
  REQUIRE(fx.sync->viewReady(fx.a2));
  // No edit replay: the normal hook did not run, values are untouched.
  REQUIRE(fx.hookCalls == hooksBefore);
  REQUIRE(*fx.matte->parameterValueAs<float>("opacity") == value);
  // Transitions reported.
  REQUIRE(fx.transitions.size() == 2);
  REQUIRE(fx.transitions[0]
      == std::make_tuple(fx.a, SyncState::FAILED, SyncState::SYNCHRONIZING));
  REQUIRE(fx.transitions[1]
      == std::make_tuple(fx.a, SyncState::SYNCHRONIZING, SyncState::READY));
  auto status = fx.engine->status(fx.creds, fx.ref(2));
  REQUIRE(status.kind == "resynchronize");
  REQUIRE(status.terminal == Outcome::OK);
  REQUIRE(status.result.find("\"state\":\"ready\"") != std::string::npos);

  // Earlier edit result is unchanged.
  REQUIRE(fx.edits->result(fx.ref(1))->outcome
      == Outcome::APPLIED_WITH_SYNC_FAILURE);
}

SCENARIO(
    "A failed Resynchronize reports failure and never retries", "[integration]")
{
  for (int mode = 0; mode < 2; mode++) {
    Fixture fx;
    fx.failSceneA(1);
    if (mode == 0)
      fx.recovery = [](SceneId) { return false; };
    else
      fx.recovery = [](SceneId) -> bool { throw 1; };
    auto r = fx.resync(2, fx.a);
    REQUIRE_FALSE(r.ready);
    REQUIRE(r.state == SyncState::FAILED);
    REQUIRE(fx.recoveryCalls == 1);
    // No automatic retry at later safe points.
    fx.drain();
    REQUIRE(fx.integration.service() == 0);
    REQUIRE(fx.recoveryCalls == 1);
    REQUIRE_FALSE(fx.sync->viewReady(fx.a1));
    auto status = fx.engine->status(fx.creds, fx.ref(2));
    REQUIRE(status.result.find("\"ready\":false") != std::string::npos);
  }
}

SCENARIO("Resynchronize requires read and resynchronize on the scene",
    "[integration]")
{
  for (auto cap :
      {session::Capability::READ, session::Capability::RESYNCHRONIZE}) {
    Fixture fx;
    fx.failSceneA(1);
    fx.authority->restrict(session::Target::scene(fx.a), cap);
    auto adm = fx.sync->submitResynchronize(fx.creds, fx.ref(2), fx.a);
    REQUIRE(adm.code == Outcome::FORBIDDEN);
    fx.drain();
    REQUIRE(fx.recoveryCalls == 0);
    REQUIRE(fx.sync->state(fx.a) == SyncState::FAILED);
  }
}

SCENARIO("Resynchronize consumes a scene-work slot", "[integration]")
{
  operations::Limits limits;
  limits.sceneSlots = 1;
  Fixture fx(limits);
  REQUIRE(fx.sync->submitResynchronize(fx.creds, fx.ref(1), fx.a).code
      == Outcome::OK);
  REQUIRE(fx.engine->slotsInUse() == 1);
  REQUIRE(fx.sync->submitLocalResynchronize(fx.b).code == Outcome::BUSY);
  REQUIRE(fx.sync->submitResynchronize(fx.creds, fx.ref(2), fx.b).code
      == Outcome::BUSY);
  fx.drain();
  REQUIRE(fx.engine->slotsInUse() == 0);
  auto local = fx.sync->submitLocalResynchronize(fx.b);
  REQUIRE(local.code == Outcome::OK);
  fx.drain();
  REQUIRE(fx.sync->resultLocal(local.ref)->ready);
}

SCENARIO(
    "Inspection and authorized edits continue while not ready", "[integration]")
{
  Fixture fx;
  fx.failSceneA(1);
  auto r = fx.runEdit(2, 0.75);
  REQUIRE(r.committed);
  REQUIRE(r.outcome == Outcome::APPLIED_WITH_SYNC_FAILURE);
  REQUIRE(*fx.matte->parameterValueAs<float>("opacity") == 0.75f);
  REQUIRE(fx.sync->state(fx.a) == SyncState::FAILED);
  // Read access is not gated on readiness.
  REQUIRE(fx.authority->authorize(
              fx.creds, session::Target::scene(fx.a), session::Capability::READ)
      == session::Code::OK);

  fx.resync(3, fx.a);
  REQUIRE(fx.runEdit(4, 0.5).outcome == Outcome::OK);
  REQUIRE(fx.sync->state(fx.a) == SyncState::READY);
}

SCENARIO("View registration during a hook waits for a later safe point",
    "[integration]")
{
  Fixture fx;
  std::optional<ViewId> added;
  bool unregistered = false;
  std::vector<ViewId> viewsInHook;
  fx.hook = [&](SceneId s) {
    viewsInHook = fx.integration.viewsOfScene(s);
    REQUIRE(fx.sync->requestRegisterView(
        Fixture::view("a3", fx.a),
        [](ViewId) { return true; },
        UnconfirmedView::FAIL_REGISTRATION,
        [&](std::optional<ViewId> id) { added = id; }));
    REQUIRE(fx.sync->requestUnregisterView(
        fx.a2, [&](bool ok) { unregistered = ok; }));
    return true;
  };
  REQUIRE(fx.edits->submit(fx.creds, fx.ref(1), fx.opacity(0.5)).code
      == Outcome::OK);
  REQUIRE(fx.integration.service() == 1); // commit
  REQUIRE(fx.integration.service() == 1); // hook
  REQUIRE(fx.hookCalls == 1);
  REQUIRE(viewsInHook == std::vector<ViewId>{fx.a1, fx.a2});
  REQUIRE_FALSE(added);
  REQUIRE_FALSE(unregistered);
  REQUIRE(fx.integration.isRegisteredView(fx.a2));

  fx.drain(); // later safe point
  REQUIRE(added);
  REQUIRE(unregistered);
  REQUIRE_FALSE(fx.integration.isRegisteredView(fx.a2));
  REQUIRE(
      fx.integration.viewsOfScene(fx.a) == std::vector<ViewId>{fx.a1, *added});
  REQUIRE(fx.sync->viewReady(*added));
}

SCENARIO(
    "View registration from outside a safe point is refused", "[integration]")
{
  Fixture fx;
  REQUIRE_FALSE(fx.integration.registerView(Fixture::view("x", fx.a)));
  REQUIRE_FALSE(fx.integration.unregisterView(fx.a1));
}

SCENARIO("A new view is usable only after host confirmation", "[integration]")
{
  GIVEN("confirmation that succeeds")
  {
    Fixture fx;
    std::optional<bool> readyDuringConfirm;
    std::optional<ViewId> added;
    REQUIRE(fx.sync->requestRegisterView(
        Fixture::view("a3", fx.a),
        [&](ViewId v) {
          readyDuringConfirm = fx.sync->viewReady(v);
          return true;
        },
        UnconfirmedView::FAIL_REGISTRATION,
        [&](std::optional<ViewId> id) { added = id; }));
    fx.drain();
    REQUIRE(readyDuringConfirm == false);
    REQUIRE(added);
    REQUIRE(fx.sync->viewReady(*added));
    REQUIRE(fx.sync->state(fx.a) == SyncState::READY);
  }
  GIVEN("unconfirmable registration that fails")
  {
    Fixture fx;
    std::optional<ViewId> added{0};
    REQUIRE(fx.sync->requestRegisterView(
        Fixture::view("a3", fx.a),
        [](ViewId) { return false; },
        UnconfirmedView::FAIL_REGISTRATION,
        [&](std::optional<ViewId> id) { added = id; }));
    fx.drain();
    REQUIRE_FALSE(added);
    REQUIRE(
        fx.integration.viewsOfScene(fx.a) == std::vector<ViewId>{fx.a1, fx.a2});
    REQUIRE(fx.sync->state(fx.a) == SyncState::READY);
  }
  GIVEN("unconfirmable registration that marks the scene needs_sync")
  {
    Fixture fx;
    std::optional<ViewId> added;
    REQUIRE(fx.sync->requestRegisterView(
        Fixture::view("a3", fx.a),
        [](ViewId) -> bool { throw 1; },
        UnconfirmedView::NEEDS_SYNC,
        [&](std::optional<ViewId> id) { added = id; }));
    fx.drain();
    REQUIRE(added);
    REQUIRE(fx.sync->state(fx.a) == SyncState::NEEDS_SYNC);
    REQUIRE_FALSE(fx.sync->viewReady(*added));
    REQUIRE_FALSE(fx.sync->viewReady(fx.a1));
    REQUIRE(fx.sync->viewReady(fx.b1));

    // An ordinary edit cannot clear needs_sync.
    REQUIRE(fx.runEdit(1, 0.5).outcome == Outcome::APPLIED_WITH_SYNC_FAILURE);
    REQUIRE(fx.sync->state(fx.a) == SyncState::NEEDS_SYNC);

    auto r = fx.resync(2, fx.a);
    REQUIRE(r.ready);
    REQUIRE(r.views == std::vector<ViewId>{fx.a1, fx.a2, *added});
    REQUIRE(fx.sync->viewReady(*added));
    REQUIRE(fx.sync->state(fx.a) == SyncState::READY);
  }
}

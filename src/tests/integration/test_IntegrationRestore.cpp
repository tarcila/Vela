// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/activity/Activity.hpp"
#include "vsr/integration/edit/AgentEdit.hpp"
#include "vsr/integration/restore/BatchRestore.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace vsr::integration;
using namespace vsr::integration::edit;
using namespace vsr::integration::restore;
using vsr::core::math::float3;
namespace tokens = vsr::scene::tokens;

namespace {

struct Fixture
{
  vsr::scene::Scene scene;
  vsr::scene::Scene readOnlyScene;
  Integration integration;
  SceneId sceneId{INVALID_SCENE_ID};
  SceneId readOnlyId{INVALID_SCENE_ID};
  double now{1000.0};
  int hookCalls{0};
  std::function<bool()> hook = [] { return true; };
  std::unique_ptr<session::SessionAuthority> authority;
  std::unique_ptr<operations::OperationEngine> engine;
  std::unique_ptr<identity::IdentityAuthority> ids;
  std::unique_ptr<identity::SceneIdentity> identity;
  std::unique_ptr<EditService> edits;
  std::unique_ptr<RestoreService> restores;
  operations::Credentials creds;
  std::string sessionId;
  vsr::scene::MaterialRef matte;
  vsr::scene::LightRef light;
  vsr::scene::MaterialRef other;

  Fixture()
  {
    matte = scene.createObject<vsr::scene::Material>(tokens::material::matte);
    light = scene.createObject<vsr::scene::Light>(tokens::light::directional);
    other = scene.createObject<vsr::scene::Material>(tokens::material::matte);
    matte->setParameter("opacity", 1.f);
    matte->setParameter("color", float3(1.f, 1.f, 1.f));
    light->setParameter("irradiance", 1.f);
    other->setParameter("opacity", 1.f);

    SceneRegistration s;
    s.name = "scene";
    s.scene = &scene;
    s.grants.read = true;
    s.grants.edit = true;
    s.normalSyncHook = [this](SceneId) {
      ++hookCalls;
      return hook();
    };
    sceneId = *integration.registerScene(s);
    SceneRegistration ro;
    ro.name = "read-only";
    ro.scene = &readOnlyScene;
    ro.grants.read = true;
    readOnlyId = *integration.registerScene(ro);
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
    restores = std::make_unique<RestoreService>(*engine, *edits);
    restores->attachScene(sceneId, *identity);
    attach();
  }

  ~Fixture()
  {
    restores.reset();
    edits.reset();
    identity.reset();
  }

  void attach()
  {
    auto a = authority->attach(authority->launchCredential());
    REQUIRE(a.code == session::Code::OK);
    sessionId = a.sessionId;
    creds = {authority->launchCredential(), a.sessionId, a.sessionSecret};
  }

  void drain()
  {
    while (integration.service() > 0) {
    }
  }

  ParameterVersionToken version(const vsr::scene::Object *o)
  {
    return *identity->parameterVersion(identity->handleOf(o));
  }

  EditEntry entry(const vsr::scene::Object *o, std::string p, double v)
  {
    return {version(o), std::move(p), JsonValue::fromNumber(v)};
  }

  operations::OperationRef ref(std::uint64_t id)
  {
    return {sessionId, id};
  }

  BatchRef edit(std::uint64_t id, std::vector<EditEntry> e)
  {
    auto a = edits->submit(creds, ref(id), {sceneId, std::move(e)});
    REQUIRE(a.code == Outcome::OK);
    drain();
    auto r = edits->result(ref(id));
    REQUIRE(r);
    REQUIRE(r->committed);
    return *r->batchRef;
  }

  RestoreRequest target(const BatchRef &b)
  {
    return {sceneId, b};
  }

  operations::Admission submit(std::uint64_t id, const BatchRef &b)
  {
    return restores->submit(creds, ref(id), target(b));
  }

  RestoreResult restore(std::uint64_t id, const BatchRef &b)
  {
    auto a = submit(id, b);
    REQUIRE(a.code == Outcome::OK);
    drain();
    auto r = restores->result(ref(id));
    REQUIRE(r);
    return *r;
  }

  RestoreResult restoreLocal(const BatchRef &b)
  {
    auto a = restores->submitLocal(target(b));
    REQUIRE(a.code == Outcome::OK);
    drain();
    auto r = restores->resultLocal(a.ref);
    REQUIRE(r);
    return *r;
  }

  float opacity(const vsr::scene::MaterialRef &m)
  {
    return *m->parameterValueAs<float>("opacity");
  }
  float irradiance()
  {
    return *light->parameterValueAs<float>("irradiance");
  }
};

} // namespace

SCENARIO("Restoration selects a batch by target_batch_ref", "[integration]")
{
  Fixture fx;
  auto first = fx.edit(1, {fx.entry(fx.matte.data(), "opacity", 0.5)});
  auto second = fx.edit(2, {fx.entry(fx.light.data(), "irradiance", 3)});
  REQUIRE(fx.opacity(fx.matte) == 0.5f);
  REQUIRE(fx.irradiance() == 3.f);

  WHEN("the older batch is restored under its own operation_ref")
  {
    const int hooksBefore = fx.hookCalls;
    auto r = fx.restore(3, first);
    THEN("only that batch's parameters return, with one hook")
    {
      REQUIRE(r.outcome == Outcome::OK);
      REQUIRE(r.committed);
      REQUIRE(r.targetBatchRef == first);
      REQUIRE(fx.opacity(fx.matte) == 1.f);
      REQUIRE(fx.irradiance() == 3.f); // newer batch untouched
      REQUIRE(fx.hookCalls == hooksBefore + 1);
      REQUIRE(fx.edits->syncState(fx.sceneId) == SyncState::READY);
      auto s = fx.engine->status(fx.creds, fx.ref(3));
      REQUIRE(s.kind == "restore");
      REQUIRE(s.terminal == Outcome::OK);
      REQUIRE(s.committed);
      // Addendum 04 §6/§8: committed, scene_revision and new versions.
      REQUIRE(r.sceneRevision);
      const auto &rev = *r.sceneRevision;
      REQUIRE(s.result.find("\"committed\":true") != std::string::npos);
      REQUIRE(s.result.find("\"scene_revision\":\""
                  + std::to_string(rev.launch.activation) + "."
                  + std::to_string(rev.launch.nonce) + "."
                  + std::to_string(rev.domain) + "."
                  + std::to_string(rev.value) + "\"")
          != std::string::npos);
      REQUIRE(r.versions.size() == 1);
      REQUIRE(s.result.find("\"version\":"
                  + std::to_string(r.versions[0].version) + "}")
          != std::string::npos);
    }
    THEN("versions advance, eligibility is consumed and no redo exists")
    {
      REQUIRE(r.versions.size() == 1);
      REQUIRE(fx.identity->isCurrent(r.versions[0]));
      REQUIRE_FALSE(fx.restores->eligible(first));
      REQUIRE(fx.restores->restored(first));
      REQUIRE(fx.restores->eligible(second));
      // The restoration published no restorable batch and released the
      // restored one's position.
      REQUIRE(fx.edits->restorations().size() == 1);
    }
    THEN("a new ID returns already_restored; the same ID replays")
    {
      auto again = fx.restore(4, first);
      REQUIRE(again.outcome == Outcome::ALREADY_RESTORED);
      REQUIRE_FALSE(again.committed);
      auto replay = fx.submit(3, first);
      REQUIRE(replay.code == Outcome::OK);
      REQUIRE(replay.replayed);
      REQUIRE(replay.status.terminal == Outcome::OK);
      REQUIRE(replay.status.committed);
      auto changed = fx.submit(3, second);
      REQUIRE(changed.code == Outcome::REQUEST_ID_CONFLICT);
      fx.drain();
      REQUIRE(fx.irradiance() == 3.f);
      REQUIRE(fx.restores->eligible(second));
    }
  }
}

SCENARIO("Later changes on an affected object conflict", "[integration]")
{
  Fixture fx;
  auto b = fx.edit(1,
      {fx.entry(fx.matte.data(), "opacity", 0.5),
          fx.entry(fx.light.data(), "irradiance", 3)});

  auto expectConflict = [&](std::uint64_t id) {
    auto r = fx.restore(id, b);
    REQUIRE(r.outcome == Outcome::CONFLICT);
    REQUIRE_FALSE(r.committed);
    REQUIRE(fx.irradiance() == 3.f); // nothing partially restored
    REQUIRE_FALSE(fx.restores->eligible(b));
    REQUIRE(fx.restores->conflicting(b));
    REQUIRE_FALSE(fx.restores->restored(b));
    auto s = fx.engine->status(fx.creds, fx.ref(id));
    REQUIRE(s.kind == "restore");
    REQUIRE(s.terminal == Outcome::CONFLICT);
    REQUIRE_FALSE(s.committed);
  };

  WHEN("the same parameter changed")
  {
    fx.matte->setParameter("opacity", 0.9f);
    expectConflict(2);
  }
  WHEN("another parameter on an affected object changed")
  {
    fx.matte->setParameter("color", float3(0.f, 0.f, 0.f));
    expectConflict(2);
  }
  WHEN("the parameter changed back")
  {
    fx.matte->setParameter("opacity", 0.2f);
    fx.matte->setParameter("opacity", 0.5f);
    expectConflict(2);
  }
  WHEN("an affected object was removed")
  {
    fx.scene.removeObject(fx.light.data());
    auto r = fx.restore(2, b);
    REQUIRE(r.outcome == Outcome::CONFLICT);
    REQUIRE(fx.opacity(fx.matte) == 0.5f);
  }
  WHEN("only another object changed")
  {
    fx.other->setParameter("opacity", 0.3f);
    auto r = fx.restore(2, b);
    THEN("the restore commits and unrelated state is preserved")
    {
      REQUIRE(r.outcome == Outcome::OK);
      REQUIRE(fx.opacity(fx.matte) == 1.f);
      REQUIRE(fx.irradiance() == 1.f);
      REQUIRE(fx.opacity(fx.other) == 0.3f);
    }
  }
  WHEN("a newer batch on the same object is restored first")
  {
    auto newer = fx.edit(2, {fx.entry(fx.matte.data(), "opacity", 0.25)});
    REQUIRE(fx.restore(3, newer).outcome == Outcome::OK);
    THEN("the older record is not revived")
    {
      REQUIRE(fx.opacity(fx.matte) == 0.5f);
      expectConflict(4);
    }
  }
}

SCENARIO("Restoration preparation failure preserves eligibility",
    "[integration]")
{
  Fixture fx;
  auto b = fx.edit(1, {fx.entry(fx.matte.data(), "opacity", 0.5)});
  const auto v = fx.version(fx.matte.data());
  const int hooks = fx.hookCalls;
  fx.restores->setFaultInjector([] { return true; });
  auto r = fx.restore(2, b);
  REQUIRE_FALSE(r.committed);
  REQUIRE(r.outcome == Outcome::BUSY);
  REQUIRE(fx.opacity(fx.matte) == 0.5f);
  REQUIRE(fx.identity->isCurrent(v));
  REQUIRE(fx.hookCalls == hooks);
  REQUIRE(fx.restores->eligible(b));

  fx.restores->setFaultInjector({});
  REQUIRE(fx.restore(3, b).outcome == Outcome::OK);
  REQUIRE(fx.opacity(fx.matte) == 1.f);
}

SCENARIO("A committed restore consumes eligibility even if sync fails",
    "[integration]")
{
  Fixture fx;
  auto b = fx.edit(1, {fx.entry(fx.matte.data(), "opacity", 0.5)});
  fx.hook = [] { return false; };
  auto r = fx.restore(2, b);
  REQUIRE(r.outcome == Outcome::APPLIED_WITH_SYNC_FAILURE);
  REQUIRE(r.committed);
  REQUIRE(fx.opacity(fx.matte) == 1.f);
  REQUIRE(fx.edits->syncState(fx.sceneId) == SyncState::FAILED);
  REQUIRE(fx.restores->restored(b));
  fx.hook = [] { return true; };
  REQUIRE(fx.restore(3, b).outcome == Outcome::ALREADY_RESTORED);
}

SCENARIO("Unknown or wrong-launch references are outcome_unavailable",
    "[integration]")
{
  Fixture fx;
  auto b = fx.edit(1, {fx.entry(fx.matte.data(), "opacity", 0.5)});
  BatchRef unknown{b.launchId, std::string(32, '0')};
  BatchRef wrongLaunch{std::string(32, 'f'), b.batchId};
  REQUIRE(fx.restore(2, unknown).outcome == Outcome::OUTCOME_UNAVAILABLE);
  REQUIRE(fx.restore(3, wrongLaunch).outcome == Outcome::OUTCOME_UNAVAILABLE);
  // Addendum 04 §7: no reason for unknown or wrong-launch targets.
  REQUIRE(fx.engine->status(fx.creds, fx.ref(2)).result.empty());
  REQUIRE(fx.engine->status(fx.creds, fx.ref(3)).result.empty());
  REQUIRE(fx.opacity(fx.matte) == 0.5f);
  REQUIRE(fx.restores->eligible(b));
}

SCENARIO("A fresh session restores an older session's batch", "[integration]")
{
  Fixture fx;
  auto b = fx.edit(1, {fx.entry(fx.matte.data(), "opacity", 0.5)});
  auto oldCreds = fx.creds;
  REQUIRE(fx.engine->detach(fx.creds) == Outcome::OK);
  fx.attach();
  REQUIRE(fx.sessionId != oldCreds.sessionId);

  THEN("the old secret cannot act")
  {
    auto stale = fx.restores->submit(
        oldCreds, {oldCreds.sessionId, 2}, fx.target(b));
    REQUIRE(stale.code == Outcome::INVALID_SESSION_PROOF);
    REQUIRE(fx.restores->eligible(b));
  }
  THEN("current proof restores it without prior inspection")
  {
    auto r = fx.restore(1, b);
    REQUIRE(r.outcome == Outcome::OK);
    REQUIRE(fx.opacity(fx.matte) == 1.f);
  }
}

SCENARIO("Two local callers racing for one batch cannot both consume it",
    "[integration]")
{
  Fixture fx;
  auto b = fx.edit(1, {fx.entry(fx.matte.data(), "opacity", 0.5)});
  auto a1 = fx.restores->submitLocal(fx.target(b));
  auto a2 = fx.restores->submitLocal(fx.target(b));
  REQUIRE(a1.code == Outcome::OK);
  REQUIRE(a2.code == Outcome::OK);
  fx.drain();
  auto r1 = fx.restores->resultLocal(a1.ref);
  auto r2 = fx.restores->resultLocal(a2.ref);
  REQUIRE(r1);
  REQUIRE(r2);
  REQUIRE(int(r1->committed) + int(r2->committed) == 1);
  auto loser = r1->committed ? *r2 : *r1;
  REQUIRE(loser.outcome == Outcome::ALREADY_RESTORED);
  REQUIRE(fx.opacity(fx.matte) == 1.f);
  REQUIRE(fx.engine->highWater(fx.sessionId) == 1); // local uses no IDs
}

SCENARIO("Local Restore survives disable and detach within host permission",
    "[integration]")
{
  Fixture fx;
  auto b = fx.edit(1, {fx.entry(fx.matte.data(), "opacity", 0.5)});

  WHEN("agent edits are disabled")
  {
    fx.authority->setAgentEditsDisabled(true);
    auto ext = fx.submit(2, b);
    REQUIRE(ext.code == Outcome::FORBIDDEN);
    REQUIRE(fx.restoreLocal(b).outcome == Outcome::OK);
    REQUIRE(fx.opacity(fx.matte) == 1.f);
  }
  WHEN("Pi detaches")
  {
    REQUIRE(fx.engine->detach(fx.creds) == Outcome::OK);
    REQUIRE_FALSE(fx.authority->hasSession());
    REQUIRE(fx.restoreLocal(b).outcome == Outcome::OK);
    REQUIRE(fx.opacity(fx.matte) == 1.f);
  }
  WHEN("the host grants no edit on the target scene")
  {
    auto a = fx.restores->submitLocal({fx.readOnlyId, b});
    REQUIRE(a.code == Outcome::FORBIDDEN);
    REQUIRE(fx.restores->eligible(b));
  }
}

SCENARIO("An expired batch reports outcome_unavailable", "[integration]")
{
  // Ticket 10: the 33rd restorable batch visibly expires the oldest one.
  Fixture fx;
  std::vector<BatchRef> batches;
  for (std::uint64_t i = 1; i <= 33; ++i)
    batches.push_back(
        fx.edit(i, {fx.entry(fx.other.data(), "opacity", 0.01 * double(i))}));
  REQUIRE(fx.edits->restorations().size() == 32);
  REQUIRE(fx.edits->restorations().expired(batches[0]));
  REQUIRE_FALSE(fx.restores->eligible(batches[0]));
  REQUIRE(fx.restores->eligible(batches[32]));
  const float before = fx.opacity(fx.other);
  auto r = fx.restore(100, batches[0]);
  REQUIRE(r.outcome == Outcome::OUTCOME_UNAVAILABLE);
  REQUIRE_FALSE(r.committed);
  REQUIRE(r.expired);
  REQUIRE(fx.engine->status(fx.creds, fx.ref(100)).result
      == "{\"reason\":\"expired\"}");
  REQUIRE(fx.opacity(fx.other) == before);
}

namespace {

// Restorations whose records feed an application-owned ActivityLog.
struct LoggedFixture
{
  Fixture fx;
  activity::ActivityLog log;
  activity::EngineActivitySink sink;

  explicit LoggedFixture(activity::Limits limits = {})
      : log(limits), sink(log, fx.integration)
  {
    fx.engine->setActivitySink(&sink);
  }
  ~LoggedFixture()
  {
    fx.engine->setActivitySink(nullptr);
  }
};

} // namespace

SCENARIO("Restoration updates the activity log's eligibility", "[integration]")
{
  LoggedFixture lf;
  auto &fx = lf.fx;
  auto a = fx.edit(1, {fx.entry(fx.matte.data(), "opacity", 0.5)});
  auto b = fx.edit(2, {fx.entry(fx.light.data(), "irradiance", 3)});
  REQUIRE(lf.log.usage().restorableBatches == 2);
  const auto usageBefore = lf.log.usage().bytes;
  const std::size_t bytesA = lf.log.get(1)->restorationBytes;
  const std::size_t bytesB = lf.log.get(2)->restorationBytes;
  REQUIRE(bytesA > 0);
  REQUIRE(bytesB > 0);

  WHEN("a batch is restored")
  {
    auto r = fx.restore(3, a);
    REQUIRE(r.outcome == Outcome::OK);
    THEN("its entry is RESTORED and its payload bytes are released")
    {
      auto e = lf.log.get(1);
      REQUIRE(e);
      REQUIRE(e->eligibility == activity::Eligibility::RESTORED);
      REQUIRE(e->restorationBytes == 0);
      REQUIRE(lf.log.eligibility(a) == activity::Eligibility::RESTORED);
      REQUIRE(lf.log.usage().restorableBatches == 1);
      auto restoreEntry = lf.log.get(3);
      REQUIRE(restoreEntry);
      REQUIRE(lf.log.usage().bytes
          == usageBefore - bytesA + activity::ActivityLog::cost(*restoreEntry));
      REQUIRE(lf.log.eligibility(b) == activity::Eligibility::ELIGIBLE);
    }
  }
  WHEN("a batch conflicts")
  {
    fx.light->setParameter("irradiance", 5.f);
    auto r = fx.restore(3, b);
    REQUIRE(r.outcome == Outcome::CONFLICT);
    THEN("its entry is CONFLICTING, nothing mutated, payload released")
    {
      REQUIRE(fx.irradiance() == 5.f);
      auto e = lf.log.get(2);
      REQUIRE(e);
      REQUIRE(e->eligibility == activity::Eligibility::CONFLICTING);
      REQUIRE(e->restorationBytes == 0);
      REQUIRE(lf.log.usage().restorableBatches == 1);
      REQUIRE_FALSE(fx.restores->eligible(b));
      REQUIRE(fx.restores->conflicting(b));
      REQUIRE_FALSE(fx.edits->restorations().find(b));
    }
    THEN("a retry still reports conflict and cannot revive it")
    {
      fx.light->setParameter("irradiance", 3.f);
      REQUIRE(fx.restore(4, b).outcome == Outcome::CONFLICT);
      REQUIRE(fx.irradiance() == 3.f);
      REQUIRE(lf.log.eligibility(b) == activity::Eligibility::CONFLICTING);
    }
  }
}

SCENARIO("A batch the log expired reports outcome_unavailable",
    "[integration]")
{
  activity::Limits l;
  l.restorableBatches = 2;
  LoggedFixture lf(l);
  auto &fx = lf.fx;
  auto first = fx.edit(1, {fx.entry(fx.other.data(), "opacity", 0.1)});
  fx.edit(2, {fx.entry(fx.other.data(), "opacity", 0.2)});
  fx.edit(3, {fx.entry(fx.other.data(), "opacity", 0.3)});
  REQUIRE(lf.log.eligibility(first) == activity::Eligibility::EXPIRED);
  REQUIRE_FALSE(fx.restores->eligible(first));
  auto r = fx.restore(4, first);
  REQUIRE(r.outcome == Outcome::OUTCOME_UNAVAILABLE);
  REQUIRE_FALSE(r.committed);
  REQUIRE(fx.engine->status(fx.creds, fx.ref(4)).result
      == "{\"reason\":\"expired\"}");
  REQUIRE(fx.opacity(fx.other) == 0.3f);
  REQUIRE(lf.log.eligibility(first) == activity::Eligibility::EXPIRED);
}

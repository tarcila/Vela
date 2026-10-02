// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/activity/Activity.hpp"
#include "vsr/integration/edit/AgentEdit.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace vsr::integration;
using namespace vsr::integration::edit;
using operations::Progress;
using vsr::core::math::float3;
namespace tokens = vsr::scene::tokens;

namespace {

// Records every mutation notification and what the scene looked like then.
struct Recorder : vsr::scene::EmptyUpdateDelegate
{
  std::function<void()> onParameters;
  int parameterSignals{0};

  void signalParameterUpdated(
      const vsr::scene::Object *, const vsr::scene::Parameter *) override
  {
    ++parameterSignals;
    if (onParameters)
      onParameters();
  }
  void signalParameterBatchUpdated(const vsr::scene::Object *,
      const std::vector<const vsr::scene::Parameter *> &) override
  {
    ++parameterSignals;
    if (onParameters)
      onParameters();
  }
};

struct Fixture
{
  vsr::scene::Scene scene;
  Integration integration;
  SceneId sceneId{INVALID_SCENE_ID};
  double now{1000.0};
  int hookCalls{0};
  std::function<bool()> hook = [] { return true; };
  std::unique_ptr<session::SessionAuthority> authority;
  std::unique_ptr<operations::OperationEngine> engine;
  std::unique_ptr<identity::IdentityAuthority> ids;
  std::unique_ptr<identity::SceneIdentity> identity;
  std::unique_ptr<EditService> edits;
  operations::Credentials creds;
  std::string sessionId;
  vsr::scene::MaterialRef matte;
  vsr::scene::LightRef light;
  Recorder *recorder{nullptr};

  Fixture()
  {
    matte = scene.createObject<vsr::scene::Material>(tokens::material::matte);
    light = scene.createObject<vsr::scene::Light>(tokens::light::directional);
    matte->setParameter("opacity", 1.f);
    matte->setParameter("color", float3(1.f, 1.f, 1.f));
    light->setParameter("irradiance", 1.f);

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
    recorder = scene.updateDelegate().emplace<Recorder>();

    auto a = authority->attach(authority->launchCredential());
    REQUIRE(a.code == session::Code::OK);
    sessionId = a.sessionId;
    creds = {authority->launchCredential(), a.sessionId, a.sessionSecret};
  }

  ~Fixture()
  {
    scene.updateDelegate().erase(recorder);
    edits.reset();
    identity.reset();
  }

  ParameterVersionToken version(const vsr::scene::Object *o)
  {
    return *identity->parameterVersion(identity->handleOf(o));
  }

  EditEntry entry(const vsr::scene::Object *o, std::string p, JsonValue v)
  {
    return {version(o), std::move(p), std::move(v)};
  }

  EditBatch batch(std::vector<EditEntry> e)
  {
    return {sceneId, std::move(e)};
  }

  operations::OperationRef ref(std::uint64_t id)
  {
    return {sessionId, id};
  }

  operations::Admission submit(std::uint64_t id, EditBatch b)
  {
    return edits->submit(creds, ref(id), std::move(b));
  }

  EditResult run(std::uint64_t id, EditBatch b)
  {
    auto a = submit(id, std::move(b));
    REQUIRE(a.code == Outcome::OK);
    while (integration.service() > 0) {
    }
    auto r = edits->result(ref(id));
    REQUIRE(r);
    return *r;
  }

  float opacity()
  {
    return *matte->parameterValueAs<float>("opacity");
  }
  float irradiance()
  {
    return *light->parameterValueAs<float>("irradiance");
  }
};

bool isLowerHex32(const std::string &s)
{
  if (s.size() != 32)
    return false;
  for (char c : s)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      return false;
  return true;
}

} // namespace

SCENARIO("An Agent Edit Batch commits in full and publishes a Batch Reference",
    "[integration]")
{
  Fixture fx;
  GIVEN("a valid multi-object batch")
  {
    auto r = fx.run(1,
        fx.batch(
            {fx.entry(fx.matte.data(), "opacity", JsonValue::fromNumber(0.5)),
                fx.entry(
                    fx.light.data(), "irradiance", JsonValue::fromNumber(3))}));
    THEN("every edit is applied and the host hook ran once")
    {
      REQUIRE(r.outcome == Outcome::OK);
      REQUIRE(r.committed);
      REQUIRE(fx.opacity() == 0.5f);
      REQUIRE(fx.irradiance() == 3.f);
      REQUIRE(fx.hookCalls == 1);
      REQUIRE(fx.edits->syncState(fx.sceneId) == SyncState::READY);
    }
    THEN("the batch_ref is launch-qualified 32 lowercase hex")
    {
      REQUIRE(r.batchRef);
      REQUIRE(r.batchRef->launchId == fx.authority->launchId());
      REQUIRE(isLowerHex32(r.batchRef->batchId));
      auto status = fx.engine->status(fx.creds, fx.ref(1));
      REQUIRE(status.terminal == Outcome::OK);
      REQUIRE(status.committed);
      REQUIRE(status.result.find(r.batchRef->batchId) != std::string::npos);
    }
    THEN("the result carries committed, scene_revision and new versions")
    {
      REQUIRE(r.sceneRevision);
      REQUIRE(r.versions.size() == 2);
      const auto &rev = *r.sceneRevision;
      const std::string token = std::to_string(rev.launch.activation) + "."
          + std::to_string(rev.launch.nonce) + "." + std::to_string(rev.domain)
          + "." + std::to_string(rev.value);
      auto status = fx.engine->status(fx.creds, fx.ref(1));
      REQUIRE(status.result.find("\"committed\":true") != std::string::npos);
      REQUIRE(status.result.find("\"scene_revision\":\"" + token + "\"")
          != std::string::npos);
      for (const auto &v : r.versions)
        REQUIRE(status.result.find(
                    "\"version\":" + std::to_string(v.version) + "}")
            != std::string::npos);
      REQUIRE(status.result.find("\"versions\":[{\"handle\":\"")
          != std::string::npos);
    }
    THEN("restoration data is reserved for the batch")
    {
      auto rec = fx.edits->restorations().find(*r.batchRef);
      REQUIRE(rec);
      REQUIRE(rec->changes.size() == 2);
      REQUIRE(rec->changes[0].before.get<float>() == 1.f);
      REQUIRE(rec->changes[0].after.get<float>() == 0.5f);
      REQUIRE(rec->provenance == std::nullopt); // provenance is ticket 10's
      REQUIRE_FALSE(fx.edits->restorations().find(
          BatchRef{"00000000000000000000000000000000", r.batchRef->batchId}));
    }
    THEN("later batches get fresh, never-reused batch IDs")
    {
      std::set<std::string> seen{r.batchRef->batchId};
      for (std::uint64_t id = 2; id < 6; id++) {
        auto next = fx.run(id,
            fx.batch({fx.entry(
                fx.matte.data(), "opacity", JsonValue::fromNumber(0.1 * id))}));
        REQUIRE(next.batchRef);
        REQUIRE(seen.insert(next.batchRef->batchId).second);
      }
    }
  }

  GIVEN("a batch of 64 edits and one of 65")
  {
    std::vector<vsr::scene::MaterialRef> mats;
    std::vector<EditEntry> entries;
    for (int i = 0; i < 65; i++) {
      mats.push_back(
          fx.scene.createObject<vsr::scene::Material>(tokens::material::matte));
      mats.back()->setParameter("opacity", 1.f);
      entries.push_back(
          fx.entry(mats.back().data(), "opacity", JsonValue::fromNumber(0.25)));
    }
    THEN("65 rejects before admission and 64 commits")
    {
      auto over = fx.submit(1, fx.batch(entries));
      REQUIRE(over.code == Outcome::INVALID_REQUEST);
      REQUIRE(fx.engine->highWater(fx.sessionId) == 0);
      entries.pop_back();
      auto r = fx.run(1, fx.batch(entries));
      REQUIRE(r.outcome == Outcome::OK);
      for (int i = 0; i < 64; i++)
        REQUIRE(*mats[i]->parameterValueAs<float>("opacity") == 0.25f);
    }
  }
}

SCENARIO("Stale or invalid batches leave the scene unchanged", "[integration]")
{
  Fixture fx;
  auto staleMatte =
      fx.entry(fx.matte.data(), "opacity", JsonValue::fromNumber(0.5));
  auto lightEdit =
      fx.entry(fx.light.data(), "irradiance", JsonValue::fromNumber(2));

  auto expectUnchanged = [&](const EditResult &r, Outcome code) {
    REQUIRE(r.outcome == code);
    REQUIRE_FALSE(r.committed);
    REQUIRE_FALSE(r.batchRef);
    REQUIRE(fx.irradiance() == 1.f);
    REQUIRE(fx.hookCalls == 0);
    REQUIRE(fx.edits->restorations().size() == 0);
  };

  WHEN("the same parameter changed since inspection")
  {
    fx.matte->setParameter("opacity", 0.9f);
    expectUnchanged(
        fx.run(1, fx.batch({lightEdit, staleMatte})), Outcome::CONFLICT);
    REQUIRE(fx.opacity() == 0.9f);
  }
  WHEN("another parameter on the same object changed")
  {
    fx.matte->setParameter("color", float3(0.f, 0.f, 0.f));
    expectUnchanged(
        fx.run(1, fx.batch({lightEdit, staleMatte})), Outcome::CONFLICT);
  }
  WHEN("the parameter changed and changed back")
  {
    fx.matte->setParameter("opacity", 0.2f);
    fx.matte->setParameter("opacity", 1.f);
    expectUnchanged(
        fx.run(1, fx.batch({lightEdit, staleMatte})), Outcome::CONFLICT);
  }
  WHEN("only another object changed")
  {
    auto other =
        fx.scene.createObject<vsr::scene::Material>(tokens::material::matte);
    other->setParameter("opacity", 0.3f);
    auto r = fx.run(1, fx.batch({lightEdit, staleMatte}));
    THEN("the batch commits")
    {
      REQUIRE(r.outcome == Outcome::OK);
      REQUIRE(fx.opacity() == 0.5f);
    }
  }
  WHEN("one member has an invalid value")
  {
    auto bad = fx.entry(
        fx.matte.data(), "opacity", JsonValue::fromNumber(1.0000000000000002));
    auto r = fx.run(1, fx.batch({lightEdit, bad}));
    expectUnchanged(r, Outcome::INVALID_REQUEST);
    REQUIRE(r.failedEntry == std::size_t(1));
    REQUIRE(r.valueError == ValueError::OUT_OF_DOMAIN);
    auto status = fx.engine->status(fx.creds, fx.ref(1));
    REQUIRE(status.result
        == "{\"committed\":false,\"failed_entry\":1,"
           "\"value_error\":\"out_of_domain\"}");
    REQUIRE(fx.engine->highWater(fx.sessionId) == 1); // ID consumed
  }
}

SCENARIO("Empty or duplicate-entry batches are invalid_request, no ID used",
    "[integration]")
{
  Fixture fx;
  auto e = fx.entry(fx.matte.data(), "opacity", JsonValue::fromNumber(0.5));
  REQUIRE(fx.submit(1, fx.batch({})).code == Outcome::INVALID_REQUEST);
  REQUIRE(fx.submit(1, fx.batch({e, e})).code == Outcome::INVALID_REQUEST);
  REQUIRE(fx.engine->highWater(fx.sessionId) == 0);
  REQUIRE(fx.opacity() == 1.f);
  auto r = fx.run(1, fx.batch({e}));
  REQUIRE(r.outcome == Outcome::OK);
  REQUIRE(fx.opacity() == 0.5f);
}

SCENARIO("Preparation failures publish nothing and emit no notifications",
    "[integration]")
{
  for (auto stage : {FaultStage::PREPARE,
           FaultStage::ALLOCATE,
           FaultStage::RESERVE_RESTORATION}) {
    Fixture fx;
    fx.edits->setFaultInjector([stage](FaultStage s) { return s == stage; });
    const auto before = fx.version(fx.matte.data());
    const auto revision = fx.identity->sceneRevision();
    auto r = fx.run(7,
        fx.batch(
            {fx.entry(fx.matte.data(), "opacity", JsonValue::fromNumber(0.5)),
                fx.entry(
                    fx.light.data(), "irradiance", JsonValue::fromNumber(2))}));
    REQUIRE_FALSE(r.committed);
    REQUIRE_FALSE(r.batchRef);
    REQUIRE(fx.opacity() == 1.f);
    REQUIRE(fx.irradiance() == 1.f);
    REQUIRE(fx.recorder->parameterSignals == 0);
    REQUIRE(fx.hookCalls == 0);
    REQUIRE(fx.identity->isCurrent(before));
    REQUIRE(fx.identity->sceneRevision() == revision);
    REQUIRE(fx.edits->restorations().size() == 0);
    REQUIRE(fx.edits->syncState(fx.sceneId) == SyncState::READY);

    auto status = fx.engine->status(fx.creds, fx.ref(7));
    // Addendum 03 §4: terminal busy, nothing committed.
    REQUIRE(status.terminal == Outcome::BUSY);
    REQUIRE(r.outcome == Outcome::BUSY);
    REQUIRE_FALSE(status.committed);
    // The admitted ID stays consumed.
    fx.edits->setFaultInjector({});
    auto retry = fx.submit(7,
        fx.batch({fx.entry(
            fx.matte.data(), "opacity", JsonValue::fromNumber(0.4))}));
    REQUIRE(retry.code == Outcome::REQUEST_ID_CONFLICT);
  }
}

SCENARIO("Observers only see complete commits", "[integration]")
{
  Fixture fx;
  std::vector<std::pair<float, float>> seen;
  fx.recorder->onParameters = [&] {
    seen.emplace_back(fx.opacity(), fx.irradiance());
  };
  auto r = fx.run(1,
      fx.batch({fx.entry(
                    fx.matte.data(), "opacity", JsonValue::fromNumber(0.5)),
          fx.entry(fx.light.data(), "irradiance", JsonValue::fromNumber(4))}));
  REQUIRE(r.committed);
  REQUIRE_FALSE(seen.empty());
  for (auto &s : seen) {
    REQUIRE(s.first == 0.5f);
    REQUIRE(s.second == 4.f);
  }
}

SCENARIO(
    "Commit reports synchronizing until the host acknowledges", "[integration]")
{
  Fixture fx;
  std::optional<bool> committedDuringHook;
  std::optional<std::string> progressDuringHook;
  std::optional<SyncState> stateDuringHook;
  fx.hook = [&] {
    auto s = fx.engine->status(fx.creds, fx.ref(1));
    committedDuringHook = s.committed && !s.terminal;
    if (s.progress)
      progressDuringHook = operations::progressName(*s.progress);
    stateDuringHook = fx.edits->syncState(fx.sceneId);
    return true;
  };
  REQUIRE(fx.submit(1,
                fx.batch({fx.entry(
                    fx.matte.data(), "opacity", JsonValue::fromNumber(0.5))}))
              .code
      == Outcome::OK);

  REQUIRE(fx.integration.service() == 1); // commit step
  auto mid = fx.engine->status(fx.creds, fx.ref(1));
  REQUIRE(mid.progress == Progress::SYNCHRONIZING);
  REQUIRE(mid.committed);
  REQUIRE_FALSE(mid.terminal);
  REQUIRE(fx.opacity() == 0.5f);
  REQUIRE(fx.edits->syncState(fx.sceneId) == SyncState::SYNCHRONIZING);
  REQUIRE(fx.hookCalls == 0);

  REQUIRE(fx.integration.service() == 1); // hook step
  REQUIRE(fx.hookCalls == 1);
  // Committed work reports `synchronizing` while its hook runs (addendum
  // 03 §5).
  REQUIRE(committedDuringHook == true);
  REQUIRE(progressDuringHook == std::string("synchronizing"));
  REQUIRE(stateDuringHook == SyncState::SYNCHRONIZING);
  auto done = fx.engine->status(fx.creds, fx.ref(1));
  REQUIRE(done.terminal == Outcome::OK);
  REQUIRE(fx.edits->syncState(fx.sceneId) == SyncState::READY);
}

SCENARIO("Hook failure reports applied_with_sync_failure without rollback",
    "[integration]")
{
  // Failure/unconfirmable readiness (false) and an exception.
  for (int mode = 0; mode < 2; mode++) {
    Fixture fx;
    if (mode == 0)
      fx.hook = [] { return false; };
    else
      fx.hook = []() -> bool { throw std::runtime_error("renderer lost"); };

    auto r = fx.run(1,
        fx.batch({fx.entry(
            fx.matte.data(), "opacity", JsonValue::fromNumber(0.5))}));
    REQUIRE(r.outcome == Outcome::APPLIED_WITH_SYNC_FAILURE);
    REQUIRE(r.committed);
    REQUIRE(r.batchRef);
    REQUIRE(fx.edits->restorations().find(*r.batchRef));
    REQUIRE(fx.opacity() == 0.5f);
    REQUIRE(fx.hookCalls == 1);
    REQUIRE(fx.edits->syncState(fx.sceneId) == SyncState::FAILED);
    auto status = fx.engine->status(fx.creds, fx.ref(1));
    REQUIRE(status.terminal == Outcome::APPLIED_WITH_SYNC_FAILURE);
    REQUIRE(status.committed);
  }
}

SCENARIO("A failed scene stays failed after a later successful hook",
    "[integration]")
{
  Fixture fx;
  fx.hook = [] { return false; };
  fx.run(1,
      fx.batch(
          {fx.entry(fx.matte.data(), "opacity", JsonValue::fromNumber(0.5))}));
  REQUIRE(fx.edits->syncState(fx.sceneId) == SyncState::FAILED);

  fx.hook = [] { return true; };
  auto r = fx.run(2,
      fx.batch(
          {fx.entry(fx.light.data(), "irradiance", JsonValue::fromNumber(5))}));
  REQUIRE(fx.hookCalls == 2);
  REQUIRE(r.committed);
  REQUIRE(r.outcome == Outcome::APPLIED_WITH_SYNC_FAILURE);
  REQUIRE(fx.edits->syncState(fx.sceneId) == SyncState::FAILED);
  REQUIRE(fx.irradiance() == 5.f);
}

SCENARIO("Cancellation or disconnect during the hook never reports no-commit",
    "[integration]")
{
  for (bool detach : {false, true}) {
    Fixture fx;
    fx.hook = [&] {
      if (detach)
        REQUIRE(fx.engine->detach(fx.creds) == Outcome::OK);
      else
        fx.engine->cancel(fx.creds, fx.ref(1));
      return true;
    };
    REQUIRE(fx.submit(1,
                  fx.batch({fx.entry(
                      fx.matte.data(), "opacity", JsonValue::fromNumber(0.5))}))
                .code
        == Outcome::OK);
    fx.integration.service(); // commit
    if (!detach) {
      // Cancel between commit and hook is also refused.
      auto c = fx.engine->cancel(fx.creds, fx.ref(1));
      REQUIRE(c.committed);
    }
    fx.integration.service(); // hook
    REQUIRE(fx.hookCalls == 1);
    auto r = fx.edits->result(fx.ref(1));
    REQUIRE(r);
    REQUIRE(r->committed);
    REQUIRE(r->outcome == Outcome::OK);
    REQUIRE(r->batchRef);
    REQUIRE(fx.opacity() == 0.5f);
  }
}

SCENARIO("Local edits share the path under host authority", "[integration]")
{
  Fixture fx;
  auto a = fx.edits->submitLocal(fx.batch(
      {fx.entry(fx.matte.data(), "opacity", JsonValue::fromNumber(0.5))}));
  REQUIRE(a.code == Outcome::OK);
  while (fx.integration.service() > 0) {
  }
  auto r = fx.edits->resultLocal(a.ref);
  REQUIRE(r);
  REQUIRE(r->outcome == Outcome::OK);
  REQUIRE(r->batchRef);
  REQUIRE(fx.hookCalls == 1);
  REQUIRE(fx.engine->highWater(fx.sessionId) == 0);
}

namespace {

// Edits whose records feed an application-owned ActivityLog.
struct LoggedFixture
{
  Fixture fx;
  std::unique_ptr<activity::ActivityLog> log;
  std::unique_ptr<activity::EngineActivitySink> sink;

  explicit LoggedFixture(activity::Limits limits)
  {
    log = std::make_unique<activity::ActivityLog>(limits);
    sink = std::make_unique<activity::EngineActivitySink>(*log, fx.integration);
    fx.engine->setActivitySink(sink.get());
  }
  ~LoggedFixture()
  {
    fx.engine->setActivitySink(nullptr);
  }

  EditResult edit(std::uint64_t id, float opacity)
  {
    return fx.run(id,
        fx.batch({fx.entry(
            fx.matte.data(), "opacity", JsonValue::fromNumber(opacity))}));
  }

  // Active host-local work that holds its record until the test ends.
  void holdActive()
  {
    operations::Request r;
    r.kind = "inspection";
    r.target = session::Target::scene(fx.sceneId);
    r.capability = session::Capability::READ;
    r.canonicalArguments = "{}";
    r.executor = [](operations::StepContext &) {
      operations::Step s;
      s.status = operations::Step::Status::BACKEND_PENDING;
      return s;
    };
    REQUIRE(fx.engine->submitLocal(std::move(r)).code == Outcome::OK);
    while (fx.integration.service() > 0) {
    }
  }
};

// Cost of one committed single-edit entry, its result payload, and the
// cost of an active placeholder, measured under an unconstrained log.
struct Costs
{
  std::size_t edit{0}, result{0}, active{0};
};

Costs measureCosts()
{
  LoggedFixture m({});
  Costs c;
  auto r = m.edit(1, 0.5f);
  REQUIRE(r.committed);
  auto e = m.log->get(1);
  REQUIRE(e);
  REQUIRE(e->restorationBytes > 0);
  c.edit = activity::ActivityLog::cost(*e);
  c.result = e->result.size();
  m.holdActive();
  c.active = m.log->usage().bytes - c.edit;
  return c;
}

} // namespace

SCENARIO("A committed edit is an eligible batch in the activity log",
    "[integration]")
{
  LoggedFixture lf({});
  auto r = lf.edit(1, 0.5f);
  REQUIRE(r.committed);
  auto e = lf.log->get(1);
  REQUIRE(e);
  REQUIRE(e->kind == activity::OperationKind::EDIT);
  REQUIRE(e->batchRef);
  REQUIRE(*e->batchRef == *r.batchRef);
  REQUIRE(e->eligibility == activity::Eligibility::ELIGIBLE);
  REQUIRE(e->restorationBytes
      == restorationBytes(*lf.fx.edits->restorations().find(*r.batchRef)));
  REQUIRE(lf.log->usage().restorableBatches == 1);
  REQUIRE(lf.log->usage().bytes == activity::ActivityLog::cost(*e));
}

SCENARIO("Restoration payloads share the activity byte budget", "[integration]")
{
  const Costs c = measureCosts();

  GIVEN("a budget holding about two committed edits")
  {
    activity::Limits l;
    l.bytes = 2 * c.edit + c.edit / 2;
    LoggedFixture lf(l);
    std::vector<BatchRef> refs;
    for (std::uint64_t id = 1; id <= 4; id++) {
      auto r = lf.edit(id, 0.1f * float(id));
      REQUIRE(r.committed);
      refs.push_back(*r.batchRef);
    }
    THEN("older terminal records are evicted and their batches expire")
    {
      REQUIRE(lf.log->usage().bytes <= l.bytes);
      REQUIRE_FALSE(lf.log->get(1));
      REQUIRE_FALSE(lf.log->eligibility(refs[0]));
      REQUIRE(lf.fx.edits->restorations().expired(refs[0]));
      REQUIRE_FALSE(lf.fx.edits->restorations().find(refs[0]));
      REQUIRE(lf.log->eligibility(refs[3]) == activity::Eligibility::ELIGIBLE);
      REQUIRE(lf.fx.edits->restorations().find(refs[3]));
    }
  }

  GIVEN("active work leaving room to admit but not to reserve restoration")
  {
    activity::Limits l;
    l.bytes = c.active + c.edit - c.result - 1;
    LoggedFixture lf(l);
    lf.holdActive();
    const int signals = lf.fx.recorder->parameterSignals;
    auto r = lf.edit(1, 0.25f);
    THEN("the edit rejects busy before commit")
    {
      REQUIRE(r.outcome == Outcome::BUSY);
      REQUIRE_FALSE(r.committed);
      REQUIRE_FALSE(r.batchRef);
      REQUIRE(lf.fx.opacity() == 1.f);
      REQUIRE(lf.fx.recorder->parameterSignals == signals);
      REQUIRE(lf.fx.edits->restorations().size() == 0);
      REQUIRE(lf.log->usage().restorableBatches == 0);
      REQUIRE(lf.log->usage().bytes <= l.bytes);
      auto e = lf.log->get(2);
      REQUIRE(e);
      REQUIRE(e->terminal == Outcome::BUSY);
      REQUIRE_FALSE(e->batchRef);
      REQUIRE(e->restorationBytes == 0);
    }
  }
}

SCENARIO("The oldest eligible batch visibly expires past 32", "[integration]")
{
  LoggedFixture lf({});
  std::vector<BatchRef> refs;
  for (std::uint64_t id = 1; id <= 33; id++) {
    auto r = lf.edit(id, 0.01f * float(id));
    REQUIRE(r.committed);
    refs.push_back(*r.batchRef);
  }
  REQUIRE(lf.log->usage().restorableBatches == 32);
  auto first = lf.log->get(1);
  REQUIRE(first);
  REQUIRE(first->eligibility == activity::Eligibility::EXPIRED);
  REQUIRE(first->restorationBytes == 0);
  REQUIRE(first->batchRef);
  REQUIRE(lf.log->eligibility(refs[0]) == activity::Eligibility::EXPIRED);
  REQUIRE(lf.fx.edits->restorations().expired(refs[0]));
  REQUIRE(lf.log->eligibility(refs[1]) == activity::Eligibility::ELIGIBLE);
  REQUIRE(lf.fx.edits->restorations().find(refs[1]));
}

SCENARIO("Canonical arguments carry the full content of every JSON kind",
    "[integration]")
{
  Fixture fx;
  auto withValue = [&](JsonValue v) {
    return fx.batch({fx.entry(fx.matte.data(), "opacity", std::move(v))});
  };
  auto str = [](std::string s) {
    auto j = JsonValue::ofKind(JsonKind::STRING);
    j.text = std::move(s);
    return j;
  };
  auto boolean = [](bool b) {
    auto j = JsonValue::ofKind(JsonKind::BOOLEAN);
    j.boolean = b;
    return j;
  };
  auto object = [](std::vector<std::pair<std::string, JsonValue>> m) {
    auto j = JsonValue::ofKind(JsonKind::OBJECT);
    j.members = std::move(m);
    return j;
  };
  auto canon = [&](JsonValue v) {
    return EditService::canonicalArguments(withValue(std::move(v)));
  };

  // Values differing only in non-number content canonicalize differently.
  CHECK(canon(str("a")) != canon(str("b")));
  CHECK(canon(str("a;b")) != canon(str("a")));
  CHECK(canon(boolean(true)) != canon(boolean(false)));
  CHECK(
      canon(JsonValue::ofKind(JsonKind::NULL_VALUE)) != canon(boolean(false)));
  CHECK(canon(object({{"k", boolean(true)}}))
      != canon(object({{"k", boolean(false)}})));
  CHECK(canon(object({{"a", str("x")}})) != canon(object({{"b", str("x")}})));
  CHECK(canon(JsonValue::array({str("x")}))
      != canon(JsonValue::array({str("y")})));
  // Objects are key-order independent; identical content is identical.
  CHECK(canon(object({{"a", str("1")}, {"b", str("2")}}))
      == canon(object({{"b", str("2")}, {"a", str("1")}})));
  CHECK(canon(str("same")) == canon(str("same")));

  THEN("reusing a request_id with a different string is not a replay")
  {
    auto first = fx.run(1, withValue(str("red")));
    CHECK(first.outcome == Outcome::INVALID_REQUEST);
    auto again = fx.submit(1, withValue(str("red")));
    CHECK(again.code == Outcome::OK);
    CHECK(again.replayed);
    auto other = fx.submit(1, withValue(str("blue")));
    CHECK(other.code == Outcome::REQUEST_ID_CONFLICT);
    CHECK_FALSE(other.replayed);
  }
}

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "ChessFixture.hpp"
#include "catch.hpp"
#include "vsr/integration/context/ContextAdapter.hpp"
#include "vsr/integration/inspection/Inspection.hpp"
#include "vsr/integration/mcp/Json.hpp"
// vsr
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/activity/Activity.hpp"
#include "vsr/integration/edit/AgentEdit.hpp"
#include "vsr/integration/restore/BatchRestore.hpp"
#include "vsr/integration/sync/SceneSync.hpp"
#include "vsr/scene/Scene.hpp"
#include "vsr/ui/imgui/windows/IntegrationActivityModel.h"
// std
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <random>
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
  int layerSignals{0};
  void signalLayerStructureUpdated(const vsr::scene::Layer *) override
  {
    ++layerSignals;
    if (onParameters)
      onParameters();
  }

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

  Fixture(bool nodeEdits = false)
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
    s.nodeEdits = nodeEdits;
    s.grants.resynchronize = true;
    s.recoveryCallback = [](SceneId) { return true; };
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

  EditEntry nodeEntry(vsr::scene::LayerNodeRef n, bool enabled = false)
  {
    EditEntry e;
    e.targetKind = TargetKind::NODE;
    e.nodeExpected = *identity->nodeEditVersion(identity->handleOf(n));
    e.nodeDependencies = *identity->beginNodeDependencies(e.nodeExpected.node);
    while (e.nodeDependencies.ancestorSlot)
      REQUIRE(identity->advanceNodeDependencies(e.nodeDependencies));
    e.parameter = "enabled";
    e.value = JsonValue::ofKind(JsonKind::BOOLEAN);
    e.value.boolean = enabled;
    return e;
  }

  EditEntry positionEntry(vsr::scene::LayerNodeRef n, JsonValue value)
  {
    auto e = nodeEntry(n);
    e.parameter = "position";
    e.value = std::move(value);
    return e;
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

TEST_CASE("Mixed batches commit and restore one complete scene adjustment",
    "[integration][mixed-edit]")
{
  Fixture f(true);
  auto *layer = f.scene.defaultLayer();
  auto parent = layer->root()->insert_last_child(vsr::scene::LayerNodeData(
      layer, vsr::core::math::IDENTITY_MAT4, "parent"));
  auto a = parent->insert_last_child(vsr::scene::LayerNodeData(
      layer, vsr::core::math::IDENTITY_MAT4, "matrix"));
  auto b = parent->insert_last_child(vsr::scene::LayerNodeData(
      layer, vsr::core::math::IDENTITY_MAT4, "position"));
  auto original = vsr::core::math::IDENTITY_MAT4;
  original[1].x = 3.f;
  original[0].x = -2.f;
  a->value().setAsTransform(original);
  a->value().setCurrentValueAsDefault();
  auto matrix = f.nodeEntry(a);
  matrix.parameter = "transform";
  matrix.value = JsonValue::array({JsonValue::numbers({2, 3, 0, 11}),
      JsonValue::numbers({0, 4, 0, -12}),
      JsonValue::numbers({0, 0, 5, 13}),
      JsonValue::numbers({0, 0, 0, 1})});
  auto batch =
      f.batch({f.entry(f.matte.data(), "opacity", JsonValue::fromNumber(0.5)),
          matrix,
          f.positionEntry(b, JsonValue::numbers({7, 8, 9})),
          f.nodeEntry(a),
          f.entry(
              f.matte.data(), "color", JsonValue::numbers({0.2, 0.3, 0.4}))});
  const auto revision = f.identity->sceneRevision();
  f.recorder->parameterSignals = f.recorder->layerSignals = 0;
  f.recorder->onParameters = [&] {
    CHECK(f.opacity() == 0.5f);
    CHECK(*f.matte->parameterValueAs<float3>("color")
        == float3(0.2f, 0.3f, 0.4f));
    CHECK(a->value().getTransform()[3].xyz() == float3(11, -12, 13));
    CHECK(b->value().getTransform()[3].xyz() == float3(7, 8, 9));
    CHECK_FALSE(a->value().isEnabled());
  };
  auto r = f.run(1, batch);
  REQUIRE(r.outcome == Outcome::OK);
  REQUIRE(r.committed);
  REQUIRE(r.batchRef);
  REQUIRE(r.sceneRevision);
  CHECK_FALSE(*r.sceneRevision == revision);
  REQUIRE(r.versions.size() == 1);
  REQUIRE(r.nodeVersions.size() == 2);
  CHECK(f.identity->isCurrent(r.versions[0]));
  for (const auto &v : r.nodeVersions)
    CHECK(f.identity->isCurrent(v.expected));
  CHECK(f.recorder->parameterSignals == 1);
  CHECK(f.recorder->layerSignals == 1);
  CHECK(f.hookCalls == 1);
  CHECK(f.submit(1, batch).replayed);
  CHECK(f.hookCalls == 1);
  auto other = batch;
  other.edits[0] = f.nodeEntry(b);
  const auto different = f.submit(1, other);
  CHECK_FALSE(different.replayed);
  CHECK(different.code == Outcome::INVALID_REQUEST);

  restore::RestoreService restores(*f.engine, *f.edits);
  restores.attachScene(f.sceneId, *f.identity);
  restores.setFaultInjector([] { return true; });
  const auto parameterSignals = f.recorder->parameterSignals;
  const auto layerSignals = f.recorder->layerSignals;
  auto restore = restores.submitLocal({f.sceneId, *r.batchRef});
  while (f.integration.service()) {
  }
  REQUIRE(restores.resultLocal(restore.ref));
  CHECK(restores.resultLocal(restore.ref)->outcome == Outcome::BUSY);
  CHECK(restores.eligible(*r.batchRef));
  CHECK(f.identity->isCurrent(r.versions[0]));
  for (const auto &v : r.nodeVersions)
    CHECK(f.identity->isCurrent(v.expected));
  CHECK(f.recorder->parameterSignals == parameterSignals);
  CHECK(f.recorder->layerSignals == layerSignals);
  CHECK(f.hookCalls == 1);
  f.recorder->onParameters();

  // Metadata, unedited parameters and defaults are not restored.
  a->value().name() = "keep name";
  f.light->setParameter("irradiance", 4.f);
  auto parentMatrix = vsr::core::math::IDENTITY_MAT4;
  parentMatrix[3].x = 100.f;
  parent->value().setAsTransform(parentMatrix);
  f.recorder->onParameters = [&] {
    CHECK(f.opacity() == 1.f);
    CHECK(*f.matte->parameterValueAs<float3>("color") == float3(1.f));
    CHECK(a->value().getTransform() == original);
    CHECK(b->value().getTransform() == vsr::core::math::IDENTITY_MAT4);
    CHECK(a->value().isEnabled());
  };
  restores.setFaultInjector({});
  f.authority->setAgentEditsDisabled(true);
  f.authority->detach(f.creds);
  restore = restores.submitLocal({f.sceneId, *r.batchRef});
  while (f.integration.service()) {
  }
  auto restored = restores.resultLocal(restore.ref);
  REQUIRE(restored);
  REQUIRE(restored->committed);
  CHECK(restored->versions.size() == 1);
  CHECK(restored->nodeVersions.size() == 2);
  CHECK_FALSE(restores.eligible(*r.batchRef));
  CHECK(f.hookCalls == 2);
  CHECK(a->value().name() == "keep name");
  CHECK(a->value().isDefaultValue());
  CHECK(f.irradiance() == 4.f);
  CHECK(parent->value().getTransform() == parentMatrix);
  restore = restores.submitLocal({f.sceneId, *r.batchRef});
  while (f.integration.service()) {
  }
  CHECK(
      restores.resultLocal(restore.ref)->outcome == Outcome::ALREADY_RESTORED);
}

TEST_CASE("One invalid mixed entry or preparation failure changes nothing",
    "[integration][mixed-edit]")
{
  Fixture f(true);
  auto *layer = f.scene.defaultLayer();
  auto n = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, vsr::core::math::IDENTITY_MAT4, "n"));
  auto batch =
      f.batch({f.entry(f.matte.data(), "opacity", JsonValue::fromNumber(0.5)),
          f.positionEntry(n, JsonValue::numbers({4, 5, 6})),
          f.nodeEntry(n)});
  const auto objectVersion = batch.edits[0].expected;
  const auto nodeVersion = batch.edits[1].nodeExpected;
  const auto revision = f.identity->sceneRevision();
  f.recorder->parameterSignals = f.recorder->layerSignals = 0;
  SECTION("stale object")
  {
    ++batch.edits[0].expected.version;
    CHECK(f.run(1, batch).outcome == Outcome::CONFLICT);
  }
  SECTION("stale node")
  {
    ++batch.edits[1].nodeExpected.version;
    ++batch.edits[2].nodeExpected.version;
    CHECK(f.run(1, batch).outcome == Outcome::CONFLICT);
  }
  SECTION("invalid node value")
  {
    batch.edits[1].value = JsonValue::numbers({4, 5});
    CHECK(f.run(1, batch).outcome == Outcome::INVALID_REQUEST);
  }
  SECTION("invalid object value")
  {
    batch.edits[0].value = JsonValue::fromNumber(2);
    CHECK(f.run(1, batch).outcome == Outcome::INVALID_REQUEST);
  }
  SECTION("object allowlist stays narrow")
  {
    batch.edits[0].parameter = "unsupported";
    CHECK(f.run(1, batch).outcome == Outcome::INVALID_REQUEST);
  }
  SECTION("all preparation stages")
  {
    std::uint64_t id = 1;
    for (auto stage : {FaultStage::PREPARE,
             FaultStage::ALLOCATE,
             FaultStage::RESERVE_RESTORATION}) {
      f.edits->setFaultInjector([stage](FaultStage s) { return s == stage; });
      auto r = f.run(id++, batch);
      CHECK(r.outcome == Outcome::BUSY);
      CHECK_FALSE(r.committed);
      CHECK_FALSE(r.batchRef);
      CHECK(f.identity->isCurrent(objectVersion));
      CHECK(f.identity->isCurrent(nodeVersion));
      CHECK(f.identity->sceneRevision() == revision);
      CHECK(f.recorder->parameterSignals == 0);
      CHECK(f.recorder->layerSignals == 0);
    }
  }
  CHECK(f.opacity() == 1.f);
  CHECK(n->value().getTransform() == vsr::core::math::IDENTITY_MAT4);
  CHECK(n->value().isEnabled());
  CHECK(f.identity->isCurrent(objectVersion));
  CHECK(f.identity->isCurrent(nodeVersion));
  CHECK(f.identity->sceneRevision() == revision);
  CHECK(f.recorder->parameterSignals == 0);
  CHECK(f.recorder->layerSignals == 0);
  CHECK(f.hookCalls == 0);
  CHECK(f.edits->restorations().size() == 0);
}

TEST_CASE(
    "Mixed batches share entry and journal limits and permanent conflicts",
    "[integration][mixed-edit]")
{
  Fixture f(true);
  auto *layer = f.scene.defaultLayer();
  std::vector<EditEntry> entries{
      f.entry(f.matte.data(), "opacity", JsonValue::fromNumber(0.5))};
  for (int i = 0; i < 63; ++i) {
    auto n =
        layer->root()->insert_last_child(vsr::scene::LayerNodeData(layer, "n"));
    entries.push_back(f.nodeEntry(n));
  }
  auto oversized = entries;
  oversized.push_back(
      f.entry(f.light.data(), "irradiance", JsonValue::fromNumber(2)));
  CHECK(f.submit(1, f.batch(oversized)).code == Outcome::INVALID_REQUEST);
  auto r = f.run(1, f.batch(entries));
  REQUIRE(r.committed);
  REQUIRE(r.batchRef);
  CHECK(r.versions.size() == 1);
  CHECK(r.nodeVersions.size() == 63);
  restore::RestoreService restores(*f.engine, *f.edits);
  restores.attachScene(f.sceneId, *f.identity);
  REQUIRE(restores.eligible(*r.batchRef));
  SECTION("object change-back permanently conflicts the whole journal")
  {
    f.matte->setParameter("opacity", 0.25f);
    f.matte->setParameter("opacity", 0.5f);
  }
  SECTION("node change-back permanently conflicts the whole journal")
  {
    auto n = layer->root()->next();
    n->value().setEnabled(true);
    n->value().setEnabled(false);
  }
  auto a = restores.submitLocal({f.sceneId, *r.batchRef});
  while (f.integration.service()) {
  }
  REQUIRE(restores.resultLocal(a.ref));
  CHECK(restores.resultLocal(a.ref)->outcome == Outcome::CONFLICT);
  CHECK_FALSE(restores.resultLocal(a.ref)->committed);
  CHECK(restores.conflicting(*r.batchRef));
  CHECK_FALSE(restores.eligible(*r.batchRef));
  CHECK(f.opacity() == 0.5f);
  for (const auto &e : entries) {
    if (e.targetKind == TargetKind::NODE)
      CHECK_FALSE(
          f.identity->resolve(e.nodeExpected.node)->value().isEnabled());
  }
  a = restores.submitLocal({f.sceneId, *r.batchRef});
  while (f.integration.service()) {
  }
  CHECK(restores.resultLocal(a.ref)->outcome == Outcome::CONFLICT);
}

TEST_CASE("Mixed edits preserve exposure authority and honest synchronization",
    "[integration][mixed-edit]")
{
  const bool exposed = GENERATE(false, true);
  Fixture f(exposed);
  auto *layer = f.scene.defaultLayer();
  auto n =
      layer->root()->insert_last_child(vsr::scene::LayerNodeData(layer, "n"));
  auto batch =
      f.batch({f.entry(f.matte.data(), "opacity", JsonValue::fromNumber(0.5)),
          f.nodeEntry(n)});
  if (!exposed) {
    CHECK(f.run(1, batch).outcome == Outcome::FORBIDDEN);
    CHECK(f.opacity() == 1.f);
    CHECK(n->value().isEnabled());
    CHECK(f.hookCalls == 0);
    return;
  }
  SECTION("edit disabled between admission and execution")
  {
    REQUIRE(f.submit(1, batch).code == Outcome::OK);
    f.authority->setAgentEditsDisabled(true);
    while (f.integration.service()) {
    }
    const auto status = f.engine->status(f.creds, f.ref(1));
    CHECK(status.terminal == Outcome::FORBIDDEN);
    CHECK_FALSE(status.committed);
    CHECK(f.opacity() == 1.f);
    CHECK(n->value().isEnabled());
  }
  SECTION("disconnect before execution")
  {
    REQUIRE(f.submit(1, batch).code == Outcome::OK);
    f.authority->detach(f.creds);
    while (f.integration.service()) {
    }
    CHECK(f.opacity() == 1.f);
    CHECK(n->value().isEnabled());
    CHECK(f.hookCalls == 0);
    CHECK(f.edits->restorations().size() == 0);
  }
  SECTION("local edit then newly attached session restore")
  {
    f.authority->setAgentEditsDisabled(true);
    f.authority->detach(f.creds);
    auto a = f.edits->submitLocal(batch);
    REQUIRE(a.code == Outcome::OK);
    while (f.integration.service()) {
    }
    auto r = f.edits->resultLocal(a.ref);
    REQUIRE(r);
    REQUIRE(r->committed);
    REQUIRE(r->batchRef);
    f.authority->setAgentEditsDisabled(false);
    auto attached = f.authority->attach(f.authority->launchCredential());
    REQUIRE(attached.code == session::Code::OK);
    operations::Credentials credentials{f.authority->launchCredential(),
        attached.sessionId,
        attached.sessionSecret};
    restore::RestoreService restores(*f.engine, *f.edits);
    restores.attachScene(f.sceneId, *f.identity);
    operations::OperationRef ref{attached.sessionId, 1};
    REQUIRE(restores.submit(credentials, ref, {f.sceneId, *r->batchRef}).code
        == Outcome::OK);
    while (f.integration.service()) {
    }
    REQUIRE(restores.result(ref));
    CHECK(restores.result(ref)->committed);
    CHECK(f.opacity() == 1.f);
    CHECK(n->value().isEnabled());
    CHECK(
        restores.submit(credentials, ref, {f.sceneId, *r->batchRef}).replayed);
  }
  SECTION("sync failure is committed and still restorable")
  {
    f.hook = [] { return false; };
    auto r = f.run(1, batch);
    REQUIRE(r.committed);
    REQUIRE(r.batchRef);
    CHECK(r.outcome == Outcome::APPLIED_WITH_SYNC_FAILURE);
    CHECK(f.opacity() == 0.5f);
    CHECK_FALSE(n->value().isEnabled());
    restore::RestoreService restores(*f.engine, *f.edits);
    restores.attachScene(f.sceneId, *f.identity);
    REQUIRE(restores.eligible(*r.batchRef));
    auto a = restores.submitLocal({f.sceneId, *r.batchRef});
    while (f.integration.service()) {
    }
    REQUIRE(restores.resultLocal(a.ref));
    CHECK(restores.resultLocal(a.ref)->committed);
    CHECK(restores.resultLocal(a.ref)->outcome
        == Outcome::APPLIED_WITH_SYNC_FAILURE);
    CHECK_FALSE(restores.eligible(*r.batchRef));
    CHECK(f.opacity() == 1.f);
    CHECK(n->value().isEnabled());
  }
}

TEST_CASE("Mixed journals use the shared bounded restoration store",
    "[integration][mixed-edit]")
{
  Fixture f(true);
  auto *layer = f.scene.defaultLayer();
  auto n =
      layer->root()->insert_last_child(vsr::scene::LayerNodeData(layer, "n"));
  std::optional<BatchRef> first;
  for (std::uint64_t id = 1; id <= 33; ++id) {
    auto r = f.run(id,
        f.batch({f.entry(f.matte.data(),
                     "opacity",
                     JsonValue::fromNumber(id % 2 ? 0.5 : 1)),
            f.nodeEntry(n, id % 2 == 0)}));
    REQUIRE(r.committed);
    REQUIRE(r.batchRef);
    if (!first)
      first = r.batchRef;
  }
  restore::RestoreService restores(*f.engine, *f.edits);
  restores.attachScene(f.sceneId, *f.identity);
  CHECK_FALSE(restores.eligible(*first));
  auto a = restores.submitLocal({f.sceneId, *first});
  while (f.integration.service()) {
  }
  REQUIRE(restores.resultLocal(a.ref));
  CHECK(restores.resultLocal(a.ref)->outcome == Outcome::OUTCOME_UNAVAILABLE);
  CHECK(f.opacity() == 0.5f);
  CHECK_FALSE(n->value().isEnabled());
}

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
        REQUIRE(
            status.result.find("\"version\":" + std::to_string(v.version) + "}")
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

  explicit LoggedFixture(activity::Limits limits, bool nodeEdits = false)
      : fx(nodeEdits)
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

TEST_CASE("Mixed restoration payloads count against the shared activity budget",
    "[integration][mixed-edit]")
{
  auto mixed = [](LoggedFixture &lf, std::uint64_t id) {
    auto &f = lf.fx;
    auto *layer = f.scene.defaultLayer();
    auto n =
        layer->root()->insert_last_child(vsr::scene::LayerNodeData(layer, "n"));
    return f.run(id,
        f.batch({f.entry(f.matte.data(), "opacity", JsonValue::fromNumber(0.5)),
            f.nodeEntry(n)}));
  };
  LoggedFixture measured({}, true);
  auto r = mixed(measured, 1);
  REQUIRE(r.committed);
  auto record = measured.log->get(1);
  REQUIRE(record);
  REQUIRE(record->batchRef);
  CHECK(*record->batchRef == *r.batchRef);
  CHECK(record->restorationBytes > 0);
  CHECK(measured.log->usage().restorableBatches == 1);
  const auto cost = activity::ActivityLog::cost(*record);
  const auto resultBytes = record->result.size();
  measured.holdActive();
  const auto active = measured.log->usage().bytes - cost;

  activity::Limits limits;
  limits.bytes = active + cost - resultBytes - 1;
  LoggedFixture constrained(limits, true);
  constrained.holdActive();
  r = mixed(constrained, 1);
  CHECK(r.outcome == Outcome::BUSY);
  CHECK_FALSE(r.committed);
  CHECK_FALSE(r.batchRef);
  CHECK(constrained.fx.opacity() == 1.f);
  CHECK(
      constrained.fx.scene.defaultLayer()->root()->next()->value().isEnabled());
  CHECK(constrained.fx.hookCalls == 0);
  CHECK(constrained.log->usage().restorableBatches == 0);
  CHECK(constrained.log->usage().bytes <= limits.bytes);
}

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

TEST_CASE("Full local matrix preserves affine state and restores cached SRT",
    "[integration][node-edit][node-matrix]")
{
  Fixture f(true);
  auto *layer = f.scene.addLayer("matrix");
  auto n = layer->root()->insert_last_child(vsr::scene::LayerNodeData(
      layer, vsr::core::math::IDENTITY_MAT4, "placement"));
  // Keep an unnormalized angle cache that cannot be recovered by decomposition.
  const vsr::core::math::mat3 srt{
      {2.f, 3.f, 4.f}, {450.f, 0.f, 0.f}, {5.f, 6.f, 7.f}};
  n->value().setAsTransform(srt);
  SECTION("prior SRT angles are retained exactly") {}
  SECTION("prior shear matrix is retained exactly")
  {
    auto shear = vsr::core::math::IDENTITY_MAT4;
    shear[1].x = 2.5f;
    shear[0].x = -3.f;
    n->value().setAsTransform(shear);
  }
  SECTION("prior singular matrix and nonfinite decomposition cache")
  {
    auto singular = vsr::core::math::IDENTITY_MAT4;
    singular[0].x = 0.f;
    n->value().setAsTransform(singular);
  }
  const auto beforeSRT = n->value().getTransformSRT();
  const auto before = n->value().getTransform();
  f.scene.signalLayerStructureChanged(layer);
  auto matrix = f.nodeEntry(n);
  matrix.parameter = "transform";
  matrix.value = JsonValue::array({JsonValue::numbers({-2, 3, 0, 11}),
      JsonValue::numbers({0, 0, 0, 13}),
      JsonValue::numbers({0, 1, 4, 17}),
      JsonValue::numbers({0, 0, 0, 1})});
  auto enabled = f.nodeEntry(n, false);
  auto result = f.run(1, f.batch({matrix, enabled}));
  REQUIRE(result.committed);
  REQUIRE(result.batchRef);
  CHECK(result.nodeVersions.size() == 1);
  const auto after = n->value().getTransform();
  CHECK(after[0].x == -2.f);
  CHECK(after[1].x == 3.f);
  CHECK(after[1].z == 1.f);
  CHECK(after[3].x == 11.f);
  CHECK(after[3].y == 13.f);
  CHECK(after[3].z == 17.f);
  CHECK_FALSE(n->value().isEnabled());
  restore::RestoreService restores(*f.engine, *f.edits);
  restores.attachScene(f.sceneId, *f.identity);
  const auto admission = restores.submitLocal({f.sceneId, *result.batchRef});
  REQUIRE(admission.code == Outcome::OK);
  while (f.integration.service()) {
  }
  REQUIRE(restores.resultLocal(admission.ref)->committed);
  const auto restored = n->value().getTransform();
  const auto restoredSRT = n->value().getTransformSRT();
  CHECK(std::memcmp(&restored, &before, sizeof(before)) == 0);
  CHECK(std::memcmp(&restoredSRT, &beforeSRT, sizeof(beforeSRT)) == 0);
  CHECK(n->value().isEnabled());
  n->value().setToDefaultValue();
  const auto baseline = n->value().getTransform();
  CHECK(baseline[0].x == 1.f);
  CHECK(baseline[3].x == 0.f);
}

TEST_CASE("Matrix rejection and preparation leave the whole batch unchanged",
    "[integration][node-edit][node-matrix]")
{
  Fixture f(true);
  auto *l = f.scene.addLayer("matrix-validation");
  auto n = l->root()->insert_last_child(
      vsr::scene::LayerNodeData(l, vsr::core::math::IDENTITY_MAT4, "n"));
  f.scene.signalLayerStructureChanged(l);
  auto e = f.nodeEntry(n);
  e.parameter = "transform";
  e.value = JsonValue::array({JsonValue::numbers({1, 2, 3, 4}),
      JsonValue::numbers({0, 5, 6, 7}),
      JsonValue::numbers({0, 0, -8, 9}),
      JsonValue::numbers({0, 0, 0, 1})});
  const auto version = e.nodeExpected;
  const auto revision = f.identity->sceneRevision();
  f.recorder->layerSignals = 0;
  ValueError error = ValueError::NONE;
  SECTION("projective tiny value cannot round to affine zero")
  {
    e.value.elements[3].elements[0] = JsonValue::fromNumber(1e-100);
    error = ValueError::NON_AFFINE;
  }
  SECTION("projective near-one cannot round to affine one")
  {
    e.value.elements[3].elements[3] = JsonValue::fromNumber(1.0000000000000002);
    error = ValueError::NON_AFFINE;
  }
  SECTION("overflow")
  {
    e.value.elements[0].elements[0] = JsonValue::fromNumber(1e100);
    error = ValueError::OUT_OF_DOMAIN;
  }
  SECTION("nonfinite")
  {
    e.value.elements[0].elements[0] =
        JsonValue::fromNumber(std::numeric_limits<double>::infinity());
    error = ValueError::NON_FINITE;
  }
  SECTION("wrong shape")
  {
    e.value.elements[2].elements.pop_back();
    error = ValueError::INCOMPATIBLE_TYPE;
  }
  SECTION("wrong scalar type")
  {
    e.value.elements[0].elements[0] = JsonValue::ofKind(JsonKind::BOOLEAN);
    error = ValueError::INCOMPATIBLE_TYPE;
  }
  SECTION("wrong node kind")
  {
    n->value().setEmpty();
    e = f.nodeEntry(n);
    e.parameter = "transform";
    e.value = JsonValue::numbers({1, 2, 3});
    error = ValueError::UNSUPPORTED_NODE_KIND;
  }
  SECTION("all fallible preparation stages")
  {
    for (auto stage : {FaultStage::PREPARE,
             FaultStage::ALLOCATE,
             FaultStage::RESERVE_RESTORATION}) {
      f.edits->setFaultInjector([stage](FaultStage s) { return s == stage; });
      auto r = f.run(10 + static_cast<int>(stage), f.batch({e}));
      CHECK(r.outcome == Outcome::BUSY);
      CHECK_FALSE(r.committed);
      CHECK_FALSE(r.batchRef);
    }
  }
  SECTION("reserved convenience conflicts and duplicates")
  {
    for (auto field : {"position", "rotation", "scale", "transform"}) {
      auto other = e;
      other.parameter = field;
      CHECK(f.submit(1, f.batch({e, other})).code == Outcome::INVALID_REQUEST);
      CHECK(f.submit(2, f.batch({other, e})).code == Outcome::INVALID_REQUEST);
    }
  }
  if (error != ValueError::NONE) {
    auto r = f.run(1, f.batch({f.nodeEntry(n, false), e}));
    CHECK(r.outcome == Outcome::INVALID_REQUEST);
    CHECK(r.valueError == error);
    CHECK(r.failedEntry == 1);
    CHECK_FALSE(r.committed);
    CHECK_FALSE(r.batchRef);
  }
  CHECK(n->value().isEnabled());
  CHECK(f.recorder->layerSignals == 0);
  CHECK(f.hookCalls == 0);
  CHECK(f.edits->restorations().size() == 0);
  if (error != ValueError::UNSUPPORTED_NODE_KIND) {
    CHECK(f.identity->isCurrent(version));
    CHECK(f.identity->sceneRevision() == revision);
    CHECK(n->value().getTransform()[1].x == 0.f);
  }
}

TEST_CASE(
    "Matrix journals keep narrow conflicts and permanent restore outcomes",
    "[integration][node-edit][node-matrix]")
{
  Fixture f(true);
  auto *l = f.scene.addLayer("matrix-history");
  auto parent = l->root()->insert_last_child(
      vsr::scene::LayerNodeData(l, vsr::core::math::IDENTITY_MAT4, "parent"));
  auto n = parent->insert_last_child(
      vsr::scene::LayerNodeData(l, vsr::core::math::IDENTITY_MAT4, "n"));
  auto sibling =
      parent->insert_last_child(vsr::scene::LayerNodeData(l, "sibling"));
  f.scene.signalLayerStructureChanged(l);
  auto e = f.nodeEntry(n);
  e.parameter = "transform";
  e.value = JsonValue::array({JsonValue::numbers({-1, 2, 0, 5}),
      JsonValue::numbers({0, 3, 0, 6}),
      JsonValue::numbers({0, 0, 4, 7}),
      JsonValue::numbers({0, 0, 0, 1})});
  // A live parent's transform is not a local edit dependency.
  auto translated = vsr::core::math::IDENTITY_MAT4;
  translated[3].x = 100.f;
  parent->value().setAsTransform(translated);
  f.scene.signalLayerTransformChanged(l);
  f.recorder->layerSignals = 0;
  f.recorder->onParameters = [&] {
    CHECK(n->value().getTransform()[3].x == 5.f);
    CHECK_FALSE(sibling->value().isEnabled());
  };
  auto b = f.batch({e, f.nodeEntry(sibling, false)});
  auto r = f.run(1, b);
  REQUIRE(r.committed);
  REQUIRE(r.batchRef);
  CHECK(f.recorder->layerSignals == 1);
  CHECK(f.submit(1, b).replayed);
  restore::RestoreService restores(*f.engine, *f.edits);
  restores.attachScene(f.sceneId, *f.identity);
  f.recorder->onParameters = {};
  bool conflict = false;
  SECTION("unrelated metadata and ancestor transforms survive restoration")
  {
    n->value().name() = "renamed";
    translated[3].x = 200.f;
    parent->value().setAsTransform(translated);
    f.scene.signalLayerTransformChanged(l);
  }
  SECTION("target transform change-back permanently conflicts")
  {
    const auto after = n->value().getTransform();
    n->value().setAsTransform(vsr::core::math::IDENTITY_MAT4);
    n->value().setAsTransform(after);
    f.scene.signalLayerTransformChanged(l);
    conflict = true;
  }
  SECTION("restore preparation failure retains eligibility")
  {
    restores.setFaultInjector([] { return true; });
    auto failed = restores.submitLocal({f.sceneId, *r.batchRef});
    REQUIRE(failed.code == Outcome::OK);
    while (f.integration.service()) {
    }
    CHECK(restores.resultLocal(failed.ref)->outcome == Outcome::BUSY);
    CHECK(restores.eligible(*r.batchRef));
    CHECK(n->value().getTransform()[3].x == 5.f);
    restores.setFaultInjector({});
  }
  auto restore = restores.submitLocal({f.sceneId, *r.batchRef});
  REQUIRE(restore.code == Outcome::OK);
  while (f.integration.service()) {
  }
  auto result = restores.resultLocal(restore.ref);
  REQUIRE(result);
  CHECK(result->outcome == (conflict ? Outcome::CONFLICT : Outcome::OK));
  CHECK_FALSE(restores.eligible(*r.batchRef));
  CHECK(n->value().getTransform()[3].x == (conflict ? 5.f : 0.f));
  CHECK(sibling->value().isEnabled() == !conflict);
  auto again = restores.submitLocal({f.sceneId, *r.batchRef});
  REQUIRE(again.code == Outcome::OK);
  while (f.integration.service()) {
  }
  CHECK(restores.resultLocal(again.ref)->outcome
      == (conflict ? Outcome::CONFLICT : Outcome::ALREADY_RESTORED));
}

TEST_CASE("enabled nodes commit atomically and restore exactly",
    "[integration][node-edit]")
{
  Fixture f(true);
  auto *layer = f.scene.addLayer("nodes");
  auto a =
      layer->root()->insert_last_child(vsr::scene::LayerNodeData(layer, "a"));
  auto b = a->insert_last_child(vsr::scene::LayerNodeData(layer, "b"));
  f.scene.signalLayerStructureChanged(layer);
  auto entry = [&](vsr::scene::LayerNodeRef n) {
    EditEntry e;
    e.targetKind = TargetKind::NODE;
    e.nodeExpected = *f.identity->nodeEditVersion(f.identity->handleOf(n));
    e.nodeDependencies =
        *f.identity->beginNodeDependencies(e.nodeExpected.node);
    while (e.nodeDependencies.ancestorSlot)
      REQUIRE(f.identity->advanceNodeDependencies(e.nodeDependencies));
    e.parameter = "enabled";
    e.value = JsonValue::ofKind(JsonKind::BOOLEAN);
    e.value.boolean = false;
    return e;
  };
  auto batch = f.batch({entry(a), entry(b)});
  f.recorder->layerSignals = 0;
  f.recorder->onParameters = [&] {
    CHECK_FALSE(a->value().isEnabled());
    CHECK_FALSE(b->value().isEnabled());
  };
  auto r = f.run(1, batch);
  REQUIRE(r.committed);
  REQUIRE(r.batchRef);
  CHECK(r.versions.empty());
  CHECK(r.nodeVersions.size() == 2);
  CHECK(f.recorder->layerSignals == 1);
  CHECK(f.hookCalls == 1);
  auto replay = f.submit(1, batch);
  CHECK(replay.replayed);
  restore::RestoreService restores(*f.engine, *f.edits);
  restores.attachScene(f.sceneId, *f.identity);
  f.recorder->onParameters = [&] {
    CHECK(a->value().isEnabled());
    CHECK(b->value().isEnabled());
  };
  auto restore = restores.submitLocal({f.sceneId, *r.batchRef});
  REQUIRE(restore.code == Outcome::OK);
  while (f.integration.service()) {
  }
  REQUIRE(restores.resultLocal(restore.ref));
  CHECK(restores.resultLocal(restore.ref)->committed);
  CHECK_FALSE(restores.eligible(*r.batchRef));
  CHECK(restores.resultLocal(restore.ref)->nodeVersions.size() == 2);
  CHECK(restores.resultLocal(restore.ref)->versions.empty());
  CHECK(f.recorder->layerSignals == 2);
  CHECK(f.hookCalls == 2);
}

TEST_CASE("Node validation and preparation never partially commit",
    "[integration][node-edit]")
{
  Fixture f(true);
  auto *l = f.scene.addLayer("nodes");
  auto a = l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "a"));
  auto b = l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "b"));
  f.scene.signalLayerStructureChanged(l);
  const bool position = GENERATE(false, true);
  if (position) {
    a->value().setAsTransform(vsr::core::math::IDENTITY_MAT4);
    b->value().setAsTransform(vsr::core::math::IDENTITY_MAT4);
  }
  auto moved = JsonValue::array({JsonValue::fromNumber(4),
      JsonValue::fromNumber(-5),
      JsonValue::fromNumber(6)});
  auto batch = position
      ? f.batch({f.positionEntry(a, moved), f.positionEntry(b, moved)})
      : f.batch({f.nodeEntry(a), f.nodeEntry(b)});
  const auto version = batch.edits[0].nodeExpected;
  const auto revision = f.identity->sceneRevision();
  f.recorder->layerSignals = 0;
  SECTION("stale second entry")
  {
    ++batch.edits[1].nodeExpected.version;
    CHECK(f.run(1, batch).outcome == Outcome::CONFLICT);
  }
  SECTION("all preparation failure stages")
  {
    std::uint64_t id = 1;
    for (auto stage : {FaultStage::PREPARE,
             FaultStage::ALLOCATE,
             FaultStage::RESERVE_RESTORATION}) {
      f.edits->setFaultInjector([stage](FaultStage s) { return s == stage; });
      auto r = f.run(id++, batch);
      CHECK(r.outcome == Outcome::BUSY);
      CHECK_FALSE(r.committed);
      CHECK_FALSE(r.batchRef);
    }
  }
  SECTION("malformed, duplicate and oversized envelopes")
  {
    auto bad = batch;
    bad.edits[1].value = JsonValue::fromNumber(0);
    if (position)
      CHECK(f.run(1, bad).outcome == Outcome::INVALID_REQUEST);
    else
      CHECK(f.submit(1, bad).code == Outcome::INVALID_REQUEST);
    bad = batch;
    bad.edits[1].nodeDependencies.structuralVersion = 0;
    CHECK(f.submit(2, bad).code == Outcome::INVALID_REQUEST);
    bad = batch;
    bad.edits.push_back(bad.edits[0]);
    CHECK(f.submit(3, bad).code == Outcome::INVALID_REQUEST);
    bad.edits.resize(65, batch.edits[0]);
    CHECK(f.submit(4, bad).code == Outcome::INVALID_REQUEST);
  }
  CHECK(a->value().isEnabled());
  CHECK(b->value().isEnabled());
  if (position) {
    CHECK(a->value().getTransform() == vsr::core::math::IDENTITY_MAT4);
    CHECK(b->value().getTransform() == vsr::core::math::IDENTITY_MAT4);
  }
  CHECK(f.identity->isCurrent(version));
  CHECK(f.identity->sceneRevision() == revision);
  CHECK(f.recorder->layerSignals == 0);
  CHECK(f.hookCalls == 0);
  CHECK(f.edits->restorations().size() == 0);
}

TEST_CASE("Grouped SRT publishes final parent child dependencies and restores",
    "[integration][node-edit][node-srt]")
{
  for (bool childFirst : {false, true}) {
    Fixture f(true);
    auto *l = f.scene.defaultLayer();
    auto parent = l->root()->insert_last_child(
        vsr::scene::LayerNodeData(l, vsr::math::IDENTITY_MAT4));
    auto child = parent->insert_last_child(
        vsr::scene::LayerNodeData(l, vsr::math::IDENTITY_MAT4));
    f.scene.signalLayerStructureChanged(l);
    auto rotation = f.nodeEntry(parent);
    rotation.parameter = "rotation";
    rotation.value = JsonValue::ofKind(JsonKind::OBJECT);
    rotation.value.members = {{"azimuth", JsonValue::fromNumber(90)},
        {"elevation", JsonValue::fromNumber(0)},
        {"roll", JsonValue::fromNumber(0)}};
    auto scale = f.nodeEntry(parent);
    scale.parameter = "scale";
    scale.value = JsonValue::numbers({2, 3, 4});
    auto position = f.positionEntry(child, JsonValue::numbers({5, 6, 7}));
    auto enabled = f.nodeEntry(child);
    auto entries = childFirst
        ? std::vector<EditEntry>{position, enabled, scale, rotation}
        : std::vector<EditEntry>{rotation, scale, enabled, position};
    f.recorder->layerSignals = 0;
    f.recorder->onParameters = [&] {
      const auto matrix = parent->value().getTransform();
      CHECK(matrix[0][2] == Approx(-2).margin(1e-6));
      CHECK(matrix[2][0] == Approx(4).margin(1e-6));
      CHECK(child->value().getTransform()[3] == vsr::math::float4(5, 6, 7, 1));
      CHECK_FALSE(child->value().isEnabled());
    };
    auto result = f.run(1, f.batch(entries));
    REQUIRE(result.committed);
    REQUIRE(result.nodeVersions.size() == 2);
    CHECK(f.recorder->layerSignals == 1);
    for (const auto &version : result.nodeVersions) {
      CHECK(f.identity->isCurrent(version.expected));
      auto live = *f.identity->beginNodeDependencies(version.expected.node);
      while (live.ancestorSlot)
        REQUIRE(f.identity->advanceNodeDependencies(live));
      CHECK(live.matchesAncestry(version.dependencies));
    }
    f.recorder->onParameters = {};
    restore::RestoreService restores(*f.engine, *f.edits);
    restores.attachScene(f.sceneId, *f.identity);
    auto admitted = restores.submitLocal({f.sceneId, *result.batchRef});
    REQUIRE(admitted.code == Outcome::OK);
    while (f.integration.service()) {
    }
    auto restored = restores.resultLocal(admitted.ref);
    REQUIRE(restored->committed);
    for (const auto &version : restored->nodeVersions) {
      CHECK(f.identity->isCurrent(version.expected));
      auto live = *f.identity->beginNodeDependencies(version.expected.node);
      while (live.ancestorSlot)
        REQUIRE(f.identity->advanceNodeDependencies(live));
      CHECK(live.matchesAncestry(version.dependencies));
    }
    CHECK(parent->value().getTransform() == vsr::math::IDENTITY_MAT4);
    CHECK(child->value().getTransform() == vsr::math::IDENTITY_MAT4);
    CHECK(child->value().isEnabled());
  }
}

TEST_CASE("Local rotation uses named degree angles and preserves translation",
    "[integration][node-edit][node-srt]")
{
  Fixture f(true);
  auto *l = f.scene.defaultLayer();
  auto n = l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "rotate"));
  auto original = vsr::math::IDENTITY_MAT4;
  original[3] = {5.f, -6.f, 7.f, 1.f};
  n->value().setAsTransform(original);
  n->value().setCurrentValueAsDefault();
  auto cached = n->value().getTransformSRT();
  f.scene.signalLayerStructureChanged(l);
  auto e = f.nodeEntry(n);
  e.parameter = "rotation";
  e.value = JsonValue::ofKind(JsonKind::OBJECT);
  e.value.members = {{"azimuth", JsonValue::fromNumber(450)},
      {"elevation", JsonValue::fromNumber(90)},
      {"roll", JsonValue::fromNumber(90)}};
  auto result = f.run(1, f.batch({e}));
  REQUIRE(result.committed);
  auto m = n->value().getTransform();
  // Y(90) X(90) Z(90): columns (1,0,0), (0,0,1), (0,-1,0).
  const vsr::math::float3 columns[] = {{1, 0, 0}, {0, 0, 1}, {0, -1, 0}};
  for (int c = 0; c < 3; ++c)
    for (int r = 0; r < 3; ++r)
      CHECK(m[c][r] == Approx(columns[c][r]).margin(2e-6));
  CHECK(m[3] == original[3]);
  CHECK(n->value().getTransformSRT()[1] == vsr::math::float3(450, 90, 90));
  restore::RestoreService restores(*f.engine, *f.edits);
  restores.attachScene(f.sceneId, *f.identity);
  auto adm = restores.submitLocal({f.sceneId, *result.batchRef});
  REQUIRE(adm.code == Outcome::OK);
  while (f.integration.service()) {
  }
  REQUIRE(restores.resultLocal(adm.ref)->committed);
  auto restored = n->value().getTransform();
  auto restoredSrt = n->value().getTransformSRT();
  CHECK(std::memcmp(&restored, &original, sizeof(original)) == 0);
  CHECK(std::memcmp(&restoredSrt, &cached, sizeof(cached)) == 0);
  CHECK(n->value().isDefaultValue());
}

TEST_CASE("SRT groups are order independent with faithful signed zero scales",
    "[integration][node-edit][node-srt]")
{
  std::vector<int> order{0, 1, 2, 3};
  do {
    Fixture f(true);
    auto *l = f.scene.defaultLayer();
    vsr::math::mat3 srt;
    srt[0] = {-2, 0, 4};
    srt[1] = {720, 0, 0};
    srt[2] = {5, -6, 7};
    auto n = l->root()->insert_last_child(vsr::scene::LayerNodeData(l, srt));
    auto original = n->value().getTransform();
    n->value().setCurrentValueAsDefault();
    f.scene.signalLayerStructureChanged(l);
    f.recorder->layerSignals = 0;
    auto rotation = f.nodeEntry(n);
    rotation.parameter = "rotation";
    rotation.value = JsonValue::ofKind(JsonKind::OBJECT);
    rotation.value.members = {{"azimuth", JsonValue::fromNumber(90)},
        {"elevation", JsonValue::fromNumber(0)},
        {"roll", JsonValue::fromNumber(0)}};
    auto scale = f.nodeEntry(n);
    scale.parameter = "scale";
    scale.value = JsonValue::numbers({-3, 0, 2});
    auto position = f.positionEntry(n, JsonValue::numbers({11, 12, 13}));
    std::vector<EditEntry> fields{rotation, scale, position, f.nodeEntry(n)};
    std::vector<EditEntry> entries;
    for (auto i : order)
      entries.push_back(fields[i]);
    f.recorder->onParameters = [&] {
      auto m = n->value().getTransform();
      CHECK(m[0][2] == Approx(3).margin(1e-6));
      CHECK(m[2][0] == Approx(2).margin(1e-6));
      CHECK(m[3] == vsr::math::float4(11, 12, 13, 1));
      CHECK_FALSE(n->value().isEnabled());
    };
    auto result = f.run(1, f.batch(entries));
    REQUIRE(result.committed);
    REQUIRE(result.nodeVersions.size() == 1);
    CHECK(f.identity->isCurrent(result.nodeVersions[0].expected));
    auto m = n->value().getTransform();
    const vsr::math::float3 expected[] = {{0, 0, 3}, {0, 0, 0}, {2, 0, 0}};
    for (int c = 0; c < 3; ++c)
      for (int r = 0; r < 3; ++r)
        CHECK(m[c][r] == Approx(expected[c][r]).margin(1e-6));
    CHECK(n->value().getTransformSRT()[0] == vsr::math::float3(-3, 0, 2));
    CHECK(f.recorder->layerSignals == 1);
    CHECK(f.hookCalls == 1);
    f.recorder->onParameters = {};
    restore::RestoreService restores(*f.engine, *f.edits);
    restores.attachScene(f.sceneId, *f.identity);
    auto adm = restores.submitLocal({f.sceneId, *result.batchRef});
    REQUIRE(adm.code == Outcome::OK);
    while (f.integration.service()) {
    }
    REQUIRE(restores.resultLocal(adm.ref)->committed);
    auto restored = n->value().getTransform();
    auto restoredSrt = n->value().getTransformSRT();
    CHECK(std::memcmp(&restored, &original, sizeof(original)) == 0);
    CHECK(std::memcmp(&restoredSrt, &srt, sizeof(srt)) == 0);
    CHECK(n->value().isDefaultValue());
    CHECK(n->value().isEnabled());
  } while (std::next_permutation(order.begin(), order.end()));
}

TEST_CASE("SRT rejects unfaithful caches and invalid values without any writes",
    "[integration][node-edit][node-srt]")
{
  Fixture f(true);
  auto *l = f.scene.defaultLayer();
  auto n = l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "srt"));
  auto matrix = vsr::math::IDENTITY_MAT4;
  auto cache = n->value().getTransformSRT();
  SECTION("shear")
  {
    matrix[1][0] = 0.2f;
  }
  SECTION("unfaithful reflection")
  {
    matrix[0][0] = -1;
  }
  SECTION("unfaithful degeneracy")
  {
    matrix[0][0] = 0;
  }
  SECTION("nonfinite cache")
  {
    cache[1][0] = std::numeric_limits<float>::infinity();
  }
  SECTION("out of tolerance")
  {
    matrix[0][0] += 2e-5f;
  }
  n->value().setTransformState(matrix, cache);
  f.scene.signalLayerStructureChanged(l);
  f.recorder->layerSignals = 0;
  auto e = f.nodeEntry(n);
  e.parameter = "scale";
  e.value = JsonValue::numbers({1, 2, 3});
  const auto version = e.nodeExpected;
  auto result = f.run(1, f.batch({f.nodeEntry(n), e}));
  CHECK(result.outcome == Outcome::INVALID_REQUEST);
  CHECK_FALSE(result.committed);
  CHECK_FALSE(result.batchRef);
  CHECK(n->value().getTransform() == matrix);
  CHECK(n->value().isEnabled());
  CHECK(f.identity->isCurrent(version));
  CHECK(f.hookCalls == 0);
  CHECK(f.recorder->layerSignals == 0);
  CHECK(f.edits->restorations().size() == 0);
}

TEST_CASE("SRT validates field shapes finite range and envelope exclusions",
    "[integration][node-edit][node-srt]")
{
  Fixture f(true);
  auto *l = f.scene.defaultLayer();
  auto n = l->root()->insert_last_child(
      vsr::scene::LayerNodeData(l, vsr::math::IDENTITY_MAT4));
  f.scene.signalLayerStructureChanged(l);
  auto e = f.nodeEntry(n);
  e.parameter = "scale";
  e.value = JsonValue::numbers({1, 2, 3});
  SECTION("overflow")
  {
    e.value.elements[0] = JsonValue::fromNumber(1e100);
  }
  SECTION("infinity")
  {
    e.value.elements[0] =
        JsonValue::fromNumber(std::numeric_limits<double>::infinity());
  }
  SECTION("NaN")
  {
    e.value.elements[0] =
        JsonValue::fromNumber(std::numeric_limits<double>::quiet_NaN());
  }
  SECTION("wrong shape")
  {
    e.value = JsonValue::numbers({1, 2});
  }
  SECTION("rotation array")
  {
    e.parameter = "rotation";
  }
  SECTION("unknown named angle")
  {
    e.parameter = "rotation";
    e.value = JsonValue::ofKind(JsonKind::OBJECT);
    e.value.members = {{"azimuth", JsonValue::fromNumber(0)},
        {"elevation", JsonValue::fromNumber(0)},
        {"yaw", JsonValue::fromNumber(0)}};
  }
  auto result = f.run(1, f.batch({e}));
  CHECK(result.outcome == Outcome::INVALID_REQUEST);
  CHECK_FALSE(result.committed);
  CHECK(f.identity->isCurrent(e.nodeExpected));
  auto scale = f.nodeEntry(n);
  scale.parameter = "scale";
  scale.value = JsonValue::numbers({1, 2, 3});
  auto transform = f.nodeEntry(n);
  transform.parameter = "transform";
  CHECK(f.submit(2, f.batch({scale, scale})).code == Outcome::INVALID_REQUEST);
  CHECK(f.submit(3, f.batch({scale, transform})).code
      == Outcome::INVALID_REQUEST);
  CHECK(f.submit(4, f.batch({transform, scale})).code
      == Outcome::INVALID_REQUEST);
  auto rotation = scale;
  rotation.parameter = "rotation";
  CHECK(f.submit(5, f.batch({rotation, transform})).code
      == Outcome::INVALID_REQUEST);
  CHECK(f.submit(6, f.batch({transform, rotation})).code
      == Outcome::INVALID_REQUEST);
  rotation.nodeExpected.version++;
  CHECK(
      f.submit(7, f.batch({scale, rotation})).code == Outcome::INVALID_REQUEST);
}

TEST_CASE("Local position preserves exact linear and cached representation",
    "[integration][node-edit][node-position]")
{
  Fixture f(true);
  auto *l = f.scene.defaultLayer();
  auto n = l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "move"));
  auto original = vsr::core::math::IDENTITY_MAT4;
  original[0] = {-2.f, 0.f, 0.f, 0.f};
  original[1] = {3.f, 4.f, 0.f, 0.f};
  original[2] = {0.f, 0.f, 0.f, 0.f};
  original[3] = {5.f, -6.f, 7.f, 1.f};
  n->value().setAsTransform(original);
  n->value().setCurrentValueAsDefault();
  auto cached = n->value().getTransformSRT();
  f.scene.signalLayerStructureChanged(l);
  auto e = f.positionEntry(n,
      JsonValue::array({JsonValue::fromNumber(11),
          JsonValue::fromNumber(-12),
          JsonValue::fromNumber(13)}));
  auto result = f.run(1, f.batch({e}));
  REQUIRE(result.committed);
  REQUIRE(result.batchRef);
  auto moved = n->value().getTransform();
  for (int c = 0; c < 3; ++c)
    for (int row = 0; row < 4; ++row)
      CHECK(moved[c][row] == original[c][row]);
  CHECK(moved[3] == vsr::core::math::float4(11.f, -12.f, 13.f, 1.f));
  CHECK_FALSE(n->value().isDefaultValue());
  restore::RestoreService restores(*f.engine, *f.edits);
  restores.attachScene(f.sceneId, *f.identity);
  auto restore = restores.submitLocal({f.sceneId, *result.batchRef});
  REQUIRE(restore.code == Outcome::OK);
  while (f.integration.service()) {
  }
  REQUIRE(restores.resultLocal(restore.ref)->committed);
  auto restored = n->value().getTransform();
  auto restoredSrt = n->value().getTransformSRT();
  CHECK(std::memcmp(&restored, &original, sizeof(original)) == 0);
  CHECK(std::memcmp(&restoredSrt, &cached, sizeof(cached)) == 0);
  CHECK(n->value().isDefaultValue());
  n->value().setToDefaultValue();
  CHECK(n->value().getTransform() == original);
}

TEST_CASE("Local position groups publish final versions and restore atomically",
    "[integration][node-edit][node-position]")
{
  Fixture f(true);
  auto *l = f.scene.defaultLayer();
  auto parent =
      l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "parent"));
  auto child = parent->insert_last_child(vsr::scene::LayerNodeData(l, "child"));
  vsr::core::math::mat3 srt;
  srt[0] = {-2.f, 3.f, 4.f};
  srt[1] = {725.f, -380.f, 1090.f};
  srt[2] = {3.f, 4.f, 5.f};
  parent->value().setAsTransform(srt);
  child->value().setAsTransform(vsr::core::math::IDENTITY_MAT4);
  const auto original = parent->value().getTransform();
  const auto childOriginal = child->value().getTransform();
  f.scene.signalLayerStructureChanged(l);
  auto position = [](double x, double y, double z) {
    return JsonValue::array({JsonValue::fromNumber(x),
        JsonValue::fromNumber(y),
        JsonValue::fromNumber(z)});
  };
  auto batch = f.batch({f.positionEntry(parent, position(7, 8, 9)),
      f.nodeEntry(parent),
      f.positionEntry(child, position(-1, -2, -3))});
  SECTION("reverse entry order")
  {
    std::reverse(batch.edits.begin(), batch.edits.end());
  }
  SECTION("forward entry order") {}
  SECTION("position and full matrix groups share one journal")
  {
    auto &matrix = batch.edits.back();
    matrix.parameter = "transform";
    matrix.value = JsonValue::array({JsonValue::numbers({-2, 3, 0, -1}),
        JsonValue::numbers({0, 0, 0, -2}),
        JsonValue::numbers({0, 1, 4, -3}),
        JsonValue::numbers({0, 0, 0, 1})});
    batch.edits.push_back(f.nodeEntry(child, false));
    if (GENERATE(false, true))
      std::reverse(batch.edits.begin(), batch.edits.end());
  }
  f.recorder->layerSignals = 0;
  f.recorder->onParameters = [&] {
    CHECK(parent->value().getTransform()[3].xyz() == float3(7, 8, 9));
    CHECK(child->value().getTransform()[3].xyz() == float3(-1, -2, -3));
    CHECK_FALSE(parent->value().isEnabled());
  };
  auto r = f.run(1, batch);
  REQUIRE(r.committed);
  REQUIRE(r.batchRef);
  REQUIRE(r.nodeVersions.size() == 2);
  const auto journal = f.edits->restorations().find(*r.batchRef);
  REQUIRE(journal);
  REQUIRE(journal->nodeChanges.size() == 2);
  for (const auto &c : journal->nodeChanges)
    CHECK(f.identity->isCurrent(c.resulting.expected));
  for (const auto &v : r.nodeVersions)
    CHECK(f.identity->isCurrent(v.expected));
  CHECK(f.recorder->layerSignals == 1);
  auto movedSrt = parent->value().getTransformSRT();
  CHECK(movedSrt[0] == srt[0]);
  CHECK(movedSrt[1] == srt[1]);
  CHECK(movedSrt[2] == float3(7, 8, 9));
  parent->value().name() = "renamed";
  restore::RestoreService restores(*f.engine, *f.edits);
  restores.attachScene(f.sceneId, *f.identity);
  f.recorder->onParameters = [&] {
    CHECK(parent->value().getTransform() == original);
    CHECK(child->value().getTransform() == childOriginal);
    CHECK(parent->value().isEnabled());
  };
  auto restore = restores.submitLocal({f.sceneId, *r.batchRef});
  REQUIRE(restore.code == Outcome::OK);
  while (f.integration.service()) {
  }
  REQUIRE(restores.resultLocal(restore.ref)->committed);
  auto restoredSrt = parent->value().getTransformSRT();
  CHECK(std::memcmp(&restoredSrt, &srt, sizeof(srt)) == 0);
  CHECK(parent->value().name() == "renamed");
  CHECK(f.recorder->layerSignals == 2);
}

TEST_CASE("Local position rejects bad values without any mutation",
    "[integration][node-edit][node-position]")
{
  Fixture f(true);
  auto *l = f.scene.defaultLayer();
  auto a = l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "a"));
  auto b = l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "b"));
  a->value().setAsTransform(vsr::core::math::IDENTITY_MAT4);
  b->value().setAsTransform(vsr::core::math::IDENTITY_MAT4);
  f.scene.signalLayerStructureChanged(l);
  auto vector = JsonValue::array({JsonValue::fromNumber(1),
      JsonValue::fromNumber(2),
      JsonValue::fromNumber(3)});
  auto good = f.positionEntry(a, vector);
  auto bad = f.positionEntry(b, vector);
  auto error = ValueError::INCOMPATIBLE_TYPE;
  SECTION("wrong arity")
  {
    bad.value.elements.pop_back();
  }
  SECTION("wrong kind")
  {
    bad.value.elements[1] = JsonValue::ofKind(JsonKind::BOOLEAN);
  }
  SECTION("non finite")
  {
    bad.value.elements[1] =
        JsonValue::fromNumber(std::numeric_limits<double>::infinity());
    error = ValueError::NON_FINITE;
  }
  SECTION("finite overflow before rounding")
  {
    bad.value.elements[1] = JsonValue::fromNumber(
        std::nextafter(double(std::numeric_limits<float>::max()),
            std::numeric_limits<double>::infinity()));
    error = ValueError::OUT_OF_DOMAIN;
  }
  SECTION("unsupported empty node")
  {
    b->value().setEmpty();
    bad = f.positionEntry(b, vector);
    error = ValueError::UNSUPPORTED_NODE_KIND;
  }
  SECTION("unsupported object node")
  {
    b->value().setAsObject(f.matte.data());
    bad = f.positionEntry(b, vector);
    error = ValueError::UNSUPPORTED_NODE_KIND;
  }
  SECTION("unsupported transform array node")
  {
    auto array = f.scene.createArray(ANARI_FLOAT32_MAT4, 2);
    b->value().setAsTransformArray(array.data());
    bad = f.positionEntry(b, vector);
    error = ValueError::UNSUPPORTED_NODE_KIND;
  }
  const auto versionA = good.nodeExpected;
  const auto versionB = bad.nodeExpected;
  auto revision = f.identity->sceneRevision();
  f.recorder->layerSignals = 0;
  auto r = f.run(1, f.batch({good, bad}));
  CHECK(r.outcome == Outcome::INVALID_REQUEST);
  CHECK(r.valueError == error);
  CHECK(r.failedEntry == std::optional<std::size_t>(1));
  CHECK_FALSE(r.committed);
  CHECK_FALSE(r.batchRef);
  CHECK(f.identity->isCurrent(versionA));
  CHECK(f.identity->isCurrent(versionB));
  CHECK(f.identity->sceneRevision() == revision);
  CHECK(a->value().getTransform() == vsr::core::math::IDENTITY_MAT4);
  CHECK(f.recorder->layerSignals == 0);
  CHECK(f.hookCalls == 0);
  CHECK(f.edits->restorations().size() == 0);
}

TEST_CASE("Local position keeps narrow dependencies and binary64 identity",
    "[integration][node-edit][node-position]")
{
  Fixture f(true);
  auto *l = f.scene.defaultLayer();
  auto p = l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "parent"));
  auto n = p->insert_last_child(vsr::scene::LayerNodeData(l, "child"));
  p->value().setAsTransform(vsr::core::math::IDENTITY_MAT4);
  n->value().setAsTransform(vsr::core::math::IDENTITY_MAT4);
  f.scene.signalLayerStructureChanged(l);
  auto e = f.positionEntry(n,
      JsonValue::array({JsonValue::fromNumber(1.000000059604644775390625),
          JsonValue::fromNumber(-0.0),
          JsonValue::fromNumber(3)}));
  auto batch = f.batch({e});
  auto other = batch;
  other.edits[0].value.elements[0] = JsonValue::fromNumber(1.0);
  CHECK(EditService::canonicalArguments(batch)
      != EditService::canonicalArguments(other));
  SECTION("parent writes alone permit edit and restore")
  {
    auto matrix = vsr::core::math::IDENTITY_MAT4;
    matrix[3].x = 42.f;
    p->value().setAsTransform(matrix);
    auto r = f.run(1, batch);
    REQUIRE(r.committed);
    CHECK(n->value().getTransform()[3].x == 1.f); // exact halfway: ties to even
    matrix[3].x = -9.f;
    p->value().setAsTransform(matrix);
    restore::RestoreService restores(*f.engine, *f.edits);
    restores.attachScene(f.sceneId, *f.identity);
    auto a = restores.submitLocal({f.sceneId, *r.batchRef});
    while (f.integration.service()) {
    }
    REQUIRE(restores.resultLocal(a.ref)->committed);
    CHECK(n->value().getTransform() == vsr::core::math::IDENTITY_MAT4);
    CHECK(p->value().getTransform()[3].x == -9.f);
  }
  SECTION("target change back conflicts")
  {
    n->value().setAsTransform(vsr::core::math::IDENTITY_MAT4);
    CHECK(f.run(1, batch).outcome == Outcome::CONFLICT);
  }
  SECTION("restore target change back conflicts permanently")
  {
    auto r = f.run(1, batch);
    REQUIRE(r.committed);
    n->value().setAsTransform(n->value().getTransform());
    restore::RestoreService restores(*f.engine, *f.edits);
    restores.attachScene(f.sceneId, *f.identity);
    auto a = restores.submitLocal({f.sceneId, *r.batchRef});
    while (f.integration.service()) {
    }
    CHECK(restores.resultLocal(a.ref)->outcome == Outcome::CONFLICT);
    CHECK_FALSE(restores.eligible(*r.batchRef));
  }
  SECTION("duplicate fields and inconsistent versions reject")
  {
    batch.edits.push_back(e);
    CHECK(f.submit(1, batch).code == Outcome::INVALID_REQUEST);
    batch.edits[1] = f.nodeEntry(n);
    ++batch.edits[1].nodeExpected.version;
    CHECK(f.submit(2, batch).code == Outcome::INVALID_REQUEST);
  }
  SECTION("unsupported space rejects")
  {
    batch.edits[0].space = "world";
    CHECK(f.submit(1, batch).code == Outcome::INVALID_REQUEST);
    batch.edits[0].space = "other";
    CHECK(f.submit(2, batch).code == Outcome::INVALID_REQUEST);
  }
}

TEST_CASE("Local position restore preparation replay and field isolation",
    "[integration][node-edit][node-position]")
{
  Fixture f(true);
  auto *l = f.scene.defaultLayer();
  auto n = l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "move"));
  n->value().setAsTransform(vsr::core::math::IDENTITY_MAT4);
  auto batch = f.batch({f.positionEntry(n,
      JsonValue::array({JsonValue::fromNumber(4),
          JsonValue::fromNumber(5),
          JsonValue::fromNumber(6)}))});
  auto r = f.run(1, batch);
  REQUIRE(r.committed);
  REQUIRE(r.batchRef);
  auto version = r.nodeVersions[0].expected;
  CHECK(f.submit(1, batch).replayed);
  CHECK(f.identity->isCurrent(version));
  CHECK(f.hookCalls == 1);
  restore::RestoreService restores(*f.engine, *f.edits);
  restores.attachScene(f.sceneId, *f.identity);
  restores.setFaultInjector([] { return true; });
  f.recorder->layerSignals = 0;
  auto a = restores.submitLocal({f.sceneId, *r.batchRef});
  while (f.integration.service()) {
  }
  CHECK(restores.resultLocal(a.ref)->outcome == Outcome::BUSY);
  CHECK(restores.eligible(*r.batchRef));
  CHECK(f.identity->isCurrent(version));
  CHECK(n->value().getTransform()[3].xyz() == float3(4, 5, 6));
  CHECK(f.recorder->layerSignals == 0);
  n->value().name() = "keep name";
  auto baseline = n->value().getTransform();
  n->value().setCurrentValueAsDefault();
  f.authority->setAgentEditsDisabled(true);
  f.authority->detach(f.creds);
  restores.setFaultInjector({});
  a = restores.submitLocal({f.sceneId, *r.batchRef});
  while (f.integration.service()) {
  }
  REQUIRE(restores.resultLocal(a.ref)->committed);
  CHECK(n->value().getTransform() == vsr::core::math::IDENTITY_MAT4);
  CHECK(n->value().name() == "keep name");
  CHECK_FALSE(restores.eligible(*r.batchRef));
  CHECK_FALSE(n->value().isDefaultValue());
  n->value().setToDefaultValue();
  CHECK(n->value().getTransform() == baseline);
  a = restores.submitLocal({f.sceneId, *r.batchRef});
  while (f.integration.service()) {
  }
  CHECK(restores.resultLocal(a.ref)->outcome == Outcome::ALREADY_RESTORED);
}

TEST_CASE("Node edits use precise target and structural history",
    "[integration][node-edit]")
{
  Fixture f(true);
  auto *l = f.scene.addLayer("nodes");
  auto parent = l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "p"));
  auto a = parent->insert_last_child(vsr::scene::LayerNodeData(l, "a"));
  auto sibling =
      l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "s"));
  const bool position = GENERATE(false, true);
  a->value().setAsTransform(vsr::core::math::IDENTITY_MAT4);
  f.scene.signalLayerStructureChanged(l);
  auto batch =
      f.batch({position ? f.positionEntry(a,
                              JsonValue::array({JsonValue::fromNumber(4),
                                  JsonValue::fromNumber(5),
                                  JsonValue::fromNumber(6)}))
                        : f.nodeEntry(a)});
  SECTION("unrelated sibling and parent enabled writes are not dependencies")
  {
    sibling->value().setEnabled(false);
    parent->value().setEnabled(false);
    f.scene.signalLayerStructureChanged(l);
    auto r = f.run(1, batch);
    REQUIRE(r.committed);
    restore::RestoreService restores(*f.engine, *f.edits);
    restores.attachScene(f.sceneId, *f.identity);
    sibling->value().name() = "preserved";
    auto adm = restores.submitLocal({f.sceneId, *r.batchRef});
    REQUIRE(adm.code == Outcome::OK);
    while (f.integration.service()) {
    }
    REQUIRE(restores.resultLocal(adm.ref)->committed);
    CHECK(a->value().isEnabled());
    CHECK_FALSE(sibling->value().isEnabled());
    CHECK_FALSE(parent->value().isEnabled());
    CHECK(sibling->value().name() == "preserved");
    CHECK(a->value().getTransform() == vsr::core::math::IDENTITY_MAT4);
  }
  SECTION("reparent conflicts")
  {
    l->root()->container()->move_subtree(a, sibling);
    CHECK(f.run(1, batch).outcome == Outcome::CONFLICT);
  }
  SECTION("target change-back conflicts")
  {
    a->value().setEnabled(false);
    a->value().setEnabled(true);
    CHECK(f.run(1, batch).outcome == Outcome::CONFLICT);
    CHECK(a->value().isEnabled());
  }
  SECTION("removed ancestor and reused slots conflict")
  {
    l->erase(parent);
    l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "replacement"));
    f.scene.signalLayerStructureChanged(l);
    CHECK(f.run(1, batch).outcome == Outcome::CONFLICT);
  }
}

TEST_CASE(
    "Node restore failures preserve eligibility except permanent conflicts",
    "[integration][node-edit]")
{
  Fixture f(true);
  auto *l = f.scene.addLayer("nodes");
  auto a = l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "a"));
  f.scene.signalLayerStructureChanged(l);
  auto r = f.run(1, f.batch({f.nodeEntry(a)}));
  REQUIRE(r.committed);
  restore::RestoreService restores(*f.engine, *f.edits);
  restores.attachScene(f.sceneId, *f.identity);
  SECTION("preparation failure can be retried after disable and disconnect")
  {
    restores.setFaultInjector([] { return true; });
    auto adm = restores.submitLocal({f.sceneId, *r.batchRef});
    REQUIRE(adm.code == Outcome::OK);
    while (f.integration.service()) {
    }
    CHECK(restores.resultLocal(adm.ref)->outcome == Outcome::BUSY);
    CHECK(restores.eligible(*r.batchRef));
    CHECK_FALSE(a->value().isEnabled());
    f.authority->setAgentEditsDisabled(true);
    f.authority->detach(f.creds);
    restores.setFaultInjector({});
    adm = restores.submitLocal({f.sceneId, *r.batchRef});
    REQUIRE(adm.code == Outcome::OK);
    while (f.integration.service()) {
    }
    CHECK(restores.resultLocal(adm.ref)->committed);
    CHECK(a->value().isEnabled());
    adm = restores.submitLocal({f.sceneId, *r.batchRef});
    REQUIRE(adm.code == Outcome::OK);
    while (f.integration.service()) {
    }
    CHECK(restores.resultLocal(adm.ref)->outcome == Outcome::ALREADY_RESTORED);
  }
  SECTION("change-back consumes eligibility permanently")
  {
    a->value().setEnabled(true);
    a->value().setEnabled(false);
    for (int i = 0; i < 2; ++i) {
      auto adm = restores.submitLocal({f.sceneId, *r.batchRef});
      REQUIRE(adm.code == Outcome::OK);
      while (f.integration.service()) {
      }
      CHECK(restores.resultLocal(adm.ref)->outcome == Outcome::CONFLICT);
      CHECK_FALSE(restores.eligible(*r.batchRef));
      CHECK_FALSE(a->value().isEnabled());
    }
  }
}

TEST_CASE(
    "Node exposure, live authority and committed synchronization stay honest",
    "[integration][node-edit]")
{
  bool exposed = true;
  SECTION("old object grants do not expose nodes")
  {
    exposed = false;
  }
  Fixture f(exposed);
  auto *l = f.scene.addLayer("nodes");
  auto a = l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "a"));
  const bool position = GENERATE(false, true);
  a->value().setAsTransform(vsr::core::math::IDENTITY_MAT4);
  f.scene.signalLayerStructureChanged(l);
  auto batch =
      f.batch({position ? f.positionEntry(a,
                              JsonValue::array({JsonValue::fromNumber(4),
                                  JsonValue::fromNumber(5),
                                  JsonValue::fromNumber(6)}))
                        : f.nodeEntry(a)});
  if (!exposed) {
    CHECK(f.run(1, batch).outcome == Outcome::FORBIDDEN);
    CHECK(a->value().isEnabled());
    return;
  }
  SECTION("disable before safe point")
  {
    REQUIRE(f.submit(1, batch).code == Outcome::OK);
    f.authority->setAgentEditsDisabled(true);
    while (f.integration.service()) {
    }
    const auto status = f.engine->status(f.creds, f.ref(1));
    CHECK(status.terminal == Outcome::FORBIDDEN);
    CHECK_FALSE(status.committed);
    CHECK_FALSE(f.edits->result(f.ref(1)));
    CHECK(a->value().isEnabled());
  }
  SECTION("disconnect before safe point")
  {
    REQUIRE(f.submit(1, batch).code == Outcome::OK);
    f.authority->detach(f.creds);
    while (f.integration.service()) {
    }
    CHECK(a->value().isEnabled());
    CHECK(f.hookCalls == 0);
  }
  SECTION("sync failure retains committed restore then consumes it")
  {
    f.hook = [] { return false; };
    auto r = f.run(1, batch);
    CHECK(r.outcome == Outcome::APPLIED_WITH_SYNC_FAILURE);
    REQUIRE(r.committed);
    REQUIRE(r.batchRef);
    CHECK(a->value().isEnabled() == position);
    if (position)
      CHECK(a->value().getTransform()[3].xyz() == float3(4, 5, 6));
    restore::RestoreService restores(*f.engine, *f.edits);
    restores.attachScene(f.sceneId, *f.identity);
    auto adm = restores.submitLocal({f.sceneId, *r.batchRef});
    REQUIRE(adm.code == Outcome::OK);
    while (f.integration.service()) {
    }
    CHECK(restores.resultLocal(adm.ref)->outcome
        == Outcome::APPLIED_WITH_SYNC_FAILURE);
    CHECK(restores.resultLocal(adm.ref)->committed);
    CHECK_FALSE(restores.eligible(*r.batchRef));
    CHECK(a->value().isEnabled());
    CHECK(f.hookCalls == 2);
    sync::SyncService sync(f.integration, *f.engine, f.edits->syncStates());
    auto recovery = sync.submitLocalResynchronize(f.sceneId);
    REQUIRE(recovery.code == Outcome::OK);
    while (f.integration.service()) {
    }
    CHECK(f.edits->syncState(f.sceneId) == SyncState::READY);
  }
  SECTION("post-commit cancellation is not a rollback")
  {
    f.hook = [&] {
      f.engine->cancel(f.creds, f.ref(1));
      return true;
    };
    auto r = f.run(1, batch);
    CHECK(r.committed);
    CHECK(a->value().isEnabled() == position);
    if (position)
      CHECK(a->value().getTransform()[3].xyz() == float3(4, 5, 6));
    CHECK(f.hookCalls == 1);
  }
}

TEST_CASE("Node journal shares activity budgets and expires oldest batches",
    "[integration][node-edit]")
{
  activity::Limits limits;
  limits.restorableBatches = 2;
  LoggedFixture lf(limits, true);
  auto &f = lf.fx;
  auto *l = f.scene.addLayer("nodes");
  auto a = l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "a"));
  const bool position = GENERATE(false, true);
  a->value().setAsTransform(vsr::core::math::IDENTITY_MAT4);
  f.scene.signalLayerStructureChanged(l);
  std::vector<BatchRef> refs;
  for (std::uint64_t id = 1; id <= 3; ++id) {
    auto entry = position
        ? f.positionEntry(a,
              JsonValue::array({JsonValue::fromNumber(double(id)),
                  JsonValue::fromNumber(2),
                  JsonValue::fromNumber(3)}))
        : f.nodeEntry(a, id % 2 == 0);
    auto r = f.run(id, f.batch({entry}));
    REQUIRE(r.committed);
    refs.push_back(*r.batchRef);
    auto activity = lf.log->get(id);
    REQUIRE(activity);
    CHECK(activity->committed);
    CHECK(activity->restorationBytes > 0);
    CHECK(activity->batchRef == r.batchRef);
  }
  CHECK(lf.log->usage().restorableBatches == 2);
  CHECK(f.edits->restorations().expired(refs[0]));
  restore::RestoreService restores(*f.engine, *f.edits);
  restores.attachScene(f.sceneId, *f.identity);
  auto adm = restores.submitLocal({f.sceneId, refs[0]});
  REQUIRE(adm.code == Outcome::OK);
  while (f.integration.service()) {
  }
  CHECK(restores.resultLocal(adm.ref)->outcome == Outcome::OUTCOME_UNAVAILABLE);
}

TEST_CASE("Enabled edits preserve every node payload and stored default",
    "[integration][node-edit]")
{
  Fixture f(true);
  auto *l = f.scene.addLayer("nodes");
  auto empty =
      l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "empty"));
  auto object = l->root()->insert_last_child(
      vsr::scene::LayerNodeData(l, f.matte, "object"));
  auto matrix = vsr::math::IDENTITY_MAT4;
  matrix[3][0] = 7.f;
  auto transform = l->root()->insert_last_child(
      vsr::scene::LayerNodeData(l, matrix, "transform"));
  transform->value().setCurrentValueAsDefault();
  auto array = f.scene.createArray(ANARI_FLOAT32_MAT4, 2);
  auto instanced =
      l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "array"));
  instanced->value().setAsTransformArray(array.data());
  f.scene.signalLayerStructureChanged(l);
  auto batch = f.batch({f.nodeEntry(empty),
      f.nodeEntry(object),
      f.nodeEntry(transform),
      f.nodeEntry(instanced),
      f.nodeEntry(l->root())});
  auto r = f.run(1, batch);
  REQUIRE(r.committed);
  CHECK(empty->value().isEmpty());
  CHECK(object->value().getObject() == f.matte.data());
  CHECK(transform->value().getTransform()[3][0] == 7.f);
  CHECK(transform->value().hasDefault());
  CHECK(transform->value().isDefaultValue());
  CHECK(instanced->value().getTransformArray() == array.data());
  restore::RestoreService restores(*f.engine, *f.edits);
  restores.attachScene(f.sceneId, *f.identity);
  auto adm = restores.submitLocal({f.sceneId, *r.batchRef});
  REQUIRE(adm.code == Outcome::OK);
  while (f.integration.service()) {
  }
  CHECK(restores.resultLocal(adm.ref)->committed);
  CHECK(transform->value().getTransform()[3][0] == 7.f);
  CHECK(transform->value().isDefaultValue());
  CHECK(instanced->value().getTransformArray() == array.data());
  CHECK(l->root()->value().isEnabled());
}

TEST_CASE("Viewer activity controls restore enabled nodes after disconnect",
    "[integration][node-edit][node-viewer]")
{
  LoggedFixture logged({}, true);
  auto &f = logged.fx;
  auto *layer = f.scene.defaultLayer();
  auto node = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, "viewer-node"));
  f.scene.signalLayerStructureChanged(layer);
  restore::RestoreService restores(*f.engine, *f.edits);
  restores.attachScene(f.sceneId, *f.identity);
  vsr::ui::imgui::IntegrationServices services;
  services.integration = &f.integration;
  services.authority = f.authority.get();
  services.activity = logged.log.get();
  services.restore = &restores;
  services.scenes = {{f.sceneId, "nodes"}};
  vsr::ui::imgui::IntegrationActivityModel model(std::move(services));
  auto result = f.run(1, f.batch({f.nodeEntry(node)}));
  REQUIRE(result.committed);
  REQUIRE(result.batchRef);
  CHECK_FALSE(node->value().isEnabled());
  const auto &state = model.refresh();
  REQUIRE(state.activity.size() == 1);
  CHECK(state.activity[0].entry.committed);
  CHECK(state.activity[0].entry.batchRef == result.batchRef);
  CHECK(state.activity[0].entry.eligibility == activity::Eligibility::ELIGIBLE);
  REQUIRE(model.disconnect());
  REQUIRE(model.setAgentEditsDisabled(true));
  auto admission = model.restore(f.sceneId, *result.batchRef);
  REQUIRE(admission.code == Outcome::OK);
  while (f.integration.service()) {
  }
  REQUIRE(restores.resultLocal(admission.ref));
  CHECK(restores.resultLocal(admission.ref)->committed);
  CHECK(node->value().isEnabled());
  CHECK_FALSE(restores.eligible(*result.batchRef));
}

TEST_CASE(
    "Grouped parent and child SRT precede world conversion in either order",
    "[integration][node-srt][node-world][mixed-edit]")
{
  for (bool reverse : {false, true}) {
    Fixture f(true);
    auto *layer = f.scene.defaultLayer();
    auto parent = layer->root()->insert_last_child(
        vsr::scene::LayerNodeData(layer, vsr::math::IDENTITY_MAT4, "parent"));
    auto child = parent->insert_last_child(
        vsr::scene::LayerNodeData(layer, vsr::math::IDENTITY_MAT4, "child"));
    parent->value().setCurrentValueAsDefault();
    child->value().setCurrentValueAsDefault();
    const auto parentBefore = parent->value().getTransform();
    const auto parentCache = parent->value().getTransformSRT();
    const auto childBefore = child->value().getTransform();
    const auto childCache = child->value().getTransformSRT();
    f.scene.signalLayerStructureChanged(layer);
    auto vector = [](double x, double y, double z) {
      return JsonValue::array({JsonValue::fromNumber(x),
          JsonValue::fromNumber(y),
          JsonValue::fromNumber(z)});
    };
    auto rotation =
        [&](vsr::scene::LayerNodeRef node, double azimuth, double elevation) {
          auto e = f.nodeEntry(node);
          e.parameter = "rotation";
          e.value = JsonValue::ofKind(JsonKind::OBJECT);
          e.value.members = {{"azimuth", JsonValue::fromNumber(azimuth)},
              {"elevation", JsonValue::fromNumber(elevation)},
              {"roll", JsonValue::fromNumber(0)}};
          return e;
        };
    auto parentScale = f.positionEntry(parent, vector(2, 3, 4));
    parentScale.parameter = "scale";
    auto childScale = f.positionEntry(child, vector(5, 6, 7));
    childScale.parameter = "scale";
    auto world = f.positionEntry(child, vector(22, 26, 28));
    world.space = "world";
    world.hasAncestryVersion = true;
    auto batch = f.batch({rotation(parent, 90, 0),
        parentScale,
        f.positionEntry(parent, vector(10, 20, 30)),
        world,
        rotation(child, 0, 90),
        childScale,
        f.nodeEntry(child),
        f.entry(f.matte.data(), "opacity", JsonValue::fromNumber(0.5))});
    if (reverse)
      std::reverse(batch.edits.begin(), batch.edits.end());
    const auto result = f.run(1, batch);
    REQUIRE(result.outcome == Outcome::OK);
    REQUIRE(result.committed);
    REQUIRE(result.nodeVersions.size() == 2);
    const auto matrix = child->value().getTransform();
    CHECK(matrix[3].x == Approx(1).margin(2e-6));
    CHECK(matrix[3].y == Approx(2).margin(2e-6));
    CHECK(matrix[3].z == Approx(3).margin(2e-6));
    auto expectedSRT = childCache;
    expectedSRT[0] = {5, 6, 7};
    expectedSRT[1] = {0, 90, 0};
    const vsr::scene::LayerNodeData expected(nullptr, expectedSRT);
    for (int col = 0; col < 3; ++col)
      CHECK(matrix[col] == expected.getTransform()[col]);
    for (const auto &version : result.nodeVersions) {
      auto dependencies =
          *f.identity->beginNodeDependencies(version.expected.node);
      while (dependencies.ancestorSlot)
        REQUIRE(f.identity->advanceNodeDependencies(dependencies));
      CHECK(version.dependencies.matchesStructure(dependencies));
      CHECK(version.dependencies.matchesAncestry(dependencies));
      CHECK(f.identity->isCurrent(version.expected));
    }
    bool observedRestore = false;
    f.recorder->onParameters = [&] {
      observedRestore = true;
      CHECK(parent->value().getTransform() == parentBefore);
      CHECK(child->value().getTransform() == childBefore);
      CHECK(child->value().isEnabled());
      CHECK(f.matte->parameter("opacity")->value().get<float>() == 1.f);
    };
    restore::RestoreService restores(*f.engine, *f.edits);
    restores.attachScene(f.sceneId, *f.identity);
    auto ack = restores.submitLocal({f.sceneId, *result.batchRef});
    REQUIRE(ack.code == Outcome::OK);
    while (f.integration.service()) {
    }
    REQUIRE(restores.resultLocal(ack.ref)->outcome == Outcome::OK);
    REQUIRE(restores.resultLocal(ack.ref)->committed);
    CHECK(observedRestore);
    const auto restoredParent = parent->value().getTransform();
    const auto restoredParentCache = parent->value().getTransformSRT();
    const auto restoredChild = child->value().getTransform();
    const auto restoredChildCache = child->value().getTransformSRT();
    CHECK(
        std::memcmp(&restoredParent, &parentBefore, sizeof(parentBefore)) == 0);
    CHECK(std::memcmp(&restoredParentCache, &parentCache, sizeof(parentCache))
        == 0);
    CHECK(std::memcmp(&restoredChild, &childBefore, sizeof(childBefore)) == 0);
    CHECK(
        std::memcmp(&restoredChildCache, &childCache, sizeof(childCache)) == 0);
    CHECK(parent->value().isDefaultValue());
    CHECK(child->value().isDefaultValue());
  }
}

TEST_CASE("World origin conversion uses final ancestors and restores exactly",
    "[integration][node-world]")
{
  for (int mode : {0, 1, 2})
    for (bool reverse : {false, true})
      for (bool laterChange : {false, true}) {
        Fixture f(true);
        auto *l = f.scene.defaultLayer();
        auto grand =
            l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "grand"));
        auto parent =
            grand->insert_last_child(vsr::scene::LayerNodeData(l, "parent"));
        auto child =
            parent->insert_last_child(vsr::scene::LayerNodeData(l, "child"));
        auto g = vsr::math::IDENTITY_MAT4;
        g[0] = {0, 2, 0, 0};
        g[1] = {-3, 0, 0, 0};
        g[3] = {10, 20, 30, 1};
        grand->value().setAsTransform(g);
        parent->value().setAsTransform(vsr::math::IDENTITY_MAT4);
        auto original = vsr::math::IDENTITY_MAT4;
        original[1].x = 0.5f;
        original[3] = {7, 8, 9, 1};
        child->value().setAsTransform(original);
        child->value().setCurrentValueAsDefault();
        const auto cache = child->value().getTransformSRT();
        f.scene.signalLayerStructureChanged(l);
        auto p = f.positionEntry(parent,
            JsonValue::array({JsonValue::fromNumber(1),
                JsonValue::fromNumber(2),
                JsonValue::fromNumber(3)}));
        auto c = f.positionEntry(child,
            JsonValue::array({JsonValue::fromNumber(-8),
                JsonValue::fromNumber(30),
                JsonValue::fromNumber(39)}));
        c.space = "world";
        c.hasAncestryVersion = true;
        if (mode == 1) {
          p.parameter = "transform";
          p.value =
              JsonValue::array({JsonValue::array({JsonValue::fromNumber(2),
                                    JsonValue::fromNumber(0),
                                    JsonValue::fromNumber(0),
                                    JsonValue::fromNumber(1)}),
                  JsonValue::array({JsonValue::fromNumber(0),
                      JsonValue::fromNumber(4),
                      JsonValue::fromNumber(0),
                      JsonValue::fromNumber(2)}),
                  JsonValue::array({JsonValue::fromNumber(0),
                      JsonValue::fromNumber(0),
                      JsonValue::fromNumber(3),
                      JsonValue::fromNumber(3)}),
                  JsonValue::array({JsonValue::fromNumber(0),
                      JsonValue::fromNumber(0),
                      JsonValue::fromNumber(0),
                      JsonValue::fromNumber(1)})});
        } else if (mode == 2) {
          p.space = "world";
          p.hasAncestryVersion = true;
          p.value = JsonValue::array({JsonValue::fromNumber(4),
              JsonValue::fromNumber(22),
              JsonValue::fromNumber(33)});
        }
        auto batch = reverse ? f.batch({c, p}) : f.batch({p, c});
        auto r = f.run(1, batch);
        REQUIRE(r.outcome == Outcome::OK);
        REQUIRE(r.committed);
        CHECK(child->value().getTransform()[3]
            == (mode == 1 ? vsr::math::float4(2, 1, 2, 1)
                          : vsr::math::float4(4, 4, 6, 1)));
        for (int col = 0; col < 3; ++col)
          CHECK(child->value().getTransform()[col] == original[col]);
        CHECK(f.hookCalls == 1);
        if (laterChange) {
          const auto finalParent = parent->value().getTransform();
          parent->value().setAsTransform(vsr::math::IDENTITY_MAT4);
          parent->value().setAsTransform(finalParent);
          f.scene.signalLayerStructureChanged(l);
        }
        restore::RestoreService restores(*f.engine, *f.edits);
        restores.attachScene(f.sceneId, *f.identity);
        auto ack = restores.submitLocal({f.sceneId, *r.batchRef});
        REQUIRE(ack.code == Outcome::OK);
        while (f.integration.service()) {
        }
        if (laterChange) {
          CHECK(restores.resultLocal(ack.ref)->outcome == Outcome::CONFLICT);
          CHECK_FALSE(restores.resultLocal(ack.ref)->committed);
          continue;
        }
        REQUIRE(restores.resultLocal(ack.ref)->committed);
        auto restored = child->value().getTransform();
        auto restoredCache = child->value().getTransformSRT();
        CHECK(std::memcmp(&restored, &original, sizeof(original)) == 0);
        CHECK(std::memcmp(&restoredCache, &cache, sizeof(cache)) == 0);
        CHECK(child->value().isDefaultValue());
        CHECK(parent->value().getTransform() == vsr::math::IDENTITY_MAT4);
      }
}

TEST_CASE("World conversion accepts invertible shear reflection and tiny scale",
    "[integration][node-world]")
{
  for (bool tiny : {false, true}) {
    Fixture f(true);
    auto *l = f.scene.defaultLayer();
    auto parent =
        l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "p"));
    auto child = parent->insert_last_child(vsr::scene::LayerNodeData(l, "c"));
    auto matrix = vsr::math::IDENTITY_MAT4;
    matrix[0].x = tiny ? 1e-20f : -2.f;
    matrix[1].x = tiny ? 0.f : 3.f;
    matrix[1].y = 4.f;
    matrix[3] = {0, 2, 3, 1};
    parent->value().setAsTransform(matrix);
    child->value().setAsTransform(vsr::math::IDENTITY_MAT4);
    f.scene.signalLayerStructureChanged(l);
    auto e = f.positionEntry(child,
        JsonValue::array(
            {JsonValue::fromNumber(tiny ? double(matrix[0].x) * 2 : 5),
                JsonValue::fromNumber(14),
                JsonValue::fromNumber(7)}));
    e.space = "world";
    e.hasAncestryVersion = true;
    auto result = f.run(1, f.batch({e}));
    REQUIRE(result.outcome == Outcome::OK);
    CHECK(child->value().getTransform()[3] == vsr::math::float4(2, 3, 4, 1));
  }
}

TEST_CASE("World position rejects invalid final parents without partial writes",
    "[integration][node-world]")
{
  for (int mode : {0, 1, 2, 3}) {
    Fixture f(true);
    auto *l = f.scene.defaultLayer();
    auto parent =
        l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "p"));
    auto child = parent->insert_last_child(vsr::scene::LayerNodeData(l, "c"));
    auto matrix = vsr::math::IDENTITY_MAT4;
    if (mode == 0)
      matrix[0].x = 0;
    if (mode == 1)
      matrix[0].x = std::numeric_limits<float>::denorm_min();
    if (mode == 2)
      matrix[0].x = std::numeric_limits<float>::infinity();
    if (mode == 3)
      matrix[0].w = 1;
    parent->value().setAsTransform(matrix);
    child->value().setAsTransform(vsr::math::IDENTITY_MAT4);
    f.scene.signalLayerStructureChanged(l);
    auto e = f.positionEntry(child,
        JsonValue::array({JsonValue::fromNumber(1),
            JsonValue::fromNumber(2),
            JsonValue::fromNumber(3)}));
    e.space = "world";
    e.hasAncestryVersion = true;
    auto result = f.run(1, f.batch({f.nodeEntry(parent, false), e}));
    CHECK(result.outcome == Outcome::INVALID_REQUEST);
    CHECK_FALSE(result.committed);
    CHECK_FALSE(result.batchRef);
    CHECK(result.valueError
        == (mode == 0       ? ValueError::SINGULAR_PARENT
                : mode == 1 ? ValueError::OUT_OF_DOMAIN
                : mode == 2 ? ValueError::NON_FINITE
                            : ValueError::NON_AFFINE));
    CHECK(parent->value().isEnabled());
    CHECK(child->value().getTransform() == vsr::math::IDENTITY_MAT4);
    CHECK(f.hookCalls == 0);
  }
}

TEST_CASE("World ancestry change-back conflicts but local restore stays narrow",
    "[integration][node-world]")
{
  for (bool world : {false, true}) {
    Fixture f(true);
    auto *l = f.scene.defaultLayer();
    auto parent =
        l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "p"));
    auto child = parent->insert_last_child(vsr::scene::LayerNodeData(l, "c"));
    parent->value().setAsTransform(vsr::math::IDENTITY_MAT4);
    child->value().setAsTransform(vsr::math::IDENTITY_MAT4);
    f.scene.signalLayerStructureChanged(l);
    auto entry = [&] {
      auto e = f.positionEntry(child,
          JsonValue::array({JsonValue::fromNumber(1),
              JsonValue::fromNumber(2),
              JsonValue::fromNumber(3)}));
      e.space = world ? "world" : "local";
      e.hasAncestryVersion = world;
      return e;
    };
    auto stale = entry();
    auto changeBack = [&] {
      auto m = vsr::math::IDENTITY_MAT4;
      m[3].x = 20;
      parent->value().setAsTransform(m);
      parent->value().setAsTransform(vsr::math::IDENTITY_MAT4);
      f.scene.signalLayerStructureChanged(l);
    };
    changeBack();
    auto result = f.run(1, f.batch({stale}));
    CHECK(result.outcome == (world ? Outcome::CONFLICT : Outcome::OK));
    result = f.run(2, f.batch({entry()}));
    REQUIRE(result.committed);
    changeBack();
    restore::RestoreService restores(*f.engine, *f.edits);
    restores.attachScene(f.sceneId, *f.identity);
    auto ack = restores.submitLocal({f.sceneId, *result.batchRef});
    REQUIRE(ack.code == Outcome::OK);
    while (f.integration.service()) {
    }
    auto restored = restores.resultLocal(ack.ref);
    CHECK(restored->outcome == (world ? Outcome::CONFLICT : Outcome::OK));
    CHECK(restored->committed == !world);
    if (world) {
      auto retry = restores.submitLocal({f.sceneId, *result.batchRef});
      while (f.integration.service()) {
      }
      CHECK(restores.resultLocal(retry.ref)->outcome == Outcome::CONFLICT);
    }
  }
}

TEST_CASE("Fools Mate discovers moves verifies and restores one live scene",
    "[integration][chess]")
{
  Fixture f(true);
  chess::Fixture board(f.scene);
  context::ViewerExecutionHook safePoint(f.integration);
  auto drain = [&] {
    for (int i = 0; i < 10000; ++i)
      if (safePoint() == 0)
        return;
    FAIL("headless safe point did not become idle");
  };
  inspection::Inspector inspector(
      f.integration, *f.authority, [&](SceneId) { return f.identity.get(); });
  restore::RestoreService restores(*f.engine, *f.edits);
  restores.attachScene(f.sceneId, *f.identity);
  activity::ActivityLog log;
  activity::EngineActivitySink sink(log, f.integration);
  f.engine->setActivitySink(&sink);
  struct ResetSink
  {
    operations::OperationEngine &engine;
    ~ResetSink()
    {
      engine.setActivitySink(nullptr);
    }
  } reset{*f.engine};
  std::uint64_t id = 100;
  auto inspect = [&](const char *name) {
    inspection::PageArgs args;
    args.kind = "nodes";
    args.filters.name = name;
    auto built = inspector.makeInspection(f.sessionId, f.sceneId, args);
    REQUIRE(built.code == operations::Outcome::OK);
    const auto ref = f.ref(++id);
    REQUIRE(f.engine->submit(f.creds, ref, std::move(built.request)).code
        == operations::Outcome::OK);
    drain();
    auto status = f.engine->status(f.creds, ref);
    REQUIRE(status.terminal == operations::Outcome::OK);
    mcp::json::Value result;
    REQUIRE(mcp::json::parse(status.result, result));
    REQUIRE(result.get("items")->a.size() == 1);
    return result.get("items")->a[0];
  };
  std::vector<EditEntry> entries;
  for (const auto &move : chess::MOVES) {
    auto item = inspect(move.name);
    vsr::scene::LayerNodeRef target;
    for (auto node : board.pieces)
      if (node->value().name() == move.name)
        target = node;
    REQUIRE(target);
    auto e = f.positionEntry(target,
        JsonValue::numbers({move.world[0], move.world[1], move.world[2]}));
    CHECK(item.get("name")->get("text")->s == move.name);
    CHECK_FALSE(item.get("handle")->s.empty());
    e.space = "world";
    e.hasAncestryVersion = true;
    entries.push_back(e);
  }
  const auto whiteObject = f.version(board.surfaces[0].data());
  const auto blackObject = f.version(board.surfaces[1].data());
  const auto matteObject = f.version(f.matte.data());
  const auto lightObject = f.version(f.light.data());
  const auto stale = entries;
  const auto editId = ++id;
  REQUIRE(f.submit(editId, f.batch(entries)).code == operations::Outcome::OK);
  CHECK_FALSE(f.edits->result(f.ref(editId)));
  board.unchanged(true); // admission is not mutation
  drain();
  auto edited = f.edits->result(f.ref(editId));
  REQUIRE(edited);
  REQUIRE(edited->outcome == operations::Outcome::OK);
  REQUIRE(edited->committed);
  REQUIRE(edited->batchRef);
  CHECK(f.hookCalls == 1);
  for (const auto &move : chess::MOVES) {
    auto item = inspect(move.name);
    REQUIRE(item.get("world_matrix"));
    REQUIRE(item.get("world_matrix")->get("data"));
    REQUIRE(item.get("world_matrix")->get("data")->a.size() == 4);
    REQUIRE(item.get("local_position"));
    REQUIRE(item.get("local_position")->get("data"));
    REQUIRE(item.get("local_position")->get("data")->a.size() == 3);
    for (int axis = 0; axis < 3; ++axis) {
      REQUIRE(item.get("world_matrix")->get("data")->a[axis].a.size() == 4);
      CHECK(item.get("world_matrix")->get("data")->a[axis].a[3].n
          == move.world[axis]);
      CHECK(item.get("local_position")->get("data")->a[axis].n
          == move.local[axis]);
    }
  }
  board.unchanged(false);
  auto rejected = f.submit(++id, f.batch(stale));
  REQUIRE(rejected.code == operations::Outcome::OK);
  drain();
  CHECK(f.edits->result(f.ref(id))->outcome == operations::Outcome::CONFLICT);
  bool unrelatedEdit = false;
  SECTION("All other 28 placements remain exactly unchanged") {}
  SECTION("Restoration preserves a later unrelated sibling edit")
  {
    REQUIRE(f.submit(++id, f.batch({f.nodeEntry(board.pieces[0], false)})).code
        == operations::Outcome::OK);
    drain();
    REQUIRE(f.edits->result(f.ref(id))->committed);
    board.original[0].setEnabled(false);
    unrelatedEdit = true;
  }
  REQUIRE(
      restores.submit(f.creds, f.ref(++id), {f.sceneId, *edited->batchRef}).code
      == operations::Outcome::OK);
  drain();
  REQUIRE(restores.result(f.ref(id))->outcome == operations::Outcome::OK);
  board.unchanged(true);
  CHECK(f.hookCalls == (unrelatedEdit ? 3 : 2));
  CHECK(f.version(board.surfaces[0].data()) == whiteObject);
  CHECK(f.version(board.surfaces[1].data()) == blackObject);
  CHECK(f.version(f.matte.data()) == matteObject);
  CHECK(f.version(f.light.data()) == lightObject);
  bool sawEdit = false;
  bool sawRestore = false;
  for (const auto &record : log.snapshot()) {
    REQUIRE_FALSE(record.entry.active);
    if (record.entry.ref && record.entry.ref->sessionId == f.sessionId
        && record.entry.ref->requestId == editId) {
      CHECK(record.entry.terminal == operations::Outcome::OK);
      CHECK(record.entry.committed);
      sawEdit = true;
    }
    if (record.entry.kind == activity::OperationKind::RESTORE) {
      CHECK(record.entry.terminal == operations::Outcome::OK);
      CHECK(record.entry.committed);
      sawRestore = true;
    }
  }
  CHECK(sawEdit);
  CHECK(sawRestore);
  // Host change-back invalidates the freshly observed world dependency.
  auto e = f.positionEntry(board.pieces[13], JsonValue::numbers({1, 2, 3}));
  e.space = "world";
  e.hasAncestryVersion = true;
  auto parent = board.board->value().getTransform();
  auto changed = parent;
  changed[3].x += 1;
  board.board->value().setAsTransform(changed);
  board.board->value().setAsTransform(parent);
  f.scene.signalLayerStructureChanged(f.scene.defaultLayer());
  REQUIRE(f.submit(++id, f.batch({e})).code == operations::Outcome::OK);
  drain();
  CHECK(f.edits->result(f.ref(id))->outcome == operations::Outcome::CONFLICT);
  board.unchanged(true);
}

TEST_CASE(
    "Every matrix component rejects nonfinite and binary64 overflow atomically",
    "[integration][reliability-numeric][mixed-edit][node-matrix]")
{
  Fixture f(true);
  auto *layer = f.scene.defaultLayer();
  auto node = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, vsr::math::IDENTITY_MAT4, "numeric"));
  node->value().setCurrentValueAsDefault();
  const auto cache = node->value().getTransformSRT();
  const auto version = f.nodeEntry(node).nodeExpected;
  const auto objectVersion = f.version(f.matte.data());
  const auto revision = f.identity->sceneRevision();
  const double invalid[] = {std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
      std::nextafter(double(std::numeric_limits<float>::max()),
          std::numeric_limits<double>::infinity())};
  std::uint64_t id = 1;
  for (int row = 0; row < 4; ++row)
    for (int col = 0; col < 4; ++col)
      for (int kind = 0; kind < 4; ++kind) {
        INFO("row=" << row << " col=" << col << " kind=" << kind);
        auto matrix = f.nodeEntry(node);
        matrix.parameter = "transform";
        matrix.value = JsonValue::array({JsonValue::numbers({1, 0, 0, 2}),
            JsonValue::numbers({0, 1, 0, 3}),
            JsonValue::numbers({0, 0, 1, 5}),
            JsonValue::numbers({0, 0, 0, 1})});
        matrix.value.elements[row].elements[col] =
            JsonValue::fromNumber(invalid[kind]);
        const auto result = f.run(id++,
            f.batch(
                {f.entry(f.matte.data(), "opacity", JsonValue::fromNumber(0.5)),
                    f.nodeEntry(node, false),
                    matrix}));
        CHECK(result.outcome == Outcome::INVALID_REQUEST);
        CHECK(result.valueError
            == (kind == 3 ? ValueError::OUT_OF_DOMAIN
                          : ValueError::NON_FINITE));
        CHECK(result.failedEntry == 2);
        CHECK_FALSE(result.committed);
        CHECK_FALSE(result.batchRef);
        CHECK(f.opacity() == 1.f);
        CHECK(node->value().isEnabled());
        CHECK(node->value().getTransform() == vsr::math::IDENTITY_MAT4);
        const auto afterCache = node->value().getTransformSRT();
        CHECK(std::memcmp(&afterCache, &cache, sizeof(cache)) == 0);
        CHECK(node->value().isDefaultValue());
        CHECK(f.identity->isCurrent(version));
        CHECK(f.identity->isCurrent(objectVersion));
        CHECK(f.identity->sceneRevision() == revision);
        CHECK(f.hookCalls == 0);
        CHECK(f.edits->restorations().size() == 0);
      }
}

TEST_CASE(
    "Seeded dyadic affine groups restore exactly and reject stale batches",
    "[integration][reliability-property][node-world][mixed-edit]")
{
  constexpr std::uint32_t SEED = 0x042a2e7b;
  std::mt19937 random(SEED);
  auto dyadic = [&] { return (int(random() % 33) - 16) / 4.; };
  auto scale = [&] {
    const double powers[] = {-4, -2, -0.5, 0.5, 2, 4};
    return powers[random() % 6];
  };
  for (int iteration = 0; iteration < 48; ++iteration) {
    INFO("seed=" << SEED << " iteration=" << iteration);
    Fixture f(true);
    auto *layer = f.scene.defaultLayer();
    auto grand = layer->root()->insert_last_child(
        vsr::scene::LayerNodeData(layer, vsr::math::IDENTITY_MAT4, "grand"));
    auto parent = grand->insert_last_child(
        vsr::scene::LayerNodeData(layer, vsr::math::IDENTITY_MAT4, "parent"));
    auto child = parent->insert_last_child(
        vsr::scene::LayerNodeData(layer, vsr::math::IDENTITY_MAT4, "child"));
    auto sibling = grand->insert_last_child(
        vsr::scene::LayerNodeData(layer, vsr::math::IDENTITY_MAT4, "sibling"));
    const double gx = scale(), gy = scale(), gz = scale();
    const double gh = dyadic(), gtX = dyadic(), gtY = dyadic(), gtZ = dyadic();
    const bool turn = iteration % 2;
    auto g = vsr::math::IDENTITY_MAT4;
    g[0] =
        turn ? vsr::math::float4(0, gy, 0, 0) : vsr::math::float4(gx, 0, 0, 0);
    g[1] = turn ? vsr::math::float4(-gx, 0, 0, 0)
                : vsr::math::float4(gh, gy, 0, 0);
    g[2].z = gz;
    g[3] = {float(gtX), float(gtY), float(gtZ), 1};
    grand->value().setAsTransform(g);

    // Independent forward equations, not inversion, matrix multiplication,
    // decomposition or any implementation transform helper.
    auto world = [&](double x, double y, double z) {
      return JsonValue::numbers({gtX + (turn ? -gx * y : gx * x + gh * y),
          gtY + gy * (turn ? x : y),
          gtZ + gz * z});
    };
    const int mode = iteration % 3;
    auto beforeParent = vsr::math::IDENTITY_MAT4;
    beforeParent[1].x = mode == 0 ? 0.75f : 0.f;
    beforeParent[3] = {-0.f, 2, -3, 1};
    parent->value().setAsTransform(beforeParent);
    auto beforeChild = vsr::math::IDENTITY_MAT4;
    beforeChild[0].x = iteration % 4 == 0 ? 0.f : -2.f;
    beforeChild[1].x = 1.25f;
    beforeChild[3] = {3, -0.f, 5, 1};
    child->value().setAsTransform(beforeChild);
    child->value().setEnabled(iteration % 3 != 0);
    if (iteration % 2 == 0) {
      parent->value().setCurrentValueAsDefault();
      child->value().setCurrentValueAsDefault();
    }
    const auto parentCache = parent->value().getTransformSRT();
    const auto childCache = child->value().getTransformSRT();
    const bool beforeEnabled = child->value().isEnabled();
    const bool parentWasDefault = parent->value().isDefaultValue();
    const bool childWasDefault = child->value().isDefaultValue();
    f.scene.signalLayerStructureChanged(layer);

    const double px = scale(), py = scale(), pz = scale();
    const double ph = mode == 0 ? dyadic() : 0;
    const double pk = mode == 0 ? dyadic() : 0;
    const double pj = mode == 0 ? dyadic() : 0;
    const double tx = dyadic(), ty = dyadic(), tz = dyadic();
    const double cx = dyadic(), cy = dyadic(), cz = dyadic();
    auto p = f.nodeEntry(parent);
    p.parameter = "transform";
    p.value = JsonValue::array({JsonValue::numbers({px, ph, pk, tx}),
        JsonValue::numbers({0, py, pj, ty}),
        JsonValue::numbers({0, 0, pz, tz}),
        JsonValue::numbers({0, 0, 0, 1})});
    auto c = f.positionEntry(child,
        world(tx + px * cx + ph * cy + pk * cz,
            ty + py * cy + pj * cz,
            tz + pz * cz));
    c.space = "world";
    c.hasAncestryVersion = true;
    auto batch = f.batch({p,
        c,
        f.nodeEntry(child, !beforeEnabled),
        f.entry(f.matte.data(), "opacity", JsonValue::fromNumber(0.25)),
        f.entry(f.light.data(), "irradiance", JsonValue::fromNumber(2))});
    if (mode != 0) {
      batch.edits[0].parameter = "scale";
      batch.edits[0].value = JsonValue::numbers({px, py, pz});
      auto position = f.positionEntry(parent,
          mode == 2 ? world(tx, ty, tz) : JsonValue::numbers({tx, ty, tz}));
      position.space = mode == 2 ? "world" : "local";
      position.hasAncestryVersion = mode == 2;
      batch.edits.push_back(position);
    }
    // Fisher-Yates with the engine's raw output keeps permutations portable.
    for (std::size_t n = batch.edits.size(); n > 1; --n)
      std::swap(batch.edits[n - 1], batch.edits[random() % n]);
    EditResult result;
    if (iteration % 2) {
      const auto admitted = f.edits->submitLocal(batch);
      REQUIRE(admitted.code == Outcome::OK);
      for (int step = 0; f.integration.service() > 0; ++step)
        REQUIRE(step < 1000);
      REQUIRE(f.edits->resultLocal(admitted.ref));
      result = *f.edits->resultLocal(admitted.ref);
    } else
      result = f.run(1, batch);
    REQUIRE(result.outcome == Outcome::OK);
    REQUIRE(result.committed);
    REQUIRE(result.batchRef);
    CHECK(result.nodeVersions.size() == 2);
    CHECK(result.versions.size() == 2);
    const auto finalParent = parent->value().getTransform();
    CHECK(finalParent[0] == vsr::math::float4(px, 0, 0, 0));
    CHECK(finalParent[1] == vsr::math::float4(ph, py, 0, 0));
    CHECK(finalParent[2] == vsr::math::float4(pk, pj, pz, 0));
    CHECK(finalParent[3] == vsr::math::float4(tx, ty, tz, 1));
    const auto after = child->value().getTransform();
    CHECK(after[3] == vsr::math::float4(cx, cy, cz, 1));
    for (int col = 0; col < 3; ++col)
      CHECK(after[col] == beforeChild[col]);
    CHECK(child->value().isEnabled() == !beforeEnabled);
    CHECK(f.opacity() == 0.25f);
    CHECK(f.irradiance() == 2.f);

    const auto revision = f.identity->sceneRevision();
    const auto rejected = f.run(2, batch);
    CHECK(rejected.outcome == Outcome::CONFLICT);
    CHECK_FALSE(rejected.committed);
    CHECK_FALSE(rejected.batchRef);
    CHECK(f.identity->sceneRevision() == revision);
    CHECK(child->value().getTransform() == after);
    CHECK(f.opacity() == 0.25f);
    CHECK(f.irradiance() == 2.f);
    CHECK(f.hookCalls == 1);
    CHECK(f.edits->restorations().size() == 1);
    for (const auto &v : result.nodeVersions)
      CHECK(f.identity->isCurrent(v.expected));
    for (const auto &v : result.versions)
      CHECK(f.identity->isCurrent(v));

    // Later unrelated host edits must survive conditional restoration.
    auto unrelated = vsr::math::IDENTITY_MAT4;
    unrelated[3] = {17, 19, 23, 1};
    sibling->value().setAsTransform(unrelated);
    child->value().name() = "keep rename";
    f.scene.signalLayerStructureChanged(layer);
    restore::RestoreService restores(*f.engine, *f.edits);
    restores.attachScene(f.sceneId, *f.identity);
    const auto ack = restores.submitLocal({f.sceneId, *result.batchRef});
    REQUIRE(ack.code == Outcome::OK);
    for (int step = 0; f.integration.service() > 0; ++step)
      REQUIRE(step < 1000);
    REQUIRE(restores.resultLocal(ack.ref));
    REQUIRE(restores.resultLocal(ack.ref)->outcome == Outcome::OK);
    REQUIRE(restores.resultLocal(ack.ref)->committed);
    const auto restoredParent = parent->value().getTransform();
    const auto restoredChild = child->value().getTransform();
    const auto restoredParentCache = parent->value().getTransformSRT();
    const auto restoredChildCache = child->value().getTransformSRT();
    CHECK(
        std::memcmp(&restoredParent, &beforeParent, sizeof(beforeParent)) == 0);
    CHECK(std::memcmp(&restoredChild, &beforeChild, sizeof(beforeChild)) == 0);
    CHECK(std::memcmp(&restoredParentCache, &parentCache, sizeof(parentCache))
        == 0);
    CHECK(
        std::memcmp(&restoredChildCache, &childCache, sizeof(childCache)) == 0);
    CHECK(child->value().isEnabled() == beforeEnabled);
    CHECK(parent->value().isDefaultValue() == parentWasDefault);
    CHECK(child->value().isDefaultValue() == childWasDefault);
    CHECK(child->value().name() == "keep rename");
    CHECK(sibling->value().getTransform() == unrelated);
    CHECK(grand->value().getTransform() == g);
    CHECK(f.opacity() == 1.f);
    CHECK(f.irradiance() == 1.f);
    CHECK(f.hookCalls == 2);
    CHECK_FALSE(restores.eligible(*result.batchRef));
    parent->value().setToDefaultValue();
    child->value().setToDefaultValue();
    CHECK(parent->value().getTransform()
        == (iteration % 2 == 0 ? beforeParent : vsr::math::IDENTITY_MAT4));
    CHECK(child->value().getTransform()
        == (iteration % 2 == 0 ? beforeChild : vsr::math::IDENTITY_MAT4));
  }
}

TEST_CASE("Deep node preparation fails boundedly without mutation",
    "[integration][node-edit]")
{
  Fixture f(true);
  auto *l = f.scene.addLayer("deep");
  auto n = l->root();
  for (int i = 0; i < 4097; ++i)
    n = n->insert_last_child(vsr::scene::LayerNodeData(l, "deep"));
  f.scene.signalLayerStructureChanged(l);
  auto entry = f.nodeEntry(n);
  const auto revision = f.identity->sceneRevision();
  auto r = f.run(1, f.batch({entry}));
  CHECK(r.outcome == Outcome::BUSY);
  CHECK_FALSE(r.committed);
  CHECK_FALSE(r.batchRef);
  CHECK(n->value().isEnabled());
  CHECK(f.identity->isCurrent(entry.nodeExpected));
  CHECK(f.identity->sceneRevision() == revision);
  CHECK(f.hookCalls == 0);
}

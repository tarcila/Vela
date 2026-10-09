// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/activity/Activity.hpp"
#include "vsr/integration/edit/AgentEdit.hpp"
#include "vsr/integration/mcp/Json.hpp"
#include "vsr/integration/restore/BatchRestore.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace vsr::integration;
using namespace vsr::integration::edit;
using namespace vsr::integration::restore;
using vsr::core::math::float3;
namespace tokens = vsr::scene::tokens;

namespace {

struct RemovalReservation
{
  std::size_t *charge;
  std::shared_ptr<const void> reserve(std::size_t bytes);
};

std::shared_ptr<const void> RemovalReservation::reserve(std::size_t bytes)
{
  if (charge)
    *charge = bytes;
  return std::make_shared<int>(0);
}

enum class NotificationHook
{
  PARAMETER_BATCH,
  LAYER_STRUCTURE,
  BATCH_END,
  NONE
};

struct NotificationProbe
{
  NotificationHook failAt{NotificationHook::NONE};
  std::exception_ptr failure;
  std::array<int, 3> calls{};
  std::size_t parameterCount{0};

  void notify(NotificationHook hook);
};

void NotificationProbe::notify(NotificationHook hook)
{
  ++calls[static_cast<std::size_t>(hook)];
  if (failAt == hook)
    std::rethrow_exception(failure);
}

struct ThrowingWriteObserver : vsr::scene::EmptyUpdateDelegate
{
  explicit ThrowingWriteObserver(NotificationProbe *probe) : m_probe(probe) {}
  void signalParameterBatchUpdated(const vsr::scene::Object *,
      const std::vector<const vsr::scene::Parameter *> &) override;
  void signalLayerStructureUpdated(const vsr::scene::Layer *) override;
  void signalUpdateBatchEnd() override;

 private:
  NotificationProbe *m_probe{nullptr};
};

void ThrowingWriteObserver::signalParameterBatchUpdated(
    const vsr::scene::Object *,
    const std::vector<const vsr::scene::Parameter *> &parameters)
{
  m_probe->parameterCount = parameters.size();
  m_probe->notify(NotificationHook::PARAMETER_BATCH);
}

void ThrowingWriteObserver::signalLayerStructureUpdated(
    const vsr::scene::Layer *)
{
  m_probe->notify(NotificationHook::LAYER_STRUCTURE);
}

void ThrowingWriteObserver::signalUpdateBatchEnd()
{
  m_probe->notify(NotificationHook::BATCH_END);
}

struct WriteOrderObserver : vsr::scene::EmptyUpdateDelegate
{
  explicit WriteOrderObserver(std::vector<const vsr::scene::Object *> *order)
      : m_order(order)
  {}
  void signalParameterBatchUpdated(const vsr::scene::Object *object,
      const std::vector<const vsr::scene::Parameter *> &) override;

 private:
  std::vector<const vsr::scene::Object *> *m_order{nullptr};
};

void WriteOrderObserver::signalParameterBatchUpdated(
    const vsr::scene::Object *object,
    const std::vector<const vsr::scene::Parameter *> &)
{
  m_order->push_back(object);
}

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
  std::unique_ptr<activity::ActivityLog> hdriLog;
  std::unique_ptr<activity::EngineActivitySink> hdriSink;
  operations::Credentials creds;
  std::string sessionId;
  vsr::scene::MaterialRef matte;
  vsr::scene::LightRef light;
  vsr::scene::MaterialRef other;

  explicit Fixture(
      bool nodeEdits = false, NotificationProbe *notificationFailure = nullptr)
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
    s.nodeEdits = nodeEdits;
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
    if (notificationFailure)
      scene.updateDelegate().emplace<ThrowingWriteObserver>(
          notificationFailure);
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
    engine->setActivitySink(nullptr);
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

  void enableHdriLog()
  {
    hdriLog = std::make_unique<activity::ActivityLog>();
    hdriSink =
        std::make_unique<activity::EngineActivitySink>(*hdriLog, integration);
    engine->setActivitySink(hdriSink.get());
  }

  BatchRef stagedHdri(bool ownsLayer,
      content::HdriHandles &handles,
      std::size_t *tombstoneCharge = nullptr)
  {
    REQUIRE(scene.trackImageWrites(
        std::make_shared<vsr::scene::ArrayWriteSequence>()));
    scene.defaultLayer();
    const float3 radiance[] = {{8.f, 4.f, 2.f}};
    content::PreparedHdriPublication prepared(*identity,
        {},
        "HDRI root",
        1,
        1,
        radiance,
        {},
        ownsLayer ? "owned HDRI" : vsr::scene::Token{});
    handles = prepared.handles();
    auto snapshot = std::make_shared<HdriSnapshot>();
    snapshot->reserveResult = [] { return std::make_shared<int>(0); };
    snapshot->controlOwnership =
        operations::ResultOwnership(snapshot->reserveResult);
    snapshot->scopeLease = snapshot->reserveResult();
    snapshot->reserveRemoval =
        operations::Reservation::from(std::make_shared<RemovalReservation>(
            RemovalReservation{tombstoneCharge}));
    snapshot->retainedBytes = sizeof(HdriSnapshot);
    RestorationRecord record;
    record.ref = {edits->launchId(), *edits->newBatchId()};
    record.scene = sceneId;
    record.image = snapshot;
    record.hdri = snapshot;
    const auto batch = record.ref;
    operations::Request request;
    request.kind = "test_staged_hdri"; // not public Content Creation admission
    request.target = session::Target::scene(sceneId);
    request.capability = session::Capability::EDIT;
    request.canonicalArguments = "staged HDRI test fixture";
    request.executor = [&](operations::StepContext &ctx) {
      REQUIRE(ctx.reserveRestoration(
          restorationBytes(record), batch.launchId, batch.batchId));
      REQUIRE(edits->restorations().prepareImage(std::move(record)));
      ctx.prepareCommit("{\"committed\":true}", 128);
      REQUIRE(ctx.beforeWrite() == Outcome::OK);
      REQUIRE(prepared.commit());
      REQUIRE(snapshot->evidence.seal(*identity, handles, ownsLayer));
      ctx.markCommitted();
      edits->restorations().publishImage(batch);
      ctx.publishRestoration();
      prepared.notify();
      operations::Step step;
      step.committed = true;
      return step;
    };
    const auto admission = engine->submitLocal(std::move(request));
    REQUIRE(admission.code == Outcome::OK);
    drain();
    REQUIRE(engine->statusLocal(admission.ref).committed);
    return batch;
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

TEST_CASE("Normal shared Restore consumes exact staged HDRI removal",
    "[integration][content][restore][hdri-restore]")
{
  const bool ownsLayer = GENERATE(false, true);
  Fixture fx;
  fx.enableHdriLog();
  const auto old = fx.edit(1, {fx.entry(fx.matte.data(), "opacity", .25)});
  content::HdriHandles handles;
  std::size_t transient = 0;
  const auto batch = fx.stagedHdri(ownsLayer, handles, &transient);
  const auto record = fx.edits->restorations().find(batch);
  REQUIRE(record);
  REQUIRE(record->hdri);
  CHECK(record->hdri->bytes.empty());
  CHECK(record->hdri->evidence.check(*fx.identity) == nullptr);
  CHECK_FALSE(record->hdri->physicalLease); // no retained radiance undo owner
  const auto bytes = restorationBytes(*record);
  CHECK(bytes >= sizeof(HdriSnapshot));
  CHECK(bytes < 16384);
  fx.scene.defaultLayer()->setName("borrowed label");
  auto restored = fx.restore(2, batch);
  CHECK(restored.committed);
  CHECK(restored.outcome == Outcome::OK);
  // The temporary reservation covers radiance AND backing/growth/tombstones,
  // not the former unexplained 8-KiB control estimate or retained undo texels.
  CHECK(transient > 8192);
  CHECK(transient < (1u << 20));
  CHECK_FALSE(fx.identity->resolve(handles.radiance));
  CHECK_FALSE(fx.identity->resolve(handles.light));
  CHECK_FALSE(fx.identity->resolve(handles.contentRoot));
  CHECK(fx.scene.numberOfLayers() == 1);
  CHECK(fx.restores->restored(batch));
  CHECK(fx.restores->eligible(old));
  CHECK(fx.restore(3, batch).outcome == Outcome::ALREADY_RESTORED);
  CHECK(fx.restore(4, old).committed);
  CHECK(fx.opacity(fx.matte) == 1.f);
}

TEST_CASE("Normal HDRI Restore rejects writes and preparation faults safely",
    "[integration][content][restore][hdri-restore]")
{
  const int mutation = GENERATE(0, 1, 2);
  Fixture fx;
  fx.enableHdriLog();
  content::HdriHandles handles;
  const auto batch = fx.stagedHdri(false, handles);
  if (mutation == 0) {
    auto *array = dynamic_cast<vsr::scene::Array *>(
        fx.identity->resolve(handles.radiance));
    array->mapAs<float3>()[0] = {8.f, 4.f, 2.f};
    array->unmap();
    CHECK(fx.restore(1, batch).outcome == Outcome::CONFLICT);
    CHECK(fx.restores->conflicting(batch));
    CHECK(fx.restore(2, batch).outcome == Outcome::CONFLICT);
  } else if (mutation == 1) {
    fx.restores->setFaultInjector([] { return true; });
    CHECK(fx.restore(1, batch).outcome == Outcome::BUSY);
    CHECK(fx.restores->eligible(batch));
    CHECK(fx.identity->resolve(handles.light));
    fx.restores->setFaultInjector({});
    CHECK(fx.restore(2, batch).committed);
  } else {
    fx.scene.defaultLayer()->root()->insert_last_child(
        vsr::scene::LayerNodeData(fx.scene.defaultLayer(),
            fx.identity->resolve(handles.light),
            "outside user"));
    CHECK(fx.restore(1, batch).outcome == Outcome::CONFLICT);
    CHECK(fx.identity->resolve(handles.light));
    CHECK(fx.identity->resolve(handles.radiance));
  }
}

TEST_CASE("Generic typed edits restore exact values or conflict as one batch",
    "[integration][generic-edit][restore]")
{
  Fixture fx;
  auto point = fx.scene.createObject<vsr::scene::Light>("point");
  auto camera = fx.scene.createObject<vsr::scene::Camera>("perspective");
  std::string originalLabel(200, 'x');
  originalLabel[100] = '\0';
  camera->setParameter("label", "initial");
  camera->parameter("label")->setValue(vsr::core::Any(originalLabel));
  const std::int64_t count = -9007199254740991LL;
  const std::uint16_t bins[3] = {0, 32768, 65535};
  const double weights[4] = {1.25, -2.5, 3.75, 4.5};
  camera->setParameter("count", ANARI_INT64, &count);
  camera->setParameter("bins", ANARI_UINT16_VEC3, bins);
  const std::uint16_t minBins[3] = {0, 1, 0};
  const std::uint16_t maxBins[3] = {65535, 32768, 65535};
  camera->parameter("bins")
      ->setMin(vsr::core::Any(ANARI_UINT16_VEC3, minBins))
      .setMax(vsr::core::Any(ANARI_UINT16_VEC3, maxBins));
  camera->setParameter("weights", ANARI_FLOAT64_VEC4, weights);
  camera->parameter("count")->setEnabled(false);
  std::vector<std::pair<std::string, vsr::core::Any>> before;
  for (const char *name : {"stereoMode", "label", "count", "bins", "weights"})
    before.emplace_back(name, camera->parameter(name)->value());
  auto typedEntry =
      [&](const vsr::scene::Object *o, const char *name, JsonValue value) {
        return EditEntry{fx.version(o), name, std::move(value)};
      };
  auto stringValue = [](const std::string &text) {
    auto value = JsonValue::ofKind(JsonKind::STRING);
    value.text = text;
    return value;
  };
  auto visible = JsonValue::ofKind(JsonKind::BOOLEAN);
  visible.boolean = false;
  const auto oldCameraVersion = fx.version(camera.data());
  auto batch = fx.edit(1,
      {typedEntry(point.data(), "visible", visible),
          typedEntry(camera.data(), "stereoMode", stringValue("left")),
          typedEntry(camera.data(), "label", stringValue("edited label")),
          typedEntry(
              camera.data(), "count", JsonValue::fromNumber(9007199254740991.)),
          typedEntry(camera.data(), "bins", JsonValue::numbers({65535, 1, 0})),
          typedEntry(camera.data(),
              "weights",
              JsonValue::numbers({-4.5, 3.75, -2.5, 1.25}))});
  CHECK_FALSE(point->parameter("visible")->value().get<bool>());
  CHECK(camera->parameter("stereoMode")->value().getString() == "left");
  CHECK(camera->parameter("label")->value().getString() == "edited label");
  CHECK(camera->parameter("count")->value().type() == ANARI_INT64);
  CHECK(camera->parameter("count")->value().get<std::int64_t>()
      == 9007199254740991LL);
  const std::uint16_t editedBins[3] = {65535, 1, 0};
  const double editedWeights[4] = {-4.5, 3.75, -2.5, 1.25};
  CHECK(camera->parameter("bins")->value()
      == vsr::core::Any(ANARI_UINT16_VEC3, editedBins));
  CHECK(camera->parameter("weights")->value()
      == vsr::core::Any(ANARI_FLOAT64_VEC4, editedWeights));
  CHECK_FALSE(camera->parameter("count")->isEnabled());
  CHECK_FALSE(fx.identity->isCurrent(oldCameraVersion));
  bool conflict = false;
  SECTION("successful restore preserves types and disabled state") {}
  SECTION("newer edit on the affected object blocks the older restore")
  {
    fx.edit(2, {typedEntry(camera.data(), "count", JsonValue::fromNumber(5))});
    conflict = true;
  }
  SECTION("string change back still permanently conflicts")
  {
    camera->setParameter("stereoMode", "right");
    camera->setParameter("stereoMode", "left");
    conflict = true;
  }
  SECTION("boolean change back still permanently conflicts")
  {
    point->setParameter("visible", true);
    point->setParameter("visible", false);
    conflict = true;
  }
  const auto currentCount = camera->parameter("count")->value();
  const auto cameraVersion = fx.version(camera.data());
  const auto pointVersion = fx.version(point.data());
  const auto revision = fx.identity->sceneRevision();
  const auto hooks = fx.hookCalls;
  auto restored = fx.restore(3, batch);
  CHECK(restored.outcome == (conflict ? Outcome::CONFLICT : Outcome::OK));
  CHECK(restored.committed == !conflict);
  CHECK_FALSE(camera->parameter("count")->isEnabled());
  if (conflict) {
    CHECK_FALSE(point->parameter("visible")->value().get<bool>());
    CHECK(camera->parameter("label")->value().getString() == "edited label");
    CHECK(camera->parameter("count")->value() == currentCount);
    CHECK(camera->parameter("bins")->value()
        == vsr::core::Any(ANARI_UINT16_VEC3, editedBins));
    CHECK(camera->parameter("weights")->value()
        == vsr::core::Any(ANARI_FLOAT64_VEC4, editedWeights));
    CHECK(fx.identity->isCurrent(cameraVersion));
    CHECK(fx.identity->isCurrent(pointVersion));
    CHECK(fx.identity->sceneRevision() == revision);
    CHECK(fx.hookCalls == hooks);
    CHECK(fx.restores->conflicting(batch));
  } else {
    CHECK(point->parameter("visible")->value().get<bool>());
    for (const auto &p : before)
      CHECK(camera->parameter(p.first)->value() == p.second);
    REQUIRE(restored.versions.size() == 2);
    for (const auto &v : restored.versions)
      CHECK(fx.identity->isCurrent(v));
    CHECK_FALSE(fx.identity->isCurrent(cameraVersion));
    CHECK_FALSE(fx.identity->isCurrent(pointVersion));
    CHECK(fx.hookCalls == hooks + 1);
    CHECK(fx.restores->restored(batch));
  }
  CHECK_FALSE(fx.restores->eligible(batch));
  CHECK(fx.restore(4, batch).outcome
      == (conflict ? Outcome::CONFLICT : Outcome::ALREADY_RESTORED));
}

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
                  + std::to_string(rev.domain) + "." + std::to_string(rev.value)
                  + "\"")
          != std::string::npos);
      REQUIRE(r.versions.size() == 1);
      REQUIRE(s.result.find(
                  "\"version\":" + std::to_string(r.versions[0].version) + "}")
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

SCENARIO(
    "Restoration preparation failure preserves eligibility", "[integration]")
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
    auto stale =
        fx.restores->submit(oldCreds, {oldCreds.sessionId, 2}, fx.target(b));
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

  explicit LoggedFixture(activity::Limits limits = {},
      bool nodeEdits = false,
      NotificationProbe *notificationFailure = nullptr)
      : fx(nodeEdits, notificationFailure),
        log(limits),
        sink(log, fx.integration)
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
  const auto recoveryBefore = lf.log.usage().retentionBytes;
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
          == usageBefore + activity::ActivityLog::cost(*restoreEntry));
      REQUIRE(lf.log.usage().retentionBytes == recoveryBefore - bytesA);
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

TEST_CASE("Normal parameter edits use inclusive soft-budget FIFO",
    "[integration][retention-fifo]")
{
  LoggedFixture measured;
  measured.fx.edit(
      1, {measured.fx.entry(measured.fx.matte.data(), "opacity", 0.5)});
  activity::Limits limits;
  limits.retentionBytes = measured.log.usage().retentionBytes;
  LoggedFixture lf(limits);
  auto &f = lf.fx;
  const auto first = f.edit(1, {f.entry(f.matte.data(), "opacity", 0.5)});
  CHECK(lf.log.usage().retentionBytes == limits.retentionBytes);
  const auto second = f.edit(2, {f.entry(f.other.data(), "opacity", 0.25)});
  CHECK(lf.log.eligibility(first) == activity::Eligibility::EXPIRED);
  CHECK(lf.log.eligibility(second) == activity::Eligibility::ELIGIBLE);
  CHECK(lf.log.usage().retentionBytes == limits.retentionBytes);
  // No-op/replay leave both age and eligibility unchanged.
  REQUIRE(f.edits
              ->submit(f.creds,
                  f.ref(3),
                  {f.sceneId, {f.entry(f.other.data(), "opacity", 0.25)}})
              .code
      == Outcome::OK);
  f.drain();
  REQUIRE(f.edits->result(f.ref(3)));
  CHECK_FALSE(f.edits->result(f.ref(3))->committed);
  CHECK(lf.log.usage().restorableBatches == 1);
  CHECK(f.restore(4, first).outcome == Outcome::OUTCOME_UNAVAILABLE);
  f.matte->setParameter("color", float3(0.2f, 0.3f, 0.4f));
  REQUIRE(f.restore(5, second).committed);
  CHECK(f.opacity(f.other) == 1.f);
  CHECK(f.opacity(f.matte) == 0.5f);
  CHECK(
      *f.matte->parameterValueAs<float3>("color") == float3(0.2f, 0.3f, 0.4f));
}

TEST_CASE(
    "Concurrent public admissions serialize complete recovery publication",
    "[integration][retention-fifo][retention-concurrent]")
{
  const bool reverseAdmission = GENERATE(false, true);
  activity::Limits limits;
  limits.restorableBatches = 1;
  LoggedFixture lf(limits);
  auto &f = lf.fx;
  const auto old = f.edit(1, {f.entry(f.light.data(), "irradiance", 3)});
  const auto revision = f.identity->sceneRevision();
  const auto retainedBytes = lf.log.usage().retentionBytes;
  const EditBatch left{f.sceneId, {f.entry(f.matte.data(), "opacity", 0.5)}};
  const EditBatch right{f.sceneId, {f.entry(f.other.data(), "opacity", 0.25)}};
  std::vector<const vsr::scene::Object *> writeOrder;
  writeOrder.reserve(2);
  f.scene.updateDelegate().emplace<WriteOrderObserver>(&writeOrder);
  operations::Admission a, b;
  std::atomic<bool> rightSubmitted{false};
  std::thread one([&] {
    if (reverseAdmission)
      while (!rightSubmitted)
        std::this_thread::yield();
    a = f.edits->submit(f.creds, f.ref(2), left);
  });
  std::thread two([&] {
    b = f.edits->submit(f.creds, f.ref(3), right);
    rightSubmitted = true;
  });
  one.join();
  two.join();
  INFO("left admission: " << operations::outcomeName(a.code));
  REQUIRE(b.code == Outcome::OK);
  REQUIRE_FALSE(b.replayed);
  REQUIRE(f.engine->highWater(f.sessionId) == 3);
  REQUIRE((a.code == Outcome::OK || a.code == Outcome::OUTCOME_UNAVAILABLE));
  if (reverseAdmission)
    REQUIRE(a.code == Outcome::OUTCOME_UNAVAILABLE);

  // Admission cannot evict the full history or mutate either target.
  CHECK(f.identity->sceneRevision() == revision);
  CHECK(f.identity->isCurrent(left.edits.front().expected));
  CHECK(f.identity->isCurrent(right.edits.front().expected));
  CHECK(f.opacity(f.matte) == 1.f);
  CHECK(f.opacity(f.other) == 1.f);
  CHECK(f.irradiance() == 3.f);
  CHECK(writeOrder.empty());
  CHECK(lf.log.eligibility(old) == activity::Eligibility::ELIGIBLE);
  CHECK(lf.log.usage().restorableBatches == 1);
  CHECK(lf.log.usage().retentionBytes == retainedBytes);
  CHECK(lf.log.usage().reservedRetentionBytes == 0);

  std::uint64_t leftId = 2;
  if (a.code == Outcome::OUTCOME_UNAVAILABLE) {
    REQUIRE_FALSE(f.edits->result(f.ref(2)));
    REQUIRE(f.engine->status(f.creds, f.ref(2)).code
        == Outcome::OUTCOME_UNAVAILABLE);
    REQUIRE(f.engine->slotsInUse() == 1);
    // A higher same-session ID may win concurrent admission. The lower ID
    // is then permanently unavailable, not BUSY: retry the unchanged evidence
    // once under a new monotonic ID, without servicing or refreshing versions.
    leftId = 4;
    a = f.edits->submit(f.creds, f.ref(leftId), left);
  }
  REQUIRE(a.code == Outcome::OK);
  REQUIRE_FALSE(a.replayed);
  REQUIRE(f.engine->slotsInUse() == 2);
  f.drain();
  const auto leftResult = f.edits->result(f.ref(leftId));
  const auto rightResult = f.edits->result(f.ref(3));
  REQUIRE(leftResult);
  REQUIRE(rightResult);
  REQUIRE(leftResult->outcome == Outcome::OK);
  REQUIRE(rightResult->outcome == Outcome::OK);
  REQUIRE(leftResult->committed);
  REQUIRE(rightResult->committed);
  REQUIRE(leftResult->batchRef);
  REQUIRE(rightResult->batchRef);
  REQUIRE(writeOrder.size() == 2);
  CHECK(writeOrder.front() != writeOrder.back());
  const auto first = *leftResult->batchRef;
  const auto second = *rightResult->batchRef;
  const bool leftRetained = writeOrder.back() == f.matte.data();
  CHECK(f.restores->eligible(first) == leftRetained);
  CHECK(f.restores->eligible(second) == !leftRetained);
  CHECK(lf.log.eligibility(first)
      == (leftRetained ? activity::Eligibility::ELIGIBLE
                       : activity::Eligibility::EXPIRED));
  CHECK(lf.log.eligibility(second)
      == (leftRetained ? activity::Eligibility::EXPIRED
                       : activity::Eligibility::ELIGIBLE));
  CHECK(lf.log.eligibility(old) == activity::Eligibility::EXPIRED);
  CHECK(lf.log.usage().restorableBatches == 1);
  CHECK(lf.log.usage().reservedRetentionBytes == 0);
  CHECK(f.opacity(f.matte) == 0.5f);
  CHECK(f.opacity(f.other) == 0.25f);
  CHECK(f.restore(5, leftRetained ? second : first).outcome
      == Outcome::OUTCOME_UNAVAILABLE);
  CHECK(f.restore(6, old).outcome == Outcome::OUTCOME_UNAVAILABLE);
  REQUIRE(f.restore(7, leftRetained ? first : second).committed);
  CHECK(f.opacity(f.matte) == (leftRetained ? 1.f : 0.5f));
  CHECK(f.opacity(f.other) == (leftRetained ? 0.25f : 1.f));
  CHECK(f.irradiance() == 3.f);
  CHECK(lf.log.usage().restorableBatches == 0);
}

TEST_CASE("Failed prepared edits preserve complete recovery history",
    "[integration][retention-fifo]")
{
  activity::Limits limits;
  limits.restorableBatches = 1;
  LoggedFixture lf(limits);
  auto &f = lf.fx;
  const auto old = f.edit(1, {f.entry(f.matte.data(), "opacity", 0.5)});
  operations::Request request;
  request.kind = "edit";
  request.target = session::Target::scene(f.sceneId);
  request.capability = session::Capability::EDIT;
  request.canonicalArguments = "prepared-rejection";
  request.executor = [&](operations::StepContext &ctx) {
    PreparedBatch prepared;
    std::size_t remaining = 4096;
    const EditBatch batch{
        f.sceneId, {f.entry(f.light.data(), "irradiance", 3)}};
    const auto valid = f.edits->prepare(batch, f.scene, prepared, remaining);
    REQUIRE(valid.outcome == Outcome::OK);
    const auto rejected = f.edits->commitPrepared(
        f.sceneId, prepared, ctx, [] { return Outcome::FORBIDDEN; });
    CHECK_FALSE(rejected.committed);
    operations::Step step;
    step.outcome = rejected.outcome;
    return step;
  };
  REQUIRE(f.engine->submit(f.creds, f.ref(2), std::move(request)).code
      == Outcome::OK);
  f.drain();
  CHECK(f.engine->status(f.creds, f.ref(2)).terminal == Outcome::FORBIDDEN);
  CHECK(f.irradiance() == 1.f);
  CHECK(lf.log.eligibility(old) == activity::Eligibility::ELIGIBLE);
  REQUIRE(f.restore(3, old).committed);
  CHECK(f.opacity(f.matte) == 1.f);
}

TEST_CASE(
    "Public generic commits retain recovery through late allocation faults",
    "[integration][retention-fifo][retention-allocation]")
{
  const auto stage = GENERATE(
      FaultStage::LAST_PREWRITE, FaultStage::RECEIPT, FaultStage::SYNC);
  const bool nodeEdit = GENERATE(false, true);
  activity::Limits limits;
  limits.restorableBatches = 1;
  LoggedFixture lf(limits, nodeEdit);
  auto &f = lf.fx;
  const auto old = f.edit(1, {f.entry(f.matte.data(), "opacity", 0.5)});
  auto *layer = f.scene.defaultLayer();
  const auto node = f.scene.insertChildTransformNode(layer->root());
  EditEntry entry = f.entry(f.other.data(), "opacity", 0.25);
  if (nodeEdit) {
    entry.targetKind = TargetKind::NODE;
    entry.parameter = "enabled";
    entry.value = JsonValue::ofKind(JsonKind::BOOLEAN);
    entry.value.boolean = false;
    entry.nodeExpected =
        *f.identity->nodeEditVersion(f.identity->handleOf(node));
    entry.nodeDependencies =
        *f.identity->beginNodeDependencies(entry.nodeExpected.node);
    while (entry.nodeDependencies.ancestorSlot)
      REQUIRE(f.identity->advanceNodeDependencies(entry.nodeDependencies));
  }
  const auto priorRevision = f.identity->sceneRevision();
  const auto priorVersion = f.version(f.other.data());
  const auto priorNode =
      *f.identity->nodeEditVersion(f.identity->handleOf(node));
  const auto priorBytes = lf.log.usage().retentionBytes;
  bool hit = false;
  f.edits->setFaultInjector([&](FaultStage current) {
    if (current != stage)
      return false;
    hit = true;
    return true;
  });
  const EditBatch batch{f.sceneId, {entry}};
  REQUIRE(f.edits->submit(f.creds, f.ref(2), batch).code == Outcome::OK);
  REQUIRE_NOTHROW(f.drain());
  REQUIRE(hit);
  const auto status = f.engine->status(f.creds, f.ref(2));
  const auto result = f.edits->result(f.ref(2));
  REQUIRE(result);
  f.edits->setFaultInjector({});
  if (stage == FaultStage::LAST_PREWRITE) {
    CHECK_FALSE(status.committed);
    CHECK_FALSE(result->committed);
    CHECK(status.terminal == Outcome::BUSY);
    CHECK(f.opacity(f.other) == 1.f);
    CHECK(node->value().isEnabled());
    CHECK(f.identity->sceneRevision() == priorRevision);
    CHECK(f.version(f.other.data()) == priorVersion);
    CHECK(
        *f.identity->nodeEditVersion(f.identity->handleOf(node)) == priorNode);
    CHECK(lf.log.usage().retentionBytes == priorBytes);
    CHECK(lf.log.usage().reservedRetentionBytes == 0);
    CHECK_FALSE(result->batchRef);
    CHECK(lf.log.eligibility(old) == activity::Eligibility::ELIGIBLE);
    REQUIRE(f.restore(3, old).committed);
    CHECK(f.opacity(f.matte) == 1.f);
  } else {
    CHECK(status.committed);
    CHECK(result->committed);
    CHECK(f.opacity(f.other) == (nodeEdit ? 1.f : 0.25f));
    CHECK(node->value().isEnabled() == !nodeEdit);
    CHECK(f.edits->syncState(f.sceneId) != SyncState::READY);
    CHECK(status.terminal == Outcome::APPLIED_WITH_SYNC_FAILURE);
    CHECK(status.result.find("\"committed\":true") != std::string::npos);
    REQUIRE(result->batchRef);
    CHECK(lf.log.eligibility(old) == activity::Eligibility::EXPIRED);
    CHECK(lf.log.eligibility(*result->batchRef)
        == activity::Eligibility::ELIGIBLE);
    CHECK(lf.log.usage().reservedRetentionBytes == 0);
    REQUIRE(f.edits->submit(f.creds, f.ref(2), batch).replayed);
    CHECK(lf.log.usage().restorableBatches == 1);
    CHECK(f.restore(3, old).outcome == Outcome::OUTCOME_UNAVAILABLE);
    REQUIRE(f.restore(4, *result->batchRef).committed);
    CHECK(f.opacity(f.other) == 1.f);
    CHECK(node->value().isEnabled());
    CHECK(f.opacity(f.matte) == 0.5f);
  }
}

TEST_CASE(
    "Committed generic recovery survives synchronization queue exhaustion",
    "[integration][retention-allocation]")
{
  activity::Limits limits;
  limits.restorableBatches = 1;
  LoggedFixture lf(limits);
  auto &f = lf.fx;
  const auto old = f.edit(1, {f.entry(f.matte.data(), "opacity", 0.5)});
  f.edits->setFaultInjector([&](FaultStage stage) {
    if (stage == FaultStage::RECEIPT)
      for (std::size_t i = 0; i < f.integration.queueCapacity(); ++i)
        REQUIRE(f.integration.submit(f.sceneId, [](vsr::scene::Scene &) {}));
    return false;
  });
  const auto newest = f.edit(2, {f.entry(f.other.data(), "opacity", 0.25)});
  const auto status = f.engine->status(f.creds, f.ref(2));
  CHECK(status.committed);
  CHECK(status.terminal == Outcome::APPLIED_WITH_SYNC_FAILURE);
  CHECK(status.result.find("\"committed\":true") != std::string::npos);
  CHECK(status.result.find(newest.batchId) != std::string::npos);
  CHECK(
      f.edits->result(f.ref(2))->outcome == Outcome::APPLIED_WITH_SYNC_FAILURE);
  CHECK(f.edits->syncState(f.sceneId) == SyncState::FAILED);
  CHECK(lf.log.eligibility(old) == activity::Eligibility::EXPIRED);
  CHECK(lf.log.eligibility(newest) == activity::Eligibility::ELIGIBLE);
  CHECK(f.hookCalls == 1);
  f.edits->setFaultInjector({});
  REQUIRE(f.restore(3, newest).committed);
  CHECK(f.opacity(f.other) == 1.f);
  CHECK(f.opacity(f.matte) == 0.5f);
}

TEST_CASE("Scene notification fanout rethrows the original first failure",
    "[integration][retention-notification]")
{
  const auto hook = GENERATE(NotificationHook::PARAMETER_BATCH,
      NotificationHook::LAYER_STRUCTURE,
      NotificationHook::BATCH_END);
  NotificationProbe first, second, later;
  first.failure = std::make_exception_ptr(std::runtime_error("first"));
  second.failure = std::make_exception_ptr(std::bad_alloc());
  Fixture f(true, &first);
  f.scene.updateDelegate().emplace<ThrowingWriteObserver>(&second);
  f.scene.updateDelegate().emplace<ThrowingWriteObserver>(&later);
  first.failAt = second.failAt = hook;
  std::exception_ptr received;
  try {
    if (hook == NotificationHook::PARAMETER_BATCH) {
      f.other->beginParameterBatch();
      f.other->setParameter("opacity", 0.25f);
      f.other->endParameterBatch();
    } else if (hook == NotificationHook::LAYER_STRUCTURE) {
      f.scene.signalLayerStructureChanged(f.scene.defaultLayer());
    } else {
      f.scene.beginUpdateBatch();
      f.scene.endUpdateBatch();
    }
  } catch (...) {
    received = std::current_exception();
  }
  CHECK(received == first.failure);
  const auto index = static_cast<std::size_t>(hook);
  CHECK(first.calls[index] == 1);
  CHECK(second.calls[index] == 1);
  CHECK(later.calls[index] == 1);
  first.failAt = second.failAt = NotificationHook::NONE;
  if (hook == NotificationHook::PARAMETER_BATCH) {
    CHECK(f.opacity(f.other) == 0.25f);
    // A failed notification must not leave parameters queued for the next
    // batch.
    f.other->beginParameterBatch();
    REQUIRE_NOTHROW(f.other->endParameterBatch());
    CHECK(later.parameterCount == 0);
    CHECK(later.calls[index] == 2);
  }
}

TEST_CASE("Generic publication survives failing downstream notification owners",
    "[integration][retention-allocation][retention-notification]")
{
  const auto hook = GENERATE(NotificationHook::PARAMETER_BATCH,
      NotificationHook::LAYER_STRUCTURE,
      NotificationHook::BATCH_END);
  NotificationProbe first, second, later;
  first.failure = std::make_exception_ptr(std::bad_alloc());
  second.failure = std::make_exception_ptr(std::runtime_error("second"));
  activity::Limits limits;
  limits.restorableBatches = 1;
  LoggedFixture lf(limits, true, &first);
  auto &f = lf.fx;
  f.scene.updateDelegate().emplace<ThrowingWriteObserver>(&second);
  f.scene.updateDelegate().emplace<ThrowingWriteObserver>(&later);
  const auto old = f.edit(1, {f.entry(f.matte.data(), "opacity", 0.5)});
  const auto node =
      f.scene.insertChildTransformNode(f.scene.defaultLayer()->root());
  EditEntry n;
  n.targetKind = TargetKind::NODE;
  n.nodeExpected = *f.identity->nodeEditVersion(f.identity->handleOf(node));
  n.nodeDependencies = *f.identity->beginNodeDependencies(n.nodeExpected.node);
  while (n.nodeDependencies.ancestorSlot)
    REQUIRE(f.identity->advanceNodeDependencies(n.nodeDependencies));
  n.parameter = "enabled";
  n.value = JsonValue::ofKind(JsonKind::BOOLEAN);
  const EditBatch batch{
      f.sceneId, {f.entry(f.other.data(), "opacity", 0.25), n}};
  first.calls = second.calls = later.calls = {};
  first.failAt = second.failAt = hook;
  REQUIRE(f.edits->submit(f.creds, f.ref(2), batch).code == Outcome::OK);
  REQUIRE_NOTHROW(f.drain());
  const auto status = f.engine->status(f.creds, f.ref(2));
  REQUIRE(status.committed);
  CHECK(status.terminal == Outcome::APPLIED_WITH_SYNC_FAILURE);
  CHECK(status.result.find("\"committed\":true") != std::string::npos);
  const auto result = f.edits->result(f.ref(2));
  REQUIRE(result);
  REQUIRE(result->committed);
  REQUIRE(result->batchRef);
  const auto newest = *result->batchRef;
  CHECK(f.opacity(f.other) == 0.25f);
  CHECK_FALSE(node->value().isEnabled());
  CHECK(f.edits->syncState(f.sceneId) != SyncState::READY);
  for (std::size_t i = 0; i < 3; ++i) {
    CHECK(first.calls[i] == 1);
    CHECK(second.calls[i] == 1);
    CHECK(later.calls[i] == 1);
  }
  CHECK(lf.log.eligibility(old) == activity::Eligibility::EXPIRED);
  CHECK(lf.log.eligibility(newest) == activity::Eligibility::ELIGIBLE);
  REQUIRE(result->versions.size() == 1);
  REQUIRE(result->nodeVersions.size() == 1);
  CHECK(result->versions.front() == f.version(f.other.data()));
  CHECK(result->nodeVersions.front().expected
      == *f.identity->nodeEditVersion(f.identity->handleOf(node)));
  CHECK(lf.log.usage().reservedRetentionBytes == 0);
  const auto retainedBytes = lf.log.usage().retentionBytes;
  REQUIRE(f.edits->submit(f.creds, f.ref(2), batch).replayed);
  f.drain();
  CHECK(lf.log.usage().retentionBytes == retainedBytes);
  CHECK(lf.log.usage().restorableBatches == 1);
  for (std::size_t i = 0; i < 3; ++i)
    CHECK(later.calls[i] == 1);
  first.failAt = second.failAt = NotificationHook::NONE;
  CHECK(f.restore(3, old).outcome == Outcome::OUTCOME_UNAVAILABLE);
  REQUIRE(f.restore(4, newest).committed);
  CHECK(f.opacity(f.other) == 1.f);
  CHECK(node->value().isEnabled());
  CHECK(f.opacity(f.matte) == 0.5f);
}

TEST_CASE("Generic commits survive allocation failure in sync-state listeners",
    "[integration][retention-allocation]")
{
  const auto failedTransition =
      GENERATE(SyncState::SYNCHRONIZING, SyncState::READY);
  LoggedFixture lf;
  auto &f = lf.fx;
  bool hit = false;
  f.edits->syncStates().setTransitionListener(
      [&, large = std::string(4096, 'x')](SceneId, SyncState, SyncState to) {
        if (to == failedTransition) {
          hit = true;
          throw std::bad_alloc();
        }
      });
  const auto batch = f.edit(1, {f.entry(f.matte.data(), "opacity", 0.5)});
  CHECK(hit);
  CHECK(f.edits->syncState(f.sceneId) == SyncState::FAILED);
  CHECK(f.engine->status(f.creds, f.ref(1)).committed);
  CHECK(f.engine->status(f.creds, f.ref(1)).terminal
      == Outcome::APPLIED_WITH_SYNC_FAILURE);
  CHECK(lf.log.eligibility(batch) == activity::Eligibility::ELIGIBLE);
  f.edits->syncStates().setTransitionListener({});
  REQUIRE(f.restore(2, batch).committed);
  CHECK(f.opacity(f.matte) == 1.f);
}

TEST_CASE("Last generic prewrite guard rejects changed dependency pointers",
    "[integration][retention-allocation]")
{
  LoggedFixture lf;
  auto &f = lf.fx;
  const auto old = f.edit(1, {f.entry(f.matte.data(), "opacity", 0.5)});
  const EditBatch batch{f.sceneId, {f.entry(f.other.data(), "opacity", 0.25)}};
  f.edits->setFaultInjector([&](FaultStage stage) {
    if (stage == FaultStage::LAST_PREWRITE)
      f.other->removeParameter("opacity");
    return false;
  });
  REQUIRE(f.edits->submit(f.creds, f.ref(2), batch).code == Outcome::OK);
  f.drain();
  const auto status = f.engine->status(f.creds, f.ref(2));
  CHECK_FALSE(status.committed);
  CHECK(status.terminal == Outcome::CONFLICT);
  CHECK(lf.log.eligibility(old) == activity::Eligibility::ELIGIBLE);
  CHECK(lf.log.usage().reservedRetentionBytes == 0);
  f.edits->setFaultInjector({});
  REQUIRE(f.restore(3, old).committed);
  CHECK(f.opacity(f.matte) == 1.f);
  CHECK_FALSE(f.other->parameter("opacity"));
}

TEST_CASE("Maximum public Node batch has complete bounded receipt and recovery",
    "[integration][retention-allocation]")
{
  LoggedFixture lf({}, true);
  auto &f = lf.fx;
  EditBatch batch;
  batch.scene = f.sceneId;
  std::vector<vsr::scene::LayerNodeRef> nodes;
  for (int i = 0; i < 64; ++i) {
    const auto node =
        f.scene.insertChildTransformNode(f.scene.defaultLayer()->root());
    nodes.push_back(node);
    EditEntry n;
    n.targetKind = TargetKind::NODE;
    n.nodeExpected = *f.identity->nodeEditVersion(f.identity->handleOf(node));
    n.nodeDependencies =
        *f.identity->beginNodeDependencies(n.nodeExpected.node);
    while (n.nodeDependencies.ancestorSlot)
      REQUIRE(f.identity->advanceNodeDependencies(n.nodeDependencies));
    n.parameter = "enabled";
    n.value = JsonValue::ofKind(JsonKind::BOOLEAN);
    batch.edits.push_back(n);
  }
  REQUIRE(f.edits->submit(f.creds, f.ref(1), batch).code == Outcome::OK);
  f.drain();
  const auto status = f.engine->status(f.creds, f.ref(1));
  CHECK(status.terminal == Outcome::OK);
  mcp::json::Value receipt;
  REQUIRE(mcp::json::parse(status.result, receipt));
  REQUIRE(receipt.get("node_versions"));
  CHECK(receipt.get("node_versions")->a.size() == 64);
  const auto result = f.edits->result(f.ref(1));
  REQUIRE(result);
  REQUIRE(result->batchRef);
  REQUIRE(result->nodeVersions.size() == 64);
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    CHECK_FALSE(nodes[i]->value().isEnabled());
    CHECK(result->nodeVersions[i].expected
        == *f.identity->nodeEditVersion(f.identity->handleOf(nodes[i])));
  }
  REQUIRE(f.restore(2, *result->batchRef).committed);
  for (const auto &node : nodes)
    CHECK(node->value().isEnabled());
}

SCENARIO("A batch the log expired reports outcome_unavailable", "[integration]")
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

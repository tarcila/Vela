// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// Included after the private MCP fixtures; operations still cross the real
// loopback Import/status/Restore boundary. No production service is replaced.
// vsr_io
#include "vsr/io/archives/SceneArchive.hpp"

namespace {

struct PressureReadGate
{
  PressureReadGate();
  ~PressureReadGate();
  void wait();
  std::atomic<bool> entered{false};
  std::atomic<bool> released{false};
  std::atomic<bool> timedOut{false};
};

PressureReadGate::PressureReadGate()
{
  afterImportRead = [this](int) {
    entered = true;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!released.load()) {
      if (std::chrono::steady_clock::now() >= deadline) {
        timedOut = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
}

PressureReadGate::~PressureReadGate()
{
  // Declare before the host: shutdown joins its worker before this hook dies.
  released = true;
  interceptImportRead = false;
  afterImportRead = {};
}

void PressureReadGate::wait()
{
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!entered.load()) {
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

struct PressureGateRelease
{
  explicit PressureGateRelease(PressureReadGate *gate);
  ~PressureGateRelease();
  PressureReadGate *gate{nullptr};
};

PressureGateRelease::PressureGateRelease(PressureReadGate *value) : gate(value)
{}

PressureGateRelease::~PressureGateRelease()
{
  gate->released = true;
}

json::Value pressureCapacity(
    Fixture &host, const Session &session, std::uint64_t request)
{
  host.tool(session,
      "vsr_discover",
      "{\"request_id\":" + std::to_string(request) + "}");
  const auto reply = host.await(session, request);
  REQUIRE(reply.str("outcome") == "ok");
  const auto *limits = reply.result()
                           ->get("items")
                           ->a.at(0)
                           .get("image_exchange")
                           ->get("exchange_limits");
  REQUIRE(limits);
  return *limits;
}

std::string pressureNodeEdit(Fixture &host,
    const Session &session,
    std::uint64_t inspectId,
    bool enabled)
{
  host.tool(session,
      "vsr_inspect",
      "{\"request_id\":" + std::to_string(inspectId)
          + ",\"scene_id\":" + std::to_string(host.sceneId)
          + ",\"kind\":\"nodes\",\"name\":\"pressure-node\"}");
  const auto inspected = host.await(session, inspectId);
  REQUIRE(inspected.str("outcome") == "ok");
  const auto &item = inspected.result()->get("items")->a.at(0);
  return "{\"target_kind\":\"node\",\"handle\":"
      + json::write(*item.get("handle")) + ",\"version\":"
      + json::write(*item.get("node_edit_version")) + ",\"structural_version\":"
      + json::write(*item.get("structural_version"))
      + ",\"parameter\":\"enabled\",\"value\":"
      + (enabled ? "true}" : "false}");
}

struct PressurePublicationObserver : vsr::scene::EmptyUpdateDelegate
{
  void signalArrayUnmapped(const vsr::scene::Array *array) override;
  std::vector<const vsr::scene::Array *> writes;
};

void PressurePublicationObserver::signalArrayUnmapped(
    const vsr::scene::Array *array)
{
  writes.push_back(array);
}

} // namespace

TEST_CASE("Concurrent public Image admissions publish both logical changes",
    "[integration][mcp][retention-pressure][pressure-admissions]")
{
  const bool reverseAdmission = GENERATE(false, true);
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 400000;
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, limits, true);
  const auto session = f.attach();
  const auto target = f.firstTarget(session, 1);
  f.editOpacity(session, 2, target, 0.25);
  const auto old = f.await(session, 2);
  REQUIRE(old.flag("committed"));
  const auto oldBatch = *old.result()->get("batch_ref");
  auto a = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
  auto b = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
  const std::vector<unsigned char> before(262144, 71), rows(262400, 0);
  a->setData(static_cast<const void *>(before.data()));
  b->setData(static_cast<const void *>(before.data()));
  writeInput(f.exchangePath() + "/both.png", inputPng(256, 256, 6, rows, {}));
  const auto aHandle = f.sceneIds->handleOf(a.data());
  const auto bHandle = f.sceneIds->handleOf(b.data());
  const auto aVersion = *f.sceneIds->imageDataVersion(aHandle);
  const auto bVersion = *f.sceneIds->imageDataVersion(bHandle);
  const operations::Credentials credentials{
      f.authority->launchCredential(), session.id, session.secret};
  operations::Admission left, right;
  std::atomic<bool> higherAdmitted{false};
  std::thread lower([&] {
    if (reverseAdmission)
      while (!higherAdmitted.load())
        std::this_thread::yield();
    left = f.images->submitImport(
        credentials, {session.id, 3}, f.sceneId, aHandle, aVersion, "both.png");
  });
  std::thread higher([&] {
    right = f.images->submitImport(
        credentials, {session.id, 4}, f.sceneId, bHandle, bVersion, "both.png");
    higherAdmitted = true;
  });
  lower.join();
  higher.join();
  REQUIRE(right.code == Outcome::OK);
  REQUIRE(
      (left.code == Outcome::OK || left.code == Outcome::OUTCOME_UNAVAILABLE));
  if (reverseAdmission)
    REQUIRE(left.code == Outcome::OUTCOME_UNAVAILABLE);
  CHECK(f.opacity0() == 0.25f);
  CHECK(std::memcmp(a->data(), before.data(), before.size()) == 0);
  CHECK(std::memcmp(b->data(), before.data(), before.size()) == 0);
  REQUIRE(f.restores->eligible(
      {oldBatch.get("launch_id")->s, oldBatch.get("batch_id")->s}));
  std::uint64_t leftId = 3;
  if (left.code == Outcome::OUTCOME_UNAVAILABLE) {
    // This is the session's monotonic admission high-water, not BUSY or a
    // retention denial. Retry once with a fresh ID, without refreshing
    // evidence.
    leftId = 5;
    left = f.images->submitImport(credentials,
        {session.id, leftId},
        f.sceneId,
        aHandle,
        aVersion,
        "both.png");
  }
  REQUIRE(left.code == Outcome::OK);
  CHECK_FALSE(left.replayed);
  CHECK_FALSE(right.replayed);
  auto *observer =
      f.scene.updateDelegate().emplace<PressurePublicationObserver>();
  observer->writes.reserve(3);
  const auto completedLeft = f.await(session, leftId);
  const auto completedRight = f.await(session, 4);
  REQUIRE(completedLeft.flag("committed"));
  REQUIRE(completedRight.flag("committed"));
  REQUIRE(completedLeft.str("outcome") == "ok");
  REQUIRE(completedRight.str("outcome") == "ok");
  REQUIRE(observer->writes.size() == 2);
  const bool leftNewest = observer->writes.back() == a.data();
  const auto newest =
      *(leftNewest ? completedLeft : completedRight).result()->get("batch_ref");
  const auto expired =
      *(leftNewest ? completedRight : completedLeft).result()->get("batch_ref");
  CHECK(f.images
          ->submitImport(credentials,
              {session.id, leftId},
              f.sceneId,
              aHandle,
              aVersion,
              "both.png")
          .replayed);
  CHECK(f.images
          ->submitImport(credentials,
              {session.id, 4},
              f.sceneId,
              bHandle,
              bVersion,
              "both.png")
          .replayed);
  f.restoreBatch(session, 6, expired);
  CHECK(f.await(session, 6).str("outcome") == "outcome_unavailable");
  f.restoreBatch(session, 7, oldBatch);
  CHECK(f.await(session, 7).str("outcome") == "outcome_unavailable");
  f.restoreBatch(session, 8, newest);
  REQUIRE(f.await(session, 8).flag("committed"));
  const std::vector<unsigned char> zero(before.size(), 0);
  CHECK(std::memcmp((leftNewest ? a : b)->data(), before.data(), before.size())
      == 0);
  CHECK(
      std::memcmp((leftNewest ? b : a)->data(), zero.data(), zero.size()) == 0);
  CHECK(f.opacity0() == 0.25f);
  CHECK(f.imageActivity->usage().restorableBatches == 0);
}

TEST_CASE("Pending public Mesh work ages at publication, not arrival",
    "[integration][mcp][retention-pressure][pressure-publication]")
{
  const bool oversized = GENERATE(false, true);
  const bool cancel = GENERATE(false, true);
  PressureReadGate gate;
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 400000;
  limits.hardRetentionBytes = 2000000;
  PositionFixture f(limits, true);
  PressureGateRelease releaseGate(&gate);
  const auto session = f.attach();
  const auto evidence = f.working(session);
  auto *layer = f.scene.defaultLayer();
  auto node = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, "pressure-node"));
  const auto edits = pressureNodeEdit(f, session, 2, false);
  f.tool(session,
      "vsr_edit",
      "{\"request_id\":3,\"scene_id\":" + std::to_string(f.sceneId)
          + ",\"edits\":[" + edits + "]}");
  const auto old = f.await(session, 3);
  REQUIRE(old.flag("committed"));
  const auto oldBatch = *old.result()->get("batch_ref");
  const auto side = oversized ? 512u : 256u;
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, side, side);
  const std::vector<unsigned char> before(side * side * 4, 71),
      rows(side * (side * 4 + 1), 0);
  image->setData(static_cast<const void *>(before.data()));
  writeInput(
      f.exchangePath() + "/pressure.png", inputPng(side, side, 6, rows, {}));
  const auto imageHandle = f.objectHandle(image.data());
  const auto imageVersion =
      *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(image.data()));

  interceptImportRead = true;
  REQUIRE(f.importPositions(session, 4, *evidence.result()).str("state")
      == "pending");
  REQUIRE(f.integration.service() > 0);
  gate.wait();
  CHECK(f.status(session, session.id, 4).str("state") != "terminal");
  CHECK_FALSE(node->value().isEnabled());
  CHECK(f.restores->eligible(
      {oldBatch.get("launch_id")->s, oldBatch.get("batch_id")->s}));
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == 2);

  f.importImage(session, 5, imageHandle, imageVersion, "pressure.png");
  const auto newerArrival = f.await(session, 5);
  REQUIRE(newerArrival.flag("committed"));
  const auto imageBatch = *newerArrival.result()->get("batch_ref");
  CHECK(f.status(session, session.id, 4).str("state") != "terminal");
  CHECK(f.restores->eligible(
            {oldBatch.get("launch_id")->s, oldBatch.get("batch_id")->s})
      == !oversized);
  CHECK(f.restores->eligible(
      {imageBatch.get("launch_id")->s, imageBatch.get("batch_id")->s}));
  const auto capacity = pressureCapacity(f, session, 6);
  CHECK(capacity.get("oversized_retention")->b == oversized);
  CHECK(capacity.get("retained_physical_bytes")->n > 0);

  if (cancel)
    REQUIRE(
        f.tool(session,
             "vsr_cancel",
             "{\"operation_ref\":" + ref(session.id, 4) + "}")
            .errorCode()
            .empty());
  gate.released = true;
  const auto mesh = f.await(session, 4);
  REQUIRE_FALSE(gate.timedOut.load());
  if (cancel) {
    REQUIRE(mesh.str("outcome") == "cancelled");
    CHECK_FALSE(mesh.flag("committed"));
    CHECK(
        f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
        == f.positions.data());
    CHECK(f.restores->eligible(
        {imageBatch.get("launch_id")->s, imageBatch.get("batch_id")->s}));
    f.restoreBatch(session, 7, imageBatch);
    REQUIRE(f.await(session, 7).flag("committed"));
    CHECK(std::memcmp(image->data(), before.data(), before.size()) == 0);
    if (!oversized) {
      f.restoreBatch(session, 8, oldBatch);
      REQUIRE(f.await(session, 8).flag("committed"));
      CHECK(node->value().isEnabled());
    }
  } else {
    REQUIRE(mesh.flag("committed"));
    // The earlier-arriving Mesh is newest because its publication was last.
    f.restoreBatch(session, 7, imageBatch);
    CHECK(f.await(session, 7).str("outcome") == "outcome_unavailable");
    f.restoreBatch(session, 8, oldBatch);
    CHECK(f.await(session, 8).str("outcome") == "outcome_unavailable");
    REQUIRE(f.importImage(session, 5, imageHandle, imageVersion, "pressure.png")
            .flag("replayed"));
    f.restoreBatch(session, 9, *mesh.result()->get("batch_ref"));
    REQUIRE(f.await(session, 9).flag("committed"));
    CHECK(
        f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
        == f.positions.data());
    CHECK_FALSE(node->value().isEnabled());
    const std::vector<unsigned char> zero(before.size(), 0);
    CHECK(std::memcmp(image->data(), zero.data(), zero.size()) == 0);
  }
  CHECK(std::filesystem::exists(f.exchangePath() + "/pressure.png"));
  CHECK(std::filesystem::exists(f.exchangePath() + "/edited.vmesh"));
}

TEST_CASE("Late Mesh preparation faults preserve complete mixed recovery",
    "[integration][mcp][retention-pressure][pressure-faults]")
{
  const auto fault = GENERATE(0, 1, 2, 3, 4);
  PositionFixture *host = nullptr;
  bool armed = false, prepared = false, fired = false, escaped = false;
  Session session;
  const auto physical =
      std::make_shared<vsr::rendering::FrameBudget>(16u << 20);
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 400000;
  limits.physicalByteCap = physical->limit();
  limits.reservePhysicalMemory = [&](std::size_t bytes) {
    if (armed && host) {
      escaped = escaped || host->scene.numberOfObjects(ANARI_ARRAY) != 2;
      if (bytes == 4096) {
        prepared = true;
        if (fault == 0) {
          fired = true;
          return std::shared_ptr<const void>{};
        }
      }
      if (prepared && bytes == 65536 && !fired) {
        fired = true;
        if (fault == 1)
          host->authority->setAgentEditsDisabled(true);
        else if (fault == 2)
          host->authority->restrict(
              session::Target::scene(host->sceneId), session::Capability::READ);
        else if (fault == 3)
          host->positions->setData(
              static_cast<const void *>(PositionFixture::POINTS));
        else
          return std::shared_ptr<const void>{};
      }
    }
    return physical->reserve(bytes);
  };
  PositionFixture f(limits, true);
  host = &f;
  session = f.attach();
  const auto evidence = f.working(session, "tracer", true);
  auto *layer = f.scene.defaultLayer();
  auto node = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, "pressure-node"));
  const auto nodeEdit = pressureNodeEdit(f, session, 2, false);
  f.tool(session,
      "vsr_inspect",
      "{\"request_id\":3,\"scene_id\":" + std::to_string(f.sceneId)
          + ",\"object_type\":\"ANARI_MATERIAL\"}");
  const auto inspected = f.await(session, 3);
  const auto &material = inspected.result()->get("items")->a.at(0);
  const auto objectEdit = "{\"handle\":" + json::write(*material.get("handle"))
      + ",\"version\":" + json::write(*material.get("version"))
      + ",\"parameter\":\"opacity\",\"value\":0.25}";
  f.tool(session,
      "vsr_edit",
      "{\"request_id\":4,\"scene_id\":" + std::to_string(f.sceneId)
          + ",\"edits\":[" + nodeEdit + "," + objectEdit + "]}");
  const auto old = f.await(session, 4);
  REQUIRE(old.flag("committed"));
  const auto oldBatch = *old.result()->get("batch_ref");
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
  const std::vector<unsigned char> before(262144, 71), rows(262400, 0);
  image->setData(static_cast<const void *>(before.data()));
  writeInput(f.exchangePath() + "/old.png", inputPng(256, 256, 6, rows, {}));
  f.importImage(session,
      5,
      f.objectHandle(image.data()),
      *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(image.data())),
      "old.png");
  const auto imported = f.await(session, 5);
  REQUIRE(imported.flag("committed"));
  const auto imageBatch = *imported.result()->get("batch_ref");
  const auto revision = f.sceneIds->sceneRevision();
  const auto charge = f.imageActivity->usage().retentionBytes;
  armed = true;
  f.importPositions(session, 6, *evidence.result());
  const auto failed = f.await(session, 6);
  armed = false;
  INFO(json::write(failed.json));
  REQUIRE(fired);
  REQUIRE(prepared);
  CHECK_FALSE(escaped);
  const char *outcomes[] = {
      "busy", "forbidden", "forbidden", "conflict", "busy"};
  CHECK(failed.str("outcome") == outcomes[fault]);
  CHECK_FALSE(failed.flag("committed"));
  if (fault != 3)
    CHECK(f.sceneIds->sceneRevision() == revision);
  CHECK(f.imageActivity->usage().retentionBytes == charge);
  CHECK(f.imageActivity->usage().reservedRetentionBytes == 0);
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == 2);
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK(f.other->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK(std::memcmp(f.positions->data(),
            PositionFixture::POINTS,
            sizeof(PositionFixture::POINTS))
      == 0);
  CHECK_FALSE(node->value().isEnabled());
  CHECK(f.opacity0() == 0.25f);
  const std::vector<unsigned char> zero(before.size(), 0);
  CHECK(std::memcmp(image->data(), zero.data(), zero.size()) == 0);
  f.authority->setAgentEditsDisabled(false);
  f.authority->relax(
      session::Target::scene(f.sceneId), session::Capability::READ);
  CHECK(f.importPositions(session, 6, *evidence.result()).flag("replayed"));
  f.restoreBatch(session, 7, imageBatch);
  REQUIRE(f.await(session, 7).flag("committed"));
  CHECK(std::memcmp(image->data(), before.data(), before.size()) == 0);
  f.restoreBatch(session, 8, oldBatch);
  REQUIRE(f.await(session, 8).flag("committed"));
  CHECK(node->value().isEnabled());
  CHECK(f.opacity0() == 1.f);
}

TEST_CASE("Real Image allocator failure cannot evict older Mesh and Image",
    "[integration][mcp][retention-pressure][pressure-allocator]")
{
  const auto allocations = GENERATE(0, 1, 2, 3, 4);
  ImageAllocator allocator;
  allocator.remaining = 1000;
  image::ExchangeLimits limits;
  limits.allocator = &allocator;
  limits.globalRetentionBytes = 500000;
  PositionFixture f(limits);
  const auto session = f.attach();
  const auto evidence = f.working(session);
  f.importPositions(session, 2, *evidence.result());
  const auto mesh = f.await(session, 2);
  REQUIRE(mesh.flag("committed"));
  const auto meshBatch = *mesh.result()->get("batch_ref");
  auto small = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  const unsigned char original[] = {71, 71, 71, 71};
  small->setData(static_cast<const void *>(original));
  writeInput(
      f.exchangePath() + "/small.png", inputPng(1, 1, 6, {0, 1, 2, 3, 4}, {}));
  f.importImage(session,
      3,
      f.objectHandle(small.data()),
      *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(small.data())),
      "small.png");
  const auto old = f.await(session, 3);
  REQUIRE(old.flag("committed"));
  const auto oldBatch = *old.result()->get("batch_ref");
  REQUIRE(f.restores->eligible(
      {meshBatch.get("launch_id")->s, meshBatch.get("batch_id")->s}));
  auto large = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
  const std::vector<unsigned char> before(262144, 71), rows(262400, 0);
  large->setData(static_cast<const void *>(before.data()));
  writeInput(f.exchangePath() + "/agent.png", inputPng(256, 256, 6, rows, {}));
  const auto revision = f.sceneIds->sceneRevision();
  const auto retained = f.imageActivity->usage().retentionBytes;
  allocator.remaining = allocations;
  f.importImage(session,
      4,
      f.objectHandle(large.data()),
      *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(large.data())),
      "agent.png");
  const auto failed = f.await(session, 4);
  INFO("successful PMR allocations before refusal: " << allocations);
  REQUIRE(failed.str("outcome") == "busy");
  CHECK_FALSE(failed.flag("committed"));
  CHECK(failed.result()->get("reason")->s == "resource_limit");
  CHECK(f.sceneIds->sceneRevision() == revision);
  CHECK(f.imageActivity->usage().retentionBytes == retained);
  CHECK(f.imageActivity->usage().reservedRetentionBytes == 0);
  CHECK(std::memcmp(large->data(), before.data(), before.size()) == 0);
  CHECK_FALSE(large->isMapped());
  allocator.remaining = 1000;
  f.restoreBatch(session, 5, oldBatch);
  REQUIRE(f.await(session, 5).flag("committed"));
  CHECK(std::memcmp(small->data(), original, sizeof(original)) == 0);
  f.restoreBatch(session, 6, meshBatch);
  REQUIRE(f.await(session, 6).flag("committed"));
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
}

TEST_CASE(
    "Evicted pinned Mesh owners survive GC, adoption and deferred release",
    "[integration][mcp][retention-pressure][pressure-lifetime]")
{
  const auto memory = std::make_shared<vsr::rendering::FrameBudget>(16u << 20);
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 400000;
  limits.hardRetentionBytes = 2000000;
  limits.reservePhysicalMemory = [memory](std::size_t bytes) {
    return memory->reserve(bytes);
  };
  limits.physicalByteCap = memory->limit();
  PositionFixture f(limits);
  const auto session = f.attach();
  const auto evidence = f.working(session, "tracer", true);
  f.importPositions(session, 2, *evidence.result());
  const auto mesh = f.await(session, 2);
  REQUIRE(mesh.flag("committed"));
  const auto batch = *mesh.result()->get("batch_ref");
  const operations::Credentials credentials{
      f.authority->launchCredential(), session.id, session.secret};
  std::optional<operations::Status> receipt =
      f.engine->status(credentials, {session.id, 2});
  auto receiptCopy = receipt;
  auto held = f.edits->restorations().find(
      {batch.get("launch_id")->s, batch.get("batch_id")->s});
  REQUIRE(held);
  const auto oldHandle = f.sceneIds->handleOf(f.positions.data());
  auto *replacement =
      f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position");
  const auto adoptedHandle = f.sceneIds->handleOf(replacement);
  const auto adoptedBytes = std::vector<unsigned char>(
      static_cast<const unsigned char *>(replacement->data()),
      static_cast<const unsigned char *>(replacement->data())
          + replacement->size() * replacement->elementSize());
  const bool restoreConflict = GENERATE(false, true);
  // Mutating and changing back shared old storage must still conflict.
  const float changed[] = {7, 8, 9, 10, 11, 12, 13, 14, 15};
  f.positions->setData(static_cast<const void *>(changed));
  f.positions->setData(static_cast<const void *>(PositionFixture::POINTS));
  const auto revision = f.sceneIds->sceneRevision();
  if (restoreConflict) {
    f.restoreBatch(session, 3, batch);
    CHECK(f.await(session, 3).str("outcome") == "conflict");
    CHECK(f.sceneIds->sceneRevision() == revision);
  }
  f.other->setParameterObject("vertex.position", *replacement);
  f.scene.removeUnusedObjects();
  f.scene.defragmentObjectStorage();
  REQUIRE(f.sceneIds->resolve(oldHandle));
  REQUIRE(f.sceneIds->resolve(adoptedHandle));

  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 512, 512);
  const auto imageIdentity = f.sceneIds->handleOf(image.data());
  auto user = f.scene.createObject<vsr::scene::Light>("hdri");
  user->setParameterObject("radiance", *image);
  f.scene.insertChildObjectNode(f.scene.defaultLayer()->root(), user);
  const std::vector<unsigned char> before(1048576, 71), rows(1049088, 0);
  image->setData(static_cast<const void *>(before.data()));
  writeInput(f.exchangePath() + "/large.png", inputPng(512, 512, 6, rows, {}));
  f.importImage(session,
      4,
      f.objectHandle(image.data()),
      *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(image.data())),
      "large.png");
  const auto newer = f.await(session, 4);
  REQUIRE(newer.flag("committed"));
  f.restoreBatch(session, 5, batch);
  // A previously conflicting reference is a consumed conflict tombstone,
  // not an eligible entry evicted by this publication.
  CHECK(f.await(session, 5).str("outcome")
      == (restoreConflict ? "conflict" : "outcome_unavailable"));
  CHECK(receipt->result == receiptCopy->result);
  json::Value metadata;
  REQUIRE(json::parse(f.images->metadata(f.sceneId, session.id), metadata));
  const auto charged =
      metadata.get("exchange_limits")->get("retained_physical_bytes")->n;
  CHECK(charged > before.size());
  f.scene.removeUnusedObjects();
  f.scene.defragmentObjectStorage();
  REQUIRE(f.sceneIds->resolve(oldHandle));
  std::thread release([&] { held.reset(); });
  release.join();
  // Off-thread release cannot touch Scene storage; its deferred owner keeps
  // the payload lease until the owning-thread pump actually drops the pins.
  REQUIRE(json::parse(f.images->metadata(f.sceneId, session.id), metadata));
  CHECK(metadata.get("exchange_limits")->get("retained_physical_bytes")->n
      == charged);
  REQUIRE(f.sceneIds->resolve(oldHandle));
  f.images->pump();
  f.scene.removeUnusedObjects();
  f.scene.defragmentObjectStorage();
  CHECK_FALSE(f.sceneIds->resolve(oldHandle));
  REQUIRE(f.sceneIds->resolve(adoptedHandle));
  auto *adopted =
      dynamic_cast<vsr::scene::Array *>(f.sceneIds->resolve(adoptedHandle));
  REQUIRE(adopted);
  CHECK(std::memcmp(adopted->data(), adoptedBytes.data(), adoptedBytes.size())
      == 0);
  REQUIRE(json::parse(f.images->metadata(f.sceneId, session.id), metadata));
  CHECK(metadata.get("exchange_limits")->get("retained_physical_bytes")->n
      == before.size());
  f.restoreBatch(session, 6, *newer.result()->get("batch_ref"));
  REQUIRE(f.await(session, 6).flag("committed"));
  auto *liveImage =
      dynamic_cast<vsr::scene::Array *>(f.sceneIds->resolve(imageIdentity));
  REQUIRE(liveImage);
  CHECK(std::memcmp(liveImage->data(), before.data(), before.size()) == 0);
  CHECK(f.status(session, session.id, 2).flag("committed"));
}

TEST_CASE("Public Image charge boundaries preserve history until publication",
    "[integration][mcp][retention-pressure][pressure-boundaries]")
{
  std::uint64_t charge = 0;
  const std::vector<unsigned char> before(262144, 71), rows(262400, 0);
  {
    image::ExchangeLimits limits;
    limits.globalRetentionBytes = 10000;
    limits.hardRetentionBytes = 10000;
    Fixture probe({}, true, false, false, 120., true, UINT64_MAX, limits, true);
    const auto session = probe.attach();
    auto image = probe.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
    image->setData(static_cast<const void *>(before.data()));
    writeInput(
        probe.exchangePath() + "/large.png", inputPng(256, 256, 6, rows, {}));
    probe.importImage(session,
        1,
        probe.objectHandle(image.data()),
        *probe.sceneIds->imageDataVersion(
            probe.sceneIds->handleOf(image.data())),
        "large.png");
    const auto refused = probe.await(session, 1);
    REQUIRE_FALSE(refused.flag("committed"));
    REQUIRE(refused.result()->get("required_capacity_bytes"));
    charge = std::uint64_t(refused.result()->get("required_capacity_bytes")->n);
    REQUIRE(charge > 262144);
  }
  const auto boundary = GENERATE(0, 1, 2, 3);
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = boundary == 0 ? charge : charge - 1;
  limits.hardRetentionBytes = boundary == 3 ? charge - 1
      : boundary == 2                       ? charge
                                            : charge + 1;
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, limits, true);
  const auto session = f.attach();
  auto small = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  const unsigned char original[] = {71, 71, 71, 71};
  small->setData(static_cast<const void *>(original));
  writeInput(
      f.exchangePath() + "/small.png", inputPng(1, 1, 6, {0, 1, 2, 3, 4}, {}));
  f.importImage(session,
      1,
      f.objectHandle(small.data()),
      *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(small.data())),
      "small.png");
  const auto previous = f.await(session, 1);
  REQUIRE(previous.flag("committed"));
  const auto batch = *previous.result()->get("batch_ref");
  auto large = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
  large->setData(static_cast<const void *>(before.data()));
  writeInput(f.exchangePath() + "/large.png", inputPng(256, 256, 6, rows, {}));
  const auto revision = f.sceneIds->sceneRevision();
  f.importImage(session,
      2,
      f.objectHandle(large.data()),
      *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(large.data())),
      "large.png");
  // Admission alone is not eviction, even when the exact charge fills the pool.
  REQUIRE(f.restores->eligible(
      {batch.get("launch_id")->s, batch.get("batch_id")->s}));
  const auto result = f.await(session, 2);
  INFO("boundary=" << boundary << " measured public charge=" << charge);
  if (boundary == 3) {
    CHECK_FALSE(result.flag("committed"));
    CHECK(result.result()->get("approval_required")->b);
    CHECK(result.result()->get("required_capacity_bytes")->n == charge);
    CHECK(f.sceneIds->sceneRevision() == revision);
    CHECK(std::memcmp(large->data(), before.data(), before.size()) == 0);
    f.restoreBatch(session, 3, batch);
    REQUIRE(f.await(session, 3).flag("committed"));
    CHECK(std::memcmp(small->data(), original, sizeof(original)) == 0);
  } else {
    REQUIRE(result.flag("committed"));
    REQUIRE(result.result()->get("restoration_policy"));
    CHECK(result.result()
              ->get("restoration_policy")
              ->get("required_capacity_bytes")
              ->n
        == charge);
    CHECK(result.result()->get("restoration_policy")->get("oversized")->b
        == (boundary != 0));
    const auto capacity = pressureCapacity(f, session, 3);
    CHECK(capacity.get("available_global_retention_bytes")->n == 0);
    f.restoreBatch(session, 4, batch);
    CHECK(f.await(session, 4).str("outcome") == "outcome_unavailable");
    f.restoreBatch(session, 5, *result.result()->get("batch_ref"));
    REQUIRE(f.await(session, 5).flag("committed"));
    CHECK(std::memcmp(large->data(), before.data(), before.size()) == 0);
  }
}

TEST_CASE("Seeded cross-session mixed pressure keeps exact newest recovery",
    "[integration][mcp][retention-pressure][pressure-seeded]")
{
  const char *configuredSeed = std::getenv("VSR_RETENTION_PRESSURE_SEED");
  std::uint32_t seed = configuredSeed
      ? std::uint32_t(std::strtoul(configuredSeed, nullptr, 10))
      : 104729;
  REQUIRE(seed != 0);
  INFO("reproduce with VSR_RETENTION_PRESSURE_SEED=" << seed);
  const auto random = [&]() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
  };
  // Twelve sessions retain real receipt/control owners. A 512x512 decoder
  // also needs 12,651,520 transient bytes. Size this independent working pool
  // for that overlap; the recovery target remains only 400,000 bytes.
  const auto memory = std::make_shared<vsr::rendering::FrameBudget>(32u << 20);
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 400000;
  limits.hardRetentionBytes = 2000000;
  limits.reservePhysicalMemory = [memory](std::size_t bytes) {
    return memory->reserve(bytes);
  };
  limits.physicalByteCap = memory->limit();
  PositionFixture f(limits, true);
  auto *layer = f.scene.defaultLayer();
  auto node = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, "pressure-node"));
  auto small = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
  auto large = f.scene.createArray(ANARI_UFIXED8_VEC4, 512, 512);
  small->setName("pressure-small-image");
  large->setName("pressure-large-image");
  const std::vector<unsigned char> smallInitial(262144, 71),
      largeInitial(1048576, 71);
  small->setData(static_cast<const void *>(smallInitial.data()));
  large->setData(static_cast<const void *>(largeInitial.data()));
  auto smallUser = f.scene.createObject<vsr::scene::Light>("hdri");
  smallUser->setParameterObject("radiance", *small);
  auto largeUser = f.scene.createObject<vsr::scene::Light>("hdri");
  largeUser->setParameterObject("radiance", *large);
  f.scene.insertChildObjectNode(layer->root(), smallUser);
  f.scene.insertChildObjectNode(layer->root(), largeUser);
  TempDir assets;
  const auto source = assets.path + "/source.obj";
  std::ofstream(source) << "original mesh asset\n";
  const auto oldPositions = f.sceneIds->handleOf(f.positions.data());
  const auto smallIdentity = f.sceneIds->handleOf(small.data());
  const auto largeIdentity = f.sceneIds->handleOf(large.data());
  json::Value permanentlyExpired;
  for (unsigned cycle = 0; cycle < 12; ++cycle) {
    INFO("pressure cycle=" << cycle);
    const bool meshFirst = (random() & 1) != 0;
    const bool oversized = (random() & 1) != 0;
    // This host has no explicit backend-recovery grant. Put the failed sync
    // last: setting a hook back to true does not erase a FAILED sync state.
    const bool syncFailure = cycle == 11;
    const auto session = f.attach();
    const auto evidence = f.working(session);
    const auto path = f.exchangePath();
    const auto edit =
        pressureNodeEdit(f, session, 2, !node->value().isEnabled());
    const auto editArgs = "{\"request_id\":3,\"scene_id\":"
        + std::to_string(f.sceneId) + ",\"edits\":[" + edit + "]}";
    f.tool(session, "vsr_edit", editArgs);
    const auto generic = f.await(session, 3);
    REQUIRE(generic.flag("committed"));
    const auto nodeBatch = *generic.result()->get("batch_ref");
    auto image = oversized ? large : small;
    const auto side = oversized ? 512u : 256u;
    const auto imageBefore = std::vector<unsigned char>(
        static_cast<const unsigned char *>(image->data()),
        static_cast<const unsigned char *>(image->data()) + side * side * 4);
    std::vector<unsigned char> rows(side * (side * 4 + 1), 0);
    for (std::size_t y = 0; y < side; ++y)
      std::fill_n(rows.begin() + y * (side * 4 + 1) + 1,
          side * 4,
          static_cast<unsigned char>(cycle + 1));
    writeInput(path + "/cycle.png", inputPng(side, side, 6, rows, {}));
    const auto imageHandle = f.objectHandle(image.data());
    const auto imageVersion =
        *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(image.data()));
    const auto meshBefore = f.sceneIds->handleOf(
        f.geometry->parameterValueAsObject<vsr::scene::Array>(
            "vertex.position"));
    const auto submit = [&](bool mesh, std::uint64_t id) {
      if (mesh)
        f.importPositions(session, id, *evidence.result());
      else
        f.importImage(session, id, imageHandle, imageVersion, "cycle.png");
      return f.await(session, id);
    };
    const auto firstUsage = memory->used();
    const auto first = submit(meshFirst, 4);
    INFO("physical bytes before first publication=" << firstUsage);
    INFO(json::write(first.json));
    REQUIRE(first.flag("committed"));
    const auto firstBatch = *first.result()->get("batch_ref");
    CHECK(f.status(session, session.id, 3).flag("committed"));
    f.hookOk = !syncFailure;
    const auto newest = submit(!meshFirst, 5);
    REQUIRE(newest.flag("committed"));
    CHECK(newest.str("outcome")
        == (syncFailure ? "applied_with_sync_failure" : "ok"));
    f.hookOk = true;
    const auto newestBatch = *newest.result()->get("batch_ref");
    CHECK(f.tool(session, "vsr_edit", editArgs).flag("replayed"));
    const auto replay = meshFirst
        ? f.importPositions(session, 4, *evidence.result())
        : f.importImage(session, 4, imageHandle, imageVersion, "cycle.png");
    CHECK(replay.flag("replayed"));
    f.restoreBatch(session, 6, nodeBatch);
    CHECK(f.await(session, 6).str("outcome") == "outcome_unavailable");
    f.restoreBatch(session, 7, firstBatch);
    CHECK(f.await(session, 7).str("outcome") == "outcome_unavailable");
    if (cycle) {
      f.restoreBatch(session, 8, permanentlyExpired);
      CHECK(f.await(session, 8).str("outcome") == "outcome_unavailable");
    }
    permanentlyExpired = firstBatch;
    const auto capacity = pressureCapacity(f, session, 9);
    CHECK(capacity.get("oversized_retention")->b == (meshFirst && oversized));
    CHECK(capacity.get("effective_global_retention_byte_cap")->n == 400000);
    CHECK(capacity.get("effective_single_entry_hard_cap_bytes")->n == 2000000);
    const auto archive = assets.path + "/scene.vsr";
    REQUIRE(vsr::io::save_SceneArchive(f.scene, archive.c_str()));
    vsr::scene::Scene loaded;
    REQUIRE(vsr::io::load_SceneArchive(loaded, archive.c_str()));
    REQUIRE(loaded.numberOfObjects(ANARI_GEOMETRY) == 2);
    auto loadedGeometry = loaded.getObject<vsr::scene::Geometry>(0);
    auto *loadedPositions =
        loadedGeometry->parameterValueAsObject<vsr::scene::Array>(
            "vertex.position");
    auto *currentPositions =
        f.geometry->parameterValueAsObject<vsr::scene::Array>(
            "vertex.position");
    REQUIRE(loadedPositions);
    CHECK(loadedPositions->size() == currentPositions->size());
    CHECK(std::memcmp(loadedPositions->data(),
              currentPositions->data(),
              currentPositions->size() * currentPositions->elementSize())
        == 0);
    vsr::scene::Array *loadedImage = nullptr;
    for (std::size_t index = 0; index < loaded.numberOfObjects(ANARI_ARRAY);
        ++index) {
      auto candidate = loaded.getObject<vsr::scene::Array>(index);
      if (candidate->name() == image->name())
        loadedImage = candidate.data();
    }
    REQUIRE(loadedImage);
    REQUIRE(loadedImage->size() == image->size());
    CHECK(std::memcmp(loadedImage->data(),
              image->data(),
              image->size() * image->elementSize())
        == 0);
    REQUIRE(::unlink((path + "/cycle.png").c_str()) == 0);
    REQUIRE(::unlink((path + "/edited.vmesh").c_str()) == 0);
    // Recovery owns Scene data, not these now-deleted working carriers.
    f.restoreBatch(session, 10, newestBatch);
    REQUIRE(f.await(session, 10).flag("committed"));
    if (meshFirst)
      CHECK(std::memcmp(image->data(), imageBefore.data(), imageBefore.size())
          == 0);
    else
      CHECK(f.sceneIds->handleOf(
                f.geometry->parameterValueAsObject<vsr::scene::Array>(
                    "vertex.position"))
          == meshBefore);
    f.restoreBatch(session, 11, newestBatch);
    CHECK(f.await(session, 11).str("outcome") == "already_restored");
    f.images->pump();
    f.scene.removeUnusedObjects();
    f.scene.defragmentObjectStorage();
    // Pool indices may change on defragmentation; only identity handles are
    // stable. Reacquire the fixture refs before the next public cycle.
    auto *originalPositions =
        dynamic_cast<vsr::scene::Array *>(f.sceneIds->resolve(oldPositions));
    auto *smallArray =
        dynamic_cast<vsr::scene::Array *>(f.sceneIds->resolve(smallIdentity));
    auto *largeArray =
        dynamic_cast<vsr::scene::Array *>(f.sceneIds->resolve(largeIdentity));
    REQUIRE(originalPositions);
    REQUIRE(smallArray);
    REQUIRE(largeArray);
    f.positions = originalPositions->self();
    small = smallArray->self();
    large = largeArray->self();
    CHECK(f.scene.numberOfObjects(ANARI_ARRAY) <= 5);
    CHECK(memory->used() < memory->limit());
    CHECK(f.imageActivity->usage().restorableBatches == 0);
    CHECK(f.imageActivity->usage().reservedRetentionBytes == 0);
    json::Value metadata;
    REQUIRE(json::parse(f.images->metadata(f.sceneId, session.id), metadata));
    CHECK(metadata.get("exchange_limits")->get("retained_physical_bytes")->n
        == 0);
    CHECK(f.other->parameterValueAsObject<vsr::scene::Array>("vertex.position")
        == f.positions.data());
    CHECK(std::memcmp(f.positions->data(),
              PositionFixture::POINTS,
              sizeof(PositionFixture::POINTS))
        == 0);
    REQUIRE(f.rpc("vsr/detach", "{}", &session).status == 200);
    std::ifstream original(source);
    std::string text;
    std::getline(original, text);
    CHECK(text == "original mesh asset");
  }
}

TEST_CASE("Active Restore and pending Mesh are not evictable history",
    "[integration][mcp][retention-pressure][pressure-active-restore]")
{
  const bool cancelRestore = GENERATE(false, true);
  PressureReadGate gate;
  const auto memory = std::make_shared<vsr::rendering::FrameBudget>(16u << 20);
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 400000;
  limits.reservePhysicalMemory = [memory](std::size_t bytes) {
    return memory->reserve(bytes);
  };
  limits.physicalByteCap = memory->limit();
  PositionFixture f(limits, true);
  PressureGateRelease releaseGate(&gate);
  const auto session = f.attach();
  const auto evidence = f.working(session);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
  const std::vector<unsigned char> before(262144, 71), rows(262400, 0);
  image->setData(static_cast<const void *>(before.data()));
  writeInput(f.exchangePath() + "/image.png", inputPng(256, 256, 6, rows, {}));
  f.importImage(session,
      2,
      f.objectHandle(image.data()),
      *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(image.data())),
      "image.png");
  const auto oldImage = f.await(session, 2);
  REQUIRE(oldImage.flag("committed"));
  const auto imageBatch = *oldImage.result()->get("batch_ref");
  auto *layer = f.scene.defaultLayer();
  auto node = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, "pressure-node"));
  const auto edit = pressureNodeEdit(f, session, 3, false);
  f.tool(session,
      "vsr_edit",
      "{\"request_id\":4,\"scene_id\":" + std::to_string(f.sceneId)
          + ",\"edits\":[" + edit + "]}");
  const auto nodeResult = f.await(session, 4);
  REQUIRE(nodeResult.flag("committed"));
  const auto nodeBatch = *nodeResult.result()->get("batch_ref");
  interceptImportRead = true;
  f.importPositions(session, 5, *evidence.result());
  REQUIRE(f.integration.service() > 0);
  gate.wait();
  const auto pendingUsage = memory->used();
  const auto pendingHistory = f.imageActivity->usage().retentionBytes;
  bool active = false;
  f.restores->setFaultInjector([&] {
    active = true;
    CHECK(memory->used() > pendingUsage);
    CHECK(f.imageActivity->usage().retentionBytes == pendingHistory);
    CHECK(f.restores->eligible(
        {imageBatch.get("launch_id")->s, imageBatch.get("batch_id")->s}));
    CHECK(
        f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
        == f.positions.data());
    if (cancelRestore) {
      const operations::Credentials credentials{
          f.authority->launchCredential(), session.id, session.secret};
      f.engine->cancel(credentials, {session.id, 6});
    }
    return false;
  });
  f.restoreBatch(session, 6, imageBatch);
  const auto restoredImage = f.await(session, 6);
  REQUIRE(active);
  CHECK(restoredImage.flag("committed") == !cancelRestore);
  CHECK(restoredImage.str("outcome") == (cancelRestore ? "cancelled" : "ok"));
  f.restores->setFaultInjector({});
  CHECK(f.status(session, session.id, 5).str("state") != "terminal");
  CHECK(f.restores->eligible(
      {nodeBatch.get("launch_id")->s, nodeBatch.get("batch_id")->s}));
  gate.released = true;
  const auto mesh = f.await(session, 5);
  REQUIRE_FALSE(gate.timedOut.load());
  REQUIRE(mesh.flag("committed"));
  f.restoreBatch(session, 7, imageBatch);
  CHECK(f.await(session, 7).str("outcome")
      == (cancelRestore ? "outcome_unavailable" : "already_restored"));
  f.restoreBatch(session, 8, nodeBatch);
  REQUIRE(f.await(session, 8).flag("committed"));
  CHECK(node->value().isEnabled());
  f.restoreBatch(session, 9, *mesh.result()->get("batch_ref"));
  REQUIRE(f.await(session, 9).flag("committed"));
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  const std::vector<unsigned char> zero(before.size(), 0);
  CHECK(std::memcmp(image->data(),
            cancelRestore ? zero.data() : before.data(),
            before.size())
      == 0);
  CHECK(f.imageActivity->usage().restorableBatches == 0);
  CHECK(f.imageActivity->usage().reservedRetentionBytes == 0);
}

TEST_CASE("No-op and malformed public workflows cannot refresh full history",
    "[integration][mcp][retention-pressure][pressure-noop]")
{
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 500000;
  PositionFixture f(limits, true);
  const auto session = f.attach();
  const auto evidence = f.working(session);
  f.importPositions(session, 2, *evidence.result());
  const auto mesh = f.await(session, 2);
  REQUIRE(mesh.flag("committed"));
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  const unsigned char before[] = {17, 29, 41, 53};
  image->setData(static_cast<const void *>(before));
  writeInput(
      f.exchangePath() + "/image.png", inputPng(1, 1, 6, {0, 1, 2, 3, 4}, {}));
  const auto handle = f.objectHandle(image.data());
  f.importImage(session,
      3,
      handle,
      *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(image.data())),
      "image.png");
  const auto imported = f.await(session, 3);
  REQUIRE(imported.flag("committed"));
  auto *layer = f.scene.defaultLayer();
  auto node = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, "pressure-node"));
  const auto edits = pressureNodeEdit(f, session, 4, false);
  f.tool(session,
      "vsr_edit",
      "{\"request_id\":5,\"scene_id\":" + std::to_string(f.sceneId)
          + ",\"edits\":[" + edits + "]}");
  const auto generic = f.await(session, 5);
  REQUIRE(generic.flag("committed"));
  const auto charge = f.imageActivity->usage().retentionBytes;
  REQUIRE(f.imageActivity->usage().restorableBatches == 3);
  f.exportMesh(session, 6, f.geometry.data());
  const auto unchangedMesh = f.await(session, 6);
  REQUIRE(unchangedMesh.str("outcome") == "ok");
  f.importPositions(session,
      7,
      *unchangedMesh.result(),
      unchangedMesh.result()->get("file_name")->s);
  const auto meshNoop = f.await(session, 7);
  REQUIRE(meshNoop.str("outcome") == "ok");
  CHECK_FALSE(meshNoop.flag("committed"));
  CHECK(meshNoop.result()->get("unchanged")->b);
  const auto unchangedNode = pressureNodeEdit(f, session, 8, false);
  f.tool(session,
      "vsr_edit",
      "{\"request_id\":9,\"scene_id\":" + std::to_string(f.sceneId)
          + ",\"edits\":[" + unchangedNode + "]}");
  const auto nodeNoop = f.await(session, 9);
  REQUIRE(nodeNoop.str("outcome") == "ok");
  CHECK_FALSE(nodeNoop.flag("committed"));
  f.importImage(session,
      10,
      handle,
      *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(image.data())),
      "image.png");
  const auto imageNoop = f.await(session, 10);
  REQUIRE(imageNoop.str("outcome") == "ok");
  CHECK_FALSE(imageNoop.flag("committed"));
  CHECK(imageNoop.result()->get("unchanged")->b);
  const auto revision = f.sceneIds->sceneRevision();
  writeInput(f.exchangePath() + "/broken.png", {1, 2, 3});
  writeInput(f.exchangePath() + "/broken.vmesh", {1, 2, 3});
  f.importImage(session,
      11,
      handle,
      *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(image.data())),
      "broken.png");
  CHECK(f.await(session, 11).str("outcome") == "invalid_request");
  f.importPositions(session, 12, *unchangedMesh.result(), "broken.vmesh");
  CHECK(f.await(session, 12).str("outcome") == "invalid_request");
  CHECK(f.sceneIds->sceneRevision() == revision);
  CHECK(f.imageActivity->usage().retentionBytes == charge);
  CHECK(f.imageActivity->usage().restorableBatches == 3);
  CHECK(f.imageActivity->usage().reservedRetentionBytes == 0);
  f.restoreBatch(session, 13, *mesh.result()->get("batch_ref"));
  REQUIRE(f.await(session, 13).flag("committed"));
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  f.restoreBatch(session, 14, *imported.result()->get("batch_ref"));
  REQUIRE(f.await(session, 14).flag("committed"));
  CHECK(std::memcmp(image->data(), before, sizeof(before)) == 0);
  f.restoreBatch(session, 15, *generic.result()->get("batch_ref"));
  REQUIRE(f.await(session, 15).flag("committed"));
  CHECK(node->value().isEnabled());
}

TEST_CASE(
    "Rolling recovery and copied results outlive off-thread host teardown",
    "[integration][mcp][retention-pressure][pressure-teardown]")
{
  const auto memory = std::make_shared<vsr::rendering::FrameBudget>(16u << 20);
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 400000;
  limits.reservePhysicalMemory = [memory](std::size_t bytes) {
    return memory->reserve(bytes);
  };
  limits.physicalByteCap = memory->limit();
  const auto baseline = memory->used();
  auto host = std::make_unique<PositionFixture>(limits);
  const auto session = host->attach();
  const auto evidence = host->working(session, "tracer", true);
  host->importPositions(session, 2, *evidence.result());
  const auto imported = host->await(session, 2);
  REQUIRE(imported.flag("committed"));
  const auto batch = *imported.result()->get("batch_ref");
  auto held = host->edits->restorations().find(
      {batch.get("launch_id")->s, batch.get("batch_id")->s});
  const operations::Credentials credentials{
      host->authority->launchCredential(), session.id, session.secret};
  auto receipt = host->engine->status(credentials, {session.id, 2});
  auto copiedReceipt = receipt;
  REQUIRE(held);
  REQUIRE(receipt.committed);
  const auto bytes = receipt.result;
  // Start independent real preparation, then drop session and Scene lifetime.
  host->exportMesh(session, 3, host->geometry.data());
  REQUIRE(host->integration.service() > 0);
  REQUIRE(host->rpc("vsr/detach", "{}", &session).status == 200);
  std::thread shutdown([&] { host.reset(); });
  shutdown.join();
  CHECK(receipt.result == bytes);
  CHECK(copiedReceipt.result == bytes);
  CHECK(memory->used() > 0);
  auto charge = memory->used();
  std::thread release([&] { held.reset(); });
  release.join();
  CHECK(memory->used() < charge);
  CHECK(
      memory->used() > 0); // Independent public result copies still own leases.
  charge = memory->used();
  receipt = {};
  CHECK(memory->used() < charge);
  copiedReceipt = {};
  CHECK(memory->used() == baseline);
  // The same physical pool can immediately serve a fresh independent host.
  PositionFixture next(limits);
  const auto fresh = next.attach();
  next.exportMesh(fresh, 1, next.geometry.data());
  REQUIRE(next.await(fresh, 1).str("outcome") == "ok");
}

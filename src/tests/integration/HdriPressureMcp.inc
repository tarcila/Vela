// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

SCENARIO("Public HDRI reserves worker backing before allocation",
    "[integration][content][mcp][public-hdri-capacity]")
{
  const bool denyWorker = GENERATE(false, true);
  const auto ownerThread = std::this_thread::get_id();
  const auto physical =
      std::make_shared<vsr::rendering::FrameBudget>(256u << 20);
  std::atomic<size_t> workerReservations{0};
  image::ExchangeLimits limits;
  limits.reservePhysicalMemory = [&, physical](size_t bytes) {
    // Source Snapshot/Currency allocations are below 512 bytes on this
    // qualified ABI. Job coallocation now occurs on the MCP thread too and
    // is larger; deny actual worker metadata, not pre-admission Job controls.
    if (std::this_thread::get_id() != ownerThread && bytes < 512) {
      ++workerReservations;
      if (denyWorker)
        return std::shared_ptr<const void>{};
    }
    return physical->reserve(bytes);
  };
  HdriFixture f(limits);
  f.scene.defaultLayer();
  const auto s = f.attach();
  const auto target = f.firstTarget(s, 1);
  f.editOpacity(s, 2, target, .25);
  const auto prior = f.await(s, 2);
  REQUIRE(prior.flag("committed"));
  const auto retained = f.imageActivity->usage().retentionBytes;
  writePublicHdri(f.tmp.path + "/safety.hdr");
  f.tool(s, "vsr_create_hdri", publicHdriArgs(f, 3));
  const auto created = f.await(s, 3);
  CHECK(workerReservations.load() > 0);
  CHECK(created.flag("committed") == !denyWorker);
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == (denyWorker ? 0 : 1));
  if (denyWorker) {
    REQUIRE(created.result());
    CHECK(created.str("outcome") == "busy");
    CHECK(created.result()->get("reason")->s == "worker_backing_limit");
    CHECK(f.imageActivity->usage().retentionBytes == retained);
    CHECK(f.imageActivity->usage().reservedRetentionBytes == 0);
  } else {
    REQUIRE(created.result());
    const auto *peak = created.result()->get("worker_allocator_peak_bytes");
    REQUIRE(peak);
    CHECK(peak->n > 0);
    CHECK(peak->n < 4096); // This real two-pixel HDR needs no large payload.
    f.restoreBatch(s, 4, *created.result()->get("batch_ref"));
    CHECK(f.await(s, 4).str("outcome") == "ok");
  }
  f.restoreBatch(s, 5, *prior.result()->get("batch_ref"));
  CHECK(f.await(s, 5).str("outcome") == "ok");
}

SCENARIO("Public HDRI profiles actual backing before preparation",
    "[integration][content][mcp][public-hdri-profile]")
{
  const auto fault = GENERATE(0, 1, 2);
  const bool oversizedMetadata = fault == 1;
  image::ExchangeLimits limits;
  const auto physical =
      std::make_shared<vsr::rendering::FrameBudget>(256u << 20);
  limits.reservePhysicalMemory = [physical, fault](size_t bytes) {
    // A two-pixel graph must not need an unexplained giant publication lease.
    // Denying the owner-side peak must not pre-evict old recovery either.
    const size_t cap = fault == 2 ? 65537 : (1u << 20);
    return bytes < cap ? physical->reserve(bytes)
                       : std::shared_ptr<const void>{};
  };
  HdriFixture f(limits);
  auto root = f.scene.defaultLayer()->root();
  const auto s = f.attach();
  const auto target = f.firstTarget(s, 1);
  f.editOpacity(s, 2, target, .25);
  const auto prior = f.await(s, 2);
  REQUIRE(prior.flag("committed"));
  const auto retained = f.imageActivity->usage().retentionBytes;
  if (oversizedMetadata)
    root->value().setInstanceParameter("large", std::string(1u << 20, 'x'));
  writePublicHdri(f.tmp.path + "/safety.hdr");
  f.tool(s, "vsr_create_hdri", publicHdriArgs(f, 3));
  const auto created = f.await(s, 3);
  CHECK(created.flag("committed") == (fault == 0));
  if (fault != 0) {
    REQUIRE(created.result());
    CHECK(created.result()->get("reason")->s
        == (oversizedMetadata ? "initial_profile_limit"
                              : "preparation_resource_limit"));
    CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 0);
    CHECK(f.imageActivity->usage().retentionBytes == retained);
  } else {
    REQUIRE(created.result());
    REQUIRE(created.result()->get("preparation_peak_bound_bytes"));
    CHECK(
        created.result()->get("preparation_peak_bound_bytes")->n < (1u << 20));
    REQUIRE(created.result()->get("live_payload_adopted_bytes"));
    CHECK(created.result()->get("live_payload_adopted_bytes")->n == 24);
    f.restoreBatch(s, 4, *created.result()->get("batch_ref"));
    CHECK(f.await(s, 4).str("outcome") == "ok");
  }
  f.restoreBatch(s, 5, *prior.result()->get("batch_ref"));
  CHECK(f.await(s, 5).str("outcome") == "ok");
}

SCENARIO("Public HDRI rejects oversized destination before reading its source",
    "[integration][content][mcp][capacity-retained-preflight]")
{
  image::ExchangeLimits limits;
  const auto physical =
      std::make_shared<vsr::rendering::FrameBudget>(256u << 20);
  limits.reservePhysicalMemory = [physical](size_t bytes) {
    return physical->reserve(bytes);
  };
  HdriFixture f(limits);
  auto root = f.scene.defaultLayer()->root();
  const auto s = f.attach();
  const auto target = f.firstTarget(s, 1);
  f.editOpacity(s, 2, target, .25);
  const auto prior = f.await(s, 2);
  REQUIRE(prior.flag("committed"));
  const auto retained = f.imageActivity->usage().retentionBytes;
  root->value().setInstanceParameter("large", std::string(1u << 20, 'x'));
  // Deliberately absent source: destination admission must precede worker I/O,
  // not merely reject the oversized copy after source failure or decoding.
  f.tool(s, "vsr_create_hdri", publicHdriArgs(f, 3));
  const auto refused = f.await(s, 3);
  REQUIRE(refused.result());
  CHECK(refused.str("outcome") == "busy");
  CHECK(refused.result()->get("reason")->s == "initial_profile_limit");
  CHECK_FALSE(refused.flag("committed"));
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 0);
  CHECK(f.imageActivity->usage().retentionBytes == retained);
  f.restoreBatch(s, 4, *prior.result()->get("batch_ref"));
  CHECK(f.await(s, 4).str("outcome") == "ok");
}

SCENARIO("HDRI retained record copies reserve before copying and release last",
    "[integration][content][mcp][capacity-retained-copy]")
{
  const auto physical =
      std::make_shared<vsr::rendering::FrameBudget>(256u << 20);
  bool deny = false;
  image::ExchangeLimits limits;
  limits.reservePhysicalMemory = [physical, &deny](size_t bytes) {
    return deny ? std::shared_ptr<const void>{} : physical->reserve(bytes);
  };
  HdriFixture f(limits);
  f.scene.defaultLayer();
  const auto s = f.attach();
  writePublicHdri(f.tmp.path + "/safety.hdr");
  f.tool(s, "vsr_create_hdri", publicHdriArgs(f, 1));
  const auto created = f.await(s, 1);
  REQUIRE(created.flag("committed"));
  const auto &batch = *created.result()->get("batch_ref");
  const edit::BatchRef ref{batch.get("launch_id")->s, batch.get("batch_id")->s};
  const auto before = physical->used();
  auto pin = f.edits->restorations().find(ref);
  REQUIRE(pin);
  CHECK(physical->used() > before);
  const auto first = physical->used();
  auto copy = pin;
  CHECK(physical->used() > first);
  const auto both = physical->used();
  deny = true;
  CHECK_THROWS_AS(f.edits->restorations().find(ref), std::bad_alloc);
  deny = false;
  CHECK(physical->used() == both);
  CHECK(f.restores->eligible(ref));
  std::thread release([&] { copy.reset(); });
  release.join();
  CHECK(physical->used() == first);
  pin.reset();
  CHECK(physical->used() == before);
  f.restoreBatch(s, 2, batch);
  CHECK(f.await(s, 2).str("outcome") == "ok");
}

SCENARIO(
    "Public HDRI Status copies and assignments retain independent result charges",
    "[integration][content][mcp][capacity-owner-final-public]")
{
  const auto physical =
      std::make_shared<vsr::rendering::FrameBudget>(256u << 20);
  bool deny = false;
  image::ExchangeLimits limits;
  limits.reservePhysicalMemory = [physical, &deny](size_t bytes) {
    return deny ? std::shared_ptr<const void>{} : physical->reserve(bytes);
  };
  HdriFixture f(limits);
  f.scene.defaultLayer();
  const auto session = f.attach();
  writePublicHdri(f.tmp.path + "/safety.hdr");
  f.tool(session, "vsr_create_hdri", publicHdriArgs(f, 1));
  const auto created = f.await(session, 1);
  REQUIRE(created.flag("committed"));
  const auto before = physical->used();
  auto pin = f.engine->status(
      {f.authority->launchCredential(), session.id, session.secret},
      {session.id, 1});
  REQUIRE(pin.resultVisible);
  const auto first = physical->used();
  CHECK(first > before);
  auto copy = pin;
  const auto both = physical->used();
  CHECK(both > first);
  copy = pin;
  CHECK(physical->used() == both);
  deny = true;
  const auto refusedWire = mcp::tools::statusResult({session.id, 1}, pin);
  CHECK(refusedWire.code == operations::Outcome::BUSY);
  CHECK_THROWS_AS(copy = pin, std::bad_alloc);
  CHECK(copy.result == pin.result);
  CHECK(physical->used() == both);
  deny = false;
  f.restoreBatch(session, 2, *created.result()->get("batch_ref"));
  CHECK(f.await(session, 2).str("outcome") == "ok");
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 0);
  CHECK(copy.result == pin.result); // Historical payload, not a radiance pin.
  const auto wireBefore = physical->used();
  auto wire = mcp::tools::statusResult({session.id, 1}, pin);
  CHECK(wire.ownership.lease);
  CHECK(physical->used() > wireBefore);
  const auto wireHeld = physical->used();
  auto wireCopy = wire;
  CHECK(physical->used() > wireHeld);
  wireCopy = {};
  CHECK(physical->used() == wireHeld);
  std::thread wireRelease([&] { wire = {}; });
  wireRelease.join();
  CHECK(physical->used() == wireBefore);
  const auto held = physical->used();
  std::thread release([&] { copy = {}; });
  release.join();
  CHECK(physical->used() == held - (both - first));
  pin = {};
  CHECK(physical->used() == held - (both - before));
}

SCENARIO("HDRI snapshot control survives retirement through last weak owner",
    "[integration][content][mcp][capacity-controls-weak]")
{
  const auto physical =
      std::make_shared<vsr::rendering::FrameBudget>(256u << 20);
  const auto baseline = physical->used();
  std::weak_ptr<edit::HdriSnapshot> weak;
  {
    image::ExchangeLimits limits;
    limits.reservePhysicalMemory = [physical](size_t bytes) {
      return physical->reserve(bytes);
    };
    HdriFixture f(limits);
    f.scene.defaultLayer();
    const auto s = f.attach();
    writePublicHdri(f.tmp.path + "/safety.hdr");
    f.tool(s, "vsr_create_hdri", publicHdriArgs(f, 1));
    const auto created = f.await(s, 1);
    REQUIRE(created.flag("committed"));
    const auto &batch = *created.result()->get("batch_ref");
    auto record = f.edits->restorations().find(
        {batch.get("launch_id")->s, batch.get("batch_id")->s});
    REQUIRE(record);
    REQUIRE(record->hdri);
    CHECK(record->hdri->bytes.empty());
    weak = record->hdri;
    f.restoreBatch(s, 2, batch);
    CHECK(f.await(s, 2).str("outcome") == "ok");
    CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == 0);
  }
  CHECK(weak.expired());
  CHECK(physical->used() > baseline);
  std::thread release([&] { weak.reset(); });
  release.join();
  CHECK(physical->used() == baseline);
}

namespace {
struct ThrowHdriPublication : vsr::scene::EmptyUpdateDelegate
{
  void signalObjectAdded(const vsr::scene::Object *) override;
};
void ThrowHdriPublication::signalObjectAdded(const vsr::scene::Object *object)
{
  if (object->type() == ANARI_LIGHT)
    throw std::runtime_error("public HDRI postcommit consumer failure");
}
} // namespace

SCENARIO(
    "Public HDRI notification failure retains committed receipt and normal batch",
    "[integration][content][mcp][public-hdri-notification]")
{
  const bool notification = GENERATE(false, true);
  HdriFixture f;
  f.scene.defaultLayer();
  if (notification)
    f.scene.updateDelegate().emplace<ThrowHdriPublication>();
  else
    f.hookOk = false;
  const auto s = f.attach();
  writePublicHdri(f.tmp.path + "/safety.hdr");
  const auto args = publicHdriArgs(f, 1);
  f.tool(s, "vsr_create_hdri", args);
  const auto created = f.await(s, 1);
  REQUIRE(created.flag("committed"));
  CHECK(created.str("outcome") == "applied_with_sync_failure");
  REQUIRE(created.result());
  REQUIRE(created.result()->get("batch_ref"));
  REQUIRE(created.result()->get("primary"));
  const auto batch = *created.result()->get("batch_ref");
  CHECK(f.restores->eligible(
      {batch.get("launch_id")->s, batch.get("batch_id")->s}));
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 1);
  CHECK(f.imageActivity->usage().restorableBatches == 1);
  CHECK(f.tool(s, "vsr_create_hdri", args).flag("replayed"));
  CHECK(f.await(s, 1).flag("committed"));
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 1);
  f.hookOk = true;
  f.restoreBatch(s, 2, batch);
  const auto restored = f.await(s, 2);
  CHECK(restored.str("outcome")
      == (notification ? "ok" : "applied_with_sync_failure"));
  CHECK(restored.flag("committed"));
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 0);
}

SCENARIO(
    "Public HDRI shares small publication FIFO with ordinary scalar recovery",
    "[integration][content][mcp][public-hdri-pressure]")
{
  image::ExchangeLimits limits;
  limits.globalRetentionBytes =
      1; // Single oversized entry, not 8 GiB allocation.
  limits.hardRetentionBytes = 1048576;
  const auto physical =
      std::make_shared<vsr::rendering::FrameBudget>(256u << 20);
  limits.reservePhysicalMemory = [physical](size_t bytes) {
    return physical->reserve(bytes);
  };
  HdriFixture f(limits);
  f.scene.defaultLayer();
  const auto s = f.attach();
  writePublicHdri(f.tmp.path + "/safety.hdr");
  f.tool(s, "vsr_create_hdri", publicHdriArgs(f, 1));
  const auto first = f.await(s, 1);
  REQUIRE(first.flag("committed"));
  const auto batch = *first.result()->get("batch_ref");
  auto pinned = f.edits->restorations().find(
      {batch.get("launch_id")->s, batch.get("batch_id")->s});
  REQUIRE(pinned);
  REQUIRE(pinned->hdri);
  CHECK(pinned->hdri->bytes.empty()); // No automatic whole-live-radiance undo.
  CHECK(pinned->hdri->evidence.registration.lock());
  const auto retained = first.result()
                            ->get("restoration_policy")
                            ->get("required_capacity_bytes")
                            ->n;
  CHECK(retained < 100000);
  CHECK(retained > sizeof(edit::HdriSnapshot));
  CHECK(f.imageActivity->usage().restorableBatches == 1);
  f.tool(s,
      "vsr_inspect",
      "{\"request_id\":2,\"scene_id\":" + std::to_string(f.sceneId)
          + ",\"object_type\":\"ANARI_MATERIAL\"}");
  const auto inspected = f.await(s, 2);
  REQUIRE(inspected.str("outcome") == "ok");
  const auto &material = inspected.result()->get("items")->a.at(0);
  const auto target = "\"handle\":" + json::write(*material.get("handle"))
      + ",\"version\":" + json::write(*material.get("version"));
  f.editOpacity(s, 3, target, .25);
  const auto scalar = f.await(s, 3);
  REQUIRE(scalar.flag("committed"));
  CHECK(f.imageActivity->usage().restorableBatches == 1);
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 1); // Eviction is not removal.
  f.restoreBatch(s, 4, batch);
  CHECK(f.await(s, 4).str("outcome") == "outcome_unavailable");
  CHECK(pinned->hdri->bytes.empty());
  const auto heldBytes = physical->used();
  REQUIRE(heldBytes > 0);
  std::thread release([&] { pinned.reset(); });
  release.join();
  CHECK(physical->used() < heldBytes);
  auto *opacity =
      f.scene.getObject<vsr::scene::Material>(0)->parameter("opacity");
  opacity->setDescription("unrelated scalar metadata");
  opacity->setMin(.1f);
  f.restoreBatch(s, 5, *scalar.result()->get("batch_ref"));
  CHECK(f.await(s, 5).str("outcome") == "ok");
  CHECK(f.opacity0() == 1.f);
  CHECK(opacity->description() == "unrelated scalar metadata");
  CHECK(opacity->min().get<float>() == .1f);
}

SCENARIO(
    "Public HDRI source metadata and current authority are rechecked after preparation",
    "[integration][content][mcp][public-hdri-final-guard]")
{
  const auto fault = GENERATE(0, 1, 2, 3);
  Fixture *host = nullptr;
  bool armed = false;
  const auto physical =
      std::make_shared<vsr::rendering::FrameBudget>(256u << 20);
  image::ExchangeLimits limits;
  limits.reservePhysicalMemory = [&](size_t bytes) {
    if (armed
        && bytes
            == edit::RestorationStore::hdriScopeBytes(
                {host->edits->launchId(), std::string(32, '0')})) {
      // Actual retained-scope node/key reservation after decoding.
      armed = false;
      if (fault == 0) {
        std::ofstream file(
            host->tmp.path + "/safety.hdr", std::ios::binary | std::ios::app);
        file.put('x');
      } else if (fault == 1)
        host->creator->assetRoots().revoke(0);
      else if (fault == 2)
        host->authority->setAgentEditsDisabled(true);
      else
        host->authority->restrict(
            session::Target::scene(host->sceneId), session::Capability::READ);
    }
    return physical->reserve(bytes);
  };
  HdriFixture f(limits);
  host = &f;
  f.scene.defaultLayer();
  const auto s = f.attach();
  writePublicHdri(f.tmp.path + "/safety.hdr");
  armed = true;
  f.tool(s, "vsr_create_hdri", publicHdriArgs(f, 1));
  const auto result = f.await(s, 1);
  REQUIRE_FALSE(armed);
  CHECK_FALSE(result.flag("committed"));
  CHECK(result.str("outcome") != "ok");
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 0);
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == 0);
  CHECK(f.imageActivity->usage().restorableBatches == 0);
  CHECK(f.imageActivity->usage().reservedRetentionBytes == 0);
}

SCENARIO(
    "Public HDRI above-hard refusal preserves ordinary history without allocating a huge asset",
    "[integration][content][mcp][public-hdri-pressure]")
{
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 10000;
  limits.hardRetentionBytes = 10000;
  HdriFixture f(limits);
  f.scene.defaultLayer();
  const auto s = f.attach();
  const auto target = f.firstTarget(s, 1);
  f.editOpacity(s, 2, target, .25);
  const auto prior = f.await(s, 2);
  REQUIRE(prior.flag("committed"));
  const auto retained = f.imageActivity->usage().retentionBytes;
  writePublicHdri(f.tmp.path + "/safety.hdr");
  f.tool(s, "vsr_create_hdri", publicHdriArgs(f, 3));
  const auto refused = f.await(s, 3);
  CHECK_FALSE(refused.flag("committed"));
  CHECK(refused.str("outcome") == "busy");
  REQUIRE(refused.result());
  CHECK(refused.result()->get("reason")->s == "restoration_approval_required");
  CHECK(f.imageActivity->usage().retentionBytes == retained);
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 0);
  f.restoreBatch(s, 4, *prior.result()->get("batch_ref"));
  CHECK(f.await(s, 4).str("outcome") == "ok");
  const auto proposal = f.imageActivity->approvals().snapshot();
  REQUIRE(proposal);
  CHECK(proposal->identity.originalRequest == 3);
  REQUIRE(f.imageActivity->approvals().approve(
      proposal->identity, proposal->requiredBytes));
  CHECK(f.tool(s, "vsr_create_hdri", publicHdriArgs(f, 3)).flag("replayed"));
  const auto approved = publicHdriArgs(f, 5, ",\"approval_request_id\":3");
  f.tool(s, "vsr_create_hdri", approved);
  const auto accepted = f.await(s, 5);
  REQUIRE(accepted.flag("committed"));
  REQUIRE(accepted.str("outcome") == "ok");
  CHECK_FALSE(f.imageActivity->approvals().snapshot());
  CHECK(f.imageActivity->limits().hardRetentionBytes == 10000);
  CHECK(f.tool(s, "vsr_create_hdri", approved).flag("replayed"));
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 1);
  f.restoreBatch(s, 6, *accepted.result()->get("batch_ref"));
  CHECK(f.await(s, 6).str("outcome") == "ok");
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 0);
  f.tool(s, "vsr_create_hdri", publicHdriArgs(f, 7));
  CHECK_FALSE(f.await(s, 7).flag("committed"));
}

SCENARIO(
    "Public HDRI scan work exhaustion refuses addition and destructive Restore",
    "[integration][content][mcp][public-hdri-scan-limit]")
{
  const bool restore = GENERATE(false, true);
  HdriFixture f;
  f.scene.defaultLayer();
  const auto s = f.attach();
  writePublicHdri(f.tmp.path + "/safety.hdr");
  Reply created;
  if (restore) {
    f.tool(s, "vsr_create_hdri", publicHdriArgs(f, 1));
    created = f.await(s, 1);
    REQUIRE(created.flag("committed"));
  }
  // ~1 MiB indices, no large payload allocation or synthetic 8 GiB override.
  f.scene.createArray(ANARI_LIGHT, 131073);
  if (restore)
    f.restoreBatch(s, 2, *created.result()->get("batch_ref"));
  else
    f.tool(s, "vsr_create_hdri", publicHdriArgs(f, 2));
  const auto result = f.await(s, 2);
  CHECK_FALSE(result.flag("committed"));
  CHECK(result.str("outcome") == (restore ? "conflict" : "busy"));
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == (restore ? 1 : 0));
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == (restore ? 2 : 1));
}

SCENARIO("Public HDRI honors exact practical single-entry hard boundaries",
    "[integration][content][mcp][public-hdri-pressure]")
{
  const auto delta = GENERATE(-1, 0, 1);
  std::uint64_t required = 0;
  {
    HdriFixture measurement;
    measurement.scene.defaultLayer();
    const auto s = measurement.attach();
    writePublicHdri(measurement.tmp.path + "/safety.hdr");
    measurement.tool(s, "vsr_create_hdri", publicHdriArgs(measurement, 1));
    const auto result = measurement.await(s, 1);
    REQUIRE(result.flag("committed"));
    required = std::uint64_t(result.result()
            ->get("restoration_policy")
            ->get("required_capacity_bytes")
            ->n);
  }
  REQUIRE(required > 1);
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = required + delta;
  limits.hardRetentionBytes = required + delta;
  HdriFixture f(limits);
  f.scene.defaultLayer();
  const auto s = f.attach();
  writePublicHdri(f.tmp.path + "/safety.hdr");
  f.tool(s, "vsr_create_hdri", publicHdriArgs(f, 1));
  const auto result = f.await(s, 1);
  CHECK(result.flag("committed") == (delta >= 0));
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == (delta >= 0 ? 1 : 0));
  REQUIRE(result.result());
  const auto *policy = result.result()->get("restoration_policy");
  const auto *capacity = policy
      ? policy->get("required_capacity_bytes")
      : result.result()->get("required_capacity_bytes");
  REQUIRE(capacity);
  CHECK(capacity->n == double(required));
}

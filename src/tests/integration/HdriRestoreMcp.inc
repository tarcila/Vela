// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/* Supplementary public Restore seam. Input admission is intentionally staged
 * internally; this must not be reported as the public HDRI creation tracer.
 */
SCENARIO("MCP normal Restore removes staged HDRI with shared batch ownership",
    "[integration][content][mcp][public-hdri-restore]")
{
  const bool ownsLayer = GENERATE(false, true);
  Fixture f({}, true, true, false, 120., true, UINT64_MAX, {}, true);
  f.scene.defaultLayer();
  const auto session = f.attach();
  const auto budget = std::make_shared<vsr::rendering::FrameBudget>(1u << 20);
  const operations::ResultOwnership::Reserve reserveResult = [budget] {
    return budget->reserve(65536);
  };
  const vsr::core::math::float3 radiance[] = {{8.f, 4.f, 2.f}};
  auto prepared =
      std::make_unique<content::PreparedHdriPublication>(*f.sceneIds,
          vsr::scene::LayerNodeRef{},
          "HDRI Root",
          1,
          1,
          radiance,
          vsr::scene::detail::HdriSettings{},
          ownsLayer ? "owned HDRI" : vsr::scene::Token{});
  const auto handles = prepared->handles();
  auto snapshot = std::make_shared<edit::HdriSnapshot>();
  snapshot->controlOwnership = operations::ResultOwnership(reserveResult);
  snapshot->reserveResult = reserveResult;
  snapshot->scopeLease = budget->reserve(8192);
  snapshot->reserveRemoval = operations::Reservation::from(budget);
  snapshot->retainedBytes = sizeof(edit::HdriSnapshot);
  edit::RestorationRecord record;
  record.ref = {f.edits->launchId(), *f.edits->newBatchId()};
  record.scene = f.sceneId;
  record.hdri = snapshot;
  record.image = snapshot;
  const auto batch = record.ref;
  operations::Request creation;
  creation.kind = "test_staged_hdri";
  creation.target = session::Target::scene(f.sceneId);
  creation.capability = session::Capability::EDIT;
  creation.canonicalArguments = "internal HDRI fixture, not public admission";
  creation.executor = [&](operations::StepContext &ctx) {
    REQUIRE(ctx.reserveRestoration(
        edit::restorationBytes(record), batch.launchId, batch.batchId));
    REQUIRE(f.edits->restorations().prepareImage(std::move(record)));
    ctx.prepareCommit("{\"committed\":true}", 128);
    REQUIRE(ctx.beforeWrite() == Outcome::OK);
    REQUIRE(prepared->commit());
    REQUIRE(snapshot->evidence.seal(*f.sceneIds, handles, ownsLayer));
    ctx.markCommitted();
    f.edits->restorations().publishImage(batch);
    ctx.publishRestoration();
    prepared->notify();
    operations::Step step;
    step.committed = true;
    return step;
  };
  const auto admitted = f.engine->submitLocal(std::move(creation));
  REQUIRE(admitted.code == Outcome::OK);
  f.drain();
  REQUIRE(f.engine->statusLocal(admitted.ref).committed);
  prepared.reset();
  snapshot.reset();
  REQUIRE(
      f.imageActivity->eligibility(batch) == activity::Eligibility::ELIGIBLE);
  const auto retained = f.imageActivity->usage().retentionBytes;
  CHECK(retained < 16384); // metadata-only recovery, no radiance copy/pin
  f.tool(session,
      "vsr_inspect",
      "{\"request_id\":1,\"scene_id\":" + std::to_string(f.sceneId)
          + ",\"object_type\":\"ANARI_LIGHT\"}");
  const auto inspection = f.await(session, 1);
  REQUIRE(inspection.str("outcome") == "ok");
  REQUIRE(inspection.result()->get("items")->a.size() == 1);
  CHECK(inspection.result()->get("items")->a[0].get("handle")->s
      == edit::detail::hex64(handles.light.domain)
          + edit::detail::hex64(handles.light.serial));
  json::Value target;
  REQUIRE(json::parse(edit::detail::batchRefJson(batch), target));
  f.restoreBatch(session, 2, target);
  const auto removed = f.await(session, 2);
  REQUIRE(removed.str("outcome") == "ok");
  CHECK(removed.flag("committed"));
  REQUIRE(removed.result());
  REQUIRE(removed.result()->get("removed_content"));
  CHECK(removed.result()->get("removed_content")->b);
  CHECK(f.imageActivity->eligibility(batch) == activity::Eligibility::RESTORED);
  CHECK(f.imageActivity->usage().retentionBytes == 0);
  CHECK_FALSE(f.sceneIds->resolve(handles.radiance));
  CHECK_FALSE(f.sceneIds->resolve(handles.light));
  CHECK_FALSE(f.sceneIds->resolve(handles.contentRoot));
  CHECK(f.scene.numberOfLayers() == 1);
  f.restoreBatch(session, 3, target);
  CHECK(f.await(session, 3).str("outcome") == "already_restored");
  // Historical replay cannot repeat deletion or consume another history entry.
  f.restoreBatch(session, 2, target);
  CHECK(f.await(session, 2).flag("committed"));
  CHECK(f.scene.numberOfLayers() == 1);
}

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

TEST_CASE("Busy eligible Restore does not refresh mixed FIFO age",
    "[integration][mcp][retention-delivery][retention-busy-age]")
{
  const bool reverseRestore = GENERATE(false, true);
  const auto memory = std::make_shared<vsr::rendering::FrameBudget>(16u << 20);
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 500000;
  limits.hardRetentionBytes = 1000000;
  limits.physicalByteCap = memory->limit();
  limits.reservePhysicalMemory = [memory](std::size_t bytes) {
    return memory->reserve(bytes);
  };
  PositionFixture f(limits);
  const auto session = f.attach();
  const auto evidence = f.working(session);
  REQUIRE(
      f.importPositions(session, 2, *evidence.result()).errorCode().empty());
  const auto mesh = f.await(session, 2);
  REQUIRE(mesh.str("outcome") == "ok");
  REQUIRE(mesh.flag("committed"));
  INFO("Mesh receipt: " << json::write(*mesh.result()));
  const auto meshBatch = *mesh.result()->get("batch_ref");
  const auto replacement = f.scene.getObject<vsr::scene::Array>(
      f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
          ->index());
  REQUIRE(replacement != f.positions);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  auto newestImage = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  const unsigned char original[] = {17, 29, 41, 53};
  const unsigned char changed[] = {1, 2, 3, 4};
  image->setData(static_cast<const void *>(original));
  newestImage->setData(static_cast<const void *>(original));
  writeInput(
      f.exchangePath() + "/age.png", inputPng(1, 1, 6, {0, 1, 2, 3, 4}, {}));
  const auto imageHandle = f.objectHandle(image.data());
  const auto imageVersion =
      *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(image.data()));
  REQUIRE(f.importImage(session, 3, imageHandle, imageVersion, "age.png")
          .errorCode()
          .empty());
  const auto imported = f.await(session, 3);
  REQUIRE(imported.str("outcome") == "ok");
  REQUIRE(imported.flag("committed"));
  const auto imageBatch = *imported.result()->get("batch_ref");
  const edit::BatchRef older{
      meshBatch.get("launch_id")->s, meshBatch.get("batch_id")->s};
  const edit::BatchRef newer{
      imageBatch.get("launch_id")->s, imageBatch.get("batch_id")->s};
  REQUIRE(f.restores->eligible(older));
  REQUIRE(f.restores->eligible(newer));
  const auto full = pressureCapacity(f, session, 4);
  REQUIRE_FALSE(full.get("oversized_retention")->b);
  REQUIRE(full.get("available_global_retention_bytes")->n < 20000);
  const auto revision = f.sceneIds->sceneRevision();
  const auto version = f.geometry->lastParameterChange();
  {
    // Another real host consumer holds physical headroom. Admission fits,
    // but complete Restore preparation does not; no injected failure or write.
    auto guard = memory->reserve(memory->limit() - memory->used() - 150000);
    REQUIRE(guard);
    REQUIRE(f.restoreBatch(session, 5, meshBatch).errorCode().empty());
    f.drain();
    guard.reset();
    const auto busy = f.await(session, 5);
    REQUIRE(busy.str("outcome") == "busy");
    REQUIRE(busy.result()->get("reason")->s == "resource_limit");
    REQUIRE_FALSE(busy.flag("committed"));
    CHECK(f.restores->eligible(older));
    CHECK_FALSE(f.restores->conflicting(older));
    CHECK_FALSE(f.restores->restored(older));
    CHECK(f.restores->eligible(newer));
    CHECK(f.sceneIds->sceneRevision() == revision);
    CHECK(f.geometry->lastParameterChange() == version);
    CHECK(
        f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
        == replacement.data());
    CHECK(std::memcmp(image->data(), changed, sizeof(changed)) == 0);
    CHECK(f.status(session, session.id, 5).str("outcome") == "busy");
  }
  REQUIRE(f.restores->eligible(older));
  CHECK(f.sceneIds->sceneRevision() == revision);
  CHECK(f.status(session, session.id, 2).flag("committed"));
  CHECK(f.importPositions(session, 2, *evidence.result()).flag("replayed"));
  CHECK(f.restoreBatch(session, 5, meshBatch).flag("replayed"));
  CHECK(f.status(session, session.id, 5).str("outcome") == "busy");
  const auto unchanged = pressureCapacity(f, session, 6);
  CHECK(unchanged.get("available_global_retention_bytes")->n
      == full.get("available_global_retention_bytes")->n);
  REQUIRE(
      f.importImage(session,
           7,
           f.objectHandle(newestImage.data()),
           *f.sceneIds->imageDataVersion(
               f.sceneIds->handleOf(newestImage.data())),
           "age.png")
          .errorCode()
          .empty());
  const auto newest = f.await(session, 7);
  REQUIRE(newest.str("outcome") == "ok");
  REQUIRE(newest.flag("committed"));
  CHECK_FALSE(f.restores->eligible(older));
  REQUIRE(f.restores->eligible(newer));
  const auto normal = pressureCapacity(f, session, 8);
  CHECK_FALSE(normal.get("oversized_retention")->b);
  CHECK(normal.get("effective_global_retention_byte_cap")->n == 500000);
  f.restoreBatch(session, 9, meshBatch);
  const auto expired = f.await(session, 9);
  REQUIRE(expired.str("outcome") == "outcome_unavailable");
  REQUIRE(expired.result()->get("reason")->s == "expired");
  CHECK_FALSE(expired.flag("committed"));
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == replacement.data());
  // Publication order is fixed; only recovery order varies. Fresh IDs respect
  // the session high-water even when recovering newest before older.
  const auto newestBatch = *newest.result()->get("batch_ref");
  f.restoreBatch(session, 10, reverseRestore ? newestBatch : imageBatch);
  REQUIRE(f.await(session, 10).flag("committed"));
  f.restoreBatch(session, 11, reverseRestore ? imageBatch : newestBatch);
  REQUIRE(f.await(session, 11).flag("committed"));
  CHECK(std::memcmp(image->data(), original, sizeof(original)) == 0);
  CHECK(std::memcmp(newestImage->data(), original, sizeof(original)) == 0);
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == replacement.data());
  CHECK(f.other->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK(std::memcmp(f.positions->data(),
            PositionFixture::POINTS,
            sizeof(PositionFixture::POINTS))
      == 0);
  CHECK(std::filesystem::exists(f.exchangePath() + "/age.png"));
  CHECK(std::filesystem::exists(f.exchangePath() + "/edited.vmesh"));
}

/* Approval is pending while an unrelated operation publishes normal recovery.
 * All operation requests/status/Restore cross the real loopback MCP seam. */
TEST_CASE("Approved imports recheck rolling history after pending pressure",
    "[integration][mcp][retention-delivery]")
{
  const bool mesh = GENERATE(false, true);
  const bool cancel = GENERATE(false, true);
  const bool restoreApproved = GENERATE(false, true);
  PressureReadGate gate;
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 110000;
  limits.hardRetentionBytes = 200000;
  PositionFixture f(limits);
  PressureGateRelease release(&gate);
  const auto session = f.attach();
  const auto evidence = f.working(session);
  auto small = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  auto next = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  auto finalImage = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  const unsigned char original[] = {17, 29, 41, 53};
  small->setData(static_cast<const void *>(original));
  next->setData(static_cast<const void *>(original));
  finalImage->setData(static_cast<const void *>(original));
  writeInput(
      f.exchangePath() + "/small.png", inputPng(1, 1, 6, {0, 1, 2, 3, 4}, {}));
  const auto smallImport =
      [&](const Session &owner, std::uint64_t id, vsr::scene::Array *array) {
        return f.importImage(owner,
            id,
            f.objectHandle(array),
            *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(array)),
            "small.png");
      };
  smallImport(session, 2, small.data());
  const auto old = f.await(session, 2);
  REQUIRE(old.flag("committed"));
  const auto oldBatch = *old.result()->get("batch_ref");
  auto large = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
  const std::vector<unsigned char> before(262144, 71), rows(262400, 0);
  large->setData(static_cast<const void *>(before.data()));
  writeInput(f.exchangePath() + "/agent.png", inputPng(256, 256, 6, rows, {}));
  const auto args = mesh
      ? "\"handle\":" + json::quote(f.objectHandle(f.geometry.data()))
          + ",\"target_version\":"
          + json::write(*evidence.result()->get("target_version"))
          + ",\"dependencies\":"
          + json::write(*evidence.result()->get("dependencies"))
          + ",\"file_name\":\"edited.vmesh\""
      : "\"handle\":" + json::quote(f.objectHandle(large.data()))
          + ",\"data_version\":"
          + json::quote(
              *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(large.data())))
          + ",\"file_name\":\"agent.png\"";
  const auto submit = [&](std::uint64_t id, bool retry) {
    return f.tool(session,
        mesh ? "vsr_mesh_import" : "vsr_image_import",
        "{\"request_id\":" + std::to_string(id)
            + ",\"scene_id\":" + std::to_string(f.sceneId) + "," + args
            + (retry ? ",\"approval_request_id\":3}" : "}"));
  };
  submit(3, false);
  const auto refused = f.await(session, 3);
  REQUIRE_FALSE(refused.flag("committed"));
  REQUIRE(refused.result()->get("history_unchanged")->b);
  auto proposal = f.imageActivity->approvals().snapshot();
  REQUIRE(proposal);
  REQUIRE(f.imageActivity->approvals().approve(
      proposal->identity, proposal->requiredBytes));
  interceptImportRead = true;
  submit(4, true);
  f.integration.service();
  gate.wait();
  REQUIRE(f.restores->eligible(
      {oldBatch.get("launch_id")->s, oldBatch.get("batch_id")->s}));
  CHECK(std::memcmp(large->data(), before.data(), before.size()) == 0);
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  smallImport(session, 5, next.data());
  const auto normal = f.await(session, 5);
  REQUIRE(normal.flag("committed"));
  REQUIRE(f.imageActivity->usage().restorableBatches == 1);
  const auto normalBatch = *normal.result()->get("batch_ref");
  if (cancel)
    f.tool(session,
        "vsr_cancel",
        "{\"operation_ref\":" + ref(session.id, 4) + "}");
  gate.released = true;
  interceptImportRead = false;
  const auto completed = f.await(session, 4);
  std::uint64_t acceptedId = 4;
  Reply accepted = completed;
  if (cancel) {
    REQUIRE_FALSE(completed.flag("committed"));
    REQUIRE(f.restores->eligible(
        {normalBatch.get("launch_id")->s, normalBatch.get("batch_id")->s}));
    REQUIRE(f.imageActivity->approvals().snapshot());
    acceptedId = 6;
    submit(acceptedId, true);
    accepted = f.await(session, acceptedId);
  }
  REQUIRE(accepted.flag("committed"));
  REQUIRE(accepted.str("outcome") == "ok");
  CHECK_FALSE(f.imageActivity->approvals().snapshot());
  CHECK(f.imageActivity->usage().restorableBatches == 1);
  CHECK(f.imageActivity->limits().retentionBytes == 110000);
  CHECK(f.imageActivity->limits().hardRetentionBytes == 200000);
  const auto approvedBatch = *accepted.result()->get("batch_ref");
  f.restoreBatch(session, 7, normalBatch);
  CHECK(f.await(session, 7).str("outcome") == "outcome_unavailable");
  if (restoreApproved) {
    f.restoreBatch(session, 8, approvedBatch);
    REQUIRE(f.await(session, 8).flag("committed"));
    CHECK(std::memcmp(large->data(), before.data(), before.size()) == 0);
    CHECK(
        f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
        == f.positions.data());
    CHECK(std::memcmp(f.positions->data(),
              PositionFixture::POINTS,
              sizeof(PositionFixture::POINTS))
        == 0);
  }
  smallImport(session, 9, finalImage.data());
  const auto newest = f.await(session, 9);
  REQUIRE(newest.flag("committed"));
  const auto currentRevision = f.sceneIds->sceneRevision();
  CHECK(submit(acceptedId, true).flag("replayed"));
  CHECK(f.sceneIds->sceneRevision() == currentRevision);
  CHECK(f.imageActivity->usage().restorableBatches == 1);
  const auto capacity = pressureCapacity(f, session, 10);
  CHECK_FALSE(capacity.get("oversized_retention")->b);
  CHECK(capacity.get("effective_global_retention_byte_cap")->n == 110000);
  CHECK(capacity.get("effective_single_entry_hard_cap_bytes")->n == 200000);
  if (!restoreApproved) {
    f.restoreBatch(session, 11, approvedBatch);
    CHECK(f.await(session, 11).str("outcome") == "outcome_unavailable");
  }
  f.restoreBatch(session, 12, *newest.result()->get("batch_ref"));
  REQUIRE(f.await(session, 12).flag("committed"));
  CHECK(std::memcmp(finalImage->data(), original, sizeof(original)) == 0);
  CHECK_FALSE(gate.timedOut);
}

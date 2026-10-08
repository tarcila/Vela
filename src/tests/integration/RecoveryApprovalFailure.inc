// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

TEST_CASE("Approved public work survives cancellation and allocation failure",
    "[integration][mcp][recovery-approval-failure]")
{
  const bool mesh = GENERATE(false, true);
  const bool cancel = GENERATE(false, true);
  PressureReadGate gate;
  ImageAllocator allocator;
  allocator.remaining = SIZE_MAX;
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 110000;
  limits.hardRetentionBytes = 200000;
  limits.allocator = &allocator;
  PositionFixture f(limits);
  PressureGateRelease release(&gate);
  const auto session = f.attach();
  const auto evidence = f.working(session);
  auto small = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  const unsigned char original[] = {17, 29, 41, 53};
  small->setData(static_cast<const void *>(original));
  writeInput(
      f.exchangePath() + "/small.png", inputPng(1, 1, 6, {0, 1, 2, 3, 4}, {}));
  f.importImage(session,
      2,
      f.objectHandle(small.data()),
      *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(small.data())),
      "small.png");
  const auto old = f.await(session, 2);
  REQUIRE(old.flag("committed"));
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
  REQUIRE(f.await(session, 3).str("outcome") == "busy");
  const auto proposal = f.imageActivity->approvals().snapshot();
  REQUIRE(proposal);
  REQUIRE(f.imageActivity->approvals().approve(
      proposal->identity, proposal->requiredBytes));
  const auto revision = f.sceneIds->sceneRevision();
  const auto held = f.imageActivity->usage().retentionBytes;
  if (cancel)
    interceptImportRead = true;
  else
    allocator.remaining = 0;
  submit(4, true);
  if (cancel) {
    f.integration.service();
    gate.wait();
    f.tool(session,
        "vsr_cancel",
        "{\"operation_ref\":" + ref(session.id, 4) + "}");
    gate.released = true;
    interceptImportRead = false;
  }
  const auto failed = f.await(session, 4);
  REQUIRE_FALSE(failed.flag("committed"));
  CHECK(failed.str("outcome") == (cancel ? "cancelled" : "busy"));
  CHECK(f.sceneIds->sceneRevision() == revision);
  CHECK(f.imageActivity->usage().retentionBytes == held);
  CHECK(f.imageActivity->usage().reservedRetentionBytes == 0);
  const auto retryable = f.imageActivity->approvals().snapshot();
  REQUIRE(retryable);
  CHECK_FALSE(retryable->claimed);
  CHECK(retryable->approvedMaximum == proposal->requiredBytes);
  CHECK(std::memcmp(large->data(), before.data(), before.size()) == 0);
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  allocator.remaining = SIZE_MAX;
  submit(5, true);
  const auto committed = f.await(session, 5);
  REQUIRE(committed.flag("committed"));
  f.restoreBatch(session, 6, *committed.result()->get("batch_ref"));
  REQUIRE(f.await(session, 6).flag("committed"));
  CHECK(std::memcmp(large->data(), before.data(), before.size()) == 0);
}

TEST_CASE("Above-hard no-op Image and Mesh cannot offer approval",
    "[integration][mcp][recovery-approval-noop]")
{
  const bool mesh = GENERATE(false, true);
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 1;
  limits.hardRetentionBytes = 2;
  PositionFixture f(limits);
  const auto session = f.attach();
  const auto evidence = f.working(session);
  if (mesh) {
    f.importPositions(
        session, 2, *evidence.result(), evidence.result()->get("file_name")->s);
  } else {
    auto array = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
    const unsigned char samples[] = {1, 2, 3, 4};
    array->setData(static_cast<const void *>(samples));
    writeInput(
        f.exchangePath() + "/noop.png", inputPng(1, 1, 6, {0, 1, 2, 3, 4}, {}));
    f.importImage(session,
        2,
        f.objectHandle(array.data()),
        *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(array.data())),
        "noop.png");
  }
  const auto unchanged = f.await(session, 2);
  REQUIRE(unchanged.str("outcome") == "ok");
  CHECK_FALSE(unchanged.flag("committed"));
  CHECK(unchanged.result()->get("unchanged")->b);
  CHECK_FALSE(f.imageActivity->approvals().snapshot());
  CHECK(f.imageActivity->usage().restorableBatches == 0);
}

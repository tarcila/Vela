// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

TEST_CASE("Public approved retries compete without multiplying commits",
    "[integration][mcp][recovery-approval-competing]")
{
  const bool mesh = GENERATE(false, true);
  PressureReadGate gate;
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 110000;
  limits.hardRetentionBytes = 200000;
  PositionFixture f(limits);
  PressureGateRelease release(&gate);
  const auto session = f.attach();
  const auto evidence = f.working(session);
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
            + (retry ? ",\"approval_request_id\":2}" : "}"));
  };
  submit(2, false);
  REQUIRE_FALSE(f.await(session, 2).flag("committed"));
  const auto proposal = f.imageActivity->approvals().snapshot();
  REQUIRE(proposal);
  REQUIRE(f.imageActivity->approvals().approve(
      proposal->identity, proposal->requiredBytes));
  const auto revision = f.sceneIds->sceneRevision();
  interceptImportRead = true;
  submit(3, true);
  f.integration.service();
  gate.wait();
  submit(4, true); // independent high-water request while the first is pending
  f.integration.service();
  CHECK(f.sceneIds->sceneRevision() == revision);
  CHECK(f.imageActivity->usage().restorableBatches == 0);
  gate.released = true;
  interceptImportRead = false;
  const auto first = f.await(session, 3);
  const auto second = f.await(session, 4);
  REQUIRE(first.flag("committed") != second.flag("committed"));
  const auto &winner = first.flag("committed") ? first : second;
  CHECK(f.imageActivity->usage().restorableBatches == 1);
  CHECK_FALSE(f.imageActivity->approvals().snapshot());
  const auto published = f.sceneIds->sceneRevision();
  CHECK(submit(3, true).flag("replayed"));
  CHECK(submit(4, true).flag("replayed"));
  CHECK(f.sceneIds->sceneRevision() == published);
  CHECK(f.imageActivity->usage().restorableBatches == 1);
  f.restoreBatch(session, 5, *winner.result()->get("batch_ref"));
  REQUIRE(f.await(session, 5).flag("committed"));
  CHECK(std::memcmp(large->data(), before.data(), before.size()) == 0);
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK_FALSE(gate.timedOut);
}

TEST_CASE("Trusted approval cannot bypass the independent physical guard",
    "[integration][mcp][recovery-approval-physical]")
{
  const bool mesh = GENERATE(false, true);
  const auto memory = std::make_shared<vsr::rendering::FrameBudget>(16u << 20);
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 110000;
  limits.hardRetentionBytes = 200000;
  limits.physicalByteCap = memory->limit();
  limits.reservePhysicalMemory = [memory](std::size_t bytes) {
    return memory->reserve(bytes);
  };
  PositionFixture f(limits);
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
  REQUIRE_FALSE(f.await(session, 3).flag("committed"));
  const auto proposal = f.imageActivity->approvals().snapshot();
  REQUIRE(proposal);
  REQUIRE(f.imageActivity->approvals().approve(
      proposal->identity, proposal->requiredBytes));
  const auto revision = f.sceneIds->sceneRevision();
  const auto retained = f.imageActivity->usage().retentionBytes;
  // Leave transport/result headroom, but not the import's prepared ownership.
  // Approval cannot alter this independent physical limit.
  auto pin = memory->reserve(memory->limit() - memory->used() - (512u << 10));
  REQUIRE(pin);
  REQUIRE_FALSE(memory->reserve(1u << 20));
  const auto started = submit(4, true);
  INFO("physical refusal submission: " << json::write(started.json));
  REQUIRE(started.errorCode().empty());
  const auto refused = f.await(session, 4);
  REQUIRE_FALSE(refused.flag("committed"));
  CHECK(refused.str("outcome") == "busy");
  CHECK(f.sceneIds->sceneRevision() == revision);
  CHECK(f.imageActivity->usage().retentionBytes == retained);
  CHECK(f.imageActivity->usage().restorableBatches == 1);
  CHECK(std::memcmp(large->data(), before.data(), before.size()) == 0);
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  const auto retryable = f.imageActivity->approvals().snapshot();
  REQUIRE(retryable);
  CHECK_FALSE(retryable->claimed);
  CHECK(retryable->approvedMaximum == proposal->requiredBytes);
  pin.reset();
  f.restoreBatch(session, 5, *old.result()->get("batch_ref"));
  REQUIRE(f.await(session, 5).flag("committed"));
  CHECK(std::memcmp(small->data(), original, sizeof(original)) == 0);
  submit(6, true);
  const auto committed = f.await(session, 6);
  REQUIRE(committed.flag("committed"));
  f.restoreBatch(session, 7, *committed.result()->get("batch_ref"));
  REQUIRE(f.await(session, 7).flag("committed"));
  CHECK(std::memcmp(large->data(), before.data(), before.size()) == 0);
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
}

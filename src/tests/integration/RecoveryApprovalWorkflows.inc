// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

TEST_CASE("Public Image and Mesh obtain trusted approval and restore exactly",
    "[integration][mcp][recovery-approval-workflow]")
{
  const bool mesh = GENERATE(false, true);
  const bool failSync = GENERATE(false, true);
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 110000;
  limits.hardRetentionBytes = 200000;
  PositionFixture f(limits);
  const auto session = f.attach();
  const auto evidence = f.working(session);
  auto small = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  const unsigned char smallBefore[] = {17, 29, 41, 53};
  small->setData(static_cast<const void *>(smallBefore));
  writeInput(
      f.exchangePath() + "/small.png", inputPng(1, 1, 6, {0, 1, 2, 3, 4}, {}));
  f.importImage(session,
      2,
      f.objectHandle(small.data()),
      *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(small.data())),
      "small.png");
  const auto old = f.await(session, 2);
  REQUIRE(old.flag("committed"));
  const auto oldBatch = *old.result()->get("batch_ref");
  auto large = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
  const std::vector<unsigned char> before(262144, 71), rows(262400, 0);
  large->setData(static_cast<const void *>(before.data()));
  writeInput(f.exchangePath() + "/large.png", inputPng(256, 256, 6, rows, {}));
  const auto handle = f.objectHandle(large.data());
  const auto version =
      *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(large.data()));
  const auto submit = [&](std::uint64_t id, bool retry, bool forged = false) {
    const auto args = mesh
        ? "\"handle\":" + json::quote(f.objectHandle(f.geometry.data()))
            + ",\"target_version\":"
            + json::write(*evidence.result()->get("target_version"))
            + ",\"dependencies\":"
            + json::write(*evidence.result()->get("dependencies"))
            + ",\"file_name\":\"edited.vmesh\""
        : "\"handle\":" + json::quote(handle) + ",\"data_version\":"
            + json::quote(version) + ",\"file_name\":\"large.png\"";
    return f.tool(session,
        mesh ? "vsr_mesh_import" : "vsr_image_import",
        "{\"request_id\":" + std::to_string(id)
            + ",\"scene_id\":" + std::to_string(f.sceneId) + "," + args
            + (forged       ? ",\"approved\":true}"
                    : retry ? ",\"approval_request_id\":3}"
                            : "}"));
  };
  submit(3, false);
  const auto refused = f.await(session, 3);
  REQUIRE(refused.str("outcome") == "busy");
  REQUIRE_FALSE(refused.flag("committed"));
  vsr::ui::imgui::IntegrationServices services;
  services.integration = &f.integration;
  services.authority = f.authority.get();
  services.activity = f.imageActivity.get();
  vsr::ui::imgui::IntegrationActivityModel model(std::move(services));
  auto proposal = model.refresh().recoveryProposal;
  REQUIRE(proposal);
  CHECK(proposal->identity.originalRequest == 3);
  CHECK(proposal->requiredBytes > 200000);
  CHECK(submit(4, false, true).errorCode() == "invalid_request");
  CHECK(model.refresh().recoveryProposal->approvedMaximum == 0);
  REQUIRE(model.approveRecovery(proposal->identity, proposal->requiredBytes));
  REQUIRE(submit(3, false).flag("replayed"));
  f.hookOk = !failSync;
  submit(4, true);
  const auto accepted = f.await(session, 4);
  INFO(json::write(accepted.json));
  REQUIRE(accepted.flag("committed"));
  REQUIRE(accepted.str("outcome")
      == (failSync ? "applied_with_sync_failure" : "ok"));
  f.hookOk = true;
  CHECK(f.imageActivity->usage().restorableBatches == 1);
  CHECK(f.imageActivity->limits().hardRetentionBytes == 200000);
  CHECK_FALSE(f.imageActivity->approvals().snapshot());
  CHECK(submit(4, true).flag("replayed"));
  f.restoreBatch(session, 5, oldBatch);
  CHECK(f.await(session, 5).str("outcome") == "outcome_unavailable");
  f.restoreBatch(session, 6, *accepted.result()->get("batch_ref"));
  REQUIRE(f.await(session, 6).flag("committed"));
  CHECK(std::memcmp(large->data(), before.data(), before.size()) == 0);
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK(std::memcmp(f.positions->data(),
            PositionFixture::POINTS,
            sizeof(PositionFixture::POINTS))
      == 0);
  submit(7, false);
  CHECK_FALSE(f.await(session, 7).flag("committed"));
}

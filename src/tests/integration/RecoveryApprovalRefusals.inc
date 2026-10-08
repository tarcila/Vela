// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

TEST_CASE("Public approval retry rejects changed bytes and withdrawn decisions",
    "[integration][mcp][recovery-approval-refusal]")
{
  const bool mesh = GENERATE(false, true);
  const int invalid = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
  INFO("mesh=" << mesh << " invalid=" << invalid);
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 110000;
  limits.hardRetentionBytes = 200000;
  PositionFixture f(limits);
  auto session = f.attach();
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
  auto large = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
  const std::vector<unsigned char> before(262144, 71), rows(262400, 0);
  large->setData(static_cast<const void *>(before.data()));
  const auto plain = inputPng(256, 256, 6, rows, {});
  writeInput(f.exchangePath() + "/large.png",
      inputPng(
          256, 256, 6, rows, {{"tEXt", std::vector<unsigned char>(4096)}}));
  const auto handle = f.objectHandle(large.data());
  const auto version =
      *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(large.data()));
  auto args = mesh
      ? "\"handle\":" + json::quote(f.objectHandle(f.geometry.data()))
          + ",\"target_version\":"
          + json::write(*evidence.result()->get("target_version"))
          + ",\"dependencies\":"
          + json::write(*evidence.result()->get("dependencies"))
          + ",\"file_name\":\"edited.vmesh\""
      : "\"handle\":" + json::quote(handle) + ",\"data_version\":"
          + json::quote(version) + ",\"file_name\":\"large.png\"";
  const auto submit = [&](std::uint64_t id, std::uint64_t approval) {
    return f.tool(session,
        mesh ? "vsr_mesh_import" : "vsr_image_import",
        "{\"request_id\":" + std::to_string(id)
            + ",\"scene_id\":" + std::to_string(f.sceneId) + "," + args
            + (approval ? ",\"approval_request_id\":" + std::to_string(approval)
                        + "}"
                        : "}"));
  };
  submit(3, 0);
  REQUIRE(f.await(session, 3).str("outcome") == "busy");
  const auto proposal = f.imageActivity->approvals().snapshot();
  REQUIRE(proposal);
  const auto bytes = f.imageActivity->usage().retentionBytes;
  const auto revision = f.sceneIds->sceneRevision();
  if (invalid == 4) {
    CHECK_FALSE(f.imageActivity->approvals().approve(
        proposal->identity, proposal->requiredBytes - 1));
  } else {
    REQUIRE(f.imageActivity->approvals().approve(
        proposal->identity, proposal->requiredBytes));
  }
  if (invalid == 0) {
    const auto path =
        f.exchangePath() + (mesh ? "/edited.vmesh" : "/large.png");
    struct stat previous{};
    REQUIRE(::stat(path.c_str(), &previous) == 0);
    std::vector<unsigned char> changed;
    if (mesh) {
      std::ifstream input(path, std::ios::binary);
      changed.assign(std::istreambuf_iterator<char>(input), {});
      REQUIRE(changed.size() > 4);
      changed[changed.size() - 4] ^= 1; // finite sample's low mantissa bit
    } else {
      auto changedRows = rows;
      for (std::size_t row = 0; row < 256; ++row)
        std::fill(changedRows.begin() + row * 1025 + 1,
            changedRows.begin() + (row + 1) * 1025,
            1);
      const auto changedPlain = inputPng(256, 256, 6, changedRows, {});
      REQUIRE(changedPlain.size() < plain.size() + 4096);
      changed = inputPng(256,
          256,
          6,
          changedRows,
          {{"tEXt",
              std::vector<unsigned char>(
                  4096 + plain.size() - changedPlain.size())}});
    }
    REQUIRE(changed.size() == std::size_t(previous.st_size));
    writeInput(path, changed);
    const struct timespec times[] = {previous.st_atim, previous.st_mtim};
    REQUIRE(::utimensat(AT_FDCWD, path.c_str(), times, 0) == 0);
  } else if (invalid == 1 || invalid == 7) {
    REQUIRE(f.imageActivity->approvals().revoke(proposal->identity));
  } else if (invalid == 3) {
    f.authority->setAgentEditsDisabled(true);
  } else if (invalid == 5) {
    const operations::Credentials credentials{
        f.authority->launchCredential(), session.id, session.secret};
    // Keep ordinary session authority current; only the proposal expires.
    for (int i = 0; i < 30; ++i) {
      f.now += 10.;
      REQUIRE(f.authority->renew(credentials) == session::Code::OK);
    }
  } else if (invalid == 8) {
    if (mesh) {
      f.exportMesh(session, 10, f.other.data());
      const auto otherEvidence = f.await(session, 10);
      args = "\"handle\":" + json::quote(f.objectHandle(f.other.data()))
          + ",\"target_version\":"
          + json::write(*otherEvidence.result()->get("target_version"))
          + ",\"dependencies\":"
          + json::write(*otherEvidence.result()->get("dependencies"))
          + ",\"file_name\":\"edited.vmesh\"";
    } else {
      auto other = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
      other->setData(static_cast<const void *>(before.data()));
      args = "\"handle\":" + json::quote(f.objectHandle(other.data()))
          + ",\"data_version\":"
          + json::quote(
              *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(other.data())))
          + ",\"file_name\":\"large.png\"";
    }
  } else if (invalid == 9) {
    REQUIRE(f.authority->revokeSession());
    session = f.attach();
    CHECK_FALSE(f.imageActivity->approvals().snapshot());
  } else if (invalid == 6) {
    if (mesh)
      f.positions->setData(static_cast<const void *>(PositionFixture::POINTS));
    else
      large->setData(static_cast<const void *>(before.data()));
  }
  const auto admission = submit(11, invalid == 2 ? 999 : 3);
  if (invalid == 3) {
    REQUIRE(admission.errorCode() == "forbidden");
  } else {
    const auto denied = f.await(session, 11);
    REQUIRE_FALSE(denied.flag("committed"));
  }
  CHECK(f.imageActivity->usage().retentionBytes == bytes);
  CHECK(f.imageActivity->usage().reservedRetentionBytes == 0);
  CHECK(std::memcmp(large->data(), before.data(), before.size()) == 0);
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  if (invalid != 6 && invalid != 8)
    CHECK(f.sceneIds->sceneRevision() == revision);
  f.authority->setAgentEditsDisabled(false);
  f.restoreBatch(session, 12, *old.result()->get("batch_ref"));
  REQUIRE(f.await(session, 12).flag("committed"));
  CHECK(std::memcmp(small->data(), smallBefore, sizeof(smallBefore)) == 0);
}

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

namespace {

struct ApprovalDeviceMap : vsr::scene::EmptyUpdateDelegate
{
  ApprovalDeviceMap(anari::Device device, const vsr::scene::Array &array);
  ~ApprovalDeviceMap() override;
  VSR_NOT_COPYABLE(ApprovalDeviceMap)
  VSR_NOT_MOVEABLE(ApprovalDeviceMap)
  void signalArrayMapped(const vsr::scene::Array *) override;
  void signalArrayUnmapped(const vsr::scene::Array *) override;
  bool mapped{false};

 private:
  anari::Device m_device{nullptr};
  anari::Array m_array{nullptr};
};

ApprovalDeviceMap::ApprovalDeviceMap(
    anari::Device device, const vsr::scene::Array &array)
    : m_device(device), m_array((anari::Array)array.makeANARIObject(device))
{}

ApprovalDeviceMap::~ApprovalDeviceMap()
{
  if (mapped)
    anariUnmapArray(m_device, m_array);
  anari::release(m_device, m_array);
}

void ApprovalDeviceMap::signalArrayMapped(const vsr::scene::Array *)
{
  REQUIRE_FALSE(mapped);
  REQUIRE(anariMapArray(m_device, m_array));
  mapped = true;
}

void ApprovalDeviceMap::signalArrayUnmapped(const vsr::scene::Array *)
{
  REQUIRE(mapped);
  anariUnmapArray(m_device, m_array);
  mapped = false;
}

} // namespace

TEST_CASE("Revocation at Image map preserves its prior recovery evidence",
    "[integration][mcp][recovery-approval-map]")
{
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 110000;
  limits.hardRetentionBytes = 200000;
  MeshQualificationDevice device("helide");
  REQUIRE(device.device);
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, limits, true);
  const auto session = f.attach();
  auto array = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
  const std::vector<unsigned char> before(262144, 71), rows(262400, 0);
  array->setData(static_cast<const void *>(before.data()));
  writeInput(f.exchangePath() + "/agent.png", inputPng(256, 256, 6, rows, {}));
  const auto handle = f.objectHandle(array.data());
  const auto h = f.sceneIds->handleOf(array.data());
  const auto submit = [&](std::uint64_t id,
                          const std::string &version,
                          std::uint64_t approval) {
    return f.tool(session,
        "vsr_image_import",
        "{\"request_id\":" + std::to_string(id) + ",\"scene_id\":"
            + std::to_string(f.sceneId) + ",\"handle\":" + json::quote(handle)
            + ",\"data_version\":" + json::quote(version)
            + ",\"file_name\":\"agent.png\""
            + (approval ? ",\"approval_request_id\":" + std::to_string(approval)
                        + "}"
                        : "}"));
  };
  auto version = *f.sceneIds->imageDataVersion(h);
  submit(1, version, 0);
  REQUIRE_FALSE(f.await(session, 1).flag("committed"));
  auto proposal = f.imageActivity->approvals().snapshot();
  REQUIRE(proposal);
  REQUIRE(f.imageActivity->approvals().approve(
      proposal->identity, proposal->requiredBytes));
  submit(2, version, 1);
  const auto old = f.await(session, 2);
  REQUIRE(old.flag("committed"));
  version = *f.sceneIds->imageDataVersion(h);
  const auto revision = f.sceneIds->sceneRevision();
  const auto held = f.imageActivity->usage().retentionBytes;
  auto changed = rows;
  for (std::size_t row = 0; row < 256; ++row)
    std::fill(changed.begin() + row * 1025 + 1,
        changed.begin() + (row + 1) * 1025,
        1);
  writeInput(
      f.exchangePath() + "/agent.png", inputPng(256, 256, 6, changed, {}));
  submit(3, version, 0);
  REQUIRE_FALSE(f.await(session, 3).flag("committed"));
  proposal = f.imageActivity->approvals().snapshot();
  REQUIRE(proposal);
  REQUIRE(f.imageActivity->approvals().approve(
      proposal->identity, proposal->requiredBytes));
  auto *deviceMap = f.scene.updateDelegate().emplace<ApprovalDeviceMap>(
      device.device, *array);
  auto *observer = f.scene.updateDelegate().emplace<WriteFaultObserver>();
  observer->failMap = false;
  observer->afterBurn = [&] {
    f.imageActivity->approvals().revoke(proposal->identity);
  };
  submit(4, version, 3);
  const auto denied = f.await(session, 4);
  REQUIRE_FALSE(denied.flag("committed"));
  CHECK(f.sceneIds->imageDataVersion(h) == version);
  CHECK(f.sceneIds->sceneRevision() == revision);
  CHECK(f.imageActivity->usage().retentionBytes == held);
  CHECK_FALSE(array->isMapped());
  CHECK_FALSE(deviceMap->mapped);
  observer->afterBurn = {};
  f.restoreBatch(session, 5, *old.result()->get("batch_ref"));
  REQUIRE(f.await(session, 5).flag("committed"));
  CHECK(std::memcmp(array->data(), before.data(), before.size()) == 0);
  CHECK(array->dataWriteSequence() == 4); // aborted reservation 3 stays spent
  CHECK_FALSE(deviceMap->mapped);
}

TEST_CASE("Prepared Image abort preserves real callback edits and lifetimes",
    "[integration][mcp][recovery-approval-map-cleanup]")
{
  const auto boundary = GENERATE(0, 1, 2, 3, 4, 5);
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
  const auto session = f.attach();
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  const unsigned char original[] = {17, 29, 41, 53};
  image->setData(static_cast<const void *>(original));
  writeInput(
      f.exchangePath() + "/agent.png", inputPng(1, 1, 6, {0, 1, 2, 3, 4}, {}));
  const auto h = f.sceneIds->handleOf(image.data());
  const auto handle = f.objectHandle(image.data());
  f.importImage(
      session, 1, handle, *f.sceneIds->imageDataVersion(h), "agent.png");
  const auto old = f.await(session, 1);
  REQUIRE(old.flag("committed"));
  const auto version = *f.sceneIds->imageDataVersion(h);
  auto other = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  other->setData(static_cast<const void *>(original));
  const auto otherSequence = *other->dataWriteSequence();
  const auto revision = f.sceneIds->sceneRevision();
  const auto retained = f.imageActivity->usage().retentionBytes;
  auto auxiliary = std::make_unique<identity::SceneIdentity>(f.ids, f.scene);
  auto *observer = f.scene.updateDelegate().emplace<WriteFaultObserver>();
  observer->failMap = boundary == 1;
  const operations::Credentials credentials{
      f.authority->launchCredential(), session.id, session.secret};
  bool entered = false;
  const unsigned char external[] = {9, 10, 11, 12};
  observer->afterBurn = [&] {
    if (entered)
      return;
    entered = true;
    if (boundary == 2)
      other->setData(static_cast<const void *>(external));
    if (boundary == 3)
      f.scene.removeObject(image.data());
    if (boundary == 5)
      auxiliary.reset();
    if (boundary == 4) {
      // Relocate pooled storage and erase the active callback's registration.
      // Prepared notification leases keep it alive until cleanup finishes.
      for (unsigned i = 0; i < 100; ++i)
        f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
      f.scene.updateDelegate().erase(observer);
    }
    f.engine->cancel(credentials, {session.id, 2});
  };
  writeInput(
      f.exchangePath() + "/agent.png", inputPng(1, 1, 6, {0, 5, 6, 7, 8}, {}));
  f.importImage(session, 2, handle, version, "agent.png");
  const auto refused = f.await(session, 2);
  REQUIRE_FALSE(refused.flag("committed"));
  CHECK(f.imageActivity->usage().retentionBytes == retained);
  if (boundary == 2) {
    CHECK(other->dataWriteSequence() > otherSequence);
    CHECK(std::memcmp(other->data(), external, sizeof(external)) == 0);
  } else
    CHECK(other->dataWriteSequence() == otherSequence);
  if (boundary == 0 || boundary == 1 || boundary == 5)
    CHECK(f.sceneIds->sceneRevision() == revision);
  else
    CHECK_FALSE(f.sceneIds->sceneRevision() == revision);
  if (boundary != 4) {
    observer->afterBurn = {};
    observer->failMap = false;
  }
  if (boundary != 3) {
    auto *current = dynamic_cast<vsr::scene::Array *>(f.sceneIds->resolve(h));
    REQUIRE(current);
    CHECK_FALSE(current->isMapped());
    CHECK(f.sceneIds->imageDataVersion(h) == version);
    f.restoreBatch(session, 3, *old.result()->get("batch_ref"));
    REQUIRE(f.await(session, 3).flag("committed"));
    CHECK(std::memcmp(current->data(), original, sizeof(original)) == 0);
    const auto completed = *current->dataWriteSequence();
    current->map(); // ordinary no-write map/unmap still publishes freshness
    current->unmap();
    CHECK(current->dataWriteSequence() == completed + 1);
  } else {
    CHECK_FALSE(f.sceneIds->resolve(h));
    f.restoreBatch(session, 3, *old.result()->get("batch_ref"));
    CHECK_FALSE(f.await(session, 3).flag("committed"));
  }
}

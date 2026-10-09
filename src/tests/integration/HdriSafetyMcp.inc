// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

namespace {
void writePublicHdri(const std::string &path)
{
  std::ofstream file(path, std::ios::binary);
  file << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 1\n";
  const unsigned char pixels[] = {128, 64, 32, 132, 32, 128, 64, 132};
  file.write(reinterpret_cast<const char *>(pixels), sizeof(pixels));
  REQUIRE(file.good());
}

std::string publicHdriArgs(
    Fixture &f, std::uint64_t id, const std::string &extra = {})
{
  return "{\"request_id\":" + std::to_string(id)
      + ",\"scene_id\":" + std::to_string(f.sceneId)
      + ",\"asset_root\":0,\"file_name\":\"safety.hdr\"" + extra + "}";
}

struct HdriCheckpointNotification
{
  std::shared_ptr<std::atomic<bool>> entered;
  std::shared_ptr<std::atomic<bool>> released;
  void observe() const;
};

void HdriCheckpointNotification::observe() const
{
  entered->store(true);
  while (!released->load())
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

struct HdriWorkerCheckpoint
{
  HdriWorkerCheckpoint();
  ~HdriWorkerCheckpoint();
  VSR_NOT_COPYABLE(HdriWorkerCheckpoint)
  std::shared_ptr<std::atomic<bool>> entered;
  std::shared_ptr<std::atomic<bool>> released;
  operations::SharedNotification observer() const;
  void wait(Fixture &f) const;
};

HdriWorkerCheckpoint::HdriWorkerCheckpoint()
    : entered(std::make_shared<std::atomic<bool>>(false)),
      released(std::make_shared<std::atomic<bool>>(false))
{}

HdriWorkerCheckpoint::~HdriWorkerCheckpoint()
{
  released->store(true);
}

operations::SharedNotification HdriWorkerCheckpoint::observer() const
{
  auto notification = std::make_shared<HdriCheckpointNotification>();
  notification->entered = entered;
  notification->released = released;
  return operations::SharedNotification::from(std::move(notification));
}

void HdriWorkerCheckpoint::wait(Fixture &f) const
{
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!entered->load()) {
    f.drain();
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
} // namespace

SCENARIO("Public HDRI typed defaults preserve resident HDR rows and placement",
    "[integration][content][mcp][public-hdri-safety]")
{
  HdriFixture f;
  auto parent = f.scene.insertChildTransformNode(f.scene.defaultLayer()->root(),
      vsr::math::IDENTITY_MAT4,
      "borrowed parent");
  auto camera = f.scene.defaultCamera();
  const auto cameraVersion = camera->lastParameterChange();
  const auto s = f.attach();
  writePublicHdri(f.tmp.path + "/safety.hdr");
  f.tool(s,
      "vsr_content_options",
      "{\"request_id\":1,\"scene_id\":" + std::to_string(f.sceneId) + "}");
  const auto options = f.await(s, 1);
  REQUIRE(options.str("outcome") == "ok");
  CHECK(options.result()->get("defaults")->get("scale")->n == 1);
  CHECK(options.result()->get("source_profile")->s
      == "radiance_rgbe_minus_y_plus_x");
  const auto ph = f.sceneIds->handleOf(parent);
  f.tool(s,
      "vsr_create_hdri",
      publicHdriArgs(f,
          2,
          ",\"parent\":\"" + edit::detail::hex64(ph.domain)
              + edit::detail::hex64(ph.serial)
              + "\",\"transform\":[1,0,0,0,0,1,0,0,0,0,1,0,2,3,4,1]"));
  const auto created = f.await(s, 2);
  REQUIRE(created.str("outcome") == "ok");
  const auto *primary = created.result()->get("primary");
  const auto lh =
      mcp::tools::detail::handleArg(primary->get("light"), f.ids.launch());
  const auto ah =
      mcp::tools::detail::handleArg(primary->get("radiance"), f.ids.launch());
  const auto rh = mcp::tools::detail::handleArg(
      primary->get("content_root"), f.ids.launch());
  REQUIRE(lh);
  REQUIRE(ah);
  REQUIRE(rh);
  auto *light = f.sceneIds->resolve(*lh);
  auto *array = dynamic_cast<vsr::scene::Array *>(f.sceneIds->resolve(*ah));
  const auto root = f.sceneIds->resolve(
      identity::NodeHandle{rh->launch, rh->domain, rh->serial});
  REQUIRE(light);
  REQUIRE(array);
  REQUIRE(root);
  CHECK(array->elementType() == ANARI_FLOAT32_VEC3);
  CHECK(array->type() == ANARI_ARRAY2D);
  REQUIRE(array->size() == 2);
  const auto *rgb = static_cast<const vsr::math::float3 *>(array->data());
  CHECK(rgb[0] == vsr::math::float3(2, 8, 4));
  CHECK(rgb[1] == vsr::math::float3(8, 4, 2));
  CHECK(*light->parameterValueAs<float>("scale") == 1.f);
  CHECK(created.result()->get("effective_settings")->get("scale")->n == 1);
  CHECK(*light->parameterValueAs<vsr::math::float3>("up")
      == vsr::math::float3(0, 1, 0));
  CHECK(*light->parameterValueAs<vsr::math::float3>("direction")
      == vsr::math::float3(1, 0, 0));
  CHECK(root->parent() == parent);
  CHECK(root->value().getTransform()[3] == vsr::math::float4(2, 3, 4, 1));
  parent->value().setName("unrelated borrowed label");
  f.scene.defaultLayer()->setName("unrelated borrowed Layer label");
  camera->setParameter("position", vsr::math::float3(9, 8, 7));
  CHECK(camera->lastParameterChange() != cameraVersion);
  f.restoreBatch(s, 3, *created.result()->get("batch_ref"));
  CHECK(f.await(s, 3).str("outcome") == "ok");
  CHECK(parent->value().name() == "unrelated borrowed label");
  CHECK(f.scene.defaultLayer()->name() == "unrelated borrowed Layer label");
  CHECK(*camera->parameterValueAs<vsr::math::float3>("position")
      == vsr::math::float3(9, 8, 7));
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 0);
}

SCENARIO("Public HDRI owned Parameter metadata change-back conflicts",
    "[integration][content][mcp][meta-param-public]")
{
  const auto change = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
  const auto ownLayer = GENERATE(false, true);
  HdriFixture f;
  auto parent = f.scene.insertChildTransformNode(
      f.scene.defaultLayer()->root(), vsr::math::IDENTITY_MAT4, "parent");
  const auto s = f.attach();
  writePublicHdri(f.tmp.path + "/safety.hdr");
  const auto ph = f.sceneIds->handleOf(parent);
  const auto destination = ownLayer
      ? ",\"new_layer\":\"owned\""
      : ",\"parent\":\"" + edit::detail::hex64(ph.domain)
          + edit::detail::hex64(ph.serial) + "\"";
  const auto args = publicHdriArgs(f, 1, destination);
  f.tool(s, "vsr_create_hdri", args);
  const auto created = f.await(s, 1);
  REQUIRE(created.flag("committed"));
  const auto handle = mcp::tools::detail::handleArg(
      created.result()->get("primary")->get("light"), f.ids.launch());
  REQUIRE(handle);
  auto *light = f.sceneIds->resolve(*handle);
  REQUIRE(light);
  auto *scale = light->parameter("scale");
  REQUIRE(scale);
  const auto metadata = light->lastMetadataChange();
  const auto parameters = light->lastParameterChange();
  const auto before = *scale;
  switch (change) {
  case 0:
    scale->setDescription("ordinary owned metadata edit");
    scale->setDescription(before.description().c_str());
    break;
  case 1:
    scale->setMin(2.f);
    scale->setMin(before.min());
    break;
  case 2:
    scale->setMax(2.f);
    scale->setMax(before.max());
    break;
  case 3:
    scale->setUsage(vsr::scene::ParameterUsageHint::COLOR);
    scale->setUsage(before.usage());
    break;
  case 4:
    scale->setStringValues({"edited", "metadata"});
    scale->setStringValues(before.stringValues());
    break;
  case 5:
    scale->setStringSelection(7);
    scale->setStringSelection(before.stringSelection());
    break;
  case 6:
    scale->setEnabled(false);
    scale->setEnabled(before.isEnabled());
    break;
  case 7: {
    auto copy = before;
    copy.setDescription("assigned metadata");
    *scale = copy;
    *scale = before;
    break;
  }
  case 8: {
    auto copy = before;
    copy.setDescription("move-assigned metadata");
    *scale = std::move(copy);
    auto original = before;
    *scale = std::move(original);
    break;
  }
  case 9:
    light->removeParameter("scale");
    break;
  }
  if (change < 9)
    CHECK(light->lastMetadataChange() > metadata);
  else
    CHECK(light->lastParameterChange() > parameters);
  if (change < 6)
    CHECK(light->lastParameterChange() == parameters);
  f.restoreBatch(s, 2, *created.result()->get("batch_ref"));
  const auto result = f.await(s, 2);
  CHECK(result.str("outcome") == "conflict");
  CHECK_FALSE(result.flag("committed"));
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 1);
  CHECK(f.sceneIds->resolve(*handle) == light);
}

SCENARIO("Detached Parameter editing cannot mutate a created Light",
    "[integration][content][mcp][meta-param-public]")
{
  const auto ownLayer = GENERATE(false, true);
  HdriFixture f;
  f.scene.defaultLayer();
  const auto s = f.attach();
  writePublicHdri(f.tmp.path + "/safety.hdr");
  f.tool(s,
      "vsr_create_hdri",
      publicHdriArgs(f, 1, ownLayer ? ",\"new_layer\":\"owned\"" : ""));
  const auto created = f.await(s, 1);
  REQUIRE(created.flag("committed"));
  const auto handle = mcp::tools::detail::handleArg(
      created.result()->get("primary")->get("light"), f.ids.launch());
  REQUIRE(handle);
  auto *light = f.sceneIds->resolve(*handle);
  REQUIRE(light);
  const auto metadata = light->lastMetadataChange();
  const auto parameters = light->lastParameterChange();
  const auto original = light->parameter("scale")->value().get<float>();
  auto copy = *light->parameter("scale");
  auto moved = std::move(*light->parameter("scale"));
  copy.setDescription("private copy").setValue(2.f);
  moved.setDescription("private move snapshot").setMin(-2.f).setValue(3.f);
  CHECK(light->lastMetadataChange() == metadata);
  CHECK(light->lastParameterChange() == parameters);
  CHECK(light->parameter("scale")->value().get<float>() == original);
  f.restoreBatch(s, 2, *created.result()->get("batch_ref"));
  const auto result = f.await(s, 2);
  CHECK(result.str("outcome") == "ok");
  CHECK(result.flag("committed"));
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 0);
}

SCENARIO("Public HDRI edited owned closure never regains removal eligibility",
    "[integration][content][mcp][public-hdri-safety]")
{
  const auto change = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
  HdriFixture f;
  f.scene.defaultLayer();
  const auto s = f.attach();
  writePublicHdri(f.tmp.path + "/safety.hdr");
  f.tool(
      s, "vsr_create_hdri", publicHdriArgs(f, 1, ",\"new_layer\":\"owned\""));
  const auto created = f.await(s, 1);
  REQUIRE(created.str("outcome") == "ok");
  const auto *p = created.result()->get("primary");
  const auto lh =
      mcp::tools::detail::handleArg(p->get("light"), f.ids.launch());
  const auto ah =
      mcp::tools::detail::handleArg(p->get("radiance"), f.ids.launch());
  const auto rh =
      mcp::tools::detail::handleArg(p->get("content_root"), f.ids.launch());
  const auto nh =
      mcp::tools::detail::handleArg(p->get("light_node"), f.ids.launch());
  REQUIRE(lh);
  REQUIRE(ah);
  REQUIRE(rh);
  REQUIRE(nh);
  auto *light = f.sceneIds->resolve(*lh);
  auto *array = dynamic_cast<vsr::scene::Array *>(f.sceneIds->resolve(*ah));
  auto root = f.sceneIds->resolve(
      identity::NodeHandle{rh->launch, rh->domain, rh->serial});
  auto node = f.sceneIds->resolve(
      identity::NodeHandle{nh->launch, nh->domain, nh->serial});
  REQUIRE(light);
  REQUIRE(array);
  REQUIRE(root);
  REQUIRE(node);
  switch (change) {
  case 0: {
    const auto color = static_cast<const vsr::math::float3 *>(array->data())[0];
    array->setData(&color, 1); // A write of equal bytes spends history.
    break;
  }
  case 1:
    light->setParameter("scale", 2.f);
    light->setParameter("scale", 1.f);
    break;
  case 2:
    root->value().setName("changed");
    root->value().setName("HDRI Content");
    break;
  case 3:
    node->value().setInstanceParameter("scale", vsr::core::Any(2.f));
    node->value().clearInstanceParameters();
    break;
  case 4:
    root->value().setAsTransform(
        vsr::math::translation_matrix(vsr::math::float3(1)));
    root->value().setAsTransform(vsr::math::IDENTITY_MAT4);
    break;
  case 5:
    f.scene.insertChildNode(root, "new child");
    break;
  case 6:
    root->value().layer()->setName("changed");
    root->value().layer()->setName("owned");
    break;
  case 7:
    f.scene.setLayerActive("owned", false);
    f.scene.setLayerActive("owned", true);
    break;
  case 8:
    light->setName("changed");
    light->setName("");
    break;
  case 9:
    root->container()->move_subtree(node, root->parent());
    root->container()->move_subtree(node, root);
    break;
  case 10:
    root->value().setEnabled(false);
    root->value().setEnabled(true);
    break;
  }
  const auto objects = f.scene.numberOfObjects(ANARI_LIGHT);
  const auto nodes = root->value().layer()->size();
  f.restoreBatch(s, 2, *created.result()->get("batch_ref"));
  const auto result = f.await(s, 2);
  CHECK(result.str("outcome") == "conflict");
  CHECK_FALSE(result.flag("committed"));
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == objects);
  CHECK(root->value().layer()->size() == nodes);
  CHECK(f.sceneIds->resolve(*lh) == light);
  CHECK(f.sceneIds->resolve(*ah) == array);
}

SCENARIO(
    "Public HDRI worker currency authority cancellation and deadline are guarded",
    "[integration][content][mcp][public-hdri-lifecycle]")
{
  const auto fault = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
  const auto physical =
      std::make_shared<vsr::rendering::FrameBudget>(256u << 20);
  const auto ownerThread = std::this_thread::get_id();
  std::atomic<size_t> workerBytes{0};
  image::ExchangeLimits limits;
  limits.reservePhysicalMemory = [&, physical](size_t bytes) {
    auto lease = physical->reserve(bytes);
    // This two-pixel source/decoder has a sub-4-KiB allocator peak. Larger
    // RPC Job/status controls have different surviving owner lifetimes.
    if (!lease || bytes >= 4096 || std::this_thread::get_id() == ownerThread)
      return lease;
    workerBytes.fetch_add(bytes);
    return std::shared_ptr<const void>(physical.get(),
        [lease = std::move(lease), &workerBytes, bytes](
            const void *) { workerBytes.fetch_sub(bytes); });
  };
  HdriFixture f(limits);
  f.scene.defaultLayer();
  HdriWorkerCheckpoint
      checkpoint; // Releases before Fixture destruction on failure.
  f.creator->setWorkerObserver(checkpoint.observer());
  const auto s = f.attach();
  writePublicHdri(f.tmp.path + "/safety.hdr");
  // An ordinary existing recovery entry must survive every failed attempt.
  const auto target = f.firstTarget(s, 1);
  f.editOpacity(s, 2, target, .25);
  const auto prior = f.await(s, 2);
  REQUIRE(prior.flag("committed"));
  const auto batches = f.imageActivity->usage().restorableBatches;
  f.tool(s, "vsr_create_hdri", publicHdriArgs(f, 3));
  checkpoint.wait(f);
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 0);
  CHECK(f.status(s, s.id, 3).str("state") != "terminal");
  if (fault == 0) {
    std::ofstream file(
        f.tmp.path + "/safety.hdr", std::ios::binary | std::ios::app);
    file.put('x');
  } else if (fault == 1) {
    REQUIRE(f.creator->assetRoots().revoke(0));
  } else if (fault == 2) {
    f.authority->setAgentEditsDisabled(true);
  } else if (fault == 3) {
    f.authority->restrict(
        session::Target::scene(f.sceneId), session::Capability::READ);
  } else if (fault == 4) {
    f.tool(s, "vsr_cancel", "{\"operation_ref\":" + ref(s.id, 3) + "}");
  } else if (fault == 5) {
    f.now += 31.;
  } else if (fault == 6) {
    const auto name = f.tmp.path + "/safety.hdr";
    REQUIRE(::unlink(name.c_str()) == 0);
    writePublicHdri(
        name); // Same bytes, new file identity is not source currency.
  } else {
    f.scene.defaultLayer()->root()->value().setAsTransform(
        vsr::math::translation_matrix(vsr::math::float3(1)));
  }
  f.drain(); // Native work remains held, not falsely freed by cancellation.
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 0);
  CHECK(workerBytes.load() > 0);
  if (fault == 4 || fault == 5)
    CHECK(f.status(s, s.id, 3).str("progress") == "awaiting_cleanup");
  checkpoint.released->store(true);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  Reply result;
  do {
    f.drain();
    result = f.status(s, s.id, 3);
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (
      result.str("state") != "terminal" || !result.str("progress").empty());
  CHECK(workerBytes.load() == 0);
  CHECK_FALSE(result.flag("committed"));
  CHECK(result.str("outcome") != "ok");
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 0);
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == 0);
  CHECK(f.imageActivity->usage().restorableBatches == batches);
  CHECK(f.imageActivity->usage().reservedRetentionBytes == 0);
}

SCENARIO("Public HDRI malformed and confined input failures preserve Scene",
    "[integration][content][mcp][public-hdri-safety]")
{
  const auto file = GENERATE("missing.hdr",
      "../escape.hdr",
      "/etc/passwd",
      "link.hdr",
      "truncated.hdr");
  HdriFixture f;
  f.scene.defaultLayer();
  const auto s = f.attach();
  REQUIRE(::symlink("/etc/passwd", (f.tmp.path + "/link.hdr").c_str()) == 0);
  {
    std::ofstream truncated(f.tmp.path + "/truncated.hdr");
    truncated << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 1\n";
  }
  f.tool(s,
      "vsr_create_hdri",
      "{\"request_id\":1,\"scene_id\":" + std::to_string(f.sceneId)
          + ",\"asset_root\":0,\"file_name\":" + json::quote(file) + "}");
  const auto result = f.await(s, 1);
  CHECK_FALSE(result.flag("committed"));
  CHECK(result.str("outcome") != "ok");
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 0);
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == 0);
  CHECK(f.scene.numberOfLayers() == 1);
  CHECK(f.imageActivity->usage().restorableBatches == 0);
}

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

SCENARIO("Public HDRI IncludeData survives input and original host retirement",
    "[integration][content][mcp][public-hdri-persistence]")
{
  TempDir archiveDir;
  const auto archive = archiveDir.path + "/resident.vsr";
  std::string oldBatch;
  std::string oldLaunch;
  session::Credentials oldCredentials;
  {
    HdriFixture f;
    auto positions = f.scene.createArray(ANARI_FLOAT32_VEC3, 3);
    const vsr::math::float3 points[] = {{-1, -1, 0}, {1, -1, 0}, {0, 1, 0}};
    positions->setData(points, 3);
    auto geometry = f.scene.createObject<vsr::scene::Geometry>("triangle");
    geometry->setParameterObject("vertex.position", *positions);
    auto material = f.scene.createObject<vsr::scene::Material>("matte");
    material->setParameter("color", vsr::math::float3(.5f));
    auto surface =
        f.scene.createSurface("fixed persistence geometry", geometry, material);
    f.scene.insertChildObjectNode(f.scene.defaultLayer()->root(), surface);
    auto camera = f.scene.createObject<vsr::scene::Camera>("perspective");
    camera->setName("persistence camera");
    camera->setParameter("position", vsr::math::float3(0, 0, 3));
    camera->setParameter("direction", vsr::math::float3(0, 0, -1));
    camera->setParameter("up", vsr::math::float3(0, 1, 0));
    const auto session = f.attach();
    const auto input = f.tmp.path + "/resident.hdr";
    {
      std::ofstream file(input, std::ios::binary);
      file << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 1\n";
      const unsigned char pixels[] = {128, 64, 32, 132, 128, 64, 32, 133};
      file.write(reinterpret_cast<const char *>(pixels), sizeof(pixels));
      REQUIRE(file.good());
    }
    f.tool(session, "vsr_create_hdri",
        "{\"request_id\":1,\"scene_id\":" + std::to_string(f.sceneId)
        + ",\"asset_root\":0,\"file_name\":\"resident.hdr\",\"root_name\":\"resident root\","
          "\"new_layer\":\"resident HDRI\",\"scale\":0.0625,\"up\":[0,0,1],\"direction\":[1,0,0],"
          "\"transform\":[1,0,0,0,0,1,0,0,0,0,1,0,2,3,4,1]}");
    const auto created = f.await(session, 1);
    REQUIRE(created.str("outcome") == "ok");
    REQUIRE(created.flag("committed"));
    oldBatch = json::write(*created.result()->get("batch_ref"));
    oldLaunch = f.authority->launchId();
    oldCredentials = {
        f.authority->launchCredential(), session.id, session.secret};
    REQUIRE(std::remove(input.c_str()) == 0);
    REQUIRE_FALSE(std::filesystem::exists(input));
    REQUIRE(vsr::io::save_SceneArchive(
        f.scene, archive.c_str(), vsr::io::ArrayDataPolicy::IncludeData));
  } // Original creator, workers, Scene, roots, session and recovery are
    // retired.

  HdriFixture loaded;
  REQUIRE(vsr::io::load_SceneArchive(loaded.scene, archive.c_str()));
  REQUIRE(loaded.scene.numberOfObjects(ANARI_LIGHT) == 1);
  REQUIRE(loaded.scene.numberOfLayers() == 2);
  auto *layer = loaded.scene.layer(vsr::core::Token("resident HDRI"));
  REQUIRE(layer);
  auto root = layer->root()->next();
  REQUIRE(root);
  CHECK(root->value().name() == "resident root");
  CHECK(root->parent() == layer->root());
  CHECK(root->value().getTransform()[3] == vsr::math::float4(2, 3, 4, 1));
  auto node = root->next();
  REQUIRE(node);
  CHECK(node->parent() == root);
  auto light = loaded.scene.getObject<vsr::scene::Light>(0);
  REQUIRE(light);
  CHECK(node->value().getObject() == light.data());
  CHECK(light->subtype() == vsr::scene::tokens::light::hdri);
  CHECK(*light->parameterValueAs<float>("scale") == .0625f);
  CHECK(*light->parameterValueAs<vsr::math::float3>("up")
      == vsr::math::float3(0, 0, 1));
  CHECK(*light->parameterValueAs<vsr::math::float3>("direction")
      == vsr::math::float3(1, 0, 0));
  auto *radiance = dynamic_cast<vsr::scene::Array *>(
      light->parameterValueAsObject("radiance"));
  REQUIRE(radiance);
  CHECK(radiance->type() == ANARI_ARRAY2D);
  CHECK(radiance->elementType() == ANARI_FLOAT32_VEC3);
  CHECK(radiance->dim(0) == 1);
  CHECK(radiance->dim(1) == 2);
  REQUIRE(radiance->size() == 2);
  REQUIRE(radiance->data());
  const auto *rgb = static_cast<const vsr::math::float3 *>(radiance->data());
  CHECK(rgb[0] == vsr::math::float3(16, 8, 4));
  CHECK(rgb[1] == vsr::math::float3(8, 4, 2));
  const auto session = loaded.attach();
  CHECK(loaded.authority->launchId() != oldLaunch);
  loaded.tool(session,
      "vsr_restore",
      "{\"request_id\":1,\"scene_id\":" + std::to_string(loaded.sceneId)
          + ",\"target_batch_ref\":" + oldBatch + "}");
  CHECK(loaded.await(session, 1).str("outcome") != "ok");
  const auto denied =
      loaded.engine->status(oldCredentials, {oldCredentials.sessionId, 1});
  CHECK(denied.code != operations::Outcome::OK);
  loaded.tool(session,
      "vsr_create_hdri",
      "{\"request_id\":2,\"scene_id\":" + std::to_string(loaded.sceneId)
          + ",\"asset_root\":0,\"file_name\":\"resident.hdr\"}");
  CHECK_FALSE(loaded.await(session, 2).flag("committed"));
  CHECK(loaded.scene.numberOfObjects(ANARI_LIGHT) == 1);

  MeshQualificationDevice device("visrtx");
  REQUIRE(device.device);
  const auto renderer = device.lightingRenderer("visrtx");
  REQUIRE(renderer);
  std::ifstream maps("/proc/self/maps");
  std::string map;
  while (std::getline(maps, map))
    if (map.find("libanari") != std::string::npos
        || map.find("libnvoptix") != std::string::npos)
      std::fprintf(stderr, "HDRI persistence loaded: %s\n", map.c_str());
  auto *index = loaded.scene.updateDelegate()
                    .emplace<vsr::rendering::RenderIndexAllLayers>(loaded.scene,
                        vsr::core::Token("resident control"),
                        device.device);
  index->populate();
  HelideBackend backend;
  backend.d = device.device;
  backend.index = index;
  backend.cameraIndex = 0;
  backend.renderer = anari::newObject<anari::Renderer>(device.device, renderer);
  anari::setParameter(device.device, backend.renderer, "pixelSamples", 64u);
  anari::setParameter(device.device, backend.renderer, "ambientRadiance", 0.f);
  anari::setParameter(device.device,
      backend.renderer,
      "background",
      vsr::math::float4(0, 0, 0, 1));
  anari::commitParameters(device.device, backend.renderer);
  const auto world = index->world();
  const auto cameraVersion =
      loaded.scene.getObject<vsr::scene::Camera>(0)->lastParameterChange();
  const auto pixels = [&] {
    CHECK(index->world() == world);
    CHECK(loaded.scene.getObject<vsr::scene::Camera>(0)->lastParameterChange()
        == cameraVersion);
    REQUIRE(backend.begin(64, 48));
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (backend.poll() == render::BackendState::PENDING) {
      REQUIRE(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto image = backend.image();
    const auto *bytes = static_cast<const std::uint8_t *>(image.pixels);
    std::vector<std::uint8_t> result(bytes, bytes + 64 * 48 * 4);
    REQUIRE(backend.release());
    return result;
  };
  loaded.scene.setLayerActive(vsr::core::Token("resident HDRI"), false);
  const auto before = pixels();
  loaded.scene.setLayerActive(vsr::core::Token("resident HDRI"), true);
  const auto lit = pixels();
  if (const char *evidence = std::getenv("VSR_HDRI_CONTROL_EVIDENCE")) {
    for (const auto &image :
        {std::make_pair("before", &before), std::make_pair("lit", &lit)}) {
      std::ofstream output(
          std::string(evidence) + "/persistence-" + image.first + ".rgba",
          std::ios::binary);
      output.write(reinterpret_cast<const char *>(image.second->data()),
          std::streamsize(image.second->size()));
      REQUIRE(output.good());
    }
  }
  size_t brighter = 0;
  for (size_t y = 12; y < 36; ++y)
    for (size_t x = 24; x < 40; ++x) {
      const auto p = (y * 64 + x) * 4;
      if (int(lit[p]) > int(before[p]) + 20 && lit[p] > lit[p + 2] + 10)
        ++brighter;
    }
  const auto center = (24 * 64 + 32) * 4;
  std::fprintf(stderr,
      "HDRI persistence %s@visrtx center=%u,%u,%u -> %u,%u,%u; brighter=%zu\n",
      renderer,
      before[center],
      before[center + 1],
      before[center + 2],
      lit[center],
      lit[center + 1],
      lit[center + 2],
      brighter);
  CHECK(brighter > 30);
  loaded.scene.setLayerActive(vsr::core::Token("resident HDRI"), false);
  CHECK(pixels() == before);
  anari::release(device.device, backend.renderer);
  loaded.scene.updateDelegate().erase(index);
}

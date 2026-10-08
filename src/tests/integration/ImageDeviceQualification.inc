// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/* Public Image Import/Restore qualification. Reuses only the isolated device
 * and host lifetime guards from Mesh qualification, not its pixel verdict. */
SCENARIO("Public Image exchange refreshes existing real device textures",
    "[mcp][.image-device]")
{
  const char *library = std::getenv("VSR_IMAGE_QUALIFICATION_LIBRARY");
  REQUIRE(library);
  const std::string name(library);
  REQUIRE((name == "helide" || name == "visrtx" || name == "barney"));
  MeshQualificationDevice device(library);
  REQUIRE(device.device);
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
  auto geometry = f.scene.createObject<vsr::scene::Geometry>("triangle");
  const float points[] = {-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0};
  const std::uint32_t triangles[] = {0, 1, 2, 0, 2, 3};
  const float uv[] = {0, 0, 1, 0, 1, 1, 0, 1};
  auto positions = f.scene.createArray(ANARI_FLOAT32_VEC3, 4);
  auto indices = f.scene.createArray(ANARI_UINT32_VEC3, 2);
  auto coordinates = f.scene.createArray(ANARI_FLOAT32_VEC2, 4);
  positions->setData(static_cast<const void *>(points));
  indices->setData(static_cast<const void *>(triangles));
  coordinates->setData(static_cast<const void *>(uv));
  geometry->setParameterObject("vertex.position", *positions);
  geometry->setParameterObject("primitive.index", *indices);
  geometry->setParameterObject("vertex.attribute0", *coordinates);
  auto image = f.scene.createArray(ANARI_UFIXED8_RGBA_SRGB, 1, 1);
  const unsigned char green[] = {0, 255, 0, 255};
  const unsigned char red[] = {255, 0, 0, 255};
  image->setData(static_cast<const void *>(green));
  auto sampler = f.scene.createObject<vsr::scene::Sampler>("image2D");
  sampler->setParameterObject("image", *image);
  sampler->setParameter("inAttribute", "attribute0");
  sampler->setParameter("filter", "nearest");
  auto material = f.scene.createObject<vsr::scene::Material>("matte");
  material->setParameterObject("color", *sampler);
  auto control = f.scene.createObject<vsr::scene::Material>("matte");
  control->setParameter("color", vsr::math::float3(0, 1, 0));
  auto root = f.scene.defaultLayer()->root();
  std::array<vsr::scene::SurfaceRef, 3> surfaces;
  for (int i = 0; i < 3; ++i) {
    surfaces[i] = f.scene.createSurface(
        "private Image qualification", geometry, i == 2 ? control : material);
    auto matrix = vsr::math::IDENTITY_MAT4;
    matrix[3].x = float(i - 1) * 3.7f;
    f.scene.insertChildObjectNode(
        f.scene.insertChildTransformNode(root, matrix), surfaces[i]);
  }
  auto light = f.scene.createObject<vsr::scene::Light>("directional");
  light->setParameter("direction", vsr::math::float3(0, 0, -1));
  light->setParameter("irradiance", 3.14159265f);
  f.scene.insertChildObjectNode(root, light);
  auto camera = f.scene.createObject<vsr::scene::Camera>("perspective");
  camera->setParameter("position", vsr::math::float3(0, 0, 9));
  camera->setParameter("direction", vsr::math::float3(0, 0, -1));
  camera->setParameter("up", vsr::math::float3(0, 1, 0));
  camera->setParameter("fovy", 0.6f);
  auto *index =
      f.scene.updateDelegate().emplace<vsr::rendering::RenderIndexAllLayers>(
          f.scene, vsr::core::Token("image-qualification"), device.device);
  index->populate(); // Initial upload only; never rebuild/rebind to refresh.
  const auto world = index->world();
  const auto nativeCamera = index->camera(camera.index());
  MeshQualificationBackend backend;
  backend.d = device.device;
  backend.current = "default@" + name;
  backend.index = index;
  backend.cameraIndex = camera.index();
  backend.renderer =
      anari::newObject<anari::Renderer>(device.device, "default");
  REQUIRE(backend.renderer);
  anari::setParameter(device.device,
      backend.renderer,
      "background",
      vsr::math::float4(0, 0, 0, 1));
  anari::setParameter(device.device, backend.renderer, "pixelSamples", 8u);
  anari::commitParameters(device.device, backend.renderer);
  ViewRegistration registration;
  registration.name = "independent Image qualification";
  registration.scene = f.sceneId;
  registration.grants = {true, true};
  registration.role = ViewRole::HEADLESS;
  std::optional<ViewId> view;
  REQUIRE(f.integration.submit(f.sceneId, [&](vsr::scene::Scene &) {
    view = f.integration.registerView(registration);
  }));
  f.drain();
  REQUIRE(view);
  identity::ViewRevisionTracker tracker(f.ids, *f.sceneIds);
  tracker.setCamera(f.sceneIds->handleOf(camera.data()));
  sync::SyncStates states;
  sync::SyncService syncService(f.integration, *f.engine, states);
  render::RenderService renders(
      f.integration, *f.engine, syncService, *f.imageActivity);
  REQUIRE(renders.attachView(*view, *f.sceneIds, tracker, backend));
  renders.setDimensions(*view, 192, 96);
  f.inspector->setViewRevisions(
      [&](ViewId id) { return id == *view ? &tracker : nullptr; });
  f.server.reset();
  MeshQualificationHostScope hostScope(&f);
  mcp::Config cfg;
  cfg.imageExchange = f.images->metadata(f.sceneId);
  mcp::tools::addImageTools(cfg.tools, {f.ids, *f.images});
  mcp::tools::addQueryTools(cfg.tools, {*f.engine, *f.inspector});
  mcp::tools::addLookupTools(cfg.tools, *f.engine);
  mcp::tools::addEditTools(cfg.tools, {f.ids, *f.edits, *f.restores, nullptr});
  mcp::tools::addRenderTools(cfg.tools, {renders, syncService, *f.engine});
  REQUIRE(mcp::McpServer::start(cfg, *f.authority, *f.engine, f.dir, f.server)
          .ok());
  const auto session = f.attach();
  std::uint64_t request = 0;
  const auto pixels = [&](const std::string &phase) {
    CHECK(backend.d == device.device);
    CHECK(backend.index == index);
    CHECK(index->world() == world);
    CHECK(index->camera(camera.index()) == nativeCamera);
    CHECK(sampler->parameterValueAsObject<vsr::scene::Array>("image")
        == image.data());
    CHECK(sampler->parameter("filter")->value().getString() == "nearest");
    CHECK(material->parameterValueAsObject<vsr::scene::Sampler>("color")
        == sampler.data());
    CHECK(std::memcmp(positions->data(), points, sizeof(points)) == 0);
    const auto discoverId = ++request;
    f.tool(session,
        "vsr_discover",
        "{\"request_id\":" + std::to_string(discoverId) + "}");
    const auto discovery = f.await(session, discoverId);
    REQUIRE(discovery.str("outcome") == "ok");
    std::string viewRevision;
    for (const auto &item : discovery.result()->get("items")->a)
      if (item.get("kind")->s == "view" && item.get("id")->n == double(*view))
        viewRevision = item.get("view_revision")->s;
    REQUIRE_FALSE(viewRevision.empty());
    const auto inspectId = ++request;
    f.tool(session,
        "vsr_inspect",
        "{\"request_id\":" + std::to_string(inspectId)
            + ",\"scene_id\":" + std::to_string(f.sceneId) + "}");
    const auto inspection = f.await(session, inspectId);
    REQUIRE(inspection.str("outcome") == "ok");
    const auto sceneRevision = inspection.result()->get("scene_revision")->s;
    const auto id = ++request;
    REQUIRE(f.tool(session,
                 "vsr_render",
                 "{\"request_id\":" + std::to_string(id)
                     + ",\"view_id\":" + std::to_string(*view)
                     + ",\"scene_revision\":" + json::quote(sceneRevision)
                     + ",\"view_revision\":" + json::quote(viewRevision) + "}")
                .str("kind")
        == "render");
    Reply reply;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(120);
    do {
      f.drain();
      renders.pump();
      reply = f.tool(session,
          "vsr_render_image",
          "{\"operation_ref\":" + ref(session.id, id) + "}");
      REQUIRE(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (reply.str("state") != "terminal");
    REQUIRE(reply.str("outcome") == "ok");
    CHECK(reply.result()->get("scene_revision")->s == sceneRevision);
    CHECK(reply.result()->get("view_revision")->s == viewRevision);
    const auto *item = imageItem(reply);
    REQUIRE(item);
    const auto png = unbase64(item->get("data")->s);
    if (const char *evidence =
            std::getenv("VSR_IMAGE_QUALIFICATION_EVIDENCE")) {
      std::ofstream output(std::string(evidence) + "/independent-image-" + name
              + "-" + phase + ".png",
          std::ios::binary);
      output.write(reinterpret_cast<const char *>(png.data()),
          std::streamsize(png.size()));
      REQUIRE(output.good());
    }
    int w{}, h{}, channels{};
    auto *decoded = stbi_load_from_memory(
        png.data(), int(png.size()), &w, &h, &channels, 4);
    REQUIRE(decoded);
    REQUIRE(w == 192);
    REQUIRE(h == 96);
    std::vector<std::uint8_t> result(decoded, decoded + w * h * 4);
    stbi_image_free(decoded);
    return result;
  };
  const auto count =
      [](const std::vector<std::uint8_t> &p, int roi, int channel) {
        std::size_t n = 0;
        for (int y = 0; y < 96; ++y)
          for (int x = roi * 64; x < (roi + 1) * 64; ++x) {
            const auto at = (y * 192 + x) * 4;
            if (p[at + channel] > 40
                && p[at + channel] > p[at + (channel + 1) % 3] + 30
                && p[at + channel] > p[at + (channel + 2) % 3] + 30)
              ++n;
          }
        return n;
      };
  const auto difference = [](const std::vector<std::uint8_t> &a,
                              const std::vector<std::uint8_t> &b,
                              int roi) {
    std::size_t n = 0;
    for (int y = 0; y < 96; ++y)
      for (int x = roi * 64; x < (roi + 1) * 64; ++x) {
        const auto at = (y * 192 + x) * 4;
        bool changed = false;
        for (int c = 0; c < 3; ++c)
          changed |= std::abs(int(a[at + c]) - int(b[at + c])) > 12;
        n += changed;
      }
    return n;
  };
  const auto baseline = pixels("baseline");
  for (int roi = 0; roi < 3; ++roi)
    REQUIRE(count(baseline, roi, 1) > 160);
  std::ifstream maps("/proc/self/maps");
  std::string line;
  while (std::getline(maps, line))
    if (line.find("libanari") != std::string::npos
        || line.find("libbarney") != std::string::npos
        || line.find("libcuda") != std::string::npos
        || line.find("libnvoptix") != std::string::npos)
      std::fprintf(
          stdout, "loaded Image default@%s: %s\n", library, line.c_str());
  const auto handle = f.objectHandle(image.data());
  writeInput(f.exchangePath() + "/red.png",
      inputPng(1, 1, 6, {0, 255, 0, 0, 255}, {}));
  const auto importId = ++request;
  f.importImage(session,
      importId,
      handle,
      *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(image.data())),
      "red.png");
  const auto imported = f.await(session, importId);
  REQUIRE(imported.flag("committed"));
  REQUIRE(imported.str("outcome") == "ok");
  REQUIRE(std::memcmp(image->data(), red, sizeof(red)) == 0);
  const auto changed = pixels("imported");
  for (int roi = 0; roi < 3; ++roi) {
    std::fprintf(stdout,
        "Image default@%s roi=%d green=%zu -> %zu red=%zu committed=true\n",
        library,
        roi,
        count(baseline, roi, 1),
        count(changed, roi, 1),
        count(changed, roi, 0));
    if (roi < 2) {
      CHECK(count(changed, roi, 0) > 160);
      CHECK(count(changed, roi, 1) < 10);
      CHECK(difference(baseline, changed, roi) > 160);
    } else
      CHECK(difference(baseline, changed, roi) <= 61);
  }
  // Nonfatal pixel checks above intentionally allow truthful recovery evidence
  // even when a required backend renders a stale native texture.
  const auto restoreId = ++request;
  f.restoreBatch(session, restoreId, *imported.result()->get("batch_ref"));
  const auto restored = f.await(session, restoreId);
  REQUIRE(restored.flag("committed"));
  REQUIRE(restored.str("outcome") == "ok");
  REQUIRE(std::memcmp(image->data(), green, sizeof(green)) == 0);
  const auto recovered = pixels("restored");
  for (int roi = 0; roi < 3; ++roi)
    CHECK(difference(baseline, recovered, roi) <= 61);
  std::fprintf(stdout,
      "Image restore default@%s committed=true exact_cpu=true exact_pixels=%s\n",
      library,
      recovered == baseline ? "true" : "false");
}

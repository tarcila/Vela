// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

namespace {
struct HdriFixture : Fixture
{
  explicit HdriFixture(image::ExchangeLimits limits = {})
      : Fixture({},
            true,
            true,
            false,
            120.,
            true,
            UINT64_MAX,
            limits,
            true,
            {},
            true)
  {}
};
} // namespace

SCENARIO("MCP creates real RGBE HDRI and ordinary Restore removes it",
    "[integration][content][mcp][public-hdri-create]")
{
  const bool owned = GENERATE(false, true);
  HdriFixture f;
  auto parent = f.scene.insertChildTransformNode(f.scene.defaultLayer()->root(),
      vsr::math::IDENTITY_MAT4,
      "existing parent");
  auto positions = f.scene.createArray(ANARI_FLOAT32_VEC3, 3);
  const vsr::math::float3 points[] = {{-1, -1, 0}, {1, -1, 0}, {0, 1, 0}};
  positions->setData(points, 3);
  auto geometry = f.scene.createObject<vsr::scene::Geometry>("triangle");
  geometry->setParameterObject("vertex.position", *positions);
  auto material = f.scene.createObject<vsr::scene::Material>("matte");
  material->setParameter("color", vsr::math::float3(.5f));
  auto surface = f.scene.createSurface("fixed geometry", geometry, material);
  f.scene.insertChildObjectNode(parent, surface);
  auto existingLight = f.scene.createObject<vsr::scene::Light>("directional");
  existingLight->setName("preexisting lighting");
  existingLight->setParameter("direction", vsr::math::float3(0, 0, -1));
  existingLight->setParameter("irradiance", .01f);
  f.scene.insertChildObjectNode(parent, existingLight);
  const auto existingLightVersion = existingLight->lastParameterChange();
  auto camera = f.scene.createObject<vsr::scene::Camera>("perspective");
  camera->setParameter("position", vsr::math::float3(0, 0, 3));
  camera->setParameter("direction", vsr::math::float3(0, 0, -1));
  camera->setParameter("up", vsr::math::float3(0, 1, 0));
  const auto cameraVersion = camera->lastParameterChange();
  const char *profile = std::getenv("VSR_HDRI_CONTROL_LIBRARY");
  const std::string library = profile ? profile : "visrtx";
  REQUIRE((library == "visrtx" || library == "barney"));
  MeshQualificationDevice helide(library.c_str());
  REQUIRE(helide.device);
  const auto rendererSubtype = helide.lightingRenderer(library);
  REQUIRE(rendererSubtype);
  std::ifstream maps("/proc/self/maps");
  std::string map;
  while (std::getline(maps, map))
    if (map.find("libanari") != std::string::npos
        || map.find("libbarney") != std::string::npos
        || map.find("libnvoptix") != std::string::npos)
      std::fprintf(
          stderr, "HDRI loaded %s: %s\n", library.c_str(), map.c_str());
  const auto *lightTypes = anariGetObjectSubtypes(helide.device, ANARI_LIGHT);
  std::string advertisedLights;
  for (const char *const *type = lightTypes; type && *type; ++type)
    advertisedLights += std::string(*type) + " ";
  const auto probe = anariNewLight(helide.device, "hdri");
  std::fprintf(stderr,
      "HDRI GPU runtime Light subtypes: '%s'; newLight(hdri)=%s\n",
      advertisedLights.c_str(),
      probe ? "non-null" : "null");
  if (probe)
    anari::release(helide.device, probe);
  auto *index =
      f.scene.updateDelegate().emplace<vsr::rendering::RenderIndexAllLayers>(
          f.scene, vsr::core::Token("hdri-helide"), helide.device);
  index->populate();
  HelideBackend backend;
  backend.d = helide.device;
  backend.index = index;
  backend.cameraIndex = camera.index();
  backend.renderer =
      anari::newObject<anari::Renderer>(helide.device, rendererSubtype);
  anari::setParameter(helide.device, backend.renderer, "pixelSamples", 64u);
  anari::setParameter(helide.device,
      backend.renderer,
      "background",
      vsr::math::float4(0, 0, 0, 1));
  anari::setParameter(helide.device, backend.renderer, "ambientRadiance", 0.f);
  anari::commitParameters(helide.device, backend.renderer);
  const auto boundWorld = index->world();
  const auto boundCamera = index->camera(camera.index());
  ViewRegistration registration;
  registration.name = "independent HDRI control";
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
  renders.setDimensions(*view, 64, 48);
  f.inspector->setViewRevisions(
      [&](ViewId id) { return id == *view ? &tracker : nullptr; });
  f.server.reset();
  MeshQualificationHostScope hostScope(&f);
  mcp::Config cfg;
  mcp::tools::addContentTools(cfg.tools, f.ids, *f.creator, *f.engine);
  mcp::tools::addQueryTools(cfg.tools, {*f.engine, *f.inspector});
  mcp::tools::addLookupTools(cfg.tools, *f.engine);
  mcp::tools::addEditTools(cfg.tools, {f.ids, *f.edits, *f.restores, nullptr});
  mcp::tools::addRenderTools(cfg.tools, {renders, syncService, *f.engine});
  REQUIRE(mcp::McpServer::start(cfg, *f.authority, *f.engine, f.dir, f.server)
          .ok());
  const auto session = f.attach();
  std::uint64_t renderRequest = 0;
  const auto pixels = [&] {
    CHECK(backend.d == helide.device);
    CHECK(backend.index == index);
    CHECK(index->world() == boundWorld);
    CHECK(index->camera(camera.index()) == boundCamera);
    const auto discoverId = ++renderRequest;
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
    const auto inspectId = ++renderRequest;
    f.tool(session,
        "vsr_inspect",
        "{\"request_id\":" + std::to_string(inspectId)
            + ",\"scene_id\":" + std::to_string(f.sceneId) + "}");
    const auto inspection = f.await(session, inspectId);
    REQUIRE(inspection.str("outcome") == "ok");
    const auto sceneRevision = inspection.result()->get("scene_revision")->s;
    const auto id = ++renderRequest;
    f.tool(session,
        "vsr_render",
        "{\"request_id\":" + std::to_string(id)
            + ",\"view_id\":" + std::to_string(*view)
            + ",\"scene_revision\":" + json::quote(sceneRevision)
            + ",\"view_revision\":" + json::quote(viewRevision) + "}");
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
    const auto *item = imageItem(reply);
    REQUIRE(item);
    const auto png = unbase64(item->get("data")->s);
    if (const char *evidence = std::getenv("VSR_HDRI_CONTROL_EVIDENCE")) {
      std::ofstream output(std::string(evidence) + "/public-final-" + library
              + (owned ? "-owned-" : "-parent-") + std::to_string(id) + ".png",
          std::ios::binary);
      output.write(reinterpret_cast<const char *>(png.data()),
          std::streamsize(png.size()));
      REQUIRE(output.good());
    }
    int w = 0, h = 0, channels = 0;
    auto *decoded = stbi_load_from_memory(
        png.data(), int(png.size()), &w, &h, &channels, 4);
    REQUIRE(decoded);
    REQUIRE(w == 64);
    REQUIRE(h == 48);
    std::vector<std::uint8_t> result(decoded, decoded + w * h * 4);
    stbi_image_free(decoded);
    return result;
  };
  // Control this very geometry, not an inherited Mesh test.
  anari::setParameter(helide.device, backend.renderer, "ambientRadiance", 1.f);
  anari::commitParameters(helide.device, backend.renderer);
  const auto geometryControl = pixels();
  size_t visibleGeometry = 0;
  for (size_t y = 12; y < 36; ++y)
    for (size_t x = 24; x < 40; ++x)
      if (geometryControl[(y * 64 + x) * 4] > 20)
        ++visibleGeometry;
  REQUIRE(visibleGeometry > 30);
  anari::setParameter(helide.device, backend.renderer, "ambientRadiance", 0.f);
  anari::commitParameters(helide.device, backend.renderer);
  const auto before = pixels();
  {
    std::ofstream file(f.tmp.path + "/source.hdr", std::ios::binary);
    file << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n";
    const unsigned char pixel[] = {128, 64, 32, 132}; // 8,4,2 linear HDR
    file.write(reinterpret_cast<const char *>(pixel), sizeof(pixel));
  }
  const auto args =
      "{\"request_id\":7,\"scene_id\":" + std::to_string(f.sceneId)
      + ",\"asset_root\":0,\"file_name\":\"source.hdr\",\"root_name\":\"HDRI Root\",\"scale\":0.0625"
      + (owned ? ",\"new_layer\":\"owned HDRI\""
               : ",\"parent\":\""
                  + edit::detail::hex64(f.sceneIds->handleOf(parent).domain)
                  + edit::detail::hex64(f.sceneIds->handleOf(parent).serial)
                  + "\"")
      + "}";
  f.tool(session, "vsr_create_hdri", args);
  const auto created = f.await(session, 7);
  REQUIRE(created.str("outcome") == "ok");
  REQUIRE(created.flag("committed"));
  const auto *result = created.result();
  REQUIRE(result);
  REQUIRE(result->get("batch_ref"));
  REQUIRE(result->get("primary"));
  f.tool(session,
      "vsr_inspect",
      "{\"request_id\":8,\"scene_id\":" + std::to_string(f.sceneId)
          + ",\"object_type\":\"ANARI_LIGHT\"}");
  const auto inspected = f.await(session, 8);
  renderRequest = 8;
  REQUIRE(inspected.str("outcome") == "ok");
  REQUIRE(inspected.result()->get("items")->a.size() == 2);
  bool foundPrimary = false;
  for (const auto &item : inspected.result()->get("items")->a)
    foundPrimary |=
        item.get("handle")->s == result->get("primary")->get("light")->s;
  CHECK(foundPrimary);
  const auto lit = pixels();
  const auto center = (24 * 64 + 32) * 4;
  std::fprintf(stderr,
      "HDRI %s@%s scale=0.0625 center before=%u,%u,%u lit=%u,%u,%u\n",
      rendererSubtype,
      library.c_str(),
      before[center],
      before[center + 1],
      before[center + 2],
      lit[center],
      lit[center + 1],
      lit[center + 2]);
  size_t brighter = 0;
  for (size_t y = 12; y < 36; ++y)
    for (size_t x = 24; x < 40; ++x) {
      const auto p = (y * 64 + x) * 4;
      if (int(lit[p]) > int(before[p]) + 20 && lit[p] > lit[p + 2] + 10)
        ++brighter;
    }
  std::fprintf(stderr,
      "HDRI fixed-geometry control=%zu, HDRI brighter=%zu, baseline_equals_hdri=%s\n",
      visibleGeometry,
      brighter,
      before == lit ? "true" : "false");
  CHECK(brighter
      > 30); // Fixed geometry is meaningfully lit, not merely a new background.
  CHECK(camera->lastParameterChange() == cameraVersion);
  f.tool(session, "vsr_create_hdri", args);
  CHECK(f.await(session, 7).flag("committed"));
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 2);
  f.restoreBatch(session, 12, *result->get("batch_ref"));
  CHECK(f.await(session, 12).str("outcome") == "ok");
  renderRequest = 12;
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 1);
  CHECK(existingLight->lastParameterChange() == existingLightVersion);
  CHECK(*existingLight->parameterValueAs<float>("irradiance") == .01f);
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == 1);
  CHECK(f.scene.numberOfLayers() == 1);
  CHECK(pixels() == before);
  CHECK(camera->lastParameterChange() == cameraVersion);
  CHECK(
      geometry->parameterValueAsObject("vertex.position") == positions.data());
  anari::release(helide.device, backend.renderer);
  f.scene.updateDelegate().erase(index);
}

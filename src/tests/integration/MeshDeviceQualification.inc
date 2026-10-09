// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// Included beside the existing public loopback fixture; no alternate Import
// seam.
namespace {

struct MeshQualificationDevice
{
  explicit MeshQualificationDevice(const char *name);
  ~MeshQualificationDevice();
  const char *lightingRenderer(const std::string &name) const;
  VSR_NOT_COPYABLE(MeshQualificationDevice)
  VSR_NOT_MOVEABLE(MeshQualificationDevice)
  anari::Library library{nullptr};
  anari::Device device{nullptr};
};

MeshQualificationDevice::MeshQualificationDevice(const char *name)
{
  library = anari::loadLibrary(name,
      [](const void *,
          ANARIDevice,
          ANARIObject,
          anari::DataType,
          ANARIStatusSeverity severity,
          ANARIStatusCode,
          const char *message) {
        if (severity <= ANARI_SEVERITY_WARNING)
          std::fprintf(stderr, "ANARI qualification: %s\n", message);
      });
  if (library) {
    device = anari::newDevice(library, "default");
    if (device)
      anari::commitParameters(device, device);
  }
}

const char *MeshQualificationDevice::lightingRenderer(
    const std::string &name) const
{
  // Require an explicit known lighting profile, rather than assuming a
  // backend's default implements HDRI. No fallback to a diagnostic subtype.
  const char *wanted = name == "visrtx" ? "quality" : "default";
  const auto *types = anariGetObjectSubtypes(device, ANARI_RENDERER);
  for (auto p = types; p && *p; ++p) {
    std::fprintf(stderr, "HDRI renderer subtype: %s\n", *p);
    if (std::strcmp(*p, wanted) == 0)
      return wanted;
  }
  return nullptr;
}

MeshQualificationDevice::~MeshQualificationDevice()
{
  if (device)
    anari::release(device, device);
  if (library)
    anari::unloadLibrary(library);
}

struct MeshQualificationBackend : HelideBackend
{
  MeshQualificationBackend() = default;
  ~MeshQualificationBackend();
  VSR_NOT_COPYABLE(MeshQualificationBackend)
  VSR_NOT_MOVEABLE(MeshQualificationBackend)
};

MeshQualificationBackend::~MeshQualificationBackend()
{
  release();
  if (renderer)
    anari::release(d, renderer);
}

struct MeshQualificationHostScope
{
  explicit MeshQualificationHostScope(Fixture *fixture);
  ~MeshQualificationHostScope();
  VSR_NOT_COPYABLE(MeshQualificationHostScope)
  VSR_NOT_MOVEABLE(MeshQualificationHostScope)

 private:
  Fixture *m_fixture{nullptr};
};

MeshQualificationHostScope::MeshQualificationHostScope(Fixture *fixture)
    : m_fixture(fixture)
{}

MeshQualificationHostScope::~MeshQualificationHostScope()
{
  // Also stop dispatch before referenced services disappear on assertion
  // failure.
  m_fixture->server.reset();
  m_fixture->inspector->setViewRevisions({});
}

} // namespace

SCENARIO("Public mesh exchange refreshes a bound real ANARI device repeatedly",
    "[mcp][.mesh-device]")
{
  const char *library = std::getenv("VSR_MESH_QUALIFICATION_LIBRARY");
  REQUIRE(library != nullptr);
  const std::string name(library);
  REQUIRE((name == "helide" || name == "visrtx" || name == "barney"));
  INFO("Independent render, not a presented viewport: default@" << name);
  MeshQualificationDevice device(library);
  REQUIRE(device.device != nullptr); // Unavailable required devices FAIL.
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
  auto geometry = f.scene.createObject<vsr::scene::Geometry>("triangle");
  geometry->setName("private qualification square");
  geometry->setParameter("custom", 17.f);
  auto other = f.scene.createObject<vsr::scene::Geometry>("triangle");
  const float points[] = {-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0};
  const std::uint32_t triangles[] = {0, 1, 2, 0, 2, 3};
  const float uv[] = {0, 0, 1, 0, 1, 1, 0, 1};
  const float normals[] = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
  const float colors[] = {1, 0, 0, 0, 1, 0};
  auto positions = f.scene.createArray(ANARI_FLOAT32_VEC3, 4);
  auto indices = f.scene.createArray(ANARI_UINT32_VEC3, 2);
  auto coordinates = f.scene.createArray(ANARI_FLOAT32_VEC2, 4);
  auto normal = f.scene.createArray(ANARI_FLOAT32_VEC3, 4);
  auto color = f.scene.createArray(ANARI_FLOAT32_VEC3, 2);
  positions->setData(static_cast<const void *>(points));
  indices->setData(static_cast<const void *>(triangles));
  coordinates->setData(static_cast<const void *>(uv));
  normal->setData(static_cast<const void *>(normals));
  color->setData(static_cast<const void *>(colors));
  for (auto g : {geometry, other}) {
    g->setParameterObject("vertex.position", *positions);
    g->setParameterObject("primitive.index", *indices);
    g->setParameterObject("vertex.attribute0", *coordinates);
    g->setParameterObject("vertex.normal", *normal);
    g->setParameterObject("primitive.color", *color);
    g->setParameterObject("primitive.attribute1", *color);
    g->parameter("primitive.attribute1")->setEnabled(false);
  }
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 2, 1);
  const unsigned char texels[] = {255, 0, 0, 255, 0, 0, 255, 255};
  image->setData(static_cast<const void *>(texels));
  auto sampler = f.scene.createObject<vsr::scene::Sampler>("image2D");
  sampler->setParameterObject("image", *image);
  sampler->setParameter("inAttribute", "attribute0");
  sampler->setParameter("filter", "nearest");
  auto textured = f.scene.createObject<vsr::scene::Material>("matte");
  textured->setParameterObject("color", *sampler);
  auto attributed = f.scene.createObject<vsr::scene::Material>("matte");
  attributed->setParameter("color", "color");
  auto first =
      f.scene.createSurface("textured shared user", geometry, textured);
  auto second =
      f.scene.createSurface("primitive shared user", geometry, attributed);
  auto untouched =
      f.scene.createSurface("old shared Arrays", other, attributed);
  std::array<vsr::math::mat4, 3> matrices;
  std::array<vsr::scene::LayerNodeRef, 3> placements;
  auto root = f.scene.defaultLayer()->root();
  int i = 0;
  for (auto surface : {first, second, untouched}) {
    matrices[i] = vsr::math::IDENTITY_MAT4;
    matrices[i][3].x = float(i - 1) * 3.7f;
    placements[i] = f.scene.insertChildTransformNode(root, matrices[i]);
    f.scene.insertChildObjectNode(placements[i], surface);
    ++i;
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
          f.scene, vsr::core::Token("mesh-qualification"), device.device);
  index->populate(); // Only initial upload; no populate/rebuild after Import.
  const auto boundWorld = index->world();
  const auto boundCamera = index->camera(camera.index());
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

  // Expose the independent index through the same public Render tools.
  ViewRegistration registration;
  registration.name = "independent mesh qualification";
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
  const auto unchanged = [&] {
    CHECK(other->parameterValueAsObject<vsr::scene::Array>("vertex.position")
        == positions.data());
    CHECK(other->parameterValueAsObject<vsr::scene::Array>("primitive.index")
        == indices.data());
    CHECK(other->parameterValueAsObject<vsr::scene::Array>("vertex.attribute0")
        == coordinates.data());
    CHECK(other->parameterValueAsObject<vsr::scene::Array>("vertex.normal")
        == normal.data());
    CHECK(other->parameterValueAsObject<vsr::scene::Array>("primitive.color")
        == color.data());
    CHECK(
        other->parameterValueAsObject<vsr::scene::Array>("primitive.attribute1")
        == color.data());
    CHECK_FALSE(other->parameter("primitive.attribute1")->isEnabled());
    CHECK(std::memcmp(positions->data(), points, sizeof(points)) == 0);
    CHECK(std::memcmp(indices->data(), triangles, sizeof(triangles)) == 0);
    CHECK(std::memcmp(coordinates->data(), uv, sizeof(uv)) == 0);
    CHECK(std::memcmp(normal->data(), normals, sizeof(normals)) == 0);
    CHECK(std::memcmp(color->data(), colors, sizeof(colors)) == 0);
    CHECK(std::memcmp(image->data(), texels, sizeof(texels)) == 0);
    CHECK(sampler->parameterValueAsObject<vsr::scene::Array>("image")
        == image.data());
    CHECK(sampler->parameter("filter")->value().getString() == "nearest");
    CHECK(
        sampler->parameter("inAttribute")->value().getString() == "attribute0");
    CHECK(textured->parameterValueAsObject<vsr::scene::Sampler>("color")
        == sampler.data());
    CHECK(attributed->parameter("color")->value().getString() == "color");
    CHECK(first->parameterValueAsObject<vsr::scene::Geometry>("geometry")
        == geometry.data());
    CHECK(second->parameterValueAsObject<vsr::scene::Geometry>("geometry")
        == geometry.data());
    CHECK(untouched->parameterValueAsObject<vsr::scene::Geometry>("geometry")
        == other.data());
    CHECK(first->parameterValueAsObject<vsr::scene::Material>("material")
        == textured.data());
    CHECK(second->parameterValueAsObject<vsr::scene::Material>("material")
        == attributed.data());
    for (int placement = 0; placement < 3; ++placement)
      CHECK(
          placements[placement]->value().getTransform() == matrices[placement]);
    CHECK(geometry->name() == "private qualification square");
    CHECK(geometry->parameter("custom")->value().get<float>() == 17.f);
    CHECK(*camera->parameterValueAs<vsr::math::float3>("position")
        == vsr::math::float3(0, 0, 9));
    CHECK(*camera->parameterValueAs<vsr::math::float3>("direction")
        == vsr::math::float3(0, 0, -1));
    CHECK(*camera->parameterValueAs<vsr::math::float3>("up")
        == vsr::math::float3(0, 1, 0));
    CHECK(*camera->parameterValueAs<float>("fovy") == 0.6f);
    CHECK(backend.d == device.device);
    CHECK(backend.index == index);
    CHECK(index->world() == boundWorld);
    CHECK(index->camera(camera.index()) == boundCamera);
  };
  const auto pixels = [&] {
    unchanged();
    const auto discoverId = ++request;
    REQUIRE(f.tool(session,
                 "vsr_discover",
                 "{\"request_id\":" + std::to_string(discoverId) + "}")
                .str("kind")
        == "discover");
    const auto discovery = f.await(session, discoverId);
    REQUIRE(discovery.str("outcome") == "ok");
    std::string viewRevision;
    for (const auto &item : discovery.result()->get("items")->a)
      if (item.get("kind")->s == "view" && item.get("id")->n == double(*view))
        viewRevision = item.get("view_revision")->s;
    REQUIRE_FALSE(viewRevision.empty());
    const auto inspectId = ++request;
    REQUIRE(f.tool(session,
                 "vsr_inspect",
                 "{\"request_id\":" + std::to_string(inspectId)
                     + ",\"scene_id\":" + std::to_string(f.sceneId) + "}")
                .str("kind")
        == "inspect");
    const auto inspection = f.await(session, inspectId);
    REQUIRE(inspection.str("outcome") == "ok");
    const auto sceneRevision = inspection.result()->get("scene_revision")->s;
    const auto id = ++request;
    const auto args = "{\"request_id\":" + std::to_string(id)
        + ",\"view_id\":" + std::to_string(*view)
        + ",\"scene_revision\":" + json::quote(sceneRevision)
        + ",\"view_revision\":" + json::quote(viewRevision) + "}";
    REQUIRE(f.tool(session, "vsr_render", args).str("kind") == "render");
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
    if (const char *evidence = std::getenv("VSR_MESH_QUALIFICATION_EVIDENCE")) {
      std::ofstream output(std::string(evidence) + "/independent-" + name
              + "-request-" + std::to_string(id) + ".png",
          std::ios::binary);
      output.write(reinterpret_cast<const char *>(png.data()),
          std::streamsize(png.size()));
      REQUIRE(output.good());
    }
    int w = 0, h = 0, channels = 0;
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
    std::size_t different = 0;
    for (int y = 0; y < 96; ++y)
      for (int x = roi * 64; x < (roi + 1) * 64; ++x) {
        const auto at = (y * 192 + x) * 4;
        bool changed = false;
        for (int c = 0; c < 3; ++c)
          changed |= std::abs(int(a[at + c]) - int(b[at + c])) > 12;
        different += changed;
      }
    return different;
  };
  const auto close = [&](const std::vector<std::uint8_t> &a,
                         const std::vector<std::uint8_t> &b,
                         int roi) {
    // <=1% of ROI; >12/255 per-channel rejects real appearance changes while
    // allowing the small stochastic edge differences of the Barney renderer.
    return difference(a, b, roi) <= 61;
  };
  const auto before = pixels();
  const auto repeated = pixels();
  for (int roi = 0; roi < 3; ++roi)
    REQUIRE(close(before, repeated, roi));
  for (int roi = 0; roi < 3; ++roi) {
    REQUIRE(count(before, roi, 0) > 80);
    REQUIRE(count(before, roi, roi == 0 ? 2 : 1) > 80);
  }
  // Observe actual loaded objects, not library search guesses.
  std::ifstream maps("/proc/self/maps");
  std::string line;
  while (std::getline(maps, line))
    if (line.find("libanari") != std::string::npos
        || line.find("libbarney") != std::string::npos
        || line.find("libcuda") != std::string::npos
        || line.find("libnvoptix") != std::string::npos)
      std::fprintf(stdout, "loaded default@%s: %s\n", library, line.c_str());

  for (int round = 0; round < 3; ++round) {
    INFO("Incremental update/normal restore round " << round);
    const auto exportId = ++request;
    REQUIRE(f.exportMesh(session, exportId, geometry.data()).str("kind")
        == "mesh_export");
    const auto exported = f.await(session, exportId);
    REQUIRE(exported.str("outcome") == "ok");
    const auto file = "qualification-" + std::to_string(round) + ".vmesh";
    const char *installedExamples = std::getenv("VSR_MESH_EXAMPLE_PATH");
    const std::string examples = installedExamples
        ? installedExamples
        : std::string(VSR_MESH_TEST_SOURCE) + "/scripts";
    const auto command = std::string(VSR_MESH_TEST_PYTHON) + " '" + examples
        + "/mesh-device-edit.py' '" + f.exchangePath() + "/"
        + exported.result()->get("file_name")->s + "' '" + f.exchangePath()
        + "/" + file + "' " + std::to_string(round);
    REQUIRE(std::system(command.c_str()) == 0);
    const auto importId = ++request;
    const auto args = "{\"request_id\":" + std::to_string(importId)
        + ",\"scene_id\":" + std::to_string(f.sceneId) + ",\"handle\":"
        + json::quote(f.objectHandle(geometry.data())) + ",\"target_version\":"
        + json::write(*exported.result()->get("target_version"))
        + ",\"dependencies\":"
        + json::write(*exported.result()->get("dependencies"))
        + ",\"file_name\":" + json::quote(file) + "}";
    REQUIRE(
        f.tool(session, "vsr_mesh_import", args).str("kind") == "mesh_import");
    const auto imported = f.await(session, importId);
    REQUIRE(imported.flag("committed"));
    REQUIRE(imported.str("outcome") == "ok");
    auto *replacement =
        geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position");
    REQUIRE(replacement);
    REQUIRE(replacement != positions.data());
    CHECK(replacement->size() == (round == 1 ? 5 : 9));
    CHECK(geometry->parameterValueAsObject<vsr::scene::Array>("primitive.index")
              ->size()
        == (round == 1 ? 2 : 8));
    if (round == 1) {
      CHECK(
          geometry->parameterValueAsObject<vsr::scene::Array>("primitive.color")
          == color.data());
      CHECK(geometry->parameterValueAsObject<vsr::scene::Array>(
                "primitive.attribute1")
          == color.data());
    }
    CHECK(geometry->parameterValueAsObject<vsr::scene::Array>("primitive.color")
        == geometry->parameterValueAsObject<vsr::scene::Array>(
            "primitive.attribute1"));
    CHECK_FALSE(geometry->parameter("primitive.attribute1")->isEnabled());
    CHECK(replacement->elementType() == ANARI_FLOAT32_VEC3);
    const auto scale = round == 0 ? 0.375f : round == 1 ? 0.5f : 0.625f;
    const auto *newPoints = static_cast<const float *>(replacement->data());
    for (int component = 0; component < 12; ++component)
      CHECK(newPoints[component] == points[component] * scale);
    const auto *newUV = geometry->parameterValueAsObject<vsr::scene::Array>(
        "vertex.attribute0");
    const auto *newNormals =
        geometry->parameterValueAsObject<vsr::scene::Array>("vertex.normal");
    REQUIRE(newUV);
    REQUIRE(newNormals);
    REQUIRE(newUV->size() == replacement->size());
    REQUIRE(newNormals->size() == replacement->size());
    CHECK(newUV->elementType() == ANARI_FLOAT32_VEC2);
    CHECK(newNormals->elementType() == ANARI_FLOAT32_VEC3);
    CHECK(std::memcmp(newUV->data(), uv, sizeof(uv)) == 0);
    const auto *normalValues = static_cast<const float *>(newNormals->data());
    for (std::size_t vertex = 0; vertex < newNormals->size(); ++vertex) {
      CHECK(normalValues[3 * vertex] == 0.f);
      CHECK(normalValues[3 * vertex + 1] == 0.f);
      CHECK(normalValues[3 * vertex + 2] == 1.f);
    }
    const auto changed = pixels();
    for (int roi = 0; roi < 2; ++roi) {
      for (int channel : {0, roi == 0 ? 2 : 1}) {
        CHECK(count(changed, roi, channel) > 10);
        CHECK(count(changed, roi, channel) < count(before, roi, channel) / 2);
      }
      CHECK_FALSE(close(before, changed, roi));
    }
    CHECK(close(before, changed, 2));
    std::fprintf(stdout,
        "pixels default@%s round=%d independent textured(red/blue)=%zu/%zu -> %zu/%zu attributed(red/green)=%zu/%zu -> %zu/%zu\n",
        library,
        round,
        count(before, 0, 0),
        count(before, 0, 2),
        count(changed, 0, 0),
        count(changed, 0, 2),
        count(before, 1, 0),
        count(before, 1, 1),
        count(changed, 1, 0),
        count(changed, 1, 1));
    const auto restoreId = ++request;
    REQUIRE(
        f.restoreBatch(session, restoreId, *imported.result()->get("batch_ref"))
            .str("kind")
        == "restore");
    const auto restored = f.await(session, restoreId);
    REQUIRE(restored.flag("committed"));
    REQUIRE(restored.str("outcome") == "ok");
    const auto recovered = pixels();
    for (int roi = 0; roi < 3; ++roi)
      CHECK(close(before, recovered, roi));
    std::fprintf(stdout,
        "restore default@%s round=%d independent exact_pixels=%s delta_gt12_by_roi=%zu/%zu/%zu\n",
        library,
        round,
        recovered == before ? "true" : "false",
        difference(before, recovered, 0),
        difference(before, recovered, 1),
        difference(before, recovered, 2));
    CHECK(geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
        == positions.data());
    CHECK(geometry->parameterValueAsObject<vsr::scene::Array>("primitive.index")
        == indices.data());
    CHECK(
        geometry->parameterValueAsObject<vsr::scene::Array>("vertex.attribute0")
        == coordinates.data());
    CHECK(geometry->parameterValueAsObject<vsr::scene::Array>("vertex.normal")
        == normal.data());
    CHECK(geometry->parameterValueAsObject<vsr::scene::Array>("primitive.color")
        == color.data());
    CHECK(geometry->parameterValueAsObject<vsr::scene::Array>(
              "primitive.attribute1")
        == color.data());
    CHECK_FALSE(geometry->parameter("primitive.attribute1")->isEnabled());
    unchanged();
  }
  // The host scope stops dispatch; the index/device outlive service teardown.
}

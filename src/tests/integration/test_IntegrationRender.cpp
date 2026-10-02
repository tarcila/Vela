// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/render/RevisionRender.hpp"
#include "vsr/rendering/index/RenderIndexAllLayers.hpp"
#include "vsr/scene/Scene.hpp"
// Independent PNG decoder for the oracle, private to this TU.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../../../external/vela_stb_image/stb_image.h"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <cmath>
#include <memory>
#include <vector>

using namespace vsr::integration;
using namespace vsr::integration::render;
using operations::Outcome;
namespace tokens = vsr::scene::tokens;

namespace {

// A real helide device. Absence is a test FAILURE: skipped rendering is not
// acceptance evidence (spec §14).
struct Helide
{
  Helide()
  {
    library = anari::loadLibrary("helide",
        [](const void *,
            ANARIDevice,
            ANARIObject,
            anari::DataType,
            ANARIStatusSeverity,
            ANARIStatusCode,
            const char *) {});
    if (library)
      device = anari::newDevice(library, "default");
  }
  ~Helide()
  {
    if (device)
      anari::release(device, device);
    if (library)
      anari::unloadLibrary(library);
  }
  anari::Library library{nullptr};
  anari::Device device{nullptr};
};

// Spec §8 fixture: sphere above a ground plane, matte materials, one
// directional light, fixed camera.
void buildFixture(vsr::scene::Scene &s,
    vsr::scene::MaterialRef &sphereMat,
    vsr::scene::CameraRef &camera)
{
  using namespace vsr::scene;
  auto root = s.defaultLayer()->root();

  auto sphere = s.createObject<Geometry>(tokens::geometry::sphere);
  auto pos = s.createArray(ANARI_FLOAT32_VEC3, 1);
  const vsr::math::float3 c(0.f, 1.f, 0.f);
  pos->setData(&c, 1);
  sphere->setParameterObject("vertex.position", *pos);
  sphere->setParameter("radius", 1.f);
  sphereMat = s.createObject<Material>(tokens::material::matte);
  sphereMat->setParameter("color", vsr::math::float3(0.9f, 0.1f, 0.1f));
  s.insertChildObjectNode(root, s.createSurface("sphere", sphere, sphereMat));

  auto ground = s.createObject<Geometry>(tokens::geometry::triangle);
  auto verts = s.createArray(ANARI_FLOAT32_VEC3, 4);
  const vsr::math::float3 v[4] = {
      {-10.f, 0.f, -10.f}, {10.f, 0.f, -10.f}, {10.f, 0.f, 10.f}, {-10.f, 0.f, 10.f}};
  verts->setData(v, 4);
  auto idx = s.createArray(ANARI_UINT32_VEC3, 2);
  const vsr::math::uint3 tri[2] = {{0, 1, 2}, {0, 2, 3}};
  idx->setData(tri, 2);
  ground->setParameterObject("vertex.position", *verts);
  ground->setParameterObject("primitive.index", *idx);
  auto groundMat = s.createObject<Material>(tokens::material::matte);
  groundMat->setParameter("color", vsr::math::float3(0.2f, 0.8f, 0.2f));
  s.insertChildObjectNode(root, s.createSurface("ground", ground, groundMat));

  auto light = s.createObject<Light>(tokens::light::directional);
  light->setParameter("direction", vsr::math::float3(-0.3f, -1.f, -0.5f));
  s.insertChildObjectNode(root, light);

  camera = s.createObject<Camera>(tokens::camera::perspective);
  camera->setParameter("position", vsr::math::float3(0.f, 2.f, 6.f));
  camera->setParameter("direction", vsr::math::float3(0.f, -0.2f, -1.f));
  camera->setParameter("up", vsr::math::float3(0.f, 1.f, 0.f));
}

// Real helide rendering of the view. Tests may hold the frame pending or
// refuse release to model non-interruptible backend resources.
struct HelideBackend : RenderBackend
{
  anari::Device d{nullptr};
  vsr::rendering::RenderIndexAllLayers *index{nullptr};
  size_t cameraIndex{0};
  anari::Renderer renderer{nullptr};
  anari::Frame frame{nullptr};
  bool mapped{false};
  int holdPolls{0}; // report PENDING this many times after completion
  bool allowRelease{true};
  int begins{0};

  bool begin(std::uint32_t w, std::uint32_t h) override
  {
    ++begins;
    frame = anari::newObject<anari::Frame>(d);
    anari::setParameter(d, frame, "size", vsr::math::uint2(w, h));
    anari::setParameter(
        d, frame, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
    anari::setParameter(d, frame, "world", index->world());
    anari::setParameter(d, frame, "renderer", renderer);
    auto cam = index->camera(cameraIndex);
    anari::setParameter(d, cam, "aspect", float(w) / float(h));
    anari::commitParameters(d, cam);
    anari::setParameter(d, frame, "camera", cam);
    anari::commitParameters(d, frame);
    anari::render(d, frame);
    return true;
  }
  BackendState poll() override
  {
    if (!anari::isReady(d, frame))
      return BackendState::PENDING;
    if (holdPolls > 0) {
      --holdPolls;
      return BackendState::PENDING;
    }
    return BackendState::DONE;
  }
  image::SourceImage image() override
  {
    auto fb = anari::map<std::uint32_t>(d, frame, "channel.color");
    mapped = true;
    image::SourceImage src;
    src.width = fb.width;
    src.height = fb.height;
    src.encoding = image::ChannelEncoding::SRGB_UINT8;
    src.alpha = image::AlphaMode::STRAIGHT;
    src.origin = image::RowOrigin::BOTTOM_LEFT;
    src.pixels = fb.data;
    return src;
  }
  bool release() override
  {
    if (!allowRelease)
      return false;
    if (frame) {
      anari::wait(d, frame);
      if (mapped)
        anari::unmap(d, frame, "channel.color");
      anari::release(d, frame);
    }
    frame = nullptr;
    mapped = false;
    return true;
  }
};

struct Decoded
{
  int w{0}, h{0};
  std::vector<std::uint8_t> rgba;
};

Decoded decode(const std::vector<std::uint8_t> &png)
{
  Decoded d;
  int ch = 0;
  unsigned char *p = stbi_load_from_memory(
      png.data(), int(png.size()), &d.w, &d.h, &ch, 4);
  REQUIRE(p != nullptr);
  d.rgba.assign(p, p + std::size_t(d.w) * d.h * 4);
  stbi_image_free(p);
  return d;
}

struct Fixture
{
  Helide helide;
  vsr::scene::Scene scene;
  vsr::scene::MaterialRef sphereMat;
  vsr::scene::CameraRef camera;
  Integration integration;
  SceneId sceneId{INVALID_SCENE_ID};
  ViewId viewId{INVALID_VIEW_ID};
  double now{1000.0};
  std::unique_ptr<session::SessionAuthority> authority;
  std::unique_ptr<operations::OperationEngine> engine;
  identity::IdentityAuthority ids;
  std::unique_ptr<identity::SceneIdentity> identity;
  std::unique_ptr<identity::ViewRevisionTracker> tracker;
  sync::SyncStates states;
  std::unique_ptr<sync::SyncService> sync;
  activity::ActivityLog log;
  HelideBackend backend;
  std::unique_ptr<RenderService> renders;
  operations::Credentials creds;
  std::string sessionId;

  Fixture()
  {
    REQUIRE(helide.device != nullptr); // missing helide fails, never skips
    buildFixture(scene, sphereMat, camera);

    SceneRegistration sr;
    sr.name = "fixture";
    sr.scene = &scene;
    sr.grants.read = true;
    sceneId = *integration.registerScene(sr);
    ViewRegistration vr;
    vr.name = "main";
    vr.scene = sceneId;
    vr.grants = {true, true};
    viewId = *integration.registerView(vr);
    REQUIRE(integration.activate().ok());

    authority =
        session::SessionAuthority::create(integration, [this] { return now; });
    REQUIRE(authority);
    engine = operations::OperationEngine::create(
        integration, *authority, {}, [this] { return now; });
    REQUIRE(engine);
    identity = std::make_unique<identity::SceneIdentity>(ids, scene);
    tracker = std::make_unique<identity::ViewRevisionTracker>(ids, *identity);
    tracker->setCamera(identity->handleOf(camera.data()));
    sync = std::make_unique<sync::SyncService>(integration, *engine, states);

    auto *index =
        scene.updateDelegate().emplace<vsr::rendering::RenderIndexAllLayers>(
            scene, vsr::core::Token("helide"), helide.device);
    index->populate();
    backend.d = helide.device;
    backend.index = index;
    backend.cameraIndex = camera->index();
    backend.renderer =
        anari::newObject<anari::Renderer>(helide.device, "default");
    anari::setParameter(helide.device,
        backend.renderer,
        "background",
        vsr::math::float4(0.f, 0.f, 0.f, 1.f));
    anari::commitParameters(helide.device, backend.renderer);

    renders = std::make_unique<RenderService>(integration, *engine, *sync, log);
    REQUIRE(renders->attachView(viewId, *identity, *tracker, backend));
    renders->setDimensions(viewId, 64, 48);

    auto at = authority->attach(authority->launchCredential());
    REQUIRE(at.code == session::Code::OK);
    sessionId = at.sessionId;
    creds = {authority->launchCredential(), at.sessionId, at.sessionSecret};
  }

  ~Fixture()
  {
    backend.allowRelease = true;
    backend.release();
    renders.reset();
    if (backend.renderer)
      anari::release(helide.device, backend.renderer);
    sync.reset();
    tracker.reset();
    identity.reset();
    scene.updateDelegate().clear();
  }

  RenderRequest current()
  {
    return {viewId, identity->sceneRevision(), tracker->revision()};
  }

  operations::OperationRef ref(std::uint64_t id)
  {
    return {sessionId, id};
  }

  // Services safe points until the operation is terminal (bounded).
  operations::Status run(std::uint64_t id)
  {
    operations::Status st;
    for (int i = 0; i < 100000; ++i) {
      integration.service();
      renders->pump();
      st = engine->status(creds, ref(id));
      if (st.terminal && !st.progress)
        break;
    }
    return st;
  }

  // Services until the render reaches the backend (running, not terminal).
  void startRender(std::uint64_t id)
  {
    for (int i = 0; i < 1000 && backend.begins == 0; ++i)
      integration.service();
    REQUIRE(backend.begins > 0);
  }
};

} // namespace

TEST_CASE("A matching render returns a PNG of the view's exact size",
    "[integration][render]")
{
  Fixture f;
  auto req = f.current();
  REQUIRE(f.renders->submit(f.creds, f.ref(1), req).code == Outcome::OK);
  auto st = f.run(1);
  REQUIRE(st.terminal == Outcome::OK);
  REQUIRE(f.engine->slotsInUse() == 0);

  auto r = *f.renders->result(f.ref(1));
  REQUIRE(r.outcome == Outcome::OK);
  CHECK(r.width == 64);
  CHECK(r.height == 48);
  CHECK(r.sceneRevision == req.sceneRevision);
  CHECK(r.viewRevision == req.viewRevision);
  auto img = decode(r.png);
  REQUIRE(img.w == 64);
  REQUIRE(img.h == 48);

  // Tolerant content checks: the red sphere is near the image centre, the
  // green ground below it, and the top edge is the black background.
  auto px = [&](int x, int y) { return &img.rgba[(std::size_t(y) * 64 + x) * 4]; };
  int red = 0, green = 0;
  for (int y = 0; y < 48; ++y)
    for (int x = 0; x < 64; ++x) {
      auto *p = px(x, y);
      if (p[0] > p[1] + 30 && p[0] > p[2] + 30)
        ++red;
      if (p[1] > p[0] + 30 && p[1] > p[2] + 30)
        ++green;
    }
  CHECK(red > 50);
  CHECK(green > 50);
  auto *centre = px(32, 20);
  CHECK(int(centre[0]) > int(centre[1]) + 30);
  auto *bottom = px(32, 46);
  CHECK(int(bottom[1]) > int(bottom[0]));
  auto *top = px(1, 0);
  CHECK(top[0] < 20);
  CHECK(top[1] < 20);
  CHECK(top[3] > 200);

  SECTION("stored through the activity model's image budget")
  {
    CHECK(!r.imageEvicted);
    CHECK(f.log.usage().imageBytes == r.png.size());
  }

  SECTION("replay is not a fresh capture")
  {
    const int begins = f.backend.begins;
    auto a = f.renders->submit(f.creds, f.ref(1), req);
    REQUIRE(a.code == Outcome::OK);
    CHECK(a.replayed);
    f.integration.service();
    CHECK(f.backend.begins == begins);
    auto again = *f.renders->result(f.ref(1));
    CHECK(again.capture == r.capture);
    CHECK(again.png == r.png);

    REQUIRE(f.renders->submit(f.creds, f.ref(2), req).code == Outcome::OK);
    REQUIRE(f.run(2).terminal == Outcome::OK);
    CHECK(f.backend.begins == begins + 1);
    CHECK(f.renders->result(f.ref(2))->capture != r.capture);
  }
}

TEST_CASE("Mismatched tokens supersede without rendering",
    "[integration][render]")
{
  Fixture f;
  auto req = f.current();

  SECTION("scene change before the safe point")
  {
    REQUIRE(f.renders->submit(f.creds, f.ref(1), req).code == Outcome::OK);
    f.sphereMat->setParameter("color", vsr::math::float3(0.1f, 0.1f, 0.9f));
    CHECK(f.run(1).terminal == Outcome::SUPERSEDED);
  }
  SECTION("resize invalidates the old view token")
  {
    f.renders->setDimensions(f.viewId, 32, 32);
    REQUIRE(f.renders->submit(f.creds, f.ref(1), req).code == Outcome::OK);
    CHECK(f.run(1).terminal == Outcome::SUPERSEDED);
  }
  SECTION("camera change invalidates the old view token")
  {
    f.camera->setParameter("position", vsr::math::float3(0.f, 3.f, 6.f));
    REQUIRE(f.renders->submit(f.creds, f.ref(1), req).code == Outcome::OK);
    CHECK(f.run(1).terminal == Outcome::SUPERSEDED);
  }
  CHECK(f.backend.begins == 0);
  CHECK(f.renders->result(f.ref(1))->png.empty());
}

TEST_CASE("A relevant change mid-render prevents success",
    "[integration][render]")
{
  Fixture f;
  f.backend.holdPolls = 1;
  REQUIRE(f.renders->submit(f.creds, f.ref(1), f.current()).code
      == Outcome::OK);
  f.startRender(1);
  f.sphereMat->setParameter("color", vsr::math::float3(0.1f, 0.1f, 0.9f));
  auto st = f.run(1);
  CHECK(st.terminal == Outcome::SUPERSEDED);
  CHECK(f.renders->result(f.ref(1))->png.empty());
  CHECK(f.engine->slotsInUse() == 0);
}

TEST_CASE("View-owned dimensions are validated before backend work",
    "[integration][render]")
{
  CHECK(validDimensions(1, 1));
  CHECK(validDimensions(1024, 1024));
  CHECK(!validDimensions(0, 10));
  CHECK(!validDimensions(10, 0));
  CHECK(!validDimensions(1025, 10));
  CHECK(!validDimensions(10, 1025));

  Fixture f;
  for (auto [w, h] : {std::pair<std::uint32_t, std::uint32_t>{0, 48},
           {1025, 48},
           {64, 2048}}) {
    f.renders->setDimensions(f.viewId, w, h);
    auto a = f.renders->submit(f.creds, f.ref(1), f.current());
    CHECK(a.code == Outcome::INVALID_REQUEST);
    CHECK(f.engine->slotsInUse() == 0);
    CHECK(f.engine->highWater(f.sessionId) == 0);
  }
  CHECK(f.backend.begins == 0);

  // No clamping: a valid 1x1 view renders exactly 1x1.
  f.renders->setDimensions(f.viewId, 1, 1);
  REQUIRE(f.renders->submit(f.creds, f.ref(1), f.current()).code
      == Outcome::OK);
  REQUIRE(f.run(1).terminal == Outcome::OK);
  auto img = decode(f.renders->result(f.ref(1))->png);
  CHECK(img.w == 1);
  CHECK(img.h == 1);
}

TEST_CASE("A scene that is not ready returns sync_required with no image",
    "[integration][render]")
{
  Fixture f;
  SECTION("at start")
  {
    f.states.markNeedsSync(f.sceneId);
    REQUIRE(f.renders->submit(f.creds, f.ref(1), f.current()).code
        == Outcome::OK);
    CHECK(f.run(1).terminal == Outcome::SYNC_REQUIRED);
    CHECK(f.backend.begins == 0);
  }
  SECTION("at completion")
  {
    f.backend.holdPolls = 1;
    REQUIRE(f.renders->submit(f.creds, f.ref(1), f.current()).code
        == Outcome::OK);
    f.startRender(1);
    f.states.markNeedsSync(f.sceneId);
    CHECK(f.run(1).terminal == Outcome::SYNC_REQUIRED);
  }
  CHECK(f.renders->result(f.ref(1))->png.empty());
  CHECK(f.log.usage().imageBytes == 0);
}

TEST_CASE("The 30-second deadline runs from admission",
    "[integration][render]")
{
  Fixture f;
  REQUIRE(f.renders->submit(f.creds, f.ref(1), f.current()).code
      == Outcome::OK);
  f.now += 30.0; // queue wait consumes the deadline
  CHECK(f.run(1).terminal == Outcome::TIMEOUT);
  CHECK(f.backend.begins == 0); // expired queued work never starts
  CHECK(f.engine->slotsInUse() == 0);

  REQUIRE(f.renders->submit(f.creds, f.ref(2), f.current()).code
      == Outcome::OK);
  f.now += 29.0;
  CHECK(f.run(2).terminal == Outcome::OK);
}

TEST_CASE("Cancelled or timed-out renders never succeed from a late image",
    "[integration][render]")
{
  Fixture f;
  f.backend.holdPolls = 3;
  f.backend.allowRelease = false;
  REQUIRE(f.renders->submit(f.creds, f.ref(1), f.current()).code
      == Outcome::OK);
  f.startRender(1);

  SECTION("cancelled")
  {
    f.engine->cancel(f.creds, f.ref(1));
  }
  SECTION("timed out")
  {
    f.now += 31.0;
  }

  for (int i = 0; i < 20; ++i) {
    f.integration.service();
    f.renders->pump();
  }
  auto st = f.engine->status(f.creds, f.ref(1));
  REQUIRE(st.terminal);
  CHECK(*st.terminal != Outcome::OK);
  CHECK(st.progress == operations::Progress::AWAITING_CLEANUP);
  CHECK(st.heldSlots == 1);
  CHECK(f.engine->slotsInUse() == 1);
  CHECK(!st.backendFault);
  CHECK(f.backend.frame != nullptr); // resources retained, never forced

  // Necessary view resources are held: busy before admission.
  auto busy = f.renders->submit(f.creds, f.ref(2), f.current());
  CHECK(busy.code == Outcome::BUSY);
  CHECK(f.engine->highWater(f.sessionId) == 1);

  // Safe release later: slot returns, still no success, no image.
  f.backend.allowRelease = true;
  f.renders->pump();
  f.integration.service();
  st = f.engine->status(f.creds, f.ref(1));
  CHECK(!st.progress);
  CHECK(*st.terminal != Outcome::OK);
  CHECK(f.engine->slotsInUse() == 0);
  auto r = f.renders->result(f.ref(1));
  CHECK((!r || r->png.empty()));
  CHECK(f.log.usage().imageBytes == 0);

  REQUIRE(f.renders->submit(f.creds, f.ref(3), f.current()).code
      == Outcome::OK);
  CHECK(f.run(3).terminal == Outcome::OK);
}

TEST_CASE("A success held by a delayed release re-checks the guards",
    "[integration][render]")
{
  Fixture f;
  f.backend.allowRelease = false; // image produced, release refused
  REQUIRE(f.renders->submit(f.creds, f.ref(1), f.current()).code
      == Outcome::OK);
  for (int i = 0; i < 100000 && !f.backend.mapped; ++i) {
    f.integration.service();
    f.renders->pump();
  }
  REQUIRE(f.backend.mapped); // completion guards passed; OK is pending
  auto st = f.engine->status(f.creds, f.ref(1));
  REQUIRE(!st.terminal);

  Outcome expected = Outcome::OK;
  SECTION("view changed while the release was held")
  {
    f.camera->setParameter("position", vsr::math::float3(0.f, 3.f, 6.f));
    expected = Outcome::SUPERSEDED;
  }
  SECTION("scene changed while the release was held")
  {
    f.sphereMat->setParameter("color", vsr::math::float3(0.1f, 0.1f, 0.9f));
    expected = Outcome::SUPERSEDED;
  }
  SECTION("readiness lost while the release was held")
  {
    f.states.markNeedsSync(f.sceneId);
    expected = Outcome::SYNC_REQUIRED;
  }

  f.backend.allowRelease = true;
  st = f.run(1);
  REQUIRE(st.terminal);
  CHECK(*st.terminal == expected);
  CHECK(f.engine->slotsInUse() == 0);
  auto r = f.renders->result(f.ref(1));
  REQUIRE(r);
  CHECK(r->outcome == expected);
  CHECK(r->png.empty());
  CHECK(f.log.usage().imageBytes == 0);
}

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#ifdef VSR_TEST_VIEWER_UI
// vsr_ui_imgui
#include "vsr/ui/imgui/Application.h"
#include "vsr/ui/imgui/windows/Viewport.h"
// vsr_integration
#include "vsr/integration/frame/ViewerFrameSource.hpp"
#include "vsr/integration/mcp/Json.hpp"
#include "vsr/integration/render/ControllerCamera.hpp"
// vsr_io
#include "vsr/io/procedural.hpp"
// catch
#include "catch.hpp"
// std
#include <cstdlib>
#include <iostream>

namespace {
namespace vi = vsr::integration;

template <typename Revision>
std::string revisionToken(const Revision &r)
{
  return std::to_string(r.launch.activation) + "."
      + std::to_string(r.launch.nonce) + "." + std::to_string(r.domain) + "."
      + std::to_string(r.value);
}

struct FrameCameraApplication : vsr::ui::imgui::Application
{
  FrameCameraApplication();
  vsr::ui::imgui::WindowArray setupWindows() override;
  void uiFrameStart() override;
  void uiRenderEnd() override;
  void teardown() override;
  const char *applicationIdentifier() const override;
  const char *getDefaultLayout() const override;
  vi::EffectiveViewBinding binding() const;
  vsr::ui::imgui::Viewport *viewport{nullptr};
  vsr::rendering::Manipulator manipulator;
  vi::Integration integration;
  vi::identity::IdentityAuthority ids;
  std::unique_ptr<vi::identity::SceneIdentity> identity;
  std::unique_ptr<vi::identity::ViewRevisionTracker> tracker;
  std::unique_ptr<vi::session::SessionAuthority> authority;
  std::unique_ptr<vi::operations::OperationEngine> engine;
  vi::sync::SyncStates states;
  std::unique_ptr<vi::sync::SyncService> sync;
  vi::activity::ActivityLog log;
  std::unique_ptr<vi::render::RenderService> renders;
  std::unique_ptr<vi::render::ControllerCamera> controller;
  std::unique_ptr<vi::frame::FrameService> frames;
  std::unique_ptr<vi::frame::ViewerFrameSource<vsr::ui::imgui::Viewport>>
      source;
  vi::operations::Credentials credentials;
  vi::SceneId sceneId{};
  vi::ViewId viewId{};
  vi::operations::OperationRef move, observation;
  vi::mcp::json::Value receipt;
  std::uint64_t nextRequest{0};
  vsr::rendering::Manipulator before;
  float beforeFovy{0.f};
  bool matched{false}, recovered{false}, pending{false};
  std::string targetSceneRevision;
  std::chrono::steady_clock::time_point deadline;
};

FrameCameraApplication::FrameCameraApplication()
{
  commandLineOptions()->useDefaultLayout = false;
  deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
}

const char *FrameCameraApplication::applicationIdentifier() const
{
  return "viewport-camera-frame-test";
}

const char *FrameCameraApplication::getDefaultLayout() const
{
  return "";
}

vi::EffectiveViewBinding FrameCameraApplication::binding() const
{
  vi::EffectiveViewBinding value;
  static_cast<vsr::rendering::EffectiveView &>(value) =
      viewport->effectiveView();
  if (auto camera = viewport->boundCamera())
    value.cameraHandle = identity->handleOf(camera.data());
  if (auto renderer = viewport->boundRenderer())
    value.rendererHandle = identity->handleOf(renderer.data());
  return value;
}

vsr::ui::imgui::WindowArray FrameCameraApplication::setupWindows()
{
  auto windows = Application::setupWindows();
  auto &scene = appContext()->vsr.scene;
  vsr::io::generate_icosphere(scene, {}, 1, false);
  vsr::io::generate_default_lights(scene);
  appContext()->vsr.sceneLoadComplete = true;
  auto window = std::make_unique<vsr::ui::imgui::Viewport>(
      this, &manipulator, "Main interactive viewport");
  viewport = window.get();
  viewport->setLibrary("helide");
  windows.emplace_back(std::move(window));
  return windows;
}

void FrameCameraApplication::uiFrameStart()
{
  Application::uiFrameStart();
  if (engine) {
    integration.service();
    REQUIRE(integration.submit(sceneId, [&](auto &) { frames->pump(); }));
    integration.service();
  }
  if (recovered || std::chrono::steady_clock::now() >= deadline) {
    if (!recovered)
      std::cout << "CAMERA_FRAME_ACCEPTANCE state=timeout limit_seconds=120\n";
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
  }
}

void FrameCameraApplication::uiRenderEnd()
{
  if (!engine && viewport->boundCamera()
      && viewport->effectiveView().unavailableReason.empty()) {
    auto &scene = appContext()->vsr.scene;
    identity = std::make_unique<vi::identity::SceneIdentity>(ids, scene);
    tracker =
        std::make_unique<vi::identity::ViewRevisionTracker>(ids, *identity);
    vi::SceneRegistration sr;
    sr.scene = &scene;
    sr.grants.read = true;
    sceneId = *integration.registerScene(sr);
    vi::ViewRegistration vr;
    vr.scene = sceneId;
    vr.name = "Main interactive viewport";
    vr.role = vi::ViewRole::INTERACTIVE_VIEWPORT;
    vr.grants.read = true;
    vr.observeBinding = [&] {
      return std::optional<vi::EffectiveViewBinding>(binding());
    };
    viewId = *integration.registerView(vr);
    REQUIRE(integration.activate().ok());
    authority = vi::session::SessionAuthority::create(integration);
    engine = vi::operations::OperationEngine::create(integration, *authority);
    const auto attached = authority->attach(authority->launchCredential());
    REQUIRE(attached.code == vi::session::Code::OK);
    credentials = {authority->launchCredential(),
        attached.sessionId,
        attached.sessionSecret};
    sync =
        std::make_unique<vi::sync::SyncService>(integration, *engine, states);
    renders = std::make_unique<vi::render::RenderService>(
        integration, *engine, *sync, log);
    controller = std::make_unique<vi::render::ControllerCamera>(
        *identity,
        *tracker,
        manipulator,
        [&] { return viewport->boundCamera().data(); },
        [&] { return viewport->currentCamera() == viewport->boundCamera(); });
    REQUIRE(renders->attachCameraController(
        viewId, *identity, *tracker, *controller));
    frames = std::make_unique<vi::frame::FrameService>(
        integration, *authority, *engine);
    REQUIRE(frames->attachView(viewId,
        *identity,
        *tracker,
        vi::frame::SourceQualification::VIEWER_CPU_READY));
    REQUIRE(frames->setExposed(viewId, true));
    source = std::make_unique<
        vi::frame::ViewerFrameSource<vsr::ui::imgui::Viewport>>(
        *frames, viewId, *identity, *viewport);
    REQUIRE(source->connected());

    integration.setAgentCameraControlEnabled(true);
    // First warm the natural source; startup layout/device changes are not
    // evidence for a camera operation against a stable interactive binding.
    observation = {credentials.sessionId, ++nextRequest};
    REQUIRE(frames->submit(credentials, observation, viewId).code
        == vi::operations::Outcome::OK);
    return;
  }
  if (!engine || recovered)
    return;
  if (move.requestId == 0) {
    const auto baseline = engine->status(credentials, observation);
    if (!baseline.terminal)
      return;
    REQUIRE(baseline.terminal == vi::operations::Outcome::OK);
    if (!frames->result(credentials, observation)) {
      vi::mcp::json::Value result;
      REQUIRE(vi::mcp::json::parse(baseline.result, result));
      INFO("baseline=" << baseline.result);
      REQUIRE(result.get("reason"));
      // Zero-sized initial layout is transient, not a newly qualified source.
      REQUIRE(result.get("reason")->s == "inactive_binding");
      observation = {credentials.sessionId, ++nextRequest};
      REQUIRE(frames->submit(credentials, observation, viewId).code
          == vi::operations::Outcome::OK);
      return;
    }
    before = manipulator;
    beforeFovy = *viewport->boundCamera()->parameterValueAs<float>("fovy");
    vi::render::CameraRequest request;
    request.view = viewId;
    request.position = {3., 2., 7.};
    request.direction = {0., 0., -1.};
    request.up = {0., 1., 0.};
    request.fovyDegrees = 30.;
    move = {credentials.sessionId, ++nextRequest};
    REQUIRE(renders->submitSetCamera(credentials, move, request).code
        == vi::operations::Outcome::OK);
    return;
  }
  const auto moved = engine->status(credentials, move);
  if (!moved.terminal)
    return;
  REQUIRE(moved.terminal == vi::operations::Outcome::OK);
  REQUIRE(moved.committed);
  REQUIRE(vi::mcp::json::parse(moved.result, receipt));
  if (targetSceneRevision.empty())
    targetSceneRevision = revisionToken(identity->sceneRevision());
  if (!pending) {
    observation = {credentials.sessionId, ++nextRequest};
    REQUIRE(frames->submit(credentials, observation, viewId).code
        == vi::operations::Outcome::OK);
    pending = true;
    return;
  }
  const auto observed = engine->status(credentials, observation);
  if (!observed.terminal)
    return;
  REQUIRE(observed.terminal == vi::operations::Outcome::OK);
  vi::mcp::json::Value result;
  REQUIRE(vi::mcp::json::parse(observed.result, result));
  INFO("receipt=" << moved.result << " frame=" << observed.result);
  REQUIRE(result.get("state")->s == "available");
  REQUIRE(result.get("view_id")->n == viewId);
  REQUIRE(result.get("view_identity")->s == receipt.get("view_identity")->s);
  REQUIRE(result.get("image_kind")->s == "presented_frame");
  REQUIRE(
      result.get("rendered_view_revision_basis")->s == "anari_render_start");
  // Exact equality, never a newer unrelated pose or just a timestamp.
  REQUIRE(result.get("current_view_revision")->s
      == receipt.get("view_revision")->s);
  if (result.get("stale")->b) {
    pending = false;
    return;
  }
  REQUIRE(result.get("rendered_view_revision")->s
      == receipt.get("view_revision")->s);
  REQUIRE(result.get("rendered_scene_revision")->s == targetSceneRevision);
  REQUIRE(result.get("current_scene_revision")->s == targetSceneRevision);
  const auto *camera = result.get("rendered_configuration")->get("camera");
  REQUIRE(camera);
  for (const auto *key : {"position", "direction", "up"})
    for (int i = 0; i < 3; ++i)
      CHECK(camera->get(key)->a[i].n
          == Approx(receipt.get("effective_camera")->get(key)->a[i].n));
  CHECK(camera->get("projection")->s
      == receipt.get("effective_camera")->get("projection")->s);
  CHECK(camera->get("fovy_deg")->n == Approx(30.));
  const auto image = frames->result(credentials, observation);
  REQUIRE(image);
  REQUIRE_FALSE(image->png.empty());
  matched = true;
  const auto actual = binding();
  std::cout << "CAMERA_FRAME_ACCEPTANCE host=Linux/SDL3 renderer="
            << SDL_GetRendererName(sdlRenderer())
            << " device=" << actual.deviceIdentifier
            << " subtype=" << actual.deviceSubtype
            << " binding=VIEWER_CPU_READY/host_sdl_upload view=" << viewId
            << " png_bytes=" << image->png.size() << " receipt=" << moved.result
            << " frame=" << observed.result << '\n';
  const auto revision = tracker->revision();
  REQUIRE(controller->hasRecovery()); // The host's reserved Before agent slot.
  REQUIRE(controller->restore() == vi::operations::Outcome::OK);
  CHECK(tracker->revision() != revision);
  for (int i = 0; i < 3; ++i) {
    CHECK(manipulator.eye()[i] == Approx(before.eye()[i]));
    CHECK(manipulator.at()[i] == Approx(before.at()[i]));
    CHECK(manipulator.dir()[i] == Approx(before.dir()[i]));
    CHECK(manipulator.up()[i] == Approx(before.up()[i]));
  }
  CHECK(manipulator.distance() == before.distance());
  CHECK(manipulator.fixedDistance() == before.fixedDistance());
  CHECK(manipulator.axis() == before.axis());
  CHECK(manipulator.mode() == before.mode());
  CHECK(
      *viewport->boundCamera()->parameterValueAs<float>("fovy") == beforeFovy);
  std::cout << "CAMERA_FRAME_RECOVERY slot=Before_agent view=" << viewId
            << " revision=" << revisionToken(tracker->revision())
            << " fovy_radians=" << beforeFovy << '\n';
  recovered = true;
}

void FrameCameraApplication::teardown()
{
  source.reset();
  frames.reset();
  renders.reset();
  controller.reset();
  sync.reset();
  engine.reset();
  authority.reset();
  tracker.reset();
  identity.reset();
  Application::teardown();
}
} // namespace

TEST_CASE(
    "Agent camera move matches the actual main viewport presentation and recovers",
    "[.viewer-camera-frame]")
{
  REQUIRE(std::getenv("VSR_VIEWER_TEST_ISOLATED"));
  REQUIRE(std::getenv("XDG_CONFIG_HOME"));
  REQUIRE(std::getenv("XDG_RUNTIME_DIR"));
  FrameCameraApplication app;
  app.run(640, 480, "Isolated camera frame acceptance");
  CHECK(app.matched);
  CHECK(app.recovered);
}
#endif

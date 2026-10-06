// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#ifdef VSR_TEST_VIEWER_UI
// vsr_ui_imgui
#include "vsr/ui/imgui/Application.h"
#include "vsr/ui/imgui/windows/Viewport.h"
// vsr_integration
#include "vsr/integration/render/ControllerCamera.hpp"
// vsr_io
#include "vsr/io/procedural.hpp"
// catch
#include "catch.hpp"
// std
#include <cstdlib>

namespace {

struct CameraApplication : vsr::ui::imgui::Application
{
  CameraApplication();
  vsr::ui::imgui::WindowArray setupWindows() override;
  void uiFrameStart() override;
  const char *applicationIdentifier() const override;
  const char *getDefaultLayout() const override;
  vsr::ui::imgui::Viewport *viewport{nullptr};
  vsr::rendering::Manipulator manipulator;
  vsr::integration::identity::IdentityAuthority ids;
  std::unique_ptr<vsr::integration::identity::SceneIdentity> identity;
  std::unique_ptr<vsr::integration::identity::ViewRevisionTracker> tracker;
  std::unique_ptr<vsr::integration::render::ControllerCamera> controller;
  vsr::math::float3 before;
  float originalFovy{0.f};
  std::size_t frames{0};
  int phase{0};
  bool applied{false}, navigated{false}, recovered{false};
};

CameraApplication::CameraApplication()
{
  commandLineOptions()->useDefaultLayout = false;
}

const char *CameraApplication::applicationIdentifier() const
{
  return "viewport-camera-test";
}

const char *CameraApplication::getDefaultLayout() const
{
  return "";
}

vsr::ui::imgui::WindowArray CameraApplication::setupWindows()
{
  auto windows = Application::setupWindows();
  auto &scene = appContext()->vsr.scene;
  vsr::io::generate_icosphere(scene, {}, 1, false);
  vsr::io::generate_default_lights(scene);
  appContext()->vsr.sceneLoadComplete = true;
  auto window = std::make_unique<vsr::ui::imgui::Viewport>(
      this, &manipulator, "Camera test");
  viewport = window.get();
  viewport->setLibrary("helide");
  windows.emplace_back(std::move(window));
  return windows;
}

void CameraApplication::uiFrameStart()
{
  Application::uiFrameStart();
  ++frames;
  if (viewport->boundCamera()
      && viewport->effectiveView().unavailableReason.empty()) {
    using namespace vsr::integration;
    if (phase == 0) {
      identity = std::make_unique<identity::SceneIdentity>(
          ids, appContext()->vsr.scene);
      tracker = std::make_unique<identity::ViewRevisionTracker>(ids, *identity);
      controller = std::make_unique<render::ControllerCamera>(
          *identity,
          *tracker,
          *viewport->cameraManipulator(),
          [&] { return viewport->boundCamera().data(); },
          [&] { return viewport->currentCamera() == viewport->boundCamera(); });
      manipulator.pan({0.1f, 0.f});
      before = manipulator.eye();
      originalFovy = *viewport->boundCamera()->parameterValueAs<float>("fovy");
      appContext()->addCurrentViewToCameraPoses("Before agent");
      render::CameraRequest request;
      request.position = {3., 2., 7.};
      request.direction = {0., 0., -2.};
      request.up = {0., 2., 0.};
      request.fovyDegrees = 30.;
      render::PreparedCameraChange change;
      REQUIRE(controller->prepare(request, change) == operations::Outcome::OK);
      change.install();
      tracker->notePipelineChanged();
      change.synchronize();
      applied = true;
      ++phase;
    } else if (phase == 1) {
      const auto binding = viewport->effectiveView();
      CHECK(binding.camera.position[0] == Approx(3.));
      CHECK(binding.camera.position[2] == Approx(7.));
      CHECK(binding.camera.fovyDegrees == Approx(30.));
      manipulator.rotate({0.01f, 0.02f});
      manipulator.pan({0.01f, 0.f});
      manipulator.zoom(0.01f);
      ++phase;
    } else if (phase == 2) {
      const auto binding = viewport->effectiveView();
      CHECK(binding.camera.position[0] != Approx(3.));
      CHECK(controller->hasRecovery());
      navigated = true;
      REQUIRE(controller->restore() == operations::Outcome::OK);
      ++phase;
    } else if (phase == 3) {
      const auto binding = viewport->effectiveView();
      CHECK(binding.camera.position[0] == Approx(before.x));
      CHECK(binding.camera.position[2] == Approx(before.z));
      CHECK(*viewport->boundCamera()->parameterValueAs<float>("fovy")
          == originalFovy);
      REQUIRE(appContext()->view.poses.size() == 1);
      CHECK(appContext()->view.poses[0].name == "Before agent");
      recovered = true;
      ++phase;
    }
  }
  if (recovered || frames >= 600) {
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
  }
}

} // namespace

TEST_CASE("Controller recovery is rolling, bounded and per view",
    "[integration][camera-control]")
{
  using namespace vsr::integration;
  vsr::scene::Scene scene;
  identity::IdentityAuthority ids;
  identity::SceneIdentity identity(ids, scene);
  identity::ViewRevisionTracker firstTracker(ids, identity),
      secondTracker(ids, identity);
  auto firstCamera = scene.createObject<vsr::scene::Camera>(
      vsr::scene::tokens::camera::perspective);
  auto secondCamera = scene.createObject<vsr::scene::Camera>(
      vsr::scene::tokens::camera::perspective);
  vsr::rendering::Manipulator first, second;
  first.setConfig({0.f, 0.f, 0.f}, 5.f);
  second.setConfig({0.f, 0.f, 0.f}, 8.f);
  render::ControllerCamera a(
      identity,
      firstTracker,
      first,
      [&] { return firstCamera.data(); },
      [] { return true; });
  render::ControllerCamera b(
      identity,
      secondTracker,
      second,
      [&] { return secondCamera.data(); },
      [] { return true; });
  auto apply = [](render::ControllerCamera &controller, double x) {
    render::CameraRequest request;
    request.position = {x, 2., 7.};
    render::PreparedCameraChange prepared;
    REQUIRE(controller.prepare(request, prepared) == operations::Outcome::OK);
    prepared.install();
    prepared.synchronize();
  };
  apply(a, 3.);
  apply(b, 9.);
  apply(a, 4.);
  REQUIRE(a.restore() == operations::Outcome::OK);
  CHECK(first.eye().x == Approx(3.f));
  CHECK(second.eye().x == Approx(9.f));
  CHECK(first.distance() == Approx(5.f));
  REQUIRE(b.restore() == operations::Outcome::OK);
  CHECK(second.eye().x == Approx(0.f));
  CHECK(second.eye().z == Approx(-8.f));
  CHECK(second.distance() == Approx(8.f));

  for (double fovy : {1e-300, 179.99999999999}) {
    render::CameraRequest request;
    request.fovyDegrees = fovy;
    render::PreparedCameraChange prepared;
    CHECK(a.prepare(request, prepared) == operations::Outcome::INVALID_REQUEST);
    CHECK(first.eye().x == Approx(3.f));
    CHECK(a.hasRecovery());
  }
  auto orthographic = scene.createObject<vsr::scene::Camera>(
      vsr::scene::tokens::camera::orthographic);
  firstCamera = orthographic;
  CHECK_FALSE(a.recoveryAvailable());
  CHECK(a.restore() == operations::Outcome::OUTCOME_UNAVAILABLE);
  render::CameraRequest request;
  request.fovyDegrees = 30.;
  render::PreparedCameraChange prepared;
  CHECK(a.prepare(request, prepared) == operations::Outcome::INVALID_REQUEST);
}

TEST_CASE("Isolated viewport camera remains usable and recoverable",
    "[.viewer-camera]")
{
  REQUIRE(std::getenv("VSR_VIEWER_TEST_ISOLATED"));
  REQUIRE(std::getenv("XDG_CONFIG_HOME"));
  REQUIRE(std::getenv("XDG_RUNTIME_DIR"));
  CameraApplication app;
  try {
    app.run(640, 480, "Isolated camera controller test");
  } catch (const std::exception &error) {
    FAIL(error.what() << ": " << SDL_GetError());
  }
  CHECK(app.applied);
  CHECK(app.navigated);
  CHECK(app.recovered);
}
#endif

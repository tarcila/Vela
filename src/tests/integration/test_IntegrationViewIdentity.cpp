// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_integration
#include "vsr/integration/inspection/Inspection.hpp"
#include "vsr/integration/mcp/Json.hpp"
#include "vsr/integration/sync/SceneSync.hpp"
// vsr_rendering
#include "vsr/rendering/pipeline/ImagePipeline.h"
// std
#include <limits>
#include <memory>

using namespace vsr::integration;

namespace {

struct ViewHost
{
  vsr::scene::Scene scene;
  vsr::rendering::ImagePipeline pipeline{80, 40};
  vsr::rendering::ExternalFrameSource *pass{nullptr};
  vsr::scene::CameraRef bound;
  vsr::scene::CameraRef unrelated;
  vsr::scene::RendererRef renderer;
  Integration integration;
  identity::IdentityAuthority identities;
  std::unique_ptr<identity::SceneIdentity> sceneIdentity;
  std::unique_ptr<identity::ViewRevisionTracker> tracker;
  std::unique_ptr<session::SessionAuthority> authority;
  std::unique_ptr<operations::OperationEngine> engine;
  std::unique_ptr<inspection::Inspector> inspector;
  operations::Credentials credentials;
  SceneId sceneId{};
  ViewId viewId{};
  std::uint64_t requestId{0};
  std::string transition;
  std::size_t observations{0};
  CapturePolicy policy;

  ViewHost();
  std::optional<EffectiveViewBinding> snapshot();
  mcp::json::Value info();
  mcp::json::Value discover();
  mcp::json::Value run(inspection::Built built);
};

ViewHost::ViewHost()
{
  bound = scene.createObject<vsr::scene::Camera>("perspective");
  unrelated = scene.createObject<vsr::scene::Camera>("perspective");
  bound->setParameter("position", vsr::math::float3(1.f, 2.f, 3.f));
  unrelated->setParameter("position", vsr::math::float3(99.f));
  renderer = scene.createRenderer("headless_host", "default")->self();
  pass = pipeline.setSource<vsr::rendering::ExternalFrameSource>();
  SceneRegistration sceneRegistration;
  sceneRegistration.name = "real_scene";
  sceneRegistration.scene = &scene;
  sceneRegistration.grants.read = true;
  sceneId = *integration.registerScene(sceneRegistration);
  ViewRegistration registration;
  registration.name = "Primary viewport";
  registration.scene = sceneId;
  registration.grants.read = true;
  registration.role = ViewRole::INTERACTIVE_VIEWPORT;
  registration.sceneSyncConfirmsBinding = false;
  registration.observeBinding = [this] { return snapshot(); };
  viewId = *integration.registerView(registration);
  REQUIRE(integration.activate().ok());
  sceneIdentity = std::make_unique<identity::SceneIdentity>(identities, scene);
  tracker = std::make_unique<identity::ViewRevisionTracker>(
      identities, *sceneIdentity);
  authority = session::SessionAuthority::create(integration);
  engine = operations::OperationEngine::create(integration, *authority);
  const auto attached = authority->attach(authority->launchCredential());
  REQUIRE(attached.code == session::Code::OK);
  credentials = {authority->launchCredential(),
      attached.sessionId,
      attached.sessionSecret};
  inspector = std::make_unique<inspection::Inspector>(
      integration, *authority, [this](SceneId id) {
        return id == sceneId ? sceneIdentity.get() : nullptr;
      });
  inspector->setViewRevisions([this](ViewId) { return tracker.get(); });
}

std::optional<EffectiveViewBinding> ViewHost::snapshot()
{
  REQUIRE(integration.inServiceCall());
  ++observations;
  EffectiveViewBinding result;
  result.unavailableReason = transition;
  const auto position = bound->parameterValueAs<vsr::math::float3>("position");
  result.camera.position = {position->x, position->y, position->z};
  result.cameraHandle = sceneIdentity->handleOf(bound.data());
  result.rendererHandle = sceneIdentity->handleOf(renderer.data());
  const auto size = pipeline.dimensions();
  result.width = size.x;
  result.height = size.y;
  result.displayWidth = 160.;
  result.displayHeight = 80.;
  result.deviceIdentifier = "headless_host";
  result.deviceSubtype = "test";
  result.rendererSubtype = "default";
  result.rendererConfigurationRevision =
      std::to_string(renderer->lastParameterChange());
  result.rendererSettingsReason = "backend_defaults_not_observed";
  result.pipelineSource = "real_external_frame_pipeline";
  result.pipelineConfigurationRevision = "1";
  result.pipelineSettings = {{"enabled", {double(pass->isEnabled())}, {}}};
  result.outputColor = "rgba8_srgb";
  return result;
}

mcp::json::Value ViewHost::run(inspection::Built built)
{
  REQUIRE(built.code == operations::Outcome::OK);
  const operations::OperationRef ref{credentials.sessionId, ++requestId};
  REQUIRE(engine->submit(credentials, ref, std::move(built.request)).code
      == operations::Outcome::OK);
  while (integration.service()) {
  }
  const auto status = engine->status(credentials, ref);
  REQUIRE(status.terminal == operations::Outcome::OK);
  mcp::json::Value result;
  REQUIRE(mcp::json::parse(status.result, result));
  return result;
}

mcp::json::Value ViewHost::info()
{
  return run(inspector->makeViewInfo(credentials.sessionId, viewId));
}

mcp::json::Value ViewHost::discover()
{
  return run(inspector->makeDiscovery(credentials.sessionId, {}));
}

} // namespace

TEST_CASE(
    "public discovery matches real headless binding and view observations",
    "[integration][view-identity][view-info]")
{
  ViewHost host;
  const auto sceneRevision = host.sceneIdentity->sceneRevision();
  const auto first = host.info();
  CHECK(first.get("role")->s == "interactive_viewport");
  CHECK(first.get("name")->s == "Primary viewport");
  CHECK(first.get("camera")->get("position")->a[0].n == 1.);
  CHECK(first.get("width")->n == 80);
  CHECK(first.get("display")->get("scale_x")->n == 2.);
  CHECK(first.get("renderer_settings_state")->s == "incomplete");
  CHECK(first.get("renderer_quality")->get("reason")->s
      == "samples_not_observed");
  const auto discovery = host.discover();
  REQUIRE(discovery.get("items")->a.size() == 2);
  const auto &view = discovery.get("items")->a[1];
  CHECK(view.get("effective_binding")->get("camera_handle")->s
      == first.get("camera_handle")->s);
  CHECK(view.get("view_revision")->s == first.get("view_revision")->s);
  CHECK(view.get("effective_binding")->get("view_revision")->s
      == view.get("view_revision")->s);
  CHECK(view.get("capture_policy_revision")->s
      == first.get("capture_policy_revision")->s);
  CHECK(host.sceneIdentity->sceneRevision() == sceneRevision);
  CHECK(host.unrelated->parameterValueAs<vsr::math::float3>("position")->x
      == 99.f);
  CHECK(host.scene.objectDB().renderer.size() == 1);
  CHECK_FALSE(host.integration.observeViewBinding(host.viewId));
  sync::SyncStates syncStates;
  sync::SyncService synchronization(host.integration, *host.engine, syncStates);
  CHECK_FALSE(synchronization.viewReady(host.viewId));
  host.bound->setParameter("position", vsr::math::float3(5.f, 6.f, 7.f));
  host.renderer->setParameter("background", vsr::math::float4(0.25f));
  const auto reconfigured = host.info();
  CHECK(reconfigured.get("camera")->get("position")->a[0].n == 5.);
  CHECK(reconfigured.get("renderer_configuration_revision")->s
      != first.get("renderer_configuration_revision")->s);
  CHECK(reconfigured.get("view_revision")->s != first.get("view_revision")->s);
  host.pipeline.setDimensions(40, 20);
  host.pass->setEnabled(false);
  const auto changed = host.info();
  CHECK(changed.get("width")->n == 40);
  CHECK(changed.get("view_revision")->s != first.get("view_revision")->s);
  CHECK(changed.get("scene_revision")->s
      == reconfigured.get("scene_revision")->s);
  host.transition = "device_transition";
  const auto switching = host.info();
  CHECK(switching.get("reason")->s == "device_transition");
  CHECK_FALSE(switching.get("camera"));
  CHECK(switching.get("view_revision")->s != changed.get("view_revision")->s);
  host.transition.clear();
  CHECK(
      host.info().get("view_revision")->s != switching.get("view_revision")->s);
  host.authority->restrict(
      session::Target::view(host.viewId), session::Capability::READ);
  const auto denied =
      host.inspector->makeViewInfo(host.credentials.sessionId, host.viewId);
  if (denied.code == operations::Outcome::OK) {
    const operations::OperationRef ref{
        host.credentials.sessionId, ++host.requestId};
    CHECK(host.engine->submit(host.credentials, ref, denied.request).code
        == operations::Outcome::FORBIDDEN);
  } else
    CHECK(denied.code == operations::Outcome::FORBIDDEN);
}

TEST_CASE("retired view slots do not hide recreated views or reuse identity",
    "[integration][view-identity]")
{
  ViewHost host;
  const auto retired = host.viewId;
  const auto first = host.info();
  host.integration.submit(host.sceneId, [&](vsr::scene::Scene &) {
    CHECK(host.integration.unregisterView(retired));
    ViewRegistration registration;
    registration.name = std::string(257, 'x');
    registration.scene = host.sceneId;
    registration.grants = {true, false};
    CHECK_FALSE(host.integration.registerView(registration));
    registration.name = std::string(256, 'x');
    registration.role = ViewRole::HEADLESS;
    registration.observeBinding = [&] { return host.snapshot(); };
    host.viewId = *host.integration.registerView(registration);
    host.tracker = std::make_unique<identity::ViewRevisionTracker>(
        host.identities, *host.sceneIdentity);
  });
  host.integration.service();
  CHECK(host.viewId > retired);
  CHECK_FALSE(host.integration.isRegisteredView(retired));
  const auto recreated = host.info();
  CHECK(recreated.get("name")->s.size() == 256);
  CHECK(recreated.get("role")->s == "headless");
  CHECK(recreated.get("view_identity")->s != first.get("view_identity")->s);
  ViewHost independentLaunch;
  CHECK(independentLaunch.info().get("view_identity")->s
      != first.get("view_identity")->s);
  CHECK(recreated.get("view_revision")->s != first.get("view_revision")->s);
  const auto discovery = host.discover();
  REQUIRE(discovery.get("items")->a.size() == 2);
  CHECK(discovery.get("items")->a[1].get("id")->n == host.viewId);
}

TEST_CASE("host policy validation preserves tokens and bounded closed defaults",
    "[integration][view-identity][view-info]")
{
  ViewHost host;
  const auto first = host.info();
  const auto original = host.integration.capturePolicy(host.viewId)->revision;
  host.integration.submit(host.sceneId, [&](vsr::scene::Scene &) {
    for (int kind = 0; kind < 12; ++kind) {
      CapturePolicy bad;
      if (kind == 0)
        bad.maxAxis = 0;
      if (kind == 1)
        bad.maxPixels = std::numeric_limits<uint64_t>::max();
      if (kind == 2)
        bad.deadlineSeconds = 0;
      if (kind == 3)
        bad.maxQualityCount = 0;
      if (kind == 4)
        bad.perViewCapacity = 2;
      if (kind == 5)
        bad.maxPngBytes = 1;
      if (kind == 6)
        bad.maxResponseBytes = 1;
      if (kind == 7)
        bad.hostBudgetBytes = 1;
      if (kind == 8)
        bad.deviceBudgetBytes = 1;
      if (kind == 9)
        bad.qualityUnits = {"samples", "samples"};
      if (kind == 10)
        bad.qualityUnits = {std::string(4096, 'x')};
      if (kind == 11)
        bad.globalActiveCapacity = 64;
      CHECK_FALSE(host.integration.updateCapturePolicy(host.viewId, bad));
      CHECK(host.integration.capturePolicy(host.viewId)->revision == original);
    }
    CapturePolicy narrowed;
    narrowed.maxAxis = 256;
    narrowed.maxPixels = 65536;
    narrowed.maxPngBytes = 270000;
    narrowed.maxResponseBytes = 600000;
    narrowed.qualityUnits = {"samples", "completed_frames"};
    REQUIRE(host.integration.updateCapturePolicy(host.viewId, narrowed));
    const auto changed = host.integration.capturePolicy(host.viewId)->revision;
    CHECK(host.integration.updateCapturePolicy(host.viewId, narrowed));
    CHECK(host.integration.capturePolicy(host.viewId)->revision == changed);
    CHECK(host.integration.updateCapturePolicy(host.viewId, CapturePolicy{}));
    CHECK(host.integration.capturePolicy(host.viewId)->revision > changed);
  });
  host.integration.service();
  const auto last = host.info();
  CHECK(last.get("capture_policy_revision")->s
      != first.get("capture_policy_revision")->s);
  CHECK(last.get("view_revision")->s == first.get("view_revision")->s);
  CHECK_FALSE(last.get("capture_policy")->get("exposed")->b);
  CHECK_FALSE(last.get("capture_policy")->get("presented_frames_exposed")->b);
  CHECK(
      last.get("capture_policy")->get("frame_cache_goal_bytes")->n == 8388608);
  CHECK(last.get("capture_policy")->get("max_png_bytes")->n == 8388608);
}

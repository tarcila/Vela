// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// Headless agent-integration host: builds the spec §8 procedural acceptance
// fixture (a sphere above a ground plane, matte materials, one directional
// light) in a standard vsr::app::Context and exposes it to agents.
//
// The host registers one fixed-size helide view ("agent") with its own
// offscreen Image Pipeline, RenderIndex and fixed camera. The camera is not
// a scene object, so agents can neither inspect nor edit it (addendum 07).
// The scene grants read/edit/resynchronize and the view read/render; the
// synchronization hooks confirm (normal) or rebuild (recovery) the view's
// RenderIndex for the current scene.
//
// When built with MCP the host starts the loopback listener, publishes the
// private launch descriptor and prints only its path on stdout. It then runs
// the blocking service loop until interrupted (Ctrl-C / SIGTERM).
//
// Usage: vsrTutorialIntegrationHeadless [--exit-after-activation]

// vsr
#include "vsr/app/AgentOptions.hpp"
#include "vsr/app/Context.h"
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/activity/Activity.hpp"
#include "vsr/integration/context/ContextAdapter.hpp"
#include "vsr/integration/edit/Translation.hpp"
#include "vsr/integration/identity/Identity.hpp"
#include "vsr/integration/inspection/Inspection.hpp"
#include "vsr/integration/render/RevisionRender.hpp"
#include "vsr/integration/restore/BatchRestore.hpp"
#ifdef VSR_USE_INTEGRATION_MCP
#include "vsr/integration/descriptor/LaunchDescriptor.hpp"
#include "vsr/integration/mcp/McpServer.hpp"
#include "vsr/integration/mcp/tools/EditTools.hpp"
#include "vsr/integration/mcp/tools/ImageTools.hpp"
#include "vsr/integration/mcp/tools/LookupTools.hpp"
#include "vsr/integration/mcp/tools/QueryTools.hpp"
#include "vsr/integration/mcp/tools/RenderTools.hpp"
#endif
#include "vsr/rendering/pipeline/ImagePipeline.h"
#include "vsr/rendering/view/FrameObservation.h"
#include "vsr/rendering/view/ManipulatorToAnari.hpp"
// std
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace {

namespace vi = vsr::integration;
namespace math = vsr::math;
namespace tokens = vsr::scene::tokens;

volatile std::sig_atomic_t g_stop = 0;

void onSignal(int)
{
  g_stop = 1;
}

// Spec §8 fixture: unit sphere resting on a ground plane, both matte, lit by
// one directional light.
void buildFixture(vsr::scene::Scene &scene)
{
  auto root = scene.defaultLayer()->root();

  auto sphereMaterial =
      scene.createObject<vsr::scene::Material>(tokens::material::matte);
  sphereMaterial->setName("sphere_material");
  sphereMaterial->setParameter("color", math::float3(0.8f, 0.1f, 0.1f));
  sphereMaterial->setParameter("opacity", 1.f);

  auto sphere =
      scene.createObject<vsr::scene::Geometry>(tokens::geometry::sphere);
  sphere->setName("sphere_geometry");
  sphere->setParameter("radius", 1.f);
  auto center = scene.createArray(ANARI_FLOAT32_VEC3, 1);
  center->setData(std::vector<math::float3>{{0.f, 1.f, 0.f}});
  sphere->setParameterObject("vertex.position", *center);
  scene.insertChildObjectNode(
      root, scene.createSurface("sphere", sphere, sphereMaterial));

  auto groundMaterial =
      scene.createObject<vsr::scene::Material>(tokens::material::matte);
  groundMaterial->setName("ground_material");
  groundMaterial->setParameter("color", math::float3(0.6f, 0.6f, 0.6f));
  groundMaterial->setParameter("opacity", 1.f);

  const float e = 5.f;
  auto ground =
      scene.createObject<vsr::scene::Geometry>(tokens::geometry::triangle);
  ground->setName("ground_geometry");
  auto positions = scene.createArray(ANARI_FLOAT32_VEC3, 4);
  positions->setData(std::vector<math::float3>{
      {-e, 0.f, e}, {e, 0.f, e}, {e, 0.f, -e}, {-e, 0.f, -e}});
  auto normals = scene.createArray(ANARI_FLOAT32_VEC3, 4);
  normals->setData(std::vector<math::float3>(4, math::float3(0.f, 1.f, 0.f)));
  auto indices = scene.createArray(ANARI_UINT32_VEC3, 2);
  indices->setData(std::vector<math::uint3>{{0, 1, 2}, {0, 2, 3}});
  ground->setParameterObject("vertex.position", *positions);
  ground->setParameterObject("vertex.normal", *normals);
  ground->setParameterObject("primitive.index", *indices);
  scene.insertChildObjectNode(
      root, scene.createSurface("ground", ground, groundMaterial));

  auto light =
      scene.createObject<vsr::scene::Light>(tokens::light::directional);
  light->setName("sun");
  light->setParameter("color", math::float3(1.f, 1.f, 1.f));
  light->setParameter("irradiance", 1.f);
  light->setParameter("direction", math::float2(30.f, 300.f));
  scene.insertChildObjectNode(root, light);
}

// The agent-renderable view: a helide device with its own offscreen Image
// Pipeline, RenderIndex and fixed camera (same pattern as vsrViewer's agent
// view, addendum 07).
struct AgentView : vi::render::RenderBackend
{
  static constexpr uint32_t WIDTH = 640;
  static constexpr uint32_t HEIGHT = 360;

  vsr::app::Context *ctx{nullptr};
  anari::Device device{nullptr};
  vsr::rendering::RenderIndex *index{nullptr};
  anari::Camera camera{nullptr};
  anari::Renderer renderer{nullptr};
  vsr::rendering::ImagePipeline pipeline;
  vsr::rendering::AnariSceneRenderPass *pass{nullptr};
  bool rendered{false};

  bool setup(vsr::app::Context &c)
  {
    ctx = &c;
    device = c.anari.loadDevice("helide");
    if (!device)
      return false;
    index = c.anari.acquireRenderIndex(c.vsr.scene, "helide", device);
    if (!index)
      return false;

    // Fixed camera looking slightly down at the sphere.
    vsr::rendering::Manipulator pose;
    pose.setConfig(math::float3(0.f, 0.8f, 0.f), 8.f, math::float2(0.f, 20.f));
    camera = anari::newObject<anari::Camera>(device, "perspective");
    vsr::rendering::updateCameraParametersPerspective(device, camera, pose);
    anari::setParameter(device, camera, "fovy", math::radians(40.f));
    anari::setParameter(device, camera, "aspect", float(WIDTH) / HEIGHT);
    anari::commitParameters(device, camera);
    renderer = anari::newObject<anari::Renderer>(device, "default");
    anari::setParameter(
        device, renderer, "background", math::float4(0.1f, 0.1f, 0.1f, 1.f));
    anari::commitParameters(device, renderer);

    pipeline.setDimensions(WIDTH, HEIGHT);
    pass = pipeline.setSource<vsr::rendering::AnariSceneRenderPass>(device);
    pass->setRunAsync(false);
    pass->setCamera(camera);
    pass->setRenderer(renderer);
    return confirm();
  }

  // True only when the view's RenderIndex and pipeline are live for the
  // current scene; binds the index's current world into the pipeline.
  bool confirm()
  {
    if (!device || !index || !pass || pipeline.empty())
      return false;
    auto world = index->world();
    if (!world)
      return false;
    pass->setWorld(world);
    return true;
  }

  // Normal synchronization: the RenderIndex follows scene edits through the
  // update delegate; confirm it and re-bind its world.
  bool synchronize()
  {
    return confirm();
  }

  // Recovery: rebuild the view's RenderIndex from the current scene.
  bool recover()
  {
    if (!index)
      return false;
    index->populate();
    return confirm();
  }

  void teardown()
  {
    if (pass)
      pass->waitForCompletion();
    pipeline.clear();
    pass = nullptr;
    if (device) {
      if (camera)
        anari::release(device, camera);
      if (renderer)
        anari::release(device, renderer);
      if (index)
        ctx->anari.releaseRenderIndex(ctx->vsr.scene, device);
      anari::release(device, device);
    }
    camera = nullptr;
    renderer = nullptr;
    index = nullptr;
    device = nullptr;
  }

  // RenderBackend: one synchronous pipeline frame at the view's dimensions.
  bool begin(std::uint32_t width, std::uint32_t height) override
  {
    if (!confirm())
      return false;
    pipeline.setDimensions(width, height);
    // Synchronous mode renders and publishes the current scene in one call.
    pipeline.render();
    rendered = pipeline.getColorBuffer() != nullptr;
    return rendered;
  }
  vi::render::BackendState poll() override
  {
    return rendered ? vi::render::BackendState::DONE
                    : vi::render::BackendState::FAULT;
  }
  vi::image::SourceImage image() override
  {
    namespace img = vi::image;
    img::SourceImage src;
    const auto size = pipeline.dimensions();
    src.width = size.x;
    src.height = size.y;
    src.encoding = img::ChannelEncoding::SRGB_UINT8;
    src.alpha = img::AlphaMode::STRAIGHT;
    src.origin = img::RowOrigin::BOTTOM_LEFT;
    src.pixels = pipeline.getColorBuffer();
    return src;
  }
  bool release() override
  {
    if (pass)
      pass->waitForCompletion();
    rendered = false;
    return true;
  }
};

// Integration services, destroyed in reverse dependency order.
struct Host
{
  AgentView view;
  std::unique_ptr<vi::Integration> integration;
  std::unique_ptr<vi::session::SessionAuthority> authority;
  std::unique_ptr<vi::operations::OperationEngine> engine;
  std::unique_ptr<vi::identity::IdentityAuthority> ids;
  std::unique_ptr<vi::identity::SceneIdentity> identity;
  std::unique_ptr<vi::edit::EditService> edits;
  std::unique_ptr<vi::edit::TranslationService> translations;
  std::unique_ptr<vi::restore::RestoreService> restores;
  std::unique_ptr<vi::sync::SyncService> sync;
  std::unique_ptr<vi::activity::ActivityLog> activity;
  std::unique_ptr<vi::activity::EngineActivitySink> activitySink;
  std::unique_ptr<vi::identity::ViewRevisionTracker> tracker;
  std::unique_ptr<vi::render::RenderService> renders;
  std::unique_ptr<vi::inspection::Inspector> inspector;
#ifdef VSR_USE_INTEGRATION_MCP
  vi::descriptor::RuntimeDirectory runtimeDir;
  std::unique_ptr<vi::image::ImageExchange> images;
  std::unique_ptr<vi::mcp::McpServer> mcp;
#endif

  ~Host()
  {
#ifdef VSR_USE_INTEGRATION_MCP
    mcp.reset();
    images.reset();
#endif
    inspector.reset();
    view.release();
    renders.reset();
    tracker.reset();
    if (engine)
      engine->setActivitySink(nullptr);
    sync.reset();
    restores.reset();
    translations.reset();
    edits.reset();
    identity.reset();
    engine.reset();
    authority.reset();
    activitySink.reset();
    activity.reset();
    ids.reset();
    integration.reset();
    view.teardown();
  }
};

int fail(const char *what)
{
  std::fprintf(stderr, "%s\n", what);
  return 1;
}

} // namespace

int main(int argc, const char *argv[])
{
  vsr::app::AgentOptions agentOptions;
  if (!vsr::app::parseAgentOptions(argc, argv, agentOptions)) {
    std::fprintf(stderr,
        "--agentRestorationBytes requires one positive decimal byte count\n");
    return 1;
  }
  bool exitAfterActivation = false;
  for (std::size_t i = 1; i < agentOptions.arguments.size(); i++) {
    if (std::strcmp(agentOptions.arguments[i], "--exit-after-activation") == 0)
      exitAfterActivation = true;
    else {
      std::fprintf(
          stderr, "unknown argument '%s'\n", agentOptions.arguments[i]);
      return 1;
    }
  }

  vsr::app::Context ctx;
  buildFixture(ctx.vsr.scene);

  auto host = std::make_unique<Host>();
  if (!host->view.setup(ctx))
    return fail("agent view setup failed (helide)");

  host->integration = std::make_unique<vi::Integration>();
  auto &integration = *host->integration;

  vi::context::ContextSceneOptions options;
  options.name = "fixture";
  options.grants.read = true;
  options.grants.edit = true;
  options.grants.resynchronize = true;
  options.serializedArrayWrites = true;
  auto *view = &host->view;
  options.normalSyncHook = [view](vi::SceneId) { return view->synchronize(); };
  options.recoveryCallback = [view](vi::SceneId) { return view->recover(); };
  auto sceneId = vi::context::registerContextScene(integration, ctx, options);
  if (!sceneId)
    return fail("scene registration failed");

  vi::ViewRegistration vr;
  vr.name = "agent";
  vr.scene = *sceneId;
  vr.grants = {true, true}; // read, render
  auto viewId = integration.registerView(vr);
  if (!viewId)
    return fail("view registration failed");

  auto activation = integration.activate();
  if (!activation.ok()) {
    std::fprintf(stderr,
        "integration activation failed (target '%s')\n",
        activation.target.c_str());
    return 1;
  }

  host->authority = vi::session::SessionAuthority::create(integration);
  if (host->authority)
    host->engine =
        vi::operations::OperationEngine::create(integration, *host->authority);
  if (!host->engine)
    return fail("integration setup failed");
  host->ids = std::make_unique<vi::identity::IdentityAuthority>();
  host->identity =
      std::make_unique<vi::identity::SceneIdentity>(*host->ids, ctx.vsr.scene);
  host->edits = std::make_unique<vi::edit::EditService>(
      integration, *host->engine, host->authority->launchId());
  host->edits->attachScene(*sceneId, *host->identity);
  host->translations = std::make_unique<vi::edit::TranslationService>(
      integration, *host->authority, *host->engine, *host->edits);
  host->translations->attachScene(*sceneId, *host->identity);
  const auto hostImageMemory = std::make_shared<vsr::rendering::FrameBudget>(
      vsr::app::agentPhysicalByteCap(agentOptions.restorationBytes));
  host->restores = std::make_unique<vi::restore::RestoreService>(*host->engine,
      *host->edits,
      [hostImageMemory] { return hostImageMemory->reserve(65536); });
  host->restores->attachScene(*sceneId, *host->identity);
  host->sync = std::make_unique<vi::sync::SyncService>(
      integration, *host->engine, host->edits->syncStates());
  vi::activity::Limits activityLimits;
  activityLimits.retentionBytes = agentOptions.restorationBytes;
  host->activity = std::make_unique<vi::activity::ActivityLog>(activityLimits);
  host->activitySink = std::make_unique<vi::activity::EngineActivitySink>(
      *host->activity, integration);
  host->engine->setActivitySink(host->activitySink.get());
  host->tracker = std::make_unique<vi::identity::ViewRevisionTracker>(
      *host->ids, *host->identity);
  host->renders = std::make_unique<vi::render::RenderService>(
      integration, *host->engine, *host->sync, *host->activity);
  if (!host->renders->attachView(
          *viewId, *host->identity, *host->tracker, host->view))
    return fail("agent view attach failed");
  host->renders->setDimensions(*viewId, AgentView::WIDTH, AgentView::HEIGHT);
  auto *identity = host->identity.get();
  auto *tracker = host->tracker.get();
  host->inspector = std::make_unique<vi::inspection::Inspector>(integration,
      *host->authority,
      [identity, sid = *sceneId](
          vi::SceneId id) { return id == sid ? identity : nullptr; });
  host->inspector->setViewRevisions([tracker, vid = *viewId](vi::ViewId id) {
    return id == vid ? tracker : nullptr;
  });

#ifdef VSR_USE_INTEGRATION_MCP
  const auto runtimeStatus =
      vi::descriptor::openOsRuntimeDirectory(host->runtimeDir);
  vi::image::ExchangeLimits imageLimits;
  imageLimits.physicalByteCap = hostImageMemory->limit();
  imageLimits.reservePhysicalMemory = [hostImageMemory](std::size_t bytes) {
    return hostImageMemory->reserve(bytes);
  };
  imageLimits.retention = host->activity.get();
  imageLimits.globalRetentionBytes = host->activity->limits().retentionBytes;
  imageLimits.restorationRetained = [&host](const std::string &launch,
                                        const std::string &batch) {
    return host->activity->eligibility({launch, batch})
        == vi::activity::Eligibility::ELIGIBLE;
  };
  host->images = std::make_unique<vi::image::ImageExchange>(integration,
      *host->engine,
      host->authority->launchId(),
      host->runtimeDir,
      imageLimits);
  host->images->attachScene(*sceneId, *host->identity);
  host->images->attachRestoration(*host->edits, *host->restores);
  host->inspector->setImageExchange(
      [&host](vi::SceneId id, const std::string &sessionId) {
        return host->images->metadata(id, sessionId);
      },
      [&host](vi::SceneId id) { return host->images->unavailableReason(id); });
  vi::mcp::Config cfg;
  cfg.imageExchange = host->images->metadata(*sceneId);
  vi::mcp::tools::addImageTools(cfg.tools, {*host->ids, *host->images});
  vi::mcp::tools::addQueryTools(cfg.tools, {*host->engine, *host->inspector});
  vi::mcp::tools::addLookupTools(cfg.tools, *host->engine);
  vi::mcp::tools::addEditTools(cfg.tools,
      {*host->ids, *host->edits, *host->restores, host->translations.get()});
  vi::mcp::tools::addRenderTools(
      cfg.tools, {*host->renders, *host->sync, *host->engine});
  cfg.log = [](std::string_view line) {
    std::fprintf(stderr, "%.*s\n", int(line.size()), line.data());
  };
  if (!runtimeStatus.ok()) {
    std::fprintf(
        stderr, "runtime directory: %s\n", runtimeStatus.reason.c_str());
    return 1;
  }
  if (auto st = vi::mcp::McpServer::start(
          cfg, *host->authority, *host->engine, host->runtimeDir, host->mcp);
      !st.ok()) {
    std::fprintf(stderr, "MCP listener: %s\n", st.reason.c_str());
    return 1;
  }
  // stdout carries only the descriptor path.
  std::printf("%s\n", host->mcp->published().path().c_str());
#else
  std::fprintf(
      stderr, "integration active without MCP: no listener, no descriptor\n");
#endif
  std::fflush(stdout);

  if (exitAfterActivation)
    return 0;

  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  // Service integration work and pump pending renders until interrupted.
  while (!g_stop) {
    integration.service();
#ifdef VSR_USE_INTEGRATION_MCP
    host->images->pump();
#endif
    host->translations->pump();
    host->renders->pump();
    if (!g_stop)
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  std::fprintf(stderr, "integration stopped\n");
  return 0;
}

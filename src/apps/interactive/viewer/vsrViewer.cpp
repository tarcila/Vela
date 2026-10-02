// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_io
#include <vsr/io/procedural.hpp>
// vsr_ui_imgui
#include <vsr/ui/imgui/Application.h>
#include <vsr/ui/imgui/windows/Animations.h>
#include <vsr/ui/imgui/windows/CameraPoses.h>
#include <vsr/ui/imgui/windows/DatabaseEditor.h>
#include <vsr/ui/imgui/windows/IsosurfaceEditor.h>
#include <vsr/ui/imgui/windows/LayerTree.h>
#include <vsr/ui/imgui/windows/Log.h>
#include <vsr/ui/imgui/windows/ObjectEditor.h>
#include <vsr/ui/imgui/windows/Terminal.h>
#include <vsr/ui/imgui/windows/Timeline.h>
#include <vsr/ui/imgui/windows/TransferFunctionEditor.h>
#include <vsr/ui/imgui/windows/Viewport.h>
#ifdef VSR_USE_INTEGRATION
#include <vsr/ui/imgui/windows/IntegrationActivity.h>
// vsr_integration
#include <vsr/integration/activity/Activity.hpp>
#include <vsr/integration/context/ContextAdapter.hpp>
#include <vsr/integration/identity/Identity.hpp>
#include <vsr/integration/inspection/Inspection.hpp>
#include <vsr/integration/render/RevisionRender.hpp>
#ifdef VSR_USE_INTEGRATION_MCP
#include <vsr/integration/descriptor/LaunchDescriptor.hpp>
#include <vsr/integration/mcp/McpServer.hpp>
#include <vsr/integration/mcp/tools/EditTools.hpp>
#include <vsr/integration/mcp/tools/LookupTools.hpp>
#include <vsr/integration/mcp/tools/QueryTools.hpp>
#include <vsr/integration/mcp/tools/RenderTools.hpp>
#endif
// vsr_rendering
#include <vsr/rendering/pipeline/ImagePipeline.h>
#include <vsr/rendering/view/ManipulatorToAnari.hpp>
#endif

// std
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

namespace vsr_viewer {

using VSRApplication = vsr::ui::imgui::Application;
namespace vsr_ui = vsr::ui::imgui;

// Whether --secondaryView was given decides if this viewport is shown, so the
// UI State restores its other settings but not its visibility.
struct SecondaryViewport : public vsr_ui::Viewport
{
  using vsr_ui::Viewport::Viewport;

  void loadSettings(vsr::core::DataNode &thisWindowRoot) override
  {
    const bool visible = m_visible;
    vsr_ui::Viewport::loadSettings(thisWindowRoot);
    m_visible = visible;
  }
};

#ifdef VSR_USE_INTEGRATION
// The agent-renderable view: a helide device with its own offscreen Image
// Pipeline, independent of the interactive viewports. Its camera frames the
// scene's world bounds like the viewport's reset-view (addendum 08), posed
// once the bounds are known and fixed afterwards; it is not a scene object,
// so it never appears in (or pollutes) the user's scene.
struct AgentView : vsr::integration::render::RenderBackend
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
  bool framed{false};

  // Poses the camera on the world bounds if they are known and it has not
  // been posed yet; true when this call posed it.
  bool frameSceneBounds()
  {
    if (framed || !device || !index || !camera || !index->world())
      return false;
    vsr::math::float3 b[2];
    if (!anariGetProperty(device,
            index->world(),
            "bounds",
            ANARI_FLOAT32_BOX3,
            &b[0],
            sizeof(b),
            ANARI_WAIT))
      return false;
    for (int i = 0; i < 3; i++) {
      if (!std::isfinite(b[0][i]) || !std::isfinite(b[1][i])
          || b[0][i] > b[1][i])
        return false;
    }
    vsr::rendering::Manipulator pose;
    pose.setConfig(index->computeDefaultView());
    vsr::rendering::updateCameraParametersPerspective(device, camera, pose);
    anari::commitParameters(device, camera);
    framed = true;
    return true;
  }

  bool setup(vsr::app::Context &c)
  {
    ctx = &c;
    auto &scene = c.vsr.scene;
    device = c.anari.loadDevice("helide");
    if (!device)
      return false;
    index = c.anari.acquireRenderIndex(scene, "helide", device);
    if (!index)
      return false;

    camera = anari::newObject<anari::Camera>(device, "perspective");
    if (auto cam = scene.defaultCamera())
      anari::setParameter(device,
          camera,
          "fovy",
          cam->parameterValueAs<float>("fovy").value_or(
              vsr::math::radians(40.f)));
    anari::commitParameters(device, camera);
    renderer = anari::newObject<anari::Renderer>(device, "default");
    anari::commitParameters(device, renderer);

    pipeline.setDimensions(WIDTH, HEIGHT);
    pass = pipeline.setSource<vsr::rendering::AnariSceneRenderPass>(device);
    pass->setRunAsync(false);
    pass->setCamera(camera);
    pass->setRenderer(renderer);
    if (!confirm())
      return false;
    frameSceneBounds();
    return true;
  }

  // Binds the index's current world into the pipeline; true only when the
  // view's RenderIndex and pipeline are live for the current scene.
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
  vsr::integration::render::BackendState poll() override
  {
    return rendered ? vsr::integration::render::BackendState::DONE
                    : vsr::integration::render::BackendState::FAULT;
  }
  vsr::integration::image::SourceImage image() override
  {
    namespace img = vsr::integration::image;
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
#endif

class Application : public VSRApplication
{
 public:
  // `argv` must already have `--agentIntegration` stripped (see main()):
  // Context would otherwise take the unknown flag for a state file and
  // skip both the default scene and command-line imports.
  Application(int argc, const char *argv[], bool agentIntegrationRequested)
      : VSRApplication(argc, argv)
  {
#ifdef VSR_USE_INTEGRATION
    // Opt-in: nothing is exposed to agents unless the operator asks.
    m_agentIntegrationRequested = agentIntegrationRequested;
#else
    (void)agentIntegrationRequested;
#endif
  }
  ~Application() override = default;

  vsr::core::DataTreeMetadata applicationStateMetadata() const override
  {
    return {vsr::core::DATA_TREE_METADATA_ENVELOPE_VERSION,
        "application-state",
        "vsr.viewer.state",
        1};
  }

  bool validateApplicationStateMetadata(
      const vsr::core::DataTreeMetadataReadResult &metadata,
      const vsr::core::DataNode &root,
      const char *filename) const override
  {
    if (metadata.status == vsr::core::DataTreeMetadataReadStatus::Missing) {
      if (auto *projectKindNode = root.child("projectKind")) {
        const auto projectKind = projectKindNode->getValueOr<std::string>("");
        vsr::core::logError(
            "[vsrViewer] Refusing to load '%s': projectKind='%s' is not a "
            "vsrViewer state file",
            filename,
            projectKind.c_str());
        return false;
      }

      vsr::core::logWarning(
          "[vsrViewer] State file '%s' has no __vsr_metadata; loading as "
          "legacy viewer state",
          filename);
      return true;
    }

    if (metadata.status == vsr::core::DataTreeMetadataReadStatus::Malformed) {
      vsr::core::logError("[vsrViewer] Refusing to load state file '%s': %s",
          filename,
          metadata.message.c_str());
      return false;
    }

    const auto &stateMetadata = *metadata.metadata;
    if (stateMetadata.fileType != "application-state"
        || stateMetadata.schema != "vsr.viewer.state") {
      vsr::core::logError(
          "[vsrViewer] Refusing to load '%s': expected vsr.viewer.state, got "
          "fileType='%s' schema='%s'",
          filename,
          stateMetadata.fileType.c_str(),
          stateMetadata.schema.c_str());
      return false;
    }

    return true;
  }

  vsr::ui::imgui::WindowArray setupWindows() override
  {
    auto windows = VSRApplication::setupWindows();

    auto *ctx = appContext();

    auto *viewport =
        new vsr_ui::Viewport(this, &ctx->view.manipulator, "Viewport");
    auto *viewport2 =
        new SecondaryViewport(this, &ctx->view.manipulator, "Secondary View");
    auto *animations = new vsr_ui::Animations(this);
    auto *timeline = new vsr_ui::Timeline(this);
    auto *cameras = new vsr_ui::CameraPoses(this);
    auto *log = new vsr_ui::Log(this);
    auto *dbeditor = new vsr_ui::DatabaseEditor(this);
    auto *oeditor = new vsr_ui::ObjectEditor(this);
    auto *otree = new vsr_ui::LayerTree(this);
    auto *tfeditor = new vsr_ui::TransferFunctionEditor(this);
    auto *isoeditor = new vsr_ui::IsosurfaceEditor(this);
    auto *terminal = new vsr_ui::Terminal(this);

    windows.emplace_back(animations);
    windows.emplace_back(timeline);
    windows.emplace_back(cameras);
    windows.emplace_back(viewport);
    windows.emplace_back(viewport2);
    windows.emplace_back(dbeditor);
    windows.emplace_back(oeditor);
    windows.emplace_back(otree);
    windows.emplace_back(tfeditor);
    windows.emplace_back(isoeditor);
    windows.emplace_back(terminal);
    windows.emplace_back(log);
#ifdef VSR_USE_INTEGRATION
    m_activityWindow = new vsr_ui::IntegrationActivity(this, {});
    windows.emplace_back(m_activityWindow);
    if (!m_agentIntegrationRequested)
      m_activityWindow->hide();
#endif

    setWindowArray(windows);

    if (commandLineOptions()->secondaryViewportLibrary.empty())
      viewport2->hide();
    tfeditor->hide();
    isoeditor->hide();

    // Populate scene //

    auto populateScene = [this, vp = viewport, vp2 = viewport2, ctx = ctx]() {
      auto loadStart = std::chrono::steady_clock::now();
      ctx->setupSceneFromCommandLine();
      auto loadEnd = std::chrono::steady_clock::now();
      auto loadSeconds =
          std::chrono::duration<float>(loadEnd - loadStart).count();

      auto &scene = ctx->vsr.scene;

      const bool setupDefaultLight = !ctx->commandLine.loadedFromStateFile
          && scene.numberOfObjects(ANARI_LIGHT) == 0;
      if (setupDefaultLight) {
        vsr::core::logStatus("...setting up default light");
        vsr::io::generate_default_lights(scene);
      }

      vsr::core::logStatus("...scene load complete! (%.3fs)", loadSeconds);
      vsr::core::logStatus(
          "%s", vsr::scene::objectDBInfo(scene.objectDB()).c_str());
      ctx->vsr.sceneLoadComplete = true;

      auto setupDefaultRenderer = !ctx->commandLine.loadedFromStateFile
          && commandLineOptions()->useDefaultRenderer;
      if (setupDefaultRenderer) {
        vp->setLibraryToDefault();
        if (!commandLineOptions()->secondaryViewportLibrary.empty())
          vp2->setLibrary(commandLineOptions()->secondaryViewportLibrary);
      }
    };

    showTaskModal(populateScene, "Please Wait: Loading Scene...");

    return windows;
  }

  void teardown() override
  {
    if (m_sceneLoadFuture.valid())
      m_sceneLoadFuture.get();
#ifdef VSR_USE_INTEGRATION
    teardownIntegration();
#endif
    VSRApplication::teardown();
  }

  const char *applicationIdentifier() const override
  {
    return "vsrViewer";
  }

#ifdef VSR_USE_INTEGRATION
  void uiFrameStart() override
  {
    VSRApplication::uiFrameStart();
    if (m_agentIntegrationRequested && !m_integration
        && appContext()->vsr.sceneLoadComplete)
      setupIntegration();
    // Viewer safe point: queued agent and local work runs here, between
    // frames, so the UI stays interactive while work is outstanding.
    // Pose the agent camera on the first frame with scene bounds; the new
    // pose changes the view revision (addendum 08).
    if (m_agentView && m_tracker && m_agentView->frameSceneBounds())
      m_tracker->notePipelineChanged();
    if (m_serviceHook)
      (*m_serviceHook)();
    if (m_renders)
      m_renders->pump();
  }
#endif

  const char *getDefaultLayout() const override
  {
    return R"layout(
[Window][MainDockSpace]
Pos=0,56
Size=3840,2206
Collapsed=0

[Window][Debug##Default]
Pos=60,60
Size=400,400
Collapsed=0

[Window][Viewport]
Pos=901,56
Size=2939,1524
Collapsed=0
DockId=0x00000006,0

[Window][Database Editor]
Pos=0,1207
Size=899,1055
Collapsed=0
DockId=0x00000009,1

[Window][Layers]
Pos=0,56
Size=899,1149
Collapsed=0
DockId=0x00000008,0

[Window][Object Editor]
Pos=0,1207
Size=899,1055
Collapsed=0
DockId=0x00000009,0

[Window][Log]
Pos=901,1582
Size=2939,680
Collapsed=0
DockId=0x00000005,0

[Window][Terminal]
Pos=901,1582
Size=2939,680
Collapsed=0
DockId=0x00000005,1

[Window][Secondary View]
Pos=1237,26
Size=683,848
Collapsed=0
DockId=0x00000007,0

[Window][Isosurface Editor]
Pos=1370,26
Size=550,1054
Collapsed=0
DockId=0x0000000C,0

[Window][TF Editor]
Pos=2483,26
Size=550,1769
Collapsed=0
DockId=0x0000000B,0

[Window][Camera Poses]
Pos=0,56
Size=899,1149
Collapsed=0
DockId=0x00000008,2

[Window][Animations]
Pos=0,56
Size=899,1149
Collapsed=0
DockId=0x00000008,1

[Window][##]
Pos=792,507
Size=336,116
Collapsed=0

[Window][Timeline]
Pos=901,1582
Size=2939,680
Collapsed=0
DockId=0x00000005,2

[Window][##blocking_task_modal]
Pos=792,482
Size=16,72
Collapsed=0

[Window][Cutting Plane]
Pos=1293,781
Size=446,232
Collapsed=0

[Table][0x44C159D3,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0x9E1800B1,1]
Column 0  Weight=1.0000

[Table][0xFAE9835A,1]
Column 0  Weight=1.0000

[Table][0x413D162D,1]
Column 0  Weight=1.0000

[Table][0x34853C34,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xEEE697AB,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0x50507568,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xF4075185,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xF945472E,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xC7D50986,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0x7AB8FCE0,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xC6287F21,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0x3DC35996,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0x7C7C00B3,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0x7B5A3115,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Docking][Data]
DockSpace         ID=0x80F5B4C5 Window=0x079D3A04 Pos=0,56 Size=3840,2206 Split=X
  DockNode        ID=0x00000003 Parent=0x80F5B4C5 SizeRef=1368,1054 Split=X
    DockNode      ID=0x00000001 Parent=0x00000003 SizeRef=899,1105 Split=Y Selected=0xCD8384B1
      DockNode    ID=0x00000008 Parent=0x00000001 SizeRef=547,575 Selected=0xCD8384B1
      DockNode    ID=0x00000009 Parent=0x00000001 SizeRef=547,528 Selected=0x82B4C496
    DockNode      ID=0x00000002 Parent=0x00000003 SizeRef=2939,1105 Split=Y
      DockNode    ID=0x00000004 Parent=0x00000002 SizeRef=1370,1524 Split=X Selected=0xC450F867
        DockNode  ID=0x00000006 Parent=0x00000004 SizeRef=685,848 CentralNode=1 Selected=0xC450F867
        DockNode  ID=0x00000007 Parent=0x00000004 SizeRef=683,848 Selected=0xA3219422
      DockNode    ID=0x00000005 Parent=0x00000002 SizeRef=1370,680 Selected=0x139FDA3F
  DockNode        ID=0x0000000A Parent=0x80F5B4C5 SizeRef=550,1054 Split=Y Selected=0x3429FA32
    DockNode      ID=0x0000000B Parent=0x0000000A SizeRef=550,590 Selected=0x3429FA32
    DockNode      ID=0x0000000C Parent=0x0000000A SizeRef=550,462 Selected=0xBCE6538B
)layout";
  }

 private:
#ifdef VSR_USE_INTEGRATION
  void setupIntegration();
  void teardownIntegration();

  bool m_agentIntegrationRequested{false};
  vsr_ui::IntegrationActivity *m_activityWindow{nullptr};
  std::unique_ptr<vsr::integration::Integration> m_integration;
  std::unique_ptr<vsr::integration::session::SessionAuthority> m_authority;
  std::unique_ptr<vsr::integration::operations::OperationEngine> m_engine;
  std::unique_ptr<vsr::integration::identity::IdentityAuthority> m_ids;
  std::unique_ptr<vsr::integration::identity::SceneIdentity> m_identity;
  std::unique_ptr<vsr::integration::edit::EditService> m_edits;
  std::unique_ptr<vsr::integration::restore::RestoreService> m_restores;
  std::unique_ptr<vsr::integration::sync::SyncService> m_sync;
  std::unique_ptr<vsr::integration::activity::ActivityLog> m_activity;
  std::unique_ptr<vsr::integration::activity::EngineActivitySink>
      m_activitySink;
  std::unique_ptr<vsr::integration::context::ViewerExecutionHook> m_serviceHook;
  std::unique_ptr<AgentView> m_agentView;
  std::unique_ptr<vsr::integration::identity::ViewRevisionTracker> m_tracker;
  std::unique_ptr<vsr::integration::render::RenderService> m_renders;
  std::unique_ptr<vsr::integration::inspection::Inspector> m_inspector;
#ifdef VSR_USE_INTEGRATION_MCP
  vsr::integration::descriptor::RuntimeDirectory m_runtimeDir;
  std::unique_ptr<vsr::integration::mcp::McpServer> m_mcp;
#endif
#endif
  std::future<void> m_sceneLoadFuture;
};

#ifdef VSR_USE_INTEGRATION
void Application::setupIntegration()
{
  namespace vi = vsr::integration;
  auto *ctx = appContext();

  m_agentView = std::make_unique<AgentView>();
  if (!m_agentView->setup(*ctx)) {
    vsr::core::logError("[vsrViewer] agent view setup failed (helide)");
    teardownIntegration();
    m_agentIntegrationRequested = false;
    return;
  }

  m_integration = std::make_unique<vi::Integration>();

  // Both hooks confirm the agent view's RenderIndex/Image Pipeline for the
  // current scene (it is the only registered view); recovery rebuilds it.
  vi::context::ContextSceneOptions options;
  options.name = "scene";
  options.grants.read = true;
  options.grants.edit = true;
  options.nodeEdits = true;
  options.grants.resynchronize = true;
  auto *view = m_agentView.get();
  options.normalSyncHook = [view](vi::SceneId) { return view->synchronize(); };
  options.recoveryCallback = [view](vi::SceneId) { return view->recover(); };
  auto sceneId =
      vi::context::registerContextScene(*m_integration, *ctx, options);
  std::optional<vi::ViewId> viewId;
  if (sceneId) {
    vi::ViewRegistration vr;
    vr.name = "agent";
    vr.scene = *sceneId;
    vr.grants = {true, true};
    viewId = m_integration->registerView(vr);
  }
  auto activation = viewId ? m_integration->activate() : vi::ActivationResult{};
  if (!viewId || !activation.ok()) {
    vsr::core::logError("[vsrViewer] agent integration activation failed");
    teardownIntegration();
    m_agentIntegrationRequested = false;
    return;
  }

  m_authority = vi::session::SessionAuthority::create(*m_integration);
  if (m_authority)
    m_engine =
        vi::operations::OperationEngine::create(*m_integration, *m_authority);
  if (!m_engine) {
    vsr::core::logError("[vsrViewer] agent integration setup failed");
    teardownIntegration();
    m_agentIntegrationRequested = false;
    return;
  }
  m_ids = std::make_unique<vi::identity::IdentityAuthority>();
  m_identity =
      std::make_unique<vi::identity::SceneIdentity>(*m_ids, ctx->vsr.scene);
  m_edits = std::make_unique<vi::edit::EditService>(
      *m_integration, *m_engine, m_authority->launchId());
  m_edits->attachScene(*sceneId, *m_identity);
  m_restores =
      std::make_unique<vi::restore::RestoreService>(*m_engine, *m_edits);
  m_restores->attachScene(*sceneId, *m_identity);
  m_sync = std::make_unique<vi::sync::SyncService>(
      *m_integration, *m_engine, m_edits->syncStates());
  m_activity = std::make_unique<vi::activity::ActivityLog>();
  m_activitySink = std::make_unique<vi::activity::EngineActivitySink>(
      *m_activity, *m_integration);
  m_engine->setActivitySink(m_activitySink.get());
  m_tracker =
      std::make_unique<vi::identity::ViewRevisionTracker>(*m_ids, *m_identity);
  m_renders = std::make_unique<vi::render::RenderService>(
      *m_integration, *m_engine, *m_sync, *m_activity);
  if (!m_renders->attachView(*viewId, *m_identity, *m_tracker, *m_agentView)) {
    vsr::core::logError("[vsrViewer] agent view attach failed");
    teardownIntegration();
    m_agentIntegrationRequested = false;
    return;
  }
  m_renders->setDimensions(*viewId, AgentView::WIDTH, AgentView::HEIGHT);
  auto *identity = m_identity.get();
  auto *tracker = m_tracker.get();
  m_inspector = std::make_unique<vi::inspection::Inspector>(
      *m_integration, *m_authority, [identity, sid = *sceneId](vi::SceneId id) {
        return id == sid ? identity : nullptr;
      });
  m_inspector->setViewRevisions([tracker, vid = *viewId](vi::ViewId id) {
    return id == vid ? tracker : nullptr;
  });
  m_serviceHook =
      std::make_unique<vi::context::ViewerExecutionHook>(*m_integration);

  vsr_ui::IntegrationServices services;
  services.integration = m_integration.get();
  services.authority = m_authority.get();
  services.activity = m_activity.get();
  services.sync = m_sync.get();
  services.restore = m_restores.get();
  services.scenes.emplace_back(*sceneId, options.name);

#ifdef VSR_USE_INTEGRATION_MCP
  vi::mcp::Config cfg;
  vi::mcp::tools::addQueryTools(cfg.tools, {*m_engine, *m_inspector});
  vi::mcp::tools::addLookupTools(cfg.tools, *m_engine);
  vi::mcp::tools::addEditTools(cfg.tools, {*m_ids, *m_edits, *m_restores});
  vi::mcp::tools::addRenderTools(cfg.tools, {*m_renders, *m_sync, *m_engine});
  cfg.log = [](std::string_view line) {
    vsr::core::logStatus("[vsrViewer] %.*s", int(line.size()), line.data());
  };
  if (auto st = vi::descriptor::openOsRuntimeDirectory(m_runtimeDir); !st.ok())
    vsr::core::logError("[vsrViewer] runtime directory: %s", st.reason.c_str());
  else if (auto st = vi::mcp::McpServer::start(
               cfg, *m_authority, *m_engine, m_runtimeDir, m_mcp);
      !st.ok())
    vsr::core::logError("[vsrViewer] MCP listener: %s", st.reason.c_str());
  else
    services.descriptorPath = m_mcp->published().path();
#endif

  m_activityWindow->model().setServices(std::move(services));
  m_activityWindow->show();
  vsr::core::logStatus("[vsrViewer] agent integration active");
}

void Application::teardownIntegration()
{
  if (m_activityWindow)
    m_activityWindow->model().setServices({});
#ifdef VSR_USE_INTEGRATION_MCP
  m_mcp.reset();
#endif
  m_serviceHook.reset();
  m_inspector.reset();
  if (m_agentView)
    m_agentView->release();
  m_renders.reset();
  m_tracker.reset();
  if (m_engine)
    m_engine->setActivitySink(nullptr);
  m_sync.reset();
  m_restores.reset();
  m_edits.reset();
  m_identity.reset();
  m_engine.reset();
  m_authority.reset();
  m_activitySink.reset();
  m_activity.reset();
  m_ids.reset();
  m_integration.reset();
  if (m_agentView)
    m_agentView->teardown();
  m_agentView.reset();
}
#endif

} // namespace vsr_viewer

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int main(int argc, const char *argv[])
{
  // vsrViewer owns `--agentIntegration`; remove it before the base
  // application and Context parse the remaining arguments.
  bool agentIntegrationRequested = false;
  std::vector<const char *> args;
  args.reserve(argc);
  for (int i = 0; i < argc; i++) {
    if (i > 0 && std::strcmp(argv[i], "--agentIntegration") == 0)
      agentIntegrationRequested = true;
    else
      args.push_back(argv[i]);
  }

  {
    vsr_viewer::Application app(
        int(args.size()), args.data(), agentIntegrationRequested);
    app.run(1920, 1080, "VSR Viewer");
  }

  return 0;
}

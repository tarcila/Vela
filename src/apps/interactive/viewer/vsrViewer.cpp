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
#include <vsr/integration/edit/Translation.hpp>
#include <vsr/integration/frame/FrameObservation.hpp>
#include <vsr/integration/frame/ViewerFrameSource.hpp>
#include <vsr/integration/identity/Identity.hpp>
#include <vsr/integration/inspection/Inspection.hpp>
#include <vsr/integration/render/RevisionRender.hpp>
#ifdef VSR_USE_INTEGRATION_MCP
#include <vsr/integration/descriptor/LaunchDescriptor.hpp>
#include <vsr/integration/mcp/McpServer.hpp>
#include <vsr/integration/mcp/tools/EditTools.hpp>
#include <vsr/integration/mcp/tools/FrameTools.hpp>
#include <vsr/integration/mcp/tools/LookupTools.hpp>
#include <vsr/integration/mcp/tools/QueryTools.hpp>
#include <vsr/integration/mcp/tools/RenderTools.hpp>
#endif
// vsr_rendering
#include <vsr/rendering/pipeline/ImagePipeline.h>
#include <vsr/rendering/view/ManipulatorToAnari.hpp>
#endif

// std
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
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
// The agent-renderable view: an ANARI device with its own offscreen Image
// Pipeline, independent of the interactive viewports. The device follows the
// viewer's default selection (vsr::app::defaultDeviceIdentifier(): first
// VSR_ANARI_LIBRARIES entry, else ANARI_LIBRARY, else helide); an agent may
// override it among deviceCandidates() through vsr_select_device. Its camera
// frames the scene's world bounds like the viewport's reset-view (addendum
// 08), posed once the bounds are known and fixed afterwards; it is not a
// scene object, so it never appears in (or pollutes) the user's scene.
struct AgentView : vsr::integration::render::RenderBackend
{
  static constexpr uint32_t WIDTH = 640;
  static constexpr uint32_t HEIGHT = 360;
  // Frames accumulated per agent render. Progressive devices (VisRTX,
  // barney, ...) converge over unchanged frames; the others just redraw.
  static constexpr int ACCUMULATION_FRAMES = 32;

  // Everything bound to one ANARI device. A device switch builds a new set
  // and swaps it in only once it is complete, so a failed switch leaves the
  // current device rendering.
  struct DeviceResources
  {
    std::string identifier;
    anari::Device device{nullptr};
    vsr::rendering::RenderIndex *index{nullptr};
    anari::Camera camera{nullptr};
    anari::Renderer renderer{nullptr};
    std::unique_ptr<vsr::rendering::ImagePipeline> pipeline;
    vsr::rendering::AnariSceneRenderPass *pass{nullptr};
    bool denoise{false}; // the renderer advertises and enables "denoise"
  };

  // Whether `d`'s default renderer advertises parameter `name` of `type`.
  static bool rendererHasParameter(
      anari::Device d, const char *name, anari::DataType type)
  {
    auto *p = (const ANARIParameter *)anariGetObjectInfo(
        d, ANARI_RENDERER, "default", "parameter", ANARI_PARAMETER_LIST);
    for (; p && p->name; ++p) {
      if (p->type == type && std::strcmp(p->name, name) == 0)
        return true;
    }
    return false;
  }

  vsr::app::Context *ctx{nullptr};
  DeviceResources res;
  float fovy{vsr::math::radians(40.f)};
  // The current pose: framed on the scene bounds once they are known, or set
  // by an agent (which then wins over framing). Reapplied to a replacement
  // device's camera.
  struct Pose
  {
    vsr::math::float3 eye{0.f};
    vsr::math::float3 dir{0.f, 0.f, -1.f};
    vsr::math::float3 up{0.f, 1.f, 0.f};
  };
  std::optional<Pose> pose;
  bool rendered{false};
  vsr::integration::EffectiveViewBinding binding;
  std::optional<vsr::integration::EffectiveViewBinding> renderedConfiguration;

  // Poses the camera on the world bounds if they are known and it has not
  // been posed yet; true when this call posed it.
  bool frameSceneBounds()
  {
    if (pose || !res.device || !res.index || !res.camera
        || !res.index->world())
      return false;
    vsr::math::float3 b[2];
    if (!anariGetProperty(res.device,
            res.index->world(),
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
    vsr::rendering::Manipulator framing;
    framing.setConfig(res.index->computeDefaultView());
    setPose({framing.eye(), framing.dir(), framing.up()}, "framed");
    return true;
  }

  // Applies `p` (and the current fovy) to the bound device and records it
  // as the view's effective camera.
  void setPose(const Pose &p, const char *revision)
  {
    pose = p;
    applyPose(res);
    binding.camera.position = {p.eye.x, p.eye.y, p.eye.z};
    binding.camera.direction = {p.dir.x, p.dir.y, p.dir.z};
    binding.camera.up = {p.up.x, p.up.y, p.up.z};
    binding.camera.fovyDegrees = vsr::math::degrees(fovy);
    binding.pipelineConfigurationRevision = revision;
  }

  void applyPose(DeviceResources &r) const
  {
    if (!r.device || !r.camera)
      return;
    anari::setParameter(r.device, r.camera, "fovy", fovy);
    if (pose) {
      anari::setParameter(r.device, r.camera, "position", pose->eye);
      anari::setParameter(r.device, r.camera, "direction", pose->dir);
      anari::setParameter(r.device, r.camera, "up", pose->up);
    }
    anari::commitParameters(r.device, r.camera);
  }

  // RenderBackend camera control: the agent's pose replaces the framing.
  bool cameraControllable() const override
  {
    return true;
  }

  bool setCamera(const std::array<double, 3> &position,
      const std::array<double, 3> &direction,
      const std::array<double, 3> &up,
      double fovyDegrees) override
  {
    if (!res.device || !res.camera)
      return false;
    auto f3 = [](const std::array<double, 3> &v) {
      return vsr::math::float3(float(v[0]), float(v[1]), float(v[2]));
    };
    if (fovyDegrees > 0.)
      fovy = vsr::math::radians(float(fovyDegrees));
    setPose({f3(position),
                vsr::math::normalize(f3(direction)),
                vsr::math::normalize(f3(up))},
        "agent_camera");
    rendered = false;
    renderedConfiguration.reset();
    return true;
  }

  // Loads `identifier` and builds its index, camera, renderer and pipeline.
  // On failure everything acquired is released and `out` stays empty.
  bool acquire(const std::string &identifier, DeviceResources &out)
  {
    DeviceResources r;
    r.identifier = identifier;
    r.device = ctx->anari.loadDevice(identifier);
    if (!r.device)
      return false;
    r.index = ctx->anari.acquireRenderIndex(
        ctx->vsr.scene, vsr::core::Token(identifier), r.device);
    if (!r.index) {
      release(r);
      return false;
    }

    r.camera = anari::newObject<anari::Camera>(r.device, "perspective");
    anari::setParameter(r.device, r.camera, "fovy", fovy);
    anari::setParameter(
        r.device, r.camera, "position", vsr::math::float3(0.f));
    anari::setParameter(
        r.device, r.camera, "direction", vsr::math::float3(0.f, 0.f, -1.f));
    anari::setParameter(
        r.device, r.camera, "up", vsr::math::float3(0.f, 1.f, 0.f));
    applyPose(r);
    r.renderer = anari::newObject<anari::Renderer>(r.device, "default");
    // Denoise the accumulated frame where the device offers it.
    r.denoise = rendererHasParameter(r.device, "denoise", ANARI_BOOL);
    if (r.denoise)
      anari::setParameter(r.device, r.renderer, "denoise", true);
    anari::commitParameters(r.device, r.renderer);

    r.pipeline = std::make_unique<vsr::rendering::ImagePipeline>();
    r.pipeline->setDimensions(WIDTH, HEIGHT);
    r.pass =
        r.pipeline->setSource<vsr::rendering::AnariSceneRenderPass>(r.device);
    r.pass->setRunAsync(false);
    r.pass->setCamera(r.camera);
    r.pass->setRenderer(r.renderer);
    if (!r.index->world()) {
      release(r);
      return false;
    }
    r.pass->setWorld(r.index->world());
    out = std::move(r);
    return true;
  }

  void release(DeviceResources &r)
  {
    if (r.pass)
      r.pass->waitForCompletion();
    r.pipeline.reset();
    r.pass = nullptr;
    if (r.device) {
      if (r.camera)
        anari::release(r.device, r.camera);
      if (r.renderer)
        anari::release(r.device, r.renderer);
      if (r.index)
        ctx->anari.releaseRenderIndex(ctx->vsr.scene, r.device);
      anari::release(r.device, r.device);
    }
    r = DeviceResources{};
  }

  void bindDevice()
  {
    const auto id = vsr::app::DeviceIdentifier::parse(res.identifier);
    binding.deviceIdentifier = res.identifier;
    binding.deviceSubtype = id ? id->subtype : std::string("default");
    binding.rendererSettings.clear();
    if (res.denoise)
      binding.rendererSettings.push_back({"denoise", {1.}, {}});
    binding.rendererConfigurationRevision =
        res.denoise ? "independent-denoise" : "independent-default";
  }

  bool setup(vsr::app::Context &c)
  {
    ctx = &c;
    const auto cam = c.vsr.scene.defaultCamera();
    fovy = cam ? cam->parameterValueAs<float>("fovy").value_or(
                     vsr::math::radians(40.f))
               : vsr::math::radians(40.f);
    binding.camera.fovyDegrees = vsr::math::degrees(fovy);
    binding.width = WIDTH;
    binding.height = HEIGHT;
    binding.rendererSubtype = "default";
    binding.rendererSettingsReason = "backend_defaults_not_observed";
    binding.pipelineSource = "independent_agent_image_pipeline";
    binding.pipelineConfigurationRevision = "unframed";
    binding.pipelineSettings = {{"async", {0.}, {}},
        {"depth", {0.}, {}},
        {"accumulation_frames", {double(ACCUMULATION_FRAMES)}, {}}};
    binding.outputColor = "rgba8_srgb";

    const auto preferred = vsr::app::defaultDeviceIdentifier();
    if (!acquire(preferred, res)) {
      vsr::core::logWarning(
          "[vsrViewer] agent view: failed to load ANARI device '%s'; falling"
          " back to '%s'",
          preferred.c_str(),
          vsr::app::FALLBACK_DEVICE_IDENTIFIER);
      if (preferred == vsr::app::FALLBACK_DEVICE_IDENTIFIER
          || !acquire(vsr::app::FALLBACK_DEVICE_IDENTIFIER, res))
        return false;
    }
    bindDevice();
    vsr::core::logStatus("[vsrViewer] agent view renders with ANARI device '%s'",
        res.identifier.c_str());
    frameSceneBounds();
    return true;
  }

  // Binds the index's current world into the pipeline; true only when the
  // view's RenderIndex and pipeline are live for the current scene.
  bool confirm()
  {
    if (!res.device || !res.index || !res.pass || res.pipeline->empty())
      return false;
    auto world = res.index->world();
    if (!world)
      return false;
    res.pass->setWorld(world);
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
    if (!res.index)
      return false;
    res.index->populate();
    return confirm();
  }

  void teardown()
  {
    release(res);
  }

  // RenderBackend device selection: the viewer's device list (what its
  // viewports offer), with "environment" resolved to ANARI_LIBRARY and the
  // fallback always present.
  std::vector<std::string> deviceCandidates() const override
  {
    std::vector<std::string> list;
    auto add = [&](const std::string &id) {
      if (vsr::app::DeviceIdentifier::parse(id)
          && std::find(list.begin(), list.end(), id) == list.end())
        list.push_back(id);
    };
    for (const auto &entry : ctx->anari.deviceList()) {
      if (entry == "environment") {
        if (const char *env = getenv("ANARI_LIBRARY"); env && *env)
          add(env);
      } else
        add(entry);
    }
    add(vsr::app::FALLBACK_DEVICE_IDENTIFIER);
    add(res.identifier);
    return list;
  }

  std::string deviceIdentifier() const override
  {
    return res.identifier;
  }

  bool selectDevice(const std::string &identifier) override
  {
    if (identifier == res.identifier)
      return true;
    // Build the replacement before releasing the current set. Identifiers
    // naming the same device share it (and its index) by reference count.
    DeviceResources next;
    if (!acquire(identifier, next)) {
      vsr::core::logWarning(
          "[vsrViewer] agent view: failed to switch to ANARI device '%s';"
          " keeping '%s'",
          identifier.c_str(),
          res.identifier.c_str());
      return false;
    }
    release(res);
    res = std::move(next);
    bindDevice();
    rendered = false;
    renderedConfiguration.reset();
    vsr::core::logStatus("[vsrViewer] agent view switched to ANARI device '%s'",
        res.identifier.c_str());
    return true;
  }

  // RenderBackend: one synchronous pipeline frame at the view's dimensions.
  bool begin(std::uint32_t width, std::uint32_t height) override
  {
    if (!confirm())
      return false;
    res.pipeline->setDimensions(width, height);
    // Synchronous pass: each render() renders and waits for one frame of
    // the current state. Unchanged frames accumulate on progressive devices,
    // so the last one carries ACCUMULATION_FRAMES samples (then denoised).
    for (int i = 0; i < ACCUMULATION_FRAMES; ++i)
      res.pipeline->render();
    rendered = res.pipeline->getColorBuffer() != nullptr;
    renderedConfiguration.reset();
    if (rendered) {
      renderedConfiguration = binding;
      renderedConfiguration->width = width;
      renderedConfiguration->height = height;
    }
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
    const auto size = res.pipeline->dimensions();
    src.width = size.x;
    src.height = size.y;
    src.encoding = img::ChannelEncoding::SRGB_UINT8;
    src.alpha = img::AlphaMode::STRAIGHT;
    src.origin = img::RowOrigin::BOTTOM_LEFT;
    src.pixels = res.pipeline->getColorBuffer();
    return src;
  }
  std::optional<vsr::integration::EffectiveViewBinding> renderedBinding()
      const override
  {
    return renderedConfiguration;
  }
  bool release() override
  {
    if (res.pass)
      res.pass->waitForCompletion();
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
#ifdef VSR_USE_INTEGRATION
    m_viewport = viewport;
    m_secondaryViewport = viewport2;
#endif
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
    if (m_integration && m_identity) {
      m_integration->submit(0, [this](vsr::scene::Scene &) {
        namespace info = vsr::integration::inspection::viewinfo;
        registerViewportBindings();
        for (auto &entry : m_viewportTrackers) {
          auto binding = m_integration->observeViewBinding(entry.first);
          const auto reason =
              binding ? info::validate(*binding) : "missing_binding";
          entry.second->observeConfiguration(reason
                  ? std::string("\"state\":\"unavailable\",\"reason\":\"")
                      + reason + "\""
                  : info::bindingJson(*binding,
                        vsr::integration::inspection::detail::jsonString));
          if (binding && !reason) {
            entry.second->setCamera(binding->cameraHandle);
            entry.second->setRenderer(binding->rendererHandle);
            entry.second->setDimensions(binding->width, binding->height);
          }
        }
      });
    }
    if (m_serviceHook)
      (*m_serviceHook)();
    if (m_translations)
      m_translations->pump();
    if (m_renders)
      m_renders->pump();
    if (m_frames && m_integration) {
      // Bind only after initial background setup has completed. Natural render
      // starts and observation callbacks execute on the viewer owner. The
      // adapter still rejects off-owner entry before any live Scene/UI reads.
      for (auto it = m_pendingFrameSources.begin();
          it != m_pendingFrameSources.end();) {
        if (it->second->effectiveView().unavailableReason.empty()) {
          auto source = std::make_unique<ViewportFrameSource>(
              *m_frames, it->first, *m_identity, *it->second);
          if (source->connected()) {
            // SDL_CPU_READY_V1 is instance-bound, not a backend-name grant.
            // Actual transfer, rendered generation, effects and submission
            // scale are checked again on every acquisition/presentation.
            if (!m_frames->setExposed(it->first, true)) {
              ++it;
              continue;
            }
            m_viewportFrameSources.emplace_back(std::move(source));
            it = m_pendingFrameSources.erase(it);
          } else
            ++it;
        } else
          ++it;
      }
      m_integration->submit(
          0, [this](vsr::scene::Scene &) { m_frames->pump(); });
      m_integration->service();
    }
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
  void registerViewportBindings();

  bool m_agentIntegrationRequested{false};
  vsr_ui::IntegrationActivity *m_activityWindow{nullptr};
  std::unique_ptr<vsr::integration::Integration> m_integration;
  std::unique_ptr<vsr::integration::session::SessionAuthority> m_authority;
  std::unique_ptr<vsr::integration::operations::OperationEngine> m_engine;
  std::unique_ptr<vsr::integration::identity::IdentityAuthority> m_ids;
  std::unique_ptr<vsr::integration::identity::SceneIdentity> m_identity;
  std::unique_ptr<vsr::integration::edit::EditService> m_edits;
  std::unique_ptr<vsr::integration::edit::TranslationService> m_translations;
  std::unique_ptr<vsr::integration::restore::RestoreService> m_restores;
  std::unique_ptr<vsr::integration::sync::SyncService> m_sync;
  std::unique_ptr<vsr::integration::activity::ActivityLog> m_activity;
  std::unique_ptr<vsr::integration::activity::EngineActivitySink>
      m_activitySink;
  std::unique_ptr<vsr::integration::context::ViewerExecutionHook> m_serviceHook;
  std::unique_ptr<AgentView> m_agentView;
  std::unique_ptr<vsr::integration::identity::ViewRevisionTracker> m_tracker;
  vsr_ui::Viewport *m_viewport{nullptr};
  vsr_ui::Viewport *m_secondaryViewport{nullptr};
  std::vector<std::pair<vsr::integration::ViewId,
      std::unique_ptr<vsr::integration::identity::ViewRevisionTracker>>>
      m_viewportTrackers;
  std::unique_ptr<vsr::integration::render::RenderService> m_renders;
  std::unique_ptr<vsr::integration::frame::FrameService> m_frames;
  struct ViewportRegistration
  {
    vsr_ui::Viewport *viewport{nullptr};
    std::weak_ptr<const void> lifetime;
    vsr::integration::ViewId id{vsr::integration::INVALID_VIEW_ID};
  };
  std::array<ViewportRegistration, 2> m_viewportRegistrations;
  vsr::integration::SceneId m_viewportSceneId{};
  using ViewportFrameSource =
      vsr::integration::frame::ViewerFrameSource<vsr_ui::Viewport>;
  std::vector<std::unique_ptr<ViewportFrameSource>> m_viewportFrameSources;
  std::vector<std::pair<vsr::integration::ViewId, vsr_ui::Viewport *>>
      m_pendingFrameSources;
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
    vsr::core::logError("[vsrViewer] agent view setup failed (no ANARI device)");
    teardownIntegration();
    m_agentIntegrationRequested = false;
    return;
  }

  m_integration = std::make_unique<vi::Integration>();

  // These hooks confirm only the legacy render-capable AgentView. Metadata
  // viewport registrations opt out of scene-hook render confirmation.
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
    vr.role = vi::ViewRole::INDEPENDENT_AGENT;
    vr.observeBinding = [view] {
      return std::optional<vi::EffectiveViewBinding>(view->binding);
    };
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
  m_translations = std::make_unique<vi::edit::TranslationService>(
      *m_integration, *m_authority, *m_engine, *m_edits);
  m_translations->attachScene(*sceneId, *m_identity);
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
  m_frames = std::make_unique<vi::frame::FrameService>(
      *m_integration, *m_authority, *m_engine);
  auto *identity = m_identity.get();
  auto *tracker = m_tracker.get();
  m_viewportSceneId = *sceneId;
  m_viewportRegistrations = {{{m_viewport, m_viewport->lifetime()},
      {m_secondaryViewport, m_secondaryViewport->lifetime()}}};
  m_integration->submit(
      *sceneId, [this](vsr::scene::Scene &) { registerViewportBindings(); });
  m_integration->service();
  m_inspector = std::make_unique<vi::inspection::Inspector>(
      *m_integration, *m_authority, [identity, sid = *sceneId](vi::SceneId id) {
        return id == sid ? identity : nullptr;
      });
  m_inspector->setViewRevisions([this, tracker, vid = *viewId](vi::ViewId id) {
    if (id == vid)
      return tracker;
    for (auto &entry : m_viewportTrackers)
      if (entry.first == id)
        return entry.second.get();
    return static_cast<vi::identity::ViewRevisionTracker *>(nullptr);
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
  vi::mcp::tools::addEditTools(
      cfg.tools, {*m_ids, *m_edits, *m_restores, m_translations.get()});
  vi::mcp::tools::addRenderTools(cfg.tools, {*m_renders, *m_sync, *m_engine});
  vi::mcp::tools::addFrameTools(cfg.tools, {*m_frames, *m_engine});
  cfg.log = [](std::string_view line) {
    vsr::core::logStatus("[vsrViewer] %.*s", int(line.size()), line.data());
  };
  if (auto st = vi::descriptor::openOsRuntimeDirectory(m_runtimeDir); !st.ok())
    vsr::core::logError("[vsrViewer] runtime directory: %s", st.reason.c_str());
  else if (auto st = vi::mcp::McpServer::start(
               cfg, *m_authority, *m_engine, m_runtimeDir, m_mcp);
      !st.ok())
    vsr::core::logError("[vsrViewer] MCP listener: %s", st.reason.c_str());
  else {
    services.descriptorPath = m_mcp->published().path();
    // The path only, never the contents (which carry the launch
    // credential): pass it to vsrMcpProxy as its descriptor argument.
    vsr::core::logStatus("[vsrViewer] MCP descriptor: %s",
        services.descriptorPath.c_str());
  }
#endif

  m_activityWindow->model().setServices(std::move(services));
  m_activityWindow->show();
  vsr::core::logStatus("[vsrViewer] agent integration active");
}

void Application::registerViewportBindings()
{
  namespace vi = vsr::integration;
  for (auto &slot : m_viewportRegistrations) {
    const bool visible =
        !slot.lifetime.expired() && *slot.viewport->visiblePtr();
    if (slot.id != vi::INVALID_VIEW_ID && !visible) {
      const auto id = slot.id;
      // Disconnect while the live window still exists; a destroyed window
      // already disconnected at its pre-device teardown notification.
      m_viewportFrameSources.erase(
          std::remove_if(m_viewportFrameSources.begin(),
              m_viewportFrameSources.end(),
              [id](const auto &source) { return source->viewId() == id; }),
          m_viewportFrameSources.end());
      m_pendingFrameSources.erase(
          std::remove_if(m_pendingFrameSources.begin(),
              m_pendingFrameSources.end(),
              [id](const auto &entry) { return entry.first == id; }),
          m_pendingFrameSources.end());
      m_integration->unregisterView(id);
      m_frames->detachViewer(id);
      m_viewportTrackers.erase(
          std::remove_if(m_viewportTrackers.begin(),
              m_viewportTrackers.end(),
              [id](const auto &entry) { return entry.first == id; }),
          m_viewportTrackers.end());
      slot.id = vi::INVALID_VIEW_ID;
    }
    if (!visible || slot.id != vi::INVALID_VIEW_ID)
      continue;
    auto *viewport = slot.viewport;
    auto *identity = m_identity.get();
    vi::ViewRegistration registration;
    registration.name = viewport == m_viewport ? "Viewport" : "Secondary View";
    registration.scene = m_viewportSceneId;
    registration.grants = {true, false};
    registration.role = vi::ViewRole::INTERACTIVE_VIEWPORT;
    registration.sceneSyncConfirmsBinding = false;
    registration.observeBinding = [viewport, identity, life = slot.lifetime] {
      vi::EffectiveViewBinding binding;
      if (life.expired() || !*viewport->visiblePtr()) {
        binding.unavailableReason = "inactive_viewport";
        return std::optional<vi::EffectiveViewBinding>(std::move(binding));
      }
      static_cast<vsr::rendering::EffectiveView &>(binding) =
          viewport->effectiveView();
      if (auto camera = viewport->boundCamera())
        binding.cameraHandle = identity->handleOf(camera.data());
      if (auto renderer = viewport->boundRenderer())
        binding.rendererHandle = identity->handleOf(renderer.data());
      return std::optional<vi::EffectiveViewBinding>(std::move(binding));
    };
    auto id = m_integration->registerView(std::move(registration));
    if (!id)
      continue;
    auto tracker =
        std::make_unique<vi::identity::ViewRevisionTracker>(*m_ids, *identity);
    if (!m_frames->attachView(*id,
            *identity,
            *tracker,
            vi::frame::SourceQualification::VIEWER_CPU_READY)) {
      m_integration->unregisterView(*id);
      continue;
    }
    slot.id = *id;
    m_viewportTrackers.emplace_back(*id, std::move(tracker));
    m_pendingFrameSources.emplace_back(*id, viewport);
  }
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
  m_pendingFrameSources.clear();
  m_viewportFrameSources.clear();
  m_frames.reset();
  m_tracker.reset();
  m_viewportTrackers.clear();
  if (m_engine)
    m_engine->setActivitySink(nullptr);
  m_sync.reset();
  m_restores.reset();
  m_translations.reset();
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

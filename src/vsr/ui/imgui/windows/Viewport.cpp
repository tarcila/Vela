// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Viewport.h"
// vsr_app
#include "vsr/app/ANARIDeviceManager.h"
// vsr_ui_imgui
#include "imgui.h"
#include "vsr/ui/imgui/Application.h"
#include "vsr/ui/imgui/vsr_ui_imgui.h"
// vsr_core
#include "vsr/core/Logging.hpp"
#include "vsr/scene/objects/Camera.hpp"
#include "vsr/scene/objects/Renderer.hpp"
// vsr_rendering
#include "vsr/rendering/view/ManipulatorToVSR.hpp"
// std
#include <charconv>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace vsr::ui::imgui {

namespace {

std::string defaultLibraryName(const vsr::app::ANARIDeviceManager &adm)
{
  for (const auto &libName : adm.deviceList()) {
    if (adm.isLoadableDevice(libName))
      return libName;
  }

  return {};
}

} // namespace

Viewport::Viewport(
    Application *app, vsr::rendering::Manipulator *m, const char *name)
    : BaseViewport(app, name)
{
  m_viewport.resolutionScale = 0.75f;
  BaseViewport::setManipulator(m);
  m_defragToken = appContext()->vsr.scene.addDefragCallback(
      [this](const auto &) { refreshCurrentDevice(); });
}

Viewport::~Viewport()
{
  // Disconnect the adapter while this viewport and all its passes still live.
  // Registrations can outlive windows, but may no longer dereference them.
  m_lifetime.reset();
  auto disconnect = std::move(m_frameTeardown);
  if (disconnect)
    disconnect();
  teardownDevice();
  appContext()->vsr.scene.removeDefragCallback(m_defragToken);
}

void Viewport::setFrameTeardown(std::function<void()> callback)
{
  // Replacing an adapter first disconnects the previous owner. Otherwise its
  // later destructor could clear the new callbacks/teardown notification.
  auto previous = std::move(m_frameTeardown);
  m_frameTeardown = {};
  if (callback && previous)
    previous();
  m_frameTeardown = std::move(callback);
}

std::weak_ptr<const void> Viewport::lifetime() const
{
  return m_lifetime;
}

std::optional<vsr::rendering::SDLTextureUpload> Viewport::textureUpload() const
{
  if (m_bindingTransition.load(std::memory_order_acquire) || !m_outputPass)
    return {};
  return m_outputPass->upload();
}

std::optional<vsr::rendering::SDLTexturePresentation>
Viewport::displayedTexture() const
{
  if (m_bindingTransition.load(std::memory_order_acquire) || !m_outputPass
      || !m_visible || !BaseViewport::viewport_isActive())
    return {};
  return m_outputPass->presentation();
}

void Viewport::setFrameStateProvider(
    std::function<vsr::rendering::AnariSceneRenderPass::FrameState()> provider)
{
  // Owning thread, outside a device transition. First render starts naturally
  // on this thread only after setup has published its completed state.
  {
    std::lock_guard<std::mutex> lock(m_frameObservationMutex);
    m_frameStateProvider = provider;
  }
  if (!m_bindingTransition.load(std::memory_order_acquire) && m_anariPass)
    m_anariPass->setFrameStateProvider(std::move(provider));
}

vsr::rendering::AnariSceneRenderPass::FrameState Viewport::renderedFrameState()
    const
{
  if (m_bindingTransition.load(std::memory_order_acquire) || !m_anariPass)
    return {};
  return m_anariPass->frameState();
}

void Viewport::setFrameObservation(
    ReadyPixelsProvider acquire, PresentationObserver present)
{
  {
    std::lock_guard<std::mutex> lock(m_frameObservationMutex);
    m_readyPixelsProvider = acquire;
    m_presentationObserver = std::move(present);
  }
  if (!m_bindingTransition.load(std::memory_order_acquire) && m_outputPass) {
    auto *anariPass = m_anariPass;
    m_outputPass->setReadyPixelsProvider(acquire
            ? vsr::rendering::CopyToSDLTexturePass::ReadyPixelsProvider(
                  [acquire = std::move(acquire), anariPass](
                      const std::uint32_t *pixels,
                      std::uint32_t width,
                      std::uint32_t height,
                      const char *path) {
                    return acquire(pixels,
                        width,
                        height,
                        path,
                        anariPass ? anariPass->frameState() : FrameState{});
                  })
            : vsr::rendering::CopyToSDLTexturePass::ReadyPixelsProvider{});
  }
}

std::optional<vsr::rendering::SDLTexturePresentation>
Viewport::queuedTextureDraw() const
{
  if (m_bindingTransition.load(std::memory_order_acquire) || !m_outputPass)
    return {};
  return m_outputPass->queuedDraw();
}

Viewport::FrameState Viewport::presentedFrameState() const
{
  return displayedTexture() ? m_outputPass->presentedPixelsState()
                            : FrameState{};
}

void Viewport::completePresentation(bool submitted)
{
  if (!m_bindingTransition.load(std::memory_order_acquire) && m_outputPass) {
    const auto before = m_outputPass->presentation();
    m_outputPass->completePresentation(submitted);
    const auto after = displayedTexture();
    PresentationObserver observer;
    {
      std::lock_guard<std::mutex> lock(m_frameObservationMutex);
      observer = m_presentationObserver;
    }
    if (observer) {
      if (!after)
        observer({}, {});
      else if (!before || before->identity != after->identity)
        observer(after, m_outputPass->presentedPixelsState());
    }
  }
}

void Viewport::drawImage(const ImDrawList *, const ImDrawCmd *command)
{
  auto *self = static_cast<Viewport *>(command->UserCallbackData);
  if (!self || self->m_bindingTransition.load(std::memory_order_acquire)
      || !self->m_outputPass)
    return;
  auto *renderer = self->m_app->sdlRenderer();
  auto *data = ImGui::GetDrawData();
  if (!renderer || !data)
    return;
  // Match the backend's projection: renderer scale, if configured, replaces
  // ImGui framebuffer scaling rather than multiplying it twice.
  float rsx = 1.f, rsy = 1.f;
  if (!SDL_GetRenderScale(renderer, &rsx, &rsy))
    return;
  const float sx = rsx == 1.f ? data->FramebufferScale.x : 1.f;
  const float sy = rsy == 1.f ? data->FramebufferScale.y : 1.f;
  const float left =
      std::max(0.f, (command->ClipRect.x - data->DisplayPos.x) * sx);
  const float top =
      std::max(0.f, (command->ClipRect.y - data->DisplayPos.y) * sy);
  const float right = std::min(data->DisplaySize.x * sx,
      (command->ClipRect.z - data->DisplayPos.x) * sx);
  const float bottom = std::min(data->DisplaySize.y * sy,
      (command->ClipRect.w - data->DisplayPos.y) * sy);
  if (right <= left || bottom <= top)
    return;
  self->m_outputPass->draw(
      {(self->m_imagePosition.x - data->DisplayPos.x) * sx,
          (self->m_imagePosition.y - data->DisplayPos.y) * sy,
          self->m_displaySize.x * sx,
          self->m_displaySize.y * sy},
      {int(left), int(top), int(right - left), int(bottom - top)},
      rsx == 1.f ? data->FramebufferScale.x : rsx,
      rsy == 1.f ? data->FramebufferScale.y : rsy);
}

void Viewport::buildUI()
{
  // The setup worker publishes all pass/binding state before the UI accesses
  // it. Activation and the first asynchronous render belong to the UI owner,
  // not the worker: other viewports can still render while the modal is open.
  if (m_bindingTransition.load(std::memory_order_acquire))
    return;
  if (m_activateAfterSetup) {
    BaseViewport::viewport_setActive(m_device != nullptr);
    m_activateAfterSetup = false;
  }

  if (BaseViewport::viewport_isActive()) {
    BaseViewport::buildUI();
    if (m_renderingEnabled) {
      updateFrame();
      BaseViewport::camera_update();
    }
    updateImage();
  }

  ui_menubar();

  ImGui::BeginDisabled(!BaseViewport::viewport_isActive());

  if (BaseViewport::viewport_isActive()) {
    // Fit the rendered texture into the available region preserving its aspect
    // ratio. It enables keeping the same correct image on viewport interactive
    // resize, without having to render a new frame each size change.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const vsr::math::int2 texSize = m_viewport.renderSize;
    ImVec2 imageSize = avail;
    if (texSize.x > 0 && texSize.y > 0 && avail.x > 0.f && avail.y > 0.f) {
      const float texAspect = float(texSize.x) / float(texSize.y);
      if (avail.x / avail.y > texAspect)
        imageSize.x = avail.y * texAspect;
      else
        imageSize.y = avail.x / texAspect;
      const ImVec2 cursor = ImGui::GetCursorPos();
      ImGui::SetCursorPos(ImVec2(cursor.x + (avail.x - imageSize.x) * 0.5f,
          cursor.y + (avail.y - imageSize.y) * 0.5f));
    }
    m_displaySize = {imageSize.x, imageSize.y};
    const auto framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
    m_displayFramebufferScale = {framebufferScale.x, framebufferScale.y};
    // Keep ImGui layout/input semantics, but draw the same viewport quad via
    // a checked SDL callback. The generic backend discards draw return values.
    m_imagePosition = ImGui::GetCursorScreenPos();
    if (ImGui::IsRectVisible(imageSize)) {
      auto *list = ImGui::GetWindowDrawList();
      list->AddCallback(&Viewport::drawImage, this);
      list->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }
    ImGui::Dummy(imageSize);
    const ImVec2 rectMin = ImGui::GetItemRectMin();
    const ImVec2 rectMax = ImGui::GetItemRectMax();
    m_imageRectMin = vsr::math::float2(rectMin.x, rectMin.y);
    m_imageRectMax = vsr::math::float2(rectMax.x, rectMax.y);
  }

  BaseViewport::ui_gizmo();
  const bool widgetActive = BaseViewport::ui_orientationWidget();
  if (!widgetActive)
    BaseViewport::ui_handleInput();
  ui_picking(); // Needs to happen before ui_menubar

  // Render the overlay after input handling so it does not interfere.
  if (m_showOverlay)
    ui_overlay();

  BaseViewport::ui_animationSlider();

  ImGui::EndDisabled();

  if (m_rIdx) {
    auto kind = appContext()->anari.renderIndexKind();
    if (kind != m_lastIndexKind) {
      vsr::core::logWarning("render index setting changed: resetting viewport");
      m_lastIndexKind = kind;
      refreshCurrentDevice();
    }
  }
}

void Viewport::setLibrary(
    const std::string &libName, size_t rendererIndex, bool resetInitialView)
{
  teardownDevice();

  auto &adm = appContext()->anari;
  if (adm.isLoadableDevice(libName)) {
    vsr::core::logStatus(
        "[viewport] *** setting viewport to use ANARI device '%s' ***",
        libName.c_str());
  }

  auto updateLibrary = [&,
                           libName = libName,
                           rendererIndex = rendererIndex,
                           resetInitialView = resetInitialView]() {
    auto &scene = appContext()->vsr.scene;

    auto start = std::chrono::steady_clock::now();
    auto selectedLibName = libName;
    auto d = adm.loadDevice(selectedLibName);

    if (!d && adm.isLoadableDevice(selectedLibName)) {
      vsr::core::logWarning(
          "[viewport] failed to load ANARI device '%s'; falling back to a "
          "default device",
          selectedLibName.c_str());
    }

    if (!d) {
      const auto fallbackLibName = defaultLibraryName(adm);
      if (!fallbackLibName.empty() && fallbackLibName != selectedLibName) {
        selectedLibName = fallbackLibName;
        d = adm.loadDevice(selectedLibName);
      }
    }

    m_libName = d ? selectedLibName : std::string{};

    m_latestFL = 0.f;
    m_minFL.reset();
    m_maxFL.reset();

    if (d) {
      m_device = d;

      vsr::core::logStatus("[viewport] setting up renderer objects...");

      m_renderers.objects = scene.renderersOfDevice(selectedLibName);
      if (m_renderers.objects.empty())
        m_renderers.objects = scene.createStandardRenderers(selectedLibName, d);

      if (rendererIndex != VSR_INVALID_INDEX) {
        auto renderer = scene.getObject<vsr::scene::Renderer>(rendererIndex);
        if (renderer && renderer->rendererDeviceName() == selectedLibName)
          m_renderers.current = renderer;
        else {
          vsr::core::logWarning(
              "[viewport] renderer object index %zu is unavailable for ANARI "
              "device '%s'; using the default renderer",
              rendererIndex,
              selectedLibName.c_str());
        }
      }

      if (!m_renderers.current && !m_renderers.objects.empty())
        m_renderers.current = m_renderers.objects[0];

      vsr::core::logStatus("[viewport] populating render index...");

      m_rIdx = adm.acquireRenderIndex(scene, selectedLibName, d);
      setSelectionVisibilityFilterEnabled(m_showOnlySelected);

      static bool firstFrame = true;

      vsr::core::logStatus("[viewport] setting up camera...");

      if (!m_camera.current)
        BaseViewport::camera_setCurrent(scene.defaultCamera());

      rendering::updateManipulatorFromCamera(
          *m_camera.arcball, *m_camera.current);

      const bool resetView = m_camera.arcball->distance() == vsr::math::inf
          || (firstFrame && resetInitialView
              && !appContext()->commandLine.loadedFromStateFile);
      firstFrame = false;
      if (resetView) {
        vsr::core::logStatus(
            "[viewport] getting scene bounds to init camera...");
        camera_resetView(true);
      }

      vsr::core::logStatus("[viewport] setting up image pipeline...");

      BaseViewport::imagePipeline_setup();

      // Prepare bindings without entering render/wait on the setup worker.
      // The next owning-thread UI update starts the first frame naturally.
      m_anariPass->setWorld(m_rIdx->world());
      BaseViewport::camera_update(true);
      updateFrame();

      vsr::core::logStatus("[viewport] ...device load complete");
    }

    auto end = std::chrono::steady_clock::now();
    m_timeToLoadDevice = std::chrono::duration<float>(end - start).count();

    if (m_deviceChangeCb)
      m_deviceChangeCb(m_libName);
  };

  m_app->showTaskModal(
      [this, updateLibrary]() {
        updateLibrary();
        m_activateAfterSetup = true;
        m_bindingTransition.store(false, std::memory_order_release);
      },
      "Loading Device...");
}

void Viewport::setLibraryToDefault()
{
  if (appContext()->commandLine.loadedFromStateFile)
    return;

  setLibrary(m_app->commandLineOptions()->useDefaultRenderer
          ? appContext()->anari.deviceList()[0]
          : "");
}

const std::string &Viewport::libraryName() const
{
  return m_libName;
}

size_t Viewport::currentRendererObjectIndex() const
{
  return m_renderers.current ? m_renderers.current->index() : VSR_INVALID_INDEX;
}

vsr::scene::CameraRef Viewport::boundCamera() const
{
  return m_bindingTransition.load(std::memory_order_acquire) || !m_prevCamera
      ? vsr::scene::CameraRef{}
      : m_prevCamera->self();
}

vsr::scene::RendererRef Viewport::boundRenderer() const
{
  return m_bindingTransition.load(std::memory_order_acquire) || !m_prevRenderer
      ? vsr::scene::RendererRef{}
      : m_prevRenderer->self();
}

vsr::rendering::EffectiveView Viewport::effectiveView() const
{
  vsr::rendering::EffectiveView result;
  if (m_bindingTransition.load(std::memory_order_acquire)) {
    result.unavailableReason = "device_transition";
    return result;
  }
  if (m_customFrameBindingUnknown) {
    result.unavailableReason = "unsupported_custom_frame_parameters";
    return result;
  }
  if (!m_anariPass || !m_prevCamera || !m_prevRenderer || !m_device) {
    result.unavailableReason = "missing_bound_source";
    return result;
  }
  const auto size = imagePipeline().dimensions();
  result.width = size.x;
  result.height = size.y;
  result.displayWidth = m_displaySize.x;
  result.displayHeight = m_displaySize.y;
  result.displayFramebufferScaleX = m_displayFramebufferScale.x;
  result.displayFramebufferScaleY = m_displayFramebufferScale.y;
  result.displayCoordinateUnits = "imgui_logical_units";
  result.deviceIdentifier = m_libName;
  if (auto identifier = vsr::app::DeviceIdentifier::parse(m_libName))
    result.deviceSubtype = identifier->subtype;
  const auto &camera = *m_prevCamera;
  result.camera.projection = camera.subtype().str();
  auto vector = [&](const char *name, std::array<double, 3> &value) {
    if (auto v = camera.parameterValueAs<vsr::math::float3>(name))
      value = {v->x, v->y, v->z};
    else
      result.unavailableReason = "incomplete_camera_pose";
  };
  vector("position", result.camera.position);
  vector("direction", result.camera.direction);
  vector("up", result.camera.up);
  result.camera.aspect = double(size.x) / std::max(1u, size.y);
  if (result.camera.projection == "perspective") {
    if (auto fovy = camera.parameterValueAs<float>("fovy"))
      result.camera.fovyDegrees = vsr::math::degrees(*fovy);
    else
      result.unavailableReason = "incomplete_projection";
  } else if (result.camera.projection == "orthographic") {
    if (auto height = camera.parameterValueAs<float>("height"))
      result.camera.projectionSettings.push_back({"height", {*height}, {}});
    else
      result.unavailableReason = "incomplete_projection";
  } else
    result.unavailableReason = "unsupported_projection";
  result.rendererSubtype = m_prevRenderer->subtype().str();
  result.rendererConfigurationRevision = std::to_string(m_bindingGeneration)
      + ":" + std::to_string(m_prevRenderer->lastParameterChange());
  const auto &renderer = *m_prevRenderer;
  result.rendererSettingsReason = "backend_defaults_not_observed";
  // Bound before copying live strings. The owning-thread frame adapter charges
  // bounded metadata, not arbitrary renderer-owned parameter storage. Account
  // conservatively for JSON escaping/numbers without depending on integration.
  std::size_t settingsAllowance = 16u << 10;
  if (renderer.numParameters() > 128)
    result.rendererSettingsReason = "representation_limit";
  else
    for (size_t i = 0; i < renderer.numParameters(); ++i) {
      const auto &parameter = renderer.parameterAt(i);
      if (!parameter.isEnabled())
        continue;
      const auto &value = parameter.value();
      const auto *name = renderer.parameterNameAt(i);
      std::size_t nameSize = 0;
      while (nameSize <= 4096 && name[nameSize])
        ++nameSize;
      std::size_t charge = 64 + 4 * 32 + nameSize * 6;
      if (nameSize > 4096 || charge > settingsAllowance) {
        result.rendererSettingsReason = "representation_limit";
        break;
      }
      vsr::rendering::EffectiveSetting setting;
      setting.name.assign(name, nameSize);
      switch (value.type()) {
      case ANARI_BOOL:
        setting.numbers = {value.get<bool>() ? 1. : 0.};
        break;
      case ANARI_INT32:
        setting.numbers = {double(value.get<int>())};
        break;
      case ANARI_UINT32:
        setting.numbers = {double(value.get<uint32_t>())};
        break;
      case ANARI_FLOAT32:
        setting.numbers = {value.get<float>()};
        break;
      case ANARI_FLOAT32_VEC2: {
        const auto v = value.get<vsr::math::float2>();
        setting.numbers = {v.x, v.y};
        break;
      }
      case ANARI_FLOAT32_VEC3: {
        const auto v = value.get<vsr::math::float3>();
        setting.numbers = {v.x, v.y, v.z};
        break;
      }
      case ANARI_FLOAT32_VEC4: {
        const auto v = value.get<vsr::math::float4>();
        setting.numbers = {v.x, v.y, v.z, v.w};
        break;
      }
      case ANARI_STRING: {
        const auto text = value.getStringView();
        if (text.size() > 4096
            || text.size() * 6 > settingsAllowance - charge) {
          result.rendererSettingsReason = "representation_limit";
          continue;
        }
        charge += text.size() * 6;
        setting.text.assign(text.data(), text.size());
        break;
      }
      default:
        result.rendererSettingsReason = "unsupported_parameter_type";
        continue;
      }
      settingsAllowance -= charge;
      result.rendererSettings.push_back(std::move(setting));
    }
  result.sourceInstanceGeneration = m_deviceGeneration;
  result.pipelineSource = "viewer_image_pipeline";
  result.pipelineConfigurationRevision = std::to_string(m_bindingGeneration);
  result.pipelineSettings = {
      {"render_enabled", {double(m_anariPass->isEnabled())}, {}},
      {"aov", {double(m_visualizeAOV)}, {}},
      {"depth_range", {m_depthVisualMinimum, m_depthVisualMaximum}, {}},
      {"edge_invert", {double(m_edgeInvert)}, {}},
      {"tone_map_operator", {double(m_toneMapOperator)}, {}},
      {"exposure", {m_toneMapExposure}, {}},
      {"auto_exposure", {double(m_autoExposureEnabled)}, {}},
      {"gamma", {m_toneMapGamma}, {}},
      {"tone_map_enabled", {double(m_toneMapPass->isEnabled())}, {}},
      {"output_transform_enabled",
          {double(m_outputTransformPass->isEnabled())},
          {}},
      {"outline_enabled", {double(m_outlinePass->isEnabled())}, {}},
      {"primitive_outline_enabled",
          {double(m_primitiveOutlinePass->isEnabled())},
          {}},
      {"box_outline_enabled",
          {double(m_worldBounds && m_worldBounds->isShown())},
          {}},
      {"resolution_scale", {m_viewport.resolutionScale}, {}},
      {"implicit_aspect", {double(m_camera.useImplicitAspectRatio)}, {}},
      {"color_format", {double(m_colorFormat)}, {}}};
  for (const auto &setting : result.pipelineSettings) {
    result.pipelineConfigurationRevision += ":" + setting.name;
    for (double value : setting.numbers) {
      char buffer[64];
      const auto converted = std::to_chars(buffer,
          buffer + sizeof(buffer),
          value,
          std::chars_format::general,
          17);
      result.pipelineConfigurationRevision +=
          ":" + std::string(buffer, converted.ptr);
    }
  }
  result.pipelineSettingsReason = "dynamic_pass_state_not_fully_observed";
  if (m_visualizeAOV == vsr::rendering::AOVType::NONE)
    result.outputColor = m_colorFormat == ANARI_UFIXED8_RGBA_SRGB
        ? "rgba8_srgb"
        : "rgba8_gamma_encoded";
  return result;
}

void Viewport::setDeviceChangeCb(ViewportDeviceChangeCb cb)
{
  m_deviceChangeCb = std::move(cb);
}

void Viewport::setExternalInstances(
    const anari::Instance *instances, size_t count)
{
  if (m_rIdx) {
    m_rIdx->setExternalInstances(instances, count);
    ++m_bindingGeneration;
  }
}

void Viewport::setCustomFrameParameter(
    const char *name, const vsr::core::Any &value)
{
  if (!BaseViewport::viewport_isActive()) {
    vsr::core::logWarning(
        "[viewport] cannot set custom frame parameter '%s': no frame yet",
        name);
    return;
  }

  auto f = m_anariPass->getFrame();
  anari::setParameter(m_device, f, name, value.type(), value.data());
  anari::commitParameters(m_device, f);
  m_customFrameBindingUnknown = true;
  ++m_bindingGeneration;
}

void Viewport::refreshCurrentDevice()
{
  if (BaseViewport::viewport_isActive()) {
    auto lib = m_libName; // setLibrary() clears m_libName
    setLibrary(lib);
  }
}

void Viewport::saveSettings(vsr::core::DataNode &root)
{
  // Viewport settings //

  root["showOverlay"] = m_showOverlay;
  root["showOnlySelected"] = m_showOnlySelected;
  root["highlightSelection"] = m_highlightSelection;
  root["outlinePrimitives"] = m_outlinePrimitives;
  root["showWorldBounds"] = m_showWorldBounds;
  root["worldBoundsColor"] = m_worldBoundsColor;
  root["worldBoundsWidth"] = m_worldBoundsWidth;
  root["visualizeAOV"] = static_cast<int>(m_visualizeAOV);
  root["depthVisualMinimum"] = m_depthVisualMinimum;
  root["depthVisualMaximum"] = m_depthVisualMaximum;
  root["edgeInvert"] = m_edgeInvert;
  root["autoExposureEnabled"] = m_autoExposureEnabled;
  root["toneMapExposure"] = m_toneMapExposure;
  root["toneMapGamma"] = m_toneMapGamma;
  root["toneMapOperator"] = static_cast<int>(m_toneMapOperator);

  // BaseViewport settings //

  BaseViewport::saveSettings(root);
}

void Viewport::loadSettings(vsr::core::DataNode &root)
{
  BaseViewport::loadSettings(root);

  // Viewport settings //

  root["showOverlay"].getValue(ANARI_BOOL, &m_showOverlay);
  root["showOnlySelected"].getValue(ANARI_BOOL, &m_showOnlySelected);
  root["highlightSelection"].getValue(ANARI_BOOL, &m_highlightSelection);
  root["outlinePrimitives"].getValue(ANARI_BOOL, &m_outlinePrimitives);
  root["showWorldBounds"].getValue(ANARI_BOOL, &m_showWorldBounds);
  root["worldBoundsColor"].getValue(ANARI_FLOAT32_VEC4, &m_worldBoundsColor);
  root["worldBoundsWidth"].getValue(ANARI_INT32, &m_worldBoundsWidth);
  int aovType = static_cast<int>(m_visualizeAOV);
  root["visualizeAOV"].getValue(ANARI_INT32, &aovType);
  m_visualizeAOV = static_cast<vsr::rendering::AOVType>(aovType);
  root["depthVisualMinimum"].getValue(ANARI_FLOAT32, &m_depthVisualMinimum);
  root["depthVisualMaximum"].getValue(ANARI_FLOAT32, &m_depthVisualMaximum);
  root["edgeInvert"].getValue(ANARI_BOOL, &m_edgeInvert);
  root["autoExposureEnabled"].getValue(ANARI_BOOL, &m_autoExposureEnabled);
  root["toneMapExposure"].getValue(ANARI_FLOAT32, &m_toneMapExposure);
  root["toneMapGamma"].getValue(ANARI_FLOAT32, &m_toneMapGamma);
  int toneMapOperator = static_cast<int>(m_toneMapOperator);
  root["toneMapOperator"].getValue(ANARI_INT32, &toneMapOperator);
  m_toneMapOperator =
      static_cast<vsr::rendering::ToneMapOperator>(toneMapOperator);
}

void Viewport::saveSceneSettings(vsr::core::DataNode &root)
{
  root["anariLibrary"] = m_libName;
  root["rendererObjectIndex"] =
      static_cast<uint64_t>(currentRendererObjectIndex());

  // Database Camera //

  if (m_camera.current)
    root["currentCamera"] = static_cast<uint64_t>(m_camera.current->index());
}

void Viewport::loadSceneSettings(vsr::core::DataNode &root)
{
  // Database Camera //

  if (auto *c = root.child("currentCamera"); c) {
    uint64_t idx = 0;
    c->getValue(ANARI_UINT64, &idx);
    m_camera.current =
        appContext()->vsr.scene.getObject<vsr::scene::Camera>(idx);
  }

  // Setup library //

  if (m_app->commandLineOptions()->useDefaultRenderer
      && root.child("anariLibrary") != nullptr) {
    std::string libraryName;
    root["anariLibrary"].getValue(ANARI_STRING, &libraryName);
    auto rendererIndex =
        root["rendererObjectIndex"].getValueOr<uint64_t>(VSR_INVALID_INDEX);
    setLibrary(libraryName, rendererIndex, false);
  }
}

void Viewport::imagePipeline_populate(vsr::rendering::ImagePipeline &p)
{
  vsr::core::logStatus("[viewport] initialized scene for '%s' device in %.2fs",
      m_libName.c_str(),
      m_timeToLoadDevice);

  m_anariPass = p.setSource<vsr::rendering::AnariSceneRenderPass>(m_device);
  {
    std::lock_guard<std::mutex> lock(m_frameObservationMutex);
    m_anariPass->setFrameStateProvider(m_frameStateProvider);
  }
  m_anariPass->setEnabled(m_renderingEnabled);
  m_anariPass->setUseImplicitAspectRatio(m_camera.useImplicitAspectRatio);

  if (!sourceSupports(vsr::rendering::requiredChannels(m_visualizeAOV)))
    m_visualizeAOV = vsr::rendering::AOVType::NONE;

  m_autoExposurePass = p.addPass<vsr::rendering::AutoExposurePass>();

  m_toneMapPass = p.addPass<vsr::rendering::ToneMapPass>();
  m_toneMapPass->setOperator(m_toneMapOperator);
  m_toneMapPass->setAutoExposureEnabled(m_autoExposureEnabled);
  m_toneMapPass->setExposure(m_toneMapExposure);

  m_outputTransformPass = p.addPass<vsr::rendering::OutputTransformPass>();
  m_outputTransformPass->setGamma(m_toneMapGamma);

  m_visualizeAOVPass = p.addPass<vsr::rendering::VisualizeAOVPass>();
  m_visualizeAOVPass->setEnabled(false);
  m_visualizeAOVPass->setEdgeInvert(m_edgeInvert);

  m_primitiveOutlinePass =
      p.addPass<vsr::rendering::PrimitiveOutlineRenderPass>();

  m_outlinePass = p.addPass<vsr::rendering::OutlineRenderPass>();

  m_worldBounds.emplace(p);

  m_outputPass =
      p.addSink<vsr::rendering::CopyToSDLTexturePass>(m_app->sdlRenderer());
  ReadyPixelsProvider acquire;
  {
    std::lock_guard<std::mutex> lock(m_frameObservationMutex);
    acquire = m_readyPixelsProvider;
  }
  if (acquire)
    m_outputPass->setReadyPixelsProvider(
        [acquire = std::move(acquire), anariPass = m_anariPass](
            const std::uint32_t *pixels,
            std::uint32_t width,
            std::uint32_t height,
            const char *path) {
          // Device setup can run off-thread. The copied provider rejects that
          // thread before touching Scene/UI or creating an attestation.
          return acquire(pixels, width, height, path, anariPass->frameState());
        });

  syncImagePassState();
}

void Viewport::setRenderingEnabled(bool enabled)
{
  if (enabled != m_renderingEnabled)
    ++m_bindingGeneration;
  m_renderingEnabled = enabled;
  if (m_anariPass) {
    m_anariPass->setEnabled(enabled);
    if (!enabled)
      m_anariPass->waitForCompletion();
  }
}

void Viewport::releaseSceneReferences()
{
  teardownDevice();
}

void Viewport::camera_resetView(bool resetAzEl)
{
  const auto mode = m_camera.arcball->mode();
  auto axis = m_camera.arcball->axis();
  auto azel =
      resetAzEl ? vsr::math::float2(0.f, 20.f) : m_camera.arcball->azel();
  auto pose = m_rIdx->computeDefaultView();
  pose.mode = static_cast<int>(mode);
  pose.upAxis = static_cast<int>(axis);
  if (mode == vsr::rendering::ManipulatorMode::Look && !resetAzEl) {
    m_camera.arcball->setDistance(pose.azeldist.z);
    m_camera.arcball->setFixedDistance(pose.fixedDist);
  } else {
    m_camera.arcball->setConfig(pose);
    m_camera.arcball->setFixedDistance(pose.fixedDist);
    m_camera.arcball->setAzel(azel);
  }
  m_camera.arcballToken = 0;
}

void Viewport::camera_centerView()
{
  if (!BaseViewport::viewport_isActive())
    return;
  const auto mode = m_camera.arcball->mode();
  auto axis = m_camera.arcball->axis();
  auto azel = m_camera.arcball->azel();
  auto dist = m_camera.arcball->distance();
  auto fixedDist = m_camera.arcball->fixedDistance();
  auto pose = m_rIdx->computeDefaultView();
  pose.mode = static_cast<int>(mode);
  pose.upAxis = static_cast<int>(axis);
  if (mode == vsr::rendering::ManipulatorMode::Look) {
    m_camera.arcball->setCenter(pose.lookat);
    m_camera.arcball->setFixedDistance(fixedDist);
  } else {
    m_camera.arcball->setConfig(pose);
    m_camera.arcball->setAzel(azel);
    m_camera.arcball->setDistance(dist);
    m_camera.arcball->setFixedDistance(fixedDist);
  }
  m_camera.arcball->setAxis(axis);
  m_camera.arcballToken = 0;
}

void Viewport::renderer_clone()
{
  if (!m_renderers.current)
    return;

  auto *clone = vsr::scene::cloneObject(m_renderers.current.get());
  if (!clone || clone->type() != ANARI_RENDERER)
    return;

  auto cloneRef =
      appContext()->vsr.scene.getObject<vsr::scene::Renderer>(clone->index());
  if (!cloneRef)
    return;

  m_renderers.objects.push_back(cloneRef);
  m_renderers.current = cloneRef;
}

void Viewport::renderer_resetParameterDefaults()
{
  if (!m_device || !m_renderers.current)
    return;

  m_renderers.current->removeAllParameters();
  m_renderers.current->setCommonParameterDefaults();
  vsr::scene::parseANARIObjectInfo(*m_renderers.current,
      m_device,
      ANARI_RENDERER,
      m_renderers.current->subtype().c_str());
}

void Viewport::teardownDevice()
{
  m_bindingTransition.store(true, std::memory_order_release);
  m_activateAfterSetup = false;
  PresentationObserver observer;
  {
    std::lock_guard<std::mutex> lock(m_frameObservationMutex);
    observer = m_presentationObserver;
  }
  if (observer)
    observer({}, {});
  ++m_bindingGeneration;
  ++m_deviceGeneration;
  m_customFrameBindingUnknown = false;
  m_displaySize = {0.f, 0.f};
  m_displayFramebufferScale = {0.f, 0.f};
  const bool pipelineSetup = BaseViewport::imagePipeline_isSetup();

  if (pipelineSetup) {
    BaseViewport::viewport_setActive(false);
    BaseViewport::imagePipeline_teardown();
    BaseViewport::viewport_reshape(vsr::math::int2(1, 1));
  } else
    BaseViewport::viewport_setActive(false);

  m_anariPass = nullptr;
  m_visualizeAOVPass = nullptr;
  m_autoExposurePass = nullptr;
  m_toneMapPass = nullptr;
  m_outputTransformPass = nullptr;
  m_primitiveOutlinePass = nullptr;
  m_outlinePass = nullptr;
  m_worldBounds.reset();
  m_outputPass = nullptr;

  if (m_rIdx)
    appContext()->anari.releaseRenderIndex(appContext()->vsr.scene, m_device);
  m_rIdx = nullptr;
  m_libName.clear();

  m_camera.current = {};
  m_prevCamera = {};

  if (m_device)
    anari::release(m_device, m_device);

  m_renderers.objects.clear();
  m_renderers.current = nullptr;
  m_prevRenderer = {};

  m_device = nullptr;
}

void Viewport::pick(vsr::math::uint2 pixel, bool selectObject)
{
  if (!m_anariPass || !m_camera.current)
    return;

  vsr::rendering::PickRequest request;
  request.pixel = pixel;
  request.view =
      vsr::rendering::makeCameraView(*m_camera.current, *m_camera.arcball);

  const auto hit = vsr::rendering::pick(*m_anariPass, request);

  if (!selectObject) {
    if (!hit || !hit->position)
      return;
    const auto c = *hit->position;
    vsr::core::logStatus(
        "[viewport] pick center [%u, %u] depth %f | {%f, %f, %f}",
        pixel.x,
        pixel.y,
        hit->depth,
        c.x,
        c.y,
        c.z);
    m_camera.arcball->setCenter(c);
    return;
  }

  const auto object = hit ? hit->object : std::nullopt;
  if (object) {
    vsr::core::logStatus("[viewport] picked %s %zu @ (%u, %u) | z: %f",
        anari::toString(object->type),
        object->index,
        pixel.x,
        pixel.y,
        hit->depth);
  }

  auto *obj = object
      ? appContext()->vsr.scene.getObject(object->type, object->index)
      : nullptr;
  appContext()->setSelected(obj);
}

void Viewport::setSelectionVisibilityFilterEnabled(bool enabled)
{
  if (!enabled)
    m_rIdx->setFilterFunction({});
  else {
    m_rIdx->setFilterFunction([this](const vsr::scene::Object *obj) {
      auto selectedNode = appContext()->getFirstSelected();
      if (!selectedNode.valid())
        return true;
      auto *selectedObject = (*selectedNode)->getObject();
      return !selectedObject || obj == selectedObject;
    });
  }
}

void Viewport::camera_setUseImplicitAspectRatio(bool on)
{
  BaseViewport::camera_setUseImplicitAspectRatio(on);
  if (m_anariPass)
    m_anariPass->setUseImplicitAspectRatio(on);
}

void Viewport::updateFrame()
{
  if (m_prevRenderer == m_renderers.current && m_prevCamera == m_camera.current)
    return;

  if (!m_camera.current)
    m_camera.current = appContext()->vsr.scene.defaultCamera();

  if (m_camera.current) {
    m_anariPass->setCamera(m_rIdx->camera(m_camera.current->index()));
    m_prevCamera = m_camera.current;
  }
  if (m_renderers.current) {
    m_anariPass->setRenderer(m_rIdx->renderer(m_renderers.current->index()));
    m_prevRenderer = m_renderers.current;
  }
}

void Viewport::updateImage()
{
  auto frame = m_anariPass->getFrame();

  float progress = 0.f;
  auto haveProgress = anari::getProperty(
      m_device, frame, "refinementProgress", progress, ANARI_NO_WAIT);
  if (haveProgress)
    m_frameProgress = progress;
  else
    m_frameProgress.reset();

  auto selectedNode = appContext()->getFirstSelected();
  const auto *selectedObject =
      selectedNode.valid() ? (*selectedNode)->getObject() : nullptr;
  const bool doHighlight = !m_showOnlySelected && m_highlightSelection
      && selectedObject
      && (selectedObject->type() == ANARI_SURFACE
          || selectedObject->type() == ANARI_VOLUME);
  m_outlinePass->setOutlineId(doHighlight
          ? vsr::scene::encodeObjectId(
                selectedObject->type(), selectedObject->index())
          : vsr::scene::NO_OBJECT_ID);

  updateWorldBoundsOverlay();

  auto start = std::chrono::steady_clock::now();
  BaseViewport::imagePipeline_render();
  if (m_autoExposurePass)
    m_currentAutoExposure = m_autoExposurePass->currentExposure();
  auto end = std::chrono::steady_clock::now();
  m_latestFL = std::chrono::duration<float>(end - start).count() * 1000;

  float duration = 0.f;
  anari::getProperty(m_device, frame, "duration", duration, ANARI_NO_WAIT);

  m_latestAnariFL = duration * 1000;
  m_minFL = m_minFL ? std::min(*m_minFL, m_latestAnariFL) : m_latestAnariFL;
  m_maxFL = m_maxFL ? std::max(*m_maxFL, m_latestAnariFL) : m_latestAnariFL;
}

void Viewport::updateWorldBoundsOverlay()
{
  if (!m_worldBounds)
    return;

  m_worldBounds->setShown(m_showWorldBounds);
  m_worldBounds->setColor(m_worldBoundsColor);
  m_worldBounds->setWidth(uint32_t(std::max(1, m_worldBoundsWidth)));

  const auto view = m_camera.current
      ? vsr::rendering::makeCameraView(*m_camera.current, *m_camera.arcball)
      : std::nullopt;
  const auto bounds = m_showWorldBounds
      ? vsr::rendering::queryWorldBounds(m_device, m_rIdx->world())
      : std::nullopt;
  m_worldBounds->update(bounds, view);
}

void Viewport::syncImagePassState()
{
  if (!m_anariPass)
    return;

  ++m_bindingGeneration;
  m_anariPass->setColorFormat(m_colorFormat);

  if (m_visualizeAOVPass) {
    m_visualizeAOVPass->setAOVType(m_visualizeAOV);
    m_visualizeAOVPass->setDepthRange(
        m_depthVisualMinimum, m_depthVisualMaximum);
    m_visualizeAOVPass->setEdgeInvert(m_edgeInvert);
  }

  if (m_primitiveOutlinePass) {
    m_primitiveOutlinePass->setEnabled(m_outlinePrimitives
        && m_visualizeAOV == vsr::rendering::AOVType::NONE
        && sourceSupports(m_primitiveOutlinePass->requiredChannels()));
  }

  updateDisplayPassState();
}

bool Viewport::sourceSupports(vsr::rendering::ImageChannels channels) const
{
  return m_anariPass
      && vsr::rendering::hasChannels(
          m_anariPass->supportedChannels(), channels);
}

void Viewport::updateDisplayPassState()
{
  if (!m_toneMapPass || !m_outputTransformPass)
    return;
  ++m_bindingGeneration;

  const bool showBeauty = m_visualizeAOV == vsr::rendering::AOVType::NONE;
  if (m_autoExposurePass) {
    m_autoExposurePass->setEnabled(showBeauty && m_autoExposureEnabled);
    m_autoExposurePass->setHDREnabled(
        showBeauty && m_colorFormat == ANARI_FLOAT32_VEC4);
  }
  m_toneMapPass->setEnabled(showBeauty);
  m_outputTransformPass->setEnabled(showBeauty);
  m_toneMapPass->setAutoExposureEnabled(showBeauty && m_autoExposureEnabled);
  m_toneMapPass->setHDREnabled(
      showBeauty && m_colorFormat == ANARI_FLOAT32_VEC4);
  m_outputTransformPass->setColorFormat(m_colorFormat);
}

void Viewport::ui_menubar()
{
  if (ImGui::BeginMenuBar()) {
    ui_menubar_Device();
    ImGui::BeginDisabled(!viewport_isActive());
    BaseViewport::ui_menubar_Renderer();
    BaseViewport::ui_menubar_Camera();
    BaseViewport::ui_menubar_TransformManipulator();
    ui_menubar_Viewport();
    ui_menubar_World();
    ImGui::EndDisabled();
    ImGui::EndMenuBar();
  }
}

void Viewport::ui_menubar_Device()
{
  if (ImGui::BeginMenu("Device")) {
    const auto &deviceList = appContext()->anari.deviceList();
    for (auto &libName : deviceList) {
      const bool isThisLibrary = m_libName == libName;
      if (ImGui::RadioButton(libName.c_str(), isThisLibrary))
        setLibrary(libName);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Reload Current Device"))
      refreshCurrentDevice();
    ImGui::EndMenu();
  }
}

void Viewport::ui_menubar_Viewport()
{
  if (ImGui::BeginMenu("Viewport")) {
    {
      ImGui::Text("Format:");
      ImGui::Indent(INDENT_AMOUNT);
      anari::DataType format = m_colorFormat;
      if (ImGui::RadioButton(
              "UFIXED8_RGBA_SRGB", format == ANARI_UFIXED8_RGBA_SRGB))
        format = ANARI_UFIXED8_RGBA_SRGB;
      if (ImGui::RadioButton("UFIXED8_VEC4", format == ANARI_UFIXED8_VEC4))
        format = ANARI_UFIXED8_VEC4;
      if (ImGui::RadioButton("FLOAT32_VEC4", format == ANARI_FLOAT32_VEC4))
        format = ANARI_FLOAT32_VEC4;

      if (format != m_colorFormat) {
        m_colorFormat = format;
        syncImagePassState();
      }
      ImGui::Unindent(INDENT_AMOUNT);
    }

    ImGui::Separator();

    {
      ImGui::Text("Render Resolution:");
      ImGui::Indent(INDENT_AMOUNT);

      const float current = m_viewport.resolutionScale;
      if (ImGui::RadioButton("100%", current == 1.f))
        m_viewport.resolutionScale = 1.f;
      if (ImGui::RadioButton("75%", current == 0.75f))
        m_viewport.resolutionScale = 0.75f;
      if (ImGui::RadioButton("50%", current == 0.5f))
        m_viewport.resolutionScale = 0.5f;
      if (ImGui::RadioButton("25%", current == 0.25f))
        m_viewport.resolutionScale = 0.25f;
      if (ImGui::RadioButton("12.5%", current == 0.125f))
        m_viewport.resolutionScale = 0.125f;

      if (ImGui::BeginMenu("Custom")) {
        ImGui::DragFloat("##customRes",
            &m_viewport.resolutionScale,
            0.01f,
            0.1f,
            2.f,
            "%.2f");
        ImGui::EndMenu();
      }

      if (current != m_viewport.resolutionScale) {
        if (m_viewport.resolutionScale < 0.05f)
          m_viewport.resolutionScale = 0.05f;
        viewport_reshape(m_viewport.size);
      }

      ImGui::Unindent(INDENT_AMOUNT);
    }

    ImGui::Separator();

    {
      ImGui::Text("AOV Visualization:");
      ImGui::Indent(INDENT_AMOUNT);

      const char *aovItems[] = {"default",
          "depth",
          "albedo",
          "normal",
          "edges",
          "object ID",
          "primitive ID",
          "instance ID"};
      if (ImGui::BeginCombo("AOV", aovItems[int(m_visualizeAOV)])) {
        for (int i = 0; i < IM_ARRAYSIZE(aovItems); ++i) {
          const bool isSelected = i == int(m_visualizeAOV);
          const bool supported =
              sourceSupports(vsr::rendering::requiredChannels(
                  static_cast<vsr::rendering::AOVType>(i)));
          if (!supported)
            ImGui::BeginDisabled();
          if (ImGui::Selectable(aovItems[i], isSelected) && supported) {
            m_visualizeAOV = static_cast<vsr::rendering::AOVType>(i);
            syncImagePassState();
          }
          if (isSelected)
            ImGui::SetItemDefaultFocus();
          if (!supported)
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
      }

      ImGui::BeginDisabled(m_visualizeAOV != vsr::rendering::AOVType::DEPTH);
      bool depthRangeChanged = false;
      depthRangeChanged |= ImGui::DragFloat("Depth Minimum",
          &m_depthVisualMinimum,
          0.1f,
          0.f,
          m_depthVisualMaximum);
      depthRangeChanged |= ImGui::DragFloat("Depth Maximum",
          &m_depthVisualMaximum,
          0.1f,
          m_depthVisualMinimum,
          1e20f);
      if (depthRangeChanged) {
        m_visualizeAOVPass->setDepthRange(
            m_depthVisualMinimum, m_depthVisualMaximum);
        ++m_bindingGeneration;
      }
      ImGui::EndDisabled();

      ImGui::BeginDisabled(m_visualizeAOV != vsr::rendering::AOVType::EDGES);
      bool edgeSettingsChanged = false;
      edgeSettingsChanged |= ImGui::Checkbox("Invert Edges", &m_edgeInvert);
      if (edgeSettingsChanged) {
        m_visualizeAOVPass->setEdgeInvert(m_edgeInvert);
        ++m_bindingGeneration;
      }
      ImGui::EndDisabled();

      ImGui::Unindent(INDENT_AMOUNT);
    }

    ImGui::Separator();

    {
      ImGui::Text("Exposure:");
      ImGui::Indent(INDENT_AMOUNT);

      ImGui::BeginDisabled(m_colorFormat != ANARI_FLOAT32_VEC4
          || m_visualizeAOV != vsr::rendering::AOVType::NONE);

      if (ImGui::Checkbox("Auto Exposure", &m_autoExposureEnabled))
        updateDisplayPassState();

      if (m_autoExposureEnabled) {
        if (ImGui::DragFloat(
                "Compensation", &m_toneMapExposure, 0.05f, -10.f, 10.f)) {
          m_toneMapPass->setExposure(m_toneMapExposure);
          ++m_bindingGeneration;
        }
        ImGui::Text("Current EV: %.2f", m_currentAutoExposure);
      } else if (ImGui::DragFloat(
                     "Exposure", &m_toneMapExposure, 0.05f, -10.f, 10.f)) {
        m_toneMapPass->setExposure(m_toneMapExposure);
        ++m_bindingGeneration;
      }

      ImGui::EndDisabled();
      ImGui::Unindent(INDENT_AMOUNT);
    }

    ImGui::Separator();

    {
      ImGui::Text("Tonemapping:");
      ImGui::Indent(INDENT_AMOUNT);

      ImGui::BeginDisabled(m_colorFormat != ANARI_FLOAT32_VEC4
          || m_visualizeAOV != vsr::rendering::AOVType::NONE);

      const char *toneMapItems[] = {"None",
          "Reinhard",
          "ACES Filmic",
          "Hable",
          "Khronos PBR Neutral",
          "AgX"};
      if (int op = int(m_toneMapOperator); ImGui::Combo(
              "Operator", &op, toneMapItems, IM_ARRAYSIZE(toneMapItems))) {
        m_toneMapOperator = static_cast<vsr::rendering::ToneMapOperator>(op);
        m_toneMapPass->setOperator(m_toneMapOperator);
        ++m_bindingGeneration;
      }

      ImGui::EndDisabled();
      ImGui::Unindent(INDENT_AMOUNT);
    }

    ImGui::Separator();

    {
      ImGui::Text("Output Transform:");
      ImGui::Indent(INDENT_AMOUNT);

      ImGui::BeginDisabled(m_colorFormat == ANARI_UFIXED8_RGBA_SRGB
          || m_visualizeAOV != vsr::rendering::AOVType::NONE);

      if (ImGui::DragFloat("Gamma", &m_toneMapGamma, 0.01f, 0.1f, 5.f)) {
        m_outputTransformPass->setGamma(m_toneMapGamma);
        ++m_bindingGeneration;
      }

      ImGui::EndDisabled();
      ImGui::Unindent(INDENT_AMOUNT);
    }

    {
      ImGui::Text("Display:");
      ImGui::Indent(INDENT_AMOUNT);

      ImGui::BeginDisabled(m_showOnlySelected);
      ImGui::Checkbox("Highlight Selected", &m_highlightSelection);
      ImGui::EndDisabled();

      ImGui::BeginDisabled(!m_primitiveOutlinePass
          || !sourceSupports(m_primitiveOutlinePass->requiredChannels()));
      if (ImGui::Checkbox("Outline Primitives", &m_outlinePrimitives))
        syncImagePassState();
      ImGui::EndDisabled();

      if (ImGui::Checkbox("Only Show Selected", &m_showOnlySelected))
        setSelectionVisibilityFilterEnabled(m_showOnlySelected);

      ImGui::Unindent(INDENT_AMOUNT);
    }

    ImGui::Separator();

    {
      ImGui::Text("Overlay:");
      ImGui::Indent(INDENT_AMOUNT);

      ImGui::Checkbox("Axes", &m_showOrientationWidget);
      ImGui::Checkbox("Animation Time Slider", &m_showAnimationSlider);
      ImGui::Checkbox("Info Window", &m_showOverlay);
      if (ImGui::MenuItem("Reset Timing Stats")) {
        m_minFL.reset();
        m_maxFL.reset();
      }
      ImGui::Unindent(INDENT_AMOUNT);
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Take Screenshot")) {
      // Generate timestamped filename
      auto now = std::chrono::system_clock::now();
      auto time_t = std::chrono::system_clock::to_time_t(now);
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch())
          % 1000;

      std::stringstream ss;
      ss << "screenshot_"
         << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") << "_"
         << std::setfill('0') << std::setw(3) << ms.count() << ".png";

      // Ensure the screenshot is saved in the current working directory
      std::filesystem::path workingDir = std::filesystem::current_path();
      std::filesystem::path filename = workingDir / ss.str();

      // The last rendered frame, as displayed.
      vsr::rendering::saveImage(imagePipeline(), filename.string());
    }

    ImGui::EndMenu();
  }
}

void Viewport::ui_menubar_World()
{
  if (ImGui::BeginMenu("World")) {
    ImGui::Checkbox("Show Bounds", &m_showWorldBounds);

    ImGui::BeginDisabled(!m_showWorldBounds);
    ImGui::Indent(INDENT_AMOUNT);
    ImGui::ColorEdit4("Color##worldBounds",
        &m_worldBoundsColor.x,
        ImGuiColorEditFlags_NoInputs);
    if (ImGui::DragInt("Width##worldBounds", &m_worldBoundsWidth, 0.25f, 1, 16))
      m_worldBoundsWidth = std::max(1, m_worldBoundsWidth);
    ImGui::Unindent(INDENT_AMOUNT);
    ImGui::EndDisabled();

    ImGui::Separator();

    if (ImGui::MenuItem("Print Bounds")) {
      const auto b =
          vsr::rendering::queryWorldBounds(m_device, m_rIdx->world());
      if (b) {
        vsr::core::logStatus(
            "[viewport] current world bounds {%f, %f, %f} x {%f, %f, %f}",
            b->lower.x,
            b->lower.y,
            b->lower.z,
            b->upper.x,
            b->upper.y,
            b->upper.z);
      } else
        vsr::core::logStatus("[viewport] current world has no bounds");
    }

    ImGui::EndMenu();
  }
}

bool Viewport::ui_picking()
{
  if (!m_camera.current || !ImGui::IsWindowHovered()
      || !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    return false;

  const auto pixel = imagePixelUnderMouse();
  if (!pixel)
    return false;

  // Shift + double-click recenters the view; double-click selects.
  const bool pickCenter = ImGui::IsKeyDown(ImGuiKey_LeftShift);
  pick(*pixel, !pickCenter);
  return true;
}

std::optional<vsr::math::uint2> Viewport::imagePixelUnderMouse() const
{
  const auto size = m_viewport.renderSize;
  const auto extent = m_imageRectMax - m_imageRectMin;
  if (size.x <= 0 || size.y <= 0 || extent.x <= 0.f || extent.y <= 0.f)
    return {};

  const ImVec2 mouse = ImGui::GetMousePos();
  // The texture is drawn flipped vertically: ANARI row 0 is the bottom.
  const vsr::math::float2 uv((mouse.x - m_imageRectMin.x) / extent.x,
      (m_imageRectMax.y - mouse.y) / extent.y);
  if (uv.x < 0.f || uv.x >= 1.f || uv.y < 0.f || uv.y >= 1.f)
    return {}; // letterbox border

  return vsr::math::uint2(uint32_t(uv.x * size.x), uint32_t(uv.y * size.y));
}

void Viewport::ui_overlay()
{
  ImVec2 contentStart = ImGui::GetCursorStartPos();
  ImGui::SetCursorPos(ImVec2(contentStart[0] + 2.0f, contentStart[1] + 2.0f));

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.7f));

  ImGuiChildFlags childFlags = ImGuiChildFlags_Border
      | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY;
  ImGuiWindowFlags childWindowFlags =
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

  // Render overlay as a child window within the viewport.
  // This ensures it's properly occluded when other windows are on top.
  if (ImGui::BeginChild(
          "##viewportOverlay", ImVec2(0, 0), childFlags, childWindowFlags)) {
    ImGui::Text("  device: %s", m_libName.c_str());
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.f),
        "renderer: %s",
        m_renderers.current ? m_renderers.current->subtype().c_str() : "---");

    // Camera indicator
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
        "  camera: %s",
        m_camera.current ? m_camera.current->name().c_str() : "---");

    ImGui::Separator();

    ImGui::Text("viewport: %i x %i", m_viewport.size.x, m_viewport.size.y);
    ImGui::Text(
        "  render: %i x %i", m_viewport.renderSize.x, m_viewport.renderSize.y);

    ImGui::Separator();

    if (m_frameProgress)
      ImGui::Text("progress: %.2f%%", *m_frameProgress * 100.f);
    else
      ImGui::Text("progress: ---");
    ImGui::Text(" display: %.2fms", m_latestFL);
    ImGui::Text("   ANARI: %.2fms", m_latestAnariFL);
    ImGui::Text("   (min): %.2fms", m_minFL ? *m_minFL : 0.f);
    ImGui::Text("   (max): %.2fms", m_maxFL ? *m_maxFL : 0.f);

    const auto &passTimings = imagePipeline().getPassTimings();
    if (!passTimings.empty()) {
      ImGui::Separator();
      ImGui::Text("stages:");
      for (const auto &timing : passTimings) {
        ImGui::Text("  [%s] %s: %.2fms",
            vsr::rendering::toString(timing.role),
            timing.name,
            timing.milliseconds);
      }
    }
  }
  ImGui::EndChild();

  ImGui::PopStyleColor();
}

} // namespace vsr::ui::imgui

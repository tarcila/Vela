// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_animation
#include <vsr/animation/Animation.hpp>
#include <vsr/animation/AnimationManager.hpp>
// vsr_core
#include <vsr/core/Timer.hpp>
#include <vsr/scene/Scene.hpp>
// vsr_rendering
#include <vsr/rendering/pipeline/FrameChannelCatalog.h>
#include <vsr/rendering/pipeline/ImagePipeline.h>
#include <vsr/rendering/pipeline/passes/ChannelVisualizationPass.h>
#include <vsr/rendering/pipeline/saveImage.h>
#include <vsr/rendering/index/RenderIndexAllLayers.hpp>
#include <vsr/rendering/view/ManipulatorToAnari.hpp>
// vsr_app
#include <vsr/app/ApplicationDump.h>
#include <vsr/app/Context.h>
#include <vsr/app/FrameChannelOptions.h>
// std
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// Application state //////////////////////////////////////////////////////////

static std::unique_ptr<vsr::core::DataTree> g_stateFile;
static std::unique_ptr<vsr::rendering::RenderIndexAllLayers> g_renderIndex;
static std::unique_ptr<vsr::rendering::ImagePipeline> g_renderPipeline;
static vsr::core::Timer g_timer;
static vsr::rendering::Manipulator g_manipulator;
static std::vector<vsr::rendering::CameraPose> g_cameraPoses;
static std::unique_ptr<vsr::app::Context> g_ctx;
static vsr::rendering::ChannelVisualizationPass *g_visualization{nullptr};
static vsr::rendering::FrameChannelSelection g_selection;

static vsr::core::Token g_deviceName;
static anari::Library g_library{nullptr};
static anari::Device g_device{nullptr};
static anari::Camera g_camera{nullptr};

// Helper functions ///////////////////////////////////////////////////////////

static bool loadANARIDevice()
{
  auto statusFunc = [](const void *,
                        ANARIDevice,
                        ANARIObject,
                        ANARIDataType,
                        ANARIStatusSeverity severity,
                        ANARIStatusCode,
                        const char *message) {
    if (severity == ANARI_SEVERITY_FATAL_ERROR) {
      fprintf(stderr, "[ANARI][FATAL] %s\n", message);
      std::exit(1);
    } else if (severity == ANARI_SEVERITY_ERROR)
      fprintf(stderr, "[ANARI][ERROR] %s\n", message);
#if 0
  else if (severity == ANARI_SEVERITY_WARNING)
    fprintf(stderr, "[ANARI][WARN ] %s\n", message);
  else if (severity == ANARI_SEVERITY_PERFORMANCE_WARNING)
    fprintf(stderr, "[ANARI][PERF ] %s\n", message);
#endif
#if 0
  else if (severity == ANARI_SEVERITY_INFO)
    fprintf(stderr, "[ANARI][INFO ] %s\n", message);
  else if (severity == ANARI_SEVERITY_DEBUG)
    fprintf(stderr, "[ANARI][DEBUG] %s\n", message);
#endif
  };

  auto library = g_ctx->offline.renderer.libraryName;
  g_deviceName = library;

  printf("Loading ANARI device from '%s' library...", library.c_str());
  fflush(stdout);

  g_timer.start();
  g_library = anari::loadLibrary(library.c_str(), statusFunc);
  if (g_library)
    g_device = anari::newDevice(g_library, "default");
  g_timer.end();
  if (!g_device) {
    std::cerr << "Error: cannot initialize saved ANARI device '" << library
              << "'; check the saved library and library search path\n";
    return false;
  }
  anari::commitParameters(g_device, g_device);

  printf("done (%.2f ms)\n", g_timer.milliseconds());
  return true;
}

static int listFrameChannels()
{
  const auto &settings = g_ctx->offline.renderer;
  if (settings.activeRenderer < 0
      || size_t(settings.activeRenderer) >= settings.rendererObjects.size()) {
    std::cerr << "Error: invalid saved renderer selection for device '"
              << settings.libraryName << "'; save a valid offline renderer\n";
    return 1;
  }
  const auto &ro = settings.rendererObjects[settings.activeRenderer];
  auto renderer =
      anari::newObject<anari::Renderer>(g_device, ro.subtype().c_str());
  if (!renderer) {
    std::cerr << "Error: cannot initialize saved renderer '" << ro.subtype()
              << "' on device '" << settings.libraryName
              << "'; check the saved renderer\n";
    return 1;
  }
  ro.updateAllANARIParameters(g_device, renderer);
  anari::commitParameters(g_device, renderer);
  const auto catalog = vsr::rendering::discoverFrameChannels(g_device);
  vsr::rendering::printFrameChannels(std::cout, catalog);
  anari::release(g_device, renderer);
  if (!catalog.usable()) {
    std::cerr << "Error: no usable Frame Channel catalog for device '"
              << settings.libraryName << "', renderer '" << ro.subtype()
              << "'; check device metadata and supported pixel types\n";
    return 1;
  }
  return 0;
}

static bool reportChannelError(
    const std::string &error, const std::string &requested = {})
{
  const auto &settings = g_ctx->offline.renderer;
  const std::string renderer = settings.activeRenderer >= 0
          && size_t(settings.activeRenderer) < settings.rendererObjects.size()
      ? settings.rendererObjects[settings.activeRenderer].subtype().c_str()
      : "<invalid>";
  std::cerr
      << "Error: Frame Channel '"
      << (requested.empty() ? g_selection.deviceName : requested)
      << "', device '" << settings.libraryName << "', renderer '" << renderer
      << "': " << error
      << "; use --list-channels for available channels and visualizations\n";
  return false;
}

static bool validateChannelSelection(
    const vsr::app::FrameChannelOptions &options)
{
  const auto fail = [&](const std::string &error) {
    return reportChannelError(
        error, options.channel.value_or(g_selection.deviceName));
  };
  const auto &settings = g_ctx->offline.renderer;
  if (settings.activeRenderer < 0
      || size_t(settings.activeRenderer) >= settings.rendererObjects.size())
    return fail("invalid saved renderer selection");
  const auto &ro = settings.rendererObjects[settings.activeRenderer];
  auto renderer =
      anari::newObject<anari::Renderer>(g_device, ro.subtype().c_str());
  if (!renderer)
    return fail("cannot initialize saved renderer");
  ro.updateAllANARIParameters(g_device, renderer);
  anari::commitParameters(g_device, renderer);
  const auto catalog = vsr::rendering::discoverFrameChannels(g_device);
  anari::release(g_device, renderer);
  if (!g_ctx->offline.channelSelectionError.empty())
    return fail(g_ctx->offline.channelSelectionError);
  g_selection = g_ctx->offline.channelSelection.value_or(g_selection);
  std::string error;
  if (!vsr::app::applyFrameChannelOptions(options, catalog, g_selection, error))
    return fail(error);
  vsr::rendering::ChannelVisualizationPass validation;
  if (!validation.setSelection(g_selection))
    return fail(validation.error());
  return true;
}

static void initVSRDataTree()
{
  printf("Initializing VSR data tree...");
  fflush(stdout);

  g_timer.start();
  g_stateFile = std::make_unique<vsr::core::DataTree>();
  g_timer.end();

  printf("done (%.2f ms)\n", g_timer.milliseconds());
}

static void initVSRRenderIndex()
{
  printf("Initializing VSR render index...");
  fflush(stdout);

  g_timer.start();
  g_renderIndex = std::make_unique<vsr::rendering::RenderIndexAllLayers>(
      g_ctx->vsr.scene, g_deviceName, g_device);
  g_timer.end();

  printf("done (%.2f ms)\n", g_timer.milliseconds());
}

static bool loadState(const char *filename)
{
  printf("Loading state from '%s'...", filename);
  fflush(stdout);

  g_timer.start();
  const bool loaded = g_stateFile->load(filename);
  g_timer.end();

  printf("%s (%.2f ms)\n", loaded ? "done" : "failed", g_timer.milliseconds());
  return loaded;
}

static bool populateVSRContext()
{
  printf("Populating VSR context...");
  fflush(stdout);

  g_timer.start();
  const bool populated =
      vsr::app::deserialize_ApplicationDump(*g_ctx, g_stateFile->root());
  g_timer.end();

  printf(
      "%s (%.2f ms)\n", populated ? "done" : "failed", g_timer.milliseconds());
  return populated;
}

static void populateRenderIndex()
{
  printf("Populating VSR render index...");
  fflush(stdout);

  g_timer.start();
  g_renderIndex->populate();
  g_timer.end();

  printf("done (%.2f ms)\n", g_timer.milliseconds());
}

static void setupCameraManipulator()
{
  printf("Setting up camera...");
  fflush(stdout);

  g_timer.start();
  if (!g_ctx->view.poses.empty()) {
    g_cameraPoses = g_ctx->view.poses;
    printf("using %zu camera poses from file...", g_cameraPoses.size());
    fflush(stdout);
  } else {
    printf("from world bounds...");
    fflush(stdout);
    g_cameraPoses.push_back(g_renderIndex->computeDefaultView());
  }
  g_timer.end();

  printf("done (%.2f ms)\n", g_timer.milliseconds());
}

static void setupImagePipeline()
{
  const auto frameWidth = g_ctx->offline.frame.width;
  const auto frameHeight = g_ctx->offline.frame.height;

  printf("Setting up render pipeline (%u x %u)...", frameWidth, frameHeight);
  fflush(stdout);

  g_timer.start();
  g_renderPipeline =
      std::make_unique<vsr::rendering::ImagePipeline>(frameWidth, frameHeight);

  g_camera = anari::newObject<anari::Camera>(g_device, "perspective");
  anari::setParameter(
      g_device, g_camera, "aspect", frameWidth / float(frameHeight));
  anari::setParameter(g_device, g_camera, "fovy", anari::radians(40.f));
  anari::setParameter(g_device,
      g_camera,
      "apertureRadius",
      g_ctx->offline.camera.apertureRadius);
  anari::setParameter(
      g_device, g_camera, "focusDistance", g_ctx->offline.camera.focusDistance);
  anari::commitParameters(g_device, g_camera);

  auto activeRenderer = g_ctx->offline.renderer.activeRenderer;
  auto &ro = g_ctx->offline.renderer.rendererObjects[activeRenderer];
  auto r = anari::newObject<anari::Renderer>(g_device, ro.subtype().c_str());
  ro.updateAllANARIParameters(g_device, r);
  anari::commitParameters(g_device, r);

  auto *arp = g_renderPipeline->setSource<vsr::rendering::AnariSceneRenderPass>(
      g_device);
  arp->setWorld(g_renderIndex->world());
  arp->setRenderer(r);
  arp->setCamera(g_camera);
  arp->setRunAsync(false);

  g_visualization =
      g_renderPipeline->addPass<vsr::rendering::ChannelVisualizationPass>();
  g_visualization->setSelection(g_selection);

  anari::release(g_device, r);

  g_timer.end();

  printf("done (%.2f ms)\n", g_timer.milliseconds());
}

static std::string frameFilename(int i)
{
  char buf[32];
  snprintf(buf, sizeof(buf), "vsrRender_%04d.png", i);
  return buf;
}

static bool renderSamples(uint32_t samples)
{
  for (uint32_t s = 0; s < samples; ++s) {
    g_renderPipeline->render();
    if (g_visualization->status() == vsr::rendering::FrameChannelStatus::FAILED)
      return reportChannelError(g_visualization->error());
  }
  if (g_visualization->status() != vsr::rendering::FrameChannelStatus::VALID)
    return reportChannelError("no completed selected-channel frame to save");
  return true;
}

static bool renderFrames()
{
  const auto frameWidth = g_ctx->offline.frame.width;
  const auto frameHeight = g_ctx->offline.frame.height;
  const auto frameSamples = g_ctx->offline.frame.samples;

  printf("Rendering frames (%u spp)...\n", frameSamples);
  fflush(stdout);

  g_timer.start();

  // Check for camera animations
  bool hasCameraAnimation = false;
  const vsr::scene::Object *animatedCamera = nullptr;
  for (auto &anim : g_ctx->vsr.animationMgr.animations()) {
    for (auto &b : anim.objectParameterBindings()) {
      if (b.target() && b.target()->type() == ANARI_CAMERA) {
        hasCameraAnimation = true;
        animatedCamera = b.target();
        break;
      }
    }
    if (hasCameraAnimation)
      break;
  }

  if (hasCameraAnimation) {
    const int totalFrames = g_ctx->vsr.animationMgr.getAnimationTotalFrames();

    // If no animated camera, set static pose once from saved poses
    if (!animatedCamera) {
      g_manipulator.setConfig(g_cameraPoses[0]);
      vsr::rendering::updateCameraParametersPerspective(
          g_device, g_camera, g_manipulator);
      anari::commitParameters(g_device, g_camera);
    }

    printf("...animating %d frames...\n", totalFrames);

    for (int i = 0; i < totalFrames; i++) {
      g_ctx->vsr.animationMgr.setAnimationFrame(i);

      if (animatedCamera) {
        using anari::math::float3;
        if (auto v = animatedCamera->parameterValueAs<float3>("position"))
          anari::setParameter(g_device, g_camera, "position", *v);
        if (auto v = animatedCamera->parameterValueAs<float3>("direction"))
          anari::setParameter(g_device, g_camera, "direction", *v);
        if (auto v = animatedCamera->parameterValueAs<float3>("up"))
          anari::setParameter(g_device, g_camera, "up", *v);
        if (auto v = animatedCamera->parameterValueAs<float>("fovy"))
          anari::setParameter(g_device, g_camera, "fovy", *v);
        anari::commitParameters(g_device, g_camera);
      }

      printf("...frame %d / %d...\n", i, totalFrames - 1);
      fflush(stdout);

      if (!renderSamples(frameSamples))
        return false;

      vsr::rendering::saveImage(*g_renderPipeline, frameFilename(i));
    }
  } else {
    // Original camera-pose turntable behavior
    for (size_t i = 0; i < g_cameraPoses.size(); i++) {
      g_manipulator.setConfig(g_cameraPoses[i]);
      vsr::rendering::updateCameraParametersPerspective(
          g_device, g_camera, g_manipulator);
      anari::commitParameters(g_device, g_camera);

      printf("...frame %zu...\n", i);
      fflush(stdout);

      if (!renderSamples(frameSamples))
        return false;

      vsr::rendering::saveImage(*g_renderPipeline, frameFilename(i));
    }
  }

  g_timer.end();

  printf("...done (%.2f ms)\n", g_timer.milliseconds());
  return true;
}

static void cleanup()
{
  printf("Cleanup objects...");
  fflush(stdout);

  g_timer.start();
  g_renderPipeline.reset();
  g_renderIndex.reset();
  g_stateFile.reset();
  anari::release(g_device, g_camera);
  anari::release(g_device, g_device);
  anari::unloadLibrary(g_library);
  g_timer.end();

  printf("done (%.2f ms)\n", g_timer.milliseconds());
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int main(int argc, const char *argv[])
{
  bool listChannels = false;
  vsr::app::FrameChannelOptions options;
  const char *stateFilename = nullptr;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    std::string error;
    const auto parsed =
        vsr::app::parseFrameChannelOption(argc, argv, i, options, error);
    if (parsed == vsr::app::FrameChannelOptionResult::ERROR) {
      std::cerr << "Error: "
                << vsr::app::frameChannelOptionError(argc, argv, error) << '\n';
      return 1;
    }
    if (parsed == vsr::app::FrameChannelOptionResult::PARSED)
      continue;
    if (arg == "--help") {
      printf(
          "usage: %s <state_file.vsr> [channel options] [--list-channels]\n"
          "  --list-channels  List saved device channels/types/compatible "
          "visualizations; no image output\n",
          argv[0]);
      printf("%s", vsr::app::frameChannelOptionsHelp());
      return 0;
    } else if (arg == "--list-channels") {
      listChannels = true;
    } else if (!arg.empty() && arg[0] != '-' && !stateFilename) {
      stateFilename = argv[i];
    } else {
      fprintf(stderr, "Error: unexpected argument '%s'; use --help\n", argv[i]);
      return 1;
    }
  }
  if (!stateFilename) {
    fprintf(stderr, "usage: %s <state_file.vsr> [--list-channels]\n", argv[0]);
    return 1;
  }

  g_ctx = std::make_unique<vsr::app::Context>();

  initVSRDataTree();
  if (!loadState(stateFilename) || !populateVSRContext())
    return 1;
  if (!loadANARIDevice())
    return 1;
  if (listChannels) {
    const int result = listFrameChannels();
    cleanup();
    g_ctx.reset();
    return result;
  }
  if (!validateChannelSelection(options)) {
    cleanup();
    return 1;
  }
  initVSRRenderIndex();
  populateRenderIndex();
  setupCameraManipulator();
  setupImagePipeline();
  const bool rendered = renderFrames();
  cleanup();

  g_ctx.reset();

  return rendered ? 0 : 1;
}

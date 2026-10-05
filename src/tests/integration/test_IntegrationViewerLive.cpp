// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#ifdef VSR_TEST_VIEWER_UI
#include "catch.hpp"
// vsr_ui_imgui
#include "vsr/ui/imgui/Application.h"
#include "vsr/ui/imgui/windows/Viewport.h"
// vsr_io
#include "vsr/io/procedural.hpp"
// vsr_integration
#include "vsr/integration/frame/ViewerFrameSource.hpp"
#include "vsr/integration/mcp/Json.hpp"
#ifdef __linux__
#include <arpa/inet.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <unistd.h>
#include "vsr/integration/mcp/McpServer.hpp"
#include "vsr/integration/mcp/tools/FrameTools.hpp"
#include "vsr/integration/mcp/tools/LookupTools.hpp"
#include "vsr/integration/mcp/tools/QueryTools.hpp"
#endif
// Independent decoder (test only)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../../../external/vela_stb_image/stb_image.h"
// std
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>

namespace {

// Optional profiler markers bracket only observation callbacks, never the
// normal SDL transfer/render. Loaded only in a private opted-in test process.
struct ObservationTrace
{
  explicit ObservationTrace(const char *name);
  ~ObservationTrace();
  static void initialize();
  static int (*push)(const char *);
  static int (*pop)();
};

int (*ObservationTrace::push)(const char *) = nullptr;
int (*ObservationTrace::pop)() = nullptr;

void ObservationTrace::initialize()
{
#ifdef __linux__
  if (const auto *library = std::getenv("VSR_VIEWER_TEST_NVTX")) {
    auto *handle = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    REQUIRE(handle);
    push = reinterpret_cast<int (*)(const char *)>(
        dlsym(handle, "nvtxRangePushA"));
    pop = reinterpret_cast<int (*)()>(dlsym(handle, "nvtxRangePop"));
    REQUIRE(push);
    REQUIRE(pop);
  }
#endif
}

ObservationTrace::ObservationTrace(const char *name)
{
  if (push)
    push(name);
}

ObservationTrace::~ObservationTrace()
{
  if (pop)
    pop();
}

// Real ANARI geometry/material/image sampler, not injected pipeline pixels.
// Scene and normal rendering allocations are not observation-owned resources.
void generateEntropyPlane(vsr::scene::Scene &scene)
{
  using namespace vsr::scene;
  using namespace vsr::math;
  auto geometry = scene.createObject<Geometry>("triangle");
  const std::array<float3, 4> vertices = {float3(-4.f, -4.f, 0.f),
      float3(4.f, -4.f, 0.f),
      float3(4.f, 4.f, 0.f),
      float3(-4.f, 4.f, 0.f)};
  const std::array<float2, 4> coordinates = {
      float2(0.f, 0.f), float2(1.f, 0.f), float2(1.f, 1.f), float2(0.f, 1.f)};
  const std::array<uint3, 2> triangles = {uint3(0, 1, 2), uint3(0, 2, 3)};
  auto positions = scene.createArray(ANARI_FLOAT32_VEC3, 4);
  positions->setData(vertices.data());
  auto uv = scene.createArray(ANARI_FLOAT32_VEC2, 4);
  uv->setData(coordinates.data());
  auto indices = scene.createArray(ANARI_UINT32_VEC3, 2);
  indices->setData(triangles.data());
  geometry->setParameterObject("vertex.position", *positions);
  geometry->setParameterObject("vertex.attribute0", *uv);
  geometry->setParameterObject("primitive.index", *indices);
  auto texture = scene.createArray(ANARI_UFIXED8_RGBA_SRGB, 4096, 4096);
  auto *pixels = texture->mapAs<std::uint32_t>();
  std::uint32_t random = 0x71395ab1;
  for (std::size_t i = 0; i < 4096u * 4096u; ++i) {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    pixels[i] = random | 0xff000000u;
  }
  texture->unmap();
  auto sampler = scene.createObject<Sampler>("image2D");
  sampler->setParameterObject("image", *texture);
  sampler->setParameter("inAttribute", "attribute0");
  sampler->setParameter("filter", "nearest");
  auto material = scene.createObject<Material>("matte");
  material->setParameterObject("color", *sampler);
  auto surface = scene.createSurface("Real entropy plane", geometry, material);
  scene.insertChildObjectNode(scene.defaultLayer()->root(), surface);
}

struct ProbeViewport : vsr::ui::imgui::Viewport
{
  ProbeViewport(vsr::ui::imgui::Application *app,
      const char *name = "Presentation Probe",
      bool large = false,
      bool second = false);
  void buildUI() override;
  void setFrameStateProvider(std::function<FrameState()> provider);
  void setFrameObservation(
      ReadyPixelsProvider acquire, PresentationObserver present);
  bool resized{false}, large{false}, second{false};
  const std::thread::id owner{std::this_thread::get_id()};
  std::atomic<std::size_t> offOwnerStarts{0};
  std::size_t ownerStarts{0}, mismatchedAcquisitions{0};
};

ProbeViewport::ProbeViewport(vsr::ui::imgui::Application *app,
    const char *name,
    bool maximum,
    bool secondary)
    : Viewport(app, nullptr, name), large(maximum), second(secondary)
{}

void ProbeViewport::setFrameStateProvider(std::function<FrameState()> provider)
{
  Viewport::setFrameStateProvider(provider
          ? std::function<FrameState()>([this, provider = std::move(provider)] {
              if (std::this_thread::get_id() != owner) {
                ++offOwnerStarts;
                return FrameState{};
              }
              ++ownerStarts;
              ObservationTrace trace("04.observation.start");
              return provider();
            })
          : std::function<FrameState()>{});
}

void ProbeViewport::setFrameObservation(
    ReadyPixelsProvider acquire, PresentationObserver present)
{
  Viewport::setFrameObservation(acquire
          ? ReadyPixelsProvider([this, acquire = std::move(acquire)](
                                    const std::uint32_t *pixels,
                                    std::uint32_t width,
                                    std::uint32_t height,
                                    const char *path,
                                    FrameState state) {
              ObservationTrace trace("04.observation.acquire");
              auto result = acquire(pixels, width, height, path, state);
              if (result) {
                const auto receipt = std::static_pointer_cast<
                    const vsr::integration::frame::ViewerAcquisition>(result);
                if (receipt->production->binding.sourceInstanceGeneration
                    != effectiveView().sourceInstanceGeneration)
                  ++mismatchedAcquisitions;
              }
              return result;
            })
          : ReadyPixelsProvider{},
      present ? PresentationObserver([present = std::move(present)](
                                         auto receipt, FrameState state) {
        ObservationTrace trace("04.observation.present");
        present(receipt, std::move(state));
      })
              : PresentationObserver{});
}

void ProbeViewport::buildUI()
{
  ImGui::SetWindowPos({second ? 1940.f : 0.f, 0.f});
  ImGui::SetWindowSize(resized ? ImVec2(400.f, 300.f)
          : large ? (second ? ImVec2(2056.f, 1076.f) : ImVec2(1928.f, 1144.f))
                  : ImVec2(620.f, 460.f));
  Viewport::buildUI();
}

struct ProbeApplication : vsr::ui::imgui::Application
{
  explicit ProbeApplication(
      std::string device, bool observe = true, bool pressure = false);
  vsr::ui::imgui::WindowArray setupWindows() override;
  void uiRenderEnd() override;
  void uiFrameStart() override;
  void uiRenderBeforePresent() override;
  void teardown() override;
  const char *applicationIdentifier() const override;
  const char *getDefaultLayout() const override;
  std::string device;
  bool observe{true}, pressure{false}, secondaryStarted{false};
  bool native{std::getenv("VSR_VIEWER_TEST_NATIVE") != nullptr};
#ifdef __linux__
  std::unique_ptr<vsr::integration::inspection::Inspector> inspector;
  vsr::integration::descriptor::RuntimeDirectory runtime;
  std::unique_ptr<vsr::integration::mcp::McpServer> server;
#endif
  std::size_t oracleFiles{0};
  bool entropy{std::getenv("VSR_VIEWER_TEST_ENTROPY") != nullptr};
  bool entropyPose{false}, nativeWithdrawn{false}, metadataLoaded{false};
  bool lifetimeChecked{false};
  bool destructive{std::getenv("VSR_VIEWER_TEST_DESTRUCTIVE") != nullptr};
  bool metadataPressure{
      std::getenv("VSR_VIEWER_TEST_METADATA_PRESSURE") != nullptr};
  std::size_t maxPngBytes{0};
#ifdef __linux__
  std::array<int, 2> slowSockets{-1, -1};
  std::size_t drainBaseline{0}, drainHeld{0}, drainHeartbeat{0};
  bool drainsStarted{false}, drainsHeld{false}, drainsReleased{false};
  std::array<bool, 2> imageResponses{false, false};
  int openSlowImage(const vsr::integration::operations::OperationRef &ref);
#endif
  ProbeViewport *viewport{nullptr}, *secondary{nullptr};
  std::size_t heartbeats{0}, checkedSubmissions{0};
  bool matchedState{false}, matchedDevice{true}, sawResize{false};
  bool resizeRequested{false}, sawRetirement{false}, refreshRequested{false};
  std::uint64_t retiredGeneration{0}, replacementGeneration{0},
      secondaryGeneration{0};
  std::size_t secondaryAtRetirement{0}, secondaryAfterRetirement{0};
  std::size_t primaryOffOwnerStarts{0}, primaryOwnerStarts{0},
      primaryMismatchedAcquisitions{0}, secondaryOffOwnerStarts{0},
      secondaryOwnerStarts{0}, secondaryMismatchedAcquisitions{0};
  std::uint32_t firstWidth{0};
  double maxUploadMs{0.}, sumUploadMs{0.};
  std::string transferPath, sdlRendererName;
  vsr::rendering::Manipulator manipulator, secondaryManipulator;
  vsr::integration::SceneId sceneId{};
  bool movedCamera{false}, changedRenderer{false}, selectedOutline{false};
  std::size_t staleImages{0}, outlineImages{0};
  std::size_t originalSizeImages{0}, resizedImages{0};
  vsr::integration::Integration integration;
  vsr::integration::identity::IdentityAuthority ids;
  std::unique_ptr<vsr::integration::identity::SceneIdentity> identity;
  std::unique_ptr<vsr::integration::identity::ViewRevisionTracker> tracker,
      secondaryTracker;
  std::unique_ptr<vsr::integration::session::SessionAuthority> authority;
  std::unique_ptr<vsr::integration::operations::OperationEngine> engine;
  std::unique_ptr<vsr::integration::frame::FrameService> frames;
  std::unique_ptr<vsr::integration::frame::ViewerFrameSource<ProbeViewport>>
      source, secondarySource;
  vsr::integration::operations::Credentials credentials;
  vsr::integration::ViewId viewId{}, secondaryId{}, pendingView{};
  vsr::integration::operations::OperationRef operation;
  std::size_t requests{0}, images{0}, cold{0}, peakOwned{0};
  std::uint64_t firstQualifiedRequest{0};
  bool firstQualifiedAvailable{false};
  std::size_t primaryImages{0}, secondaryImages{0}, secondarySubmissions{0};
  std::uint32_t maxNativeWidth{0}, maxNativeHeight{0};
  std::vector<std::shared_ptr<const vsr::integration::frame::EncodedFrame>>
      retained;
  std::size_t retainedPeak{0};
  bool requestPending{false};
  bool withdrawalStarted{false}, cancelledDrained{false},
      withdrawalProtected{false};
  std::size_t withdrawalHeartbeat{0}, beforeWithdrawalBytes{0},
      afterWithdrawalBytes{0};
  vsr::integration::operations::OperationRef historical, cancelled;
  std::vector<std::uint8_t> firstImage;
  double maxPinMs{0.}, setupMs{0.}, releaseMs{0.}, maxDeliveryMs{0.};
  std::chrono::steady_clock::time_point requestedAt;
  vsr::integration::frame::ViewerFrameMetrics sourceMetrics, secondaryMetrics;
  SDL_Surface *oracle{nullptr}, *currentOracle{nullptr},
      *secondaryOracle{nullptr};
  struct DisplayOracle
  {
    vsr::integration::ViewId view;
    std::uint64_t presentation;
    std::shared_ptr<SDL_Surface> pixels;
  };
  std::vector<DisplayOracle> oracleHistory;
  std::size_t comparedPixels{0}, parityImages{0};
  double parityError{0.}, flippedError{0.};
  ~ProbeApplication() override;
};

ProbeApplication::ProbeApplication(
    std::string value, bool observing, bool loading)
    : device(std::move(value)), observe(observing), pressure(loading)
{
  commandLineOptions()->useDefaultLayout = false;
}

ProbeApplication::~ProbeApplication()
{
  SDL_DestroySurface(oracle);
  SDL_DestroySurface(currentOracle);
  SDL_DestroySurface(secondaryOracle);
}

const char *ProbeApplication::applicationIdentifier() const
{
  return "viewport-presentation-test";
}

const char *ProbeApplication::getDefaultLayout() const
{
  return "";
}

#ifdef __linux__
int ProbeApplication::openSlowImage(
    const vsr::integration::operations::OperationRef &ref)
{
  const auto socket = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  REQUIRE(socket >= 0);
  int receiveWindow = 4096;
  REQUIRE(
      ::setsockopt(
          socket, SOL_SOCKET, SO_RCVBUF, &receiveWindow, sizeof(receiveWindow))
      == 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(server->port());
  REQUIRE(
      ::connect(socket, reinterpret_cast<sockaddr *>(&address), sizeof(address))
      == 0);
  const auto body =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"vsr_view_frame_image\",\"arguments\":{\"operation_ref\":{\"session_id\":\""
      + ref.sessionId + "\",\"request_id\":" + std::to_string(ref.requestId)
      + "}}}}";
  const auto request = "POST /mcp HTTP/1.1\r\nHost: " + server->authority()
      + "\r\nAuthorization: Bearer " + credentials.launchCredential
      + "\r\nVSR-Session-ID: " + credentials.sessionId
      + "\r\nVSR-Session-Proof: Bearer " + credentials.sessionProof
      + "\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: "
      + std::to_string(body.size()) + "\r\n\r\n" + body;
  std::size_t sent = 0;
  while (sent < request.size()) {
    const auto count = ::send(
        socket, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
    REQUIRE(count > 0);
    sent += std::size_t(count);
  }
  return socket;
}
#endif

vsr::ui::imgui::WindowArray ProbeApplication::setupWindows()
{
  auto windows = vsr::ui::imgui::Application::setupWindows();
  auto &scene = appContext()->vsr.scene;
  if (entropy)
    generateEntropyPlane(scene);
  else
    vsr::io::generate_icosphere(scene, {}, 1, false);
  vsr::io::generate_default_lights(scene);
  appContext()->vsr.sceneLoadComplete = true;
  auto window =
      std::make_unique<ProbeViewport>(this, "Presentation Probe", pressure);
  viewport = window.get();
  viewport->setManipulator(&manipulator);
  ObservationTrace::initialize();
  viewport->setFrameStateProvider([] { return ProbeViewport::FrameState{}; });
  vsr::core::DataTree settings;
  auto *stateWindow = static_cast<vsr::ui::imgui::Window *>(viewport);
  stateWindow->saveSettings(settings.root());
  settings.root()["showOverlay"] = false;
  settings.root()["viewport.showOrientationWidget"] = false;
  settings.root()["viewport.showTimeSlider"] = false;
  settings.root()["gizmo.active"] = false;
  if (pressure)
    settings.root()["viewport.scale"] = 1.f;
  stateWindow->loadSettings(settings.root());
  viewport->setLibrary(device);
  windows.emplace_back(std::move(window));
  if (pressure && !std::getenv("VSR_VIEWER_TEST_SINGLE")) {
    auto other =
        std::make_unique<ProbeViewport>(this, "Second Probe", true, true);
    secondary = other.get();
    secondaryManipulator.rotate({35.f, 60.f});
    secondary->setManipulator(&secondaryManipulator);
    secondary->setFrameStateProvider(
        [] { return ProbeViewport::FrameState{}; });
    static_cast<vsr::ui::imgui::Window *>(secondary)->loadSettings(
        settings.root());
    windows.emplace_back(std::move(other));
  }
  return windows;
}

void ProbeApplication::uiFrameStart()
{
  vsr::ui::imgui::Application::uiFrameStart();
  // The application's task modal owns one asynchronous device setup at a time.
  // Do not overwrite that task while the first viewport is still warming up.
  if (secondary && !secondaryStarted && checkedSubmissions >= 4
      && viewport->effectiveView().unavailableReason.empty()) {
    secondaryStarted = true;
    secondary->setLibrary(device);
  }
  if (entropy && !entropyPose && checkedSubmissions >= 6
      && (!secondary || secondary->effectiveView().unavailableReason.empty())) {
    entropyPose = true;
    manipulator.setConfig({0.f, 0.f, 0.f}, 3.f, {0.f, 0.f});
    secondaryManipulator.setConfig({0.f, 0.f, 0.f}, 3.f, {0.f, 0.f});
  }
  if (engine) {
    integration.service();
    integration.submit(0, [this](auto &) { frames->pump(); });
    integration.service();
  }
}

void ProbeApplication::teardown()
{
  const auto start = std::chrono::steady_clock::now();
  // Application::run destroys all windows before returning to the test.
  // Preserve scalar evidence while they are still live, never retired pointers.
  primaryOffOwnerStarts = viewport->offOwnerStarts.load();
  primaryOwnerStarts = viewport->ownerStarts;
  primaryMismatchedAcquisitions = viewport->mismatchedAcquisitions;
  if (secondary) {
    secondaryOffOwnerStarts = secondary->offOwnerStarts.load();
    secondaryOwnerStarts = secondary->ownerStarts;
    secondaryMismatchedAcquisitions = secondary->mismatchedAcquisitions;
  }
  if (source)
    sourceMetrics = source->metrics();
  if (secondarySource)
    secondaryMetrics = secondarySource->metrics();
#ifdef __linux__
  for (auto &socket : slowSockets) {
    if (socket >= 0)
      ::close(socket);
    socket = -1;
  }
  server.reset();
  inspector.reset();
#endif
  source.reset();
  secondarySource.reset();
  retained.clear();
  frames.reset();
  releaseMs = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start)
                  .count();
  engine.reset();
  authority.reset();
  tracker.reset();
  secondaryTracker.reset();
  identity.reset();
  vsr::ui::imgui::Application::teardown();
}

void ProbeApplication::uiRenderBeforePresent()
{
  SDL_DestroySurface(currentOracle);
  currentOracle = nullptr;
  if (secondary) {
    SDL_DestroySurface(secondaryOracle);
    secondaryOracle = nullptr;
    if (auto drawn = secondary->queuedTextureDraw()) {
      const SDL_Rect rect{int(drawn->displayX),
          int(drawn->displayY),
          int(drawn->displayWidth),
          int(drawn->displayHeight)};
      auto *read = SDL_RenderReadPixels(sdlRenderer(), &rect);
      REQUIRE(read);
      secondaryOracle = SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGBA32);
      SDL_DestroySurface(read);
      REQUIRE(secondaryOracle);
    }
  }
  if (auto drawn = viewport->queuedTextureDraw()) {
    const SDL_Rect rect{int(drawn->displayX),
        int(drawn->displayY),
        int(drawn->displayWidth),
        int(drawn->displayHeight)};
    auto *read = SDL_RenderReadPixels(sdlRenderer(), &rect);
    REQUIRE(read);
    currentOracle = SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(read);
    REQUIRE(currentOracle);
  }
}

void ProbeApplication::uiRenderEnd()
{
  namespace vi = vsr::integration;
  ++heartbeats;
  sdlRendererName = SDL_GetRendererName(sdlRenderer());
  if (observe && !frames && viewport->effectiveView().unavailableReason.empty()
      && (!secondary || secondary->effectiveView().unavailableReason.empty())) {
    const auto setupStart = std::chrono::steady_clock::now();
    if (metadataPressure && !metadataLoaded) {
      metadataLoaded = true;
      const std::string text(2048, 'x');
      for (int i = 0; i < 32; ++i)
        viewport->boundRenderer()->setParameter(
            ("qualification-text-" + std::to_string(i)).c_str(), text.c_str());
      const auto binding = viewport->effectiveView();
      CHECK(binding.rendererSettingsReason == "representation_limit");
      std::size_t represented = 0;
      for (const auto &setting : binding.rendererSettings)
        represented += setting.name.size() + setting.text.size();
      CHECK(represented < 16384);
    }
    vi::SceneRegistration sr;
    sr.scene = &appContext()->vsr.scene;
    sr.grants.read = true;
    const auto sid = *integration.registerScene(sr);
    sceneId = sid;
    identity = std::make_unique<vi::identity::SceneIdentity>(ids, *sr.scene);
    tracker =
        std::make_unique<vi::identity::ViewRevisionTracker>(ids, *identity);
    vi::ViewRegistration vr;
    vr.scene = sid;
    vr.role = vi::ViewRole::INTERACTIVE_VIEWPORT;
    vr.grants.read = true;
    vr.observeBinding = [this] {
      vi::EffectiveViewBinding value;
      static_cast<vsr::rendering::EffectiveView &>(value) =
          viewport->effectiveView();
      if (auto camera = viewport->boundCamera())
        value.cameraHandle = identity->handleOf(camera.data());
      if (auto renderer = viewport->boundRenderer())
        value.rendererHandle = identity->handleOf(renderer.data());
      return std::optional<vi::EffectiveViewBinding>(std::move(value));
    };
    vr.name = "Presentation Probe";
    viewId = *integration.registerView(vr);
    if (secondary) {
      secondaryTracker =
          std::make_unique<vi::identity::ViewRevisionTracker>(ids, *identity);
      vr.name = "Second Probe";
      vr.observeBinding = [this] {
        vi::EffectiveViewBinding value;
        static_cast<vsr::rendering::EffectiveView &>(value) =
            secondary->effectiveView();
        if (auto camera = secondary->boundCamera())
          value.cameraHandle = identity->handleOf(camera.data());
        if (auto renderer = secondary->boundRenderer())
          value.rendererHandle = identity->handleOf(renderer.data());
        return std::optional<vi::EffectiveViewBinding>(std::move(value));
      };
      secondaryId = *integration.registerView(vr);
    }
    REQUIRE(integration.activate().ok());
    authority = vi::session::SessionAuthority::create(integration);
    engine = vi::operations::OperationEngine::create(integration, *authority);
    if (!native) {
      const auto attached = authority->attach(authority->launchCredential());
      REQUIRE(attached.code == vi::session::Code::OK);
      credentials = {authority->launchCredential(),
          attached.sessionId,
          attached.sessionSecret};
    }
    frames = std::make_unique<vi::frame::FrameService>(
        integration, *authority, *engine);
    REQUIRE(frames->attachView(viewId,
        *identity,
        *tracker,
        vi::frame::SourceQualification::VIEWER_CPU_READY));
    REQUIRE(frames->setExposed(viewId, true));
    source = std::make_unique<vi::frame::ViewerFrameSource<ProbeViewport>>(
        *frames, viewId, *identity, *viewport);
    REQUIRE(source->connected());
    if (secondary) {
      REQUIRE(frames->attachView(secondaryId,
          *identity,
          *secondaryTracker,
          vi::frame::SourceQualification::VIEWER_CPU_READY));
      REQUIRE(frames->setExposed(secondaryId, true));
      secondarySource =
          std::make_unique<vi::frame::ViewerFrameSource<ProbeViewport>>(
              *frames, secondaryId, *identity, *secondary);
      REQUIRE(secondarySource->connected());
    }
#ifdef __linux__
    if (native || entropy) {
      inspector = std::make_unique<vi::inspection::Inspector>(
          integration, *authority, [this](vi::SceneId id) {
            return id == sceneId ? identity.get() : nullptr;
          });
      inspector->setViewRevisions([this](vi::ViewId id) {
        return id == viewId     ? tracker.get()
            : id == secondaryId ? secondaryTracker.get()
                                : nullptr;
      });
      vi::mcp::Config config;
      config.responseDeadlineMs = 10000;
      vi::mcp::tools::addQueryTools(config.tools, {*engine, *inspector});
      vi::mcp::tools::addLookupTools(config.tools, *engine);
      vi::mcp::tools::addFrameTools(config.tools, {*frames, *engine});
      REQUIRE(vi::descriptor::openOsRuntimeDirectory(runtime).ok());
      REQUIRE(vi::mcp::McpServer::start(
          config, *authority, *engine, runtime, server)
              .ok());
      std::cout << "VIEWER_NATIVE_DESCRIPTOR " << server->published().path()
                << '\n';
    }
#endif
    setupMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - setupStart)
                  .count();
  }
  if (frames && !native) {
    // Independent SDL pixels from the checked submission, never VSR snapshot
    // or encoder bytes. Cold preparation observes AFTER admission, so keeping
    // only the admission framebuffer compares different legitimate frames.
    // A finite diagnostic ring (16 surfaces/view) associates exact identity.
    auto remember = [&](auto &connection, auto id, auto *surface) {
      if (!connection || !surface || !connection->metrics().submissions)
        return;
      const auto sequence = connection->metrics().submissions;
      if (std::any_of(oracleHistory.begin(), oracleHistory.end(), [&](auto &o) {
            return o.view == id && o.presentation == sequence;
          }))
        return;
      auto *copy = SDL_DuplicateSurface(surface);
      REQUIRE(copy);
      oracleHistory.push_back({id,
          sequence,
          std::shared_ptr<SDL_Surface>(copy, SDL_DestroySurface)});
      if (std::count_if(oracleHistory.begin(),
              oracleHistory.end(),
              [&](auto &o) { return o.view == id; })
          > 16)
        oracleHistory.erase(std::find_if(oracleHistory.begin(),
            oracleHistory.end(),
            [&](auto &o) { return o.view == id; }));
    };
    remember(source, viewId, currentOracle);
    remember(secondarySource, secondaryId, secondaryOracle);
  }
  if (frames && !lifetimeChecked && heartbeats > 20) {
    lifetimeChecked = true;
    // Genuine Viewport destruction while its adapter/registration still live.
    // This is not a mock lifetime token or an alternate private cache helper.
    auto ephemeral = std::make_unique<ProbeViewport>(this, "Lifetime Probe");
    const auto life = ephemeral->lifetime();
    auto *raw = ephemeral.get();
    vi::ViewRegistration registration;
    registration.scene = sceneId;
    registration.role = vi::ViewRole::INTERACTIVE_VIEWPORT;
    registration.grants.read = true;
    registration.observeBinding = [raw, life] {
      vi::EffectiveViewBinding value;
      if (life.expired())
        value.unavailableReason = "closed_viewport";
      else
        static_cast<vsr::rendering::EffectiveView &>(value) =
            raw->effectiveView();
      return std::optional<vi::EffectiveViewBinding>(std::move(value));
    };
    vi::ViewId temporary{};
    auto revision =
        std::make_unique<vi::identity::ViewRevisionTracker>(ids, *identity);
    REQUIRE(integration.submit(sceneId, [&](auto &) {
      temporary = *integration.registerView(registration);
      REQUIRE(frames->attachView(temporary,
          *identity,
          *revision,
          vi::frame::SourceQualification::VIEWER_CPU_READY));
    }));
    integration.service();
    auto adapter =
        std::make_unique<vi::frame::ViewerFrameSource<ProbeViewport>>(
            *frames, temporary, *identity, *ephemeral);
    REQUIRE(adapter->connected());
    auto replacement =
        std::make_unique<vi::frame::ViewerFrameSource<ProbeViewport>>(
            *frames, temporary, *identity, *ephemeral);
    REQUIRE(replacement->connected());
    CHECK_FALSE(adapter->connected());
    CHECK(adapter->viewport() == nullptr);
    adapter.reset(); // old owner cannot disconnect the replacement
    CHECK(replacement->connected());
    ephemeral.reset();
    CHECK(life.expired());
    CHECK_FALSE(replacement->connected());
    CHECK(replacement->viewport() == nullptr);
    REQUIRE(integration.submit(sceneId, [&](auto &) {
      const auto observed = integration.observeViewBinding(temporary);
      REQUIRE(observed);
      CHECK(observed->unavailableReason == "closed_viewport");
      REQUIRE(integration.unregisterView(temporary));
      frames->detachViewer(temporary);
    }));
    integration.service();
    replacement.reset(); // cannot touch the already-destroyed window
  }
  if (frames)
    peakOwned = std::max(peakOwned, frames->ownedBytes());
  if (frames && !native) {
    if (requestPending) {
      const auto status = engine->status(credentials, operation);
      if (status.terminal) {
        maxDeliveryMs = std::max(maxDeliveryMs,
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - requestedAt)
                .count());
        REQUIRE(status.terminal == vi::operations::Outcome::OK);
        if (auto image = frames->result(credentials, operation)) {
          if (operation.requestId == firstQualifiedRequest)
            firstQualifiedAvailable = true;
          int width = 0, height = 0, channels = 0;
          auto *decoded = stbi_load_from_memory(image->png.data(),
              int(image->png.size()),
              &width,
              &height,
              &channels,
              4);
          REQUIRE(decoded);
          REQUIRE(width == int(image->width));
          REQUIRE(height == int(image->height));
          maxPngBytes = std::max(maxPngBytes, image->png.size());
#ifdef __linux__
          if (entropy && image->png.size() > (2u << 20) && !drainsStarted) {
            drainsStarted = true;
            drainBaseline = frames->ownedBytes();
            drainHeartbeat = heartbeats;
            slowSockets[0] = openSlowImage(operation);
            slowSockets[1] = openSlowImage(operation);
          }
#endif
          maxNativeWidth = std::max(maxNativeWidth, image->width);
          maxNativeHeight = std::max(maxNativeHeight, image->height);
          if (pendingView == viewId)
            ++primaryImages;
          else
            ++secondaryImages;
          if (pressure) {
            retained.push_back(image);
            retainedPeak = std::max(retainedPeak, frames->ownedBytes());
          }
          vi::mcp::json::Value metadata;
          REQUIRE(vi::mcp::json::parse(status.result, metadata));
          REQUIRE(metadata.get("production_id"));
          REQUIRE(metadata.get("presentation_id"));
          REQUIRE(metadata.get("observed_presentation_id"));
          REQUIRE(metadata.get("presentation_id")->n
              == metadata.get("observed_presentation_id")->n);
          const auto matched = std::find_if(
              oracleHistory.begin(), oracleHistory.end(), [&](auto &o) {
                return o.view == pendingView
                    && o.presentation
                    == std::uint64_t(metadata.get("presentation_id")->n);
              });
          REQUIRE(matched != oracleHistory.end());
          auto *oracle = matched->pixels.get();
          if (metadata.get("stale")->b) {
            ++staleImages;
            CHECK((metadata.get("rendered_scene_revision")->s
                    != metadata.get("current_scene_revision")->s
                || metadata.get("rendered_view_revision")->s
                    != metadata.get("current_view_revision")->s));
          }
          if (std::uint32_t(width) == firstWidth)
            ++originalSizeImages;
          else
            ++resizedImages;
          bool hasOutline = false;
          for (std::size_t p = 0; p < std::size_t(width) * height * 4; p += 4)
            hasOutline = hasOutline
                || (decoded[p] >= 200 && decoded[p + 1] >= 95
                    && decoded[p + 1] <= 150 && decoded[p + 2] <= 70);
          if (hasOutline)
            ++outlineImages;
          if (firstImage.empty() && oracle) {
            firstImage.assign(
                decoded, decoded + std::size_t(width) * height * 4);
            SDL_SaveBMP(
                oracle, ".scratch/viewport-integration/logs/04-oracle.bmp");
            auto *native = SDL_CreateSurfaceFrom(
                width, height, SDL_PIXELFORMAT_RGBA32, decoded, width * 4);
            SDL_SaveBMP(
                native, ".scratch/viewport-integration/logs/04-native.bmp");
            SDL_DestroySurface(native);
            std::cout << "ORACLE first_native=" << width << "x" << height
                      << " oracle=" << oracle->w << "x" << oracle->h << '\n';
          }
          if (oracle) {
            // This is the actual isolated SDL framebuffer, not a pipeline
            // pointer/encoder receipt. Exclude peripheral HUD/gizmo regions.
            // Compare nearest display samples to the native returned PNG;
            // filtering accounts for small nonzero errors under scaling.
            for (int y = height / 5; y < 4 * height / 5; y += 3)
              for (int x = width / 5; x < 4 * width / 5; x += 3) {
                const int dx =
                    std::min(oracle->w - 1, int((x + .5) * oracle->w / width));
                const int dy =
                    std::min(oracle->h - 1, int((y + .5) * oracle->h / height));
                const auto *actual =
                    static_cast<const std::uint8_t *>(oracle->pixels)
                    + dy * oracle->pitch + dx * 4;
                const auto *png = decoded + (std::size_t(y) * width + x) * 4;
                const auto *flipped =
                    decoded + (std::size_t(height - 1 - y) * width + x) * 4;
                for (int c = 0; c < 3; ++c) {
                  parityError += std::abs(int(actual[c]) - int(png[c]));
                  flippedError += std::abs(int(actual[c]) - int(flipped[c]));
                }
                ++comparedPixels;
              }
            ++parityImages;
          }
          stbi_image_free(decoded);
          if (!historical.requestId)
            historical = operation;
          ++images;
        } else {
          if (operation.requestId == 1)
            std::cout << "VIEWER_FIRST_OPERATION " << status.result << '\n';
          ++cold;
        }
        requestPending = false;
      }
    }
    if (!requestPending && requests < 48) {
      operation = {credentials.sessionId, ++requests};
      requestedAt = std::chrono::steady_clock::now();
      pendingView = secondary && requests % 2 == 0 ? secondaryId : viewId;
      // This fixture deliberately registers during initial UI setup. Preserve
      // unavailable/incomplete-binding requests; the first qualified request
      // (not an initial missing display binding) must complete with an image.
      REQUIRE(integration.submit(sceneId, [&](auto &) {
        const auto binding = integration.observeViewBinding(pendingView);
        if (!firstQualifiedRequest && binding
            && !vi::inspection::viewinfo::validate(*binding))
          firstQualifiedRequest = operation.requestId;
      }));
      integration.service();
      REQUIRE(frames->submit(credentials, operation, pendingView).code
          == vi::operations::Outcome::OK);
      SDL_DestroySurface(oracle);
      if (secondary && pendingView == secondaryId) {
        oracle = secondaryOracle;
        secondaryOracle = nullptr;
      } else {
        oracle = currentOracle;
        currentOracle = nullptr;
      }
      const auto start = std::chrono::steady_clock::now();
      integration.service();
      maxPinMs = std::max(maxPinMs,
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - start)
              .count());
      requestPending = true;
    }
  }
  if (native && source) {
    // Independent pre-Present SDL framebuffer, named by the adapter's actual
    // successful submission count, not by encoder output or a mutable buffer.
    auto saveOracle =
        [this](auto *window, auto &connection, auto *surface, auto id) {
          if (!window || !connection || !surface || !window->displayedTexture())
            return;
          const auto sequence = connection->metrics().submissions;
          if (!sequence)
            return;
          const auto path = std::string(std::getenv("VSR_VIEWER_TEST_ORACLES"))
              + "/view-" + std::to_string(id) + "-present-"
              + std::to_string(sequence) + ".bmp";
          REQUIRE(SDL_SaveBMP(surface, path.c_str()));
          if (sequence > 64) {
            const auto old = std::string(std::getenv("VSR_VIEWER_TEST_ORACLES"))
                + "/view-" + std::to_string(id) + "-present-"
                + std::to_string(sequence - 64) + ".bmp";
            std::filesystem::remove(old);
          }
          ++oracleFiles;
        };
    saveOracle(viewport, source, currentOracle, viewId);
    saveOracle(secondary, secondarySource, secondaryOracle, secondaryId);
  }
#ifdef __linux__
  if (drainsStarted && !withdrawalStarted) {
    drainHeld = std::max(drainHeld, frames->ownedBytes());
    for (std::size_t i = 0; i < slowSockets.size(); ++i) {
      char bytes[512]{};
      const auto count =
          ::recv(slowSockets[i], bytes, sizeof(bytes), MSG_PEEK | MSG_DONTWAIT);
      if (count > 0) {
        const std::string prefix(bytes, std::size_t(count));
        imageResponses[i] = prefix.find("HTTP/1.1 200") == 0
            && prefix.find("\"result\":") != std::string::npos;
      }
    }
    if (heartbeats > drainHeartbeat + 10 && imageResponses[0]
        && imageResponses[1]
        && frames->ownedBytes() > drainBaseline + (20u << 20))
      drainsHeld = true;
  }
#endif
  if (secondary && secondary->displayedTexture()) {
    ++secondarySubmissions;
    const auto generation = secondary->effectiveView().sourceInstanceGeneration;
    if (!secondaryGeneration)
      secondaryGeneration = generation;
    CHECK(generation == secondaryGeneration);
    if (refreshRequested)
      ++secondaryAfterRetirement;
  }
  if (refreshRequested && viewport->displayedTexture())
    replacementGeneration = viewport->effectiveView().sourceInstanceGeneration;
  if (auto receipt = viewport->displayedTexture()) {
    ++checkedSubmissions;
    matchedDevice = matchedDevice && viewport->libraryName() == device;
    if (auto acquired = viewport->presentedFrameState()) {
      const auto frame =
          std::static_pointer_cast<const vi::frame::ViewerAcquisition>(
              acquired);
      matchedState = matchedState
          || (frame->production->view == viewId
              && frame->production->binding.deviceIdentifier == device
              && frame->snapshot->production
                  == frame->production->nextProduction);
    }
    if (!firstWidth)
      firstWidth = receipt->width;
    else if (receipt->width != firstWidth)
      sawResize = true;
    if (auto upload = viewport->textureUpload()) {
      maxUploadMs = std::max(maxUploadMs, upload->durationMs);
      sumUploadMs += upload->durationMs;
      transferPath = upload->path;
    }
    const auto progress =
        native && source ? source->metrics().acquisitions : checkedSubmissions;
    if (progress >= 8 && !resizeRequested) {
      viewport->resized = true;
      resizeRequested = true;
    }
    if (progress >= 16 && !refreshRequested && (!secondary || destructive)) {
      refreshRequested = true;
      // Change private presentation settings through the real Window UI-state
      // interface before genuinely reconstructing the Image Pipeline.
      vsr::core::DataTree settings;
      auto *window = static_cast<vsr::ui::imgui::Window *>(viewport);
      window->saveSettings(settings.root());
      settings.root()["toneMapGamma"] = 1.7f;
      window->loadSettings(settings.root());
      // Same Device Identifier, genuinely retired/recreated device pipeline.
      // This fixture owns its Scene/camera/configuration, never the user's.
      retiredGeneration = viewport->effectiveView().sourceInstanceGeneration;
      secondaryAtRetirement = secondarySubmissions;
      viewport->setLibrary(device, VSR_INVALID_INDEX, false);
      sawRetirement = !viewport->displayedTexture();
    }
  }
  const auto progress =
      native && source ? source->metrics().acquisitions : checkedSubmissions;
  if (progress >= 24 && !changedRenderer) {
    changedRenderer = true;
    // Private fixture only. The next natural render applies this edit; no
    // observation call itself edits a Scene or restarts accumulation.
    viewport->boundRenderer()->setParameter(
        "background", vsr::math::float4(0.3f, 0.1f, 0.1f, 1.f));
  }
  if (progress >= 32 && !movedCamera) {
    movedCamera = true;
    manipulator.rotate({55.f, 20.f});
  }
  if (progress >= 40 && !selectedOutline) {
    selectedOutline = true;
    auto surface = appContext()->vsr.scene.getObject<vsr::scene::Surface>(0);
    REQUIRE(surface);
    appContext()->setSelected(surface.data());
  }
  if (native && authority && !nativeWithdrawn
      && std::filesystem::exists(std::getenv("VSR_VIEWER_TEST_WITHDRAW"))) {
    nativeWithdrawn = true;
    authority->restrict(
        vi::session::Target::scene(sceneId), vi::session::Capability::READ);
  }
  if (checkedSubmissions >= 100 && requests >= 48 && !requestPending
      && !withdrawalStarted) {
    withdrawalStarted = true;
    withdrawalHeartbeat = heartbeats;
    beforeWithdrawalBytes = frames->ownedBytes();
    retained.clear();
#ifdef __linux__
    if (entropy) {
      for (auto &socket : slowSockets) {
        if (socket >= 0)
          ::close(socket);
        socket = -1;
      }
    }
#endif
    cancelled = {credentials.sessionId, 49};
    REQUIRE(frames->submit(credentials, cancelled, viewId).code
        == vi::operations::Outcome::OK);
    integration
        .service(); // request-point pin; encoder owns its immutable input
    engine->cancel(credentials, cancelled);
    authority->restrict(
        vi::session::Target::scene(sceneId), vi::session::Capability::READ);
  }
  if (withdrawalStarted) {
    const auto status = engine->status(credentials, cancelled);
    cancelledDrained = status.terminal == vi::operations::Outcome::CANCELLED
        && status.heldSlots == 0;
    const auto old = engine->status(credentials, historical);
    withdrawalProtected = !old.resultVisible && old.result.empty()
        && !frames->result(credentials, historical)
        && !frames->result(credentials, cancelled);
    afterWithdrawalBytes = frames->ownedBytes();
#ifdef __linux__
    if (drainsStarted && afterWithdrawalBytes < drainBaseline)
      drainsReleased = true;
#endif
  }
  if ((withdrawalStarted && heartbeats > withdrawalHeartbeat + 10
          && cancelledDrained)
      || (!observe && checkedSubmissions >= 112)
      || (native
          && std::filesystem::exists(std::getenv("VSR_VIEWER_TEST_DONE")))
      || heartbeats >= (native ? 3000u : 600u)) {
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
  }
}

} // namespace

// Explicitly hidden and not tagged integration: full CTest must not open a
// user's display or use their preferences. The private authenticated Xvfb
// runner supplies both opt-in and isolated XDG directories.
TEST_CASE(
    "Isolated real viewer checks actual SDL submission and source retirement",
    "[.viewer-live]")
{
  const auto *isolated = std::getenv("VSR_VIEWER_TEST_ISOLATED");
  const auto *device = std::getenv("VSR_VIEWER_TEST_DEVICE");
  REQUIRE(isolated);
  REQUIRE(std::string(isolated) == "1");
  REQUIRE(device);
  REQUIRE(std::getenv("XDG_CONFIG_HOME"));
  REQUIRE(std::getenv("XDG_RUNTIME_DIR"));
  const bool pressure = std::getenv("VSR_VIEWER_TEST_PRESSURE");
  ProbeApplication app(device, true, pressure);
  app.run(pressure ? 4200 : 640,
      pressure ? 1200 : 480,
      "Private viewport presentation test");
  if (pressure) {
    CHECK(app.primaryImages > 0);
    if (!std::getenv("VSR_VIEWER_TEST_SINGLE")) {
      CHECK(app.secondaryImages > 0);
      CHECK(app.secondarySubmissions >= 30);
      CHECK(app.secondaryMetrics.acquisitions > 0);
      CHECK(app.secondaryMetrics.maxStartMs < 100.);
      CHECK(app.secondaryMetrics.maxAcquireMs < 100.);
      CHECK(app.secondaryMetrics.maxSubmitMs < 100.);
    }
    CHECK(app.maxNativeWidth == (app.secondarySubmissions ? 2048u : 1920u));
    CHECK(app.maxNativeHeight == 1080);
    CHECK(app.retainedPeak > (64u << 20));
#ifdef __linux__
    if (app.entropy) {
      CHECK(app.maxPngBytes > (2u << 20));
      CHECK(app.maxPngBytes <= (8u << 20));
      CHECK(app.drainsStarted);
      CHECK(app.drainsHeld);
      CHECK(app.imageResponses[0]);
      CHECK(app.imageResponses[1]);
      CHECK(app.drainsReleased);
      CHECK(app.drainHeld <= (256u << 20));
      CHECK(app.heartbeats > app.drainHeartbeat + 20);
    }
#endif
  }
  CHECK(app.checkedSubmissions >= 30);
  CHECK(app.matchedDevice);
  CHECK(app.images > 0);
  CHECK(app.firstQualifiedRequest > 0);
  CHECK(app.firstQualifiedAvailable);
  CHECK(app.requests == 48);
  CHECK(app.maxPinMs < 100.);
  CHECK(app.setupMs < 100.);
  CHECK(app.releaseMs < 100.);
  CHECK(app.sourceMetrics.maxStartMs < 100.);
  CHECK(app.sourceMetrics.maxAcquireMs < 100.);
  CHECK(app.sourceMetrics.maxSubmitMs < 100.);
  CHECK(app.peakOwned <= (256u << 20));
  CHECK(app.staleImages > 0);
  CHECK(app.originalSizeImages > 0);
  CHECK(app.resizedImages > 0);
  if (!app.entropy)
    CHECK(app.outlineImages > 0);
  CHECK(app.cancelledDrained);
  CHECK(app.withdrawalProtected);
  CHECK(app.afterWithdrawalBytes < app.beforeWithdrawalBytes);
  CHECK(app.heartbeats > app.withdrawalHeartbeat + 10);
  CHECK(app.changedRenderer);
  CHECK(app.movedCamera);
  CHECK(app.parityImages > 0);
  CHECK(app.comparedPixels > 10000);
  CHECK(app.parityError / (3 * app.comparedPixels) < 8.);
  CHECK(app.flippedError > app.parityError * 1.5);
  CHECK(app.matchedState);
  CHECK(app.sawResize);
  CHECK(app.primaryOffOwnerStarts == 0);
  CHECK(app.primaryOwnerStarts > 0);
  CHECK(app.primaryMismatchedAcquisitions == 0);
  if (!app.pressure || app.destructive || !app.secondarySubmissions) {
    CHECK(app.sawRetirement);
    CHECK(app.replacementGeneration > app.retiredGeneration);
  }
  if (app.destructive && app.secondary) {
    CHECK(app.secondaryAtRetirement > 0);
    CHECK(app.secondaryAfterRetirement >= 30);
    CHECK(app.secondaryOffOwnerStarts == 0);
    CHECK(app.secondaryOwnerStarts > 0);
    CHECK(app.secondaryMismatchedAcquisitions == 0);
  }
  CHECK_FALSE(app.transferPath.empty());
  std::cout << "VIEWER_METRICS device=" << device
            << " transfer=" << app.transferPath
            << " heartbeats=" << app.heartbeats
            << " submissions=" << app.checkedSubmissions
            << " max_upload_ms=" << app.maxUploadMs
            << " mean_upload_ms=" << app.sumUploadMs / app.checkedSubmissions
            << " matched_state=" << app.matchedState << " images=" << app.images
            << " cold=" << app.cold << " requests=" << app.requests
            << " max_pin_ms=" << app.maxPinMs << " setup_ms=" << app.setupMs
            << " release_ms=" << app.releaseMs
            << " max_delivery_ms=" << app.maxDeliveryMs
            << " source_starts=" << app.sourceMetrics.starts
            << " source_acquisitions=" << app.sourceMetrics.acquisitions
            << " source_submissions=" << app.sourceMetrics.submissions
            << " max_start_ms=" << app.sourceMetrics.maxStartMs
            << " max_acquire_ms=" << app.sourceMetrics.maxAcquireMs
            << " max_submit_ms=" << app.sourceMetrics.maxSubmitMs
            << " peak_owned=" << app.peakOwned
            << " cancelled_drained=" << app.cancelledDrained
            << " withdrawal_protected=" << app.withdrawalProtected
            << " before_withdrawal_bytes=" << app.beforeWithdrawalBytes
            << " after_withdrawal_bytes=" << app.afterWithdrawalBytes
            << " stale_images=" << app.staleImages
            << " outline_images=" << app.outlineImages
            << " original_images=" << app.originalSizeImages
            << " resized_images=" << app.resizedImages
            << " parity_images=" << app.parityImages
            << " compared_pixels=" << app.comparedPixels
            << " mean_rgb_error=" << app.parityError / (3 * app.comparedPixels)
            << " flipped_rgb_error="
            << app.flippedError / (3 * app.comparedPixels)
            << " sdl_renderer=" << app.sdlRendererName
            << " resize=" << app.sawResize
            << " retirement=" << app.sawRetirement
            << " retired_generation=" << app.retiredGeneration
            << " replacement_generation=" << app.replacementGeneration
            << " secondary_generation=" << app.secondaryGeneration
            << " secondary_after_retirement=" << app.secondaryAfterRetirement
            << " off_owner_starts=" << app.primaryOffOwnerStarts
            << " mismatched_acquisitions=" << app.primaryMismatchedAcquisitions
            << " primary_images=" << app.primaryImages
            << " secondary_images=" << app.secondaryImages
            << " secondary_submissions=" << app.secondarySubmissions
            << " max_native=" << app.maxNativeWidth << "x"
            << app.maxNativeHeight << " retained_peak=" << app.retainedPeak
            << " secondary_max_start_ms=" << app.secondaryMetrics.maxStartMs
            << " secondary_max_acquire_ms=" << app.secondaryMetrics.maxAcquireMs
            << " secondary_max_submit_ms=" << app.secondaryMetrics.maxSubmitMs
            << " max_png_bytes=" << app.maxPngBytes
#ifdef __linux__
            << " drain_baseline=" << app.drainBaseline
            << " drain_held=" << app.drainHeld
            << " drains_started=" << app.drainsStarted
            << " drains_held=" << app.drainsHeld
            << " drains_released=" << app.drainsReleased
#endif
            << '\n';
}

#ifdef __linux__
TEST_CASE("Native Pi client retrieves the real qualified test viewport",
    "[.viewer-native]")
{
  REQUIRE(std::getenv("VSR_VIEWER_TEST_ISOLATED"));
  REQUIRE(std::string(std::getenv("VSR_VIEWER_TEST_ISOLATED")) == "1");
  REQUIRE(std::getenv("VSR_VIEWER_TEST_DEVICE"));
  REQUIRE(std::getenv("VSR_VIEWER_TEST_ORACLES"));
  REQUIRE(std::getenv("VSR_VIEWER_TEST_DONE"));
  REQUIRE(std::getenv("VSR_VIEWER_TEST_WITHDRAW"));
  const bool pressure = std::getenv("VSR_VIEWER_TEST_PRESSURE");
  ProbeApplication app(std::getenv("VSR_VIEWER_TEST_DEVICE"), true, pressure);
  app.run(pressure ? 4200 : 640,
      pressure ? 1200 : 480,
      "Private native viewport qualification test");
  CHECK(std::filesystem::exists(std::getenv("VSR_VIEWER_TEST_DONE")));
  CHECK(app.oracleFiles > 0);
  CHECK(app.nativeWithdrawn);
  CHECK(app.changedRenderer);
  CHECK(app.movedCamera);
  CHECK(app.selectedOutline);
  CHECK(app.sawResize);
  if (!pressure)
    CHECK(app.sawRetirement);
  if (pressure) {
    CHECK(app.secondaryMetrics.acquisitions > 0);
    CHECK(app.secondaryMetrics.maxAcquireMs < 100.);
    CHECK(app.secondaryMetrics.maxSubmitMs < 100.);
  }
  CHECK(app.sourceMetrics.acquisitions > 0);
  CHECK(app.checkedSubmissions > 30);
  CHECK(app.peakOwned <= (256u << 20));
  CHECK(app.sourceMetrics.maxStartMs < 100.);
  CHECK(app.sourceMetrics.maxAcquireMs < 100.);
  CHECK(app.sourceMetrics.maxSubmitMs < 100.);
  CHECK(app.releaseMs < 100.);
  std::cout << "VIEWER_NATIVE_METRICS device=" << app.device
            << " source_acquisitions=" << app.sourceMetrics.acquisitions
            << " submissions=" << app.checkedSubmissions
            << " heartbeats=" << app.heartbeats
            << " oracles=" << app.oracleFiles
            << " max_start_ms=" << app.sourceMetrics.maxStartMs
            << " max_acquire_ms=" << app.sourceMetrics.maxAcquireMs
            << " max_submit_ms=" << app.sourceMetrics.maxSubmitMs
            << " release_ms=" << app.releaseMs
            << " peak_owned=" << app.peakOwned
            << " secondary_max_acquire_ms=" << app.secondaryMetrics.maxAcquireMs
            << " secondary_max_submit_ms=" << app.secondaryMetrics.maxSubmitMs
            << " read_withdrawn=" << app.nativeWithdrawn << '\n';
}
#endif

TEST_CASE(
    "Isolated viewer baseline keeps the same GPU and SDL path without observation",
    "[.viewer-baseline]")
{
  REQUIRE(std::getenv("VSR_VIEWER_TEST_ISOLATED"));
  REQUIRE(std::string(std::getenv("VSR_VIEWER_TEST_ISOLATED")) == "1");
  REQUIRE(std::getenv("VSR_VIEWER_TEST_DEVICE"));
  REQUIRE(std::getenv("XDG_CONFIG_HOME"));
  REQUIRE(std::getenv("XDG_RUNTIME_DIR"));
  const bool pressure = std::getenv("VSR_VIEWER_TEST_PRESSURE");
  ProbeApplication app(std::getenv("VSR_VIEWER_TEST_DEVICE"), false, pressure);
  app.run(pressure ? 4200 : 640,
      pressure ? 1200 : 480,
      "Private viewport observation-disabled baseline");
  CHECK(app.checkedSubmissions >= 112);
  CHECK(app.matchedDevice);
  CHECK(app.sawResize);
  CHECK(app.primaryOffOwnerStarts == 0);
  CHECK(app.primaryOwnerStarts > 0);
  if (!pressure || app.destructive || !app.secondarySubmissions) {
    CHECK(app.sawRetirement);
    CHECK(app.replacementGeneration > app.retiredGeneration);
  }
  if (app.destructive && app.secondary) {
    CHECK(app.secondaryAtRetirement > 0);
    CHECK(app.secondaryAfterRetirement >= 30);
    CHECK(app.secondaryOffOwnerStarts == 0);
    CHECK(app.secondaryOwnerStarts > 0);
  }
  CHECK(app.changedRenderer);
  CHECK(app.movedCamera);
  CHECK(app.selectedOutline);
  CHECK_FALSE(app.frames);
  CHECK(app.requests == 0);
  CHECK(app.images == 0);
  CHECK(app.peakOwned == 0);
  std::cout << "VIEWER_BASELINE device=" << app.device
            << " transfer=" << app.transferPath
            << " submissions=" << app.checkedSubmissions
            << " heartbeats=" << app.heartbeats
            << " max_upload_ms=" << app.maxUploadMs
            << " sdl_renderer=" << app.sdlRendererName << '\n';
}
#endif

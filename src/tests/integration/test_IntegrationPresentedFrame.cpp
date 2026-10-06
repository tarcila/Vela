// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
// vsr_integration
#include "vsr/integration/frame/FrameObservation.hpp"
#include "vsr/integration/mcp/tools/FrameTools.hpp"
#include "vsr/integration/mcp/tools/LookupTools.hpp"
// POSIX loopback transport
#if defined(__linux__)
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
// vsr_rendering
#include "vsr/rendering/index/RenderIndexAllLayers.hpp"
#include "vsr/rendering/pipeline/ImagePipeline.h"
// anari
#include <anari/anari_cpp.hpp>
// SDL
#include <SDL3/SDL.h>
// Independent decoder, private to this TU.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../../../external/vela_stb_image/stb_image.h"
// std
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>

using namespace vsr::integration;

namespace {
// The natural host producer is a real synchronous Helide frame. The following
// procedural final pass is only the asymmetric image oracle, not a substitute
// operation engine, frame cache or presentation simulator.
struct HelidePass : vsr::rendering::ImageSource
{
  anari::Device device{};
  anari::Frame frame{};
  anari::Camera camera{};
  std::size_t renders{0};
  ~HelidePass() override
  {
    anari::release(device, frame);
  }

  vsr::rendering::ImageChannels supportedChannels() const override
  {
    return vsr::rendering::ImageChannels::OBJECT_ID;
  }

 private:
  void render(vsr::rendering::ImageBuffers &b) override
  {
    const auto size = dimensions();
    anari::setParameter(device, camera, "aspect", float(size.x) / size.y);
    anari::commitParameters(device, camera);
    anari::setParameter(device, frame, "size", ANARI_UINT32_VEC2, &size);
    anari::commitParameters(device, frame);
    anari::render(device, frame);
    ++renders;
    anari::wait(device, frame);
    REQUIRE(anari::isReady(device, frame));
    auto mapped = anari::map<std::uint32_t>(device, frame, "channel.color");
    REQUIRE(mapped.data);
    REQUIRE(mapped.width == size.x);
    REQUIRE(mapped.height == size.y);
    std::memcpy(b.color, mapped.data, std::size_t(size.x) * size.y * 4);
    anari::unmap(device, frame, "channel.color");
  }
};

struct PatternPass : vsr::rendering::ImagePass
{
  std::uint8_t offset{0};
  bool noise{false};

 private:
  void render(vsr::rendering::ImageBuffers &b,
      vsr::rendering::FrameState &) override
  {
    const auto size = dimensions();
    auto *rgba = reinterpret_cast<std::uint8_t *>(b.color);
    std::uint32_t state = 0x13579bdu + offset;
    for (std::uint32_t y = 0; y < size.y; ++y)
      for (std::uint32_t x = 0; x < size.x; ++x) {
        auto *p = rgba + (std::size_t(y) * size.x + x) * 4;
        p[0] = x == 0 ? 255 : offset;
        p[1] = y == 0 ? 63 : 192;
        p[2] = x == size.x - 1 ? 128 : 16;
        p[3] = x == 0 ? 127 : 255;
        if (noise) {
          state ^= state << 13;
          state ^= state >> 17;
          state ^= state << 5;
          std::memcpy(p, &state, 4);
        }
      }
    if (b.objectId) {
      std::fill(b.objectId, b.objectId + std::size_t(size.x) * size.y, ~0u);
      b.objectId[std::size_t(size.y / 2) * size.x + size.x / 2] = 7;
      if (size.x > 1)
        b.objectId[std::size_t(size.y / 2) * size.x + size.x / 2 - 1] = 7;
    }
  }
};

struct CpuHost
{
  anari::Library library{};
  anari::Device device{};
  vsr::scene::Scene scene;
  vsr::scene::CameraRef camera;
  vsr::scene::RendererRef rendererObject;
  vsr::scene::MaterialRef material;
  vsr::rendering::RenderIndexAllLayers *index{};
  HelidePass *helide{};
  vsr::rendering::ImagePipeline pipeline{
      4, 2, vsr::rendering::ImageStorage::HOST};
  Integration integration;
  identity::IdentityAuthority ids;
  std::unique_ptr<identity::SceneIdentity> identity;
  std::unique_ptr<identity::ViewRevisionTracker> tracker;
  std::unique_ptr<session::SessionAuthority> authority;
  std::unique_ptr<operations::OperationEngine> engine;
  std::unique_ptr<frame::FrameService> frames;
  operations::Credentials credentials;
  SceneId sceneId{};
  SceneId extraSceneId{INVALID_SCENE_ID};
  ViewId viewId{};
  std::uint64_t nextRequest{0};
  std::size_t peakOwned{0};
  double now{1.};
  PatternPass *pattern{};
  vsr::rendering::OutlineRenderPass *outline{};
  image::RowOrigin origin{image::RowOrigin::TOP_LEFT};
  std::vector<std::uint8_t> displayed;
  bool reuseAfterSubmission{false};
  CpuHost(std::size_t budgetBytes = 256u << 20,
      vsr::scene::Scene *extraScene = nullptr);
  ~CpuHost();
  operations::OperationRef request(bool wait = true);
  void await(const operations::OperationRef &ref);
  std::optional<EffectiveViewBinding> binding() const;
  void replaceDevice();
  bool submitDisplay(const std::uint32_t *pixels,
      std::uint32_t width,
      std::uint32_t height,
      bool accepted = true);
  void present(bool accepted = true);
};

CpuHost::CpuHost(std::size_t budgetBytes, vsr::scene::Scene *extraScene)
{
  library = anari::loadLibrary("helide", nullptr, nullptr);
  REQUIRE(library);
  device = anari::newDevice(library, "default");
  REQUIRE(device);
  anari::commitParameters(device, device);
  auto sphere = scene.createObject<vsr::scene::Geometry>("sphere");
  auto positions = scene.createArray(ANARI_FLOAT32_VEC3, 1);
  const vsr::math::float3 center(0.f);
  positions->setData(&center, 1);
  sphere->setParameterObject("vertex.position", *positions);
  sphere->setParameter("radius", 1.f);
  material = scene.createObject<vsr::scene::Material>("matte");
  scene.insertChildObjectNode(scene.defaultLayer()->root(),
      scene.createSurface("sphere", sphere, material));
  auto light = scene.createObject<vsr::scene::Light>("directional");
  scene.insertChildObjectNode(scene.defaultLayer()->root(), light);
  camera = scene.createObject<vsr::scene::Camera>("perspective");
  camera->setParameter("position", vsr::math::float3(0.f, 0.f, 5.f));
  camera->setParameter("direction", vsr::math::float3(0.f, 0.f, -1.f));
  camera->setParameter("up", vsr::math::float3(0.f, 1.f, 0.f));
  rendererObject = scene.createRenderer("helide", "default")->self();
  index = scene.updateDelegate().emplace<vsr::rendering::RenderIndexAllLayers>(
      scene, vsr::core::Token("helide"), device);
  index->populate();
  helide = pipeline.setSource<HelidePass>();
  helide->device = device;
  helide->frame = anari::newObject<anari::Frame>(device);
  helide->camera = index->camera(camera->index());
  anari::setParameter(device, helide->frame, "camera", helide->camera);
  anari::setParameter(device,
      helide->frame,
      "renderer",
      index->renderer(rendererObject->index()));
  anari::setParameter(device, helide->frame, "world", index->world());
  anari::setParameter(
      device, helide->frame, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  pattern = pipeline.addPass<PatternPass>();
  outline = pipeline.addPass<vsr::rendering::OutlineRenderPass>();
  outline->setOutlineId(7);
  outline->setEnabled(false);
  SceneRegistration sr;
  sr.scene = &scene;
  sr.grants.read = true;
  sceneId = *integration.registerScene(sr);
  if (extraScene) {
    sr.scene = extraScene;
    extraSceneId = *integration.registerScene(sr);
  }
  ViewRegistration vr;
  vr.scene = sceneId;
  vr.grants.read = true;
  vr.role = ViewRole::HEADLESS;
  vr.observeBinding = [this] { return binding(); };
  viewId = *integration.registerView(vr);
  REQUIRE(integration.activate().ok());
  identity = std::make_unique<identity::SceneIdentity>(ids, scene);
  tracker = std::make_unique<identity::ViewRevisionTracker>(ids, *identity);
  authority =
      session::SessionAuthority::create(integration, [this] { return now; });
  engine = operations::OperationEngine::create(
      integration, *authority, {}, [this] { return now; });
  const auto attached = authority->attach(authority->launchCredential());
  REQUIRE(attached.code == session::Code::OK);
  credentials = {authority->launchCredential(),
      attached.sessionId,
      attached.sessionSecret};
  frames = std::make_unique<frame::FrameService>(
      integration,
      *authority,
      *engine,
      [this] { return now; },
      std::make_shared<vsr::rendering::FrameBudget>(budgetBytes));
  REQUIRE(frames->attachView(viewId, *identity, *tracker));
}

std::optional<EffectiveViewBinding> CpuHost::binding() const
{
  EffectiveViewBinding b;
  auto dims = pipeline.dimensions();
  b.width = dims.x;
  b.height = dims.y;
  b.displayWidth = dims.x * 2.;
  b.displayHeight = dims.y * 2.;
  b.displayCoordinateUnits = "physical_pixels";
  b.displayFramebufferScaleX = 1.;
  b.displayFramebufferScaleY = 1.;
  const auto position = camera->parameterValueAs<vsr::math::float3>("position");
  b.camera.position = {position->x, position->y, position->z};
  b.camera.fovyDegrees =
      *camera->parameterValueAs<float>("fovy") * (180. / 3.141592653589793);
  b.cameraHandle = identity->handleOf(camera.data());
  b.rendererHandle = identity->handleOf(rendererObject.data());
  b.deviceIdentifier = "helide";
  b.deviceSubtype = "default";
  b.rendererSubtype = "default";
  b.rendererConfigurationRevision =
      std::to_string(rendererObject->lastParameterChange());
  b.rendererSettingsReason = "backend_defaults_not_observed";
  b.pipelineSource = "SynchronousHelide+Pattern+Outline";
  b.pipelineConfigurationRevision = std::to_string(pattern->offset) + ":"
      + std::to_string(outline->isEnabled()) + ":"
      + std::to_string(pattern->isEnabled()) + ":"
      + std::to_string(pattern->noise);
  b.pipelineSettings = {{"pattern_offset", {double(pattern->offset)}, {}},
      {"pattern_enabled", {double(pattern->isEnabled())}, {}},
      {"noise_enabled", {double(pattern->noise)}, {}},
      {"outline_enabled", {double(outline->isEnabled())}, {}}};
  b.pipelineSettingsReason = "procedural_oracle_after_real_anari_render";
  b.outputColor = "rgba8_srgb";
  return b;
}

CpuHost::~CpuHost()
{
  frames.reset();
  pipeline.clear();
  scene.updateDelegate().clear();
  anari::release(device, device);
  anari::unloadLibrary(library);
}

operations::OperationRef CpuHost::request(bool wait)
{
  operations::OperationRef ref{credentials.sessionId, ++nextRequest};
  REQUIRE(
      frames->submit(credentials, ref, viewId).code == operations::Outcome::OK);
  integration.service();
  peakOwned = std::max(peakOwned, frames->ownedBytes());
  if (wait)
    await(ref);
  return ref;
}

void CpuHost::await(const operations::OperationRef &ref)
{
  for (int i = 0; i < 30000 && !engine->status(credentials, ref).terminal;
      ++i) {
    REQUIRE(integration.submit(
        sceneId, [this](vsr::scene::Scene &) { frames->pump(); }));
    integration.service();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(engine->status(credentials, ref).terminal);
}

void CpuHost::present(bool accepted)
{
  REQUIRE(integration.submit(sceneId, [this, accepted](vsr::scene::Scene &) {
    auto production = frames->beginProduction(viewId, pipeline, origin);
    pipeline.render();
    frames->present(viewId,
        pipeline,
        std::move(production),
        [this, accepted](const std::uint32_t *pixels,
            std::uint32_t width,
            std::uint32_t height) {
          return submitDisplay(pixels, width, height, accepted);
        });
  }));
  integration.service();
}

bool CpuHost::submitDisplay(const std::uint32_t *pixels,
    std::uint32_t width,
    std::uint32_t height,
    bool accepted)
{
  auto *surface =
      SDL_CreateSurface(width * 2, height * 2, SDL_PIXELFORMAT_RGBA32);
  REQUIRE(surface);
  auto *renderer = SDL_CreateSoftwareRenderer(surface);
  REQUIRE(renderer);
  auto *texture = SDL_CreateTexture(renderer,
      SDL_PIXELFORMAT_RGBA32,
      SDL_TEXTUREACCESS_STREAMING,
      width,
      height);
  REQUIRE(texture);
  REQUIRE(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE));
  REQUIRE(SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST));
  std::vector<std::uint32_t> topLeft;
  if (origin == image::RowOrigin::BOTTOM_LEFT) {
    topLeft.resize(std::size_t(width) * height);
    for (std::uint32_t y = 0; y < height; ++y)
      std::memcpy(topLeft.data() + std::size_t(y) * width,
          pixels + std::size_t(height - y - 1) * width,
          width * 4);
    pixels = topLeft.data();
  }
  REQUIRE(SDL_UpdateTexture(texture, nullptr, pixels, width * 4));
  // Upload without successful submission is deliberately not presented.
  bool submitted = false;
  if (accepted) {
    REQUIRE(SDL_RenderTexture(renderer, texture, nullptr, nullptr));
    submitted = SDL_RenderPresent(renderer);
    REQUIRE(submitted);
    displayed.resize(std::size_t(width) * height * 4);
    for (std::uint32_t y = 0; y < height; ++y)
      for (std::uint32_t x = 0; x < width; ++x)
        std::memcpy(displayed.data() + (std::size_t(y) * width + x) * 4,
            static_cast<const std::uint8_t *>(surface->pixels)
                + (y * 2) * surface->pitch + (x * 2) * 4,
            4);
  }
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroySurface(surface);
  if (submitted && reuseAfterSubmission) {
    pattern->offset = 201;
    pipeline.render();
  }
  return submitted;
}

void CpuHost::replaceDevice()
{
  REQUIRE(frames->setExposed(viewId, false));
  auto next = anari::newDevice(library, "default");
  REQUIRE(next);
  REQUIRE(next != device); // old device is still alive at allocation
  anari::commitParameters(next, next);
  const auto size = pipeline.dimensions();
  pipeline.clear();
  scene.updateDelegate().clear();
  anari::release(device, device);
  device = next;
  index = scene.updateDelegate().emplace<vsr::rendering::RenderIndexAllLayers>(
      scene, vsr::core::Token("helide"), device);
  index->populate();
  pipeline.setDimensions(size.x, size.y);
  helide = pipeline.setSource<HelidePass>();
  helide->device = device;
  helide->frame = anari::newObject<anari::Frame>(device);
  helide->camera = index->camera(camera->index());
  anari::setParameter(device, helide->frame, "camera", helide->camera);
  anari::setParameter(device,
      helide->frame,
      "renderer",
      index->renderer(rendererObject->index()));
  anari::setParameter(device, helide->frame, "world", index->world());
  anari::setParameter(
      device, helide->frame, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  pattern = pipeline.addPass<PatternPass>();
  outline = pipeline.addPass<vsr::rendering::OutlineRenderPass>();
  outline->setOutlineId(7);
  outline->setEnabled(false);
  tracker->notePipelineChanged();
  REQUIRE(frames->setExposed(viewId, true));
}

#if defined(__linux__)
struct FrameSocket
{
  explicit FrameSocket(int descriptor);
  ~FrameSocket();
  VSR_NOT_COPYABLE(FrameSocket)
  VSR_NOT_MOVEABLE(FrameSocket)
  int fd{-1};
  std::string responsePrefix;
};

FrameSocket::FrameSocket(int descriptor) : fd(descriptor) {}

FrameSocket::~FrameSocket()
{
  if (fd >= 0)
    ::close(fd);
}

// A real loopback HTTP client; SO_RCVBUF bounds the kernel receive window to
// create genuine backpressure. It neither simulates cache nor service flags.
struct FrameTransport
{
  CpuHost *host{nullptr};
  std::string path;
  descriptor::RuntimeDirectory directory;
  std::unique_ptr<mcp::McpServer> server;
  explicit FrameTransport(CpuHost &h);
  ~FrameTransport();
  VSR_NOT_COPYABLE(FrameTransport)
  VSR_NOT_MOVEABLE(FrameTransport)
  int open(const operations::OperationRef &ref,
      const std::string &tool = "vsr_view_frame_image",
      const std::string &rpcId = "1");
  mcp::json::Value read(FrameSocket &socket, bool complete = true);
  void awaitImageHeaders(FrameSocket &socket);
  void awaitOwned(std::size_t minimum);
  void awaitReleased(std::size_t maximum);
};

FrameTransport::FrameTransport(CpuHost &h) : host(&h)
{
  const auto *tmp = std::getenv("TMPDIR");
  REQUIRE(tmp);
  path = std::string(tmp) + "/frame-http-XXXXXX";
  REQUIRE(::mkdtemp(path.data()));
  REQUIRE(descriptor::openDirectory(path, {}, directory).ok());
  mcp::Config config;
  config.maxConnections = 4;
  config.responseDeadlineMs = 10000;
  mcp::tools::addFrameTools(config.tools, {*host->frames, *host->engine});
  mcp::tools::addLookupTools(config.tools, *host->engine);
  REQUIRE(mcp::McpServer::start(
      config, *host->authority, *host->engine, directory, server)
          .ok());
}
FrameTransport::~FrameTransport()
{
  server.reset();
  std::filesystem::remove_all(path);
}
int FrameTransport::open(const operations::OperationRef &ref,
    const std::string &tool,
    const std::string &rpcId)
{
  FrameSocket socket{::socket(AF_INET, SOCK_STREAM, 0)};
  REQUIRE(socket.fd >= 0);
  int bytes = 65536;
  REQUIRE(::setsockopt(socket.fd, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes))
      == 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(server->port());
  REQUIRE(
      ::connect(
          socket.fd, reinterpret_cast<sockaddr *>(&address), sizeof(address))
      == 0);
  const auto body = "{\"jsonrpc\":\"2.0\",\"id\":" + rpcId
      + ",\"method\":\"tools/call\",\"params\":{\"name\":\"" + tool
      + "\",\"arguments\":{\"operation_ref\":{\"session_id\":\"" + ref.sessionId
      + "\",\"request_id\":" + std::to_string(ref.requestId) + "}}}}";
  const auto request = "POST /mcp HTTP/1.1\r\nHost: " + server->authority()
      + "\r\nAuthorization: Bearer " + host->credentials.launchCredential
      + "\r\nVSR-Session-ID: " + host->credentials.sessionId
      + "\r\nVSR-Session-Proof: Bearer " + host->credentials.sessionProof
      + "\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: "
      + std::to_string(body.size()) + "\r\n\r\n" + body;
  std::size_t sent = 0;
  while (sent < request.size()) {
    auto n = ::send(
        socket.fd, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
    REQUIRE(n > 0);
    sent += n;
  }
  const auto fd = socket.fd;
  socket.fd = -1;
  return fd;
}
mcp::json::Value FrameTransport::read(FrameSocket &socket, bool complete)
{
  std::string response = std::move(socket.responsePrefix);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(15);
  char bytes[65536];
  for (;;) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now())
                          .count();
    REQUIRE(left > 0);
    pollfd poll{socket.fd, POLLIN, 0};
    REQUIRE(::poll(&poll, 1, int(left)) > 0);
    const auto n = ::recv(socket.fd, bytes, sizeof(bytes), 0);
    if (n <= 0)
      break;
    response.append(bytes, std::size_t(n));
    REQUIRE(response.size() <= 12582912 + 65536);
  }
  ::close(socket.fd);
  socket.fd = -1;
  const auto split = response.find("\r\n\r\n");
  REQUIRE(split != std::string::npos);
  mcp::json::Value json;
  const auto parsed = mcp::json::parse(response.substr(split + 4), json);
  if (complete)
    REQUIRE(parsed);
  return json;
}
void FrameTransport::awaitImageHeaders(FrameSocket &socket)
{
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  auto &headers = socket.responsePrefix;
  while (headers.find("\r\n\r\n") == std::string::npos) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now())
                          .count();
    REQUIRE(left > 0);
    pollfd poll{socket.fd, POLLIN, 0};
    REQUIRE(::poll(&poll, 1, int(left)) > 0);
    char byte;
    REQUIRE(::recv(socket.fd, &byte, 1, MSG_DONTWAIT) == 1);
    headers += byte;
    REQUIRE(headers.size() <= 4096);
  }
  REQUIRE(headers.find("HTTP/1.1 200 OK\r\n") == 0);
  const std::string lengthHeader = "\r\nContent-Length: ";
  const auto length = headers.find(lengthHeader);
  REQUIRE(length != std::string::npos);
  const auto bodyBytes =
      std::stoull(headers.substr(length + lengthHeader.size()));
  REQUIRE(bodyBytes > (7u << 20));
  REQUIRE(bodyBytes <= 12582912);
}
void FrameTransport::awaitOwned(std::size_t minimum)
{
  for (int i = 0; i < 10000 && host->frames->ownedBytes() < minimum; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  REQUIRE(host->frames->ownedBytes() >= minimum);
}
void FrameTransport::awaitReleased(std::size_t maximum)
{
  for (int i = 0; i < 5000 && host->frames->ownedBytes() > maximum; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  REQUIRE(host->frames->ownedBytes() <= maximum);
}
#endif
} // namespace

TEST_CASE("Real CPU device replacement is cold even with the same Identifier",
    "[integration][presented-frame]")
{
  CpuHost host;
  REQUIRE(host.frames->setExposed(host.viewId, true));
  host.request(false);
  host.present();
  const auto old = host.request();
  auto retained = host.frames->result(host.credentials, old);
  REQUIRE(retained);
  const auto oldPng = retained->png;
  const auto renders = host.helide->renders;
  host.replaceDevice();
  CHECK(host.helide->renders == 0);
  const auto cold = host.request(false);
  CHECK_FALSE(host.engine->status(host.credentials, cold).terminal);
  CHECK(host.helide->renders == 0); // observation created/rendered no device
  CHECK(host.frames->result(host.credentials, old)->png == oldPng);
  host.pattern->offset = 153;
  host.present();
  auto fresh = host.frames->result(host.credentials, host.request());
  REQUIRE(fresh);
  CHECK(fresh->png != oldPng);
  CHECK(host.helide->renders == 1);
  CHECK(renders == 1);
  int w = 0, h = 0, channels = 0;
  auto *decoded = stbi_load_from_memory(
      fresh->png.data(), fresh->png.size(), &w, &h, &channels, 4);
  REQUIRE(decoded);
  CHECK(decoded[4] == 153);
  stbi_image_free(decoded);
}

TEST_CASE("Authorized CPU sources share near-ceiling retained frame pressure",
    "[integration][presented-frame][frame-memory]")
{
  CpuHost other;
  other.frames.reset(); // only host's service observes both real producers
  CpuHost host(256u << 20, &other.scene);
  host.pipeline.setDimensions(1920, 1080);
  other.pipeline.setDimensions(1920, 1080);
  identity::SceneIdentity extraIdentity(host.ids, other.scene);
  identity::ViewRevisionTracker extraTracker(host.ids, extraIdentity);
  const auto extraScene = host.extraSceneId;
  REQUIRE(extraScene != INVALID_SCENE_ID);
  ViewId extraView{};
  REQUIRE(host.integration.submit(host.sceneId, [&](vsr::scene::Scene &) {
    ViewRegistration vr;
    vr.scene = extraScene;
    vr.grants.read = true;
    vr.role = ViewRole::INTERACTIVE_VIEWPORT;
    vr.observeBinding = [&] {
      auto b = other.binding();
      b->cameraHandle = extraIdentity.handleOf(other.camera.data());
      b->rendererHandle = extraIdentity.handleOf(other.rendererObject.data());
      return b;
    };
    extraView = *host.integration.registerView(std::move(vr));
    REQUIRE(host.frames->attachView(extraView, extraIdentity, extraTracker));
  }));
  host.integration.service();
  const auto firstView = host.viewId;
  REQUIRE(host.frames->setExposed(firstView, true));
  REQUIRE(host.frames->setExposed(extraView, true));
  host.request(false);
  host.viewId = extraView;
  host.request(false);
  host.viewId = firstView;
  std::vector<mcp::Tool> tools;
  mcp::tools::addFrameTools(tools, {*host.frames, *host.engine});
  std::vector<mcp::ToolResult> consumers;
  std::vector<std::shared_ptr<const frame::EncodedFrame>> oldFrames;
  bool exhausted = false;
  for (int i = 0; i < 24; ++i) {
    host.viewId = firstView;
    const bool second = (i % 2) != 0;
    if (second) {
      other.pattern->offset = std::uint8_t(80 + i);
      REQUIRE(host.integration.submit(extraScene, [&](vsr::scene::Scene &) {
        auto receipt = host.frames->beginProduction(extraView, other.pipeline);
        other.pipeline.render();
        REQUIRE(host.frames->present(extraView,
            other.pipeline,
            std::move(receipt),
            [&](const std::uint32_t *p, auto w, auto h) {
              return other.submitDisplay(p, w, h);
            }));
      }));
      host.integration.service();
    } else {
      host.pattern->offset = std::uint8_t(10 + i);
      host.present();
    }
    host.viewId = second ? extraView : firstView;
    const auto ref = host.request();
    auto status = host.engine->status(host.credentials, ref);
    CHECK(host.frames->ownedBytes() <= (256u << 20));
    if (status.result.find("resource_exhausted") != std::string::npos) {
      CHECK_FALSE(host.frames->result(host.credentials, ref));
      exhausted = true;
      break;
    }
    REQUIRE(status.result.find("\"state\":\"available\"") != std::string::npos);
    auto image = host.frames->result(host.credentials, ref);
    REQUIRE(image);
    int w = 0, h = 0, channels = 0;
    auto *decoded = stbi_load_from_memory(
        image->png.data(), image->png.size(), &w, &h, &channels, 4);
    REQUIRE(decoded);
    CHECK(decoded[4] == (second ? 80 + i : 10 + i));
    stbi_image_free(decoded);
    oldFrames.push_back(std::move(image));
    mcp::json::Value args;
    REQUIRE(
        mcp::json::parse("{\"operation_ref\":{\"session_id\":\"" + ref.sessionId
                + "\",\"request_id\":" + std::to_string(ref.requestId) + "}}",
            args));
    auto delivery = tools[1].handler(host.credentials, args);
    REQUIRE(delivery.code == operations::Outcome::OK);
    consumers.push_back(std::move(delivery));
  }
  CHECK(exhausted);
  CHECK(oldFrames.size() >= 2);
  CHECK(host.peakOwned > (220u << 20));
  CHECK(host.peakOwned <= (256u << 20));
  CHECK(other.helide->renders > 0);
  CHECK(host.helide->renders > 0);
  const auto held = host.frames->ownedBytes();
  consumers.clear();
  oldFrames.clear();
  CHECK(host.frames->ownedBytes() < held);
  std::cout << "Aggregate CPU metrics: sources=2 retained_frames="
            << host.nextRequest - 3 << " peak_reserved=" << host.peakOwned
            << '\n';
  REQUIRE(host.integration.submit(host.sceneId, [&](vsr::scene::Scene &) {
    REQUIRE(host.integration.unregisterView(extraView));
    host.frames->pump();
  }));
  host.integration.service();
  host.viewId = firstView;
}

TEST_CASE("CPU observation charges fixed infrastructure before accepting views",
    "[integration][presented-frame][frame-memory]")
{
  CHECK_THROWS_AS(vsr::rendering::FrameBudget(1), std::runtime_error);
  CHECK_THROWS_AS(vsr::rendering::FrameBudget(257u << 20), std::runtime_error);
  auto budget = std::make_shared<vsr::rendering::FrameBudget>(4096);
  CHECK(budget->used() > 0); // budget object/control block are owned too
  CHECK_FALSE(budget->reserve(4096)); // lease overhead cannot exceed the limit
  const auto baseline = budget->used();
  {
    auto lease = budget->reserve(1024);
    REQUIRE(lease);
    CHECK(budget->used() > baseline + 1024);
  }
  CHECK(budget->used() == baseline);
  CHECK_FALSE(budget->reserve(std::numeric_limits<std::size_t>::max()));
  CpuHost host;
  CHECK(host.frames->ownedBytes() >= (2u << 20)); // real worker stack + service
  CHECK(host.frames->ownedBytes() < (8u << 20));
  CHECK_THROWS_AS(frame::FrameService(
                      host.integration,
                      *host.authority,
                      *host.engine,
                      [&] { return host.now; },
                      budget),
      std::runtime_error);
  CHECK(budget->used() == baseline);
}

TEST_CASE("First CPU observation waits for a successful natural presentation",
    "[integration][presented-frame]")
{
  CpuHost host;
  host.present();
  auto closed = host.request();
  REQUIRE(host.engine->status(host.credentials, closed)
              .result.find("unexposed_source")
      != std::string::npos);
  REQUIRE(host.frames->setExposed(host.viewId, true));
  const auto first = host.request(false);
  REQUIRE_FALSE(host.engine->status(host.credentials, first).terminal);
  CHECK(host.helide->renders == 1); // the earlier unobserved display only
  host.present(false);
  REQUIRE_FALSE(host.engine->status(host.credentials, first).terminal);
  CHECK_FALSE(host.frames->result(host.credentials, first));
  host.present();
  host.pattern->offset = 47;
  host.present(); // Preparation has NOT pinned the earlier successful frame.
  host.material->setParameter("color", vsr::math::float3(0.1f));
  host.await(first);
  mcp::json::Value metadata;
  REQUIRE(mcp::json::parse(
      host.engine->status(host.credentials, first).result, metadata));
  CHECK(metadata.get("presentation_id")->n == 3);
  CHECK(metadata.get("observed_presentation_id")->n == 3);
  CHECK(metadata.get("observed_monotonic_seconds")->n
      >= metadata.get("submitted_monotonic_seconds")->n);
  CHECK(metadata.get("stale")->b);
  CHECK(metadata.get("rendered_scene_revision")->s
      != metadata.get("current_scene_revision")->s);
  CHECK(host.nextRequest == 2); // closed request plus this SAME operation
  REQUIRE(host.engine->status(host.credentials, first)
              .result.find("\"state\":\"available\"")
      != std::string::npos);
  auto image = host.frames->result(host.credentials, first);
  REQUIRE(image);
  REQUIRE_FALSE(image->png.empty());
  REQUIRE(image->width == 4);
  REQUIRE(image->height == 2);
  int w = 0, h = 0, channels = 0;
  auto *decoded = stbi_load_from_memory(
      image->png.data(), image->png.size(), &w, &h, &channels, 4);
  REQUIRE(decoded);
  REQUIRE(w == 4);
  REQUIRE(h == 2);
  CHECK(std::vector<std::uint8_t>(decoded, decoded + w * h * 4)
      == host.displayed);
  CHECK(decoded[0] == 255);
  CHECK(decoded[1] == 63);
  CHECK(decoded[3] == 127);
  CHECK(decoded[4 * 4 + 1] == 192);
  stbi_image_free(decoded);
}

TEST_CASE("Cold preparation sleeps and retires without reviving observers",
    "[integration][presented-frame][first-observation]")
{
  CpuHost host;
  REQUIRE(host.frames->setExposed(host.viewId, true));
  REQUIRE(host.integration.submit(host.sceneId, [&](auto &) {
    auto policy = host.integration.capturePolicy(host.viewId)->policy;
    policy.deadlineSeconds = 2;
    REQUIRE(host.integration.updateCapturePolicy(host.viewId, policy));
  }));
  host.integration.service();
  const auto pending = host.request(false);
  REQUIRE_FALSE(host.engine->status(host.credentials, pending).terminal);
  const auto owned = host.frames->ownedBytes();
  for (int i = 0; i < 40; ++i) {
    REQUIRE(host.integration.submit(
        host.sceneId, [&](auto &) { host.frames->pump(); }));
    host.integration.service();
  }
  CHECK_FALSE(host.engine->status(host.credentials, pending).terminal);
  CHECK(host.frames->ownedBytes() == owned);
  CHECK(host.helide->renders == 0);
  CHECK(host.engine->slotsInUse() == 1);
  bool visible = false, storageFreed = false;
  operations::Outcome expected = operations::Outcome::OK;
  SECTION("Cancellation with no presentation releases its backend slot")
  {
    REQUIRE(host.engine->cancel(host.credentials, pending).code
        == operations::Outcome::OK);
    expected = operations::Outcome::CANCELLED;
  }
  SECTION(
      "Cancelled preparation needs no binding after pipeline storage is freed")
  {
    REQUIRE(host.engine->cancel(host.credentials, pending).code
        == operations::Outcome::OK);
    host.pipeline.clear();
    storageFreed = true;
    expected = operations::Outcome::CANCELLED;
  }
  SECTION("The original deadline expires without natural presentation")
  {
    for (int i = 0; i < 121; ++i) {
      host.now += 1.;
      REQUIRE(host.authority->renew(host.credentials) == session::Code::OK);
    }
    expected = operations::Outcome::TIMEOUT;
  }
  SECTION("Narrowed policy lifetime does not get extended by host pumping")
  {
    // Even though the engine admission deadline is 120s, preparation keeps
    // the original narrower two-second source lifetime.
    host.now += 3.;
    REQUIRE(host.authority->renew(host.credentials) == session::Code::OK);
    expected = operations::Outcome::TIMEOUT;
  }
  SECTION("Re-admission after expiry cannot discard the pending cleanup wake")
  {
    host.now += 3.;
    const auto next = host.request(false); // submit performs result pruning
    REQUIRE(host.engine->cancel(host.credentials, next).code
        == operations::Outcome::OK);
    expected = operations::Outcome::TIMEOUT;
  }
  SECTION("Scene READ withdrawal suppresses preparation and releases its slot")
  {
    host.authority->restrict(
        session::Target::scene(host.sceneId), session::Capability::READ);
    expected = operations::Outcome::FORBIDDEN;
  }
  SECTION("Exposure change-back cannot resurrect the pending observer")
  {
    REQUIRE(host.frames->setExposed(host.viewId, false));
    REQUIRE(host.frames->setExposed(host.viewId, true));
    visible = true;
  }
  SECTION("Same-Identifier device replacement does not migrate preparation")
  {
    host.replaceDevice();
    visible = true;
  }
  SECTION(
      "Detached and reattached source is not the originally admitted source")
  {
    host.frames->detachViewer(host.viewId);
    REQUIRE(
        host.frames->attachView(host.viewId, *host.identity, *host.tracker));
    REQUIRE(host.frames->setExposed(host.viewId, true));
    visible = true;
  }
  SECTION("Closing the registered view still routes bounded backend cleanup")
  {
    REQUIRE(host.integration.submit(host.sceneId, [&](auto &) {
      REQUIRE(host.integration.unregisterView(host.viewId));
    }));
    host.integration.service();
    expected = operations::Outcome::FORBIDDEN;
  }
  REQUIRE(host.integration.submit(
      host.sceneId, [&](auto &) { host.frames->pump(); }));
  host.integration.service();
  host.integration.service(); // processes the event wake, not a fresh request
  const auto retired = host.engine->status(host.credentials, pending);
  INFO(operations::outcomeName(
      retired.terminal.value_or(operations::Outcome::UNKNOWN)));
  CHECK(retired.terminal == expected);
  CHECK(retired.heldSlots == 0);
  CHECK(host.engine->slotsInUse() == 0);
  CHECK_FALSE(host.frames->result(host.credentials, pending));
  if (visible)
    CHECK(retired.result.find("source_changed") != std::string::npos);
  if (!storageFreed && host.integration.isRegisteredView(host.viewId)) {
    host.present(); // no cancelled/retired activation licenses another copy
    REQUIRE(host.integration.submit(host.sceneId, [&](auto &) {
      CHECK_FALSE(host.frames->beginProduction(host.viewId, host.pipeline));
    }));
    host.integration.service();
    CHECK_FALSE(host.frames->result(host.credentials, pending));
  }
}

TEST_CASE("Cold cleanup retries after real bounded queue rejection",
    "[integration][presented-frame][first-observation][frame-memory]")
{
  CpuHost host;
  REQUIRE(host.frames->setExposed(host.viewId, true));
  REQUIRE(host.integration.submit(host.sceneId, [&](auto &) {
    auto policy = host.integration.capturePolicy(host.viewId)->policy;
    policy.deadlineSeconds = 2;
    REQUIRE(host.integration.updateCapturePolicy(host.viewId, policy));
  }));
  REQUIRE(host.integration.service() == 1);
  const auto baseline = host.frames->ownedBytes();
  const auto pending = host.request(false);
  REQUIRE_FALSE(host.engine->status(host.credentials, pending).terminal);
  REQUIRE(host.engine->slotsInUse() == 1);
  const auto preparing = host.frames->ownedBytes();
  REQUIRE(preparing > baseline);
  operations::Outcome expected = operations::Outcome::CANCELLED;
  SECTION("Expiry rejects cleanup dispatch with no encoder wake")
  {
    for (int i = 0; i < 121; ++i) {
      host.now += 1.;
      REQUIRE(host.authority->renew(host.credentials) == session::Code::OK);
    }
    expected = operations::Outcome::TIMEOUT;
  }
  SECTION("Source lifetime expires before the operation admission deadline")
  {
    host.now += 3.;
    expected = operations::Outcome::TIMEOUT;
  }
  SECTION("Read withdrawal rejects cleanup dispatch")
  {
    host.authority->restrict(
        session::Target::scene(host.sceneId), session::Capability::READ);
    expected = operations::Outcome::FORBIDDEN;
  }
  SECTION("Cancellation rejects cleanup dispatch")
  {
    REQUIRE(host.engine->cancel(host.credentials, pending).code
        == operations::Outcome::OK);
  }
  SECTION("Session withdrawal rejects cleanup dispatch")
  {
    REQUIRE(host.authority->detach(host.credentials) == session::Code::OK);
  }
  std::size_t drained = 0;
  const auto capacity = host.integration.queueCapacity();
  REQUIRE(host.integration.submit(host.sceneId, [&](auto &) {
    for (std::size_t i = 0; i < capacity; ++i)
      REQUIRE(
          host.integration.submit(host.sceneId, [&](auto &) { ++drained; }));
    REQUIRE_FALSE(host.integration.submit(host.sceneId, [](auto &) {}));
    // Both pumps must fail to dispatch against the actual full public queue.
    host.frames->pump();
    host.frames->pump();
    CHECK(host.engine->slotsInUse() == 1);
    CHECK(host.frames->ownedBytes() == preparing);
    CHECK_FALSE(host.frames->beginProduction(host.viewId, host.pipeline));
  }));
  REQUIRE(host.integration.service() == 1);
  REQUIRE(host.integration.service() == capacity);
  CHECK(drained == capacity);
  CHECK(host.engine->slotsInUse() == 1);
  CHECK(host.integration.service() == 0); // no worker or self-wake spin
  REQUIRE(host.integration.submit(host.sceneId, [&](auto &) {
    host.frames->pump();
    host.frames->pump(); // queued is not cleaned; dispatch is deduplicated
    CHECK(host.engine->slotsInUse() == 1);
    CHECK(host.frames->ownedBytes() == preparing);
  }));
  REQUIRE(host.integration.service() == 1);
  CHECK(host.engine->slotsInUse() == 1);
  CHECK(host.integration.service() == 1); // exactly one executor cleanup
  CHECK(host.engine->slotsInUse() == 0);
  if (expected != operations::Outcome::CANCELLED)
    CHECK(host.engine->status(host.credentials, pending).terminal == expected);
  CHECK_FALSE(host.frames->result(host.credentials, pending));
  REQUIRE(host.integration.submit(host.sceneId, [&](auto &) {
    host.frames->pump();
    CHECK_FALSE(host.frames->beginProduction(host.viewId, host.pipeline));
  }));
  CHECK(host.integration.service() == 1);
  CHECK(host.integration.service() == 0);
  CHECK(host.frames->ownedBytes() == baseline);
  CHECK(host.helide->renders == 0);
}

TEST_CASE("Completed cold cleanup reclaims bounded job capacity",
    "[integration][presented-frame][first-observation][frame-memory]")
{
  CpuHost host;
  REQUIRE(host.frames->setExposed(host.viewId, true));
  const auto baseline = host.frames->ownedBytes();
  // Exceed the retained Job ceiling without waiting out completed lifetimes.
  for (int i = 0; i < 70; ++i) {
    const auto pending = host.request(false);
    REQUIRE_FALSE(host.engine->status(host.credentials, pending).terminal);
    REQUIRE(host.engine->cancel(host.credentials, pending).code
        == operations::Outcome::OK);
    REQUIRE(host.integration.submit(host.sceneId, [&](auto &) {
      host.frames->pump();
      host.frames->pump();
    }));
    CHECK(host.integration.service() == 1);
    CHECK(host.engine->slotsInUse() == 1);
    CHECK(host.integration.service() == 1);
    CHECK(host.engine->slotsInUse() == 0);
    CHECK(host.engine->status(host.credentials, pending).terminal
        == operations::Outcome::CANCELLED);
    REQUIRE(host.integration.submit(host.sceneId, [&](auto &) {
      host.frames->pump();
      CHECK_FALSE(host.frames->beginProduction(host.viewId, host.pipeline));
    }));
    CHECK(host.integration.service() == 1);
    CHECK(host.integration.service() == 0);
    CHECK(host.frames->ownedBytes() == baseline);
  }
  CHECK(host.helide->renders == 0);
}

TEST_CASE("Cold activation cannot follow a changed binding instance",
    "[integration][presented-frame][first-observation]")
{
  CpuHost host;
  auto binding = host.binding();
  binding->sourceInstanceGeneration = 1;
  REQUIRE(host.integration.submit(host.sceneId, [&](auto &) {
    ViewRegistration registration;
    registration.scene = host.sceneId;
    registration.grants.read = true;
    registration.observeBinding = [&] { return binding; };
    host.viewId = *host.integration.registerView(std::move(registration));
    REQUIRE(
        host.frames->attachView(host.viewId, *host.identity, *host.tracker));
  }));
  host.integration.service();
  REQUIRE(host.frames->setExposed(host.viewId, true));
  const auto pending = host.request(false);
  REQUIRE_FALSE(host.engine->status(host.credentials, pending).terminal);
  const char *reason = "source_changed";
  SECTION("Storage replacement with identical Device Identifier")
  {
    ++binding->sourceInstanceGeneration;
  }
  SECTION("Device Identifier change before any natural presentation")
  {
    binding->deviceIdentifier = "barney";
  }
  SECTION("Binding disappears during preparation")
  {
    binding.reset();
    reason = "inactive_binding";
  }
  host.await(pending);
  const auto retired = host.engine->status(host.credentials, pending);
  CHECK(retired.terminal == operations::Outcome::OK);
  CHECK(retired.result.find(reason) != std::string::npos);
  CHECK_FALSE(host.frames->result(host.credentials, pending));
  CHECK(host.helide->renders == 0);
  CHECK(host.engine->slotsInUse() == 0);
}

TEST_CASE(
    "Session withdrawal drains cold preparation without retaining authority",
    "[integration][presented-frame][first-observation]")
{
  CpuHost host;
  REQUIRE(host.frames->setExposed(host.viewId, true));
  const auto pending = host.request(false);
  REQUIRE_FALSE(host.engine->status(host.credentials, pending).terminal);
  REQUIRE(host.authority->detach(host.credentials) == session::Code::OK);
  REQUIRE(host.integration.submit(
      host.sceneId, [&](auto &) { host.frames->pump(); }));
  host.integration.service();
  host.integration.service();
  CHECK(host.engine->slotsInUse() == 0);
  CHECK_FALSE(host.frames->result(host.credentials, pending));
  REQUIRE(host.integration.submit(host.sceneId, [&](auto &) {
    CHECK_FALSE(host.frames->beginProduction(host.viewId, host.pipeline));
  }));
  host.integration.service();
  const auto attached =
      host.authority->attach(host.authority->launchCredential());
  REQUIRE(attached.code == session::Code::OK);
  host.credentials = {host.authority->launchCredential(),
      attached.sessionId,
      attached.sessionSecret};
  const auto fresh = host.request(false);
  REQUIRE_FALSE(host.engine->status(host.credentials, fresh).terminal);
  host.present();
  host.await(fresh);
  CHECK(host.frames->result(host.credentials, fresh));
  CHECK_FALSE(host.frames->result(host.credentials, pending));
}

TEST_CASE(
    "Cold requests share bounded slots and the remaining observer survives cancellation",
    "[integration][presented-frame][first-observation][frame-memory]")
{
  CpuHost host;
  REQUIRE(host.frames->setExposed(host.viewId, true));
  std::vector<operations::OperationRef> pending;
  for (int i = 0; i < 32; ++i) {
    pending.push_back(host.request(false));
    CHECK_FALSE(host.engine->status(host.credentials, pending.back()).terminal);
  }
  CHECK(host.engine->slotsInUse() == 32);
  const operations::OperationRef full{
      host.credentials.sessionId, ++host.nextRequest};
  CHECK(host.frames->submit(host.credentials, full, host.viewId).code
      == operations::Outcome::BUSY);
  for (std::size_t i = 0; i + 1 < pending.size(); ++i)
    REQUIRE(host.engine->cancel(host.credentials, pending[i]).code
        == operations::Outcome::OK);
  REQUIRE(host.integration.submit(
      host.sceneId, [&](auto &) { host.frames->pump(); }));
  host.integration.service();
  host.integration.service();
  CHECK(host.engine->slotsInUse() == 1);
  host.present();
  host.await(pending.back());
  CHECK(host.frames->result(host.credentials, pending.back()));
  CHECK(host.frames->ownedBytes() <= (256u << 20));
  for (std::size_t i = 0; i + 1 < pending.size(); ++i) {
    CHECK(host.engine->status(host.credentials, pending[i]).terminal
        == operations::Outcome::CANCELLED);
    CHECK_FALSE(host.frames->result(host.credentials, pending[i]));
  }
  CHECK(host.engine->slotsInUse() == 0);
}

TEST_CASE(
    "Frame observations retain request-point pixels across presentation and resize",
    "[integration][presented-frame]")
{
  CpuHost host;
  REQUIRE(host.frames->setExposed(host.viewId, true));
  host.request(false); // asynchronous authorized cold preparation
  host.present();
  const auto first = host.request();
  const auto status = host.engine->status(host.credentials, first);
  REQUIRE(status.result.find("\"stale\":false") != std::string::npos);
  auto retained = host.frames->result(host.credentials, first);
  REQUIRE(retained);
  const auto firstPng = retained->png;
  host.pattern->offset = 77;
  host.pipeline.setDimensions(8, 3);
  host.tracker->notePipelineChanged();
  // A produced-but-not-presented buffer must not replace the last display.
  host.pipeline.render();
  const auto oldDisplayed = host.request();
  auto old = host.frames->result(host.credentials, oldDisplayed);
  REQUIRE(old);
  CHECK(old->png == firstPng);
  CHECK(old->width == 4);
  CHECK(host.engine->status(host.credentials, oldDisplayed)
            .result.find("\"stale\":true")
      != std::string::npos);
  host.present();
  const auto fresh = host.request();
  auto newImage = host.frames->result(host.credentials, fresh);
  REQUIRE(newImage);
  CHECK(newImage->width == 8);
  CHECK(newImage->height == 3);
  CHECK(newImage->png != firstPng);
  CHECK(retained->width == 4);
  CHECK(retained->png == firstPng);
  CHECK(host.frames->submit(host.credentials, first, host.viewId).replayed);
  CHECK(host.engine->status(host.credentials, first).result == status.result);
  CHECK(host.frames->result(host.credentials, first)->png == firstPng);
}

TEST_CASE(
    "Frame reads require current Scene and view authority including retained refs",
    "[integration][presented-frame]")
{
  CpuHost host;
  REQUIRE(host.frames->setExposed(host.viewId, true));
  host.request(false);
  host.present();
  const auto ref = host.request();
  REQUIRE(host.frames->result(host.credentials, ref));
  host.authority->restrict(
      session::Target::scene(host.sceneId), session::Capability::READ);
  CHECK_FALSE(host.frames->result(host.credentials, ref));
  const auto minimal = host.engine->status(host.credentials, ref);
  CHECK_FALSE(minimal.resultVisible);
  CHECK(minimal.result.empty());
  auto denied = host.frames->submit(host.credentials,
      {host.credentials.sessionId, ++host.nextRequest},
      host.viewId);
  CHECK(denied.code == operations::Outcome::FORBIDDEN);
  host.present(); // no authorized observer, no copies
  host.authority->relax(
      session::Target::scene(host.sceneId), session::Capability::READ);
  const auto cold = host.request(false);
  CHECK_FALSE(host.engine->status(host.credentials, cold).terminal);
  host.present();
  const auto queued =
      operations::OperationRef{host.credentials.sessionId, ++host.nextRequest};
  REQUIRE(host.frames->submit(host.credentials, queued, host.viewId).code
      == operations::Outcome::OK);
  host.authority->restrict(
      session::Target::view(host.viewId), session::Capability::READ);
  host.integration.service();
  CHECK_FALSE(host.frames->result(host.credentials, queued));
  CHECK(host.engine->status(host.credentials, queued).result.empty());
}

TEST_CASE(
    "Cancelled frame operations never acquire images and expiry frees retained owners",
    "[integration][presented-frame]")
{
  CpuHost host;
  REQUIRE(host.frames->setExposed(host.viewId, true));
  host.request(false);
  host.present();
  const auto ref =
      operations::OperationRef{host.credentials.sessionId, ++host.nextRequest};
  REQUIRE(host.frames->submit(host.credentials, ref, host.viewId).code
      == operations::Outcome::OK);
  CHECK(host.engine->cancel(host.credentials, ref).code
      == operations::Outcome::OK);
  host.integration.service();
  CHECK_FALSE(host.frames->result(host.credentials, ref));
  const auto done = host.request();
  auto held = host.frames->result(host.credentials, done);
  REQUIRE(held);
  host.now += 121.;
  REQUIRE(host.integration.submit(
      host.sceneId, [&](vsr::scene::Scene &) { host.frames->pump(); }));
  host.integration.service();
  CHECK_FALSE(host.frames->result(host.credentials, done));
  CHECK_FALSE(
      held->png.empty()); // held immutable consumer keeps its reservation
  const auto heldBytes = host.frames->ownedBytes();
  REQUIRE(heldBytes > 0);
  held.reset();
  CHECK(host.frames->ownedBytes() < heldBytes);
}

TEST_CASE(
    "CPU frame acquisition pins before deferred encoding while natural presentation continues",
    "[integration][presented-frame]")
{
  CpuHost host;
  REQUIRE(host.frames->setExposed(host.viewId, true));
  host.request(false);
  host.present();
  const auto expectedPixels = host.displayed;
  const auto pending =
      operations::OperationRef{host.credentials.sessionId, ++host.nextRequest};
  REQUIRE(host.frames->submit(host.credentials, pending, host.viewId).code
      == operations::Outcome::OK);
  host.integration.service();
  REQUIRE_FALSE(host.engine->status(host.credentials, pending).terminal);
  host.pattern->offset = 99;
  host.present();
  for (int i = 0;
      i < 1000 && !host.engine->status(host.credentials, pending).terminal;
      ++i) {
    REQUIRE(host.integration.submit(
        host.sceneId, [&](vsr::scene::Scene &) { host.frames->pump(); }));
    host.integration.service();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const auto result = host.frames->result(host.credentials, pending);
  REQUIRE(result);
  int w = 0, h = 0, channels = 0;
  auto *decoded = stbi_load_from_memory(
      result->png.data(), result->png.size(), &w, &h, &channels, 4);
  REQUIRE(decoded);
  CHECK(std::vector<std::uint8_t>(decoded, decoded + w * h * 4)
      == expectedPixels);
  CHECK(host.displayed != expectedPixels);
  stbi_image_free(decoded);
}

TEST_CASE(
    "CPU presentation preserves real Scene provenance and performs no observation render",
    "[integration][presented-frame]")
{
  CpuHost host;
  host.pipeline.setDimensions(64, 32);
  host.pattern->setEnabled(false);
  host.origin = image::RowOrigin::BOTTOM_LEFT;
  REQUIRE(host.frames->setExposed(host.viewId, true));
  host.request(false);
  host.present();
  const auto renders = host.helide->renders;
  const auto old = host.request();
  const auto oldPng = host.frames->result(host.credentials, old)->png;
  CHECK(host.helide->renders == renders);
  host.camera->setParameter("position", vsr::math::float3(1.f, 0.f, 5.f));
  host.material->setParameter("color", vsr::math::float3(0.8f, 0.1f, 0.2f));
  const auto stale = host.request();
  REQUIRE(host.frames->result(host.credentials, stale));
  CHECK(host.frames->result(host.credentials, stale)->png == oldPng);
  mcp::json::Value data;
  REQUIRE(mcp::json::parse(
      host.engine->status(host.credentials, stale).result, data));
  CHECK(data.get("stale")->b);
  CHECK(data.get("rendered_scene_revision")->s
      != data.get("current_scene_revision")->s);
  CHECK(
      data.get("rendered_configuration")->get("camera")->get("position")->a[0].n
      == 0.);
  CHECK(data.get("rendered_configuration")->get("display")->get("width")->n
      == 128.);
  CHECK(data.get("rendered_configuration")->get("display")->get("scale_x")->n
      == 2.);
  CHECK(host.helide->renders == renders);
  host.present();
  const auto fresh = host.request();
  REQUIRE(host.frames->result(host.credentials, fresh));
  CHECK(host.frames->result(host.credentials, fresh)->png != oldPng);
  int w = 0, h = 0, channels = 0;
  auto frame = host.frames->result(host.credentials, fresh);
  auto *decoded = stbi_load_from_memory(
      frame->png.data(), frame->png.size(), &w, &h, &channels, 4);
  REQUIRE(decoded);
  CHECK(std::vector<std::uint8_t>(decoded, decoded + w * h * 4)
      == host.displayed);
  stbi_image_free(decoded);
}

TEST_CASE(
    "CPU snapshots include the actual Outline pass and normalize display orientation once",
    "[integration][presented-frame]")
{
  CpuHost host;
  REQUIRE(host.frames->setExposed(host.viewId, true));
  host.request(false);
  host.present();
  const auto without = host.request();
  auto plain = host.frames->result(host.credentials, without);
  REQUIRE(plain);
  host.outline->setEnabled(true);
  host.origin = image::RowOrigin::BOTTOM_LEFT;
  host.tracker->notePipelineChanged();
  host.present();
  auto with = host.frames->result(host.credentials, host.request());
  REQUIRE(with);
  CHECK(with->png != plain->png);
  int w = 0, h = 0, channels = 0;
  auto *decoded = stbi_load_from_memory(
      with->png.data(), with->png.size(), &w, &h, &channels, 4);
  REQUIRE(decoded);
  CHECK(decoded[4] == 204); // actual in-pipeline orange Outline, not UI chrome
  CHECK(std::vector<std::uint8_t>(decoded, decoded + w * h * 4)
      == host.displayed);
  stbi_image_free(decoded);
}

TEST_CASE(
    "Retained CPU frame deliveries exhaust shared ownership without substituting older images",
    "[integration][presented-frame]")
{
  CpuHost host(16u << 20);
  REQUIRE(host.frames->setExposed(host.viewId, true));
  host.request(false);
  std::vector<mcp::Tool> tools;
  mcp::tools::addFrameTools(tools, {*host.frames, *host.engine});
  std::vector<mcp::ToolResult> slowConsumers;
  bool exhausted = false;
  std::size_t admittedImages = 0;
  for (int i = 0; i < 40; ++i) {
    host.pattern->offset = std::uint8_t(i);
    host.present();
    const auto ref = host.request();
    const auto status = host.engine->status(host.credentials, ref);
    REQUIRE(host.frames->ownedBytes() <= (16u << 20));
    if (status.result.find("resource_exhausted") != std::string::npos) {
      exhausted = true;
      CHECK_FALSE(host.frames->result(host.credentials, ref));
      break;
    }
    REQUIRE(host.frames->result(host.credentials, ref));
    ++admittedImages;
    mcp::json::Value args;
    REQUIRE(
        mcp::json::parse("{\"operation_ref\":{\"session_id\":\"" + ref.sessionId
                + "\",\"request_id\":" + std::to_string(ref.requestId) + "}}",
            args));
    auto delivery = tools[1].handler(host.credentials, args);
    if (delivery.code == operations::Outcome::BUSY) {
      exhausted = true;
      break;
    }
    REQUIRE(delivery.resourceLease);
    slowConsumers.push_back(std::move(delivery));
  }
  CHECK(admittedImages > 0);
  CHECK(exhausted);
  const auto withConsumers = host.frames->ownedBytes();
  slowConsumers.clear();
  CHECK(host.frames->ownedBytes() < withConsumers);
  CHECK(
      host.helide->renders > admittedImages); // natural rendering kept running
}

#if defined(__linux__)
TEST_CASE(
    "Actual slow HTTP frame drains retain bounded ownership and current authority",
    "[integration][presented-frame][frame-memory][frame-http]")
{
  CpuHost host;
  host.pipeline.setDimensions(1920, 1080);
  host.pattern->noise = true;
  REQUIRE(host.frames->setExposed(host.viewId, true));
  host.request(false);
  host.present();
  const auto first = host.request();
  REQUIRE(
      host.frames->result(host.credentials, first)->png.size() > (7u << 20));
  FrameTransport transport(host);
  const auto baseline = host.frames->ownedBytes();
  const auto naturalRenders = host.helide->renders;
  SECTION("Multiple real socket drains exhaust aggregate response ownership")
  {
    FrameSocket a{transport.open(first)};
    transport.awaitOwned(baseline + (70u << 20));
    FrameSocket b{transport.open(first)};
    transport.awaitOwned(baseline + (140u << 20));
    const auto held = host.frames->ownedBytes();
    CHECK(held <= (256u << 20));
    // Both TCP windows are now full; leases must survive handler return.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(host.frames->ownedBytes() >= baseline + (140u << 20));
    FrameSocket c{transport.open(first)};
    auto busy = transport.read(c);
    REQUIRE(busy.get("error"));
    CHECK(busy.get("error")->get("data")->get("code")->s == "busy");
    CHECK(host.helide->renders == naturalRenders);
    auto drained = transport.read(a);
    REQUIRE(drained.get("result"));
    const auto *items = drained.get("result")->get("content");
    REQUIRE(items);
    bool image = false;
    for (const auto &item : items->a)
      image = image || (item.get("type")->s == "image");
    CHECK(image);
    drained = {};
    transport.read(b);
    transport.awaitReleased(baseline);
    CHECK(host.engine->status(host.credentials, first).resultVisible);
    FrameSocket oversizedId{transport.open(
        first, "vsr_view_frame_image", "\"" + std::string(2048, 'x') + "\"")};
    auto rejected = transport.read(oversizedId);
    REQUIRE(rejected.get("error"));
    CHECK(rejected.get("error")->get("data")->get("code")->s == "busy");
    CHECK(rejected.get("id")->type == mcp::json::Value::Type::NUL);
    std::cout << "HTTP CPU metrics: slow_drains=2 png_bytes="
              << host.frames->result(host.credentials, first)->png.size()
              << " held_reserved=" << held << '\n';
  }
  SECTION(
      "Cancellation and read withdrawal drain immutable work without publishing again")
  {
    FrameSocket slow{transport.open(first)};
    transport.awaitOwned(baseline + (70u << 20));
    // Reservation precedes encoding and its final authority check. Observe
    // actual image publication before withdrawal, consuming only headers so
    // the bounded TCP receive window still backpressures the immutable body.
    transport.awaitImageHeaders(slow);
    host.pattern->offset = 93;
    host.present(); // natural viewing continues while the client does not read
    const operations::OperationRef pending{
        host.credentials.sessionId, ++host.nextRequest};
    REQUIRE(host.frames->submit(host.credentials, pending, host.viewId).code
        == operations::Outcome::OK);
    host.integration.service();
    REQUIRE_FALSE(host.engine->status(host.credentials, pending).terminal);
    CHECK(host.engine->cancel(host.credentials, pending).code
        == operations::Outcome::OK);
    host.authority->restrict(
        session::Target::scene(host.sceneId), session::Capability::READ);
    std::size_t beats = 0;
    for (int i = 0; i < 10000; ++i) {
      REQUIRE(host.integration.submit(host.sceneId, [&](vsr::scene::Scene &) {
        ++beats;
        host.frames->pump();
      }));
      host.integration.service();
      if (!host.engine->status(host.credentials, pending).heldSlots)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(beats > 0);
    CHECK(host.engine->status(host.credentials, pending).terminal
        == operations::Outcome::CANCELLED);
    CHECK(host.engine->status(host.credentials, pending).heldSlots == 0);
    CHECK_FALSE(host.frames->result(host.credentials, pending));
    FrameSocket denied{transport.open(first)};
    auto receipt = transport.read(denied);
    const auto *result = receipt.get("result");
    REQUIRE(result);
    CHECK_FALSE(result->get("structuredContent")->get("result"));
    for (const auto &item : result->get("content")->a)
      CHECK(item.get("type")->s != "image");
    // A response already published under authority cannot retract sent bytes.
    // Its immutable lease remains held, even after cache/job withdrawal.
    CHECK(host.frames->ownedBytes() >= (70u << 20));
    transport.read(slow, false);
    transport.awaitReleased(baseline);
    CHECK(host.helide->renders == naturalRenders + 1);
  }
}
#endif

TEST_CASE("Interop without CPU readiness retires viewer exposure",
    "[integration][presented-frame][viewer-presentation]")
{
  CpuHost host;
  ViewRegistration registration;
  registration.scene = host.sceneId;
  registration.grants.read = true;
  registration.role = ViewRole::INTERACTIVE_VIEWPORT;
  registration.observeBinding = [&] { return host.binding(); };
  REQUIRE(host.integration.submit(host.sceneId, [&](auto &) {
    host.viewId = *host.integration.registerView(registration);
    REQUIRE(host.frames->attachView(host.viewId,
        *host.identity,
        *host.tracker,
        frame::SourceQualification::VIEWER_CPU_READY));
  }));
  host.integration.service();
  REQUIRE(host.frames->setExposed(host.viewId, true));
  host.request(false); // authorized preparation, never a replacement render
  REQUIRE_FALSE(host.frames->acquireViewerPixels(
      host.viewId, {}, [&] { return *host.binding(); }, nullptr, 4, 2));
  CHECK_FALSE(
      host.integration.capturePolicy(host.viewId)->presentedFramesExposed);
  CHECK_FALSE(host.frames->setExposed(host.viewId, true));
  const auto ref = host.request();
  CHECK(host.engine->status(host.credentials, ref)
            .result.find("interop_acquisition_unqualified")
      != std::string::npos);
  CHECK_FALSE(host.frames->result(host.credentials, ref));
}

TEST_CASE("Unknown viewer upload cannot borrow CPU-ready qualification",
    "[integration][presented-frame][viewer-presentation]")
{
  CpuHost host;
  ViewRegistration registration;
  registration.scene = host.sceneId;
  registration.grants.read = true;
  registration.role = ViewRole::INTERACTIVE_VIEWPORT;
  registration.observeBinding = [&] { return host.binding(); };
  REQUIRE(host.integration.submit(host.sceneId, [&](auto &) {
    host.viewId = *host.integration.registerView(registration);
    REQUIRE(host.frames->attachView(host.viewId,
        *host.identity,
        *host.tracker,
        frame::SourceQualification::VIEWER_CPU_READY));
  }));
  host.integration.service();
  REQUIRE(host.frames->setExposed(host.viewId, true));
  host.request(false);
  // A path declaration is not a CPU-ready transfer receipt. Even a non-null
  // pointer must not be touched when the actual host seam is unsupported.
  const auto *unreadable = reinterpret_cast<const std::uint32_t *>(1);
  REQUIRE_FALSE(host.frames->acquireViewerPixels(
      host.viewId, {}, [&] { return *host.binding(); }, unreadable, 4, 2));
  CHECK_FALSE(
      host.integration.capturePolicy(host.viewId)->presentedFramesExposed);
  CHECK_FALSE(host.frames->setExposed(host.viewId, true));
}

TEST_CASE("Viewer output profile and instance gate precede pixel access",
    "[integration][presented-frame][viewer-presentation]")
{
  CpuHost host;
  auto binding = *host.binding();
  binding.pipelineSource = "viewer_image_pipeline";
  binding.sourceInstanceGeneration = 1;
  binding.pipelineSettings = {{"aov", {0.}, {}},
      {"tone_map_operator", {2.}, {}},
      {"exposure", {0.}, {}},
      {"auto_exposure", {0.}, {}},
      {"gamma", {double(2.2f)}, {}},
      {"tone_map_enabled", {1.}, {}},
      {"output_transform_enabled", {1.}, {}},
      {"primitive_outline_enabled", {0.}, {}},
      {"box_outline_enabled", {0.}, {}}};
  ViewRegistration registration;
  registration.scene = host.sceneId;
  registration.grants.read = true;
  registration.role = ViewRole::INTERACTIVE_VIEWPORT;
  registration.observeBinding = [&] {
    return std::optional<EffectiveViewBinding>(binding);
  };
  REQUIRE(host.integration.submit(host.sceneId, [&](auto &) {
    host.viewId = *host.integration.registerView(registration);
    REQUIRE(host.frames->attachView(host.viewId,
        *host.identity,
        *host.tracker,
        frame::SourceQualification::VIEWER_CPU_READY));
  }));
  host.integration.service();
  REQUIRE(host.frames->setExposed(host.viewId, true));
  host.request(false);
  const auto start =
      host.frames->beginViewerProduction(host.viewId, [&] { return binding; });
  REQUIRE(start);
  auto changed = binding;
  const auto *unreadable = reinterpret_cast<const std::uint32_t *>(1);
  bool scaleCase = false;
  SECTION("Presentation-time high DPI cannot reuse a qualified old upload")
  {
    const std::vector<std::uint32_t> pixels(
        std::size_t(binding.width) * binding.height, 0xff0000ffu);
    const auto acquired = host.frames->acquireViewerPixels(
        host.viewId,
        start,
        [&] { return binding; },
        pixels.data(),
        binding.width,
        binding.height,
        "host_sdl_upload");
    REQUIRE(acquired);
    host.frames->presentViewerPixels(host.viewId,
        acquired,
        binding.width,
        binding.height,
        binding.displayWidth,
        binding.displayHeight,
        1.);
    const auto visible = host.request();
    REQUIRE(host.frames->result(host.credentials, visible));
    host.frames->presentViewerPixels(host.viewId,
        acquired,
        binding.width,
        binding.height,
        binding.displayWidth,
        binding.displayHeight,
        2.,
        2.,
        2.);
    const auto scaled = host.request();
    CHECK_FALSE(host.frames->result(host.credentials, scaled));
    CHECK(host.engine->status(host.credentials, scaled)
              .result.find("unsupported_display_scale_profile")
        != std::string::npos);
    // Fresh polling of an unchanged unsupported presentation retains bounded
    // terminal receipts, not encoder work or hidden retries. Together with the
    // initial cold and available requests, this reaches the declared 64 jobs.
    for (int i = 0; i < 61; ++i) {
      const auto unavailable = host.request();
      CHECK_FALSE(host.frames->result(host.credentials, unavailable));
      CHECK(host.engine->status(host.credentials, unavailable)
                .result.find("unsupported_display_scale_profile")
          != std::string::npos);
    }
    const auto held = host.frames->ownedBytes();
    CHECK(held < (256u << 20));
    const operations::OperationRef full{
        host.credentials.sessionId, ++host.nextRequest};
    CHECK(host.frames->submit(host.credentials, full, host.viewId).code
        == operations::Outcome::BUSY);
    // Expiry releases the jobs' infrastructure as well as the retained PNG;
    // no wider count/deadline or encoder retry is needed to recover admission.
    for (int i = 0; i < 121; ++i) {
      host.now += 1.;
      REQUIRE(host.authority->renew(host.credentials) == session::Code::OK);
    }
    REQUIRE(host.integration.submit(
        host.sceneId, [&](auto &) { host.frames->pump(); }));
    host.integration.service();
    const auto drained = host.frames->ownedBytes();
    CHECK(drained + 64 * (16u << 10) < held);
    CHECK_FALSE(host.frames->result(host.credentials, visible));
    host.request(false); // Preparation awaits natural host production.
    const auto resumed = host.frames->beginViewerProduction(
        host.viewId, [&] { return binding; });
    REQUIRE(resumed);
    const auto resumedPixels = host.frames->acquireViewerPixels(
        host.viewId,
        resumed,
        [&] { return binding; },
        pixels.data(),
        binding.width,
        binding.height,
        "host_sdl_upload");
    REQUIRE(resumedPixels);
    host.frames->presentViewerPixels(host.viewId,
        resumedPixels,
        binding.width,
        binding.height,
        binding.displayWidth,
        binding.displayHeight,
        3.);
    const auto restored = host.request();
    REQUIRE(host.frames->result(host.credentials, restored));
    const auto restoredStatus = host.engine->status(host.credentials, restored);
    CHECK(restoredStatus.result.find("\"presentation_id\":3")
        != std::string::npos);
    std::cout << "QUALIFIED terminalRequests=64 heldOwned=" << held
              << " afterExpiry=" << drained << '\n';
    scaleCase = true;
  }
  SECTION("Same Device Identifier cannot revive replacement storage")
  {
    ++changed.sourceInstanceGeneration;
  }
  SECTION("Any in-pipeline effect profile is captured as presented")
  {
    // AOV, tone mapping, auto exposure, gamma and outlines all act before
    // the uploaded RGBA8 sRGB buffer; the capture is that buffer, reported
    // with the settings of the upload that produced it.
    auto effects = binding;
    effects.pipelineSettings = {{"aov", {1.}, {}},
        {"tone_map_operator", {0.}, {}},
        {"exposure", {0.5}, {}},
        {"auto_exposure", {1.}, {}},
        {"gamma", {1.9}, {}},
        {"tone_map_enabled", {1.}, {}},
        {"output_transform_enabled", {1.}, {}},
        {"primitive_outline_enabled", {1.}, {}},
        {"box_outline_enabled", {1.}, {}}};
    const std::vector<std::uint32_t> pixels(
        std::size_t(binding.width) * binding.height, 0xff00ff00u);
    const auto acquired = host.frames->acquireViewerPixels(
        host.viewId,
        start,
        [&] { return effects; },
        pixels.data(),
        binding.width,
        binding.height,
        "host_sdl_upload");
    REQUIRE(acquired);
    host.frames->presentViewerPixels(host.viewId,
        acquired,
        binding.width,
        binding.height,
        binding.displayWidth,
        binding.displayHeight,
        1.);
    const auto ref = host.request();
    REQUIRE(host.frames->result(host.credentials, ref));
    const auto status = host.engine->status(host.credentials, ref);
    CHECK(status.result.find("\"name\":\"tone_map_operator\",\"numbers\":[0]")
        != std::string::npos);
    CHECK(status.result.find("\"name\":\"auto_exposure\",\"numbers\":[1]")
        != std::string::npos);
    scaleCase = true;
  }
  SECTION("An unknown pixel encoding is not read")
  {
    changed.outputColor = "rgba32f_linear";
  }
  SECTION("An unknown pipeline source is not read")
  {
    changed.pipelineSource = "other_pipeline";
  }
  if (!scaleCase) {
    REQUIRE_FALSE(host.frames->acquireViewerPixels(
        host.viewId,
        start,
        [&] { return changed; },
        unreadable,
        binding.width,
        binding.height,
        "host_sdl_upload"));
    host.frames->presentViewerPixels(host.viewId,
        {},
        binding.width,
        binding.height,
        binding.displayWidth,
        binding.displayHeight,
        1.);
    const auto ref = host.request();
    CHECK_FALSE(host.frames->result(host.credentials, ref));
  }
}

TEST_CASE("Read withdrawal reclaims unheld CPU PNGs without waiting for expiry",
    "[integration][presented-frame][frame-memory]")
{
  CpuHost host;
  host.pipeline.setDimensions(1920, 1080);
  host.pattern->noise = true;
  REQUIRE(host.frames->setExposed(host.viewId, true));
  host.request(false);
  host.present();
  const auto ref = host.request();
  const auto pngBytes = host.frames->result(host.credentials, ref)->png.size();
  REQUIRE(pngBytes > (7u << 20));
  const auto held = host.frames->ownedBytes();
  host.authority->restrict(
      session::Target::scene(host.sceneId), session::Capability::READ);
  REQUIRE(host.integration.submit(
      host.sceneId, [&](vsr::scene::Scene &) { host.frames->pump(); }));
  host.integration.service();
  CHECK_FALSE(host.frames->result(host.credentials, ref));
  CHECK(host.frames->ownedBytes() + pngBytes + 1920u * 1080u * 4 <= held);
}

TEST_CASE("Public frame status copies retain charged response ownership",
    "[integration][presented-frame][frame-memory]")
{
  CpuHost host(16u << 20);
  REQUIRE(host.frames->setExposed(host.viewId, true));
  host.request(false);
  host.present();
  const auto ref = host.request();
  const auto baseline = host.frames->ownedBytes();
  {
    std::vector<operations::Status> retained;
    bool busy = false;
    for (int i = 0; i < 32; ++i) {
      auto status = host.engine->status(host.credentials, ref);
      if (status.code == operations::Outcome::BUSY) {
        busy = true;
        break;
      }
      REQUIRE(status.resultVisible);
      retained.push_back(std::move(status));
    }
    CHECK(busy);
    CHECK(host.frames->ownedBytes() > baseline);
    CHECK(host.frames->ownedBytes() <= (16u << 20));
  }
  CHECK(host.frames->ownedBytes() == baseline);
  CHECK(host.engine->status(host.credentials, ref).resultVisible);
  std::vector<mcp::Tool> generic;
  mcp::tools::addLookupTools(generic, *host.engine);
  mcp::json::Value args;
  REQUIRE(
      mcp::json::parse("{\"operation_ref\":{\"session_id\":\"" + ref.sessionId
              + "\",\"request_id\":" + std::to_string(ref.requestId) + "}}",
          args));
  {
    auto response = generic[0].handler(host.credentials, args);
    REQUIRE(response.code == operations::Outcome::OK);
    REQUIRE(response.resourceLease);
    CHECK(response.maxResponseBytes == 12582912);
    CHECK(host.frames->ownedBytes() > baseline);
  }
  CHECK(host.frames->ownedBytes() == baseline);
}

TEST_CASE("Closed and recreated CPU sources cannot revive latest identities",
    "[integration][presented-frame]")
{
  CpuHost host;
  REQUIRE(host.frames->setExposed(host.viewId, true));
  host.request(false);
  host.present();
  const auto oldView = host.viewId;
  const auto old = host.request();
  const auto oldIdentity = host.integration.viewDescription(oldView)->identity;
  REQUIRE(host.frames->setExposed(oldView, false));
  REQUIRE(host.frames->setExposed(oldView, true));
  const auto cold = host.request(false);
  CHECK_FALSE(host.engine->status(host.credentials, cold).terminal);
  auto tracker =
      std::make_unique<identity::ViewRevisionTracker>(host.ids, *host.identity);
  REQUIRE(host.integration.submit(host.sceneId, [&](vsr::scene::Scene &) {
    auto binding = host.integration.observeViewBinding(oldView);
    REQUIRE(binding);
    REQUIRE(host.integration.unregisterView(oldView));
    ViewRegistration registration;
    registration.scene = host.sceneId;
    registration.grants.read = true;
    registration.role = ViewRole::HEADLESS;
    registration.observeBinding = [binding] { return binding; };
    host.viewId = *host.integration.registerView(std::move(registration));
    REQUIRE(host.frames->attachView(host.viewId, *host.identity, *tracker));
  }));
  host.integration.service();
  CHECK(host.viewId != oldView);
  CHECK(host.integration.viewDescription(host.viewId)->identity != oldIdentity);
  CHECK_FALSE(host.frames->result(host.credentials, old));
  const auto closed = host.request();
  CHECK(host.engine->status(host.credentials, closed)
            .result.find("unexposed_source")
      != std::string::npos);
  REQUIRE(host.frames->setExposed(host.viewId, true));
  host.request(false);
  host.present();
  const auto fresh = host.request();
  REQUIRE(host.frames->result(host.credentials, fresh));
  CHECK(host.engine->status(host.credentials, fresh).result.find(oldIdentity)
      == std::string::npos);
  host.frames->setExposed(host.viewId, false);
  REQUIRE(host.integration.submit(host.sceneId, [&](vsr::scene::Scene &) {
    host.integration.unregisterView(host.viewId);
  }));
  host.integration.service();
}

TEST_CASE(
    "CPU source native dimensions obey narrowed public policy without resampling",
    "[integration][presented-frame]")
{
  CpuHost host;
  REQUIRE(host.frames->setExposed(host.viewId, true));
  REQUIRE(host.integration.submit(host.sceneId, [&](vsr::scene::Scene &) {
    CapturePolicy policy;
    policy.maxAxis = 4;
    policy.maxPixels = 8;
    REQUIRE(host.integration.updateCapturePolicy(host.viewId, policy));
  }));
  host.integration.service();
  const auto oversize = host.request(false);
  host.pipeline.setDimensions(4, 3);
  host.present();
  host.await(oversize);
  mcp::json::Value result;
  REQUIRE(mcp::json::parse(
      host.engine->status(host.credentials, oversize).result, result));
  CHECK(result.get("reason")->s == "oversize_source");
  CHECK(result.get("native_width")->n == 4);
  CHECK(result.get("native_height")->n == 3);
  CHECK(result.get("limits")->get("max_axis")->n == 4);
  CHECK(result.get("limits")->get("max_pixels")->n == 8);
  CHECK_FALSE(host.frames->result(host.credentials, oversize));
  CHECK(host.pipeline.dimensions().y == 3);
}

TEST_CASE(
    "CPU cancellation and teardown drain deferred work after pipeline storage is freed",
    "[integration][presented-frame]")
{
  CpuHost host;
  REQUIRE(host.frames->setExposed(host.viewId, true));
  const auto activated = host.request(false);
  host.present();
  host.await(activated); // no unrelated cold operation may outlive its binding
  const auto ref =
      operations::OperationRef{host.credentials.sessionId, ++host.nextRequest};
  REQUIRE(host.frames->submit(host.credentials, ref, host.viewId).code
      == operations::Outcome::OK);
  host.integration.service();
  REQUIRE_FALSE(host.engine->status(host.credentials, ref).terminal);
  CHECK(host.engine->cancel(host.credentials, ref).code
      == operations::Outcome::OK);
  host.pipeline.clear();
  for (int i = 0; i < 2000; ++i) {
    REQUIRE(host.integration.submit(
        host.sceneId, [&](vsr::scene::Scene &) { host.frames->pump(); }));
    host.integration.service();
    auto status = host.engine->status(host.credentials, ref);
    if (status.heldSlots == 0)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  auto status = host.engine->status(host.credentials, ref);
  CHECK(status.terminal == operations::Outcome::CANCELLED);
  CHECK(status.heldSlots == 0);
  CHECK_FALSE(host.frames->result(host.credentials, ref));
  host.frames.reset();
  CHECK_FALSE(
      host.integration.capturePolicy(host.viewId)->presentedFramesExposed);
}

TEST_CASE(
    "Successful submission followed by premature CPU buffer reuse never returns a substitute",
    "[integration][presented-frame]")
{
  CpuHost host;
  REQUIRE(host.frames->setExposed(host.viewId, true));
  host.request(false);
  host.present();
  const auto first = host.request();
  REQUIRE(host.frames->result(host.credentials, first));
  host.reuseAfterSubmission = true;
  host.present();
  const auto latest = host.request();
  CHECK(host.engine->status(host.credentials, latest)
            .result.find("source_reused_before_acquisition")
      != std::string::npos);
  CHECK_FALSE(host.frames->result(host.credentials, latest));
  CHECK(host.frames->result(host.credentials, first));
}

TEST_CASE("An unqualified real viewer cannot inherit CPU source qualification",
    "[integration][presented-frame][viewer-presentation]")
{
  CpuHost host;
  ViewRegistration registration;
  registration.scene = host.sceneId;
  registration.role = ViewRole::INTERACTIVE_VIEWPORT;
  registration.grants.read = true;
  registration.observeBinding = [&] { return host.binding(); };
  std::optional<ViewId> viewer;
  REQUIRE(host.integration.submit(host.sceneId, [&](vsr::scene::Scene &) {
    viewer = host.integration.registerView(registration);
  }));
  host.integration.service();
  REQUIRE(viewer);
  REQUIRE(host.frames->attachView(*viewer,
      *host.identity,
      *host.tracker,
      frame::SourceQualification::VIEWER_UNQUALIFIED));
  REQUIRE_FALSE(host.frames->setExposed(*viewer, true));
  const operations::OperationRef ref{
      host.credentials.sessionId, ++host.nextRequest};
  REQUIRE(host.frames->submit(host.credentials, ref, *viewer).code
      == operations::Outcome::OK);
  host.integration.service();
  const auto status = host.engine->status(host.credentials, ref);
  REQUIRE(status.terminal == operations::Outcome::OK);
  CHECK(status.result.find("viewer_acquisition_unqualified")
      != std::string::npos);
  CHECK_FALSE(host.frames->result(host.credentials, ref));
  CHECK_FALSE(host.integration.capturePolicy(*viewer)->presentedFramesExposed);
  // The separately qualified CPU source still works; roles grant nothing.
  REQUIRE(host.frames->setExposed(host.viewId, true));
  host.request(false);
  host.present();
  CHECK(host.frames->result(host.credentials, host.request()));
}

TEST_CASE(
    "Maximum CPU frame encoding leaves the owning-thread safe point and native rendering responsive",
    "[integration][presented-frame][frame-memory]")
{
  CpuHost host;
  host.pipeline.setDimensions(1920, 1080);
  host.pattern->offset = 17;
  REQUIRE(host.frames->setExposed(host.viewId, true));
  host.request(false);
  const auto naturalStart = std::chrono::steady_clock::now();
  host.present();
  const double naturalMs = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - naturalStart)
                               .count();
  const auto ref =
      operations::OperationRef{host.credentials.sessionId, ++host.nextRequest};
  REQUIRE(host.frames->submit(host.credentials, ref, host.viewId).code
      == operations::Outcome::OK);
  const auto start = std::chrono::steady_clock::now();
  host.integration.service();
  const double pinMs = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start)
                           .count();
  CHECK(pinMs < 100.);
  const auto sampledOwned = host.frames->ownedBytes();
  CHECK(sampledOwned <= (256u << 20));
  std::size_t heartbeats = 0;
  host.pattern->offset = 33;
  host.present();
  for (int i = 0; i < 5000; ++i) {
    REQUIRE(host.integration.submit(host.sceneId, [&](vsr::scene::Scene &) {
      ++heartbeats;
      host.frames->pump();
    }));
    host.integration.service();
    if (host.engine->status(host.credentials, ref).terminal)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(heartbeats > 0);
  auto encoded = host.frames->result(host.credentials, ref);
  REQUIRE(encoded);
  CHECK(encoded->width == 1920);
  CHECK(encoded->height == 1080);
  CHECK(encoded->png.size() <= 8388608);
  int w = 0, h = 0, channels = 0;
  auto *decoded = stbi_load_from_memory(
      encoded->png.data(), encoded->png.size(), &w, &h, &channels, 4);
  REQUIRE(decoded);
  CHECK(decoded[4] == 17);
  CHECK(decoded[(std::size_t(w) * h - 1) * 4] == 17);
  CHECK(decoded[(std::size_t(w) * h - 1) * 4 + 1] == 192);
  stbi_image_free(decoded);
  std::cout << "CPU frame metrics: native=1920x1080 natural_produce_submit_ms="
            << naturalMs << " request_pin_ms=" << pinMs
            << " sampled_owned_bytes=" << sampledOwned
            << " heartbeats=" << heartbeats
            << " png_bytes=" << encoded->png.size() << '\n';
}

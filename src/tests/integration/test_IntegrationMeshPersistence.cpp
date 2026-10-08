// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#if defined(VSR_USE_INTEGRATION_MCP) && defined(__linux__)

// vsr_integration
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/activity/Activity.hpp"
#include "vsr/integration/mcp/McpServer.hpp"
#include "vsr/integration/mcp/tools/EditTools.hpp"
#include "vsr/integration/mcp/tools/ImageTools.hpp"
#include "vsr/integration/mcp/tools/LookupTools.hpp"
#include "vsr/integration/mcp/tools/QueryTools.hpp"
// vsr_io
#include "vsr/io/archives/SceneArchive.hpp"
// vsr_rendering
#include "vsr/rendering/index/RenderIndexAllLayers.hpp"
#include "vsr/rendering/view/FrameObservation.h"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// catch
#include "catch.hpp"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>
// posix
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

using namespace vsr::integration;
namespace json = mcp::json;
namespace fs = std::filesystem;

struct TestDirectory
{
  TestDirectory();
  ~TestDirectory();
  VSR_NOT_COPYABLE(TestDirectory)
  VSR_NOT_MOVEABLE(TestDirectory)
  std::string path;
};

TestDirectory::TestDirectory()
{
  const auto *tmp = std::getenv("TMPDIR");
  path = std::string(tmp ? tmp : "/tmp") + "/vsr-mesh-archive-XXXXXX";
  REQUIRE(::mkdtemp(path.data()));
}

TestDirectory::~TestDirectory()
{
  fs::remove_all(path);
}

struct Socket
{
  Socket() = default;
  ~Socket();
  VSR_NOT_COPYABLE(Socket)
  VSR_NOT_MOVEABLE(Socket)
  int fd{::socket(AF_INET, SOCK_STREAM, 0)};
};

Socket::~Socket()
{
  if (fd >= 0)
    ::close(fd);
}

struct Reply
{
  const json::Value &content() const;
  const json::Value &result() const;
  std::string text(const char *key) const;
  bool committed() const;
  json::Value body;
};

const json::Value &Reply::content() const
{
  REQUIRE(body.get("result"));
  REQUIRE(body.get("result")->get("structuredContent"));
  return *body.get("result")->get("structuredContent");
}

const json::Value &Reply::result() const
{
  REQUIRE(content().get("result"));
  return *content().get("result");
}

std::string Reply::text(const char *key) const
{
  REQUIRE(content().get(key));
  REQUIRE(content().get(key)->isString());
  return content().get(key)->s;
}

bool Reply::committed() const
{
  const auto *value = content().get("committed");
  return value && value->b;
}

// Independent loopback host: no common Image fixture, fault interception,
// renderer mock, private import entrypoint or running viewer is involved.
struct ArchiveHost
{
  ArchiveHost();
  Reply rpc(const std::string &method, const std::string &params);
  Reply tool(const std::string &name, const std::string &arguments);
  Reply await(std::uint64_t request);
  std::string inspectHandle(std::uint64_t request, const char *name);
  std::string exchangeDirectory() const;

  vsr::scene::Scene scene;
  Integration integration;
  identity::IdentityAuthority identities;
  std::unique_ptr<identity::SceneIdentity> sceneIdentity;
  SceneId sceneId{};
  std::unique_ptr<session::SessionAuthority> authority;
  activity::ActivityLog activity;
  std::unique_ptr<activity::EngineActivitySink> activitySink;
  std::unique_ptr<operations::OperationEngine> engine;
  std::unique_ptr<inspection::Inspector> inspector;
  std::unique_ptr<edit::EditService> edits;
  std::unique_ptr<restore::RestoreService> restores;
  TestDirectory directory;
  descriptor::RuntimeDirectory runtime;
  std::unique_ptr<image::ImageExchange> exchange;
  std::unique_ptr<mcp::McpServer> server;
  std::string sessionId;
  std::string sessionSecret;
};

ArchiveHost::ArchiveHost()
{
  SceneRegistration registration;
  registration.name = "archive regression";
  registration.scene = &scene;
  registration.grants.read = true;
  registration.grants.edit = true;
  registration.serializedArrayWrites = true;
  registration.normalSyncHook = [](SceneId) { return true; };
  sceneId = *integration.registerScene(registration);
  REQUIRE(integration.activate().ok());
  sceneIdentity = std::make_unique<identity::SceneIdentity>(identities, scene);
  authority = session::SessionAuthority::create(integration);
  engine = operations::OperationEngine::create(integration, *authority);
  activitySink =
      std::make_unique<activity::EngineActivitySink>(activity, integration);
  engine->setActivitySink(activitySink.get());
  edits = std::make_unique<edit::EditService>(
      integration, *engine, authority->launchId());
  edits->attachScene(sceneId, *sceneIdentity);
  const auto physical =
      std::make_shared<vsr::rendering::FrameBudget>(256u << 20);
  restores = std::make_unique<restore::RestoreService>(
      *engine, *edits, [physical] { return physical->reserve(65536); });
  restores->attachScene(sceneId, *sceneIdentity);
  REQUIRE(descriptor::openDirectory(directory.path, {}, runtime).ok());
  image::ExchangeLimits limits;
  limits.reservePhysicalMemory = [physical](std::size_t bytes) {
    return physical->reserve(bytes);
  };
  limits.retention = &activity;
  limits.restorationRetained = [this](const std::string &launch,
                                   const std::string &batch) {
    return activity.eligibility({launch, batch})
        == activity::Eligibility::ELIGIBLE;
  };
  exchange = std::make_unique<image::ImageExchange>(
      integration, *engine, authority->launchId(), runtime, limits);
  exchange->attachScene(sceneId, *sceneIdentity);
  exchange->attachRestoration(*edits, *restores);
  inspector = std::make_unique<inspection::Inspector>(
      integration, *authority, [this](SceneId id) {
        return id == sceneId ? sceneIdentity.get() : nullptr;
      });
  mcp::Config config;
  config.imageExchange = exchange->metadata(sceneId);
  mcp::tools::addImageTools(config.tools, {identities, *exchange});
  mcp::tools::addQueryTools(config.tools, {*engine, *inspector});
  mcp::tools::addLookupTools(config.tools, *engine);
  mcp::tools::addEditTools(config.tools, {identities, *edits, *restores});
  REQUIRE(
      mcp::McpServer::start(config, *authority, *engine, runtime, server).ok());
  const auto attached = rpc("vsr/attach", "{}");
  REQUIRE(attached.body.get("result"));
  sessionId = attached.body.get("result")->get("session_id")->s;
  sessionSecret = attached.body.get("result")->get("session_secret")->s;
}

Reply ArchiveHost::rpc(const std::string &method, const std::string &params)
{
  Socket socket;
  REQUIRE(socket.fd >= 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(server->port());
  REQUIRE(
      ::connect(
          socket.fd, reinterpret_cast<sockaddr *>(&address), sizeof(address))
      == 0);
  const auto body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":"
      + json::quote(method) + ",\"params\":" + params + "}";
  std::string request = "POST /mcp HTTP/1.1\r\nHost: " + server->authority()
      + "\r\nAuthorization: Bearer " + authority->launchCredential() + "\r\n";
  if (!sessionId.empty())
    request += "VSR-Session-ID: " + sessionId + "\r\nVSR-Session-Proof: Bearer "
        + sessionSecret + "\r\n";
  request +=
      "Content-Type: application/json\r\nConnection: close\r\nContent-Length: "
      + std::to_string(body.size()) + "\r\n\r\n" + body;
  std::size_t sent = 0;
  while (sent < request.size()) {
    const auto count = ::send(
        socket.fd, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
    if (count < 0 && errno == EINTR)
      continue;
    REQUIRE(count > 0);
    sent += std::size_t(count);
  }
  std::string received;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  for (;;) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now())
                          .count();
    REQUIRE(left > 0);
    pollfd pending{socket.fd, POLLIN, 0};
    const auto ready = ::poll(&pending, 1, int(left));
    if (ready < 0 && errno == EINTR)
      continue;
    REQUIRE(ready > 0);
    char buffer[4096];
    const auto count = ::recv(socket.fd, buffer, sizeof(buffer), 0);
    if (count < 0 && errno == EINTR)
      continue;
    REQUIRE(count >= 0);
    if (!count)
      break;
    received.append(buffer, std::size_t(count));
    REQUIRE(received.size() < (1u << 20));
  }
  REQUIRE(received.substr(0, 12) == "HTTP/1.1 200");
  const auto separator = received.find("\r\n\r\n");
  REQUIRE(separator != std::string::npos);
  Reply reply;
  REQUIRE(json::parse(received.substr(separator + 4), reply.body));
  return reply;
}

Reply ArchiveHost::tool(const std::string &name, const std::string &arguments)
{
  return rpc("tools/call",
      "{\"name\":" + json::quote(name) + ",\"arguments\":" + arguments + "}");
}

Reply ArchiveHost::await(std::uint64_t request)
{
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  for (;;) {
    exchange->pump();
    integration.service();
    auto reply = tool("vsr_operation_status",
        "{\"operation_ref\":{\"session_id\":" + json::quote(sessionId)
            + ",\"request_id\":" + std::to_string(request) + "}}");
    if (reply.text("state") == "terminal")
      return reply;
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

std::string ArchiveHost::inspectHandle(std::uint64_t request, const char *name)
{
  tool("vsr_inspect",
      "{\"request_id\":" + std::to_string(request) + ",\"scene_id\":"
          + std::to_string(sceneId) + ",\"name\":" + json::quote(name) + "}");
  const auto inspected = await(request);
  REQUIRE(inspected.text("outcome") == "ok");
  const auto *items = inspected.result().get("items");
  REQUIRE(items);
  REQUIRE(items->a.size() == 1);
  return items->a.front().get("handle")->s;
}

std::string ArchiveHost::exchangeDirectory() const
{
  json::Value metadata;
  REQUIRE(json::parse(exchange->metadata(sceneId), metadata));
  REQUIRE(metadata.get("mesh_import")->b);
  return metadata.get("mesh_exchange_directory")->s;
}

std::vector<std::uint8_t> fileBytes(const std::string &path)
{
  std::ifstream stream(path, std::ios::binary);
  REQUIRE(stream.good());
  return {
      std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

template <typename T>
T *named(const vsr::core::ObjectPool<T> &pool, const char *name)
{
  auto object = find_item_if(
      pool, [name](const T *item) { return item->name() == name; });
  REQUIRE(object);
  return object.data();
}

struct HelideRender
{
  explicit HelideRender(vsr::scene::Scene &scene);
  ~HelideRender();
  std::vector<std::uint8_t> pixels();
  VSR_NOT_COPYABLE(HelideRender)
  VSR_NOT_MOVEABLE(HelideRender)
  vsr::scene::Scene *scene{nullptr};
  anari::Library library{nullptr};
  anari::Device device{nullptr};
  anari::Renderer renderer{nullptr};
  vsr::rendering::RenderIndexAllLayers *index{nullptr};
};

HelideRender::HelideRender(vsr::scene::Scene &s) : scene(&s)
{
  library = anari::loadLibrary("helide", nullptr);
  REQUIRE(library);
  device = anari::newDevice(library, "default");
  REQUIRE(device);
  renderer = anari::newObject<anari::Renderer>(device, "default");
  anari::setParameter(
      device, renderer, "background", vsr::math::float4(0, 0, 0, 1));
  anari::commitParameters(device, renderer);
  index = scene->updateDelegate().emplace<vsr::rendering::RenderIndexAllLayers>(
      *scene, vsr::core::Token("archive-helide"), device);
  index->populate();
}

HelideRender::~HelideRender()
{
  scene->updateDelegate().erase(index);
  anari::release(device, renderer);
  anari::release(device, device);
  anari::unloadLibrary(library);
}

std::vector<std::uint8_t> HelideRender::pixels()
{
  auto camera =
      index->camera(named(scene->objectDB().camera, "archive camera")->index());
  anari::setParameter(device, camera, "aspect", 64.f / 48.f);
  anari::commitParameters(device, camera);
  auto frame = anari::newObject<anari::Frame>(device);
  anari::setParameter(device, frame, "size", vsr::math::uint2(64, 48));
  anari::setParameter(device, frame, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  anari::setParameter(device, frame, "world", index->world());
  anari::setParameter(device, frame, "renderer", renderer);
  anari::setParameter(device, frame, "camera", camera);
  anari::commitParameters(device, frame);
  anari::render(device, frame);
  anari::wait(device, frame);
  const auto mapped = anari::map<std::uint32_t>(device, frame, "channel.color");
  REQUIRE(mapped.data);
  REQUIRE(mapped.width == 64);
  REQUIRE(mapped.height == 48);
  const auto *bytes = reinterpret_cast<const std::uint8_t *>(mapped.data);
  std::vector<std::uint8_t> result(bytes, bytes + 64 * 48 * 4);
  anari::unmap(device, frame, "channel.color");
  anari::release(device, frame);
  return result;
}

std::size_t colored(
    const std::vector<std::uint8_t> &pixels, int begin, int end, int channel)
{
  std::size_t count = 0;
  for (int y = 0; y < 48; ++y)
    for (int x = begin; x < end; ++x) {
      const auto offset = (y * 64 + x) * 4;
      if (pixels[offset + channel] > pixels[offset + (1 - channel)] + 10
          && pixels[offset + channel] > pixels[offset + 2] + 10)
        ++count;
    }
  return count;
}

void populate(vsr::scene::Scene &scene)
{
  auto positions = scene.createArray(ANARI_FLOAT32_VEC3, 3);
  const float points[] = {-1, -1, 0, 1, -1, 0, 0, 1, 0};
  positions->setName("old shared positions");
  positions->setData(static_cast<const void *>(points));
  auto indices = scene.createArray(ANARI_UINT32_VEC3, 1);
  const std::uint32_t triangle[] = {0, 1, 2};
  indices->setData(static_cast<const void *>(triangle));
  auto uv = scene.createArray(ANARI_FLOAT32_VEC2, 3);
  const float coordinates[] = {0, 0, 1, 0, 0.5, 1};
  uv->setData(static_cast<const void *>(coordinates));
  auto geometry = scene.createObject<vsr::scene::Geometry>("triangle");
  geometry->setName("editable triangle");
  geometry->setParameterObject("vertex.position", *positions);
  geometry->setParameterObject("primitive.index", *indices);
  geometry->setParameterObject("vertex.attribute0", *uv);
  geometry->setParameterObject("vertex.attribute1", *uv);
  geometry->parameter("vertex.attribute1")->setEnabled(false);
  geometry->setParameterObject("vertex.attribute3", *uv);
  geometry->parameter("vertex.attribute3")->setEnabled(false);
  geometry->setParameter("custom", 17.f);
  geometry->setParameter("label", "ordinary scalar state");
  auto other = scene.createObject<vsr::scene::Geometry>("triangle");
  other->setName("unrelated old sharing");
  other->setParameterObject("vertex.position", *positions);
  other->setParameterObject("primitive.index", *indices);
  other->setParameterObject("vertex.attribute0", *uv);
  other->setParameterObject("vertex.attribute1", *uv);
  other->parameter("vertex.attribute1")->setEnabled(false);
  auto image = scene.createArray(ANARI_UFIXED8_VEC4, 2, 1);
  const std::uint8_t texels[] = {255, 0, 0, 255, 0, 255, 0, 255};
  image->setName("retained texture");
  image->setData(static_cast<const void *>(texels));
  auto sampler = scene.createObject<vsr::scene::Sampler>("image2D");
  sampler->setName("retained sampler");
  sampler->setParameterObject("image", *image);
  sampler->setParameter("inAttribute", "attribute0");
  sampler->setParameter("filter", "nearest");
  sampler->setParameter("wrapMode1", "clampToEdge");
  sampler->setParameter("wrapMode2", "clampToEdge");
  auto textured = scene.createObject<vsr::scene::Material>("matte");
  textured->setName("textured material");
  textured->setParameterObject("color", *sampler);
  textured->setParameter("opacity", 1.f);
  auto attributed = scene.createObject<vsr::scene::Material>("matte");
  attributed->setName("attribute material");
  attributed->setParameter("color", "color");
  attributed->setParameter("opacity", 1.f);
  const auto root = scene.defaultLayer()->root();
  for (int placement = 0; placement < 2; ++placement) {
    auto matrix = vsr::math::IDENTITY_MAT4;
    matrix[3].x = placement ? 1.5f : -1.5f;
    auto node = scene.insertChildTransformNode(
        root, matrix, placement ? "right" : "left");
    auto surface =
        scene.createSurface(placement ? "attribute surface" : "texture surface",
            geometry,
            placement ? attributed : textured);
    scene.insertChildObjectNode(
        node, surface, placement ? "right mesh" : "left mesh");
  }
  auto inactive = scene.addLayer("inactive original");
  auto otherSurface = scene.createSurface("unrelated surface", other, textured);
  scene.insertChildObjectNode(inactive->root(), otherSurface, "old mesh");
  scene.setLayerActive("inactive original", false);
  auto disabled = scene.insertChildNode(root, "disabled annotation");
  disabled->value().setEnabled(false);
  auto camera = scene.createObject<vsr::scene::Camera>("perspective");
  camera->setName("archive camera");
  camera->setParameter("position", vsr::math::float3(0, 0, 5));
  camera->setParameter("direction", vsr::math::float3(0, 0, -1));
  camera->setParameter("up", vsr::math::float3(0, 1, 0));
}

struct SavedField
{
  std::string name;
  anari::DataType type;
  std::array<std::size_t, 3> dimensions;
  bool enabled;
  std::vector<std::uint8_t> bytes;
};

std::vector<SavedField> meshFields(const vsr::scene::Geometry &geometry)
{
  std::vector<SavedField> fields;
  for (std::size_t i = 0; i < geometry.numParameters(); ++i) {
    const auto *name = geometry.parameterNameAt(i);
    const auto *array =
        geometry.parameterValueAsObject<vsr::scene::Array>(name);
    if (!array)
      continue;
    REQUIRE(array->isHost());
    REQUIRE_FALSE(array->isProxy());
    const auto *bytes = static_cast<const std::uint8_t *>(array->data());
    REQUIRE(bytes);
    fields.push_back({name,
        array->elementType(),
        {array->dim(0), array->dim(1), array->dim(2)},
        geometry.parameterAt(i).isEnabled(),
        {bytes, bytes + array->size() * array->elementSize()}});
  }
  return fields;
}

void checkMesh(const vsr::scene::Geometry &geometry,
    const std::vector<SavedField> &expected)
{
  const auto fields = meshFields(geometry);
  REQUIRE(fields.size() == expected.size());
  for (std::size_t i = 0; i < fields.size(); ++i) {
    INFO(fields[i].name);
    CHECK(fields[i].name == expected[i].name);
    CHECK(fields[i].type == expected[i].type);
    CHECK(fields[i].dimensions == expected[i].dimensions);
    CHECK(fields[i].enabled == expected[i].enabled);
    CHECK(fields[i].bytes == expected[i].bytes);
  }
}

void checkSurroundings(vsr::scene::Scene &scene, bool nonindexed)
{
  const auto &db = scene.objectDB();
  CHECK(scene.numberOfObjects(ANARI_GEOMETRY) == 2);
  CHECK(scene.numberOfObjects(ANARI_SURFACE) == 3);
  CHECK(scene.numberOfObjects(ANARI_SAMPLER) == 1);
  CHECK(scene.numberOfObjects(ANARI_ARRAY) == (nonindexed ? 13 : 14));
  auto *geometry = named(db.geometry, "editable triangle");
  auto *other = named(db.geometry, "unrelated old sharing");
  auto *textured = named(db.material, "textured material");
  auto *attributed = named(db.material, "attribute material");
  auto *sampler = named(db.sampler, "retained sampler");
  auto *image = named(db.array, "retained texture");
  CHECK(geometry->subtype() == vsr::scene::tokens::geometry::triangle);
  CHECK(geometry->parameter("custom")->value().get<float>() == 17.f);
  CHECK(geometry->parameter("label")->value().getString()
      == "ordinary scalar state");
  CHECK_FALSE(geometry->parameter("vertex.attribute3"));
  CHECK(bool(geometry->parameter("primitive.index")) == !nonindexed);
  CHECK(geometry->parameter("vertex.attribute0")->isEnabled());
  CHECK_FALSE(geometry->parameter("vertex.attribute1")->isEnabled());
  const auto binding = [geometry](const char *name) {
    return geometry->parameterValueAsObject<vsr::scene::Array>(name);
  };
  CHECK(binding("vertex.attribute0") == binding("vertex.attribute1"));
  CHECK(binding("vertex.attribute0") != binding("vertex.attribute2"));
  CHECK(binding("primitive.color") == binding("primitive.attribute0"));
  const float square[] = {
      -0.4f, -0.4f, 0, 0.4f, -0.4f, 0, 0.4f, 0.4f, 0, -0.4f, 0.4f, 0};
  const float expanded[] = {-0.4f,
      -0.4f,
      0,
      0.4f,
      -0.4f,
      0,
      0.4f,
      0.4f,
      0,
      -0.4f,
      -0.4f,
      0,
      0.4f,
      0.4f,
      0,
      -0.4f,
      0.4f,
      0};
  REQUIRE(binding("vertex.position"));
  CHECK(binding("vertex.position")->elementType() == ANARI_FLOAT32_VEC3);
  CHECK(binding("vertex.position")->size() == (nonindexed ? 6 : 4));
  CHECK(std::memcmp(binding("vertex.position")->data(),
            nonindexed ? expanded : square,
            nonindexed ? sizeof(expanded) : sizeof(square))
      == 0);
  if (!nonindexed) {
    const std::uint32_t triangles[] = {0, 1, 2, 0, 2, 3};
    REQUIRE(binding("primitive.index"));
    CHECK(binding("primitive.index")->elementType() == ANARI_UINT32_VEC3);
    CHECK(binding("primitive.index")->size() == 2);
    CHECK(std::memcmp(
              binding("primitive.index")->data(), triangles, sizeof(triangles))
        == 0);
  }
  const float colors[] = {1, 0, 0, 0, 1, 0};
  REQUIRE(binding("primitive.color"));
  CHECK(std::memcmp(binding("primitive.color")->data(), colors, sizeof(colors))
      == 0);
  REQUIRE(binding("primitive.attribute2"));
  CHECK(binding("primitive.attribute2")->elementType() == ANARI_UINT64);
  CHECK(binding("primitive.attribute2")->dataAs<std::uint64_t>()[0]
      == (UINT64_C(1) << 63) + 7);
  CHECK(binding("primitive.attribute2")->dataAs<std::uint64_t>()[1]
      == UINT64_MAX);
  CHECK(binding("vertex.tangent")->elementType() == ANARI_FLOAT64_VEC4);
  CHECK(binding("vertex.color")->elementType() == ANARI_UFIXED8_VEC4);
  CHECK(binding("faceVarying.attribute3")->elementType() == ANARI_INT16_VEC2);
  CHECK(binding("faceVarying.attribute3")->size() == 6);
  const auto *old =
      other->parameterValueAsObject<vsr::scene::Array>("vertex.position");
  REQUIRE(old);
  CHECK(old != binding("vertex.position"));
  CHECK(old->size() == 3);
  const float oldPoints[] = {-1, -1, 0, 1, -1, 0, 0, 1, 0};
  CHECK(std::memcmp(old->data(), oldPoints, sizeof(oldPoints)) == 0);
  CHECK(other->parameterValueAsObject<vsr::scene::Array>("vertex.attribute0")
      == other->parameterValueAsObject<vsr::scene::Array>("vertex.attribute1"));
  const float oldUv[] = {0, 0, 1, 0, 0.5, 1};
  CHECK(
      std::memcmp(
          other->parameterValueAsObject<vsr::scene::Array>("vertex.attribute0")
              ->data(),
          oldUv,
          sizeof(oldUv))
      == 0);
  const std::uint32_t oldTriangle[] = {0, 1, 2};
  CHECK(std::memcmp(
            other->parameterValueAsObject<vsr::scene::Array>("primitive.index")
                ->data(),
            oldTriangle,
            sizeof(oldTriangle))
      == 0);
  for (const auto *name : {"texture surface", "attribute surface"}) {
    auto *surface = named(db.surface, name);
    CHECK(surface->parameterValueAsObject<vsr::scene::Geometry>("geometry")
        == geometry);
    CHECK(surface->parameterValueAsObject<vsr::scene::Material>("material")
        == (std::string(name) == "texture surface" ? textured : attributed));
  }
  CHECK(textured->parameterValueAsObject<vsr::scene::Sampler>("color")
      == sampler);
  CHECK(attributed->parameter("color")->value().getString() == "color");
  CHECK(textured->parameter("opacity")->value().get<float>() == 1.f);
  CHECK(attributed->parameter("opacity")->value().get<float>() == 1.f);
  auto *otherSurface = named(db.surface, "unrelated surface");
  CHECK(otherSurface->parameterValueAsObject<vsr::scene::Geometry>("geometry")
      == other);
  CHECK(otherSurface->parameterValueAsObject<vsr::scene::Material>("material")
      == textured);
  CHECK(sampler->parameterValueAsObject<vsr::scene::Array>("image") == image);
  CHECK(sampler->parameter("inAttribute")->value().getString() == "attribute0");
  CHECK(sampler->parameter("filter")->value().getString() == "nearest");
  CHECK(image->elementType() == ANARI_UFIXED8_VEC4);
  CHECK(image->dim(0) == 2);
  CHECK(image->dim(1) == 1);
  CHECK(image->dim(2) == 1);
  const std::uint8_t texels[] = {255, 0, 0, 255, 0, 255, 0, 255};
  CHECK(std::memcmp(image->data(), texels, sizeof(texels)) == 0);
  REQUIRE(scene.layer("inactive original"));
  CHECK_FALSE(scene.layerIsActive("inactive original"));
  bool left = false, right = false, disabled = false;
  bool leftObject = false, rightObject = false;
  scene.defaultLayer()->traverse(scene.defaultLayer()->root(),
      [&](vsr::core::ForestNode<vsr::scene::LayerNodeData> &node, int) {
        const auto &value = node.value();
        if (value.name() == "left" || value.name() == "right") {
          auto matrix = vsr::math::IDENTITY_MAT4;
          const bool isLeft = value.name() == "left";
          matrix[3].x = isLeft ? -1.5f : 1.5f;
          CHECK(value.getTransform() == matrix);
          CHECK(value.isEnabled());
          (isLeft ? left : right) = true;
        } else if (value.name() == "disabled annotation") {
          CHECK_FALSE(value.isEnabled());
          disabled = true;
        } else if (value.name() == "left mesh"
            || value.name() == "right mesh") {
          const bool isLeft = value.name() == "left mesh";
          CHECK(value.getObject()
              == named(db.surface,
                  isLeft ? "texture surface" : "attribute surface"));
          REQUIRE(node.parent());
          CHECK(node.parent()->value().name() == (isLeft ? "left" : "right"));
          CHECK(value.isEnabled());
          (isLeft ? leftObject : rightObject) = true;
        }
        return true;
      });
  CHECK(left);
  CHECK(right);
  CHECK(disabled);
  CHECK(leftObject);
  CHECK(rightObject);
}

} // namespace

SCENARIO(
    "Imported meshes survive normal Scene Archive save/load without working files",
    "[integration][mcp][mesh-persistence]")
{
  const bool nonindexed = GENERATE(false, true);
  TestDirectory assets;
  const auto archivePath = assets.path + "/edited.vsr";
  const auto originalPath = assets.path + "/original.vsr";
  const auto sourcePath = assets.path + "/source.obj";
  const auto texturePath = assets.path + "/source.texture";
  std::ofstream(sourcePath)
      << "# original asset, never an exchange destination\n";
  std::ofstream(texturePath, std::ios::binary) << "original texture asset\n";
  const auto sourceBytes = fileBytes(sourcePath);
  const auto textureBytes = fileBytes(texturePath);
  std::vector<std::uint8_t> originalArchive, importedPixels;
  std::vector<SavedField> importedFields;
  std::string oldHandle;
  json::Value oldBatch;
  {
    ArchiveHost host;
    populate(host.scene);
    REQUIRE(vsr::io::save_SceneArchive(host.scene, originalPath.c_str()));
    originalArchive = fileBytes(originalPath);
    HelideRender render(host.scene);
    const auto before = render.pixels();
    REQUIRE(colored(before, 0, 32, 0) > 10);
    REQUIRE(colored(before, 0, 32, 1) > 10);
    oldHandle = host.inspectHandle(1, "editable triangle");
    REQUIRE(
        host.tool("vsr_mesh_export",
                "{\"request_id\":2,\"scene_id\":" + std::to_string(host.sceneId)
                    + ",\"handle\":" + json::quote(oldHandle) + "}")
            .text("kind")
        == "mesh_export");
    const auto exported = host.await(2);
    REQUIRE(exported.text("outcome") == "ok");
    const auto directory = host.exchangeDirectory();
    const auto command = std::string(VSR_MESH_TEST_PYTHON) + " "
        + VSR_MESH_TEST_SOURCE + "/scripts/test-mesh-persistence.py '"
        + directory + "/" + exported.result().get("file_name")->s + "' '"
        + directory + "/edited.vmesh' "
        + (nonindexed ? "nonindexed" : "indexed");
    REQUIRE(std::system(command.c_str()) == 0);
    const auto arguments =
        "{\"request_id\":3,\"scene_id\":" + std::to_string(host.sceneId)
        + ",\"handle\":" + json::quote(oldHandle) + ",\"target_version\":"
        + json::write(*exported.result().get("target_version"))
        + ",\"dependencies\":"
        + json::write(*exported.result().get("dependencies"))
        + ",\"file_name\":\"edited.vmesh\"}";
    REQUIRE(
        host.tool("vsr_mesh_import", arguments).text("kind") == "mesh_import");
    const auto imported = host.await(3);
    REQUIRE(imported.text("outcome") == "ok");
    REQUIRE(imported.committed());
    oldBatch = *imported.result().get("batch_ref");
    auto *geometry = named(host.scene.objectDB().geometry, "editable triangle");
    REQUIRE(
        geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
            ->size()
        == (nonindexed ? 6 : 4));
    importedFields = meshFields(*geometry);
    checkSurroundings(host.scene, nonindexed);
    importedPixels = render.pixels();
    CHECK(importedPixels != before);
    for (int begin : {0, 32}) {
      REQUIRE(colored(importedPixels, begin, begin + 32, 0) > 0);
      REQUIRE(colored(importedPixels, begin, begin + 32, 1) > 0);
    }
    CHECK(colored(importedPixels, 0, 32, 0) + colored(importedPixels, 0, 32, 1)
        < colored(before, 0, 32, 0) + colored(before, 0, 32, 1));
    REQUIRE_FALSE(fs::exists(archivePath));
    CHECK(fileBytes(originalPath) == originalArchive);
    for (const auto &entry : fs::directory_iterator(directory))
      REQUIRE(fs::remove(entry.path()));
    REQUIRE(fs::is_empty(directory));
    CHECK(render.pixels() == importedPixels);
    // Ordinary persistence, NOT Mesh Export, Object Archive or proxy carriers.
    REQUIRE(vsr::io::save_SceneArchive(host.scene,
        archivePath.c_str(),
        vsr::io::ArrayDataPolicy::IncludeData));
  }
  // The exporting host, device, launch authority, undo and its directory are
  // gone.
  ArchiveHost loaded;
  REQUIRE(vsr::io::load_SceneArchive(loaded.scene, archivePath.c_str()));
  auto *geometry = named(loaded.scene.objectDB().geometry, "editable triangle");
  checkMesh(*geometry, importedFields);
  checkSurroundings(loaded.scene, nonindexed);
  HelideRender loadedRender(loaded.scene);
  CHECK(loadedRender.pixels() == importedPixels);
  const auto handle = loaded.inspectHandle(1, "editable triangle");
  CHECK(handle != oldHandle);
  REQUIRE(
      loaded
          .tool("vsr_restore",
              "{\"request_id\":2,\"scene_id\":" + std::to_string(loaded.sceneId)
                  + ",\"target_batch_ref\":" + json::write(oldBatch) + "}")
          .text("kind")
      == "restore");
  const auto denied = loaded.await(2);
  // Prior-launch recovery authority is not reconstructed by loading Scene data.
  CHECK(denied.text("outcome") == "outcome_unavailable");
  CHECK_FALSE(denied.committed());
  REQUIRE(
      loaded
          .tool("vsr_mesh_export",
              "{\"request_id\":3,\"scene_id\":" + std::to_string(loaded.sceneId)
                  + ",\"handle\":" + json::quote(oldHandle) + "}")
          .text("kind")
      == "mesh_export");
  const auto foreignHandle = loaded.await(3);
  CHECK(foreignHandle.text("outcome") == "invalid_request");
  CHECK_FALSE(foreignHandle.committed());
  checkMesh(*geometry, importedFields);
  CHECK(loadedRender.pixels() == importedPixels);
  CHECK(fileBytes(sourcePath) == sourceBytes);
  CHECK(fileBytes(texturePath) == textureBytes);
  CHECK(fileBytes(originalPath) == originalArchive);
  CHECK(fs::is_empty(loaded.exchangeDirectory()));
}

#endif

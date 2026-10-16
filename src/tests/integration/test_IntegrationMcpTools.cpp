// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// Thin loopback checks for the discovery/inspection and lookup/cancel MCP
// tools: happy path, code mapping, capability policy and cross-session
// gates. Business rules are covered by the headless suites. Assertions read
// only machine-readable fields, never English messages.

#if defined(VSR_USE_INTEGRATION_MCP) && defined(__linux__)

// catch
#include "ChessFixture.hpp"
#include "catch.hpp"
#include "vsr/integration/context/ContextAdapter.hpp"
// vsr
#include <SDL3/SDL.h>
#include "vsr/app/AgentOptions.hpp"
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/activity/Activity.hpp"
#include "vsr/integration/mcp/McpServer.hpp"
#include "vsr/integration/mcp/tools/ContentTools.hpp"
#include "vsr/integration/mcp/tools/EditTools.hpp"
#include "vsr/integration/mcp/tools/FrameTools.hpp"
#include "vsr/integration/mcp/tools/ImageTools.hpp"
#include "vsr/integration/mcp/tools/LookupTools.hpp"
#include "vsr/integration/mcp/tools/QueryTools.hpp"
#include "vsr/integration/mcp/tools/RenderTools.hpp"
#include "vsr/integration/render/ControllerCamera.hpp"
#include "vsr/io/archives/SceneArchive.hpp"
#include "vsr/io/images/ImageCache.hpp"
#include "vsr/rendering/index/RenderIndexAllLayers.hpp"
#include "vsr/rendering/pipeline/ImagePipeline.h"
#include "vsr/rendering/view/ManipulatorToVSR.hpp"
#include "vsr/scene/Scene.hpp"
#ifdef VSR_TEST_VIEWER_UI
#include "vsr/ui/imgui/windows/IntegrationActivityModel.h"
#endif
// anari
#include <anari/anari_cpp.hpp>
// stb
#include <stb_image.h>
// zlib
#include <zlib.h>
// std
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
// posix
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>

// Mock only the OS entropy boundary; all Image services remain real.
static std::atomic<bool> fixedExportEntropy{false};
extern "C" ssize_t getrandom(void *buffer, size_t bytes, unsigned flags)
{
  if (fixedExportEntropy.load()) {
    std::memset(buffer, 0, bytes);
    return ssize_t(bytes);
  }
  return ::syscall(SYS_getrandom, buffer, bytes, flags);
}

// A deterministic filesystem-race seam after a real export write. Socket
// writes and descriptor publication are untouched; no service is substituted.
static std::atomic<bool> interceptExportWrite{false};
static std::function<void(int)> afterExportWrite;
extern "C" ssize_t write(int fd, const void *buffer, size_t bytes)
{
  const auto n = ::syscall(SYS_write, fd, buffer, bytes);
  if (n > 0 && interceptExportWrite.load()) {
    struct stat st{};
    char path[4096]{};
    const auto link = "/proc/self/fd/" + std::to_string(fd);
    const auto length = ::readlink(link.c_str(), path, sizeof(path) - 1);
    if (::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && length > 0
        && (std::string(path).find("/image-") != std::string::npos
            || std::string(path).find("/mesh-") != std::string::npos)
        && interceptExportWrite.exchange(false))
      afterExportWrite(fd);
  }
  return n;
}

static std::atomic<bool> interceptImportRead{false};
static std::function<void(int)> afterImportRead;
extern "C" ssize_t read(int fd, void *buffer, size_t bytes)
{
  const auto count = ::syscall(SYS_read, fd, buffer, bytes);
  if (count > 0 && interceptImportRead.load()) {
    char path[4096]{};
    const auto link = "/proc/self/fd/" + std::to_string(fd);
    const auto length = ::readlink(link.c_str(), path, sizeof(path) - 1);
    if (length > 0
        && (std::string(path).find("/agent.png") != std::string::npos
            || std::string(path).find("/edited.vmesh") != std::string::npos)
        && interceptImportRead.exchange(false))
      afterImportRead(fd);
  }
  return count;
}

static std::atomic<bool> failImageDirectoryOpen{false};
static std::atomic<bool> failLaunchDirectoryOpen{false};
static std::atomic<bool> interceptImageDirectoryCreate{false};
static std::function<void()> afterImageDirectoryCreate;
extern "C" int openat(int parent, const char *name, int flags, ...)
{
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list arguments;
    va_start(arguments, flags);
    mode = va_arg(arguments, mode_t);
    va_end(arguments);
  }
  if (failImageDirectoryOpen.load() && !std::strcmp(name, "images")
      && (flags & O_DIRECTORY) && failImageDirectoryOpen.exchange(false)) {
    errno = EIO;
    return -1;
  }
  if (failLaunchDirectoryOpen.load() && std::strlen(name) == 32
      && (flags & O_DIRECTORY) && failLaunchDirectoryOpen.exchange(false)) {
    errno = EIO;
    return -1;
  }
  return int(::syscall(SYS_openat, parent, name, flags, mode));
}
extern "C" int mkdirat(int parent, const char *name, mode_t mode)
{
  const auto result = int(::syscall(SYS_mkdirat, parent, name, mode));
  if (result == 0 && interceptImageDirectoryCreate.load()
      && !std::strcmp(name, "images")
      && interceptImageDirectoryCreate.exchange(false))
    afterImageDirectoryCreate();
  return result;
}

using namespace vsr::integration;
using operations::Outcome;
namespace json = mcp::json;

namespace {

struct TempDir
{
  std::string path;
  TempDir()
  {
    const char *tmp = std::getenv("TMPDIR");
    path = std::string(tmp ? tmp : "/tmp") + "/vsr-mcp-tools-XXXXXX";
    REQUIRE(::mkdtemp(path.data()) != nullptr);
  }
  ~TempDir()
  {
    std::string cmd = "rm -rf '" + path + "'";
    (void)std::system(cmd.c_str());
  }
};

struct Reply
{
  int status{0};
  json::Value json;

  std::string errorCode() const
  {
    const auto *e = json.get("error");
    const auto *d = e ? e->get("data") : nullptr;
    const auto *c = d ? d->get("code") : nullptr;
    return c && c->isString() ? c->s : std::string();
  }
  const json::Value *sc() const
  {
    const auto *r = json.get("result");
    return r ? r->get("structuredContent") : nullptr;
  }
  std::string str(const char *key) const
  {
    const auto *v = sc() ? sc()->get(key) : nullptr;
    return v && v->isString() ? v->s : std::string();
  }
  bool flag(const char *key) const
  {
    const auto *v = sc() ? sc()->get(key) : nullptr;
    return v && v->type == json::Value::Type::BOOL && v->b;
  }
  bool isError() const
  {
    const auto *r = json.get("result");
    const auto *v = r ? r->get("isError") : nullptr;
    return v && v->b;
  }
  const json::Value *result() const
  {
    return sc() ? sc()->get("result") : nullptr;
  }
};

// Receive bounds for the harness: a stalled or runaway server fails the
// test instead of hanging it or exhausting memory.
constexpr int kReceiveDeadlineMs = 10000;
constexpr std::size_t kMaxReplyBytes = 64u << 20;

Reply post(std::uint16_t port,
    const std::vector<std::string> &headers,
    const std::string &body)
{
  Reply r;
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  REQUIRE(fd >= 0);
  struct Closer
  {
    int fd;
    ~Closer()
    {
      ::close(fd);
    }
  } closer{fd};
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.sin_port = htons(port);
  REQUIRE(::connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0);
  std::string req = "POST /mcp HTTP/1.1\r\n";
  for (const auto &h : headers)
    req += h + "\r\n";
  req += "Content-Type: application/json\r\nConnection: close\r\n";
  req += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
  ::send(fd, req.data(), req.size(), MSG_NOSIGNAL);
  std::string in;
  char buf[4096];
  const auto deadline = std::chrono::steady_clock::now()
      + std::chrono::milliseconds(kReceiveDeadlineMs);
  for (;;) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now())
                          .count();
    INFO("no complete reply within " << kReceiveDeadlineMs << " ms ("
                                     << in.size() << " bytes received)");
    REQUIRE(left > 0);
    pollfd p{fd, POLLIN, 0};
    const int ready = ::poll(&p, 1, int(left));
    if (ready < 0 && errno == EINTR)
      continue;
    REQUIRE(ready >= 0);
    if (ready == 0)
      continue; // re-checks the deadline above
    const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      break;
    in.append(buf, std::size_t(n));
    INFO("reply exceeds " << kMaxReplyBytes << " bytes");
    REQUIRE(in.size() <= kMaxReplyBytes);
  }
  if (in.size() > 12)
    r.status = std::atoi(in.c_str() + 9);
  const auto split = in.find("\r\n\r\n");
  if (split != std::string::npos)
    json::parse(in.substr(split + 4), r.json);
  return r;
}

bool confirmReady(SceneId)
{
  return true;
}

struct ImageAllocator : std::pmr::memory_resource
{
  std::size_t remaining{0};
  std::atomic<std::size_t> deallocations{0};

 protected:
  void *do_allocate(std::size_t bytes, std::size_t alignment) override;
  void do_deallocate(
      void *p, std::size_t bytes, std::size_t alignment) override;
  bool do_is_equal(
      const std::pmr::memory_resource &other) const noexcept override;
};

void *ImageAllocator::do_allocate(std::size_t bytes, std::size_t alignment)
{
  if (!remaining)
    throw std::bad_alloc();
  --remaining;
  return std::pmr::new_delete_resource()->allocate(bytes, alignment);
}
void ImageAllocator::do_deallocate(
    void *p, std::size_t bytes, std::size_t alignment)
{
  std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
  ++deallocations;
}
bool ImageAllocator::do_is_equal(
    const std::pmr::memory_resource &other) const noexcept
{
  return this == &other;
}

struct WriteFaultObserver : vsr::scene::EmptyUpdateDelegate
{
  void signalArrayMapped(const vsr::scene::Array *) override;
  void signalArrayUnmapped(const vsr::scene::Array *) override;
  bool failMap{true};
  bool allocationFailure{false};
  bool failUnmap{false};
  std::function<void()> afterBurn;
};

void WriteFaultObserver::signalArrayMapped(const vsr::scene::Array *)
{
  if (afterBurn)
    afterBurn();
  if (failMap) {
    if (allocationFailure)
      throw std::bad_alloc();
    throw std::runtime_error("downstream map failure");
  }
}

void WriteFaultObserver::signalArrayUnmapped(const vsr::scene::Array *)
{
  if (failUnmap)
    throw std::runtime_error("downstream unmap failure");
}

struct Session
{
  std::string id;
  std::string secret;
};

std::string ref(const std::string &sid, std::uint64_t id)
{
  return "{\"session_id\":\"" + sid + "\",\"request_id\":" + std::to_string(id)
      + "}";
}

template <typename R>
std::string token(const R &r)
{
  return std::to_string(r.launch.activation) + "."
      + std::to_string(r.launch.nonce) + "." + std::to_string(r.domain) + "."
      + std::to_string(r.value);
}

// Fault injection is an externally owned test service. The production creator
// receives only the shared service/dispatch pair, never copies this opaque
// target. Existing Image/Mesh fixture authority remains unchanged.
struct HdriTestReservation
{
  std::function<std::shared_ptr<const void>(size_t)> callback;
  std::shared_ptr<const void> reserve(size_t bytes) const;
};

std::shared_ptr<const void> HdriTestReservation::reserve(size_t bytes) const
{
  return callback(bytes);
}

struct Fixture
{
  vsr::scene::Scene scene;
  vsr::scene::Scene hidden;
  Integration integration;
  identity::IdentityAuthority ids;
  std::unique_ptr<identity::SceneIdentity> sceneIds;
  SceneId sceneId{};
  SceneId hiddenId{};
  double now{1000.0};
  std::unique_ptr<session::SessionAuthority> authority;
  std::unique_ptr<activity::ActivityLog> imageActivity;
  std::unique_ptr<activity::EngineActivitySink> imageActivitySink;
  std::unique_ptr<operations::OperationEngine> engine;
  std::unique_ptr<inspection::Inspector> inspector;
  std::unique_ptr<edit::EditService> edits;
  std::unique_ptr<edit::TranslationService> translations;
  std::unique_ptr<restore::RestoreService> restores;
  std::unique_ptr<image::ImageExchange> images;
  std::unique_ptr<content::ContentCreation> creator;
  bool hookOk{true};
  TempDir tmp;
  descriptor::RuntimeDirectory dir;
  std::unique_ptr<mcp::McpServer> server;

  explicit Fixture(inspection::PageLimits limits = {},
      bool editable = false,
      bool nodeEdits = false,
      bool preview = false,
      double previewLifetime = 120.,
      bool imageWrites = false,
      std::uint64_t writeLimit = UINT64_MAX,
      image::ExchangeLimits exchangeLimits = {},
      bool imageImports = false,
      operations::Limits engineLimits = {},
      bool contentCreation = false)
      : integration(DEFAULT_QUEUE_CAPACITY, writeLimit)
  {
    for (int i = 0; i < 5; i++)
      scene.createObject<vsr::scene::Material>("matte")->setParameter(
          "opacity", 1.f);
    SceneRegistration s;
    s.name = "scene";
    s.scene = &scene;
    s.grants.read = true;
    s.nodeEdits = nodeEdits;
    s.serializedArrayWrites = imageWrites;
    if (editable) {
      s.grants.edit = true;
      s.normalSyncHook = [this](SceneId) { return hookOk; };
    }
    sceneId = *integration.registerScene(s);
    SceneRegistration h; // no grants: not exposed to the agent
    h.name = "hidden";
    h.scene = &hidden;
    if (imageImports) {
      h.grants.read = true;
      h.grants.edit = true;
      h.serializedArrayWrites = true;
      h.normalSyncHook = [this](SceneId) { return hookOk; };
    }
    hiddenId = *integration.registerScene(h);
    REQUIRE(integration.activate().ok());
    sceneIds = std::make_unique<identity::SceneIdentity>(ids, scene);
    authority =
        session::SessionAuthority::create(integration, [this] { return now; });
    engine = operations::OperationEngine::create(
        integration, *authority, engineLimits, [this] { return now; });
    if (imageImports) {
      // This fixture is a qualified isolated host, not a physical-lease
      // bypass. Explicit caller-supplied pools remain shared and unchanged.
      if (!exchangeLimits.reservePhysicalMemory) {
        const auto physical = std::make_shared<vsr::rendering::FrameBudget>(
            exchangeLimits.physicalByteCap);
        exchangeLimits.reservePhysicalMemory = [physical](std::size_t bytes) {
          return physical->reserve(bytes);
        };
      }
      activity::Limits activityLimits;
      activityLimits.retentionBytes = exchangeLimits.globalRetentionBytes;
      activityLimits.hardRetentionBytes = exchangeLimits.hardRetentionBytes;
      imageActivity = std::make_unique<activity::ActivityLog>(
          activityLimits, [this] { return now; });
      imageActivitySink = std::make_unique<activity::EngineActivitySink>(
          *imageActivity, integration);
      engine->setActivitySink(imageActivitySink.get());
      exchangeLimits.retention = imageActivity.get();
      exchangeLimits.restorationRetained = [this](const std::string &launch,
                                               const std::string &batch) {
        return imageActivity->eligibility({launch, batch})
            == activity::Eligibility::ELIGIBLE;
      };
    }
    inspector = std::make_unique<inspection::Inspector>(
        integration,
        *authority,
        [this](SceneId id) { return id == sceneId ? sceneIds.get() : nullptr; },
        limits);
    REQUIRE(descriptor::openDirectory(tmp.path, {}, dir).ok());
    edits = std::make_unique<edit::EditService>(
        integration, *engine, authority->launchId());
    edits->attachScene(sceneId, *sceneIds);
    if (preview) {
      edit::PreviewLimits previewLimits;
      previewLimits.lifetimeSeconds = previewLifetime;
      translations = std::make_unique<edit::TranslationService>(
          integration, *authority, *engine, *edits, previewLimits, [&] {
            return now;
          });
      REQUIRE(translations->attachScene(sceneId, *sceneIds));
    }
    operations::ResultOwnership::Reserve restoreReservation;
    if (imageImports)
      restoreReservation = [reserve = exchangeLimits.reservePhysicalMemory] {
        return reserve(65536);
      };
    restores = std::make_unique<restore::RestoreService>(
        *engine, *edits, std::move(restoreReservation));
    restores->attachScene(sceneId, *sceneIds);
    images = std::make_unique<image::ImageExchange>(
        integration, *engine, authority->launchId(), dir, exchangeLimits);
    images->attachScene(sceneId, *sceneIds);
    if (imageImports)
      images->attachRestoration(*edits, *restores);
    inspector->setImageExchange(
        [this](SceneId id, const std::string &sessionId) {
          return images->metadata(id, sessionId);
        },
        [this](SceneId id) { return images->unavailableReason(id); });

    mcp::Config cfg;
    cfg.imageExchange = images->metadata(sceneId);
    mcp::tools::addImageTools(cfg.tools, {ids, *images});
    if (contentCreation) {
      REQUIRE(imageImports);
      auto hdriAuthority = std::make_shared<HdriTestReservation>();
      hdriAuthority->callback = std::move(exchangeLimits.reservePhysicalMemory);
      creator = std::make_unique<content::ContentCreation>(integration,
          *engine,
          *edits,
          *imageActivity,
          operations::Reservation::from(std::move(hdriAuthority)));
      creator->attachScene(sceneId, *sceneIds);
      REQUIRE(creator->assetRoots().approve(tmp.path));
      mcp::tools::addContentTools(cfg.tools, ids, *creator, *engine);
    }
    mcp::tools::addQueryTools(cfg.tools, {*engine, *inspector});
    mcp::tools::addLookupTools(cfg.tools, *engine);
    mcp::tools::addEditTools(
        cfg.tools, {ids, *edits, *restores, translations.get()});
    const auto st =
        mcp::McpServer::start(cfg, *authority, *engine, dir, server);
    if (!st.ok())
      FAIL("start failed: " << st.reason);
  }

  Reply await(const Session &s, std::uint64_t id)
  {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
      drain();
      auto reply = status(s, s.id, id);
      if (reply.str("state") == "terminal")
        return reply;
      REQUIRE(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  std::string exchangePath()
  {
    json::Value metadata;
    REQUIRE(json::parse(images->metadata(sceneId), metadata));
    REQUIRE(metadata.get("enabled")->b);
    return metadata.get("image_exchange_directory")->s;
  }

  std::string objectHandle(const vsr::scene::Object *object);
  Reply exportMesh(
      const Session &s, std::uint64_t id, const vsr::scene::Geometry *geometry);

  Reply exportImage(
      const Session &s, std::uint64_t id, const std::string &handle)
  {
    return tool(s,
        "vsr_image_export",
        "{\"request_id\":" + std::to_string(id) + ",\"scene_id\":"
            + std::to_string(sceneId) + ",\"handle\":\"" + handle + "\"}");
  }

  Reply importImage(const Session &s,
      std::uint64_t id,
      const std::string &handle,
      const std::string &version,
      const std::string &file)
  {
    return tool(s,
        "vsr_image_import",
        "{\"request_id\":" + std::to_string(id) + ",\"scene_id\":"
            + std::to_string(sceneId) + ",\"handle\":" + json::quote(handle)
            + ",\"data_version\":" + json::quote(version)
            + ",\"file_name\":" + json::quote(file) + "}");
  }

  Reply inspectArray(const Session &s,
      std::uint64_t id,
      const std::string &name = "tracked-image")
  {
    tool(s,
        "vsr_inspect",
        "{\"request_id\":" + std::to_string(id) + ",\"scene_id\":"
            + std::to_string(sceneId) + ",\"name\":\"" + name + "\"}");
    drain();
    return status(s, s.id, id);
  }

  Reply rpc(const std::string &method,
      const std::string &params,
      const Session *s = nullptr)
  {
    std::vector<std::string> h{"Host: " + server->authority(),
        "Authorization: Bearer " + authority->launchCredential()};
    if (s) {
      h.push_back("VSR-Session-ID: " + s->id);
      h.push_back("VSR-Session-Proof: Bearer " + s->secret);
    }
    return post(server->port(),
        h,
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"" + method
            + "\",\"params\":" + params + "}");
  }

  Reply tool(const Session &s, const std::string &name, const std::string &args)
  {
    return rpc("tools/call",
        "{\"name\":\"" + name + "\",\"arguments\":" + args + "}",
        &s);
  }

  Session attach()
  {
    auto r = rpc("vsr/attach", "{}");
    REQUIRE(r.json.get("result"));
    return {r.json.get("result")->get("session_id")->s,
        r.json.get("result")->get("session_secret")->s};
  }

  void drain()
  {
    for (;;) {
      images->pump();
      if (creator)
        creator->pump();
      if (integration.service() == 0)
        break;
    }
  }

  Reply status(const Session &s, const std::string &sid, std::uint64_t id)
  {
    return tool(
        s, "vsr_operation_status", "{\"operation_ref\":" + ref(sid, id) + "}");
  }

  // First inspected object as {"handle":…,"version":…} JSON members.
  std::string firstTarget(const Session &s, std::uint64_t id)
  {
    tool(s,
        "vsr_inspect",
        "{\"request_id\":" + std::to_string(id)
            + ",\"scene_id\":" + std::to_string(sceneId) + "}");
    drain();
    auto st = status(s, s.id, id);
    REQUIRE(st.str("outcome") == "ok");
    const auto &item = st.result()->get("items")->a.at(0);
    return "\"handle\":\"" + item.get("handle")->s + "\",\"version\":"
        + std::to_string(std::uint64_t(item.get("version")->n));
  }

  Reply editOpacity(const Session &s,
      std::uint64_t id,
      const std::string &target,
      double value)
  {
    return tool(s,
        "vsr_edit",
        "{\"request_id\":" + std::to_string(id) + ",\"scene_id\":"
            + std::to_string(sceneId) + ",\"edits\":[{" + target
            + ",\"parameter\":\"opacity\",\"value\":" + std::to_string(value)
            + "}]}");
  }

  Reply restoreBatch(const Session &s, std::uint64_t id, const json::Value &b)
  {
    return tool(s,
        "vsr_restore",
        "{\"request_id\":" + std::to_string(id)
            + ",\"scene_id\":" + std::to_string(sceneId)
            + ",\"target_batch_ref\":" + json::write(b) + "}");
  }

  float opacity0()
  {
    return scene.getObject<vsr::scene::Material>(0)
        ->parameter("opacity")
        ->value()
        .get<float>();
  }
};

std::string Fixture::objectHandle(const vsr::scene::Object *object)
{
  const auto handle = sceneIds->handleOf(object);
  char text[33];
  std::snprintf(text,
      sizeof(text),
      "%016llx%016llx",
      (unsigned long long)handle.domain,
      (unsigned long long)handle.serial);
  return text;
}

Reply Fixture::exportMesh(
    const Session &s, std::uint64_t id, const vsr::scene::Geometry *geometry)
{
  return tool(s,
      "vsr_mesh_export",
      "{\"request_id\":" + std::to_string(id)
          + ",\"scene_id\":" + std::to_string(sceneId)
          + ",\"handle\":" + json::quote(objectHandle(geometry)) + "}");
}

struct MeshEntropyScope
{
  MeshEntropyScope();
  ~MeshEntropyScope();
};

MeshEntropyScope::MeshEntropyScope()
{
  fixedExportEntropy = true;
}

MeshEntropyScope::~MeshEntropyScope()
{
  fixedExportEntropy = false;
}

} // namespace

SCENARIO("MCP effective binding observation polls and replays over loopback",
    "[integration][view-info]")
{
  Fixture f;
  identity::ViewRevisionTracker tracker(f.ids, *f.sceneIds);
  f.inspector->setViewRevisions([&](ViewId) { return &tracker; });
  EffectiveViewBinding binding;
  binding.camera.position = {7., 8., 9.};
  binding.width = 1280;
  binding.height = 720;
  binding.deviceIdentifier = "default@helide";
  binding.rendererSubtype = "default";
  binding.rendererConfigurationRevision = "r1";
  binding.rendererSettings = {{"samples", {1.}}};
  binding.pipelineSource = "standalone-agent";
  binding.pipelineConfigurationRevision = "p1";
  ViewId view{};
  ViewId otherView{};
  bool observed = false;
  f.integration.submit(f.sceneId, [&](vsr::scene::Scene &) {
    ViewRegistration v{"agent", f.sceneId, {true, true}};
    v.role = ViewRole::INDEPENDENT_AGENT;
    v.capturePolicy.maxAxis = 1024;
    v.capturePolicy.maxPixels = 786432;
    v.capturePolicy.qualityUnits = {"completed_frames"};
    v.observeBinding = [&] {
      REQUIRE(f.integration.inServiceCall());
      observed = true;
      return std::optional<EffectiveViewBinding>(binding);
    };
    view = *f.integration.registerView(v);
    otherView = *f.integration.registerView({"other", f.sceneId, {true, true}});
  });
  f.drain();
  auto s = f.attach();
  const auto listedTools = json::write(f.rpc("tools/list", "{}").json);
  CHECK(listedTools.find("vsr_view_info") != std::string::npos);
  CHECK(listedTools.find("vsr_capture\"") == std::string::npos);
  CHECK(listedTools.find("Host-declared quality units") != std::string::npos);
  CHECK(listedTools.find("Roles are descriptive") != std::string::npos);
  const std::string args =
      "{\"request_id\":1,\"view_id\":" + std::to_string(view) + "}";
  auto ack = f.tool(s, "vsr_view_info", args);
  REQUIRE(ack.str("state") == "pending");
  CHECK_FALSE(observed);
  CHECK(f.status(s, s.id, 1).str("progress") == "queued");
  f.drain();
  auto complete = f.status(s, s.id, 1);
  REQUIRE(complete.str("outcome") == "ok");
  const auto *result = complete.result();
  REQUIRE(result);
  CHECK(result->get("camera")->get("position")->a[0].n == 7.);
  CHECK(result->get("camera")->get("fovy_deg")->n == 60.);
  CHECK(result->get("renderer_settings")->a[0].get("numbers")->a[0].n == 1.);
  CHECK(result->get("device_private_state")->get("state")->s == "unknown");
  CHECK_FALSE(result->get("capture_policy")->get("exposed")->b);
  CHECK(result->get("view_id")->n == view);
  CHECK(result->get("role")->s == "independent_agent");
  CHECK(result->get("name")->s == "agent");
  CHECK(result->get("capture_policy")->get("max_axis")->n == 1024);
  CHECK(result->get("capture_policy")->get("quality_units")->a[0].s
      == "completed_frames");
  CHECK(result->get("scene_id")->n == f.sceneId);
  CHECK(result->get("width")->n == 1280);
  CHECK(result->get("height")->n == 720);
  CHECK(result->get("device_identifier")->s == "default@helide");
  CHECK(result->get("renderer_subtype")->s == "default");
  CHECK(result->get("pipeline_source")->s == "standalone-agent");
  CHECK(result->get("capture_policy_revision")->s
      != result->get("view_revision")->s);
  CHECK(result->get("scene_revision")->s != result->get("view_revision")->s);
  CHECK(result->get("capture_policy")->get("max_png_bytes")->n == 8388608);
  CHECK(
      result->get("capture_policy")->get("host_budget_bytes")->n == 268435456);
  const auto original = json::write(*result);
  CHECK(f.tool(s,
             "vsr_view_info",
             "{\"request_id\":1,\"view_id\":" + std::to_string(otherView) + "}")
            .errorCode()
      == "request_id_conflict");
  binding.camera.position[0] = 10.;
  CHECK(f.tool(s, "vsr_view_info", args).flag("replayed"));
  f.drain();
  CHECK(json::write(*f.status(s, s.id, 1).result()) == original);
  CHECK(f.tool(s, "vsr_view_info", "{\"request_id\":1,\"view_id\":999}")
            .errorCode()
      == "invalid_request");
  f.tool(s,
      "vsr_view_info",
      "{\"request_id\":2,\"view_id\":" + std::to_string(view) + "}");
  f.drain();
  auto changed = f.status(s, s.id, 2);
  REQUIRE(changed.str("outcome") == "ok");
  CHECK(changed.result()->get("camera")->get("position")->a[0].n == 10.);
  CHECK(changed.result()->get("view_revision")->s
      != result->get("view_revision")->s);
  binding.pipelineSource = std::string(5000, 'x');
  f.tool(s,
      "vsr_view_info",
      "{\"request_id\":3,\"view_id\":" + std::to_string(view) + "}");
  f.drain();
  auto oversized = f.status(s, s.id, 3);
  REQUIRE(oversized.str("outcome") == "ok");
  CHECK(oversized.result()->get("reason")->s == "representation_unavailable");
  CHECK_FALSE(oversized.result()->get("camera"));
  f.integration.submit(f.sceneId, [&](vsr::scene::Scene &) {
    CapturePolicy policy;
    policy.qualityUnits = {"samples", "completed_frames"};
    CHECK(f.integration.updateCapturePolicy(view, policy));
  });
  f.drain();
  f.tool(s, "vsr_discover", "{\"request_id\":10}");
  f.drain();
  auto discovery = f.status(s, s.id, 10);
  REQUIRE(discovery.str("outcome") == "ok");
  bool advertised = false;
  for (const auto &item : discovery.result()->get("items")->a) {
    if (item.get("kind")->s != "view")
      continue;
    CHECK(item.get("binding_observation")->s == "vsr_view_info");
    CHECK_FALSE(item.get("capture_policy")->get("exposed")->b);
    if (item.get("id")->n == view) {
      CHECK(item.get("role")->s == "independent_agent");
      CHECK(item.get("capture_policy")->get("max_axis")->n == 2048);
      CHECK(item.get("capture_policy")->get("quality_units")->a.size() == 2);
      CHECK(item.get("capture_policy_revision")->s
          != result->get("capture_policy_revision")->s);
      CHECK(item.get("effective_binding")->get("reason")->s
          == "representation_unavailable");
    }
    advertised = true;
  }
  CHECK(advertised);
  f.authority->restrict(
      session::Target::scene(f.sceneId), session::Capability::READ);
  CHECK_FALSE(f.status(s, s.id, 1).result());
  CHECK(f.tool(s,
             "vsr_view_info",
             "{\"request_id\":4,\"view_id\":" + std::to_string(view) + "}")
            .errorCode()
      == "forbidden");
  f.authority->relax(
      session::Target::scene(f.sceneId), session::Capability::READ);
  f.authority->restrict(session::Target::view(view), session::Capability::READ);
  CHECK(f.tool(s,
             "vsr_view_info",
             "{\"request_id\":4,\"view_id\":" + std::to_string(view) + "}")
            .errorCode()
      == "forbidden");
}

SCENARIO("MCP bounds admits only default-visible missing observations",
    "[integration][bounds-mcp]")
{
  Fixture f;
  const auto s = f.attach();
  const auto listed = f.rpc("tools/list", "{}");
  const auto *tools = listed.json.get("result")->get("tools");
  REQUIRE(tools);
  const json::Value *schema = nullptr;
  for (const auto &tool : tools->a)
    if (tool.get("name")->s == "vsr_bounds")
      schema = tool.get("inputSchema");
  REQUIRE(schema);
  CHECK_FALSE(
      schema->get("properties")->get("include_disabled")->get("default")->b);
  CHECK_FALSE(schema->get("additionalProperties")->b);
  const auto suffix = ",\"scene_id\":" + std::to_string(f.sceneId);
  const auto args = "{\"request_id\":1" + suffix + "}";
  for (const auto *choice : {",\"include_disabled\":true",
           ",\"node_handle\":\"\"",
           ",\"node_handle\":null",
           ",\"include_disabled\":0",
           ",\"deadline_seconds\":999"})
    CHECK(f.tool(s, "vsr_bounds", "{\"request_id\":1" + suffix + choice + "}")
              .errorCode()
        == "invalid_request");
  REQUIRE(f.tool(s, "vsr_bounds", args).str("state") == "pending");
  CHECK(f.status(s, s.id, 1).str("progress") == "queued");
  f.drain();
  const auto completed = f.status(s, s.id, 1);
  REQUIRE(completed.str("outcome") == "ok");
  const auto *result = completed.result();
  REQUIRE(result);
  CHECK(result->get("state")->s == "unavailable");
  CHECK(result->get("reason")->s == "missing_binding");
  CHECK(result->get("scene_id")->n == f.sceneId);
  CHECK(result->get("scene_revision")->s == token(f.sceneIds->sceneRevision()));
  CHECK(result->get("inspection_revision")->s
      == token(f.sceneIds->inspectionRevision()));
  CHECK_FALSE(result->get("selection")->get("include_disabled")->b);
  CHECK_FALSE(result->get("min"));
  CHECK_FALSE(result->get("source"));
  CHECK(f.tool(s, "vsr_bounds", args).flag("replayed"));
  const auto explicitFalse =
      "{\"request_id\":1" + suffix + ",\"include_disabled\":false}";
  CHECK(f.tool(s, "vsr_bounds", explicitFalse).errorCode()
      == "request_id_conflict");
  const auto second =
      "{\"request_id\":2" + suffix + ",\"include_disabled\":false}";
  REQUIRE(f.tool(s, "vsr_bounds", second).str("state") == "pending");
  f.drain();
  CHECK(f.tool(s, "vsr_bounds", second).flag("replayed"));
  CHECK(f.tool(s, "vsr_bounds", "{\"request_id\":2" + suffix + "}").errorCode()
      == "request_id_conflict");
  f.authority->restrict(
      session::Target::scene(f.sceneId), session::Capability::READ);
  const auto hidden = f.status(s, s.id, 1);
  CHECK_FALSE(hidden.result());
  CHECK_FALSE(hidden.sc()->get("scene_id"));
  CHECK_FALSE(hidden.sc()->get("scene_revision"));
  CHECK_FALSE(hidden.sc()->get("selection"));
  CHECK(f.tool(s, "vsr_bounds", "{\"request_id\":3" + suffix + "}").errorCode()
      == "forbidden");
  CHECK(f.inspector->scratch().used() == 0);
}

SCENARIO(
    "MCP discovery and inspection tools page over loopback", "[integration]")
{
  Fixture f;
  auto list = f.rpc("tools/list", "{}");
  const std::string listBody = json::write(list.json);
  for (const char *name :
      {"vsr_discover", "vsr_inspect", "vsr_operation_status", "vsr_cancel"})
    REQUIRE(listBody.find(name) != std::string::npos);

  auto s = f.attach();
  auto ack = f.tool(s, "vsr_discover", "{\"request_id\":1}");
  REQUIRE(ack.status == 200);
  REQUIRE(ack.sc()->get("operation_ref")->get("session_id")->s == s.id);
  REQUIRE(ack.sc()->get("operation_ref")->get("request_id")->n == 1);
  CHECK(ack.str("kind") == "discover");
  CHECK(ack.str("state") == "pending");
  CHECK(ack.str("progress") == "queued");
  CHECK(!ack.flag("replayed"));

  f.drain();
  auto st = f.status(s, s.id, 1);
  REQUIRE(st.str("state") == "terminal");
  CHECK(st.str("outcome") == "ok");
  CHECK(!st.isError());
  CHECK(!st.flag("committed"));
  // Only the readable scene is listed, with its effective capabilities.
  const auto &items = st.result()->get("items")->a;
  REQUIRE(items.size() == 1);
  CHECK(items[0].get("kind")->s == "scene");
  CHECK(items[0].get("id")->n == f.sceneId);
  CHECK(items[0].get("capabilities")->get("read")->b);
  CHECK(!items[0].get("capabilities")->get("edit")->b);

  // Inspection, first page and continuation by cursor.
  f.tool(s,
      "vsr_inspect",
      "{\"request_id\":2,\"scene_id\":" + std::to_string(f.sceneId)
          + ",\"page_size\":2}");
  f.drain();
  st = f.status(s, s.id, 2);
  CHECK(st.str("kind") == "inspect");
  REQUIRE(st.str("outcome") == "ok");
  CHECK(st.result()->get("returned")->n == 2);
  const std::string cursor = st.result()->get("cursor")->s;
  f.tool(s,
      "vsr_inspect",
      "{\"request_id\":3,\"scene_id\":" + std::to_string(f.sceneId)
          + ",\"page_size\":2,\"cursor\":\"" + cursor + "\"}");
  f.drain();
  CHECK(f.status(s, s.id, 3).str("outcome") == "ok");

  // Identical retry replays; changed arguments under the same ID conflict.
  auto replay = f.tool(s, "vsr_discover", "{\"request_id\":1}");
  CHECK(replay.flag("replayed"));
  CHECK(replay.str("outcome") == "ok");
  CHECK(f.tool(s, "vsr_discover", "{\"request_id\":1,\"page_size\":3}")
            .errorCode()
      == "request_id_conflict");

  // Stale cursor: a terminal non-ok outcome inside a tool result.
  f.scene.getObject<vsr::scene::Material>(0)->setName("renamed");
  f.tool(s,
      "vsr_inspect",
      "{\"request_id\":4,\"scene_id\":" + std::to_string(f.sceneId)
          + ",\"page_size\":2,\"cursor\":\"" + cursor + "\"}");
  f.drain();
  st = f.status(s, s.id, 4);
  CHECK(st.str("outcome") == "cursor_stale");
  CHECK(st.isError());
}

SCENARIO("MCP node queries expose row matrices and transform affordances",
    "[integration][node-transform-mcp]")
{
  Fixture f;
  auto *layer = f.scene.defaultLayer();
  auto parentMatrix = vsr::math::IDENTITY_MAT4;
  parentMatrix[0].x = 2;
  parentMatrix[1].x = 1;
  parentMatrix[1].y = 3;
  parentMatrix[2].z = 4;
  parentMatrix[3] = {10, 20, 30, 1};
  auto parent = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, parentMatrix, "parent"));
  auto local = vsr::math::IDENTITY_MAT4;
  local[0].x = std::nextafter(1.f, 2.f);
  local[3] = {5, 6, 7, 1};
  parent->insert_last_child(vsr::scene::LayerNodeData(layer, local, "wire"));
  auto invalid = vsr::math::IDENTITY_MAT4;
  invalid[0].x = std::numeric_limits<float>::infinity();
  layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, invalid, "invalid"));
  vsr::math::mat3 signedSrt;
  signedSrt[0] = {-2, 0, 4};
  signedSrt[1] = {90, 0, 0};
  signedSrt[2] = {11, 12, 13};
  layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, signedSrt, "signed"));
  f.scene.signalLayerStructureChanged(layer);
  auto s = f.attach();
  auto ack = f.tool(s,
      "vsr_inspect",
      "{\"request_id\":1,\"scene_id\":" + std::to_string(f.sceneId)
          + ",\"kind\":\"nodes\"}");
  REQUIRE(ack.errorCode().empty());
  f.drain();
  const auto status = f.status(s, s.id, 1);
  REQUIRE(status.str("outcome") == "ok");
  const auto &items = status.result()->get("items")->a;
  REQUIRE(items.size() == 5);
  REQUIRE(items[2].get("world_matrix"));
  const auto &world = items[2].get("world_matrix")->get("data")->a;
  CHECK(world[0].a[3].n == 26);
  CHECK(world[1].a[3].n == 38);
  CHECK(world[2].a[3].n == 58);
  CHECK(items[2].get("local_matrix")->get("data")->a[0].a[0].n
      == 1.00000011920928955078125);
  CHECK(items[2].get("rotation")->get("state")->s == "available");
  CHECK(items[1].get("rotation")->get("reason")->s == "unfaithful_srt_cache");
  CHECK(items[1].get("edit_affordances")->get("matrix")->get("available")->b);
  CHECK(
      items[3].get("world_matrix")->get("reason")->s == "non_finite_transform");
  const auto *component =
      items[3].get("local_matrix")->get("data")->a[0].a[0].get("encoding");
  REQUIRE(component);
  CHECK(component->s == "non_finite");
  CHECK(items[3].get("edit_affordances")->get("matrix")->get("available")->b);
  CHECK(items[4].get("scale")->get("data")->a[0].n == -2);
  CHECK(items[4].get("scale")->get("data")->a[1].n == 0);
  CHECK(items[4].get("rotation")->get("data")->a[0].n == 90);
  const auto &signedMatrix = items[4].get("local_matrix")->get("data")->a;
  CHECK(std::abs(signedMatrix[0].a[2].n - 4) < 1e-5);
  CHECK(std::abs(signedMatrix[2].a[0].n - 2) < 1e-5);
}

SCENARIO("MCP client sees omission and inheritance guidance",
    "[integration][request-guidance]")
{
  Fixture f;
  auto list = f.rpc("tools/list", "{}");
  bool found = false;
  for (const auto &tool : list.json.get("result")->get("tools")->a) {
    if (tool.get("name")->s != "vsr_inspect")
      continue;
    found = true;
    const auto &required = tool.get("inputSchema")->get("required")->a;
    REQUIRE(required.size() == 2);
    CHECK(required[0].s == "request_id");
    CHECK(required[1].s == "scene_id");
    const auto &description = tool.get("description")->s;
    for (const char *text : {"Omit unused filters",
             "name=\"\"",
             "fresh inspection",
             "RootDelta",
             "BodyTop",
             "sibling",
             "node origin",
             "not geometry",
             "\"kind\":\"objects\"",
             "\"kind\":\"layers\"",
             "\"kind\":\"nodes\""})
      CHECK(description.find(text) != std::string::npos);
  }
  CHECK(found);
}

SCENARIO("MCP Array inspection reports actual mappings without pixel writes",
    "[integration][mcp][image-tracking]")
{
  Fixture f;
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  image->setName("tracked-image");
  const unsigned char texel[] = {17, 29, 41, 0};
  image->setData(static_cast<const void *>(texel));
  auto s = f.attach();
  auto before = f.inspectArray(s, 1);
  REQUIRE(before.str("outcome") == "ok");
  const auto &item = before.result()->get("items")->a.at(0);
  REQUIRE(item.get("mapped"));
  CHECK_FALSE(item.get("mapped")->b);
  REQUIRE(item.get("image_info"));
  CHECK(item.get("image_info")->get("state")->s == "unavailable");
  CHECK_FALSE(item.get("image_info")->get("data_version"));
  image->map();
  auto mapped = f.inspectArray(s, 2);
  CHECK(mapped.result()->get("items")->a.at(0).get("mapped")->b);
  CHECK_THROWS(image->map());
  CHECK(static_cast<const unsigned char *>(image->data())[0] == 17);
  image->unmap();
  CHECK_THROWS(image->unmap());
}

SCENARIO("MCP Image mapping invalidates pages without publishing a write",
    "[integration][mcp][image-tracking][image-map-revision]")
{
  Fixture f({}, true, false, false, 120., true);
  auto image = f.scene.createArray(ANARI_UFIXED8, 1, 1);
  image->setName("tracked-image");
  auto second = f.scene.createArray(ANARI_UFIXED8, 1, 1);
  second->setName("tracked-image");
  auto s = f.attach();
  auto page = [&](std::uint64_t request, const std::string &cursor = "") {
    const auto ack = f.tool(s,
        "vsr_inspect",
        "{\"request_id\":" + std::to_string(request)
            + ",\"scene_id\":" + std::to_string(f.sceneId)
            + ",\"name\":\"tracked-image\",\"page_size\":1"
            + (cursor.empty() ? "" : ",\"cursor\":\"" + cursor + "\"") + "}");
    REQUIRE(ack.errorCode().empty());
    f.drain();
    return f.status(s, s.id, request);
  };
  auto before = page(1);
  REQUIRE(before.str("outcome") == "ok");
  const auto &beforeItem = before.result()->get("items")->a.at(0);
  const auto initial = beforeItem.get("image_info")->get("data_version")->s;
  const auto version = beforeItem.get("version")->n;
  const auto revision =
      std::stoull(before.result()->get("revision")->s, nullptr, 16);
  const auto sceneRevision = before.result()->get("scene_revision")->s;
  const auto cursor = before.result()->get("cursor")->s;

  auto *bytes = static_cast<unsigned char *>(image->map());
  *bytes = 61;
  auto mapped = page(2);
  REQUIRE(mapped.str("outcome") == "ok");
  const auto &mappedItem = mapped.result()->get("items")->a.at(0);
  CHECK(mappedItem.get("mapped")->b);
  CHECK(mappedItem.get("version")->n == version);
  CHECK(mappedItem.get("image_info")->get("data_version")->s == initial);
  CHECK(std::stoull(mapped.result()->get("revision")->s, nullptr, 16)
      == revision + 1);
  CHECK(mapped.result()->get("scene_revision")->s == sceneRevision);
  CHECK(page(3, cursor).str("outcome") == "cursor_stale");
  const auto mappedCursor = mapped.result()->get("cursor")->s;
  CHECK_THROWS(image->map());
  auto stillMapped = page(4);
  CHECK(stillMapped.result()->get("revision")->s
      == mapped.result()->get("revision")->s);

  image->unmap();
  auto completed = page(5);
  REQUIRE(completed.str("outcome") == "ok");
  const auto &completedItem = completed.result()->get("items")->a.at(0);
  CHECK_FALSE(completedItem.get("mapped")->b);
  CHECK(completedItem.get("version")->n == version + 1);
  CHECK(completedItem.get("image_info")->get("data_version")->s != initial);
  CHECK(std::stoull(completed.result()->get("revision")->s, nullptr, 16)
      == revision + 2);
  const auto separator = sceneRevision.rfind('.') + 1;
  CHECK(completed.result()->get("scene_revision")->s
      == sceneRevision.substr(0, separator)
          + std::to_string(std::stoull(sceneRevision.substr(separator)) + 1));
  CHECK(static_cast<const unsigned char *>(image->data())[0] == 61);
  CHECK(page(6, mappedCursor).str("outcome") == "cursor_stale");
  auto readOnly = page(7);
  CHECK(json::write(*readOnly.result()) == json::write(*completed.result()));
}

SCENARIO("MCP automatically tracks completed ordinary Image writes launch-wide",
    "[integration][mcp][image-tracking]")
{
  Fixture f({}, true, false, false, 120., true, 4);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  image->setName("tracked-image");
  const unsigned char original[] = {17, 29, 41, 0};
  image->setData(static_cast<const void *>(original));
  auto second = f.scene.createArray(ANARI_UFIXED8_R_SRGB, 1, 1);
  second->setName("second-image");
  auto s = f.attach();
  auto before = f.inspectArray(s, 1);
  const auto &info = *before.result()->get("items")->a.at(0).get("image_info");
  REQUIRE(info.get("state")->s == "available");
  const auto initial = info.get("data_version")->s;
  CHECK(info.get("dimensions")->a.size() == 2);
  CHECK(info.get("dimensions")->a[0].n == 1);
  CHECK(info.get("dimensions")->a[1].n == 1);
  CHECK(info.get("element_type")->s == "ANARI_UFIXED8_VEC4");
  CHECK(info.get("channel_count")->n == 4);
  CHECK(info.get("color_space")->s == "linear");
  CHECK(info.get("row_order")->s == "top_down");
  auto *mapped = static_cast<unsigned char *>(image->map());
  mapped[0] = 99;
  auto outstanding = f.inspectArray(s, 2);
  const auto &mappedItem = outstanding.result()->get("items")->a.at(0);
  CHECK(mappedItem.get("mapped")->b);
  CHECK(mappedItem.get("image_info")->get("data_version")->s == initial);
  auto *other = static_cast<unsigned char *>(second->map());
  *other = 77;
  second->unmap();
  auto completedOther = f.inspectArray(s, 3, "second-image");
  CHECK(completedOther.result()
            ->get("items")
            ->a.at(0)
            .get("image_info")
            ->get("color_space")
            ->s
      == "srgb");
  image->unmap();
  auto completed = f.inspectArray(s, 4);
  auto next = completed.result()
                  ->get("items")
                  ->a.at(0)
                  .get("image_info")
                  ->get("data_version")
                  ->s;
  CHECK(next != initial);
  CHECK(static_cast<const unsigned char *>(image->data())[0] == 99);
  image->setData(static_cast<const void *>(original));
  auto changedBack = f.inspectArray(s, 5);
  auto last = changedBack.result()
                  ->get("items")
                  ->a.at(0)
                  .get("image_info")
                  ->get("data_version")
                  ->s;
  CHECK(last != initial);
  CHECK(last != next);
  CHECK_THROWS(image->map());
  CHECK_THROWS(second->map());
  CHECK_THROWS(image->setData(static_cast<const void *>(original)));
  vsr::scene::Array replacement(ANARI_UFIXED8_VEC4, 1, 1);
  CHECK_THROWS(*image = std::move(replacement));
  auto exhausted = f.inspectArray(s, 6);
  const auto &exhaustedItem = exhausted.result()->get("items")->a.at(0);
  CHECK_FALSE(exhaustedItem.get("mapped")->b);
  CHECK(exhaustedItem.get("image_info")->get("data_version")->s == last);
  CHECK(static_cast<const unsigned char *>(image->data())[0] == 17);
  f.scene.getObject<vsr::scene::Material>(0)->setName("unrelated");
  auto material = f.inspectArray(s, 7, "unrelated");
  const auto &materialItem = material.result()->get("items")->a.at(0);
  const auto target = "\"handle\":\"" + materialItem.get("handle")->s
      + "\",\"version\":"
      + std::to_string(std::uint64_t(materialItem.get("version")->n));
  f.editOpacity(s, 8, target, .5);
  f.drain();
  CHECK(f.status(s, s.id, 8).flag("committed"));
  REQUIRE(f.rpc("vsr/detach", "{}", &s).status == 200);
  auto reconnected = f.attach();
  auto retained = f.inspectArray(reconnected, 1);
  CHECK(retained.result()
            ->get("items")
            ->a.at(0)
            .get("image_info")
            ->get("data_version")
            ->s
      == last);
}

SCENARIO("MCP Images retain cache sharing but retire deleted Array identities",
    "[integration][mcp][image-tracking]")
{
  Fixture f({}, true, false, false, 120., true);
  auto gap = f.scene.createArray(ANARI_FLOAT32, 1);
  vsr::io::ImageCache cache(&f.scene);
  const vsr::io::ImageSource source{
      "embedded:tracked", vsr::io::ColorSpace::LINEAR};
  const unsigned char original[] = {17, 29, 41, 0};
  auto image = cache.acquireDecoded(
      source, ANARI_UFIXED8_VEC4, 1, 1, vsr::io::RowOrder::TOP_DOWN, original);
  image.texels->setName("tracked-image");
  auto sampler = vsr::io::makeImageSampler(cache, image, "first");
  auto s = f.attach();
  auto before = f.inspectArray(s, 1);
  const auto oldHandle =
      before.result()->get("items")->a.at(0).get("handle")->s;
  const auto oldVersion = before.result()
                              ->get("items")
                              ->a.at(0)
                              .get("image_info")
                              ->get("data_version")
                              ->s;
  const unsigned char changed[] = {91, 73, 55, 0};
  image.texels->setData(static_cast<const void *>(changed));
  auto same = cache.acquireDecoded(
      source, ANARI_UFIXED8_VEC4, 1, 1, vsr::io::RowOrder::TOP_DOWN, original);
  CHECK(same.texels == image.texels);
  CHECK(static_cast<const unsigned char *>(same.texels->data())[0] == 91);
  f.scene.removeObject(sampler.data());
  auto rebound = vsr::io::makeImageSampler(cache, cache.find(source), "second");
  CHECK(rebound->parameterValueAsObject<vsr::scene::Array>("image")
      == image.texels.data());
  f.scene.removeObject(rebound.data());
  auto written = f.inspectArray(s, 2);
  const auto writtenVersion = written.result()
                                  ->get("items")
                                  ->a.at(0)
                                  .get("image_info")
                                  ->get("data_version")
                                  ->s;
  f.scene.removeObject(gap.data());
  f.scene.defragmentObjectStorage();
  image = cache.find(source);
  REQUIRE(image);
  CHECK(image.texels.index() == 0);
  CHECK(static_cast<const unsigned char *>(image.texels->data())[0] == 91);
  auto relocated = f.inspectArray(s, 3);
  const auto &relocatedItem = relocated.result()->get("items")->a.at(0);
  CHECK(relocatedItem.get("handle")->s == oldHandle);
  CHECK(relocatedItem.get("image_info")->get("data_version")->s
      == writtenVersion);
  const auto index = image.texels.index();
  f.scene.removeObject(image.texels.data());
  auto replacement = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  REQUIRE(replacement.index() == index);
  replacement->setName("tracked-image");
  replacement->setData(static_cast<const void *>(original));
  CHECK_FALSE(cache.find(source));
  CHECK_FALSE(vsr::io::makeImageSampler(cache, image, "stale"));
  auto after = f.inspectArray(s, 4);
  const auto &item = after.result()->get("items")->a.at(0);
  CHECK(item.get("handle")->s != oldHandle);
  CHECK(item.get("image_info")->get("data_version")->s != oldVersion);
  auto decodedAgain = cache.acquireDecoded(
      source, ANARI_UFIXED8_VEC4, 1, 1, vsr::io::RowOrder::TOP_DOWN, original);
  CHECK(decodedAgain.texels != replacement);
  cache.clear();
  CHECK_FALSE(cache.find(source));
  CHECK(
      static_cast<const unsigned char *>(decodedAgain.texels->data())[0] == 17);
}

SCENARIO("Image-qualified hosts refuse safe points on another thread",
    "[integration][mcp][image-tracking]")
{
  Fixture f({}, true, false, false, 120., true);
  auto image = f.scene.createArray(ANARI_UFIXED8, 1, 1);
  image->setName("tracked-image");
  const unsigned char original = 31;
  image->setData(static_cast<const void *>(&original));
  auto s = f.attach();
  f.tool(s, "vsr_inspect", "{\"request_id\":1,\"scene_id\":0}");
  std::size_t serviced = 99;
  bool refusedWrite = false;
  std::thread worker([&] {
    serviced = f.integration.service();
    try {
      image->setData(static_cast<const void *>(&original));
    } catch (const std::runtime_error &) {
      refusedWrite = true;
    }
  });
  worker.join();
  CHECK(serviced == 0);
  CHECK(refusedWrite);
  f.drain();
  CHECK(f.status(s, s.id, 1).str("outcome") == "ok");
  CHECK(static_cast<const unsigned char *>(image->data())[0] == 31);
}

SCENARIO("MCP Image support fields distinguish formats and host availability",
    "[integration][mcp][image-tracking]")
{
  Fixture f({}, true, false, false, 120., true);
  auto s = f.attach();
  std::uint64_t request = 1;
  for (const auto type : {ANARI_FIXED8,
           ANARI_FIXED8_VEC2,
           ANARI_FIXED8_VEC3,
           ANARI_FIXED8_VEC4,
           ANARI_UFIXED8,
           ANARI_UFIXED8_VEC2,
           ANARI_UFIXED8_VEC3,
           ANARI_UFIXED8_VEC4,
           ANARI_UFIXED8_R_SRGB,
           ANARI_UFIXED8_RA_SRGB,
           ANARI_UFIXED8_RGB_SRGB,
           ANARI_UFIXED8_RGBA_SRGB}) {
    auto image = f.scene.createArray(type, 1, 1);
    image->setName("tracked-image");
    auto reply = f.inspectArray(s, request++);
    const auto &item = reply.result()->get("items")->a.at(0);
    CHECK(item.get("mapped")->type == json::Value::Type::BOOL);
    const auto &info = *item.get("image_info");
    CHECK(info.get("state")->s == "available");
    CHECK(info.get("channel_count")->n == anari::sizeOf(type));
    CHECK(info.get("element_type")->s == anari::toString(type));
    CHECK(info.get("row_order")->s == "top_down");
    f.scene.removeObject(image.data());
  }
  for (const auto type : {ANARI_UINT8,
           ANARI_FLOAT32,
           ANARI_ARRAY,
           ANARI_UFIXED16,
           ANARI_UNKNOWN}) {
    auto array = f.scene.createArray(type, 1, 1);
    array->setName("tracked-image");
    auto reply = f.inspectArray(s, request++);
    const auto &item = reply.result()->get("items")->a.at(0);
    const auto &info = *item.get("image_info");
    CHECK_FALSE(item.get("mapped")->b);
    CHECK(info.get("state")->s == "unsupported");
    CHECK(info.get("reason")->s == "unsupported_image");
    CHECK(info.o.size() == 2);
    f.scene.removeObject(array.data());
  }
  for (const auto shape : {std::array<std::size_t, 2>{0, 1}, {1, 0}}) {
    auto array = f.scene.createArray(ANARI_UFIXED8, shape[0], shape[1]);
    array->setName("tracked-image");
    auto reply = f.inspectArray(s, request++);
    CHECK(
        reply.result()->get("items")->a.at(0).get("image_info")->get("state")->s
        == "unsupported");
    f.scene.removeObject(array.data());
  }
  auto proxy = f.scene.createArrayProxy(ANARI_UFIXED8_VEC4, 1, 1);
  proxy->setName("tracked-image");
  auto unavailableProxy = f.inspectArray(s, request++);
  CHECK(unavailableProxy.result()
            ->get("items")
            ->a.at(0)
            .get("image_info")
            ->get("state")
            ->s
      == "unsupported");
  f.scene.removeObject(proxy.data());
  auto image = f.scene.createArray(ANARI_UFIXED8, 1, 1);
  image->setName("tracked-image");
  const unsigned char original = 31;
  image->setData(static_cast<const void *>(&original));
  auto before = f.inspectArray(s, request++);
  const auto initial = before.result()
                           ->get("items")
                           ->a.at(0)
                           .get("image_info")
                           ->get("data_version")
                           ->s;
  REQUIRE(f.integration.setImageExchangeAvailable(f.sceneId, false));
  auto unavailable = f.inspectArray(s, request++);
  const auto &info =
      *unavailable.result()->get("items")->a.at(0).get("image_info");
  CHECK(info.get("state")->s == "unavailable");
  CHECK(info.o.size() == 2);
  image->setData(static_cast<const void *>(&original));
  REQUIRE(f.rpc("vsr/detach", "{}", &s).status == 200);
  image->setData(static_cast<const void *>(&original));
  auto reconnected = f.attach();
  REQUIRE(f.integration.setImageExchangeAvailable(f.sceneId, true));
  auto after = f.inspectArray(reconnected, 1);
  const auto completed = after.result()
                             ->get("items")
                             ->a.at(0)
                             .get("image_info")
                             ->get("data_version")
                             ->s;
  CHECK(completed != initial);
  auto readOnly = f.inspectArray(reconnected, 2);
  CHECK(readOnly.result()
            ->get("items")
            ->a.at(0)
            .get("image_info")
            ->get("data_version")
            ->s
      == completed);
  f.integration.imageWriteSequence()->stopWrites();
  CHECK_THROWS(image->map());
  auto stopped = f.inspectArray(reconnected, 3);
  CHECK(
      stopped.result()->get("items")->a.at(0).get("image_info")->get("state")->s
      == "unavailable");
  CHECK(static_cast<const unsigned char *>(image->data())[0] == 31);
  const auto listed = f.rpc("tools/list", "{}", &reconnected);
  for (const auto &tool : listed.json.get("result")->get("tools")->a) {
    CHECK(tool.get("name")->s != "vsr_image_import");
  }
}

SCENARIO("MCP Image reservations burn failures and publish completed writes",
    "[integration][mcp][image-tracking]")
{
  Fixture f({}, true, false, false, 120., true, 2);
  auto image = f.scene.createArray(ANARI_UFIXED8, 1, 1);
  image->setName("tracked-image");
  auto s = f.attach();
  auto before = f.inspectArray(s, 1);
  const auto initial = before.result()
                           ->get("items")
                           ->a.at(0)
                           .get("image_info")
                           ->get("data_version")
                           ->s;
  auto *fault = f.scene.updateDelegate().emplace<WriteFaultObserver>();
  CHECK_THROWS(image->map());
  auto failed = f.inspectArray(s, 2);
  const auto &failedItem = failed.result()->get("items")->a.at(0);
  CHECK_FALSE(failedItem.get("mapped")->b);
  CHECK(failedItem.get("image_info")->get("data_version")->s == initial);
  fault->failMap = false;
  auto *bytes = static_cast<unsigned char *>(image->map());
  *bytes = 61;
  const unsigned char candidate = 99;
  CHECK_THROWS(image->setData(static_cast<const void *>(&candidate)));
  CHECK(image->isMapped());
  CHECK(*bytes == 61);
  fault->failUnmap = true;
  CHECK_THROWS(image->unmap());
  auto completed = f.inspectArray(s, 3);
  const auto &item = completed.result()->get("items")->a.at(0);
  CHECK_FALSE(item.get("mapped")->b);
  CHECK(item.get("image_info")->get("data_version")->s != initial);
  CHECK(static_cast<const unsigned char *>(image->data())[0] == 61);
  CHECK_THROWS(image->map());
  f.scene.updateDelegate().erase(fault);
}

SCENARIO("MCP inspection diagnostics preserve pre-admission identity",
    "[integration][request-guidance]")
{
  Fixture f;
  auto s = f.attach();
  const auto scene = "\"scene_id\":" + std::to_string(f.sceneId);
  auto bad = f.tool(s,
      "vsr_inspect",
      "{\"request_id\":1," + scene
          + ",\"kind\":\"nodes\",\"object_type\":\"ANARI_SURFACE\"}");
  REQUIRE(bad.errorCode() == "invalid_request");
  const auto *data = bad.json.get("error")->get("data");
  REQUIRE(data->get("details"));
  const auto &details = data->get("details")->a;
  REQUIRE(details.size() == 1);
  CHECK(details[0].get("field")->s == "object_type");
  CHECK(details[0].get("kind")->s == "nodes");
  CHECK(details[0].get("reason")->s == "inapplicable_filter");
  auto corrected = f.tool(
      s, "vsr_inspect", "{\"request_id\":1," + scene + ",\"kind\":\"nodes\"}");
  REQUIRE(corrected.errorCode().empty());
  f.drain();
  CHECK(f.status(s, s.id, 1).str("outcome") == "ok");
}

SCENARIO("MCP malformed cursor identifies only its argument",
    "[integration][request-guidance]")
{
  Fixture f;
  auto s = f.attach();
  auto bad = f.tool(s,
      "vsr_inspect",
      "{\"request_id\":1,\"scene_id\":0,\"cursor\":\"private:credential@0x123\"}");
  REQUIRE(bad.errorCode() == "invalid_request");
  const auto *details = bad.json.get("error")->get("data")->get("details");
  REQUIRE(details);
  REQUIRE(details->a.size() == 1);
  CHECK(details->a[0].get("field")->s == "cursor");
  CHECK(details->a[0].get("reason")->s == "invalid_cursor");
  CHECK(json::write(bad.json).find("private:credential") == std::string::npos);
  CHECK(f.engine->highWater(s.id) == 0);
}

SCENARIO("MCP diagnostics bound known fields without echoing values",
    "[integration][request-guidance]")
{
  Fixture f;
  auto s = f.attach();
  for (const char *field : {"kind",
           "name",
           "name_contains",
           "handle",
           "object_type",
           "object_subtype",
           "node_kind",
           "layer_handle",
           "parent_handle",
           "cursor",
           "page_size"}) {
    auto bad = f.tool(s,
        "vsr_inspect",
        "{\"request_id\":1,\"scene_id\":0," + json::quote(field) + ":null}");
    REQUIRE(bad.errorCode() == "invalid_request");
    const auto *details = bad.json.get("error")->get("data")->get("details");
    REQUIRE(details);
    REQUIRE(details->a.size() == 1);
    CHECK(details->a[0].get("field")->s == field);
    CHECK(details->a[0].get("reason")->s == "invalid_type");
    CHECK(details->a[0].get("kind")->s
        == (std::string(field) == "kind" ? "unknown" : "objects"));
  }
  for (const char *field : {"request_id", "scene_id"}) {
    const auto args = std::string(field) == "request_id"
        ? "{\"request_id\":null,\"scene_id\":0}"
        : "{\"request_id\":1,\"scene_id\":null}";
    auto reply = f.tool(s, "vsr_inspect", args);
    REQUIRE(reply.errorCode() == "invalid_request");
    const auto &diagnostic =
        reply.json.get("error")->get("data")->get("details")->a[0];
    CHECK(diagnostic.get("field")->s == field);
    CHECK(diagnostic.get("reason")->s == "invalid_type");
  }
  std::string bad =
      "{\"request_id\":1,\"scene_id\":0,\"kind\":\"private-kind\",\"page_size\":0";
  const auto secret = std::string("credential@0x123") + std::string(4097, 'x');
  for (const char *field : {"name",
           "name_contains",
           "handle",
           "object_type",
           "object_subtype",
           "node_kind",
           "layer_handle",
           "parent_handle"})
    bad += "," + json::quote(field) + ":" + json::quote(secret);
  auto reply = f.tool(s, "vsr_inspect", bad + "}");
  REQUIRE(reply.errorCode() == "invalid_request");
  const auto *details = reply.json.get("error")->get("data")->get("details");
  REQUIRE(details);
  CHECK(details->a.size() == 16);
  CHECK(json::write(*details).size() <= 4096);
  const auto wire = json::write(reply.json);
  CHECK(wire.find("credential") == std::string::npos);
  CHECK(wire.find("0x123") == std::string::npos);
  CHECK(wire.find("private-kind") == std::string::npos);
  CHECK(wire.find("hidden") == std::string::npos);
  for (const auto &entry : details->a) {
    CHECK(entry.get("kind")->s == "unknown");
    CHECK(entry.o.size() == 3);
  }
  auto placeholder = f.tool(s,
      "vsr_inspect",
      "{\"request_id\":1,\"scene_id\":0,\"kind\":\"nodes\",\"handle\":\"00000000000000000000000000000000\"}");
  REQUIRE(placeholder.errorCode() == "invalid_request");
  CHECK(placeholder.json.get("error")
            ->get("data")
            ->get("details")
            ->a[0]
            .get("reason")
            ->s
      == "invalid_handle_kind");
  auto unknown = f.tool(s,
      "vsr_inspect",
      "{\"request_id\":1,\"scene_id\":0,\"legacy_unknown\":\"kept-compatible\"}");
  REQUIRE(unknown.errorCode().empty());
  f.drain();
  CHECK(f.status(s, s.id, 1).str("outcome") == "ok");
}

SCENARIO("MCP minimal filters preserve meaningful empty names",
    "[integration][request-guidance]")
{
  Fixture f;
  f.scene.getObject<vsr::scene::Material>(0)->setName("");
  for (int i = 1; i <= 5; ++i)
    f.scene.getObject<vsr::scene::Material>(i)->setName("named");
  f.scene.defaultCamera()->setName("named-camera");
  auto *layer = f.scene.defaultLayer();
  layer->setName("");
  layer->root()->value().setName("named-root");
  layer->root()->insert_last_child(vsr::scene::LayerNodeData(layer, ""));
  layer->root()->insert_last_child(vsr::scene::LayerNodeData(layer, "named"));
  f.scene.signalLayerStructureChanged(layer);
  auto s = f.attach();
  std::uint64_t id = 0;
  for (const char *kind : {"objects", "layers", "nodes"}) {
    INFO("inspection kind: " << kind);
    const auto args = "\"scene_id\":0,\"kind\":" + json::quote(kind);
    auto minimal = f.tool(s,
        "vsr_inspect",
        "{\"request_id\":" + std::to_string(++id) + "," + args + "}");
    REQUIRE(minimal.errorCode().empty());
    f.drain();
    auto all = f.status(s, s.id, id);
    REQUIRE(all.str("outcome") == "ok");
    const auto total = all.result()->get("items")->a.size();
    REQUIRE(total > 0);
    auto empty = f.tool(s,
        "vsr_inspect",
        "{\"request_id\":" + std::to_string(++id) + "," + args
            + ",\"name\":\"\"}");
    REQUIRE(empty.errorCode().empty());
    f.drain();
    auto exact = f.status(s, s.id, id);
    REQUIRE(exact.str("outcome") == "ok");
    REQUIRE(exact.result()->get("items")->a.size() == 1);
    CHECK(
        exact.result()->get("items")->a[0].get("name")->get("text")->s.empty());
    auto contains = f.tool(s,
        "vsr_inspect",
        "{\"request_id\":" + std::to_string(++id) + "," + args
            + ",\"name_contains\":\"\",\"cursor\":\"\"}");
    REQUIRE(contains.errorCode().empty());
    f.drain();
    CHECK(f.status(s, s.id, id).result()->get("items")->a.size() == total);
  }
}

SCENARIO("MCP node queries parse filters and expose placement records",
    "[integration][node-mcp]")
{
  Fixture f;
  auto *layer = f.scene.defaultLayer();
  auto node = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, "wire"));
  f.scene.signalLayerStructureChanged(layer);
  auto s = f.attach();
  const auto scene = "\"scene_id\":" + std::to_string(f.sceneId);
  auto ack = f.tool(s,
      "vsr_inspect",
      "{\"request_id\":1," + scene
          + ",\"kind\":\"nodes\",\"name\":\"wire\",\"node_kind\":\"empty\"}");
  REQUIRE(ack.errorCode().empty());
  f.drain();
  auto result = f.status(s, s.id, 1);
  REQUIRE(result.str("outcome") == "ok");
  const auto &items = result.result()->get("items")->a;
  REQUIRE(items.size() == 1);
  CHECK(items[0].get("node_kind")->s == "empty");
  CHECK(items[0].get("enabled")->b);
  REQUIRE(items[0].get("node_edit_version"));
  const auto version = items[0].get("node_edit_version")->s;
  CHECK(!version.empty());
  REQUIRE(items[0].get("structural_version"));
  REQUIRE(items[0].get("ancestry_version"));
  const auto structure = items[0].get("structural_version")->s;
  const auto ancestry = items[0].get("ancestry_version")->s;
  CHECK(!structure.empty());
  CHECK(!ancestry.empty());
  auto direct = f.tool(s,
      "vsr_inspect",
      "{\"request_id\":2," + scene + ",\"kind\":\"nodes\",\"handle\":"
          + json::quote(items[0].get("handle")->s) + ",\"layer_handle\":"
          + json::quote(items[0].get("layer_handle")->s) + ",\"parent_handle\":"
          + json::quote(items[0].get("parent_handle")->s) + "}");
  REQUIRE(direct.errorCode().empty());
  f.drain();
  const auto directResult = f.status(s, s.id, 2);
  REQUIRE(directResult.result()->get("items")->a.size() == 1);
  CHECK(directResult.result()->get("items")->a[0].get("node_edit_version")->s
      == version);
  node->value().setEnabled(false);
  node->value().setEnabled(true);
  f.scene.signalLayerStructureChanged(layer);
  REQUIRE(
      f.tool(s,
           "vsr_inspect",
           "{\"request_id\":4," + scene
               + ",\"kind\":\"nodes\",\"name\":\"wire\"}")
          .errorCode()
          .empty());
  f.drain();
  const auto changed = f.status(s, s.id, 4);
  REQUIRE(changed.result()->get("items")->a.size() == 1);
  CHECK(changed.result()->get("items")->a[0].get("node_edit_version")->s
      != version);
  CHECK(changed.result()->get("items")->a[0].get("structural_version")->s
      == structure);
  CHECK(changed.result()->get("items")->a[0].get("ancestry_version")->s
      == ancestry);
  layer->root()->value().setAsTransform(vsr::math::IDENTITY_MAT4);
  f.scene.signalLayerTransformChanged(layer);
  REQUIRE(
      f.tool(s,
           "vsr_inspect",
           "{\"request_id\":5," + scene
               + ",\"kind\":\"nodes\",\"name\":\"wire\"}")
          .errorCode()
          .empty());
  f.drain();
  const auto parentChanged = f.status(s, s.id, 5);
  const auto &placement = parentChanged.result()->get("items")->a[0];
  CHECK(placement.get("structural_version")->s == structure);
  CHECK(placement.get("ancestry_version")->s != ancestry);
  CHECK(f.tool(s,
             "vsr_inspect",
             "{\"request_id\":3," + scene
                 + ",\"kind\":\"nodes\",\"node_kind\":17}")
            .errorCode()
      == "invalid_request");
}

SCENARIO("MCP layer queries expose live layer and root handles",
    "[integration][layer-mcp]")
{
  Fixture f;
  f.scene.removeAllLayers();
  auto *layer = f.scene.addLayer("wire-key");
  layer->setName("wire-name");
  f.scene.signalLayerStructureChanged(layer);
  f.scene.setLayerActive("wire-key", false);
  auto list = f.rpc("tools/list", "{}");
  for (const auto &tool : list.json.get("result")->get("tools")->a) {
    if (tool.get("name")->s == "vsr_inspect") {
      const auto &kinds = tool.get("inputSchema")
                              ->get("properties")
                              ->get("kind")
                              ->get("enum")
                              ->a;
      REQUIRE(kinds.size() == 3);
      CHECK(kinds[0].s == "objects");
      CHECK(kinds[1].s == "layers");
      CHECK(kinds[2].s == "nodes");
      const auto *props = tool.get("inputSchema")->get("properties");
      CHECK(props->get("name")->get("maxLength")->n == 4096);
      CHECK(props->get("name_contains")->get("maxLength")->n == 4096);
      CHECK(props->get("page_size")->get("maximum")->n == 256);
      CHECK(tool.get("description")->s.find("Layer filters: exact stored name")
          != std::string::npos);
    }
  }
  auto s = f.attach();
  const auto scene = "\"scene_id\":" + std::to_string(f.sceneId);
  auto ack = f.tool(
      s, "vsr_inspect", "{\"request_id\":1," + scene + ",\"kind\":\"layers\"}");
  REQUIRE(ack.errorCode().empty());
  f.drain();
  auto result = f.status(s, s.id, 1);
  REQUIRE(result.str("outcome") == "ok");
  const auto &items = result.result()->get("items")->a;
  REQUIRE(items.size() == 1);
  CHECK(items[0].get("name")->get("text")->s == "wire-name");
  CHECK_FALSE(items[0].get("active")->b);
  CHECK(items[0].get("root_handle")->s != items[0].get("handle")->s);
  auto direct = f.tool(s,
      "vsr_inspect",
      "{\"request_id\":2," + scene + ",\"kind\":\"layers\",\"handle\":"
          + json::quote(items[0].get("handle")->s) + "}");
  REQUIRE(direct.errorCode().empty());
  f.drain();
  CHECK(f.status(s, s.id, 2).str("outcome") == "ok");
  auto filtered = f.tool(s,
      "vsr_inspect",
      "{\"request_id\":4," + scene
          + ",\"kind\":\"layers\",\"name\":\"wire-name\",\"name_contains\":\"wire\",\"handle\":"
          + json::quote(items[0].get("handle")->s) + "}");
  REQUIRE(filtered.errorCode().empty());
  f.drain();
  const auto matched = f.status(s, s.id, 4);
  REQUIRE(matched.str("outcome") == "ok");
  REQUIRE(matched.result()->get("items")->a.size() == 1);
  CHECK(matched.result()->get("items")->a[0].get("root_handle")->s
      == items[0].get("root_handle")->s);
  for (const auto &bad :
      {std::string("\"kind\":\"unsupported\""),
          std::string("\"kind\":17"),
          std::string("\"kind\":\"layers\",\"name\":17"),
          std::string("\"kind\":\"layers\",\"name_contains\":null"),
          std::string("\"kind\":\"layers\",\"object_subtype\":\"matte\""),
          std::string("\"kind\":\"layers\",\"node_kind\":\"empty\""),
          std::string("\"kind\":\"layers\",\"layer_handle\":\"bad\""),
          std::string("\"kind\":\"layers\",\"parent_handle\":\"bad\""),
          std::string("\"kind\":\"layers\",\"page_size\":500"),
          std::string("\"kind\":\"layers\",\"name\":")
              + json::quote(std::string(4097, 'x')),
          std::string("\"kind\":\"layers\",\"object_type\":\"ANARI_MATERIAL\""),
          std::string("\"kind\":\"layers\",\"handle\":12"),
          std::string("\"kind\":\"layers\",\"handle\":\"bad\""),
          std::string("\"kind\":\"layers\",\"handle\":")
              + json::quote(items[0].get("root_handle")->s)}) {
    CHECK(
        f.tool(s, "vsr_inspect", "{\"request_id\":3," + scene + "," + bad + "}")
            .errorCode()
        == "invalid_request");
  }
}

SCENARIO("MCP object filters and active paging limits are exposed",
    "[integration][object-filters]")
{
  inspection::PageLimits limits;
  limits.defaultItems = 2;
  limits.maxItems = 3;
  Fixture f(limits);
  f.scene.getObject<vsr::scene::Material>(1)->setName("Queen.*");
  auto list = f.rpc("tools/list", "{}");
  const auto *tools = list.json.get("result")->get("tools");
  REQUIRE(tools);
  for (const auto &tool : tools->a) {
    if (tool.get("name")->s != "vsr_inspect"
        && tool.get("name")->s != "vsr_discover")
      continue;
    const auto *props = tool.get("inputSchema")->get("properties");
    REQUIRE(props->get("page_size")->get("maximum"));
    CHECK(props->get("page_size")->get("maximum")->n == 3);
    CHECK(props->get("page_size")->get("default")->n == 2);
    if (tool.get("name")->s == "vsr_inspect") {
      CHECK(props->get("kind")->get("enum")->a.at(0).s == "objects");
      CHECK(props->get("name")->get("maxLength")->n == 4096);
      CHECK(tool.get("description")->s.find("65536 per operation")
          != std::string::npos);
      CHECK(tool.get("description")->s.find("4096 per safe-point slice")
          != std::string::npos);
      REQUIRE(props->get("object_type")->get("enum"));
    }
  }
  auto s = f.attach();
  const auto scene = "\"scene_id\":" + std::to_string(f.sceneId);
  for (const char *bad : {"\"page_size\":500",
           "\"kind\":\"unsupported\"",
           "\"name\":1",
           "\"object_type\":\"ANARI_FLOAT32\"",
           "\"node_kind\":\"transform\"",
           "\"layer_handle\":\"x\"",
           "\"parent_handle\":\"x\"",
           "\"handle\":\"bad\""}) {
    CHECK(
        f.tool(s, "vsr_inspect", "{\"request_id\":1," + scene + "," + bad + "}")
            .errorCode()
        == "invalid_request");
  }
  CHECK(f.engine->highWater(s.id) == 0);
  auto ack = f.tool(s, "vsr_inspect", "{\"request_id\":1," + scene
      + ",\"kind\":\"objects\",\"name\":\"Queen.*\","
        "\"name_contains\":\".*\",\"object_type\":\"ANARI_MATERIAL\","
        "\"object_subtype\":\"matte\"}");
  REQUIRE(ack.errorCode().empty());
  f.drain();
  auto st = f.status(s, s.id, 1);
  REQUIRE(st.str("outcome") == "ok");
  CHECK(st.result()->get("returned")->n == 1);
  const auto handle = st.result()->get("items")->a.at(0).get("handle")->s;
  f.tool(s,
      "vsr_inspect",
      "{\"request_id\":2," + scene + ",\"handle\":\"" + handle + "\"}");
  f.drain();
  st = f.status(s, s.id, 2);
  CHECK(st.str("outcome") == "ok");
  CHECK(st.result()->get("returned")->n == 1);
  f.tool(s, "vsr_discover", "{\"request_id\":3}");
  f.drain();
  st = f.status(s, s.id, 3);
  const auto *policy = st.result()->get("items")->a.at(0).get("inspection");
  REQUIRE(policy);
  CHECK(policy->get("default_page_size")->n == 2);
  CHECK(policy->get("max_page_size")->n == 3);
}

SCENARIO("MCP inspection carries typed values without granting writes",
    "[integration][typed-inspection]")
{
  Fixture f;
  auto camera = f.scene.createObject<vsr::scene::Camera>("perspective");
  const uint64_t integer = 18446744073709551615ull;
  camera->setParameter("observed", ANARI_UINT64, &integer);
  auto array = f.scene.createArrayProxy(ANARI_FLOAT32, 8);
  camera->setParameterObject("input", *array);
  auto s = f.attach();
  f.tool(s,
      "vsr_inspect",
      "{\"request_id\":1,\"scene_id\":" + std::to_string(f.sceneId) + "}");
  f.drain();
  auto st = f.status(s, s.id, 1);
  REQUIRE(st.str("outcome") == "ok");
  const auto &items = st.result()->get("items")->a;
  const auto &params = items.back().get("parameters")->a;
  bool observed = false, linked = false;
  for (const auto &p : params) {
    if (p.get("name")->s == "observed") {
      const auto *v = p.get("value");
      CHECK(v->get("type")->s == "ANARI_UINT64");
      CHECK(v->get("data")->get("encoding")->s == "decimal");
      CHECK(v->get("data")->get("data")->s == "18446744073709551615");
      observed = true;
    }
    if (p.get("name")->s == "input") {
      CHECK(p.get("value")->get("handle")->s == items.front().get("handle")->s);
      CHECK(p.get("value")->get("summary")->get("element_count")->n == 8);
      linked = true;
    }
  }
  CHECK(observed);
  CHECK(linked);
  CHECK(f.scene.getObject<vsr::scene::Array>(array.index())->isProxy());
  CHECK(!f.authority->effectiveCapabilities(session::Target::scene(f.sceneId))
          .edit);
}

SCENARIO(
    "MCP query tools map envelope and capability rejections", "[integration]")
{
  Fixture f;
  auto s = f.attach();
  // Envelope errors consume no ID.
  for (const char *bad : {"{}",
           "{\"request_id\":\"1\"}",
           "{\"request_id\":1.5}",
           "{\"request_id\":0}",
           "{\"request_id\":9007199254740992}",
           "{\"request_id\":1,\"page_size\":0}",
           "{\"request_id\":1,\"page_size\":\"2\"}",
           "{\"request_id\":1,\"cursor\":\"tampered\"}"})
    CHECK(f.tool(s, "vsr_discover", bad).errorCode() == "invalid_request");
  CHECK(f.engine->highWater(s.id) == 0);

  // Unexposed scene: forbidden, HTTP 403, nothing admitted.
  auto r = f.tool(s,
      "vsr_inspect",
      "{\"request_id\":1,\"scene_id\":" + std::to_string(f.hiddenId) + "}");
  CHECK(r.status == 403);
  CHECK(r.errorCode() == "forbidden");
  CHECK(f.engine->highWater(s.id) == 0);

  // Missing proof never reaches a tool.
  Session wrong{s.id, "nope"};
  CHECK(f.tool(wrong, "vsr_discover", "{\"request_id\":1}").errorCode()
      == "invalid_session_proof");

  // Oversized item.
  inspection::PageLimits tiny;
  tiny.itemBytes = 16;
  Fixture g(tiny);
  auto gs = g.attach();
  g.tool(gs, "vsr_discover", "{\"request_id\":1}");
  g.drain();
  CHECK(g.status(gs, gs.id, 1).str("outcome") == "item_too_large");
}

SCENARIO("MCP lookup and cancel by qualified operation_ref", "[integration]")
{
  Fixture f;
  auto s = f.attach();
  // Unknown above high-water; outcome_unavailable below it.
  CHECK(f.status(s, s.id, 5).errorCode() == "unknown");
  f.tool(s, "vsr_discover", "{\"request_id\":5}");
  CHECK(f.status(s, s.id, 4).errorCode() == "outcome_unavailable");
  CHECK(f.tool(s, "vsr_operation_status", "{\"operation_ref\":{}}").errorCode()
      == "invalid_request");

  // Cancel queued work: terminal cancelled, never started.
  auto c = f.tool(s, "vsr_cancel", "{\"operation_ref\":" + ref(s.id, 5) + "}");
  CHECK(c.str("kind") == "discover");
  CHECK(c.str("outcome") == "cancelled");
  CHECK(c.isError());
  f.drain();
  CHECK(f.status(s, s.id, 5).str("outcome") == "cancelled");

  // Own receipt without read on a listed target: minimal status, no payload.
  // (Discovery covers exactly the targets its page lists; addendum 04.)
  f.tool(s, "vsr_discover", "{\"request_id\":6}");
  f.drain();
  f.authority->restrict(
      session::Target::scene(f.sceneId), session::Capability::READ);
  auto own = f.status(s, s.id, 6);
  CHECK(own.str("kind") == "discover");
  CHECK(!own.flag("result_visible"));
  CHECK(own.result() == nullptr);
  f.authority->relax(
      session::Target::scene(f.sceneId), session::Capability::READ);
}

SCENARIO(
    "MCP lookup across sessions keeps kind and read gates", "[integration]")
{
  Fixture f;
  auto s1 = f.attach();
  f.tool(s1,
      "vsr_inspect",
      "{\"request_id\":1,\"scene_id\":" + std::to_string(f.sceneId) + "}");
  f.tool(s1,
      "vsr_inspect",
      "{\"request_id\":2,\"scene_id\":" + std::to_string(f.sceneId) + "}");
  f.drain();
  REQUIRE(f.rpc("vsr/detach", "{}", &s1).status == 200);

  auto s2 = f.attach();
  // Retained kind and result visible with current read, under new proof.
  auto r = f.status(s2, s1.id, 1);
  CHECK(r.str("kind") == "inspect");
  CHECK(r.str("outcome") == "ok");
  CHECK(r.flag("result_visible"));
  CHECK(r.sc()->get("operation_ref")->get("session_id")->s == s1.id);
  // Old proof cannot look anything up.
  CHECK(f.status(s1, s1.id, 1).errorCode() == "invalid_session_proof");
  // Foreign unknown: outcome_unavailable; foreign cancel: forbidden.
  CHECK(f.status(s2, s1.id, 99).errorCode() == "outcome_unavailable");
  CHECK(f.tool(s2, "vsr_cancel", "{\"operation_ref\":" + ref(s1.id, 2) + "}")
            .errorCode()
      == "forbidden");
  // Withdrawn read: whole foreign record denied, no partial payload.
  f.authority->restrict(
      session::Target::scene(f.sceneId), session::Capability::READ);
  auto denied = f.status(s2, s1.id, 1);
  CHECK(denied.status == 403);
  CHECK(denied.errorCode() == "forbidden");
}

SCENARIO(
    "MCP generic edits use inspected object tokens and restore typed values",
    "[integration][generic-edit][mcp]")
{
  Fixture f({}, true);
  auto pbr = f.scene.createObject<vsr::scene::Material>("physicallyBased");
  auto camera = f.scene.createObject<vsr::scene::Camera>("perspective");
  auto point = f.scene.createObject<vsr::scene::Light>("point");
  pbr->setName("generic-pbr");
  camera->setName("generic-camera");
  point->setName("generic-point");
  const std::int32_t count = 3;
  camera->setParameter("count", ANARI_INT32, &count);
  const auto nearBefore = camera->parameter("near")->value();
  auto s = f.attach();
  auto inspectTarget = [&](std::uint64_t id, const char *name) {
    REQUIRE(
        f.tool(s,
             "vsr_inspect",
             "{\"request_id\":" + std::to_string(id)
                 + ",\"scene_id\":" + std::to_string(f.sceneId)
                 + ",\"name\":" + json::quote(name) + "}")
            .errorCode()
            .empty());
    f.drain();
    auto st = f.status(s, s.id, id);
    REQUIRE(st.str("outcome") == "ok");
    REQUIRE(st.result());
    const auto &items = st.result()->get("items")->a;
    REQUIRE(items.size() == 1);
    return "\"handle\":" + json::quote(items[0].get("handle")->s)
        + ",\"version\":"
        + std::to_string(std::uint64_t(items[0].get("version")->n));
  };
  const auto pbrTarget = inspectTarget(1, "generic-pbr");
  const auto cameraTarget = inspectTarget(2, "generic-camera");
  const auto pointTarget = inspectTarget(3, "generic-point");
  auto entry =
      [](const std::string &target, const char *parameter, const char *value) {
        return "{" + target + ",\"parameter\":" + json::quote(parameter)
            + ",\"value\":" + value + "}";
      };
  const auto entries = entry(pbrTarget, "roughness", "0.25") + ","
      + entry(cameraTarget, "near", "2") + ","
      + entry(cameraTarget, "stereoMode", "\"left\"") + ","
      + entry(cameraTarget, "count", "-7") + ","
      + entry(pointTarget, "visible", "false") + ","
      + entry(pointTarget, "intensity", "7");
  auto args = [&](std::uint64_t id) {
    return "{\"request_id\":" + std::to_string(id) + ",\"scene_id\":"
        + std::to_string(f.sceneId) + ",\"edits\":[" + entries + "]}";
  };
  REQUIRE(f.tool(s, "vsr_edit", args(4)).str("state") == "pending");
  f.drain();
  const auto edited = f.status(s, s.id, 4);
  REQUIRE(edited.str("outcome") == "ok");
  REQUIRE(edited.flag("committed"));
  REQUIRE(edited.result());
  REQUIRE(edited.result()->get("batch_ref"));
  const auto batch = *edited.result()->get("batch_ref");
  REQUIRE(edited.result()->get("versions")->a.size() == 3);
  CHECK(*pbr->parameterValueAs<float>("roughness") == 0.25f);
  CHECK(*camera->parameterValueAs<float>("near") == 2.f);
  CHECK_FALSE(camera->parameter("near")->isEnabled());
  CHECK(camera->parameter("stereoMode")->value().getString() == "left");
  CHECK(camera->parameter("count")->value().type() == ANARI_INT32);
  CHECK(camera->parameter("count")->value().get<std::int32_t>() == -7);
  CHECK_FALSE(point->parameter("visible")->value().get<bool>());
  CHECK(*point->parameterValueAs<float>("intensity") == 7.f);
  CHECK(f.tool(s, "vsr_edit", args(4)).flag("replayed"));
  const auto revision = f.sceneIds->sceneRevision();
  REQUIRE(f.tool(s, "vsr_edit", args(5)).errorCode().empty());
  f.drain();
  const auto stale = f.status(s, s.id, 5);
  CHECK(stale.str("outcome") == "conflict");
  CHECK_FALSE(stale.flag("committed"));
  CHECK(f.sceneIds->sceneRevision() == revision);
  REQUIRE(f.restoreBatch(s, 6, batch).errorCode().empty());
  f.drain();
  const auto restored = f.status(s, s.id, 6);
  REQUIRE(restored.str("outcome") == "ok");
  CHECK(restored.flag("committed"));
  CHECK(*pbr->parameterValueAs<float>("roughness") == 1.f);
  CHECK(camera->parameter("near")->value() == nearBefore);
  CHECK_FALSE(camera->parameter("near")->isEnabled());
  CHECK(camera->parameter("stereoMode")->value().getString() == "none");
  CHECK(camera->parameter("count")->value().type() == ANARI_INT32);
  CHECK(camera->parameter("count")->value().get<std::int32_t>() == 3);
  CHECK(point->parameter("visible")->value().get<bool>());
  CHECK(*point->parameterValueAs<float>("intensity") == 1.f);
}

SCENARIO(
    "MCP invalid generic values cannot partially write or create parameters",
    "[integration][generic-edit][mcp]")
{
  Fixture f({}, true);
  auto s = f.attach();
  const auto target = f.firstTarget(s, 1);
  auto camera = f.scene.createObject<vsr::scene::Camera>("perspective");
  camera->setName("generic-invalid-camera");
  const std::uint8_t count = 3;
  camera->setParameter("count", ANARI_UINT8, &count);
  const std::uint64_t wideCount = 3;
  camera->setParameter("wideCount", ANARI_UINT64, &wideCount);
  auto array = f.scene.createArrayProxy(ANARI_FLOAT32, 8);
  camera->setParameterObject("input", *array);
  const auto inputBefore = camera->parameter("input")->value();
  // The camera target is also acquired through the wire inspection API.
  REQUIRE(
      f.tool(s,
           "vsr_inspect",
           "{\"request_id\":2,\"scene_id\":" + std::to_string(f.sceneId)
               + ",\"name\":\"generic-invalid-camera\"}")
          .errorCode()
          .empty());
  f.drain();
  const auto inspected = f.status(s, s.id, 2);
  REQUIRE(inspected.str("outcome") == "ok");
  REQUIRE(inspected.result());
  REQUIRE(inspected.result()->get("items")->a.size() == 1);
  const auto *item = &inspected.result()->get("items")->a[0];
  const auto cameraTarget = "\"handle\":" + json::quote(item->get("handle")->s)
      + ",\"version\":"
      + std::to_string(std::uint64_t(item->get("version")->n));
  const auto version =
      *f.sceneIds->parameterVersion(f.sceneIds->handleOf(camera.data()));
  const auto revision = f.sceneIds->sceneRevision();
  std::uint64_t id = 2;
  for (const auto &bad :
      std::vector<std::pair<std::string, std::string>>{{"count", "-1"},
          {"count", "256"},
          {"count", "1.5"},
          {"wideCount", "9007199254740992"},
          {"count", "true"},
          {"count", "null"},
          {"count", "[1]"},
          {"stereoMode", "\"invalid\""},
          {"stereoMode", "\"left\\u0000right\""},
          {"input", "0"},
          {"input", "{\"handle\":\"unsupported\"}"},
          {"missing", "1"}}) {
    CAPTURE(bad.first, bad.second);
    auto ack = f.tool(s,
        "vsr_edit",
        "{\"request_id\":" + std::to_string(++id)
            + ",\"scene_id\":" + std::to_string(f.sceneId) + ",\"edits\":[{"
            + target + ",\"parameter\":\"opacity\",\"value\":0.5},{"
            + cameraTarget + ",\"parameter\":" + json::quote(bad.first)
            + ",\"value\":" + bad.second + "}]}");
    REQUIRE(ack.errorCode().empty());
    f.drain();
    const auto rejected = f.status(s, s.id, id);
    CHECK(rejected.str("outcome") == "invalid_request");
    CHECK_FALSE(rejected.flag("committed"));
    CHECK(f.opacity0() == 1.f);
    CHECK(camera->parameter("count")->value().get<std::uint8_t>() == 3);
    CHECK(camera->parameter("wideCount")->value().get<std::uint64_t>() == 3);
    CHECK(camera->parameter("stereoMode")->value().getString() == "none");
    CHECK_FALSE(camera->parameter("missing"));
    CHECK(camera->parameter("input")->value() == inputBefore);
    CHECK(f.sceneIds->isCurrent(version));
    CHECK(f.sceneIds->sceneRevision() == revision);
    CHECK(f.edits->restorations().size() == 0);
  }
}

SCENARIO("MCP edit and restore tools carry qualified refs and commit state",
    "[integration]")
{
  Fixture f({}, true);
  auto s = f.attach();
  const auto target = f.firstTarget(s, 1);

  auto ack = f.editOpacity(s, 2, target, 0.5);
  REQUIRE(ack.status == 200);
  CHECK(ack.sc()->get("operation_ref")->get("session_id")->s == s.id);
  CHECK(ack.sc()->get("operation_ref")->get("request_id")->n == 2);
  CHECK(ack.str("kind") == "edit");
  CHECK(ack.str("state") == "pending");
  f.drain();
  auto st = f.status(s, s.id, 2);
  REQUIRE(st.str("outcome") == "ok");
  CHECK(st.flag("committed"));
  CHECK(!st.isError());
  const json::Value batchRef = *st.result()->get("batch_ref");
  CHECK(batchRef.get("launch_id")->s == f.authority->launchId());
  CHECK(batchRef.get("batch_id")->s.size() == 32);
  CHECK(f.opacity0() == 0.5f);
  // Addendum 04 §6/§8: committed, scene_revision and the new versions.
  CHECK(st.result()->get("committed")->b);
  CHECK(st.result()->get("scene_revision")->s
      == token(f.sceneIds->sceneRevision()));
  REQUIRE(st.result()->get("versions")->a.size() == 1);
  const auto &nv = st.result()->get("versions")->a[0];
  CHECK(target.find(nv.get("handle")->s) != std::string::npos);
  CHECK(std::uint64_t(nv.get("version")->n)
      > std::uint64_t(std::stoull(target.substr(target.rfind(':') + 1))));

  // Restoration has its own operation_ref and names the batch explicitly.
  ack = f.restoreBatch(s, 3, batchRef);
  CHECK(ack.str("kind") == "restore");
  CHECK(ack.sc()->get("operation_ref")->get("request_id")->n == 3);
  f.drain();
  st = f.status(s, s.id, 3);
  REQUIRE(st.str("outcome") == "ok");
  CHECK(st.flag("committed"));
  CHECK(json::write(*st.result()->get("target_batch_ref"))
      == json::write(batchRef));
  CHECK(st.result()->get("committed")->b);
  CHECK(st.result()->get("scene_revision")->s
      == token(f.sceneIds->sceneRevision()));
  CHECK(st.result()->get("versions")->a.size() == 1);
  CHECK(f.opacity0() == 1.f);

  // Same ID replays; a new ID reports already_restored without mutation.
  CHECK(f.restoreBatch(s, 3, batchRef).flag("replayed"));
  f.restoreBatch(s, 4, batchRef);
  f.drain();
  st = f.status(s, s.id, 4);
  CHECK(st.str("kind") == "restore");
  CHECK(st.str("outcome") == "already_restored");
  CHECK(!st.flag("committed"));
  CHECK(st.isError());

  // Unknown or wrong-launch target: outcome_unavailable, never another batch.
  json::Value other = batchRef;
  other.o["launch_id"].s = std::string(32, '0');
  f.restoreBatch(s, 5, other);
  f.drain();
  CHECK(f.status(s, s.id, 5).str("outcome") == "outcome_unavailable");
  CHECK(!f.status(s, s.id, 5).flag("committed"));
  CHECK(f.status(s, s.id, 5).result() == nullptr); // no reason (addendum 04)
}

SCENARIO("MCP edit and restore map conflict, sync failure and envelopes",
    "[integration]")
{
  Fixture f({}, true);
  auto s = f.attach();
  const auto target = f.firstTarget(s, 1);

  // Stale expected version: edit conflict, no mutation.
  auto stale = target.substr(0, target.rfind(':') + 1) + "999";
  f.editOpacity(s, 2, stale, 0.25);
  f.drain();
  auto st = f.status(s, s.id, 2);
  CHECK(st.str("kind") == "edit");
  CHECK(st.str("outcome") == "conflict");
  CHECK(!st.flag("committed"));
  CHECK(st.isError());
  CHECK(f.opacity0() == 1.f);

  // Sync hook fails: committed, applied_with_sync_failure, batch_ref kept.
  f.hookOk = false;
  f.editOpacity(s, 3, target, 0.25);
  f.drain();
  st = f.status(s, s.id, 3);
  CHECK(st.str("outcome") == "applied_with_sync_failure");
  CHECK(st.flag("committed"));
  CHECK(st.isError());
  REQUIRE(st.result());
  const json::Value batchRef = *st.result()->get("batch_ref");
  CHECK(f.opacity0() == 0.25f);
  f.hookOk = true;

  // Object changed after the batch: restore conflict, batch now conflicting.
  f.scene.getObject<vsr::scene::Material>(0)->setParameter("opacity", 0.75f);
  f.restoreBatch(s, 4, batchRef);
  f.drain();
  st = f.status(s, s.id, 4);
  CHECK(st.str("kind") == "restore");
  CHECK(st.str("outcome") == "conflict");
  CHECK(!st.flag("committed"));
  CHECK(f.restores->conflicting(edit::BatchRef{
      batchRef.get("launch_id")->s, batchRef.get("batch_id")->s}));

  // Value rejection: failed_entry and value_error on the wire.
  f.editOpacity(s, 6, f.firstTarget(s, 5), 2.0);
  f.drain();
  st = f.status(s, s.id, 6);
  CHECK(st.str("outcome") == "invalid_request");
  CHECK(!st.flag("committed"));
  REQUIRE(st.result());
  CHECK(st.result()->get("failed_entry")->n == 0);
  CHECK(st.result()->get("value_error")->s == "out_of_domain");

  // Envelope errors reject before admission and consume no ID.
  const auto hw = f.engine->highWater(s.id);
  const std::string scene = std::to_string(f.sceneId);
  for (const std::string bad :
      {"{\"request_id\":9,\"scene_id\":" + scene + ",\"edits\":[]}",
          "{\"request_id\":9,\"scene_id\":" + scene
              + ",\"edits\":[{\"handle\":\"zz\",\"version\":1,"
                "\"parameter\":\"opacity\",\"value\":1}]}",
          "{\"request_id\":9,\"edits\":[{" + target
              + ",\"parameter\":\"opacity\",\"value\":1}]}"})
    CHECK(f.tool(s, "vsr_edit", bad).errorCode() == "invalid_request");
  CHECK(f.tool(s,
             "vsr_restore",
             "{\"request_id\":9,\"scene_id\":" + scene
                 + ",\"target_batch_ref\":{\"batch_id\":\"x\"}}")
            .errorCode()
      == "invalid_request");
  CHECK(f.engine->highWater(s.id) == hw);

  // Without the edit grant: forbidden before admission.
  Fixture ro;
  auto rs = ro.attach();
  auto r = ro.editOpacity(rs, 2, ro.firstTarget(rs, 1), 0.5);
  CHECK(r.status == 403);
  CHECK(r.errorCode() == "forbidden");
}

namespace {

// Real helide device; absence FAILS (spec §14), never skips.
struct Helide
{
  Helide()
  {
    library = anari::loadLibrary("helide",
        [](const void *,
            ANARIDevice,
            ANARIObject,
            anari::DataType,
            ANARIStatusSeverity,
            ANARIStatusCode,
            const char *) {});
    if (library)
      device = anari::newDevice(library, "default");
  }
  ~Helide()
  {
    if (device)
      anari::release(device, device);
    if (library)
      anari::unloadLibrary(library);
  }
  anari::Library library{nullptr};
  anari::Device device{nullptr};
};

struct HelideBackend : render::RenderBackend
{
  anari::Device d{nullptr};
  vsr::rendering::RenderIndexAllLayers *index{nullptr};
  size_t cameraIndex{0};
  anari::Renderer renderer{nullptr};
  anari::Frame frame{nullptr};
  bool mapped{false};
  int holdPolls{0};
  bool allowRelease{true};
  int begins{0};
  // Device selection model: empty candidates opts out (the default).
  std::vector<std::string> candidates;
  std::string current{"helide"};
  bool failSelect{false};

  std::vector<std::string> deviceCandidates() const override
  {
    return candidates;
  }
  std::string deviceIdentifier() const override
  {
    return current;
  }
  bool selectDevice(const std::string &id) override
  {
    if (failSelect)
      return false;
    current = id;
    return true;
  }
  // Camera control model: off by default, like device selection.
  bool controllable{false};
  int cameraSets{0};
  std::array<double, 3> eye{0., 0., 0.};
  double fovy{60.};
  std::array<double, 3> cameraDirection{0., 0., -1.};
  std::array<double, 3> cameraUp{0., 1., 0.};
  std::optional<EffectiveCamera> cameraState() const override
  {
    EffectiveCamera camera;
    camera.position = eye;
    camera.direction = cameraDirection;
    camera.up = cameraUp;
    camera.fovyDegrees = fovy;
    camera.projection = "perspective";
    return camera;
  }
  bool cameraControllable() const override
  {
    return controllable;
  }
  bool setCamera(const std::array<double, 3> &position,
      const std::array<double, 3> &direction,
      const std::array<double, 3> &up,
      double fovyDegrees) override
  {
    ++cameraSets;
    eye = position;
    cameraDirection = direction;
    cameraUp = up;
    if (fovyDegrees > 0.)
      fovy = fovyDegrees;
    return true;
  }

  bool begin(std::uint32_t w, std::uint32_t h) override
  {
    ++begins;
    frame = anari::newObject<anari::Frame>(d);
    anari::setParameter(d, frame, "size", vsr::math::uint2(w, h));
    anari::setParameter(d, frame, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
    anari::setParameter(d, frame, "world", index->world());
    anari::setParameter(d, frame, "renderer", renderer);
    auto cam = index->camera(cameraIndex);
    anari::setParameter(d, cam, "aspect", float(w) / float(h));
    anari::commitParameters(d, cam);
    anari::setParameter(d, frame, "camera", cam);
    anari::commitParameters(d, frame);
    anari::render(d, frame);
    return true;
  }
  render::BackendState poll() override
  {
    if (!anari::isReady(d, frame))
      return render::BackendState::PENDING;
    if (holdPolls > 0) {
      --holdPolls;
      return render::BackendState::PENDING;
    }
    return render::BackendState::DONE;
  }
  image::SourceImage image() override
  {
    auto fb = anari::map<std::uint32_t>(d, frame, "channel.color");
    mapped = true;
    image::SourceImage src;
    src.width = fb.width;
    src.height = fb.height;
    src.encoding = image::ChannelEncoding::SRGB_UINT8;
    src.alpha = image::AlphaMode::STRAIGHT;
    src.origin = image::RowOrigin::BOTTOM_LEFT;
    src.pixels = fb.data;
    return src;
  }
  bool release() override
  {
    if (!allowRelease)
      return false;
    if (frame) {
      anari::wait(d, frame);
      if (mapped)
        anari::unmap(d, frame, "channel.color");
      anari::release(d, frame);
    }
    frame = nullptr;
    mapped = false;
    return true;
  }
};

std::vector<std::uint8_t> unbase64(const std::string &s)
{
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z')
      return c - 'A';
    if (c >= 'a' && c <= 'z')
      return c - 'a' + 26;
    if (c >= '0' && c <= '9')
      return c - '0' + 52;
    return c == '+' ? 62 : c == '/' ? 63 : -1;
  };
  std::vector<std::uint8_t> out;
  std::uint32_t acc = 0;
  int bits = 0;
  for (char c : s) {
    const int v = val(c);
    if (v < 0)
      continue;
    acc = (acc << 6) | std::uint32_t(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(std::uint8_t(acc >> bits));
    }
  }
  return out;
}

std::uint32_t be32(const std::vector<std::uint8_t> &b, std::size_t at)
{
  return (std::uint32_t(b[at]) << 24) | (std::uint32_t(b[at + 1]) << 16)
      | (std::uint32_t(b[at + 2]) << 8) | b[at + 3];
}

// The MCP image content item of a reply, or nullptr.
const json::Value *imageItem(const Reply &r)
{
  const auto *res = r.json.get("result");
  const auto *content = res ? res->get("content") : nullptr;
  if (!content)
    return nullptr;
  for (const auto &c : content->a)
    if (c.get("type") && c.get("type")->s == "image")
      return &c;
  return nullptr;
}

struct ThrowCameraNotifications : vsr::scene::EmptyUpdateDelegate
{
  bool fail{true};
  void signalParameterBatchUpdated(const vsr::scene::Object *object,
      const std::vector<const vsr::scene::Parameter *> &) override;
};

void ThrowCameraNotifications::signalParameterBatchUpdated(
    const vsr::scene::Object *object,
    const std::vector<const vsr::scene::Parameter *> &)
{
  if (fail && object->type() == ANARI_CAMERA)
    throw std::runtime_error("test downstream notification failure");
}

struct RenderFixture
{
  Helide helide;
  vsr::scene::Scene scene;
  vsr::scene::CameraRef camera;
  Integration integration;
  SceneId sceneId{INVALID_SCENE_ID};
  ViewId viewId{INVALID_VIEW_ID};
  double now{1000.0};
  bool recoverOk{true};
  std::unique_ptr<session::SessionAuthority> authority;
  std::unique_ptr<operations::OperationEngine> engine;
  identity::IdentityAuthority ids;
  std::unique_ptr<identity::SceneIdentity> identity;
  std::unique_ptr<identity::ViewRevisionTracker> tracker;
  sync::SyncStates states;
  std::unique_ptr<sync::SyncService> sync;
  activity::ActivityLog log;
  HelideBackend backend;
  std::unique_ptr<render::RenderService> renders;
  std::unique_ptr<frame::FrameService> frames;
  std::unique_ptr<inspection::Inspector> inspector;
  TempDir tmp;
  descriptor::RuntimeDirectory dir;
  std::unique_ptr<mcp::McpServer> server;

  RenderFixture(bool exposeFrameTools = false)
  {
    REQUIRE(helide.device != nullptr); // missing helide fails, never skips
    using namespace vsr::scene;
    namespace tokens = vsr::scene::tokens;
    auto root = scene.defaultLayer()->root();
    auto sphere = scene.createObject<Geometry>(tokens::geometry::sphere);
    auto pos = scene.createArray(ANARI_FLOAT32_VEC3, 1);
    const vsr::math::float3 c(0.f, 0.f, 0.f);
    pos->setData(&c, 1);
    sphere->setParameterObject("vertex.position", *pos);
    sphere->setParameter("radius", 1.f);
    auto mat = scene.createObject<Material>(tokens::material::matte);
    scene.insertChildObjectNode(
        root, scene.createSurface("sphere", sphere, mat));
    auto light = scene.createObject<Light>(tokens::light::directional);
    scene.insertChildObjectNode(root, light);
    camera = scene.createObject<Camera>(tokens::camera::perspective);
    camera->setParameter("position", vsr::math::float3(0.f, 0.f, 5.f));
    camera->setParameter("direction", vsr::math::float3(0.f, 0.f, -1.f));
    camera->setParameter("up", vsr::math::float3(0.f, 1.f, 0.f));

    SceneRegistration sr;
    sr.name = "fixture";
    sr.scene = &scene;
    sr.grants.read = true;
    sr.grants.resynchronize = true;
    sr.recoveryCallback = [this](SceneId) { return recoverOk; };
    sceneId = *integration.registerScene(sr);
    ViewRegistration vr;
    vr.name = "main";
    vr.scene = sceneId;
    vr.grants = {true, true};
    viewId = *integration.registerView(vr);
    REQUIRE(integration.activate().ok());

    authority =
        session::SessionAuthority::create(integration, [this] { return now; });
    engine = operations::OperationEngine::create(
        integration, *authority, {}, [this] { return now; });
    identity = std::make_unique<identity::SceneIdentity>(ids, scene);
    tracker = std::make_unique<identity::ViewRevisionTracker>(ids, *identity);
    tracker->setCamera(identity->handleOf(camera.data()));
    sync = std::make_unique<sync::SyncService>(integration, *engine, states);

    auto *index =
        scene.updateDelegate().emplace<vsr::rendering::RenderIndexAllLayers>(
            scene, vsr::core::Token("helide"), helide.device);
    index->populate();
    backend.d = helide.device;
    backend.index = index;
    backend.cameraIndex = camera->index();
    backend.renderer =
        anari::newObject<anari::Renderer>(helide.device, "default");
    anari::commitParameters(helide.device, backend.renderer);

    renders = std::make_unique<render::RenderService>(
        integration, *engine, *sync, log);
    REQUIRE(renders->attachView(viewId, *identity, *tracker, backend));
    renders->setDimensions(viewId, 64, 48);
    inspector = std::make_unique<inspection::Inspector>(
        integration, *authority, [this](SceneId id) {
          return id == sceneId ? identity.get() : nullptr;
        });
    inspector->setViewRevisions(
        [this](ViewId id) { return id == viewId ? tracker.get() : nullptr; });

    REQUIRE(descriptor::openDirectory(tmp.path, {}, dir).ok());
    mcp::Config cfg;
    mcp::tools::addQueryTools(cfg.tools, {*engine, *inspector});
    mcp::tools::addLookupTools(cfg.tools, *engine);
    mcp::tools::addRenderTools(cfg.tools, {*renders, *sync, *engine});
    if (exposeFrameTools) {
      frames = std::make_unique<frame::FrameService>(
          integration, *authority, *engine, [this] { return now; });
      mcp::tools::addFrameTools(cfg.tools, {*frames, *engine});
    }
    const auto st =
        mcp::McpServer::start(cfg, *authority, *engine, dir, server);
    if (!st.ok())
      FAIL("start failed: " << st.reason);
  }

  ~RenderFixture()
  {
    server.reset();
    frames.reset();
    backend.allowRelease = true;
    backend.release();
    renders.reset();
    if (backend.renderer)
      anari::release(helide.device, backend.renderer);
    sync.reset();
    inspector.reset();
    tracker.reset();
    identity.reset();
    scene.updateDelegate().clear();
  }

  Reply tool(const Session &s, const std::string &name, const std::string &args)
  {
    std::vector<std::string> h{"Host: " + server->authority(),
        "Authorization: Bearer " + authority->launchCredential(),
        "VSR-Session-ID: " + s.id,
        "VSR-Session-Proof: Bearer " + s.secret};
    return post(server->port(),
        h,
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":"
        "{\"name\":\""
            + name + "\",\"arguments\":" + args + "}}");
  }

  Session attach()
  {
    auto at = authority->attach(authority->launchCredential());
    REQUIRE(at.code == session::Code::OK);
    return {at.sessionId, at.sessionSecret};
  }

  std::string renderArgs(std::uint64_t id, const std::string &extra = "")
  {
    return "{\"request_id\":" + std::to_string(id)
        + ",\"view_id\":" + std::to_string(viewId) + ",\"scene_revision\":\""
        + token(identity->sceneRevision()) + "\",\"view_revision\":\""
        + token(tracker->revision()) + "\"" + extra + "}";
  }

  // Services safe points until `id` is terminal with no slot held.
  Reply run(const Session &s, const std::string &toolName, std::uint64_t id)
  {
    Reply r;
    for (int i = 0; i < 100000; ++i) {
      integration.service();
      renders->pump();
      r = tool(s, toolName, "{\"operation_ref\":" + ref(s.id, id) + "}");
      if (r.str("state") == "terminal" && !r.sc()->get("progress"))
        break;
    }
    return r;
  }
};

} // namespace

SCENARIO(
    "MCP presented CPU frames cross the real loopback protocol without legacy rendering",
    "[integration][presented-frame][mcp]")
{
  RenderFixture f(true);
  auto s = f.attach();
  vsr::rendering::ImagePipeline pipeline(
      4, 2, vsr::rendering::ImageStorage::HOST);
  std::vector<std::uint8_t> naturalPixels;
  struct HostCopyPass : vsr::rendering::ImageSource
  {
    std::vector<std::uint8_t> *pixels{};
    void render(vsr::rendering::ImageBuffers &b) override
    {
      std::memcpy(b.color, pixels->data(), pixels->size());
    }
  };
  auto *copy = pipeline.setSource<HostCopyPass>();
  copy->pixels = &naturalPixels;
  identity::ViewRevisionTracker tracker(f.ids, *f.identity);
  ViewId cpu = INVALID_VIEW_ID;
  REQUIRE(f.integration.submit(f.sceneId, [&](vsr::scene::Scene &) {
    ViewRegistration registration;
    registration.scene = f.sceneId;
    registration.grants.read =
        true; // render permission is intentionally absent
    registration.role = ViewRole::HEADLESS;
    registration.observeBinding = [&] {
      EffectiveViewBinding b;
      b.width = 4;
      b.height = 2;
      b.displayWidth = 8.;
      b.displayHeight = 4.;
      b.deviceIdentifier = "helide";
      b.deviceSubtype = "default";
      b.cameraHandle = f.identity->handleOf(f.camera.data());
      b.camera.position = {0., 0., 5.};
      b.camera.fovyDegrees = *f.camera->parameterValueAs<float>("fovy")
          * (180. / 3.141592653589793);
      b.rendererSubtype = "default";
      b.rendererConfigurationRevision = "bound_backend_renderer_1";
      b.rendererSettingsReason = "backend_defaults_not_observed";
      b.pipelineSource = "CopyFromNaturalHelide";
      b.pipelineConfigurationRevision = "1";
      b.outputColor = "rgba8_srgb";
      return std::optional<EffectiveViewBinding>(b);
    };
    cpu = *f.integration.registerView(std::move(registration));
    REQUIRE(f.frames->attachView(cpu, *f.identity, tracker));
  }));
  f.integration.service();
  const auto args = [&](std::uint64_t id) {
    return "{\"request_id\":" + std::to_string(id)
        + ",\"view_id\":" + std::to_string(cpu) + "}";
  };
  CHECK(f.tool(s, "vsr_view_frame", "{\"request_id\":1}").errorCode()
      == "invalid_request");
  CHECK(f.engine->highWater(s.id) == 0);
  REQUIRE(f.tool(s, "vsr_view_frame", args(1)).str("kind") == "view_frame");
  f.integration.service();
  auto closed = f.tool(
      s, "vsr_view_frame_image", "{\"operation_ref\":" + ref(s.id, 1) + "}");
  REQUIRE(closed.result());
  CHECK(closed.result()->get("reason")->s == "unexposed_source");
  CHECK_FALSE(imageItem(closed));
  REQUIRE(f.frames->setExposed(cpu, true));
  auto ack = f.tool(s, "vsr_view_frame", args(2));
  CHECK(ack.str("state") == "pending");
  f.integration.service();
  auto preparing = f.tool(
      s, "vsr_view_frame_image", "{\"operation_ref\":" + ref(s.id, 2) + "}");
  CHECK(preparing.str("state") == "pending");
  CHECK_FALSE(imageItem(preparing));
  CHECK_FALSE(preparing.result());
  REQUIRE(f.integration.submit(f.sceneId, [&](vsr::scene::Scene &) {
    auto proof =
        f.frames->beginProduction(cpu, pipeline, image::RowOrigin::BOTTOM_LEFT);
    REQUIRE(f.backend.begin(4, 2)); // host's natural producer, not an MCP fetch
    anari::wait(f.helide.device, f.backend.frame);
    auto source = f.backend.image();
    naturalPixels.resize(4 * 2 * 4);
    std::memcpy(naturalPixels.data(), source.pixels, naturalPixels.size());
    REQUIRE(f.backend.release());
    pipeline.render();
    REQUIRE(f.frames->present(cpu,
        pipeline,
        std::move(proof),
        [](const std::uint32_t *p, std::uint32_t w, std::uint32_t h) {
          auto *surface =
              SDL_CreateSurface(w * 2, h * 2, SDL_PIXELFORMAT_RGBA32);
          REQUIRE(surface);
          auto *renderer = SDL_CreateSoftwareRenderer(surface);
          REQUIRE(renderer);
          auto *texture = SDL_CreateTexture(renderer,
              SDL_PIXELFORMAT_RGBA32,
              SDL_TEXTUREACCESS_STREAMING,
              w,
              h);
          REQUIRE(texture);
          REQUIRE(SDL_UpdateTexture(texture, nullptr, p, w * 4));
          REQUIRE(SDL_RenderTextureRotated(renderer,
              texture,
              nullptr,
              nullptr,
              0.,
              nullptr,
              SDL_FLIP_VERTICAL));
          const bool accepted = SDL_RenderPresent(renderer);
          SDL_DestroyTexture(texture);
          SDL_DestroyRenderer(renderer);
          SDL_DestroySurface(surface);
          return accepted;
        }));
  }));
  f.integration.service();
  const auto starts = f.renders->backendStarts();
  CHECK(f.engine->highWater(s.id) == 2); // no second frame request
  Reply done;
  for (int i = 0; i < 2000; ++i) {
    REQUIRE(f.integration.submit(
        f.sceneId, [&](vsr::scene::Scene &) { f.frames->pump(); }));
    f.integration.service();
    done = f.tool(
        s, "vsr_view_frame_image", "{\"operation_ref\":" + ref(s.id, 2) + "}");
    if (done.str("state") == "terminal")
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(done.str("outcome") == "ok");
  REQUIRE(imageItem(done));
  CHECK(done.result()->get("image_kind")->s == "presented_frame");
  CHECK(
      done.result()->get("rendered_configuration")->get("device_identifier")->s
      == "helide");
  const auto png = unbase64(imageItem(done)->get("data")->s);
  CHECK(be32(png, 16) == 4);
  CHECK(be32(png, 20) == 2);
  auto replay = f.tool(s, "vsr_view_frame", args(2));
  CHECK(replay.flag("replayed"));
  CHECK(replay.result()->get("frame_identity")->s
      == done.result()->get("frame_identity")->s);
  CHECK_FALSE(imageItem(f.tool(
      s, "vsr_render_image", "{\"operation_ref\":" + ref(s.id, 2) + "}")));
  CHECK(f.renders->backendStarts() == starts);
  f.authority->restrict(
      session::Target::scene(f.sceneId), session::Capability::READ);
  const auto withdrawn = f.tool(
      s, "vsr_view_frame_image", "{\"operation_ref\":" + ref(s.id, 2) + "}");
  CHECK_FALSE(imageItem(withdrawn));
  CHECK_FALSE(withdrawn.result());
  REQUIRE(f.integration.submit(f.sceneId,
      [&](vsr::scene::Scene &) { f.integration.unregisterView(cpu); }));
  f.integration.service();
}

SCENARIO(
    "MCP vsr_select_device overrides a view's render device", "[integration]")
{
  RenderFixture f;
  auto s = f.attach();
  auto args = [&](std::uint64_t id, const std::string &device) {
    return "{\"request_id\":" + std::to_string(id)
        + ",\"view_id\":" + std::to_string(f.viewId)
        + (device.empty() ? std::string()
                          : ",\"device_identifier\":\"" + device + "\"")
        + "}";
  };

  // A backend without selection rejects every request before admission.
  CHECK(f.tool(s, "vsr_select_device", args(1, "visrtx")).errorCode()
      == "invalid_request");
  CHECK(f.engine->highWater(s.id) == 0);

  f.backend.candidates = {"barney", "visrtx", "helide"};

  // Unlisted identifiers and an empty identifier reject before admission.
  CHECK(f.tool(s, "vsr_select_device", args(1, "ospray")).errorCode()
      == "invalid_request");
  CHECK(f.tool(s,
             "vsr_select_device",
             "{\"request_id\":1,\"view_id\":" + std::to_string(f.viewId)
                 + ",\"device_identifier\":\"\"}")
            .errorCode()
      == "invalid_request");
  CHECK(f.engine->highWater(s.id) == 0);

  // Omitting the identifier only reports; the view revision is unchanged.
  const auto before = token(f.tracker->revision());
  f.tool(s, "vsr_select_device", args(1, ""));
  auto query = f.run(s, "vsr_operation_status", 1);
  REQUIRE(query.str("outcome") == "ok");
  CHECK_FALSE(query.result()->get("applied")->b);
  CHECK(query.result()->get("device_identifier")->s == "helide");
  CHECK(query.result()->get("candidates")->a.size() == 3);
  CHECK(token(f.tracker->revision()) == before);

  // A switch commits, advances the view revision and supersedes renders
  // pinned to the old one.
  const std::string stale = f.renderArgs(4);
  f.tool(s, "vsr_select_device", args(2, "visrtx"));
  auto sw = f.run(s, "vsr_operation_status", 2);
  REQUIRE(sw.str("outcome") == "ok");
  CHECK(sw.flag("committed"));
  CHECK(sw.result()->get("applied")->b);
  CHECK(sw.result()->get("device_identifier")->s == "visrtx");
  CHECK(sw.result()->get("previous_device_identifier")->s == "helide");
  CHECK(f.backend.current == "visrtx");
  CHECK(token(f.tracker->revision()) != before);
  f.tool(s, "vsr_render", stale);
  CHECK(f.run(s, "vsr_operation_status", 4).str("outcome") == "superseded");
  f.tool(s, "vsr_render", f.renderArgs(5));
  CHECK(f.run(s, "vsr_render_image", 5).str("outcome") == "ok");

  // A failed switch is ok with applied:false and keeps the device/revision.
  f.backend.failSelect = true;
  const auto kept = token(f.tracker->revision());
  f.tool(s, "vsr_select_device", args(6, "barney"));
  auto failed = f.run(s, "vsr_operation_status", 6);
  REQUIRE(failed.str("outcome") == "ok");
  CHECK_FALSE(failed.flag("committed"));
  CHECK_FALSE(failed.result()->get("applied")->b);
  CHECK(failed.result()->get("device_identifier")->s == "visrtx");
  CHECK(token(f.tracker->revision()) == kept);
  f.backend.failSelect = false;

  // A view whose backend resources are held rejects busy.
  f.backend.holdPolls = 1000000;
  f.backend.allowRelease = false;
  f.tool(s, "vsr_render", f.renderArgs(7));
  for (int i = 0; i < 1000 && f.backend.begins < 2; ++i)
    f.integration.service();
  REQUIRE(f.backend.begins == 2);
  CHECK(
      f.tool(s, "vsr_select_device", args(8, "barney")).errorCode() == "busy");
  f.backend.allowRelease = true;
  f.backend.holdPolls = 0;
  CHECK(f.run(s, "vsr_operation_status", 7).str("outcome") == "ok");
}

SCENARIO("MCP frame tool describes the camera freshness predicate",
    "[integration][mcp][camera-frame]")
{
  RenderFixture f(true);
  const auto s = f.attach();
  const auto listed = post(f.server->port(),
      {"Host: " + f.server->authority(),
          "Authorization: Bearer " + f.authority->launchCredential(),
          "VSR-Session-ID: " + s.id,
          "VSR-Session-Proof: Bearer " + s.secret},
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}");
  REQUIRE(listed.json.get("result"));
  const auto *tools = listed.json.get("result")->get("tools");
  REQUIRE(tools);
  bool found = false;
  for (const auto &tool : tools->a) {
    if (tool.get("name")->s != "vsr_view_frame")
      continue;
    found = true;
    const auto &description = tool.get("description")->s;
    for (const auto *field : {"view_identity",
             "rendered_view_revision",
             "rendered_scene_revision",
             "rendered_configuration.camera",
             "anari_render_start",
             "stale=false",
             "120",
             "superseded"})
      CHECK(description.find(field) != std::string::npos);
  }
  REQUIRE(found);
}

SCENARIO(
    "MCP camera targets distinguish delayed, superseded and replayed frames",
    "[integration][mcp][camera-frame]")
{
  RenderFixture f(true);
  const auto s = f.attach();
  ViewId view = INVALID_VIEW_ID;
  identity::ViewRevisionTracker tracker(f.ids, *f.identity);
  vsr::rendering::Manipulator manipulator;
  manipulator.setConfig({0.f, 0.f, 0.f}, 5.f);
  auto binding = [&] {
    EffectiveViewBinding b;
    b.width = b.height = 2;
    b.displayWidth = b.displayHeight = 2.;
    b.deviceIdentifier = "helide";
    b.deviceSubtype = "default";
    b.cameraHandle = f.identity->handleOf(f.camera.data());
    const auto p = *f.camera->parameterValueAs<vsr::math::float3>("position");
    const auto d = *f.camera->parameterValueAs<vsr::math::float3>("direction");
    const auto u = *f.camera->parameterValueAs<vsr::math::float3>("up");
    b.camera.position = {p.x, p.y, p.z};
    b.camera.direction = {d.x, d.y, d.z};
    b.camera.up = {u.x, u.y, u.z};
    b.camera.fovyDegrees =
        vsr::math::degrees(*f.camera->parameterValueAs<float>("fovy"));
    b.rendererSubtype = "default";
    b.rendererConfigurationRevision = "stable";
    b.pipelineSource = "viewer_image_pipeline";
    b.pipelineConfigurationRevision = "stable";
    b.sourceInstanceGeneration = 1;
    b.outputColor = "rgba8_srgb";
    return b;
  };
  REQUIRE(f.integration.submit(f.sceneId, [&](auto &) {
    ViewRegistration vr;
    vr.scene = f.sceneId;
    vr.role = ViewRole::INTERACTIVE_VIEWPORT;
    vr.grants.read = true;
    vr.observeBinding = [&] {
      return std::optional<EffectiveViewBinding>(binding());
    };
    view = *f.integration.registerView(vr);
    REQUIRE(f.frames->attachView(view,
        *f.identity,
        tracker,
        frame::SourceQualification::VIEWER_CPU_READY));
  }));
  f.integration.service();
  render::ControllerCamera controller(
      *f.identity,
      tracker,
      manipulator,
      [&] { return f.camera.data(); },
      [] { return true; });
  REQUIRE(f.renders->attachCameraController(
      view, *f.identity, tracker, controller));
  REQUIRE(f.frames->setExposed(view, true));
  f.integration.setAgentCameraControlEnabled(true);
  auto args = [&](int id) {
    return "{\"request_id\":" + std::to_string(id)
        + ",\"view_id\":" + std::to_string(view) + "}";
  };
  auto move = [&](int id, int x) {
    const auto a = "{\"request_id\":" + std::to_string(id) + ",\"view_id\":"
        + std::to_string(view) + ",\"position\":[" + std::to_string(x)
        + ",2,7],\"direction\":[0,0,-1],\"up\":[0,1,0],\"fovy_deg\":30}";
    REQUIRE(
        f.tool(s, "vsr_set_view_camera", a).str("kind") == "set_view_camera");
    const auto result = f.run(s, "vsr_operation_status", id);
    REQUIRE(result.str("outcome") == "ok");
    REQUIRE(result.flag("committed"));
    return result;
  };
  auto finish = [&](int id) {
    Reply result;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
      REQUIRE(
          f.integration.submit(f.sceneId, [&](auto &) { f.frames->pump(); }));
      f.integration.service();
      result = f.tool(s,
          "vsr_operation_status",
          "{\"operation_ref\":" + ref(s.id, id) + "}");
      if (result.str("state") == "terminal")
        return result;
      std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    FAIL("frame did not reach a terminal outcome within the bounded wait");
    return result;
  };
  auto start = [&] { return f.frames->beginViewerProduction(view, binding); };
  // Controlled timing at the existing host seam tests lifecycle only. Actual
  // binding qualification uses the separate real viewport/presentation test.
  auto present = [&](std::shared_ptr<const frame::Production> production) {
    const std::uint32_t pixels[]{
        0xff0000ffu, 0xff00ff00u, 0xffff0000u, 0xffffffffu};
    auto acquired = f.frames->acquireViewerPixels(
        view, std::move(production), binding, pixels, 2, 2, "host_sdl_upload");
    REQUIRE(acquired);
    f.frames->presentViewerPixels(view, acquired, 2, 2, 2., 2., f.now);
  };
  REQUIRE(f.tool(s, "vsr_view_frame", args(1)).str("state") == "pending");
  f.integration.service();
  CHECK(f.tool(s,
             "vsr_operation_status",
             "{\"operation_ref\":" + ref(s.id, 1) + "}")
            .str("state")
      == "pending");
  SECTION("No presentation returns explicit timeout within 120 seconds")
  {
    for (int i = 0; i < 120; ++i) {
      f.now += 1.;
      REQUIRE(
          f.authority->renew({f.authority->launchCredential(), s.id, s.secret})
          == session::Code::OK);
    }
    const auto expired = finish(1);
    CHECK(expired.str("outcome") == "timeout");
    CHECK_FALSE(expired.flag("committed"));
    CHECK_FALSE(imageItem(expired));
  }
  SECTION("Controllable Helide with unknown upload remains unavailable")
  {
    move(2, 3);
    const auto production = start();
    REQUIRE(production);
    const std::uint32_t pixels[]{0, 0, 0, 0};
    CHECK_FALSE(f.frames->acquireViewerPixels(
        view, production, binding, pixels, 2, 2, "unknown_upload"));
    const auto unavailable = finish(1);
    REQUIRE(unavailable.result());
    CHECK(unavailable.result()->get("state")->s == "unavailable");
    CHECK(
        unavailable.result()->get("reason")->s == "unsupported_upload_profile");
    CHECK_FALSE(imageItem(unavailable));
    CHECK(controller.hasRecovery());
  }
  SECTION(
      "Second move during polling replaces recovery and invalidates the first target")
  {
    const auto old = start();
    REQUIRE(old);
    const auto first = move(2, 3);
    const auto firstProduction = start();
    REQUIRE(firstProduction);
    present(old);
    const auto delayed = finish(1);
    REQUIRE(delayed.result());
    CHECK(delayed.result()->get("stale")->b);
    CHECK(delayed.result()
              ->get("rendered_configuration")
              ->get("camera")
              ->get("position")
              ->a[0]
              .n
        == Approx(0.));
    CHECK(delayed.result()->get("rendered_view_revision")->s
        != first.result()->get("view_revision")->s);
    CHECK(delayed.result()->get("current_view_revision")->s
        == first.result()->get("view_revision")->s);
    REQUIRE(f.tool(s, "vsr_view_frame", args(3)).str("kind") == "view_frame");
    CHECK(finish(3).result()->get("stale")->b);
    const auto second = move(4, 8);
    present(firstProduction);
    REQUIRE(f.tool(s, "vsr_view_frame", args(5)).str("kind") == "view_frame");
    const auto superseded = finish(5);
    CHECK(superseded.result()->get("rendered_view_revision")->s
        == first.result()->get("view_revision")->s);
    CHECK(superseded.result()->get("rendered_view_revision")->s
        != second.result()->get("view_revision")->s);
    CHECK(superseded.result()->get("current_view_revision")->s
        == second.result()->get("view_revision")->s);
    CHECK(superseded.result()->get("stale")->b);
    CHECK(superseded.result()
              ->get("rendered_configuration")
              ->get("camera")
              ->get("position")
              ->a[0]
              .n
        == Approx(3.));
    const auto replay = f.tool(s, "vsr_view_frame", args(1));
    REQUIRE(replay.flag("replayed"));
    CHECK(replay.result()->get("frame_identity")->s
        == delayed.result()->get("frame_identity")->s);
    CHECK(replay.result()->get("current_view_revision")->s
        == delayed.result()->get("current_view_revision")->s);
    CHECK(replay.result()->get("rendered_view_revision")->s
        != second.result()->get("view_revision")->s);
    const auto postMoveScene = token(f.identity->sceneRevision());
    present(start());
    REQUIRE(f.tool(s, "vsr_view_frame", args(6)).str("kind") == "view_frame");
    const auto fresh = finish(6);
    REQUIRE(fresh.result());
    const auto *r = fresh.result();
    CHECK(r->get("state")->s == "available");
    CHECK(r->get("image_kind")->s == "presented_frame");
    CHECK(r->get("view_id")->n == view);
    CHECK(
        r->get("view_identity")->s == second.result()->get("view_identity")->s);
    CHECK(r->get("rendered_view_revision")->s
        == second.result()->get("view_revision")->s);
    CHECK(r->get("rendered_scene_revision")->s == postMoveScene);
    CHECK(r->get("current_scene_revision")->s == postMoveScene);
    CHECK(r->get("rendered_view_revision_basis")->s == "anari_render_start");
    CHECK_FALSE(r->get("stale")->b);
    const auto *camera = r->get("rendered_configuration")->get("camera");
    REQUIRE(camera);
    for (const auto *key : {"position", "direction", "up"})
      for (int i = 0; i < 3; ++i)
        CHECK(camera->get(key)->a[i].n
            == Approx(
                second.result()->get("effective_camera")->get(key)->a[i].n));
    CHECK(camera->get("projection")->s == "perspective");
    CHECK(camera->get("fovy_deg")->n == Approx(30.));
    const auto image = f.tool(
        s, "vsr_view_frame_image", "{\"operation_ref\":" + ref(s.id, 6) + "}");
    REQUIRE(imageItem(image));
    CHECK_FALSE(unbase64(imageItem(image)->get("data")->s).empty());
    REQUIRE(
        f.tool(s,
             "vsr_view_frame",
             "{\"request_id\":7,\"view_id\":" + std::to_string(f.viewId) + "}")
            .str("kind")
        == "view_frame");
    const auto other = finish(7);
    CHECK(other.result()->get("state")->s == "unavailable");
    CHECK(other.result()->get("reason")->s == "missing_source");
    identity::ViewRevisionTracker secondaryTracker(f.ids, *f.identity);
    ViewId secondary = INVALID_VIEW_ID;
    REQUIRE(f.integration.submit(f.sceneId, [&](auto &) {
      ViewRegistration vr;
      vr.scene = f.sceneId;
      vr.role = ViewRole::INTERACTIVE_VIEWPORT;
      vr.grants.read = true;
      vr.observeBinding = [&] {
        return std::optional<EffectiveViewBinding>(binding());
      };
      secondary = *f.integration.registerView(vr);
      REQUIRE(f.frames->attachView(secondary,
          *f.identity,
          secondaryTracker,
          frame::SourceQualification::VIEWER_CPU_READY));
    }));
    f.integration.service();
    REQUIRE(f.frames->setExposed(secondary, true));
    REQUIRE(
        f.tool(s,
             "vsr_view_frame",
             "{\"request_id\":8,\"view_id\":" + std::to_string(secondary) + "}")
            .str("kind")
        == "view_frame");
    f.integration.service();
    const auto secondaryProduction =
        f.frames->beginViewerProduction(secondary, binding);
    REQUIRE(secondaryProduction);
    const std::uint32_t pixels[]{0, 0, 0, 0};
    const auto secondaryPixels = f.frames->acquireViewerPixels(secondary,
        secondaryProduction,
        binding,
        pixels,
        2,
        2,
        "host_sdl_upload");
    REQUIRE(secondaryPixels);
    f.frames->presentViewerPixels(
        secondary, secondaryPixels, 2, 2, 2., 2., f.now);
    const auto secondView = finish(8);
    REQUIRE(secondView.result());
    CHECK(secondView.result()->get("view_id")->n == secondary);
    CHECK(secondView.result()->get("view_identity")->s
        != second.result()->get("view_identity")->s);
    CHECK(secondView.result()->get("rendered_view_revision")->s
        != second.result()->get("view_revision")->s);
    REQUIRE(f.integration.submit(f.sceneId, [&](auto &) {
      f.frames->detachViewer(secondary);
      f.integration.unregisterView(secondary);
    }));
    f.integration.service();
    manipulator.pan({0.1f, 0.f});
    vsr::rendering::updateCameraObject(*f.camera, manipulator);
    REQUIRE(f.tool(s, "vsr_view_frame", args(9)).str("kind") == "view_frame");
    const auto user = finish(9);
    CHECK(user.result()->get("stale")->b);
    CHECK(user.result()->get("current_view_revision")->s
        != second.result()->get("view_revision")->s);
    REQUIRE(controller.restore() == operations::Outcome::OK);
    CHECK(manipulator.eye().x == Approx(3.));
    CHECK(*f.camera->parameterValueAs<float>("fovy")
        == Approx(vsr::math::radians(30.f)));
    CHECK(
        token(tracker.revision()) != second.result()->get("view_revision")->s);
    present(start());
    auto material = f.scene.createObject<vsr::scene::Material>("matte");
    material->setParameter("color", vsr::math::float3(1.f, 0.f, 0.f));
    REQUIRE(f.tool(s, "vsr_view_frame", args(10)).str("kind") == "view_frame");
    const auto sceneChanged = finish(10);
    CHECK(sceneChanged.result()->get("stale")->b);
    CHECK(sceneChanged.result()->get("rendered_scene_revision")->s
        != sceneChanged.result()->get("current_scene_revision")->s);
    CHECK(sceneChanged.result()->get("rendered_view_revision")->s
        == sceneChanged.result()->get("current_view_revision")->s);
  }
}

SCENARIO("MCP moves and recovers the authoritative interactive controller",
    "[integration][mcp][camera-control]")
{
  RenderFixture f;
  const auto s = f.attach();
  ViewId view = INVALID_VIEW_ID;
  f.integration.submit(f.sceneId, [&](vsr::scene::Scene &) {
    ViewRegistration registration;
    registration.scene = f.sceneId;
    registration.role = ViewRole::INTERACTIVE_VIEWPORT;
    registration.grants = {true, false};
    view = *f.integration.registerView(registration);
  });
  f.integration.service();
  identity::ViewRevisionTracker tracker(f.ids, *f.identity);
  vsr::rendering::Manipulator manipulator;
  manipulator.setConfig({0.f, 0.f, 0.f}, 5.f);
  render::ControllerCamera controller(
      *f.identity,
      tracker,
      manipulator,
      [&] { return f.camera.data(); },
      [&] { return f.integration.isRegisteredView(view); });
  REQUIRE(f.renders->attachCameraController(
      view, *f.identity, tracker, controller));
  auto args = [&](int id, const std::string &extra = "") {
    return "{\"request_id\":" + std::to_string(id)
        + ",\"view_id\":" + std::to_string(view)
        + ",\"position\":[3,2,7],\"direction\":[0,0,-2],\"up\":[0,2,0]" + extra
        + "}";
  };
  CHECK(f.tool(s, "vsr_set_view_camera", args(1)).errorCode()
      == "camera_control_disabled");
  f.integration.setAgentCameraControlEnabled(true);
  CHECK(f.tool(s, "vsr_set_view_camera", args(1, ",\"unknown\":1")).errorCode()
      == "invalid_request");
  CHECK(
      f.tool(s, "vsr_set_view_camera", args(1, ",\"fovy_deg\":30")).str("kind")
      == "set_view_camera");
  manipulator.pan({0.2f, 0.f}); // input before the owning-thread commit
  const auto before = manipulator.eye();
  const float originalFovy = *f.camera->parameterValueAs<float>("fovy");
  const auto applied = f.run(s, "vsr_operation_status", 1);
  REQUIRE(applied.str("outcome") == "ok");
  CHECK(applied.flag("committed"));
  REQUIRE(applied.result());
  CHECK(applied.result()->get("view_identity")->isString());
  CHECK(applied.result()->get("scene_revision")->isString());
  const auto *effective = applied.result()->get("effective_camera");
  REQUIRE(effective);
  CHECK(effective->get("projection")->s == "perspective");
  CHECK(effective->get("fovy_deg")->n == Approx(30.));
  CHECK(effective->get("direction")->a[2].n == Approx(-1.));
  CHECK(manipulator.eye().x == Approx(3.f));
  CHECK(manipulator.eye().y == Approx(2.f));
  CHECK(manipulator.eye().z == Approx(7.f));
  CHECK(manipulator.distance() == Approx(5.f));
  CHECK(controller.hasRecovery());
  manipulator.rotate({0.02f, 0.f});
  manipulator.pan({0.01f, 0.f});
  manipulator.zoom(0.01f);
  CHECK(controller.hasRecovery());
  const auto movedRevision = tracker.revision();
  f.integration.setAgentCameraControlEnabled(false);
  CHECK(f.tool(s, "vsr_set_view_camera", args(1, ",\"fovy_deg\":30"))
          .flag("replayed"));
  REQUIRE(controller.restore() == operations::Outcome::OK);
  CHECK(manipulator.eye().x == Approx(before.x));
  CHECK(manipulator.eye().z == Approx(before.z));
  CHECK(f.camera->parameterValueAs<vsr::math::float3>("position")->x
      == Approx(before.x));
  CHECK(*f.camera->parameterValueAs<float>("fovy") == originalFovy);
  CHECK(tracker.revision() != movedRevision);

  f.integration.setAgentCameraControlEnabled(true);
  REQUIRE(f.tool(s, "vsr_set_view_camera", args(2)).errorCode().empty());
  f.integration.setAgentCameraControlEnabled(false);
  const auto revoked = f.run(s, "vsr_operation_status", 2);
  CHECK(revoked.str("kind") == "set_view_camera");
  CHECK(revoked.str("outcome") == "camera_control_disabled");
  CHECK_FALSE(revoked.flag("committed"));
  CHECK(revoked.isError());
  CHECK(revoked.result()->get("reason")->s == "camera_control_disabled");
  CHECK(manipulator.eye().x == Approx(before.x));

  f.integration.setAgentCameraControlEnabled(true);
  controller.setRecoveryByteLimit(0);
  REQUIRE(f.tool(s, "vsr_set_view_camera", args(3)).errorCode().empty());
  const auto busy = f.run(s, "vsr_operation_status", 3);
  CHECK(busy.str("outcome") == "busy");
  CHECK_FALSE(busy.flag("committed"));
  CHECK(manipulator.eye().x == Approx(before.x));
  CHECK(controller.hasRecovery());
  controller.setRecoveryByteLimit(64u << 10);

  f.authority->restrict(
      session::Target::scene(f.sceneId), session::Capability::READ);
  CHECK(f.tool(s, "vsr_set_view_camera", args(4)).errorCode() == "forbidden");
  f.authority->relax(
      session::Target::scene(f.sceneId), session::Capability::READ);
  REQUIRE(f.tool(s, "vsr_set_view_camera", args(4)).errorCode().empty());
  f.authority->restrict(
      session::Target::scene(f.sceneId), session::Capability::READ);
  CHECK(f.run(s, "vsr_operation_status", 4).str("outcome") == "forbidden");
  f.authority->relax(
      session::Target::scene(f.sceneId), session::Capability::READ);

  REQUIRE(f.authority->revokeSession());
  const auto later = f.attach();
  const auto historical = f.tool(later,
      "vsr_operation_status",
      "{\"operation_ref\":" + ref(s.id, 1) + "}");
  CHECK(historical.str("kind") == "set_view_camera");
  CHECK(historical.str("outcome") == "ok");
  REQUIRE(f.tool(later, "vsr_set_view_camera", args(1)).errorCode().empty());
  CHECK(
      f.tool(
           later, "vsr_cancel", "{\"operation_ref\":" + ref(later.id, 1) + "}")
          .str("outcome")
      == "cancelled");
  f.integration.service();
  CHECK(manipulator.eye().x == Approx(before.x));
  REQUIRE(controller.restore() == operations::Outcome::OK);
  CHECK(manipulator.eye().x == Approx(before.x));

  auto *failure = f.scene.updateDelegate().emplace<ThrowCameraNotifications>();
  REQUIRE(f.tool(later, "vsr_set_view_camera", args(2, ",\"fovy_deg\":45"))
          .errorCode()
          .empty());
  const auto committedFailure = f.run(later, "vsr_operation_status", 2);
  CHECK(committedFailure.str("outcome") == "applied_with_sync_failure");
  CHECK(committedFailure.flag("committed"));
  REQUIRE(committedFailure.result()->get("effective_camera"));
  CHECK(committedFailure.result()->get("effective_camera")->get("fovy_deg")->n
      == Approx(45.));
  CHECK(manipulator.eye().x == Approx(3.f));
  failure->fail = false;
  REQUIRE(controller.restore() == operations::Outcome::OK);
  CHECK(manipulator.eye().x == Approx(before.x));
  auto replacement = f.scene.createObject<vsr::scene::Camera>(
      vsr::scene::tokens::camera::perspective);
  f.camera = replacement;
  CHECK_FALSE(controller.recoveryAvailable());
  CHECK(controller.restore() == operations::Outcome::OUTCOME_UNAVAILABLE);
}

SCENARIO("MCP interactive camera admission is disabled without consuming ID",
    "[integration][mcp][camera-control]")
{
  RenderFixture f;
  const auto s = f.attach();
  f.backend.controllable = true;
  ViewId view = INVALID_VIEW_ID;
  f.integration.submit(f.sceneId, [&](vsr::scene::Scene &) {
    ViewRegistration registration;
    registration.scene = f.sceneId;
    registration.role = ViewRole::INTERACTIVE_VIEWPORT;
    registration.grants = {true, false};
    view = *f.integration.registerView(registration);
  });
  f.integration.service();
  identity::ViewRevisionTracker tracker(f.ids, *f.identity);
  vsr::rendering::Manipulator manipulator;
  render::ControllerCamera controller(
      *f.identity,
      tracker,
      manipulator,
      [&] { return f.camera.data(); },
      [] { return true; });
  REQUIRE(f.renders->attachCameraController(
      view, *f.identity, tracker, controller));
  const auto reply = f.tool(s,
      "vsr_set_view_camera",
      "{\"request_id\":1,\"view_id\":" + std::to_string(view)
          + ",\"position\":[0,0,5],\"direction\":[0,0,-1],\"up\":[0,1,0]}");
  CHECK(reply.status == 403);
  CHECK(reply.errorCode() == "camera_control_disabled");
  CHECK(f.engine->slotsInUse() == 0);
  CHECK(f.engine->highWater(s.id) == 0);
  REQUIRE(
      f.tool(s,
           "vsr_view_info",
           "{\"request_id\":1,\"view_id\":" + std::to_string(view) + "}")
          .errorCode()
          .empty());
  const auto info = f.run(s, "vsr_operation_status", 1);
  REQUIRE(info.result());
  const auto *control = info.result()->get("camera_control");
  REQUIRE(control);
  CHECK(control->get("supported")->b);
  CHECK_FALSE(control->get("enabled")->b);
  f.integration.setAgentCameraControlEnabled(true);
  REQUIRE(
      f.tool(s,
           "vsr_view_info",
           "{\"request_id\":2,\"view_id\":" + std::to_string(view) + "}")
          .errorCode()
          .empty());
  const auto enabled = f.run(s, "vsr_operation_status", 2);
  CHECK(enabled.result()->get("camera_control")->get("enabled")->b);
}

SCENARIO("MCP camera vectors handle extreme finite magnitudes",
    "[integration][mcp][camera-control]")
{
  RenderFixture f;
  f.backend.controllable = true;
  const auto s = f.attach();
  int id = 0;
  for (const auto &vectors : {"\"direction\":[0,0,-1e300],\"up\":[0,1e300,0]",
           "\"direction\":[0,0,-1e-300],\"up\":[0,1e-300,0]"}) {
    const auto reply = f.tool(s,
        "vsr_set_view_camera",
        "{\"request_id\":" + std::to_string(++id)
            + ",\"view_id\":0,\"position\":[0,0,5]," + std::string(vectors)
            + "}");
    CHECK(reply.errorCode().empty());
    if (reply.errorCode().empty()) {
      CHECK(f.run(s, "vsr_operation_status", id).str("outcome") == "ok");
    }
  }
}

SCENARIO("MCP vsr_set_view_camera poses an independent view's own camera",
    "[integration]")
{
  RenderFixture f;
  auto s = f.attach();
  auto args = [&](std::uint64_t id, const std::string &pose) {
    return "{\"request_id\":" + std::to_string(id)
        + ",\"view_id\":" + std::to_string(f.viewId) + "," + pose + "}";
  };
  const std::string good =
      "\"position\":[1,2,3],\"direction\":[0,0,-1],\"up\":[0,1,0]";

  // A backend without camera control rejects before admission.
  CHECK(f.tool(s, "vsr_set_view_camera", args(1, good)).errorCode()
      == "invalid_request");
  f.backend.controllable = true;

  // Malformed poses reject before admission: wrong arity, zero or parallel
  // vectors, out-of-range fovy.
  for (const auto *bad :
      {"\"position\":[1,2],\"direction\":[0,0,-1],\"up\":[0,1,0]",
          "\"position\":[1,2,3],\"direction\":[0,0,0],\"up\":[0,1,0]",
          "\"position\":[1,2,3],\"direction\":[0,2,0],\"up\":[0,1,0]",
          "\"position\":[1,2,3],\"direction\":[0,0,-1],\"up\":[0,1,0],"
          "\"fovy_deg\":180",
          "\"position\":[1,2,3],\"direction\":[0,0,-1]"})
    CHECK(f.tool(s, "vsr_set_view_camera", args(1, bad)).errorCode()
        == "invalid_request");
  CHECK(f.engine->highWater(s.id) == 0);
  CHECK(f.backend.cameraSets == 0);

  // A valid pose commits, advances the view revision and supersedes renders
  // pinned to the old one.
  const auto before = token(f.tracker->revision());
  const std::string stale = f.renderArgs(2);
  CHECK(f.tool(s, "vsr_set_view_camera", args(1, good + ",\"fovy_deg\":30"))
            .str("kind")
      == "set_view_camera");
  auto done = f.run(s, "vsr_operation_status", 1);
  REQUIRE(done.str("outcome") == "ok");
  CHECK(done.str("kind") == "set_view_camera");
  CHECK(f.tool(s, "vsr_set_view_camera", args(1, good + ",\"fovy_deg\":30"))
          .flag("replayed"));
  CHECK(done.flag("committed"));
  CHECK(done.result()->get("applied")->b);
  CHECK(done.result()->get("view_revision")->s == token(f.tracker->revision()));
  CHECK(token(f.tracker->revision()) != before);
  CHECK(f.backend.cameraSets == 1);
  CHECK(f.backend.eye == std::array<double, 3>{1., 2., 3.});
  CHECK(f.backend.fovy == 30.);
  f.tool(s, "vsr_render", stale);
  CHECK(f.run(s, "vsr_operation_status", 2).str("outcome") == "superseded");
  f.tool(s, "vsr_render", f.renderArgs(3));
  CHECK(f.run(s, "vsr_render_image", 3).str("outcome") == "ok");

  // A view whose backend resources are held rejects busy.
  f.backend.holdPolls = 1000000;
  f.backend.allowRelease = false;
  f.tool(s, "vsr_render", f.renderArgs(4));
  for (int i = 0; i < 1000 && f.backend.begins < 2; ++i)
    f.integration.service();
  REQUIRE(f.backend.begins == 2);
  CHECK(f.tool(s, "vsr_set_view_camera", args(5, good)).errorCode() == "busy");
  f.backend.allowRelease = true;
  f.backend.holdPolls = 0;
  CHECK(f.run(s, "vsr_operation_status", 4).str("outcome") == "ok");
  REQUIRE(f.authority->revokeSession());
  const auto later = f.attach();
  const auto historical = f.tool(later,
      "vsr_operation_status",
      "{\"operation_ref\":" + ref(s.id, 1) + "}");
  CHECK(historical.str("kind") == "set_view_camera");
  CHECK(historical.str("outcome") == "ok");
  CHECK(f.backend.cameraSets == 1);
}

SCENARIO("MCP render returns PNG image content at view-owned dimensions",
    "[integration]")
{
  RenderFixture f;
  auto s = f.attach();

  // A request dimension override is an envelope error: nothing admitted.
  CHECK(f.tool(s, "vsr_render", f.renderArgs(1, ",\"width\":8")).errorCode()
      == "invalid_request");
  CHECK(f.tool(s, "vsr_render", f.renderArgs(1, ",\"height\":8")).errorCode()
      == "invalid_request");
  CHECK(f.tool(s,
             "vsr_render",
             "{\"request_id\":1,\"view_id\":" + std::to_string(f.viewId)
                 + ",\"scene_revision\":\"1.2\",\"view_revision\":\"x\"}")
            .errorCode()
      == "invalid_request");
  CHECK(f.engine->highWater(s.id) == 0);

  auto ack = f.tool(s, "vsr_render", f.renderArgs(1));
  REQUIRE(ack.status == 200);
  CHECK(ack.str("kind") == "render");
  CHECK(ack.str("state") == "pending");
  CHECK(ack.sc()->get("operation_ref")->get("session_id")->s == s.id);
  CHECK(imageItem(ack) == nullptr);

  auto done = f.run(s, "vsr_render_image", 1);
  REQUIRE(done.str("outcome") == "ok");
  CHECK(!done.isError());
  CHECK(done.result()->get("width")->n == 64);
  CHECK(done.result()->get("height")->n == 48);
  CHECK(done.result()->get("scene_revision")->s
      == token(f.identity->sceneRevision()));
  const auto *img = imageItem(done);
  REQUIRE(img != nullptr);
  CHECK(img->get("mimeType")->s == "image/png");
  const auto png = unbase64(img->get("data")->s);
  REQUIRE(png.size() > 24);
  CHECK(png[1] == 'P');
  CHECK(png[2] == 'N');
  CHECK(png[3] == 'G');
  CHECK(be32(png, 16) == 64); // IHDR width
  CHECK(be32(png, 20) == 48); // IHDR height

  // Identical retry replays the original capture with its image.
  auto replay = f.tool(s, "vsr_render", f.renderArgs(1));
  CHECK(replay.flag("replayed"));
  CHECK(imageItem(replay) != nullptr);
  CHECK(f.backend.begins == 1);

  // Plain status lookup stays image-free; a resize supersedes old tokens.
  const std::string stale = f.renderArgs(2);
  f.renders->setDimensions(f.viewId, 32, 32);
  f.tool(s, "vsr_render", stale);
  auto sup = f.run(s, "vsr_operation_status", 2);
  CHECK(sup.str("outcome") == "superseded");
  CHECK(sup.isError());
  CHECK(imageItem(sup) == nullptr);

  // Invalid view-owned dimensions reject before admission, never clamped.
  f.renders->setDimensions(f.viewId, 2048, 16);
  CHECK(f.tool(s, "vsr_render", f.renderArgs(3)).errorCode()
      == "invalid_request");
}

SCENARIO(
    "MCP discover, inspect, render using only wire tokens", "[integration]")
{
  // Addendum 04 §8: an agent forms its first render request from wire data
  // alone; no host-side token lookup happens in this scenario.
  RenderFixture f;
  auto s = f.attach();

  f.tool(s, "vsr_discover", "{\"request_id\":1}");
  auto disc = f.run(s, "vsr_operation_status", 1);
  REQUIRE(disc.str("outcome") == "ok");
  std::string viewRevision;
  double viewId = -1;
  for (const auto &item : disc.result()->get("items")->a)
    if (item.get("kind")->s == "view") {
      viewId = item.get("id")->n;
      REQUIRE(item.get("view_revision"));
      viewRevision = item.get("view_revision")->s;
    } else {
      CHECK(item.get("view_revision") == nullptr);
    }
  REQUIRE(!viewRevision.empty());

  f.tool(s,
      "vsr_inspect",
      "{\"request_id\":2,\"scene_id\":" + std::to_string(f.sceneId) + "}");
  auto insp = f.run(s, "vsr_operation_status", 2);
  REQUIRE(insp.str("outcome") == "ok");
  REQUIRE(insp.result()->get("scene_revision"));
  const std::string sceneRevision = insp.result()->get("scene_revision")->s;

  f.tool(s,
      "vsr_render",
      "{\"request_id\":3,\"view_id\":" + std::to_string(std::int64_t(viewId))
          + ",\"scene_revision\":\"" + sceneRevision + "\",\"view_revision\":\""
          + viewRevision + "\"}");
  auto done = f.run(s, "vsr_render_image", 3);
  REQUIRE(done.str("outcome") == "ok");
  CHECK(done.result()->get("scene_revision")->s == sceneRevision);
  CHECK(done.result()->get("view_revision")->s == viewRevision);
  const auto *img = imageItem(done);
  REQUIRE(img != nullptr);
  CHECK(img->get("mimeType")->s == "image/png");
  const auto png = unbase64(img->get("data")->s);
  REQUIRE(png.size() > 24);
  CHECK(png[1] == 'P');
  CHECK(be32(png, 16) == 64);
  CHECK(be32(png, 20) == 48);
}

SCENARIO("MCP render maps sync_required and busy; resynchronize recovers",
    "[integration]")
{
  RenderFixture f;
  auto s = f.attach();

  // Not ready: sync_required, no image.
  f.states.markNeedsSync(f.sceneId);
  f.tool(s, "vsr_render", f.renderArgs(1));
  auto r = f.run(s, "vsr_render_image", 1);
  CHECK(r.str("outcome") == "sync_required");
  CHECK(r.isError());
  CHECK(imageItem(r) == nullptr);

  // Failed recovery: terminal ok, ready false, state failed (ticket 67).
  const std::string scene =
      "{\"request_id\":2,\"scene_id\":" + std::to_string(f.sceneId) + "}";
  f.recoverOk = false;
  auto ack = f.tool(s, "vsr_resynchronize", scene);
  REQUIRE(ack.status == 200);
  CHECK(ack.str("kind") == "resynchronize");
  CHECK(ack.sc()->get("operation_ref")->get("session_id")->s == s.id);
  CHECK(ack.sc()->get("operation_ref")->get("request_id")->n == 2);
  auto st = f.run(s, "vsr_operation_status", 2);
  CHECK(st.str("outcome") == "ok");
  CHECK(!st.result()->get("ready")->b);
  CHECK(st.result()->get("state")->s == "failed");

  f.recoverOk = true;
  f.tool(s,
      "vsr_resynchronize",
      "{\"request_id\":3,\"scene_id\":" + std::to_string(f.sceneId) + "}");
  st = f.run(s, "vsr_operation_status", 3);
  CHECK(st.result()->get("ready")->b);
  CHECK(st.result()->get("state")->s == "ready");

  f.tool(s, "vsr_render", f.renderArgs(4));
  CHECK(f.run(s, "vsr_render_image", 4).str("outcome") == "ok");

  // Cancelled render whose resources cannot be released yet: the view is
  // held, so a new render rejects busy before admission (ticket 91).
  f.backend.holdPolls = 1000000;
  f.backend.allowRelease = false;
  f.tool(s, "vsr_render", f.renderArgs(5));
  for (int i = 0; i < 1000 && f.backend.begins < 2; ++i)
    f.integration.service();
  REQUIRE(f.backend.begins == 2);
  f.tool(s, "vsr_cancel", "{\"operation_ref\":" + ref(s.id, 5) + "}");
  f.integration.service();
  CHECK(f.tool(s, "vsr_render", f.renderArgs(6)).errorCode() == "busy");
  f.backend.allowRelease = true;
  f.backend.holdPolls = 0;
  CHECK(f.run(s, "vsr_operation_status", 5).str("outcome") == "cancelled");

  // Unexposed scene: forbidden, nothing admitted.
  const auto hw = f.engine->highWater(s.id);
  CHECK(f.tool(s, "vsr_resynchronize", "{\"request_id\":7,\"scene_id\":99}")
            .errorCode()
      == "forbidden");
  CHECK(f.engine->highWater(s.id) == hw);
}

SCENARIO("MCP transported translation preview exposes exact nonmutating plans",
    "[integration][translation-preview][translation-mcp]")
{
  Fixture f({}, true, true, true, 2.);
  auto *layer = f.scene.defaultLayer();
  auto parent = layer->root()->insert_last_child(vsr::scene::LayerNodeData(
      layer, vsr::core::math::IDENTITY_MAT4, "wire-preview-parent"));
  auto local = vsr::core::math::IDENTITY_MAT4;
  local[3].x = 1;
  auto child = parent->insert_last_child(
      vsr::scene::LayerNodeData(layer, local, "wire-preview-child"));
  auto s = f.attach();
  auto tools = f.rpc("tools/list", "{}", &s);
  REQUIRE(tools.json.get("result"));
  bool found = false;
  for (const auto &tool : tools.json.get("result")->get("tools")->a) {
    if (tool.get("name")->s != "vsr_translate_preview")
      continue;
    found = true;
    CHECK(tool.get("description")->s.find("global plans=32")
        != std::string::npos);
    CHECK(
        tool.get("description")->s.find("NOT enumerated") != std::string::npos);
    CHECK(tool.get("inputSchema")->get("required")->a.size() == 4);
    CHECK(tool.get("inputSchema")
              ->get("properties")
              ->get("selection")
              ->get("maxItems")
              ->n
        == 64);
  }
  REQUIRE(found);
  const auto scene = "\"scene_id\":" + std::to_string(f.sceneId);
  REQUIRE(
      f.tool(s,
           "vsr_inspect",
           "{\"request_id\":1," + scene + ",\"kind\":\"nodes\"}")
          .errorCode()
          .empty());
  f.drain();
  auto observed = f.status(s, s.id, 1);
  REQUIRE(observed.str("outcome") == "ok");
  std::string selection;
  for (const auto &item : observed.result()->get("items")->a) {
    if (item.get("name")->get("text")->s != "wire-preview-parent"
        && item.get("name")->get("text")->s != "wire-preview-child")
      continue;
    if (!selection.empty())
      selection += ',';
    selection += "{\"handle\":" + json::quote(item.get("handle")->s)
        + ",\"node_edit_version\":"
        + json::quote(item.get("node_edit_version")->s)
        + ",\"structural_version\":"
        + json::quote(item.get("structural_version")->s)
        + ",\"ancestry_version\":"
        + json::quote(item.get("ancestry_version")->s) + "}";
  }
  REQUIRE(!selection.empty());
  const auto args = "{\"request_id\":2," + scene + ",\"selection\":["
      + selection + "],\"world_delta\":[2,0,0]}";
  CHECK(f.tool(s,
             "vsr_translate_preview",
             "{\"request_id\":2," + scene
                 + ",\"selection\":[],\"world_delta\":[2,0,0]}")
            .errorCode()
      == "invalid_request");
  CHECK(f.engine->highWater(s.id) == 1);
  REQUIRE(f.tool(s, "vsr_translate_preview", args).errorCode().empty());
  f.drain();
  auto done = f.status(s, s.id, 2);
  REQUIRE(done.str("outcome") == "ok");
  REQUIRE(done.result()->get("roots")->a.size() == 1);
  REQUIRE(done.result()->get("inherited_selected")->a.size() == 1);
  CHECK(done.result()->get("roots")->a[0].get("predicted_world_origin")->a[0].n
      == 2);
  CHECK(done.result()
            ->get("inherited_selected")
            ->a[0]
            .get("predicted_world_origin")
            ->a[0]
            .n
      == 3);
  CHECK_FALSE(done.result()->get("batch_ref"));
  CHECK_FALSE(done.result()->get("restore_eligible"));
  CHECK(parent->value().getTransform()[3].x == 0);
  CHECK(child->value().getTransform()[3].x == 1);
  CHECK(f.edits->restorations().size() == 0);
  const operations::Credentials credentials{
      f.authority->launchCredential(), s.id, s.secret};
  const auto historical = f.engine->status(credentials, {s.id, 2}).result;
  f.now += 3;
  f.translations->pump();
  CHECK(f.translations->retainedCount() == 0);
  REQUIRE(f.tool(s, "vsr_translate_preview", args).errorCode().empty());
  CHECK(f.translations->retainedCount()
      == 0); // retry cannot revive an expired plan
  CHECK(f.engine->status(credentials, {s.id, 2}).result == historical);
  f.authority->restrict(
      session::Target::scene(f.sceneId), session::Capability::READ);
  auto hidden = f.status(s, s.id, 2);
  CHECK_FALSE(hidden.result());
}

SCENARIO("MCP translation commits the retained plan once and restores",
    "[integration][translation-commit][translation-mcp]")
{
  Fixture f({}, true, true, true);
  auto *layer = f.scene.defaultLayer();
  auto parent = layer->root()->insert_last_child(vsr::scene::LayerNodeData(
      layer, vsr::core::math::IDENTITY_MAT4, "commit-parent"));
  auto local = vsr::core::math::IDENTITY_MAT4;
  local[3].x = 1;
  auto child = parent->insert_last_child(
      vsr::scene::LayerNodeData(layer, local, "commit-child"));
  auto s = f.attach();
  const auto scene = "\"scene_id\":" + std::to_string(f.sceneId);
  REQUIRE(
      f.tool(s,
           "vsr_inspect",
           "{\"request_id\":1," + scene + ",\"kind\":\"nodes\"}")
          .errorCode()
          .empty());
  f.drain();
  auto inspected = f.status(s, s.id, 1);
  std::string selection;
  for (const auto &item : inspected.result()->get("items")->a) {
    if (item.get("name")->get("text")->s != "commit-parent"
        && item.get("name")->get("text")->s != "commit-child")
      continue;
    if (!selection.empty())
      selection += ',';
    selection += "{\"handle\":" + json::quote(item.get("handle")->s)
        + ",\"node_edit_version\":"
        + json::quote(item.get("node_edit_version")->s)
        + ",\"structural_version\":"
        + json::quote(item.get("structural_version")->s)
        + ",\"ancestry_version\":"
        + json::quote(item.get("ancestry_version")->s) + "}";
  }
  REQUIRE(
      f.tool(s,
           "vsr_translate_preview",
           "{\"request_id\":2," + scene + ",\"selection\":[" + selection
               + "],\"world_delta\":[0.1,0,0]}")
          .errorCode()
          .empty());
  f.drain();
  auto preview = f.status(s, s.id, 2);
  REQUIRE(preview.result());
  const auto *pr = preview.result()->get("preview_ref");
  const auto ref = json::write(*pr);
  const auto args = "{\"request_id\":3,\"preview_ref\":" + ref + "}";
  CHECK(
      f.tool(s, "vsr_translate_commit", "{\"request_id\":3,\"preview_ref\":{}}")
          .errorCode()
      == "invalid_request");
  CHECK(f.engine->highWater(s.id) == 2);
  REQUIRE(f.tool(s, "vsr_translate_commit", args).errorCode().empty());
  f.drain();
  auto committed = f.status(s, s.id, 3);
  REQUIRE(committed.str("outcome") == "ok");
  REQUIRE(committed.result()->get("batch_ref"));
  CHECK(parent->value().getTransform()[3].x == float(0.1));
  CHECK(child->value().getTransform()[3].x == 1.f);
  REQUIRE(f.tool(s, "vsr_translate_commit", args).errorCode().empty());
  f.drain();
  CHECK(parent->value().getTransform()[3].x == float(0.1));
  CHECK(f.status(s, s.id, 3).result()->get("batch_ref")->get("batch_id")->s
      == committed.result()->get("batch_ref")->get("batch_id")->s);
  CHECK(f.tool(s,
             "vsr_translate_commit",
             "{\"request_id\":3,\"preview_ref\":{\"launch_id\":"
                 + json::quote(pr->get("launch_id")->s)
                 + ",\"preview_id\":\"00000000000000000000000000000000\","
                 + scene + "}}")
            .errorCode()
      == "request_id_conflict");
  REQUIRE(
      f.tool(s,
           "vsr_inspect",
           "{\"request_id\":4," + scene
               + ",\"kind\":\"nodes\",\"name\":\"commit-parent\"}")
          .errorCode()
          .empty());
  f.drain();
  auto reinspected = f.status(s, s.id, 4);
  REQUIRE(reinspected.str("outcome") == "ok");
  REQUIRE(reinspected.result()->get("items")->a.size() == 1);
  CHECK(reinspected.result()
            ->get("items")
            ->a[0]
            .get("local_matrix")
            ->get("data")
            ->a[0]
            .a[3]
            .n
      == Approx(0.1));
  REQUIRE(
      f.tool(s,
           "vsr_translate_commit",
           "{\"request_id\":5,\"preview_ref\":" + ref + "}")
          .errorCode()
          .empty());
  f.drain();
  CHECK(f.status(s, s.id, 5).str("outcome") == "outcome_unavailable");
  const auto *br = committed.result()->get("batch_ref");
  const auto batch = "{\"launch_id\":" + json::quote(br->get("launch_id")->s)
      + ",\"batch_id\":" + json::quote(br->get("batch_id")->s) + "}";
  REQUIRE(
      f.tool(s,
           "vsr_restore",
           "{\"request_id\":6," + scene + ",\"target_batch_ref\":" + batch
               + "}")
          .errorCode()
          .empty());
  f.drain();
  CHECK(f.status(s, s.id, 6).str("outcome") == "ok");
  CHECK(parent->value().getTransform()[3].x == 0.f);
  CHECK(child->value().getTransform()[3].x == 1.f);
  f.authority->restrict(
      session::Target::scene(f.sceneId), session::Capability::READ);
  auto hidden = f.status(s, s.id, 3);
  CHECK_FALSE(hidden.result());
  CHECK(hidden.str("outcome") == "ok");
  CHECK(hidden.flag("committed"));
}

SCENARIO("MCP full matrix uses row arrays and structured rejection",
    "[integration][node-mcp][node-matrix]")
{
  Fixture f({}, true, true);
  auto *layer = f.scene.defaultLayer();
  auto node = layer->root()->insert_last_child(vsr::scene::LayerNodeData(
      layer, vsr::core::math::IDENTITY_MAT4, "wire-matrix"));
  f.scene.signalLayerStructureChanged(layer);
  auto s = f.attach();
  const auto scene = "\"scene_id\":" + std::to_string(f.sceneId);
  REQUIRE(
      f.tool(s,
           "vsr_inspect",
           "{\"request_id\":1," + scene
               + ",\"kind\":\"nodes\",\"name\":\"wire-matrix\"}")
          .errorCode()
          .empty());
  f.drain();
  auto observed = f.status(s, s.id, 1);
  REQUIRE(observed.str("outcome") == "ok");
  auto item = observed.result()->get("items")->a[0];
  auto args = [&](unsigned id, const std::string &matrix) {
    return "{\"request_id\":" + std::to_string(id) + "," + scene
        + ",\"edits\":[{\"target_kind\":\"node\",\"handle\":"
        + json::quote(item.get("handle")->s)
        + ",\"version\":" + json::quote(item.get("node_edit_version")->s)
        + ",\"structural_version\":"
        + json::quote(item.get("structural_version")->s)
        + ",\"parameter\":\"transform\",\"value\":" + matrix + "}]}";
  };
  REQUIRE(
      f.tool(s,
           "vsr_edit",
           args(2, "[[1,0,0,0],[0,1,0,0],[0,0,1,0],[1e-100,0,0,1]]"))
          .errorCode()
          .empty());
  f.drain();
  auto bad = f.status(s, s.id, 2);
  CHECK(bad.str("outcome") == "invalid_request");
  REQUIRE(bad.result()->get("value_error"));
  CHECK(bad.result()->get("value_error")->s == "non_affine");
  CHECK_FALSE(bad.result()->get("committed")->b);
  REQUIRE(
      f.tool(s,
           "vsr_edit",
           args(3, "[[-2,3,0,11],[0,0,0,13],[0,1,4,17],[0,0,0,1]]"))
          .errorCode()
          .empty());
  f.drain();
  auto done = f.status(s, s.id, 3);
  REQUIRE(done.str("outcome") == "ok");
  CHECK(node->value().getTransform()[1].x == 3.f);
  CHECK(node->value().getTransform()[3].z == 17.f);
  auto batch = *done.result()->get("batch_ref");
  REQUIRE(f.restoreBatch(s, 4, batch).errorCode().empty());
  f.drain();
  CHECK(f.status(s, s.id, 4).str("outcome") == "ok");
  CHECK(node->value().getTransform()[1].x == 0.f);
  CHECK(node->value().getTransform()[3].z == 0.f);
}

SCENARIO("MCP node enabled edit inspect restore uses only wire tokens",
    "[integration][node-mcp][node-edit]")
{
  Fixture f({}, true, true);
  auto *layer = f.scene.defaultLayer();
  auto node = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, "wire-edit"));
  f.scene.signalLayerStructureChanged(layer);
  auto s = f.attach();
  const auto scene = "\"scene_id\":" + std::to_string(f.sceneId);
  auto inspect = [&](std::uint64_t id) {
    REQUIRE(
        f.tool(s,
             "vsr_inspect",
             "{\"request_id\":" + std::to_string(id) + "," + scene
                 + ",\"kind\":\"nodes\",\"name\":\"wire-edit\"}")
            .errorCode()
            .empty());
    f.drain();
    auto result = f.status(s, s.id, id);
    REQUIRE(result.str("outcome") == "ok");
    REQUIRE(result.result()->get("items")->a.size() == 1);
    return result.result()->get("items")->a[0];
  };
  auto item = inspect(1);
  std::string entry = "\"target_kind\":\"node\",\"handle\":"
      + json::quote(item.get("handle")->s)
      + ",\"version\":" + json::quote(item.get("node_edit_version")->s)
      + ",\"structural_version\":"
      + json::quote(item.get("structural_version")->s)
      + ",\"parameter\":\"enabled\"";
  auto args = [&](std::uint64_t id, const std::string &value) {
    return "{\"request_id\":" + std::to_string(id) + "," + scene
        + ",\"edits\":[{" + entry + ",\"value\":" + value + "}]}";
  };
  auto list = f.rpc("tools/list", "{}");
  REQUIRE(list.json.get("result"));
  bool schemaFound = false;
  for (const auto &tool : list.json.get("result")->get("tools")->a) {
    if (tool.get("name")->s == "vsr_edit") {
      auto schema = json::write(*tool.get("inputSchema"));
      CHECK(schema.find("structural_version") != std::string::npos);
      CHECK(schema.find("target_kind") != std::string::npos);
      schemaFound = true;
    }
  }
  CHECK(schemaFound);
  CHECK(f.tool(s, "vsr_edit", args(2, "0")).errorCode() == "invalid_request");
  REQUIRE(f.tool(s, "vsr_edit", args(2, "false")).errorCode().empty());
  f.drain();
  auto done = f.status(s, s.id, 2);
  REQUIRE(done.str("outcome") == "ok");
  REQUIRE(done.result()->get("batch_ref"));
  CHECK(done.result()->get("node_versions")->a.size() == 1);
  CHECK(done.result()->get("versions")->a.empty());
  auto batch = *done.result()->get("batch_ref");
  CHECK_FALSE(inspect(3).get("enabled")->b);
  CHECK(f.tool(s, "vsr_edit", args(2, "false")).flag("replayed"));
  REQUIRE(f.restoreBatch(s, 4, batch).errorCode().empty());
  f.drain();
  REQUIRE(f.status(s, s.id, 4).str("outcome") == "ok");
  CHECK(inspect(5).get("enabled")->b);
  CHECK(node->value().isEnabled());
}

SCENARIO("MCP named rotation and signed scale replay and restore",
    "[integration][node-mcp][node-srt]")
{
  Fixture f({}, true, true);
  auto *l = f.scene.defaultLayer();
  auto n = l->root()->insert_last_child(
      vsr::scene::LayerNodeData(l, vsr::math::IDENTITY_MAT4, "wire-srt"));
  n->value().setCurrentValueAsDefault();
  f.scene.signalLayerStructureChanged(l);
  auto s = f.attach();
  const auto scene = "\"scene_id\":" + std::to_string(f.sceneId);
  REQUIRE(
      f.tool(s,
           "vsr_inspect",
           "{\"request_id\":1," + scene
               + ",\"kind\":\"nodes\",\"name\":\"wire-srt\"}")
          .errorCode()
          .empty());
  f.drain();
  auto inspected = f.status(s, s.id, 1);
  auto before = inspected.result()->get("items")->a[0];
  const auto target = "\"target_kind\":\"node\",\"handle\":"
      + json::quote(before.get("handle")->s)
      + ",\"version\":" + json::quote(before.get("node_edit_version")->s)
      + ",\"structural_version\":"
      + json::quote(before.get("structural_version")->s);
  auto args = "{\"request_id\":2," + scene + ",\"edits\":[{" + target
      + ",\"parameter\":\"position\",\"value\":[11,12,13]},{" + target
      + ",\"parameter\":\"rotation\",\"value\":{\"roll\":0,\"azimuth\":450,\"elevation\":0}},{"
      + target + ",\"parameter\":\"scale\",\"value\":[-3,0,2]},{" + target
      + ",\"parameter\":\"enabled\",\"value\":false}]}";
  REQUIRE(f.tool(s, "vsr_edit", args).errorCode().empty());
  f.drain();
  auto done = f.status(s, s.id, 2);
  REQUIRE(done.str("outcome") == "ok");
  REQUIRE(done.result()->get("node_versions")->a.size() == 1);
  auto m = n->value().getTransform();
  CHECK(m[0][2] == Approx(3).margin(2e-6));
  CHECK(m[2][0] == Approx(2).margin(2e-6));
  CHECK(m[3] == vsr::math::float4(11, 12, 13, 1));
  CHECK_FALSE(n->value().isEnabled());
  CHECK(n->value().getTransformSRT()[1].x == 450);
  CHECK(f.tool(s, "vsr_edit", args).flag("replayed"));
  auto different = args;
  different.replace(different.find("450"), 3, "450.0000001");
  CHECK_FALSE(f.tool(s, "vsr_edit", different).errorCode().empty());
  REQUIRE(f.restoreBatch(s, 3, *done.result()->get("batch_ref"))
          .errorCode()
          .empty());
  f.drain();
  CHECK(f.status(s, s.id, 3).str("outcome") == "ok");
  CHECK(n->value().getTransform() == vsr::math::IDENTITY_MAT4);
  CHECK(n->value().isDefaultValue());
  CHECK(n->value().isEnabled());
}

SCENARIO(
    "MCP local position and enabled group preserves exact matrix on restore",
    "[integration][node-mcp][node-position]")
{
  Fixture f({}, true, true);
  auto *layer = f.scene.defaultLayer();
  auto node = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, "wire-move"));
  auto original = vsr::math::IDENTITY_MAT4;
  original[0] = {-2.f, 1.f, 0.f, 0.f};
  original[1] = {3.f, 4.f, 0.f, 0.f};
  original[2] = {0.f, 0.f, 0.f, 0.f};
  original[3] = {5.f, -6.f, 7.f, 1.f};
  node->value().setAsTransform(original);
  node->value().setCurrentValueAsDefault();
  f.scene.signalLayerStructureChanged(layer);
  auto s = f.attach();
  const auto scene = "\"scene_id\":" + std::to_string(f.sceneId);
  auto inspect = [&](std::uint64_t id) {
    REQUIRE(
        f.tool(s,
             "vsr_inspect",
             "{\"request_id\":" + std::to_string(id) + "," + scene
                 + ",\"kind\":\"nodes\",\"name\":\"wire-move\"}")
            .errorCode()
            .empty());
    f.drain();
    auto result = f.status(s, s.id, id);
    REQUIRE(result.str("outcome") == "ok");
    REQUIRE(result.result()->get("items")->a.size() == 1);
    return result.result()->get("items")->a[0];
  };
  auto before = inspect(1);
  const auto target = "\"target_kind\":\"node\",\"handle\":"
      + json::quote(before.get("handle")->s)
      + ",\"version\":" + json::quote(before.get("node_edit_version")->s)
      + ",\"structural_version\":"
      + json::quote(before.get("structural_version")->s);
  auto args = [&](const std::string &space) {
    return "{\"request_id\":2," + scene + ",\"edits\":[{" + target
        + ",\"parameter\":\"position\",\"value\":[11,-12,13]" + space + "},{"
        + target + ",\"parameter\":\"enabled\",\"value\":false}]}";
  };
  CHECK(f.tool(s, "vsr_edit", args(",\"space\":\"world\"")).errorCode()
      == "invalid_request");
  REQUIRE(f.tool(s, "vsr_edit", args("")).errorCode().empty());
  f.drain();
  auto done = f.status(s, s.id, 2);
  REQUIRE(done.str("outcome") == "ok");
  REQUIRE(done.result()->get("batch_ref"));
  CHECK(done.result()->get("node_versions")->a.size() == 1);
  auto batch = *done.result()->get("batch_ref");
  auto moved = inspect(3);
  CHECK_FALSE(moved.get("enabled")->b);
  const auto &rows = moved.get("local_matrix")->get("data")->a;
  CHECK(rows[0].a[0].n == -2);
  CHECK(rows[0].a[1].n == 3);
  CHECK(rows[0].a[3].n == 11);
  CHECK(rows[1].a[3].n == -12);
  CHECK(rows[2].a[3].n == 13);
  CHECK(rows[2].a[2].n == 0);
  CHECK(f.tool(s, "vsr_edit", args(",\"space\":\"local\"")).flag("replayed"));
  REQUIRE(f.restoreBatch(s, 4, batch).errorCode().empty());
  f.drain();
  REQUIRE(f.status(s, s.id, 4).str("outcome") == "ok");
  auto restored = inspect(5);
  CHECK(restored.get("enabled")->b);
  CHECK(json::write(*restored.get("local_matrix"))
      == json::write(*before.get("local_matrix")));
  CHECK(node->value().getTransform() == original);
  CHECK(node->value().isDefaultValue());
}

SCENARIO("MCP world position requires ancestry and restores exact node state",
    "[integration][node-mcp][node-position]")
{
  Fixture f({}, true, true);
  auto *layer = f.scene.defaultLayer();
  auto parent = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, "wire-parent"));
  auto parentMatrix = vsr::math::IDENTITY_MAT4;
  parentMatrix[0] = {0, 2, 0, 0};
  parentMatrix[1] = {-3, 0, 0, 0};
  parentMatrix[3] = {2, 3, 4, 1};
  parent->value().setAsTransform(parentMatrix);
  auto node =
      parent->insert_last_child(vsr::scene::LayerNodeData(layer, "wire-move"));
  auto original = vsr::math::IDENTITY_MAT4;
  original[0] = {-2.f, 1.f, 0.f, 0.f};
  original[1] = {3.f, 4.f, 0.f, 0.f};
  original[2] = {0.f, 0.f, 0.f, 0.f};
  original[3] = {5.f, -6.f, 7.f, 1.f};
  node->value().setAsTransform(original);
  node->value().setCurrentValueAsDefault();
  f.scene.signalLayerStructureChanged(layer);
  auto s = f.attach();
  const auto scene = "\"scene_id\":" + std::to_string(f.sceneId);
  auto inspect = [&](std::uint64_t id) {
    REQUIRE(
        f.tool(s,
             "vsr_inspect",
             "{\"request_id\":" + std::to_string(id) + "," + scene
                 + ",\"kind\":\"nodes\",\"name\":\"wire-move\"}")
            .errorCode()
            .empty());
    f.drain();
    auto result = f.status(s, s.id, id);
    REQUIRE(result.str("outcome") == "ok");
    REQUIRE(result.result()->get("items")->a.size() == 1);
    return result.result()->get("items")->a[0];
  };
  auto before = inspect(1);
  const auto target = "\"target_kind\":\"node\",\"handle\":"
      + json::quote(before.get("handle")->s)
      + ",\"version\":" + json::quote(before.get("node_edit_version")->s)
      + ",\"structural_version\":"
      + json::quote(before.get("structural_version")->s);
  auto args = [&](const std::string &space) {
    return "{\"request_id\":2," + scene + ",\"edits\":[{" + target
        + ",\"parameter\":\"position\",\"value\":[13,-9,17]" + space + "},{"
        + target + ",\"parameter\":\"enabled\",\"value\":false}]}";
  };
  CHECK(f.tool(s, "vsr_edit", args(",\"space\":\"world\"")).errorCode()
      == "invalid_request");
  CHECK(f.tool(s,
             "vsr_edit",
             args(",\"space\":\"world\",\"ancestry_version\":\"bad\""))
            .errorCode()
      == "invalid_request");
  const auto worldArgs = args(",\"space\":\"world\",\"ancestry_version\":"
      + json::quote(before.get("ancestry_version")->s));
  REQUIRE(f.tool(s, "vsr_edit", worldArgs).errorCode().empty());
  f.drain();
  auto done = f.status(s, s.id, 2);
  REQUIRE(done.str("outcome") == "ok");
  REQUIRE(done.result()->get("batch_ref"));
  CHECK(done.result()->get("node_versions")->a.size() == 1);
  auto batch = *done.result()->get("batch_ref");
  auto moved = inspect(3);
  CHECK_FALSE(moved.get("enabled")->b);
  const auto &rows = moved.get("local_matrix")->get("data")->a;
  CHECK(rows[0].a[0].n == -2);
  CHECK(rows[0].a[1].n == 3);
  CHECK(rows[0].a[3].n == -6);
  CHECK(std::abs(rows[1].a[3].n + 11. / 3) < 1e-6);
  CHECK(rows[2].a[3].n == 13);
  CHECK(rows[2].a[2].n == 0);
  CHECK(f.tool(s, "vsr_edit", worldArgs).flag("replayed"));
  CHECK(f.tool(s, "vsr_edit", args(",\"space\":\"local\"")).errorCode()
      == "request_id_conflict");
  REQUIRE(f.restoreBatch(s, 4, batch).errorCode().empty());
  f.drain();
  REQUIRE(f.status(s, s.id, 4).str("outcome") == "ok");
  auto restored = inspect(5);
  CHECK(restored.get("enabled")->b);
  CHECK(json::write(*restored.get("local_matrix"))
      == json::write(*before.get("local_matrix")));
  CHECK(node->value().getTransform() == original);
  CHECK(node->value().isDefaultValue());
}

SCENARIO("MCP mixed edit and restore carry one batch and both version lists",
    "[integration][mixed-edit][node-mcp]")
{
  Fixture f({}, true, true);
  auto *layer = f.scene.defaultLayer();
  auto node = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, vsr::math::IDENTITY_MAT4, "mixed-wire"));
  auto s = f.attach();
  const auto object = f.firstTarget(s, 1);
  const auto scene = "\"scene_id\":" + std::to_string(f.sceneId);
  REQUIRE(
      f.tool(s,
           "vsr_inspect",
           "{\"request_id\":2," + scene
               + ",\"kind\":\"nodes\",\"name\":\"mixed-wire\"}")
          .errorCode()
          .empty());
  f.drain();
  auto inspected = f.status(s, s.id, 2);
  REQUIRE(inspected.result());
  REQUIRE(inspected.result()->get("items")->a.size() == 1);
  const auto item = inspected.result()->get("items")->a[0];
  const auto target = "\"target_kind\":\"node\",\"handle\":"
      + json::quote(item.get("handle")->s)
      + ",\"version\":" + json::quote(item.get("node_edit_version")->s)
      + ",\"structural_version\":"
      + json::quote(item.get("structural_version")->s);
  const auto args = "{\"request_id\":3," + scene + ",\"edits\":[{" + object
      + ",\"parameter\":\"opacity\",\"value\":0.5},{" + target
      + ",\"parameter\":\"position\",\"value\":[4,5,6]}]}";
  REQUIRE(f.tool(s, "vsr_edit", args).errorCode().empty());
  f.drain();
  auto done = f.status(s, s.id, 3);
  REQUIRE(done.str("outcome") == "ok");
  CHECK(done.flag("committed"));
  REQUIRE(done.result());
  CHECK(done.result()->get("versions")->a.size() == 1);
  CHECK(done.result()->get("node_versions")->a.size() == 1);
  REQUIRE(done.result()->get("batch_ref"));
  const auto batch = *done.result()->get("batch_ref");
  CHECK(f.opacity0() == 0.5f);
  CHECK(node->value().getTransform()[3].xyz() == vsr::math::float3(4, 5, 6));
  CHECK(f.tool(s, "vsr_edit", args).flag("replayed"));
  REQUIRE(f.restoreBatch(s, 4, batch).errorCode().empty());
  f.drain();
  auto restored = f.status(s, s.id, 4);
  REQUIRE(restored.str("outcome") == "ok");
  CHECK(restored.flag("committed"));
  CHECK(restored.result()->get("versions")->a.size() == 1);
  CHECK(restored.result()->get("node_versions")->a.size() == 1);
  CHECK(f.opacity0() == 1.f);
  CHECK(node->value().getTransform() == vsr::math::IDENTITY_MAT4);
  CHECK(f.restoreBatch(s, 4, batch).flag("replayed"));
}

SCENARIO(
    "MCP world parent child permutations restore and reject stale ancestry",
    "[integration][node-mcp][node-world]")
{
  for (bool reverse : {false, true}) {
    Fixture f({}, true, true);
    auto *l = f.scene.defaultLayer();
    auto grand =
        l->root()->insert_last_child(vsr::scene::LayerNodeData(l, "g"));
    auto parent = grand->insert_last_child(vsr::scene::LayerNodeData(l, "p"));
    auto child = parent->insert_last_child(vsr::scene::LayerNodeData(l, "c"));
    auto matrix = vsr::math::IDENTITY_MAT4;
    matrix[0] = {0, 2, 0, 0};
    matrix[1] = {-3, 0, 0, 0};
    matrix[3] = {10, 20, 30, 1};
    grand->value().setAsTransform(matrix);
    parent->value().setAsTransform(vsr::math::IDENTITY_MAT4);
    child->value().setAsTransform(vsr::math::IDENTITY_MAT4);
    f.scene.signalLayerStructureChanged(l);
    auto s = f.attach();
    const auto scene = "\"scene_id\":" + std::to_string(f.sceneId);
    std::uint64_t id = 0;
    auto inspect = [&](const std::string &name) {
      ++id;
      REQUIRE(
          f.tool(s,
               "vsr_inspect",
               "{\"request_id\":" + std::to_string(id) + "," + scene
                   + ",\"kind\":\"nodes\",\"name\":" + json::quote(name) + "}")
              .errorCode()
              .empty());
      f.drain();
      auto result = f.status(s, s.id, id);
      REQUIRE(result.str("outcome") == "ok");
      REQUIRE(result.result()->get("items")->a.size() == 1);
      return result.result()->get("items")->a[0];
    };
    auto entry =
        [&](const json::Value &n, const std::string &value, bool world) {
          return "{\"target_kind\":\"node\",\"handle\":"
              + json::quote(n.get("handle")->s)
              + ",\"version\":" + json::quote(n.get("node_edit_version")->s)
              + ",\"structural_version\":"
              + json::quote(n.get("structural_version")->s)
              + ",\"parameter\":\"position\",\"value\":" + value
              + (world ? ",\"space\":\"world\",\"ancestry_version\":"
                          + json::quote(n.get("ancestry_version")->s)
                       : "")
              + "}";
        };
    const auto p = entry(inspect("p"), "[1,2,3]", false);
    const auto c = entry(inspect("c"), "[-8,30,39]", true);
    auto edit = [&](const std::string &entries) {
      ++id;
      REQUIRE(
          f.tool(s,
               "vsr_edit",
               "{\"request_id\":" + std::to_string(id) + "," + scene
                   + ",\"edits\":[" + entries + "]}")
              .errorCode()
              .empty());
      f.drain();
      return f.status(s, s.id, id);
    };
    auto done = edit(reverse ? c + "," + p : p + "," + c);
    REQUIRE(done.str("outcome") == "ok");
    CHECK(child->value().getTransform()[3] == vsr::math::float4(4, 4, 6, 1));
    const auto batch = *done.result()->get("batch_ref");
    REQUIRE(f.restoreBatch(s, ++id, batch).errorCode().empty());
    f.drain();
    REQUIRE(f.status(s, s.id, id).str("outcome") == "ok");
    CHECK(parent->value().getTransform() == vsr::math::IDENTITY_MAT4);
    CHECK(child->value().getTransform() == vsr::math::IDENTITY_MAT4);
    const auto stale = entry(inspect("c"), "[-8,30,39]", true);
    grand->value().setAsTransform(vsr::math::IDENTITY_MAT4);
    grand->value().setAsTransform(matrix);
    f.scene.signalLayerStructureChanged(l);
    CHECK(edit(stale).str("outcome") == "conflict");
    matrix[0] = {0, 0, 0, 0};
    grand->value().setAsTransform(matrix);
    f.scene.signalLayerStructureChanged(l);
    CHECK(edit(entry(inspect("c"), "[-8,30,39]", true)).str("outcome")
        == "invalid_request");
    CHECK(child->value().getTransform() == vsr::math::IDENTITY_MAT4);
  }
}

SCENARIO("MCP echoes finite numeric RPC identifiers without integer overflow",
    "[integration][reliability-numeric][rpc-numeric]")
{
  Fixture f;
  const std::pair<const char *, double> ids[] = {{"1e40", 1e40},
      {"-1e40", -1e40},
      {"9223372036854775808", 9223372036854775808.},
      {"-9223372036854777856", -9223372036854777856.},
      {"1e308", 1e308},
      {"-1e308", -1e308}};
  for (const auto &[text, expected] : ids) {
    INFO(text);
    const auto reply = post(f.server->port(),
        {"Host: " + f.server->authority(),
            "Authorization: Bearer " + f.authority->launchCredential()},
        std::string("{\"jsonrpc\":\"2.0\",\"id\":") + text
            + ",\"method\":\"ping\"}");
    CHECK(reply.status == 200);
    REQUIRE(reply.json.get("id"));
    REQUIRE(reply.json.get("id")->isNumber());
    CHECK(reply.json.get("id")->n == expected);
    CHECK(reply.json.get("result"));
  }
}

SCENARIO("MCP malformed numeric edits never consume valid state",
    "[integration][node-mcp][reliability-numeric]")
{
  Fixture f({}, true, true);
  auto *layer = f.scene.defaultLayer();
  auto node = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, vsr::math::IDENTITY_MAT4, "numeric"));
  node->value().setCurrentValueAsDefault();
  const auto beforeCache = node->value().getTransformSRT();
  auto s = f.attach();
  const auto scene = "\"scene_id\":" + std::to_string(f.sceneId);
  REQUIRE(
      f.tool(s,
           "vsr_inspect",
           "{\"request_id\":1," + scene
               + ",\"kind\":\"nodes\",\"name\":\"numeric\"}")
          .errorCode()
          .empty());
  f.drain();
  const auto observed = f.status(s, s.id, 1);
  REQUIRE(observed.result());
  REQUIRE(observed.result()->get("items")->a.size() == 1);
  const auto item = observed.result()->get("items")->a[0];
  const auto target = "\"target_kind\":\"node\",\"handle\":"
      + json::quote(item.get("handle")->s)
      + ",\"version\":" + json::quote(item.get("node_edit_version")->s)
      + ",\"structural_version\":"
      + json::quote(item.get("structural_version")->s);
  std::uint64_t id = 2;
  auto args = [&](const std::string &value) {
    return "{\"request_id\":" + std::to_string(id) + "," + scene
        + ",\"edits\":[{" + target
        + ",\"parameter\":\"enabled\",\"value\":false},{" + target
        + ",\"parameter\":\"position\",\"value\":" + value + "}]}";
  };
  const auto version = *f.sceneIds->nodeEditVersion(f.sceneIds->handleOf(node));
  const auto revision = f.sceneIds->sceneRevision();
  for (const std::string value : {"[1e309,0,0]",
           "[NaN,0,0]",
           "[Infinity,0,0]",
           "[01,0,0]",
           "[1.,0,0]",
           "[1e,0,0]"}) {
    INFO(value);
    const auto rejected = f.tool(s, "vsr_edit", args(value));
    REQUIRE(rejected.json.get("error"));
    REQUIRE(rejected.json.get("error")->get("code"));
    CHECK(rejected.json.get("error")->get("code")->n == -32700);
    CHECK(f.status(s, s.id, id).errorCode() == "unknown");
  }
  for (const std::string value : {"null",
           "true",
           "\"1\"",
           "[1,2]",
           "[1,2,3,4]",
           "[1e40,0,0]",
           "[{\"encoding\":\"non_finite\",\"value\":\"nan\"},0,0]"}) {
    INFO(value);
    const auto accepted = f.tool(s, "vsr_edit", args(value));
    REQUIRE(accepted.errorCode().empty());
    f.drain();
    const auto rejected = f.status(s, s.id, id++);
    CHECK(rejected.str("outcome") == "invalid_request");
    CHECK_FALSE(rejected.flag("committed"));
    CHECK(node->value().isEnabled());
    CHECK(node->value().getTransform() == vsr::math::IDENTITY_MAT4);
    CHECK(f.sceneIds->isCurrent(version));
    CHECK(f.sceneIds->sceneRevision() == revision);
    CHECK(f.edits->restorations().size() == 0);
  }
  REQUIRE(f.tool(s, "vsr_edit", args("[-0,0.5,-0.25]")).errorCode().empty());
  f.drain();
  const auto done = f.status(s, s.id, id++);
  REQUIRE(done.str("outcome") == "ok");
  REQUIRE(done.result()->get("batch_ref"));
  CHECK_FALSE(node->value().isEnabled());
  CHECK(node->value().getTransform()[3].x == 0.f);
  CHECK(node->value().getTransform()[3].y == 0.5f);
  CHECK(node->value().getTransform()[3].z == -0.25f);
  REQUIRE(f.restoreBatch(s, id, *done.result()->get("batch_ref"))
          .errorCode()
          .empty());
  f.drain();
  CHECK(f.status(s, s.id, id).str("outcome") == "ok");
  CHECK(node->value().isEnabled());
  CHECK(node->value().isDefaultValue());
  const auto restoredCache = node->value().getTransformSRT();
  CHECK(std::memcmp(&restoredCache, &beforeCache, sizeof(beforeCache)) == 0);
}

SCENARIO("Fools Mate repeats through loopback MCP and host safe points",
    "[integration][MCPchess][node-mcp]")
{
  Fixture f({}, true, true);
  chess::Fixture board(f.scene);
  context::ViewerExecutionHook safePoint(f.integration);
  auto session = f.attach();
  std::uint64_t id = 100;
  auto wait = [&](std::uint64_t request) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (int tries = 0; tries < 1000; ++tries) {
      REQUIRE(std::chrono::steady_clock::now() < deadline);
      safePoint();
      auto result = f.status(session, session.id, request);
      if (!result.str("outcome").empty())
        return result;
    }
    FAIL("MCP operation did not finish within bounded host polling");
    return Reply{};
  };
  REQUIRE(
      f.tool(session,
           "vsr_discover",
           "{\"request_id\":" + std::to_string(++id) + "}")
          .errorCode()
          .empty());
  auto discovered = wait(id);
  REQUIRE(discovered.str("outcome") == "ok");
  REQUIRE(discovered.result());
  REQUIRE(discovered.result()->get("items"));
  REQUIRE(discovered.result()->get("items")->a.size() == 1);
  const auto &sceneItem = discovered.result()->get("items")->a[0];
  REQUIRE(sceneItem.get("kind")->s == "scene");
  REQUIRE(sceneItem.get("id"));
  const auto scene = "\"scene_id\":"
      + std::to_string(static_cast<SceneId>(sceneItem.get("id")->n));
  auto inspect = [&](const char *name) {
    const auto request = ++id;
    REQUIRE(
        f.tool(session,
             "vsr_inspect",
             "{\"request_id\":" + std::to_string(request) + "," + scene
                 + ",\"kind\":\"nodes\",\"name\":" + json::quote(name) + "}")
            .errorCode()
            .empty());
    auto done = wait(request);
    REQUIRE(done.str("outcome") == "ok");
    REQUIRE(done.result()->get("items")->a.size() == 1);
    return done.result()->get("items")->a[0];
  };
  const auto parentBefore = inspect("board");
  std::vector<json::Value> before;
  std::string entries;
  for (const auto &move : chess::MOVES) {
    auto item = inspect(move.name);
    REQUIRE(item.get("name")->get("text")->s == move.name);
    REQUIRE(item.get("parent_handle")->s == parentBefore.get("handle")->s);
    before.push_back(item);
    if (!entries.empty())
      entries += ",";
    std::ostringstream destination;
    destination << std::setprecision(17) << '[' << move.world[0] << ','
                << move.world[1] << ',' << move.world[2] << ']';
    entries += "{\"target_kind\":\"node\",\"handle\":"
        + json::quote(item.get("handle")->s)
        + ",\"version\":" + json::quote(item.get("node_edit_version")->s)
        + ",\"structural_version\":"
        + json::quote(item.get("structural_version")->s)
        + ",\"ancestry_version\":"
        + json::quote(item.get("ancestry_version")->s)
        + ",\"space\":\"world\",\"parameter\":\"position\",\"value\":"
        + destination.str() + "}";
  }
  auto submit = [&](const std::string &edits) {
    const auto request = ++id;
    auto accepted = f.tool(session,
        "vsr_edit",
        "{\"request_id\":" + std::to_string(request) + "," + scene
            + ",\"edits\":[" + edits + "]}");
    REQUIRE(accepted.errorCode().empty());
    CHECK(accepted.str("state") == "pending");
    CHECK(accepted.str("progress") == "queued");
    CHECK_FALSE(accepted.flag("committed"));
    return request;
  };
  const auto editId = submit(entries);
  board.unchanged(true); // listener admission never mutates the scene
  auto done = wait(editId);
  REQUIRE(done.str("outcome") == "ok");
  REQUIRE(done.flag("committed"));
  REQUIRE(done.result()->get("batch_ref"));
  const auto batch = *done.result()->get("batch_ref");
  for (const auto &move : chess::MOVES) {
    auto item = inspect(move.name);
    REQUIRE(item.get("world_matrix"));
    REQUIRE(item.get("world_matrix")->get("data"));
    REQUIRE(item.get("world_matrix")->get("data")->a.size() == 4);
    REQUIRE(item.get("local_position"));
    REQUIRE(item.get("local_position")->get("data"));
    REQUIRE(item.get("local_position")->get("data")->a.size() == 3);
    for (int axis = 0; axis < 3; ++axis) {
      REQUIRE(item.get("world_matrix")->get("data")->a[axis].a.size() == 4);
      CHECK(item.get("world_matrix")->get("data")->a[axis].a[3].n
          == move.world[axis]);
      CHECK(item.get("local_position")->get("data")->a[axis].n
          == move.local[axis]);
    }
  }
  board.unchanged(false);
  CHECK(wait(submit(entries)).str("outcome") == "conflict");
  auto restore = [&] {
    return f.tool(session,
        "vsr_restore",
        "{\"request_id\":" + std::to_string(++id) + "," + scene
            + ",\"target_batch_ref\":" + json::write(batch) + "}");
  };
  REQUIRE(restore().errorCode().empty());
  auto restored = wait(id);
  REQUIRE(restored.str("outcome") == "ok");
  REQUIRE(restored.flag("committed"));
  for (std::size_t i = 0; i < chess::MOVES.size(); ++i) {
    auto item = inspect(chess::MOVES[i].name);
    for (const char *field :
        {"local_matrix", "enabled", "name", "node_kind", "parent_handle"})
      CHECK(
          json::write(*item.get(field)) == json::write(*before[i].get(field)));
  }
  auto parentAfter = inspect("board");
  CHECK(json::write(parentAfter) == json::write(parentBefore));
  board.unchanged(true);
  REQUIRE(restore().errorCode().empty());
  CHECK(wait(id).str("outcome") != "ok"); // one-way, never redo
}

namespace {

json::Value inspectMatrixObject(Fixture &f, const Session &s, std::uint64_t id)
{
  REQUIRE(
      f.tool(s,
           "vsr_inspect",
           "{\"request_id\":" + std::to_string(id) + ",\"scene_id\":"
               + std::to_string(f.sceneId) + ",\"name\":\"matrix-sampler\"}")
          .errorCode()
          .empty());
  f.drain();
  auto st = f.status(s, s.id, id);
  REQUIRE(st.str("outcome") == "ok");
  REQUIRE(st.result()->get("items")->a.size() == 1);
  return st.result()->get("items")->a[0];
}

std::string objectFields(const json::Value &item)
{
  return "\"target_kind\":\"object\",\"handle\":"
      + json::quote(item.get("handle")->s)
      + ",\"version\":" + json::write(*item.get("version"));
}

const json::Value &matrixParameter(const json::Value &item)
{
  for (const auto &p : item.get("parameters")->a)
    if (p.get("name")->s == "outTransform")
      return p;
  FAIL("missing inspected outTransform");
  return item;
}

std::string matrixEditArgs(Fixture &f,
    std::uint64_t id,
    const json::Value &item,
    const std::string &value)
{
  return "{\"request_id\":" + std::to_string(id) + ",\"scene_id\":"
      + std::to_string(f.sceneId) + ",\"edits\":[{" + objectFields(item)
      + ",\"parameter\":\"outTransform\",\"value\":" + value + "}]}";
}

} // namespace

SCENARIO(
    "MCP object matrices preserve independent multiplication oracles and exact restore",
    "[integration][object-matrix][mcp]")
{
  struct MatrixCase
  {
    anari::DataType type;
    std::size_t rows;
    std::size_t columns;
    const char *wire;
    std::array<float, 4> operand;
    std::array<float, 4> expected;
  };
  const MatrixCase cases[] = {
      {ANARI_FLOAT32_MAT2, 2, 2, "[[1,2],[3,4]]", {5, 6}, {17, 39}},
      {ANARI_FLOAT32_MAT3,
          3,
          3,
          "[[1,2,3],[4,5,6],[7,8,10]]",
          {2, 3, 4},
          {20, 47, 78}},
      {ANARI_FLOAT32_MAT4,
          4,
          4,
          "[[1,2,3,4],[5,6,7,8],[9,10,11,12],[13,14,15,16]]",
          {1, 2, 3, 1},
          {18, 46, 74, 102}},
      {ANARI_FLOAT32_MAT2x3, 2, 3, "[[1,2,3],[4,5,6]]", {7, 8, 9}, {50, 122}},
      {ANARI_FLOAT32_MAT3x4,
          3,
          4,
          "[[1,2,3,4],[5,6,7,8],[9,10,11,12]]",
          {1, 2, 3, 1},
          {18, 46, 74}}};
  for (const auto &test : cases) {
    INFO(anari::toString(test.type));
    Fixture f({}, true);
    auto sampler = f.scene.createObject<vsr::scene::Sampler>("image2D");
    sampler->setName("matrix-sampler");
    const float before[16] = {
        -1, -4, -2, -5, -3, -6, -7, -8, -9, -10, -11, -12, -13, -14, -15, -16};
    sampler->setParameter("outTransform", test.type, before);
    auto s = f.attach();
    const auto item = inspectMatrixObject(f, s, 1);
    CHECK(matrixParameter(item).get("editable")->b);
    const auto args = matrixEditArgs(f, 2, item, test.wire);
    REQUIRE(f.tool(s, "vsr_edit", args).str("state") == "pending");
    f.drain();
    auto done = f.status(s, s.id, 2);
    REQUIRE(done.str("outcome") == "ok");
    CHECK(done.flag("committed"));
    REQUIRE(done.result()->get("versions")->a.size() == 1);
    CHECK_FALSE(done.result()->get("matrix_versions"));
    const auto after = inspectMatrixObject(f, s, 3);
    const auto &rows = matrixParameter(after).get("value")->get("data")->a;
    REQUIRE(rows.size() == test.rows);
    CHECK(matrixParameter(after).get("value")->get("type")->s
        == anari::toString(test.type));
    float packed[16] = {};
    std::memcpy(packed,
        sampler->parameter("outTransform")->value().data(),
        anari::sizeOf(test.type));
    for (std::size_t r = 0; r < test.rows; ++r) {
      REQUIRE(rows[r].a.size() == test.columns);
      double wireProduct = 0;
      float storedProduct = 0;
      for (std::size_t c = 0; c < test.columns; ++c) {
        wireProduct += rows[r].a[c].n * test.operand[c];
        storedProduct += packed[c * test.rows + r] * test.operand[c];
      }
      CHECK(wireProduct == test.expected[r]);
      CHECK(storedProduct == test.expected[r]);
    }
    CHECK(f.tool(s, "vsr_edit", args).flag("replayed"));
    CHECK(inspectMatrixObject(f, s, 4).get("version")->n
        == after.get("version")->n);
    REQUIRE(f.restoreBatch(s, 5, *done.result()->get("batch_ref"))
            .errorCode()
            .empty());
    f.drain();
    const auto restored = f.status(s, s.id, 5);
    REQUIRE(restored.str("outcome") == "ok");
    CHECK(restored.result()->get("versions")->a.size() == 1);
    const auto final = inspectMatrixObject(f, s, 6);
    CHECK(json::write(*matrixParameter(final).get("value"))
        == json::write(*matrixParameter(item).get("value")));
    CHECK(std::memcmp(before,
              sampler->parameter("outTransform")->value().data(),
              anari::sizeOf(test.type))
        == 0);
  }
}

SCENARIO("MCP matrix schema, object space and ordinary restore receipts agree",
    "[integration][object-matrix][matrix-contract][mcp]")
{
  Fixture f({}, true);
  auto sampler = f.scene.createObject<vsr::scene::Sampler>("image2D");
  sampler->setName("matrix-sampler");
  auto s = f.attach();
  SECTION("schema describes nested numeric rows")
  {
    const auto list = f.rpc("tools/list", "{}");
    bool found = false;
    for (const auto &tool : list.json.get("result")->get("tools")->a) {
      if (tool.get("name")->s != "vsr_edit")
        continue;
      found = true;
      const auto &schema = *tool.get("inputSchema")
                                ->get("properties")
                                ->get("edits")
                                ->get("items")
                                ->get("properties")
                                ->get("value");
      REQUIRE(schema.get("anyOf"));
      bool nestedNumbers = false;
      for (const auto &choice : schema.get("anyOf")->a) {
        const auto *items = choice.get("items");
        nestedNumbers |= items && items->get("items")
            && items->get("items")->get("type")->s == "number";
      }
      CHECK(nestedNumbers);
    }
    REQUIRE(found);
  }
  const auto item = inspectMatrixObject(f, s, 1);
  auto args =
      matrixEditArgs(f, 2, item, "[[0,1,0,0],[1,0,0,0],[0,0,1,0],[0,0,0,1]]");
  SECTION("object space rejects before consuming the request ID")
  {
    auto withSpace = args;
    withSpace.insert(withSpace.find("\"parameter\""), "\"space\":\"local\",");
    CHECK(f.tool(s, "vsr_edit", withSpace).errorCode() == "invalid_request");
    REQUIRE(f.tool(s, "vsr_edit", args).str("state") == "pending");
    f.drain();
    CHECK(f.status(s, s.id, 2).str("outcome") == "ok");
  }
  SECTION("matrix restore has ordinary versions and no Image versions")
  {
    REQUIRE(f.tool(s, "vsr_edit", args).str("state") == "pending");
    f.drain();
    const auto done = f.status(s, s.id, 2);
    REQUIRE(done.str("outcome") == "ok");
    REQUIRE(f.restoreBatch(s, 3, *done.result()->get("batch_ref"))
            .errorCode()
            .empty());
    f.drain();
    const auto restored = f.status(s, s.id, 3);
    REQUIRE(restored.str("outcome") == "ok");
    REQUIRE(restored.result()->get("image_versions"));
    CHECK(restored.result()->get("image_versions")->a.empty());
    CHECK_FALSE(restored.result()->get("matrix_versions"));
  }
}

SCENARIO(
    "MCP Sampler channel swap preserves neutral samples and rejects stale edits",
    "[integration][object-matrix][mcp]")
{
  Fixture f({}, true);
  auto sampler = f.scene.createObject<vsr::scene::Sampler>("image2D");
  sampler->setName("matrix-sampler");
  auto s = f.attach();
  const auto before = inspectMatrixObject(f, s, 1);
  const auto args =
      matrixEditArgs(f, 2, before, "[[0,1,0,0],[1,0,0,0],[0,0,1,0],[0,0,0,1]]");
  REQUIRE(f.tool(s, "vsr_edit", args).str("state") == "pending");
  f.drain();
  const auto edited = f.status(s, s.id, 2);
  REQUIRE(edited.str("outcome") == "ok");
  const auto swapped = inspectMatrixObject(f, s, 3);
  const auto &rows = matrixParameter(swapped).get("value")->get("data")->a;
  const std::array<std::array<double, 4>, 3> inputs = {
      {{0, 1, 0, 1}, {0.125, 0.125, 0.125, 1}, {1, 0, 0, 1}}};
  const std::array<std::array<double, 4>, 3> expected = {
      {{1, 0, 0, 1}, {0.125, 0.125, 0.125, 1}, {0, 1, 0, 1}}};
  for (std::size_t i = 0; i < inputs.size(); ++i)
    for (std::size_t r = 0; r < 4; ++r) {
      double output = 0;
      for (std::size_t c = 0; c < 4; ++c)
        output += rows[r].a[c].n * inputs[i][c];
      CHECK(output == expected[i][r]);
    }
  REQUIRE(
      f.tool(s,
           "vsr_edit",
           matrixEditArgs(
               f, 4, before, "[[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]"))
          .errorCode()
          .empty());
  f.drain();
  CHECK(f.status(s, s.id, 4).str("outcome") == "conflict");
  CHECK_FALSE(f.status(s, s.id, 4).flag("committed"));
  auto unrelated = f.scene.getObject<vsr::scene::Material>(0);
  unrelated->setParameter("opacity", 0.75f);
  sampler->setName("renamed-sampler");
  REQUIRE(f.restoreBatch(s, 5, *edited.result()->get("batch_ref"))
          .errorCode()
          .empty());
  f.drain();
  CHECK(f.status(s, s.id, 5).str("outcome") == "ok");
  CHECK(sampler->name() == "renamed-sampler");
  CHECK(unrelated->parameter("opacity")->value().get<float>() == 0.75f);
  CHECK(sampler->parameter("outTransform")->value().get<vsr::math::mat4>()
      == vsr::math::IDENTITY_MAT4);
  REQUIRE(f.tool(s, "vsr_edit", args).flag("replayed"));
  CHECK(sampler->parameter("outTransform")->value().get<vsr::math::mat4>()
      == vsr::math::IDENTITY_MAT4);
}

SCENARIO("MCP matrix numeric preparation respects packed metadata and rounding",
    "[integration][object-matrix][matrix-numeric][mcp]")
{
  Fixture f({}, true);
  auto sampler = f.scene.createObject<vsr::scene::Sampler>("image2D");
  sampler->setName("matrix-sampler");
  const float original[6] = {};
  sampler->setParameter("outTransform", ANARI_FLOAT32_MAT2x3, original);
  auto *p = sampler->parameter("outTransform");
  p->setEnabled(false);
  auto s = f.attach();
  std::uint64_t id = 0;
  auto submit = [&](const std::string &value) {
    const auto item = inspectMatrixObject(f, s, ++id);
    const auto request = ++id;
    REQUIRE(f.tool(s, "vsr_edit", matrixEditArgs(f, request, item, value))
                .str("state")
        == "pending");
    f.drain();
    return f.status(s, s.id, request);
  };
  SECTION("matching bounds use packed component indices")
  {
    const float bounds[6] = {1, 4, 2, 5, 3, 6};
    p->setMin(vsr::core::Any(ANARI_FLOAT32_MAT2x3, bounds))
        .setMax(vsr::core::Any(ANARI_FLOAT32_MAT2x3, bounds));
    const auto done = submit("[[1,2,3],[4,5,6]]");
    REQUIRE(done.str("outcome") == "ok");
    CHECK_FALSE(p->isEnabled());
    CHECK(std::memcmp(bounds, p->value().data(), sizeof(bounds)) == 0);
    for (const char *bad : {"[[0,2,3],[4,5,6]]",
             "[[1,1,3],[4,5,6]]",
             "[[1,2,2],[4,5,6]]",
             "[[1,2,3],[3,5,6]]",
             "[[1,2,3],[4,4,6]]",
             "[[1,2,3],[4,5,5]]"}) {
      const auto rejected = submit(bad);
      CHECK(rejected.str("outcome") == "invalid_request");
      CHECK(rejected.result()->get("value_error")->s == "out_of_domain");
      CHECK_FALSE(rejected.flag("committed"));
      CHECK(std::memcmp(bounds, p->value().data(), sizeof(bounds)) == 0);
    }
  }
  SECTION("differently typed metadata is ignored and round once ties to even")
  {
    p->setMin(vsr::core::Any(std::numeric_limits<double>::infinity()))
        .setMax(vsr::core::Any(-1.f));
    const auto done = submit(
        "[[1.000000059604644775390625,1.000000178813934326171875,3.4028234663852886e38],[0,-0,7.006492321624085e-46]]");
    REQUIRE(done.str("outcome") == "ok");
    const auto item = inspectMatrixObject(f, s, ++id);
    const auto &rows = matrixParameter(item).get("value")->get("data")->a;
    CHECK(rows[0].a[0].n == 1.);
    CHECK(rows[0].a[1].n == 1.0000002384185791015625);
    CHECK(rows[0].a[2].n == double(std::numeric_limits<float>::max()));
    CHECK(rows[1].a[2].n == 0.);
  }
  SECTION("matching invalid bounds reject before mutation")
  {
    float lower[6] = {0, 0, 0, 0, 0, 0};
    float upper[6] = {10, 10, 10, 10, 10, 10};
    for (int kind = 0; kind < 3; ++kind) {
      lower[4] = kind == 0 ? std::numeric_limits<float>::infinity()
          : kind == 1      ? 11.f
                           : 0.f;
      upper[4] = kind == 2 ? std::numeric_limits<float>::quiet_NaN() : 10.f;
      p->setMin(vsr::core::Any(ANARI_FLOAT32_MAT2x3, lower))
          .setMax(vsr::core::Any(ANARI_FLOAT32_MAT2x3, upper));
      const auto bad = submit("[[1,2,3],[4,5,6]]");
      CHECK(bad.str("outcome") == "invalid_request");
      CHECK(bad.result()->get("value_error")->s == "out_of_domain");
      CHECK_FALSE(bad.flag("committed"));
      CHECK(std::memcmp(original, p->value().data(), sizeof(original)) == 0);
    }
  }
}

SCENARIO("MCP mixed matrix scalar vector and node batches are atomic",
    "[integration][object-matrix][matrix-atomic][mcp]")
{
  Fixture f({}, true, true);
  auto sampler = f.scene.createObject<vsr::scene::Sampler>("image2D");
  sampler->setName("matrix-sampler");
  auto *layer = f.scene.defaultLayer();
  auto node = layer->root()->insert_last_child(vsr::scene::LayerNodeData(
      layer, vsr::math::IDENTITY_MAT4, "matrix-node"));
  f.scene.signalLayerStructureChanged(layer);
  auto s = f.attach();
  std::uint64_t id = 0;
  auto inspectNode = [&] {
    const auto request = ++id;
    REQUIRE(
        f.tool(s,
             "vsr_inspect",
             "{\"request_id\":" + std::to_string(request)
                 + ",\"scene_id\":" + std::to_string(f.sceneId)
                 + ",\"kind\":\"nodes\",\"name\":\"matrix-node\"}")
            .errorCode()
            .empty());
    f.drain();
    const auto st = f.status(s, s.id, request);
    REQUIRE(st.str("outcome") == "ok");
    return st.result()->get("items")->a.at(0);
  };
  const auto object = f.firstTarget(s, ++id);
  const auto matrix = inspectMatrixObject(f, s, ++id);
  const auto placement = inspectNode();
  const auto nodeTarget = "\"target_kind\":\"node\",\"handle\":"
      + json::quote(placement.get("handle")->s)
      + ",\"version\":" + json::quote(placement.get("node_edit_version")->s)
      + ",\"structural_version\":"
      + json::quote(placement.get("structural_version")->s);
  const auto oldValue = sampler->parameter("outTransform")->value();
  auto material = f.scene.getObject<vsr::scene::Material>(0);
  const auto oldColor = material->parameter("color")->value();
  auto args = [&](std::uint64_t request, const std::string &value) {
    return "{\"request_id\":" + std::to_string(request)
        + ",\"scene_id\":" + std::to_string(f.sceneId) + ",\"edits\":[{"
        + object + ",\"parameter\":\"opacity\",\"value\":0.5},{" + nodeTarget
        + ",\"parameter\":\"position\",\"value\":[4,5,6]},{" + object
        + ",\"parameter\":\"color\",\"value\":[0.25,0.5,0.75]},{"
        + objectFields(matrix)
        + ",\"parameter\":\"outTransform\",\"value\":" + value + "}]}";
  };
  for (const char *invalid : {"null",
           "1",
           "[1,2,3,4]",
           "[]",
           "[[1,0,0,0],[0,1,0],[0,0,1,0],[0,0,0,1]]",
           "[[1,0,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]",
           "[[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1],[0,0,0,1]]",
           "[[1,0,0,0],[0,true,0,0],[0,0,1,0],[0,0,0,1]]",
           "[[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,3.4028235e38]]"}) {
    const auto request = ++id;
    REQUIRE(f.tool(s, "vsr_edit", args(request, invalid)).errorCode().empty());
    f.drain();
    const auto bad = f.status(s, s.id, request);
    CHECK(bad.str("outcome") == "invalid_request");
    CHECK_FALSE(bad.flag("committed"));
    CHECK(bad.result()->get("failed_entry")->n == 3);
    CHECK(material->parameter("color")->value() == oldColor);
    CHECK(f.opacity0() == 1.f);
    CHECK(node->value().getTransform() == vsr::math::IDENTITY_MAT4);
    CHECK(sampler->parameter("outTransform")->value() == oldValue);
  }
  SECTION("unsupported stored types never partially write")
  {
    const std::uint32_t packed = 0;
    sampler->setParameter("outTransform", ANARI_UFIXED8_VEC4, &packed);
    const auto updated = inspectMatrixObject(f, s, ++id);
    CHECK_FALSE(matrixParameter(updated).get("editable")->b);
    auto badArgs = args(++id, "[[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]");
    const auto oldFields = objectFields(matrix);
    badArgs.replace(
        badArgs.find(oldFields), oldFields.size(), objectFields(updated));
    REQUIRE(f.tool(s, "vsr_edit", badArgs).errorCode().empty());
    f.drain();
    const auto bad = f.status(s, s.id, id);
    CHECK(bad.str("outcome") == "invalid_request");
    CHECK(bad.result()->get("value_error")->s == "unsupported_parameter");
    CHECK(material->parameter("color")->value() == oldColor);
    CHECK(f.opacity0() == 1.f);
    CHECK(node->value().getTransform() == vsr::math::IDENTITY_MAT4);
  }
  SECTION("stale expected matrix version rejects the whole batch")
  {
    sampler->setParameter("outTransform", oldValue);
    const auto request = ++id;
    REQUIRE(
        f.tool(s,
             "vsr_edit",
             args(request, "[[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]"))
            .errorCode()
            .empty());
    f.drain();
    const auto bad = f.status(s, s.id, request);
    CHECK(bad.str("outcome") == "conflict");
    CHECK_FALSE(bad.flag("committed"));
    CHECK(material->parameter("color")->value() == oldColor);
    CHECK(f.opacity0() == 1.f);
    CHECK(node->value().getTransform() == vsr::math::IDENTITY_MAT4);
  }
  SECTION("valid mixed batch commits and restores each state exactly")
  {
    const auto request = ++id;
    const auto wire =
        args(request, "[[0,1,0,0],[1,0,0,0],[0,0,1,0],[0,0,0,1]]");
    REQUIRE(f.tool(s, "vsr_edit", wire).errorCode().empty());
    f.drain();
    const auto done = f.status(s, s.id, request);
    REQUIRE(done.str("outcome") == "ok");
    CHECK(done.flag("committed"));
    CHECK(done.result()->get("versions")->a.size() == 2);
    CHECK(done.result()->get("node_versions")->a.size() == 1);
    CHECK(material->parameter("color")->value().get<vsr::math::float3>()
        == vsr::math::float3(0.25f, 0.5f, 0.75f));
    CHECK(f.opacity0() == 0.5f);
    CHECK(node->value().getTransform()[3].xyz() == vsr::math::float3(4, 5, 6));
    const auto restoreRequest = ++id;
    REQUIRE(f.restoreBatch(s, restoreRequest, *done.result()->get("batch_ref"))
            .errorCode()
            .empty());
    f.drain();
    const auto restored = f.status(s, s.id, restoreRequest);
    REQUIRE(restored.str("outcome") == "ok");
    CHECK(restored.result()->get("versions")->a.size() == 2);
    CHECK(restored.result()->get("node_versions")->a.size() == 1);
    CHECK(restored.result()->get("image_versions")->a.empty());
    CHECK(material->parameter("color")->value() == oldColor);
    CHECK(f.opacity0() == 1.f);
    CHECK(node->value().getTransform() == vsr::math::IDENTITY_MAT4);
    CHECK(sampler->parameter("outTransform")->value() == oldValue);
    CHECK(f.tool(s, "vsr_edit", wire).flag("replayed"));
    CHECK(f.opacity0() == 1.f);
  }
}

SCENARIO(
    "MCP matrix edits obey revocation conflict and committed sync outcomes",
    "[integration][object-matrix][matrix-authority][mcp]")
{
  Fixture f({}, true);
  auto sampler = f.scene.createObject<vsr::scene::Sampler>("image2D");
  sampler->setName("matrix-sampler");
  auto s = f.attach();
  const auto before = inspectMatrixObject(f, s, 1);
  const auto oldValue = sampler->parameter("outTransform")->value();
  const auto args =
      matrixEditArgs(f, 2, before, "[[0,1,0,0],[1,0,0,0],[0,0,1,0],[0,0,0,1]]");
  SECTION("Disable agent edits denies initial admission")
  {
    f.authority->setAgentEditsDisabled(true);
    CHECK(f.tool(s, "vsr_edit", args).errorCode() == "forbidden");
    f.authority->setAgentEditsDisabled(false);
    REQUIRE(f.tool(s, "vsr_edit", args).str("state") == "pending");
    f.drain();
    CHECK(f.status(s, s.id, 2).str("outcome") == "ok");
  }
  SECTION("toggle revoked after admission is a terminal noncommit")
  {
    REQUIRE(f.tool(s, "vsr_edit", args).str("state") == "pending");
    f.authority->setAgentEditsDisabled(true);
    f.drain();
    const auto rejected = f.status(s, s.id, 2);
    CHECK(rejected.str("outcome") == "forbidden");
    CHECK_FALSE(rejected.flag("committed"));
    CHECK(rejected.flag("result_visible"));
    CHECK(sampler->parameter("outTransform")->value() == oldValue);
  }
  SECTION("EDIT authority revoked after admission is a terminal noncommit")
  {
    REQUIRE(f.tool(s, "vsr_edit", args).str("state") == "pending");
    f.authority->restrict(
        session::Target::scene(f.sceneId), session::Capability::EDIT);
    f.drain();
    const auto rejected = f.status(s, s.id, 2);
    CHECK(rejected.str("outcome") == "forbidden");
    CHECK_FALSE(rejected.flag("committed"));
    CHECK(sampler->parameter("outTransform")->value() == oldValue);
  }
  SECTION("postcommit sync failures retain truthful edit and restore receipts")
  {
    f.hookOk = false;
    REQUIRE(f.tool(s, "vsr_edit", args).str("state") == "pending");
    f.drain();
    const auto edited = f.status(s, s.id, 2);
    REQUIRE(edited.str("outcome") == "applied_with_sync_failure");
    CHECK(edited.flag("committed"));
    CHECK(edited.isError());
    CHECK(sampler->parameter("outTransform")->value() != oldValue);
    REQUIRE(f.restoreBatch(s, 3, *edited.result()->get("batch_ref"))
            .errorCode()
            .empty());
    f.drain();
    const auto restored = f.status(s, s.id, 3);
    CHECK(restored.str("outcome") == "applied_with_sync_failure");
    CHECK(restored.flag("committed"));
    CHECK(restored.result()->get("versions")->a.size() == 1);
    CHECK(restored.result()->get("image_versions")->a.empty());
    CHECK(sampler->parameter("outTransform")->value() == oldValue);
    REQUIRE(f.restoreBatch(s, 4, *edited.result()->get("batch_ref"))
            .errorCode()
            .empty());
    f.drain();
    CHECK(f.status(s, s.id, 4).str("outcome") == "already_restored");
    CHECK(f.tool(s, "vsr_edit", args).flag("replayed"));
    CHECK(sampler->parameter("outTransform")->value() == oldValue);
  }
  SECTION("later writes and change-back permanently conflict with restore")
  {
    REQUIRE(f.tool(s, "vsr_edit", args).str("state") == "pending");
    f.drain();
    const auto edited = f.status(s, s.id, 2);
    REQUIRE(edited.str("outcome") == "ok");
    const auto changed = sampler->parameter("outTransform")->value();
    sampler->setParameter("outTransform", oldValue);
    sampler->setParameter("outTransform", changed);
    REQUIRE(f.restoreBatch(s, 3, *edited.result()->get("batch_ref"))
            .errorCode()
            .empty());
    f.drain();
    const auto rejected = f.status(s, s.id, 3);
    CHECK(rejected.str("outcome") == "conflict");
    CHECK_FALSE(rejected.flag("committed"));
    CHECK(sampler->parameter("outTransform")->value() == changed);
    sampler->setParameter("outTransform", oldValue);
    REQUIRE(f.restoreBatch(s, 4, *edited.result()->get("batch_ref"))
            .errorCode()
            .empty());
    f.drain();
    CHECK(f.status(s, s.id, 4).str("outcome") == "conflict");
  }
}

SCENARIO(
    "MCP Image Exchange discovery reports only the authenticated session capacity",
    "[integration][mcp][image-export][image-export-discovery]")
{
  Fixture f({}, true, false, false, 120., true);
  const auto s = f.attach();
  f.tool(s, "vsr_discover", "{\"request_id\":1}");
  const auto discovered = f.await(s, 1);
  const auto &items = discovered.result()->get("items")->a;
  REQUIRE_FALSE(items.empty());
  const auto *limits =
      items.at(0).get("image_exchange")->get("exchange_limits");
  REQUIRE(limits->get("session_retained_physical_bytes"));
  CHECK(limits->get("session_retained_physical_bytes")->n == 0);
  descriptor::Descriptor published;
  REQUIRE(descriptor::read(f.server->published().path(), published).ok());
  json::Value metadata;
  REQUIRE(json::parse(published.imageExchange, metadata));
  CHECK_FALSE(
      metadata.get("exchange_limits")->get("session_retained_physical_bytes"));
}

struct PositionFixture : Fixture
{
  vsr::scene::ArrayRef positions;
  vsr::scene::GeometryRef geometry;
  vsr::scene::GeometryRef other;
  static constexpr float POINTS[] = {-1, -1, 0, 1, -1, 0, 0, 1, 0};
  PositionFixture(image::ExchangeLimits limits = {}, bool nodeEdits = false);
  Reply working(const Session &session,
      const std::string &mode = "",
      bool topology = false);
  Reply importPositions(const Session &session,
      std::uint64_t id,
      const json::Value &evidence,
      const std::string &file = "edited.vmesh");
};

PositionFixture::PositionFixture(image::ExchangeLimits limits, bool nodeEdits)
    : Fixture({}, true, nodeEdits, false, 120., true, UINT64_MAX, limits, true)
{
  auto hole = scene.createArray(ANARI_FLOAT32_VEC3, 1);
  positions = scene.createArray(ANARI_FLOAT32_VEC3, 3);
  scene.removeObject(hole.data());
  positions->setData(static_cast<const void *>(POINTS));
  geometry = scene.createObject<vsr::scene::Geometry>("triangle");
  geometry->setParameterObject("vertex.position", *positions);
  other = scene.createObject<vsr::scene::Geometry>("triangle");
  other->setParameterObject("vertex.position", *positions);
  auto material = scene.createObject<vsr::scene::Material>("matte");
  auto root = scene.defaultLayer()->root();
  scene.insertChildObjectNode(
      root, scene.createSurface("target", geometry, material));
  scene.insertChildObjectNode(
      root, scene.createSurface("other", other, material));
}

Reply PositionFixture::working(
    const Session &session, const std::string &mode, bool topology)
{
  REQUIRE(exportMesh(session, 1, geometry.data()).str("kind") == "mesh_export");
  auto exported = await(session, 1);
  REQUIRE(exported.str("outcome") == "ok");
  const auto path = exchangePath();
  const auto command = std::string(VSR_MESH_TEST_PYTHON) + " "
      + VSR_MESH_TEST_SOURCE
      + (topology ? "/scripts/test-mesh-topology.py '"
                  : "/scripts/test-mesh-position.py '")
      + path + "/" + exported.result()->get("file_name")->s + "' '" + path
      + "/edited.vmesh' " + mode;
  REQUIRE(std::system(command.c_str()) == 0);
  return exported;
}

Reply PositionFixture::importPositions(const Session &session,
    std::uint64_t id,
    const json::Value &evidence,
    const std::string &file)
{
  return tool(session,
      "vsr_mesh_import",
      "{\"request_id\":" + std::to_string(id)
          + ",\"scene_id\":" + std::to_string(sceneId) + ",\"handle\":"
          + json::quote(objectHandle(geometry.data())) + ",\"target_version\":"
          + json::write(*evidence.get("target_version"))
          + ",\"dependencies\":" + json::write(*evidence.get("dependencies"))
          + ",\"file_name\":" + json::quote(file) + "}");
}

namespace {
std::vector<unsigned char> inputPng(std::size_t width,
    std::size_t height,
    unsigned char colorType,
    const std::vector<unsigned char> &filtered,
    const std::vector<std::pair<std::string, std::vector<unsigned char>>>
        &chunks);
void writeInput(
    const std::string &path, const std::vector<unsigned char> &bytes);
} // namespace

TEST_CASE("Public mixed recovery retains the newest complete FIFO suffix",
    "[integration][mcp][retention-fifo]")
{
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 400000;
  PositionFixture f(limits, true);
  const bool failSync = GENERATE(false, true);
  const auto a = f.attach();
  const auto evidence = f.working(a);
  constexpr std::size_t BYTES = 262144;
  const std::vector<unsigned char> before(BYTES, 71), rows(BYTES + 256, 0);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
  image->setData(static_cast<const void *>(before.data()));
  writeInput(f.exchangePath() + "/agent.png", inputPng(256, 256, 6, rows, {}));
  const auto h = f.sceneIds->handleOf(image.data());
  f.importImage(a,
      2,
      f.objectHandle(image.data()),
      *f.sceneIds->imageDataVersion(h),
      "agent.png");
  const auto first = f.await(a, 2);
  REQUIRE(first.flag("committed"));
  const auto firstBatch = *first.result()->get("batch_ref");
  auto pinned = f.edits->restorations().find(
      {firstBatch.get("launch_id")->s, firstBatch.get("batch_id")->s});
  REQUIRE(pinned);
  REQUIRE(f.rpc("vsr/detach", "{}", &a).status == 200);
  const auto b = f.attach();
  // A status read from another session must not renew the old entry's age.
  REQUIRE(f.status(b, a.id, 2).flag("committed"));
  f.importPositions(b, 1, *evidence.result());
  const auto mesh = f.await(b, 1);
  REQUIRE(mesh.flag("committed"));
  const auto meshBatch = *mesh.result()->get("batch_ref");
  json::Value ownership;
  REQUIRE(json::parse(f.images->metadata(f.sceneId, b.id), ownership));
  CHECK(ownership.get("exchange_limits")->get("retained_physical_bytes")->n
      > limits.globalRetentionBytes);
  CHECK(ownership.get("exchange_limits")
            ->get("available_global_retention_bytes")
            ->n
      < limits.globalRetentionBytes);
  f.restoreBatch(b, 3, firstBatch);
  CHECK(f.await(b, 3).str("outcome") == "outcome_unavailable");
  CHECK(std::memcmp(image->data(), rows.data(), BYTES) == 0);

  auto *layer = f.scene.defaultLayer();
  auto node = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, "fifo-node"));
  REQUIRE(
      f.tool(b,
           "vsr_inspect",
           "{\"request_id\":4,\"scene_id\":" + std::to_string(f.sceneId)
               + ",\"kind\":\"nodes\",\"name\":\"fifo-node\"}")
          .errorCode()
          .empty());
  const auto inspected = f.await(b, 4);
  const auto item = inspected.result()->get("items")->a.at(0);
  REQUIRE(
      f.tool(b,
           "vsr_edit",
           "{\"request_id\":5,\"scene_id\":" + std::to_string(f.sceneId)
               + ",\"edits\":[{\"target_kind\":\"node\",\"handle\":"
               + json::write(*item.get("handle"))
               + ",\"version\":" + json::write(*item.get("node_edit_version"))
               + ",\"structural_version\":"
               + json::write(*item.get("structural_version"))
               + ",\"parameter\":\"enabled\",\"value\":false}]}")
          .errorCode()
          .empty());
  const auto generic = f.await(b, 5);
  REQUIRE(generic.flag("committed"));
  CHECK_FALSE(node->value().isEnabled());
  const auto genericBatch = *generic.result()->get("batch_ref");
  // Reading the older Mesh after the Node must not turn FIFO into LRU.
  CHECK(f.status(b, b.id, 1).flag("committed"));
  // Next Image publication evicts Mesh, not the newer small Node batch.
  std::vector<unsigned char> changedRows(BYTES + 256, 0);
  for (std::size_t y = 0; y < 256; ++y)
    std::fill_n(changedRows.begin() + y * 1025 + 1, 1024, 23);
  writeInput(
      f.exchangePath() + "/next.png", inputPng(256, 256, 6, changedRows, {}));
  f.hookOk = !failSync;
  f.importImage(b,
      6,
      f.objectHandle(image.data()),
      *f.sceneIds->imageDataVersion(h),
      "next.png");
  const auto newest = f.await(b, 6);
  REQUIRE(newest.flag("committed"));
  CHECK(
      newest.str("outcome") == (failSync ? "applied_with_sync_failure" : "ok"));
  f.hookOk = true;
  f.restoreBatch(b, 7, meshBatch);
  CHECK(f.await(b, 7).str("outcome") == "outcome_unavailable");
  REQUIRE(f.importPositions(b, 1, *evidence.result()).flag("replayed"));
  f.restoreBatch(b, 8, genericBatch);
  REQUIRE(f.await(b, 8).flag("committed"));
  CHECK(node->value().isEnabled());
  f.restoreBatch(b, 9, *newest.result()->get("batch_ref"));
  REQUIRE(f.await(b, 9).flag("committed"));
  const std::vector<unsigned char> zero(BYTES, 0);
  CHECK(std::memcmp(image->data(), zero.data(), BYTES) == 0);
  CHECK(f.other->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK(std::memcmp(f.positions->data(),
            PositionFixture::POINTS,
            sizeof(PositionFixture::POINTS))
      == 0);
  CHECK(std::filesystem::exists(f.exchangePath() + "/agent.png"));
  CHECK(std::filesystem::exists(f.exchangePath() + "/edited.vmesh"));
  CHECK(std::memcmp(pinned->image->bytes.data(), before.data(), BYTES) == 0);
  std::thread release([&] { pinned.reset(); });
  release.join();
  f.images->pump();
  REQUIRE(json::parse(f.images->metadata(f.sceneId, b.id), ownership));
  CHECK(
      ownership.get("exchange_limits")->get("retained_physical_bytes")->n == 0);
}

TEST_CASE("Public Image and Mesh retain oversized recovery alone",
    "[integration][mcp][retention-oversized]")
{
  const bool meshOversized = GENERATE(false, true);
  const bool failSync = GENERATE(false, true);
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = meshOversized ? 120000 : 400000;
  PositionFixture f(limits, true);
  const auto session = f.attach();
  const auto evidence = f.working(session);
  const std::size_t side = meshOversized ? 1 : 512;
  const std::size_t bytes = side * side * 4;
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, side, side);
  const std::vector<unsigned char> before(bytes, 71), rows(bytes + side, 0);
  image->setData(static_cast<const void *>(before.data()));
  writeInput(
      f.exchangePath() + "/oversized.png", inputPng(side, side, 6, rows, {}));
  const auto handle = f.sceneIds->handleOf(image.data());
  const auto imageVersion = *f.sceneIds->imageDataVersion(handle);
  const auto importImage = [&](std::uint64_t id) {
    f.importImage(session,
        id,
        f.objectHandle(image.data()),
        imageVersion,
        "oversized.png");
    return f.await(session, id);
  };
  const auto importMesh = [&](std::uint64_t id) {
    f.importPositions(session, id, *evidence.result());
    return f.await(session, id);
  };
  const auto older = meshOversized ? importImage(2) : importMesh(2);
  REQUIRE(older.flag("committed"));
  const auto oldBatch = *older.result()->get("batch_ref");
  CHECK(f.imageActivity->usage().retentionBytes <= limits.globalRetentionBytes);
  REQUIRE(f.restores->eligible(
      {oldBatch.get("launch_id")->s, oldBatch.get("batch_id")->s}));
  f.hookOk = !failSync;
  const auto oversized = meshOversized ? importMesh(3) : importImage(3);
  INFO(json::write(oversized.json));
  REQUIRE(oversized.flag("committed"));
  CHECK(oversized.str("outcome")
      == (failSync ? "applied_with_sync_failure" : "ok"));
  f.hookOk = true;
  const auto batch = *oversized.result()->get("batch_ref");
  const auto *policy = oversized.result()->get("restoration_policy");
  REQUIRE(policy);
  REQUIRE(policy->get("oversized"));
  CHECK(policy->get("oversized")->b);
  json::Value metadata;
  REQUIRE(json::parse(f.images->metadata(f.sceneId, session.id), metadata));
  const auto *capacity = metadata.get("exchange_limits");
  REQUIRE(capacity->get("oversized_retention"));
  CHECK(capacity->get("oversized_retention")->b);
  CHECK(capacity->get("available_global_retention_bytes")->n == 0);
  CHECK(
      capacity->get("effective_single_entry_hard_cap_bytes")->n == 8589934592.);
  CHECK(f.imageActivity->usage().restorableBatches == 1);
  CHECK(f.imageActivity->usage().retentionBytes > limits.globalRetentionBytes);
  f.restoreBatch(session, 4, oldBatch);
  CHECK(f.await(session, 4).str("outcome") == "outcome_unavailable");
  // Replay cannot re-publish, evict again or refresh recovery age.
  CHECK((meshOversized ? f.importPositions(session, 3, *evidence.result())
                       : f.importImage(session,
                             3,
                             f.objectHandle(image.data()),
                             imageVersion,
                             "oversized.png"))
          .flag("replayed"));
  SECTION("oversized entry restores exact data and bindings")
  {
    f.restoreBatch(session, 5, batch);
    REQUIRE(f.await(session, 5).flag("committed"));
    if (meshOversized) {
      CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>(
                "vertex.position")
          == f.positions.data());
      CHECK(std::memcmp(f.positions->data(),
                PositionFixture::POINTS,
                sizeof(PositionFixture::POINTS))
          == 0);
    } else {
      CHECK(std::memcmp(image->data(), before.data(), bytes) == 0);
    }
  }
  SECTION("the next ordinary edit returns to the soft budget")
  {
    auto held = f.edits->restorations().find(
        {batch.get("launch_id")->s, batch.get("batch_id")->s});
    REQUIRE(held);
    f.tool(session,
        "vsr_inspect",
        "{\"request_id\":5,\"scene_id\":" + std::to_string(f.sceneId)
            + ",\"object_type\":\"ANARI_MATERIAL\"}");
    const auto inspected = f.await(session, 5);
    const auto item = inspected.result()->get("items")->a.at(0);
    const auto target = "\"handle\":" + json::write(*item.get("handle"))
        + ",\"version\":" + json::write(*item.get("version"));
    f.editOpacity(session, 6, target, 0.25);
    const auto ordinary = f.await(session, 6);
    INFO(json::write(ordinary.json));
    REQUIRE(ordinary.flag("committed"));
    CHECK(f.imageActivity->usage().restorableBatches == 1);
    CHECK(
        f.imageActivity->usage().retentionBytes <= limits.globalRetentionBytes);
    REQUIRE(json::parse(f.images->metadata(f.sceneId, session.id), metadata));
    CHECK_FALSE(metadata.get("exchange_limits")->get("oversized_retention")->b);
    f.restoreBatch(session, 7, batch);
    CHECK(f.await(session, 7).str("outcome") == "outcome_unavailable");
    f.restoreBatch(session, 8, *ordinary.result()->get("batch_ref"));
    REQUIRE(f.await(session, 8).flag("committed"));
    REQUIRE(json::parse(f.images->metadata(f.sceneId, session.id), metadata));
    CHECK(
        metadata.get("exchange_limits")->get("retained_physical_bytes")->n > 0);
    std::thread release([&] { held.reset(); });
    release.join();
    f.images->pump();
    REQUIRE(json::parse(f.images->metadata(f.sceneId, session.id), metadata));
    CHECK(metadata.get("exchange_limits")->get("retained_physical_bytes")->n
        == 0);
  }
  CHECK(f.other->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK(std::filesystem::exists(f.exchangePath() + "/oversized.png"));
  CHECK(std::filesystem::exists(f.exchangePath() + "/edited.vmesh"));
}

TEST_CASE(
    "Public above-hard Image and Mesh refusal preserves Scene and recovery",
    "[integration][mcp][retention-oversized]")
{
  const bool mesh = GENERATE(false, true);
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 110000;
  limits.hardRetentionBytes = 200000;
  PositionFixture f(limits);
  const auto session = f.attach();
  const auto evidence = f.working(session);
  auto small = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  const unsigned char original[] = {17, 29, 41, 53};
  small->setData(static_cast<const void *>(original));
  writeInput(
      f.exchangePath() + "/small.png", inputPng(1, 1, 6, {0, 1, 2, 3, 4}, {}));
  f.importImage(session,
      2,
      f.objectHandle(small.data()),
      *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(small.data())),
      "small.png");
  const auto previous = f.await(session, 2);
  REQUIRE(previous.flag("committed"));
  const auto previousBatch = *previous.result()->get("batch_ref");
  auto large = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
  const std::vector<unsigned char> originalLarge(262144, 71), rows(262400, 0);
  large->setData(static_cast<const void *>(originalLarge.data()));
  writeInput(f.exchangePath() + "/large.png", inputPng(256, 256, 6, rows, {}));
  const auto version =
      *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(large.data()));
  const auto revision = f.sceneIds->sceneRevision();
  const auto retained = f.imageActivity->usage().retentionBytes;
  const auto arrays = f.scene.numberOfObjects(ANARI_ARRAY);
  const auto submit = [&] {
    return mesh
        ? f.importPositions(session, 3, *evidence.result())
        : f.importImage(
              session, 3, f.objectHandle(large.data()), version, "large.png");
  };
  submit();
  const auto denied = f.await(session, 3);
  INFO(json::write(denied.json));
  REQUIRE(denied.str("outcome") == "busy");
  CHECK_FALSE(denied.flag("committed"));
  REQUIRE(denied.result());
  CHECK(denied.result()->get("reason")->s == "restoration_approval_required");
  CHECK(denied.result()->get("required_capacity_bytes")->n > 200000);
  CHECK(denied.result()->get("effective_soft_limit_bytes")->n == 110000);
  CHECK(denied.result()->get("effective_hard_limit_bytes")->n == 200000);
  CHECK(denied.result()->get("approval_required")->b);
  CHECK(denied.result()->get("approval_available")->b);
  CHECK(denied.result()->get("history_unchanged")->b);
  CHECK_FALSE(denied.result()->get("committed")->b);
  CHECK(f.sceneIds->sceneRevision() == revision);
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == arrays);
  CHECK(f.imageActivity->usage().retentionBytes == retained);
  CHECK(f.imageActivity->usage().reservedRetentionBytes == 0);
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK(std::memcmp(large->data(), originalLarge.data(), originalLarge.size())
      == 0);
  CHECK(submit().flag("replayed"));
  f.restoreBatch(session, 4, previousBatch);
  REQUIRE(f.await(session, 4).flag("committed"));
  CHECK(std::memcmp(small->data(), original, sizeof(original)) == 0);
}

TEST_CASE("Public parameter batches obey the same per-entry hard cap",
    "[integration][mcp][retention-oversized]")
{
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 9000;
  limits.hardRetentionBytes = 10000;
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, limits, true);
  const auto session = f.attach();
  const auto target = f.firstTarget(session, 1);
  f.editOpacity(session, 2, target, 0.25);
  const auto previous = f.await(session, 2);
  INFO(json::write(previous.json));
  REQUIRE(previous.flag("committed"));
  const auto previousBatch = *previous.result()->get("batch_ref");
  const auto retained = f.imageActivity->usage().retentionBytes;
  for (int i = 0; i < 15; ++i)
    f.scene.createObject<vsr::scene::Material>("matte")->setParameter(
        "opacity", 1.f);
  f.tool(session,
      "vsr_inspect",
      "{\"request_id\":3,\"scene_id\":" + std::to_string(f.sceneId)
          + ",\"object_type\":\"ANARI_MATERIAL\"}");
  const auto inspected = f.await(session, 3);
  std::string edits;
  for (const auto &item : inspected.result()->get("items")->a) {
    if (!edits.empty())
      edits += ',';
    edits += "{\"handle\":" + json::write(*item.get("handle"))
        + ",\"version\":" + json::write(*item.get("version"))
        + ",\"parameter\":\"opacity\",\"value\":0.5}";
  }
  const auto revision = f.sceneIds->sceneRevision();
  f.tool(session,
      "vsr_edit",
      "{\"request_id\":4,\"scene_id\":" + std::to_string(f.sceneId)
          + ",\"edits\":[" + edits + "]}");
  const auto refused = f.await(session, 4);
  INFO(json::write(refused.json));
  REQUIRE(refused.str("outcome") == "busy");
  CHECK_FALSE(refused.flag("committed"));
  REQUIRE(refused.result());
  CHECK(refused.result()->get("required_capacity_bytes")->n > 10000);
  CHECK(refused.result()->get("effective_hard_limit_bytes")->n == 10000);
  CHECK(refused.result()->get("approval_required")->b);
  CHECK(f.sceneIds->sceneRevision() == revision);
  CHECK(f.imageActivity->usage().retentionBytes == retained);
  CHECK(f.opacity0() == 0.25f);
  f.restoreBatch(session, 5, previousBatch);
  REQUIRE(f.await(session, 5).flag("committed"));
  CHECK(f.opacity0() == 1.f);
}

TEST_CASE("Public oversized preparation fits host hard-aware physical headroom",
    "[integration][mcp][retention-oversized][retention-physical]")
{
  const bool legacyPhysicalCap = GENERATE(false, true);
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 120000;
  limits.hardRetentionBytes = 600000;
  // 256x256 RGBA: T=262144, F=262400, E=590336, W=2098176.
  // Import peak E+D+W+S is exactly 3212800 bytes. Use a practical scaled
  // transient ceiling, not a larger decoder/payload scope or a lease bypass.
  limits.transientBytes = 3212800;
  limits.physicalByteCap = legacyPhysicalCap
      ? 3452800 // old 2*soft + transient
      : vsr::app::agentPhysicalByteCap(limits.globalRetentionBytes,
            limits.hardRetentionBytes,
            limits.transientBytes);
  const auto physical =
      std::make_shared<vsr::rendering::FrameBudget>(limits.physicalByteCap);
  limits.reservePhysicalMemory = [physical](std::size_t bytes) {
    return physical->reserve(bytes);
  };
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, limits, true);
  const auto session = f.attach();
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
  const std::vector<unsigned char> before(262144, 71), rows(262400, 0);
  image->setData(static_cast<const void *>(before.data()));
  writeInput(f.exchangePath() + "/first.png", inputPng(256, 256, 6, rows, {}));
  const auto handle = f.sceneIds->handleOf(image.data());
  f.importImage(session,
      1,
      f.objectHandle(image.data()),
      *f.sceneIds->imageDataVersion(handle),
      "first.png");
  const auto first = f.await(session, 1);
  REQUIRE(first.flag("committed"));
  const auto batch = *first.result()->get("batch_ref");
  std::vector<unsigned char> changed(262400, 0);
  for (std::size_t y = 0; y < 256; ++y)
    std::fill_n(changed.begin() + y * 1025 + 1, 1024, 23);
  writeInput(
      f.exchangePath() + "/second.png", inputPng(256, 256, 6, changed, {}));
  f.importImage(session,
      2,
      f.objectHandle(image.data()),
      *f.sceneIds->imageDataVersion(handle),
      "second.png");
  const auto second = f.await(session, 2);
  INFO(json::write(second.json));
  CHECK(second.flag("committed") == !legacyPhysicalCap);
  if (legacyPhysicalCap) {
    REQUIRE(second.str("outcome") == "busy");
    CHECK(second.result()->get("reason")->s == "resource_limit");
    f.restoreBatch(session, 3, batch);
    REQUIRE(f.await(session, 3).flag("committed"));
    CHECK(std::memcmp(image->data(), before.data(), before.size()) == 0);
  } else {
    REQUIRE(second.str("outcome") == "ok");
    CHECK(physical->limit() == 4412800);
    CHECK(physical->used() <= physical->limit());
    CHECK(f.imageActivity->usage().restorableBatches == 1);
    CHECK(f.imageActivity->usage().oversizedRetention);
    f.restoreBatch(session, 3, batch);
    CHECK(f.await(session, 3).str("outcome") == "outcome_unavailable");
    f.restoreBatch(session, 4, *second.result()->get("batch_ref"));
    REQUIRE(f.await(session, 4).flag("committed"));
    const std::vector<unsigned char> zero(262144, 0);
    CHECK(std::memcmp(image->data(), zero.data(), zero.size()) == 0);
  }
}

SCENARIO("MCP position import checks scoped freshness before I/O and commit",
    "[integration][mcp][mesh-import][mesh-import-guards]")
{
  PositionFixture f;
  const auto session = f.attach();
  const auto evidence = f.working(session);
  const auto arrayCount = f.scene.numberOfObjects(ANARI_ARRAY);
  std::string expected = "conflict";
  bool disableAtExecution = false;
  SECTION("ordinary dependency writes and change back do not revive evidence")
  {
    f.positions->setData(static_cast<const void *>(PositionFixture::POINTS));
  }
  SECTION("binding change back does not revive target evidence")
  {
    f.geometry->parameter("vertex.position")->setEnabled(false);
    f.geometry->parameter("vertex.position")->setEnabled(true);
  }
  SECTION("a required ordinary map stays open")
  {
    REQUIRE(f.positions->map());
    expected = "busy";
  }
  SECTION("unrelated Geometry and camera work does not invalidate evidence")
  {
    f.other->setParameter("unrelated", 1.f);
    auto camera = f.scene.createObject<vsr::scene::Camera>("perspective");
    camera->setParameter("position", vsr::math::float3(4, 5, 6));
    expected = "ok";
  }
  SECTION("agent edits disabled at execution forbids before file errors")
  {
    disableAtExecution = true;
    expected = "forbidden";
  }
  REQUIRE(f.importPositions(session, 2, *evidence.result()).str("kind")
      == "mesh_import");
  if (disableAtExecution)
    f.authority->setAgentEditsDisabled(true);
  const auto result = f.await(session, 2);
  CHECK(result.str("outcome") == expected);
  if (expected != "ok") {
    CHECK_FALSE(result.flag("committed"));
    CHECK(
        f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
        == f.positions.data());
    CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == arrayCount);
    CHECK(f.edits->restorations().size() == 0);
  }
  if (f.positions->isMapped())
    f.positions->unmap();
}

SCENARIO("MCP position restoration protects both old and replacement data",
    "[integration][mcp][mesh-import][mesh-import-restore]")
{
  PositionFixture f;
  const auto session = f.attach();
  const auto evidence = f.working(session);
  f.importPositions(session, 2, *evidence.result());
  const auto imported = f.await(session, 2);
  REQUIRE(imported.flag("committed"));
  const auto batch = *imported.result()->get("batch_ref");
  auto *replacement =
      f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position");
  const auto replacementHandle = f.sceneIds->handleOf(replacement);
  const auto priorHandle = f.sceneIds->handleOf(f.positions.data());
  std::string expected = "conflict";
  SECTION("another Geometry mutates retained old positions")
  {
    f.other->parameterValueAsObject<vsr::scene::Array>("vertex.position")
        ->setData(static_cast<const void *>(PositionFixture::POINTS));
  }
  SECTION("replacement data changes back")
  {
    const auto bytes = std::vector<std::uint8_t>(
        static_cast<const std::uint8_t *>(replacement->data()),
        static_cast<const std::uint8_t *>(replacement->data())
            + sizeof(PositionFixture::POINTS));
    replacement->setData(static_cast<const void *>(PositionFixture::POINTS));
    replacement->setData(static_cast<const void *>(bytes.data()));
  }
  SECTION("explicit old Array deletion cannot be repaired by cloning")
  {
    f.scene.removeObject(f.positions.data());
  }
  SECTION("mapped prior data remains busy and eligible")
  {
    REQUIRE(f.positions->map());
    expected = "busy";
  }
  SECTION("unrelated user adopts replacement and survives exact restore")
  {
    f.other->setParameterObject("vertex.position", *replacement);
    expected = "ok";
  }
  SECTION("ordinary GC and Array relocation cannot collect retained identities")
  {
    f.other->removeParameter("vertex.position");
    for (int i = 0; i < 150; ++i)
      f.scene.createArray(ANARI_FLOAT32_VEC3, 3);
    f.scene.removeUnusedObjects();
    f.scene.defragmentObjectStorage();
    expected = "ok";
  }
  f.restoreBatch(session, 3, batch);
  const auto result = f.await(session, 3);
  CHECK(result.str("outcome") == expected);
  CHECK(result.flag("committed") == (expected == "ok"));
  if (expected == "conflict") {
    f.restoreBatch(session, 4, batch);
    CHECK(f.await(session, 4).str("outcome") == "conflict");
  } else if (expected == "ok") {
    CHECK(f.sceneIds->handleOf(
              f.geometry->parameterValueAsObject<vsr::scene::Array>(
                  "vertex.position"))
        == priorHandle);
    CHECK(std::memcmp(
              f.geometry
                  ->parameterValueAsObject<vsr::scene::Array>("vertex.position")
                  ->data(),
              PositionFixture::POINTS,
              sizeof(PositionFixture::POINTS))
        == 0);
    f.restoreBatch(session, 4, batch);
    CHECK(f.await(session, 4).str("outcome") == "already_restored");
  } else {
    CHECK(f.restores->eligible(
        {batch.get("launch_id")->s, batch.get("batch_id")->s}));
    f.positions->unmap();
  }
  // Adoption preserves replacement identity; unadopted data is collectible.
  if (expected == "ok") {
    const auto *adopter =
        f.other->parameterValueAsObject<vsr::scene::Array>("vertex.position");
    const bool adopted =
        adopter && adopter == f.sceneIds->resolve(replacementHandle);
    f.images->pump();
    f.scene.removeUnusedObjects();
    CHECK((f.sceneIds->resolve(replacementHandle) != nullptr) == adopted);
  }
}

SCENARIO(
    "MCP position import preserves aliases and accepts independently authored data",
    "[integration][mcp][mesh-import]")
{
  PositionFixture f;
  std::string mode = "independent";
  SECTION("independent source has no export receipt metadata") {}
  SECTION("selective replacement splits only the declared position alias")
  {
    f.geometry->setParameterObject("vertex.normal", *f.positions);
    f.geometry->parameter("vertex.normal")->setEnabled(false);
    mode = "split";
  }
  const auto session = f.attach();
  const auto evidence = f.working(session, mode);
  f.importPositions(session, 2, *evidence.result());
  auto result = f.await(session, 2);
  REQUIRE(result.flag("committed"));
  if (mode == "split") {
    CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.normal")
        == f.positions.data());
    CHECK_FALSE(f.geometry->parameter("vertex.normal")->isEnabled());
    REQUIRE(result.result()->get("dependencies")->a.size() == 2);
  }
  // Returned evidence is immediately usable; equivalent data is a no-op.
  f.importPositions(session, 3, *result.result());
  const auto unchanged = f.await(session, 3);
  INFO(json::write(unchanged.json));
  INFO(json::write(result.json));
  REQUIRE(unchanged.str("outcome") == "ok");
  CHECK_FALSE(unchanged.flag("committed"));
  f.restoreBatch(session, 4, *result.result()->get("batch_ref"));
  CHECK(f.await(session, 4).flag("committed"));
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
}

SCENARIO(
    "MCP position import refuses insufficient retained capacity before publication",
    "[integration][mcp][mesh-import][mesh-import-capacity]")
{
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 4096;
  limits.hardRetentionBytes = 4096;
  PositionFixture f(limits);
  const auto session = f.attach();
  const auto evidence = f.working(session);
  const auto arrayCount = f.scene.numberOfObjects(ANARI_ARRAY);
  f.importPositions(session, 2, *evidence.result());
  const auto result = f.await(session, 2);
  REQUIRE(result.str("outcome") == "busy");
  CHECK_FALSE(result.flag("committed"));
  REQUIRE(result.result()->get("required_capacity_bytes"));
  CHECK(result.result()->get("required_capacity_bytes")->n > 4096);
  CHECK(result.result()->get("configured_capacity_bytes")->n == 4096);
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == arrayCount);
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK(f.edits->restorations().size() == 0);
}

struct ThrowMeshNotifications : vsr::scene::EmptyUpdateDelegate
{
  bool fail{true};
  void signalParameterUpdated(
      const vsr::scene::Object *object, const vsr::scene::Parameter *) override;
  void signalParameterBatchUpdated(const vsr::scene::Object *object,
      const std::vector<const vsr::scene::Parameter *> &) override;
};

void ThrowMeshNotifications::signalParameterBatchUpdated(
    const vsr::scene::Object *object,
    const std::vector<const vsr::scene::Parameter *> &)
{
  signalParameterUpdated(object, nullptr);
}

void ThrowMeshNotifications::signalParameterUpdated(
    const vsr::scene::Object *object, const vsr::scene::Parameter *)
{
  if (fail && object->type() == ANARI_GEOMETRY)
    throw std::runtime_error("test downstream mesh notification failure");
}

SCENARIO(
    "MCP position synchronization failure remains committed and restorable",
    "[integration][mcp][mesh-import][mesh-import-notification]")
{
  const bool topology = GENERATE(false, true);
  PositionFixture f;
  const auto session = f.attach();
  const auto evidence = f.working(session, topology ? "tracer" : "", topology);
  auto *notifications =
      f.scene.updateDelegate().emplace<ThrowMeshNotifications>();
  SECTION("downstream Geometry notification fails") {}
  SECTION("normal synchronization hook fails")
  {
    notifications->fail = false;
    f.hookOk = false;
  }
  f.importPositions(session, 2, *evidence.result());
  const auto imported = f.await(session, 2);
  REQUIRE(imported.flag("committed"));
  REQUIRE(imported.str("outcome") == "applied_with_sync_failure");
  const auto batch = *imported.result()->get("batch_ref");
  CHECK(f.restores->eligible(
      {batch.get("launch_id")->s, batch.get("batch_id")->s}));
  f.restoreBatch(session, 3, batch);
  const auto restored = f.await(session, 3);
  REQUIRE(restored.flag("committed"));
  CHECK(restored.str("outcome") == "applied_with_sync_failure");
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK(std::memcmp(f.positions->data(),
            PositionFixture::POINTS,
            sizeof(PositionFixture::POINTS))
      == 0);
  f.restoreBatch(session, 4, batch);
  CHECK(f.await(session, 4).str("outcome") == "already_restored");
}

SCENARIO(
    "MCP position reservations expose no staging and unwind precommit refusal",
    "[integration][mcp][mesh-import][mesh-import-memory]")
{
  const bool topology = GENERATE(false, true);
  PositionFixture *host = nullptr;
  bool observe = false, escaped = false, deny = false,
       allocationFailure = false;
  bool preparing = false, stale = false, map = false;
  const auto physical =
      std::make_shared<vsr::rendering::FrameBudget>(16u << 20);
  image::ExchangeLimits limits;
  limits.reservePhysicalMemory =
      [&](std::size_t bytes) -> std::shared_ptr<const void> {
    if (observe && host) {
      if (host->geometry->parameterValueAsObject<vsr::scene::Array>(
              "vertex.position")
          == host->positions.data())
        escaped = escaped || host->scene.numberOfObjects(ANARI_ARRAY) != 1;
      if (bytes == 4096)
        preparing = true;
      if (bytes == 65536 && preparing && stale) {
        stale = false;
        host->positions->setData(
            static_cast<const void *>(PositionFixture::POINTS));
      }
      if (bytes == 65536 && preparing && map) {
        map = false;
        REQUIRE(host->positions->map());
      }
      if (bytes == 65536 && preparing && deny) {
        deny = false;
        host->authority->setAgentEditsDisabled(true);
      }
      if (bytes == 4096 && allocationFailure) {
        allocationFailure = false;
        return {};
      }
    }
    return physical->reserve(bytes);
  };
  PositionFixture f(limits);
  host = &f;
  const auto session = f.attach();
  const auto evidence = f.working(session, topology ? "tracer" : "", topology);
  observe = true;
  std::string expected = "ok";
  SECTION("reentrant host reservations only enumerate published objects") {}
  SECTION("authority withdrawal during preparation forbids before publication")
  {
    deny = true;
    expected = "forbidden";
  }
  SECTION("a required Array becoming mapped at receipt preparation rejects")
  {
    map = true;
    expected = "busy";
  }
  SECTION("a dependency write at receipt preparation is stale")
  {
    stale = true;
    expected = "conflict";
  }
  SECTION("control allocation failure unwinds before publication")
  {
    allocationFailure = true;
    expected = "busy";
  }
  f.importPositions(session, 2, *evidence.result());
  const auto result = f.await(session, 2);
  CHECK(result.str("outcome") == expected);
  CHECK_FALSE(escaped);
  if (expected != "ok") {
    CHECK_FALSE(result.flag("committed"));
    CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == 1);
    CHECK(f.edits->restorations().size() == 0);
    CHECK(
        f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
        == f.positions.data());
    f.authority->setAgentEditsDisabled(false);
    observe = false;
    if (f.positions->isMapped())
      f.positions->unmap();
    if (expected != "conflict" && expected != "busy") {
      f.importPositions(session, 3, *evidence.result());
      CHECK(f.await(session, 3).flag("committed"));
    }
    json::Value metadata;
    REQUIRE(json::parse(f.images->metadata(f.sceneId, session.id), metadata));
    if (expected == "busy" || expected == "conflict")
      CHECK(metadata.get("exchange_limits")
                ->get("session_retained_physical_bytes")
                ->n
          == 0);
  }
}

SCENARIO("MCP position restoration rechecks authority after preparation",
    "[integration][mcp][mesh-import][mesh-import-guards]")
{
  PositionFixture f;
  const auto session = f.attach();
  const auto evidence = f.working(session);
  f.importPositions(session, 2, *evidence.result());
  const auto imported = f.await(session, 2);
  REQUIRE(imported.flag("committed"));
  const auto batch = *imported.result()->get("batch_ref");
  f.restores->setFaultInjector([&] {
    f.authority->setAgentEditsDisabled(true);
    return false;
  });
  f.restoreBatch(session, 3, batch);
  const auto rejected = f.await(session, 3);
  CHECK(rejected.str("outcome") == "forbidden");
  CHECK_FALSE(rejected.flag("committed"));
  CHECK(f.restores->eligible(
      {batch.get("launch_id")->s, batch.get("batch_id")->s}));
  f.restores->setFaultInjector({});
  f.authority->setAgentEditsDisabled(false);
  f.restoreBatch(session, 4, batch);
  CHECK(f.await(session, 4).flag("committed"));
}

SCENARIO(
    "MCP position import refuses unsafe confined files without publishing objects",
    "[integration][mcp][mesh-import][mesh-import-files]")
{
  PositionFixture f;
  const auto session = f.attach();
  const auto evidence = f.working(session);
  std::string file = "edited.vmesh";
  bool envelope = false;
  SECTION("traversal is not a bundle name")
  {
    file = "../edited.vmesh";
    envelope = true;
  }
  SECTION("absolute paths are not authority")
  {
    file = f.exchangePath() + "/edited.vmesh";
    envelope = true;
  }
  SECTION("symbolic links are never followed")
  {
    REQUIRE(
        ::symlink("edited.vmesh", (f.exchangePath() + "/link.vmesh").c_str())
        == 0);
    file = "link.vmesh";
  }
  SECTION("hard links cannot supply an input")
  {
    REQUIRE(::link((f.exchangePath() + "/edited.vmesh").c_str(),
                (f.exchangePath() + "/linked.vmesh").c_str())
        == 0);
    file = "linked.vmesh";
  }
  SECTION("malformed bundles cannot publish a staging Array")
  {
    std::ofstream(f.exchangePath() + "/edited.vmesh", std::ios::binary)
        << "VMESH01\nmalformed";
  }
  const auto admission =
      f.importPositions(session, 2, *evidence.result(), file);
  if (envelope)
    CHECK(admission.errorCode() == "invalid_request");
  else {
    const auto result = f.await(session, 2);
    CHECK(result.str("outcome") == "invalid_request");
    CHECK_FALSE(result.flag("committed"));
  }
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == 1);
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
}

SCENARIO(
    "MCP position import rejects exhausted tracking without a binding write",
    "[integration][mcp][mesh-import][mesh-import-exhaustion]")
{
  const bool topology = GENERATE(false, true);
  // The compound case permits two detached payload writes, then exhausts
  // admission. Even partially prepared Arrays must never enter the Scene.
  Fixture f({}, true, false, false, 120., true, topology ? 3 : 1, {}, true);
  auto positions = f.scene.createArray(ANARI_FLOAT32_VEC3, 3);
  positions->setData(static_cast<const void *>(PositionFixture::POINTS));
  auto geometry = f.scene.createObject<vsr::scene::Geometry>("triangle");
  geometry->setParameterObject("vertex.position", *positions);
  const auto session = f.attach();
  f.exportMesh(session, 1, geometry.data());
  const auto evidence = f.await(session, 1);
  REQUIRE(evidence.str("outcome") == "ok");
  const auto path = f.exchangePath();
  REQUIRE(std::system(
              (std::string(VSR_MESH_TEST_PYTHON) + " " + VSR_MESH_TEST_SOURCE
                  + (topology ? "/scripts/test-mesh-topology.py '"
                              : "/scripts/test-mesh-position.py '")
                  + path + "/" + evidence.result()->get("file_name")->s + "' '"
                  + path + "/edited.vmesh'")
                  .c_str())
      == 0);
  f.tool(session,
      "vsr_mesh_import",
      "{\"request_id\":2,\"scene_id\":" + std::to_string(f.sceneId)
          + ",\"handle\":" + json::quote(f.objectHandle(geometry.data()))
          + ",\"target_version\":"
          + json::write(*evidence.result()->get("target_version"))
          + ",\"dependencies\":"
          + json::write(*evidence.result()->get("dependencies"))
          + ",\"file_name\":\"edited.vmesh\"}");
  const auto rejected = f.await(session, 2);
  REQUIRE_FALSE(rejected.flag("committed"));
  CHECK(rejected.str("outcome") == "busy");
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == 1);
  CHECK(geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == positions.data());
  CHECK(std::memcmp(positions->data(),
            PositionFixture::POINTS,
            sizeof(PositionFixture::POINTS))
      == 0);
  CHECK(f.edits->restorations().size() == 0);
}

SCENARIO("MCP position import shares Image retention and directory quotas",
    "[integration][mcp][mesh-import][mesh-import-capacity]")
{
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 200000;
  limits.hardRetentionBytes = 200000;
  PositionFixture f(limits);
  const auto session = f.attach();
  const auto evidence = f.working(session);
  std::string expected = "busy";
  SECTION("an identical Image import creates no recovery or eviction")
  {
    auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 64, 64);
    const std::vector<unsigned char> oldPixels(16384, 71);
    image->setData(static_cast<const void *>(oldPixels.data()));
    const auto handle = f.objectHandle(image.data());
    f.exportImage(session, 2, handle);
    const auto exported = f.await(session, 2);
    REQUIRE(exported.str("outcome") == "ok");
    f.importImage(session,
        3,
        handle,
        exported.result()->get("data_version")->s,
        exported.result()->get("file_name")->s);
    const auto imported = f.await(session, 3);
    CHECK_FALSE(imported.flag("committed")); // identical exported bytes
    CHECK(imported.result()->get("unchanged")->b);
    CHECK(f.edits->restorations().size() == 0);
  }
  SECTION("agent-created working files cannot bypass the shared finite quota")
  {
    for (int i = 0; i < 1024; ++i)
      std::ofstream(f.exchangePath() + "/quota-" + std::to_string(i) + ".png")
          << 'x';
  }
  f.importPositions(session, 4, *evidence.result());
  const auto rejected = f.await(session, 4);
  CHECK(rejected.str("outcome") == expected);
  CHECK_FALSE(rejected.flag("committed"));
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK(std::memcmp(f.positions->data(),
            PositionFixture::POINTS,
            sizeof(PositionFixture::POINTS))
      == 0);
}

SCENARIO(
    "MCP position cancellation never fabricates rollback after publication",
    "[integration][mcp][mesh-import][mesh-import-cancellation]")
{
  PositionFixture f;
  const auto session = f.attach();
  const auto evidence = f.working(session);
  f.importPositions(session, 2, *evidence.result());
  bool afterCommit = false;
  SECTION("cancel before capture") {}
  SECTION("cancel after binding publication but before synchronization")
  {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (
        f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
        == f.positions.data()) {
      f.images->pump();
      f.integration.service();
      REQUIRE(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    afterCommit = true;
  }
  f.tool(
      session, "vsr_cancel", "{\"operation_ref\":" + ref(session.id, 2) + "}");
  const auto result = f.await(session, 2);
  CHECK(result.flag("committed") == afterCommit);
  CHECK(
      (f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
          != f.positions.data())
      == afterCommit);
  if (afterCommit) {
    REQUIRE(result.result());
    REQUIRE(result.result()->get("batch_ref"));
    f.restoreBatch(session, 3, *result.result()->get("batch_ref"));
    CHECK(f.await(session, 3).flag("committed"));
  } else {
    CHECK(result.str("outcome") == "cancelled");
    CHECK(f.edits->restorations().size() == 0);
  }
}

SCENARIO("MCP mesh import rejects incoherent actions without partial writes",
    "[integration][mcp][mesh-import][mesh-import-actions]")
{
  PositionFixture f;
  std::string mode;
  SECTION("count changes require compatible retained attributes")
  {
    f.geometry->setParameterObject("vertex.normal", *f.positions);
    mode = "count";
  }
  SECTION("enabled state changes are not position data edits")
  {
    mode = "state";
  }
  SECTION("per-primitive additions require primitive cardinality")
  {
    mode = "primitive";
  }
  const auto session = f.attach();
  const auto evidence = f.working(session, mode);
  const auto revision = f.sceneIds->sceneRevision();
  f.importPositions(session, 2, *evidence.result());
  const auto rejected = f.await(session, 2);
  CHECK(rejected.str("outcome") == "invalid_request");
  CHECK_FALSE(rejected.flag("committed"));
  CHECK(f.sceneIds->sceneRevision() == revision);
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == 1);
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK(f.geometry->parameter("vertex.position")->isEnabled());
  CHECK(bool(f.geometry->parameter("vertex.normal")) == (mode == "count"));
  CHECK_FALSE(f.geometry->parameter("primitive.color"));
}

struct ObserveMeshPublication : vsr::scene::EmptyUpdateDelegate
{
  std::function<void()> observe;
  void signalObjectAdded(const vsr::scene::Object *) override;
  void signalParameterBatchUpdated(const vsr::scene::Object *,
      const std::vector<const vsr::scene::Parameter *> &) override;
  void signalParameterUpdated(
      const vsr::scene::Object *, const vsr::scene::Parameter *) override;
  void signalParameterRemoved(
      const vsr::scene::Object *, const vsr::scene::Parameter *) override;
};

void ObserveMeshPublication::signalParameterBatchUpdated(
    const vsr::scene::Object *,
    const std::vector<const vsr::scene::Parameter *> &)
{
  if (observe)
    observe();
}

void ObserveMeshPublication::signalObjectAdded(const vsr::scene::Object *)
{
  if (observe)
    observe();
}
void ObserveMeshPublication::signalParameterUpdated(
    const vsr::scene::Object *, const vsr::scene::Parameter *)
{
  if (observe)
    observe();
}
void ObserveMeshPublication::signalParameterRemoved(
    const vsr::scene::Object *, const vsr::scene::Parameter *)
{
  if (observe)
    observe();
}

SCENARIO(
    "MCP compound mesh updates preserve associations and exact prior presence",
    "[integration][mcp][mesh-import][mesh-compound]")
{
  PositionFixture f;
  auto uv = f.scene.createArray(ANARI_FLOAT32_VEC2, 3);
  const float coordinates[] = {0, 0, 1, 0, 0, 1};
  uv->setData(static_cast<const void *>(coordinates));
  f.geometry->setParameterObject("vertex.attribute0", *uv)
      ->setDescription("original gathered UV binding")
      .setEnabled(false);
  f.geometry->setParameterObject("vertex.attribute1", *uv);
  f.other->setParameterObject("vertex.attribute0", *uv);
  const auto oldUV = f.sceneIds->handleOf(uv.data());
  const auto oldPositions = f.sceneIds->handleOf(f.positions.data());
  std::string mode = "tracer";
  SECTION("indexed open triangles and optional aliases") {}
  SECTION("nonindexed count change with explicit attribute removals")
  {
    mode = "nonindexed";
  }
  SECTION("nonmanifold overlaps and degeneracy remain structural data")
  {
    mode = "nonmanifold";
  }
  SECTION("explicit enabled state and independent equal payloads")
  {
    mode = "enabled";
  }
  SECTION(
      "replacement changes numeric widths and retains fixed-point semantics")
  {
    mode = "typed64";
  }
  const auto session = f.attach();
  const auto evidence = f.working(session, mode, true);
  auto *observer = f.scene.updateDelegate().emplace<ObserveMeshPublication>();
  bool observed = false, coherent = true;
  observer->observe = [&] {
    observed = true;
    const auto *positions =
        f.geometry->parameterValueAsObject<vsr::scene::Array>(
            "vertex.position");
    const auto *indices = f.geometry->parameterValueAsObject<vsr::scene::Array>(
        "primitive.index");
    coherent = coherent && positions && positions != f.positions.data()
        && positions->size() == (mode == "nonindexed" ? 6 : 4)
        && !f.geometry->parameter("vertex.attribute0")
        && !f.geometry->parameter("vertex.attribute1");
    coherent = coherent
        && (mode == "nonindexed" ? !indices
                                 : indices
                    && indices->size() == (mode == "nonmanifold" ? 4 : 2));
  };
  f.importPositions(session, 2, *evidence.result());
  const auto imported = f.await(session, 2);
  INFO(json::write(imported.json));
  REQUIRE(imported.flag("committed"));
  REQUIRE(imported.str("outcome") == "ok");
  CHECK(observed);
  CHECK(coherent);
  observer->observe = {};
  CHECK(f.sceneIds->handleOf(f.other->parameterValueAsObject<vsr::scene::Array>(
            "vertex.position"))
      == oldPositions);
  CHECK(f.sceneIds->handleOf(f.other->parameterValueAsObject<vsr::scene::Array>(
            "vertex.attribute0"))
      == oldUV);
  CHECK(std::memcmp(uv->data(), coordinates, sizeof(coordinates)) == 0);
  if (mode == "nonindexed") {
    const float expected[] = {
        9, -1, 0, 9, -1, 0, 11, -1, 0, 11, -1, 0, 10, 1, 0, 10, 1, 0};
    CHECK(std::memcmp(
              f.geometry
                  ->parameterValueAsObject<vsr::scene::Array>("vertex.position")
                  ->data(),
              expected,
              sizeof(expected))
        == 0);
  }
  if (mode == "typed64") {
    const auto *positions =
        f.geometry->parameterValueAsObject<vsr::scene::Array>(
            "vertex.position");
    const auto *indices = f.geometry->parameterValueAsObject<vsr::scene::Array>(
        "primitive.index");
    const auto *colors =
        f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.color");
    REQUIRE(colors);
    CHECK(positions->elementType() == ANARI_FLOAT64_VEC3);
    CHECK(static_cast<const double *>(positions->data())[0] == -0.4);
    CHECK(indices->elementType() == ANARI_UINT64_VEC3);
    const std::uint64_t expected[] = {0, 1, 2, 0, 2, 3};
    CHECK(std::memcmp(indices->data(), expected, sizeof(expected)) == 0);
    CHECK(colors->elementType() == ANARI_UFIXED8_VEC4);
    const unsigned char expectedColor[] = {0, 1, 254, 255};
    CHECK(
        std::memcmp(colors->data(), expectedColor, sizeof(expectedColor)) == 0);
    CHECK_FALSE(f.geometry->parameter("vertex.color")->isEnabled());
  }
  if (mode == "nonmanifold") {
    const std::uint32_t expected[] = {0, 1, 2, 0, 1, 3, 0, 1, 2, 0, 0, 0};
    const auto *indices = f.geometry->parameterValueAsObject<vsr::scene::Array>(
        "primitive.index");
    CHECK(indices->elementType() == ANARI_UINT32_VEC3);
    CHECK(std::memcmp(indices->data(), expected, sizeof(expected)) == 0);
  }
  if (mode != "nonindexed") {
    const auto *corner = f.geometry->parameterValueAsObject<vsr::scene::Array>(
        "faceVarying.attribute2");
    REQUIRE(corner);
    CHECK(corner->elementType() == ANARI_INT16_VEC2);
    CHECK(corner->size() == (mode == "nonmanifold" ? 12 : 6));
    CHECK(static_cast<const std::int16_t *>(corner->data())[1] == 1);
    CHECK_FALSE(f.geometry->parameter("faceVarying.attribute2")->isEnabled());
    const auto *tangent =
        f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.tangent");
    REQUIRE(tangent);
    CHECK(tangent->elementType() == ANARI_FLOAT64_VEC4);
    CHECK(static_cast<const double *>(tangent->data())[3] == -1);
    const auto *color = f.geometry->parameterValueAsObject<vsr::scene::Array>(
        "primitive.color");
    const auto *alias = f.geometry->parameterValueAsObject<vsr::scene::Array>(
        "primitive.attribute1");
    CHECK((color == alias) == (mode != "enabled"));
    CHECK(f.geometry->parameter("primitive.attribute1")->isEnabled()
        == (mode == "enabled"));
  }
  const auto batch = *imported.result()->get("batch_ref");
  f.scene.removeUnusedObjects();
  f.scene.defragmentObjectStorage();
  f.restoreBatch(session, 3, batch);
  const auto restored = f.await(session, 3);
  INFO(json::write(restored.json));
  REQUIRE(restored.flag("committed"));
  CHECK(f.sceneIds->handleOf(
            f.geometry->parameterValueAsObject<vsr::scene::Array>(
                "vertex.position"))
      == oldPositions);
  CHECK(f.sceneIds->handleOf(
            f.geometry->parameterValueAsObject<vsr::scene::Array>(
                "vertex.attribute0"))
      == oldUV);
  CHECK(
      f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.attribute0")
      == f.geometry->parameterValueAsObject<vsr::scene::Array>(
          "vertex.attribute1"));
  CHECK_FALSE(f.geometry->parameter("vertex.attribute0")->isEnabled());
  CHECK(f.geometry->parameter("vertex.attribute0")->description()
      == "original gathered UV binding");
  CHECK_FALSE(f.geometry->parameter("primitive.index"));
  CHECK_FALSE(f.geometry->parameter("primitive.color"));
  CHECK_FALSE(f.geometry->parameter("primitive.attribute1"));
  CHECK_FALSE(f.geometry->parameter("faceVarying.attribute2"));
  CHECK_FALSE(f.geometry->parameter("vertex.tangent"));
  CHECK_FALSE(f.geometry->parameter("vertex.color"));
}

SCENARIO("MCP mesh rejects incompatible whole final state before publication",
    "[integration][mcp][mesh-import][mesh-compound]")
{
  PositionFixture f;
  auto uv = f.scene.createArray(ANARI_FLOAT32_VEC2, 3);
  const float coordinates[] = {0, 0, 1, 0, 0, 1};
  uv->setData(static_cast<const void *>(coordinates));
  f.geometry->setParameterObject("vertex.attribute0", *uv);
  std::string mode;
  SECTION("omitted incompatible vertex attributes remain retained")
  {
    mode = "retain";
  }
  SECTION("index equal to vertex count is out of range")
  {
    mode = "range";
  }
  SECTION("disabled corner data still requires corner cardinality")
  {
    mode = "corner-count";
  }
  SECTION("disabled indices require implicit triangle cardinality")
  {
    mode = "index-off";
  }
  const auto session = f.attach();
  const auto evidence = f.working(session, mode, true);
  const auto revision = f.sceneIds->sceneRevision();
  const auto arrayCount = f.scene.numberOfObjects(ANARI_ARRAY);
  f.importPositions(session, 2, *evidence.result());
  const auto result = f.await(session, 2);
  REQUIRE(result.str("outcome") == "invalid_request");
  CHECK_FALSE(result.flag("committed"));
  CHECK(f.sceneIds->sceneRevision() == revision);
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == arrayCount);
  CHECK(
      f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.attribute0")
      == uv.data());
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK(f.edits->restorations().size() == 0);
}

SCENARIO("MCP compound restore conflicts on every required old and new buffer",
    "[integration][mcp][mesh-import][mesh-compound]")
{
  PositionFixture f;
  auto uv = f.scene.createArray(ANARI_FLOAT32_VEC2, 3);
  const float coordinates[] = {0, 0, 1, 0, 0, 1};
  uv->setData(static_cast<const void *>(coordinates));
  f.geometry->setParameterObject("vertex.attribute0", *uv);
  f.other->setParameterObject("vertex.attribute0", *uv);
  const auto session = f.attach();
  const auto evidence = f.working(session, "tracer", true);
  f.importPositions(session, 2, *evidence.result());
  const auto imported = f.await(session, 2);
  REQUIRE(imported.flag("committed"));
  auto *replacement =
      f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position");
  const auto replacementIdentity = f.sceneIds->handleOf(replacement);
  vsr::scene::Array *changed = nullptr;
  bool remove = false;
  SECTION("old positions through another Geometry")
  {
    changed = f.positions.data();
  }
  SECTION("removed old UV through another Geometry")
  {
    changed = uv.data();
  }
  SECTION("old removed dependency retired")
  {
    changed = uv.data();
    remove = true;
  }
  SECTION("new position data")
  {
    changed = replacement;
  }
  SECTION("new index data")
  {
    changed = f.geometry->parameterValueAsObject<vsr::scene::Array>(
        "primitive.index");
  }
  SECTION("new alias data")
  {
    changed = f.geometry->parameterValueAsObject<vsr::scene::Array>(
        "primitive.attribute1");
  }
  SECTION("new disabled corner data")
  {
    changed = f.geometry->parameterValueAsObject<vsr::scene::Array>(
        "faceVarying.attribute2");
  }
  SECTION("new disabled tangent data")
  {
    changed =
        f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.tangent");
  }
  REQUIRE(changed);
  if (remove)
    f.scene.removeObject(changed);
  else {
    // Even a byte-exact write/change-back cannot revive restoration
    // eligibility.
    REQUIRE(changed->map());
    changed->unmap();
  }
  f.restoreBatch(session, 3, *imported.result()->get("batch_ref"));
  const auto refused = f.await(session, 3);
  CHECK(refused.str("outcome") == "conflict");
  CHECK_FALSE(refused.flag("committed"));
  CHECK(f.sceneIds->handleOf(
            f.geometry->parameterValueAsObject<vsr::scene::Array>(
                "vertex.position"))
      == replacementIdentity);
  CHECK_FALSE(f.geometry->parameter("vertex.attribute0"));
  f.restoreBatch(session, 4, *imported.result()->get("batch_ref"));
  CHECK(f.await(session, 4).str("outcome") == "conflict");
}

SCENARIO("MCP mesh no-change and deliberate alias actions preserve intent",
    "[integration][mcp][mesh-import][mesh-compound]")
{
  PositionFixture f;
  f.geometry->setParameterObject("vertex.normal", *f.positions)
      ->setEnabled(false);
  const auto old = f.sceneIds->handleOf(f.positions.data());
  std::string mode;
  SECTION("lossless preserved alias no-op")
  {
    mode = "noop";
  }
  SECTION("equivalent explicit full alias replacement is a no-op")
  {
    mode = "equivalent";
  }
  SECTION("helper write replaces the full alias group")
  {
    mode = "alias-write";
  }
  SECTION("byte-equivalent deliberate split still splits")
  {
    mode = "split-equal";
  }
  SECTION("selective data replacement splits only declared field")
  {
    mode = "split";
  }
  const auto session = f.attach();
  const auto evidence = f.working(session, mode, true);
  const auto arrayCount = f.scene.numberOfObjects(ANARI_ARRAY);
  const auto revision = f.sceneIds->sceneRevision();
  f.importPositions(session, 2, *evidence.result());
  const auto imported = f.await(session, 2);
  REQUIRE(imported.str("outcome") == "ok");
  const bool unchanged = mode == "noop" || mode == "equivalent";
  CHECK(imported.flag("committed") == !unchanged);
  const auto *positions =
      f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position");
  const auto *normals =
      f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.normal");
  CHECK((positions == normals) == (unchanged || mode == "alias-write"));
  CHECK_FALSE(f.geometry->parameter("vertex.normal")->isEnabled());
  if (unchanged) {
    CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == arrayCount);
    CHECK(f.sceneIds->sceneRevision() == revision);
    CHECK(f.sceneIds->handleOf(positions) == old);
    CHECK_FALSE(imported.result()->get("batch_ref"));
  } else {
    CHECK(f.sceneIds->handleOf(positions) != old);
    if (mode != "alias-write")
      CHECK(f.sceneIds->handleOf(normals) == old);
    f.restoreBatch(session, 3, *imported.result()->get("batch_ref"));
    REQUIRE(f.await(session, 3).flag("committed"));
    CHECK(
        f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
        == f.geometry->parameterValueAsObject<vsr::scene::Array>(
            "vertex.normal"));
    CHECK(f.sceneIds->handleOf(
              f.geometry->parameterValueAsObject<vsr::scene::Array>(
                  "vertex.position"))
        == old);
  }
}

SCENARIO("MCP compound mesh rejects malformed action and numeric envelopes",
    "[integration][mcp][mesh-import][mesh-compound]")
{
  const auto mode = GENERATE("malformed-preserve",
      "malformed-field",
      "malformed-type",
      "malformed-revision",
      "malformed-overflow",
      "malformed-length",
      "malformed-nonfinite",
      "malformed-duplicate");
  PositionFixture f;
  const auto session = f.attach();
  const auto evidence = f.working(session, mode, true);
  const auto revision = f.sceneIds->sceneRevision();
  f.importPositions(session, 2, *evidence.result());
  const auto rejected = f.await(session, 2);
  INFO(mode);
  INFO(json::write(rejected.json));
  CHECK(rejected.str("outcome") == "invalid_request");
  CHECK_FALSE(rejected.flag("committed"));
  CHECK(f.sceneIds->sceneRevision() == revision);
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == 1);
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK(f.edits->restorations().size() == 0);
}

SCENARIO("MCP mesh bounds copied Parameter metadata before preparation",
    "[integration][mcp][mesh-import][mesh-compound]")
{
  PositionFixture f;
  std::size_t length = 65536;
  SECTION("existing metadata alone exceeds the copy bound") {}
  SECTION("added controls must leave the final target immediately restorable")
  {
    length = 64000;
  }
  f.geometry->setParameter("notes", std::string(length, 'x'));
  const auto session = f.attach();
  const auto evidence = f.working(session, "tracer", true);
  const auto revision = f.sceneIds->sceneRevision();
  f.importPositions(session, 2, *evidence.result());
  const auto rejected = f.await(session, 2);
  CHECK(rejected.str("outcome") == "invalid_request");
  CHECK(rejected.result()->get("reason")->s == "mesh_parameter_limit");
  CHECK_FALSE(rejected.flag("committed"));
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == 1);
  CHECK(f.sceneIds->sceneRevision() == revision);
  CHECK(
      f.geometry->parameter("notes")->value().getStringView().size() == length);
}

SCENARIO("MCP mesh respects active ordinary Parameter batches",
    "[integration][mcp][mesh-import][mesh-compound]")
{
  PositionFixture f;
  const auto session = f.attach();
  const auto evidence = f.working(session, "tracer", true);
  const auto count = f.scene.numberOfObjects(ANARI_ARRAY);
  f.geometry->beginParameterBatch();
  f.importPositions(session, 2, *evidence.result());
  const auto rejected = f.await(session, 2);
  CHECK_FALSE(rejected.flag("committed"));
  CHECK(rejected.result()->get("reason")->s == "parameter_batch_active");
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == count);
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  f.geometry->endParameterBatch();
  CHECK(f.edits->restorations().size() == 0);
}

SCENARIO(
    "MCP compound preparation captures removed metadata without overwriting unrelated edits",
    "[integration][mcp][mesh-import][mesh-compound]")
{
  PositionFixture *host = nullptr;
  bool preparing = false, update = false, updated = false;
  const auto physical =
      std::make_shared<vsr::rendering::FrameBudget>(16u << 20);
  image::ExchangeLimits limits;
  limits.reservePhysicalMemory =
      [&](std::size_t bytes) -> std::shared_ptr<const void> {
    if (host && update) {
      if (bytes == 4096)
        preparing = true;
      if (preparing && bytes == 65536
          && host->geometry->parameter("vertex.attribute0")) {
        update = false;
        updated = true;
        host->geometry->parameter("vertex.attribute0")
            ->setDescription("latest old metadata");
        host->geometry->parameter("custom")->setDescription(
            "unrelated metadata");
      }
    }
    return physical->reserve(bytes);
  };
  PositionFixture f(limits);
  host = &f;
  auto uv = f.scene.createArray(ANARI_FLOAT32_VEC2, 3);
  const float coordinates[] = {0, 0, 1, 0, 0, 1};
  uv->setData(static_cast<const void *>(coordinates));
  f.geometry->setParameterObject("vertex.attribute0", *uv)
      ->setDescription("initial metadata");
  f.geometry->setParameter("custom", 17.f);
  const auto session = f.attach();
  const auto evidence = f.working(session, "tracer", true);
  update = true;
  f.importPositions(session, 2, *evidence.result());
  const auto imported = f.await(session, 2);
  REQUIRE(imported.flag("committed"));
  CHECK(updated);
  CHECK(f.geometry->parameter("custom")->description() == "unrelated metadata");
  f.geometry->parameter("vertex.position")
      ->setDescription("post-import unrelated metadata");
  f.restoreBatch(session, 3, *imported.result()->get("batch_ref"));
  REQUIRE(f.await(session, 3).flag("committed"));
  CHECK(f.geometry->parameter("vertex.attribute0")->description()
      == "latest old metadata");
  CHECK(f.geometry->parameter("vertex.position")->description()
      == "post-import unrelated metadata");
  CHECK(f.geometry->parameter("custom")->description() == "unrelated metadata");
  CHECK(f.geometry->parameter("custom")->value().get<float>() == 17.f);
}

SCENARIO(
    "MCP mesh can change an optional enabled state without allocating Arrays",
    "[integration][mcp][mesh-import][mesh-compound]")
{
  PositionFixture f;
  f.geometry->setParameterObject("vertex.normal", *f.positions)
      ->setEnabled(false);
  const auto session = f.attach();
  const auto evidence = f.working(session, "normal-on", true);
  const auto count = f.scene.numberOfObjects(ANARI_ARRAY);
  f.importPositions(session, 2, *evidence.result());
  const auto imported = f.await(session, 2);
  REQUIRE(imported.flag("committed"));
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == count);
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.normal")
      == f.positions.data());
  CHECK(f.geometry->parameter("vertex.normal")->isEnabled());
  f.restoreBatch(session, 3, *imported.result()->get("batch_ref"));
  REQUIRE(f.await(session, 3).flag("committed"));
  CHECK_FALSE(f.geometry->parameter("vertex.normal")->isEnabled());
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.normal")
      == f.positions.data());
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == count);
}

SCENARIO("MCP mesh receipts retain charged owners safely past Scene teardown",
    "[integration][mcp][mesh-import][mesh-import-owners]")
{
  auto memory = std::make_shared<vsr::rendering::FrameBudget>(16u << 20);
  const auto baseline = memory->used();
  image::ExchangeLimits limits;
  limits.reservePhysicalMemory = [memory](std::size_t bytes) {
    return memory->reserve(bytes);
  };
  auto f = std::make_unique<PositionFixture>(limits);
  const auto session = f->attach();
  const auto evidence = f->working(session);
  f->importPositions(session, 2, *evidence.result());
  REQUIRE(f->await(session, 2).flag("committed"));
  const operations::Credentials credentials{
      f->authority->launchCredential(), session.id, session.secret};
  std::optional<operations::Status> held =
      f->engine->status(credentials, {session.id, 2});
  REQUIRE(held->committed);
  const auto live = memory->used();
  auto entries = f->imageActivity->snapshot();
  REQUIRE(entries.size() == 2);
  CHECK(entries[1].entry.kind == activity::OperationKind::MESH_IMPORT);
  std::optional<activity::Entry> copied = entries[1].entry;
  CHECK(memory->used() > live);
  auto oldLifetime = f->positions->lifetime();
  auto replacementLifetime =
      f->geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
          ->lifetime();
  const auto bytes = held->result;
  f.reset();
  CHECK(oldLifetime.expired());
  CHECK(replacementLifetime.expired());
  CHECK(memory->used() > 0);
  CHECK(held->result == bytes);
  std::thread release([&] {
    held.reset();
    entries.clear();
    copied.reset();
  });
  release.join();
  CHECK(memory->used() == baseline);
}

SCENARIO(
    "MCP position import checks identity exposure and strict envelopes before file I/O",
    "[integration][mcp][mesh-import][mesh-import-authority]")
{
  PositionFixture f;
  const auto session = f.attach();
  const auto evidence = f.working(session);
  identity::SceneIdentity foreignIds(f.ids, f.hidden);
  f.images->attachScene(f.hiddenId, foreignIds);
  auto foreign = f.hidden.createObject<vsr::scene::Geometry>("triangle");
  auto handle = f.objectHandle(f.geometry.data());
  std::string extra, expected = "invalid_request";
  bool schema = false, withdraw = false;
  SECTION("wrong kind rejects before missing-file disclosure")
  {
    handle = f.objectHandle(f.scene.getObject<vsr::scene::Material>(0).data());
  }
  SECTION("unknown handle rejects before missing-file disclosure")
  {
    handle = "ffffffffffffffffffffffffffffffff";
  }
  SECTION("foreign Scene Geometry rejects before missing-file disclosure")
  {
    const auto h = foreignIds.handleOf(foreign.data());
    handle = edit::detail::hex64(h.domain) + edit::detail::hex64(h.serial);
  }
  SECTION("unknown envelope fields do not gain reference-edit authority")
  {
    extra = ",\"array_ref\":\"arbitrary-live-array\"";
    schema = true;
  }
  SECTION("READ withdrawal at execution wins over file errors")
  {
    withdraw = true;
    expected = "forbidden";
  }
  const auto args =
      "{\"request_id\":2,\"scene_id\":" + std::to_string(f.sceneId)
      + ",\"handle\":" + json::quote(handle) + ",\"target_version\":"
      + json::write(*evidence.result()->get("target_version"))
      + ",\"dependencies\":"
      + json::write(*evidence.result()->get("dependencies"))
      + ",\"file_name\":\"missing.vmesh\"" + extra + "}";
  const auto admission = f.tool(session, "vsr_mesh_import", args);
  if (schema)
    CHECK(admission.errorCode() == "invalid_request");
  else {
    REQUIRE(admission.str("kind") == "mesh_import");
    if (withdraw)
      f.authority->restrict(
          session::Target::scene(f.sceneId), session::Capability::READ);
    const auto rejected = f.await(session, 2);
    CHECK(rejected.str("outcome") == expected);
    CHECK_FALSE(rejected.flag("committed"));
    if (!withdraw)
      CHECK(rejected.result()->get("reason")->s != "file_rejected");
  }
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == 1);
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
}

SCENARIO("MCP eligible space and pinned physical recovery are distinct",
    "[integration][mcp][mesh-import][mesh-pressure][mesh-pressure-owners]")
{
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 600000;
  PositionFixture f(limits);
  auto uv = f.scene.createArray(ANARI_FLOAT32_VEC2, 3);
  const float coordinates[] = {0, 0, 1, 0, 0, 1};
  uv->setData(static_cast<const void *>(coordinates));
  f.geometry->setParameterObject("vertex.attribute0", *uv)
      ->setDescription(std::string(24000, 'u').c_str());
  const auto a = f.attach();
  const auto evidence = f.working(a, "tracer", true);
  f.importPositions(a, 2, *evidence.result());
  const auto imported = f.await(a, 2);
  REQUIRE(imported.flag("committed"));
  const auto batch = *imported.result()->get("batch_ref");
  // Normal restoration retention can outlive eligibility, just like a worker
  // or copied result. Observe the consequences through public operations.
  auto retained = f.edits->restorations().find(
      {batch.get("launch_id")->s, batch.get("batch_id")->s});
  REQUIRE(retained);
  const auto oldUV = f.sceneIds->handleOf(uv.data());
  f.other->removeParameter("vertex.position");
  f.restoreBatch(a, 3, batch);
  REQUIRE(f.await(a, 3).flag("committed"));
  REQUIRE(f.rpc("vsr/detach", "{}", &a).status == 200);
  const auto b = f.attach();
  f.tool(b, "vsr_discover", "{\"request_id\":1}");
  const auto discovered = f.await(b, 1);
  const auto *capacity = discovered.result()
                             ->get("items")
                             ->a.at(0)
                             .get("image_exchange")
                             ->get("exchange_limits");
  CHECK(capacity->get("effective_global_retention_byte_cap")->n == 600000);
  CHECK(capacity->get("available_global_retention_bytes")->n == 600000);
  CHECK(capacity->get("retained_physical_bytes")->n > 262144);
  f.scene.removeUnusedObjects();
  f.scene.defragmentObjectStorage();
  REQUIRE(f.sceneIds->resolve(oldUV));
  CHECK(f.geometry->parameter("vertex.attribute0")->description()
      == std::string(24000, 'u'));
  f.exportMesh(b, 2, f.geometry.data());
  const auto fresh = f.await(b, 2);
  REQUIRE(fresh.str("outcome") == "ok");
  f.importPositions(b, 3, *fresh.result());
  const auto admitted = f.await(b, 3);
  REQUIRE(admitted.flag("committed"));
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      != f.sceneIds->resolve(f.sceneIds->handleOf(f.positions.data())));
  std::thread release([&] { retained.reset(); });
  release.join();
  f.images->pump();
  f.restoreBatch(b, 5, *admitted.result()->get("batch_ref"));
  REQUIRE(f.await(b, 5).flag("committed"));
  CHECK(f.geometry->parameter("vertex.attribute0")->description()
      == std::string(24000, 'u'));
}

SCENARIO(
    "MCP restore bounds the union of current and recovered Parameter copies",
    "[integration][mcp][mesh-import][mesh-pressure][mesh-pressure-metadata]")
{
  const auto metadata = GENERATE(0, 1, 2);
  PositionFixture f;
  auto uv = f.scene.createArray(ANARI_FLOAT32_VEC2, 3);
  const float coordinates[] = {0, 0, 1, 0, 0, 1};
  uv->setData(static_cast<const void *>(coordinates));
  auto *prior = f.geometry->setParameterObject("vertex.attribute0", *uv);
  if (metadata == 0)
    prior->setDescription(std::string(40000, 'u').c_str());
  else if (metadata == 1)
    prior->setStringValues({std::string(20000, 'u'), std::string(20000, 'v')});
  else
    prior->setMin(vsr::core::Any(std::string(20000, 'u')))
        .setMax(vsr::core::Any(std::string(20000, 'v')));
  f.geometry->setParameter("custom", 17.f);
  const auto s = f.attach();
  const auto evidence = f.working(s, "tracer", true);
  f.importPositions(s, 2, *evidence.result());
  const auto imported = f.await(s, 2);
  REQUIRE(imported.flag("committed"));
  const auto batch = *imported.result()->get("batch_ref");
  // Unversioned, unrelated UI metadata must not be lost. Each source map fits
  // individually, but copying their union must also fit the enforced bound.
  f.geometry->parameter("custom")->setDescription(
      std::string(40000, 'c').c_str());
  const auto version = f.geometry->lastParameterChange();
  const auto positions = f.sceneIds->handleOf(
      f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position"));
  f.restoreBatch(s, 3, batch);
  const auto busy = f.await(s, 3);
  REQUIRE(busy.str("outcome") == "busy");
  CHECK_FALSE(busy.flag("committed"));
  CHECK(f.geometry->lastParameterChange() == version);
  CHECK_FALSE(f.geometry->parameter("vertex.attribute0"));
  CHECK(f.sceneIds->handleOf(
            f.geometry->parameterValueAsObject<vsr::scene::Array>(
                "vertex.position"))
      == positions);
  CHECK(f.geometry->parameter("custom")->description()
      == std::string(40000, 'c'));
  f.geometry->parameter("custom")->setDescription("later UI metadata");
  f.restoreBatch(s, 4, batch);
  REQUIRE(f.await(s, 4).flag("committed"));
  const auto *recovered = f.geometry->parameter("vertex.attribute0");
  REQUIRE(recovered);
  if (metadata == 0)
    CHECK(recovered->description() == std::string(40000, 'u'));
  else if (metadata == 1) {
    REQUIRE(recovered->stringValues().size() == 2);
    CHECK(recovered->stringValues()[0] == std::string(20000, 'u'));
    CHECK(recovered->stringValues()[1] == std::string(20000, 'v'));
  } else {
    CHECK(recovered->min().getStringView() == std::string(20000, 'u'));
    CHECK(recovered->max().getStringView() == std::string(20000, 'v'));
  }
  CHECK(f.geometry->parameter("custom")->description() == "later UI metadata");
}

SCENARIO("MCP repeated compound restore collects only unadopted Arrays",
    "[integration][mcp][mesh-import][mesh-pressure][mesh-pressure-lifetimes]")
{
  PositionFixture f;
  auto uv = f.scene.createArray(ANARI_FLOAT32_VEC2, 3);
  const float coordinates[] = {0, 0, 1, 0, 0, 1};
  uv->setData(static_cast<const void *>(coordinates));
  f.geometry->setParameterObject("vertex.attribute0", *uv);
  f.geometry->setParameterObject("vertex.attribute1", *uv)->setEnabled(false);
  const auto originalPositions = f.sceneIds->handleOf(f.positions.data());
  const auto originalUV = f.sceneIds->handleOf(uv.data());
  const auto s = f.attach();
  std::uint64_t id = 0;
  json::Value firstBatch;
  std::vector<identity::ObjectHandle> adopted;
  for (unsigned cycle = 0; cycle < 6; ++cycle) {
    const auto exportId = ++id;
    f.exportMesh(s, exportId, f.geometry.data());
    const auto evidence = f.await(s, exportId);
    REQUIRE(evidence.str("outcome") == "ok");
    const auto path = f.exchangePath();
    REQUIRE(std::system((std::string(VSR_MESH_TEST_PYTHON) + " "
                + VSR_MESH_TEST_SOURCE + "/scripts/test-mesh-topology.py '"
                + path + "/" + evidence.result()->get("file_name")->s + "' '"
                + path + "/edited.vmesh' tracer")
                    .c_str())
        == 0);
    const auto importId = ++id;
    f.importPositions(s, importId, *evidence.result());
    const auto imported = f.await(s, importId);
    REQUIRE(imported.flag("committed"));
    const auto batch = *imported.result()->get("batch_ref");
    if (!cycle)
      firstBatch = batch;
    std::vector<identity::ObjectHandle> replacements;
    for (std::size_t i = 0; i < f.geometry->numParameters(); ++i) {
      const auto &parameter = f.geometry->parameterAt(i);
      if (!anari::isArray(parameter.value().type()))
        continue;
      auto *array = f.geometry->parameterValueAsObject<vsr::scene::Array>(
          parameter.name());
      replacements.push_back(f.sceneIds->handleOf(array));
      if (!cycle)
        f.other->setParameterObject(parameter.name(), *array)
            ->setEnabled(parameter.isEnabled());
    }
    f.scene.removeUnusedObjects();
    for (unsigned i = 0; i < 150; ++i)
      f.scene.createArray(ANARI_FLOAT32_VEC3, 3);
    f.scene.removeUnusedObjects();
    f.scene.defragmentObjectStorage();
    REQUIRE(f.sceneIds->resolve(originalPositions));
    REQUIRE(f.sceneIds->resolve(originalUV));
    const auto restoreId = ++id;
    f.restoreBatch(s, restoreId, batch);
    const auto restored = f.await(s, restoreId);
    REQUIRE(restored.flag("committed"));
    f.images->pump();
    f.scene.removeUnusedObjects();
    f.scene.defragmentObjectStorage();
    CHECK(f.sceneIds->handleOf(
              f.geometry->parameterValueAsObject<vsr::scene::Array>(
                  "vertex.position"))
        == originalPositions);
    CHECK(f.sceneIds->handleOf(
              f.geometry->parameterValueAsObject<vsr::scene::Array>(
                  "vertex.attribute0"))
        == originalUV);
    CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>(
              "vertex.attribute0")
        == f.geometry->parameterValueAsObject<vsr::scene::Array>(
            "vertex.attribute1"));
    CHECK_FALSE(f.geometry->parameter("vertex.attribute1")->isEnabled());
    CHECK(std::memcmp(f.geometry
                          ->parameterValueAsObject<vsr::scene::Array>(
                              "vertex.attribute0")
                          ->data(),
              coordinates,
              sizeof(coordinates))
        == 0);
    for (const auto &handle : replacements)
      CHECK(bool(f.sceneIds->resolve(handle)) == (cycle == 0));
    if (!cycle) {
      adopted = replacements;
      f.exportMesh(s, ++id, f.other.data());
      REQUIRE(f.await(s, id).str("outcome") == "ok");
    }
    for (const auto &handle : adopted)
      REQUIRE(f.sceneIds->resolve(handle));
    CHECK(f.scene.numberOfObjects(ANARI_ARRAY) <= 8);
    f.restoreBatch(s, ++id, firstBatch);
    CHECK(f.await(s, id).str("outcome") == "already_restored");
    REQUIRE(
        f.importPositions(s, importId, *evidence.result()).flag("replayed"));
    CHECK(f.sceneIds->handleOf(
              f.geometry->parameterValueAsObject<vsr::scene::Array>(
                  "vertex.position"))
        == originalPositions);
    REQUIRE(
        ::unlink((path + "/" + evidence.result()->get("file_name")->s).c_str())
        == 0);
    CHECK(std::filesystem::exists(path + "/edited.vmesh"));
  }
}

SCENARIO(
    "MCP preserved dependencies and target change back permanently conflict",
    "[integration][mcp][mesh-import][mesh-pressure][mesh-pressure-conflicts]")
{
  const auto kind = GENERATE(0, 1, 2, 3);
  PositionFixture f;
  auto uv = f.scene.createArray(ANARI_FLOAT32_VEC2, 3);
  const float coordinates[] = {0, 0, 1, 0, 0, 1};
  uv->setData(static_cast<const void *>(coordinates));
  f.geometry->setParameterObject("vertex.attribute0", *uv);
  f.other->setParameterObject("vertex.attribute0", *uv);
  const auto s = f.attach();
  const auto evidence = f.working(s);
  f.importPositions(s, 2, *evidence.result());
  const auto imported = f.await(s, 2);
  REQUIRE(imported.flag("committed"));
  const auto batch = *imported.result()->get("batch_ref");
  const auto replacementHandle = f.sceneIds->handleOf(
      f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position"));
  if (kind == 0) {
    auto *parameter = f.geometry->parameter("vertex.position");
    parameter->setEnabled(false);
    parameter->setEnabled(true);
  } else if (kind == 1) {
    auto *shared =
        f.other->parameterValueAsObject<vsr::scene::Array>("vertex.attribute0");
    const float changed[] = {7, 8, 9, 10, 11, 12};
    shared->setData(static_cast<const void *>(changed));
    shared->setData(static_cast<const void *>(coordinates));
  } else if (kind == 2) {
    auto *array = f.geometry->parameterValueAsObject<vsr::scene::Array>(
        "vertex.position");
    auto bytes = std::vector<unsigned char>(
        static_cast<const unsigned char *>(array->data()),
        static_cast<const unsigned char *>(array->data()) + 36);
    array->setData(static_cast<const void *>(PositionFixture::POINTS));
    array->setData(static_cast<const void *>(bytes.data()));
  } else {
    const auto old = f.sceneIds->handleOf(uv.data());
    f.scene.removeObject(uv.data());
    auto replacement = f.scene.createArray(ANARI_FLOAT32_VEC2, 3);
    replacement->setData(static_cast<const void *>(coordinates));
    f.geometry->setParameterObject("vertex.attribute0", *replacement);
    f.other->setParameterObject("vertex.attribute0", *replacement);
    CHECK(f.sceneIds->handleOf(replacement.data()) != old);
  }
  for (std::uint64_t id = 3; id < 6; ++id) {
    const auto version = f.geometry->lastParameterChange();
    f.restoreBatch(s, id, batch);
    const auto conflict = f.await(s, id);
    CHECK(conflict.str("outcome") == "conflict");
    CHECK_FALSE(conflict.flag("committed"));
    CHECK(f.geometry->lastParameterChange() == version);
    CHECK(f.sceneIds->handleOf(
              f.geometry->parameterValueAsObject<vsr::scene::Array>(
                  "vertex.position"))
        == replacementHandle);
    auto *currentUV = f.geometry->parameterValueAsObject<vsr::scene::Array>(
        "vertex.attribute0");
    CHECK(
        std::memcmp(currentUV->data(), coordinates, sizeof(coordinates)) == 0);
  }
  f.exportMesh(s, 6, f.geometry.data());
  CHECK(f.await(s, 6).str("outcome") == "ok");
}

SCENARIO(
    "MCP copied receipts and recovery retain capacity across offthread teardown",
    "[integration][mcp][mesh-import][mesh-pressure][mesh-pressure-teardown]")
{
  const auto memory = std::make_shared<vsr::rendering::FrameBudget>(8u << 20);
  image::ExchangeLimits limits;
  limits.reservePhysicalMemory = [memory](std::size_t bytes) {
    return memory->reserve(bytes);
  };
  auto f = std::make_unique<PositionFixture>(limits);
  const auto s = f->attach();
  const auto evidence = f->working(s, "tracer", true);
  f->importPositions(s, 2, *evidence.result());
  const auto imported = f->await(s, 2);
  REQUIRE(imported.flag("committed"));
  const auto batch = *imported.result()->get("batch_ref");
  const operations::Credentials credentials{
      f->authority->launchCredential(), s.id, s.secret};
  std::optional<operations::Status> heldStatus =
      f->engine->status(credentials, {s.id, 2});
  REQUIRE(heldStatus->committed);
  auto entries = f->imageActivity->snapshot();
  auto retained = f->edits->restorations().find(
      {batch.get("launch_id")->s, batch.get("batch_id")->s});
  REQUIRE(retained);
  const auto receipt = heldStatus->result;
  f->exportMesh(s, 3, f->geometry.data());
  REQUIRE(
      f->integration.service() > 0); // Starts a real worker before shutdown.
  std::thread teardown([&] { f.reset(); });
  teardown.join();
  CHECK(heldStatus->result == receipt);
  // A second isolated host shares the physical pool, not Scene/authority.
  // Keep finite external pressure, then observe reservation refusal/recovery
  // through READ export rather than interpreting a private byte counter.
  auto pressure = memory->reserve(26u << 18);
  REQUIRE(pressure);
  PositionFixture next(limits);
  const auto b = next.attach();
  next.exportMesh(b, 1, next.geometry.data());
  const auto busy = next.await(b, 1);
  CHECK(busy.str("outcome") == "busy");
  CHECK_FALSE(busy.flag("committed"));
  CHECK(std::filesystem::is_empty(next.exchangePath()));
  std::thread release([&] {
    heldStatus.reset();
    entries.clear();
    retained.reset();
  });
  release.join();
  next.exportMesh(b, 2, next.geometry.data());
  const auto exported = next.await(b, 2);
  REQUIRE(exported.str("outcome") == "ok");
  CHECK(std::filesystem::exists(
      next.exchangePath() + "/" + exported.result()->get("file_name")->s));
}

SCENARIO(
    "MCP compound late withdrawal cancellation and replay never invent rollback",
    "[integration][mcp][mesh-import][mesh-pressure][mesh-pressure-delivery]")
{
  const auto kind = GENERATE(0, 1, 2);
  PositionFixture f;
  const auto s = f.attach();
  const auto evidence = f.working(s, "tracer", true);
  const auto path = f.exchangePath();
  const operations::Credentials credentials{
      f.authority->launchCredential(), s.id, s.secret};
  auto *observer = f.scene.updateDelegate().emplace<ObserveMeshPublication>();
  bool once = false;
  observer->observe = [&] {
    if (once)
      return;
    once = true;
    if (kind < 2)
      f.authority->restrict(session::Target::scene(f.sceneId),
          kind == 0 ? session::Capability::READ : session::Capability::EDIT);
    else
      f.engine->cancel(credentials, {s.id, 2});
  };
  f.importPositions(s, 2, *evidence.result());
  const auto completed = f.await(s, 2);
  REQUIRE(once);
  REQUIRE(completed.flag("committed"));
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
            ->size()
      == 4);
  if (kind == 0) {
    CHECK_FALSE(completed.flag("result_visible"));
    CHECK_FALSE(completed.result());
    f.authority->relax(
        session::Target::scene(f.sceneId), session::Capability::READ);
  } else
    REQUIRE(completed.result()->get("batch_ref"));
  if (kind == 1) {
    // Late EDIT withdrawal still permits READ, but neither another import
    // nor restoration. The existing commit remains truthfully observable.
    f.exportMesh(s, 3, f.geometry.data());
    CHECK(f.await(s, 3).str("outcome") == "ok");
    const auto batch = *completed.result()->get("batch_ref");
    CHECK(f.restoreBatch(s, 4, batch).errorCode() == "forbidden");
    f.authority->relax(
        session::Target::scene(f.sceneId), session::Capability::EDIT);
  }
  const auto historical = f.status(s, s.id, 2);
  REQUIRE(historical.flag("committed"));
  REQUIRE(historical.result()->get("batch_ref"));
  const auto batch = *historical.result()->get("batch_ref");
  observer->observe = {};
  REQUIRE(
      ::unlink((path + "/" + evidence.result()->get("file_name")->s).c_str())
      == 0);
  REQUIRE(::unlink((path + "/edited.vmesh").c_str()) == 0);
  CHECK(f.exportMesh(s, 1, f.geometry.data()).flag("replayed"));
  CHECK_FALSE(std::filesystem::exists(
      path + "/" + evidence.result()->get("file_name")->s));
  f.restoreBatch(s, 5, batch);
  const auto restored = f.await(s, 5);
  REQUIRE(restored.flag("committed"));
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK(f.importPositions(s, 2, *evidence.result()).flag("replayed"));
  CHECK_FALSE(std::filesystem::exists(path + "/edited.vmesh"));
  f.restoreBatch(s, 6, batch);
  CHECK(f.await(s, 6).str("outcome") == "already_restored");
  CHECK(f.status(s, s.id, 2).flag("committed"));
}

SCENARIO(
    "MCP evicted mesh recovery stays charged without removing working files",
    "[integration][mcp][mesh-import][mesh-pressure][mesh-pressure-eviction]")
{
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 400000;
  PositionFixture f(limits);
  const auto a = f.attach();
  const auto evidence = f.working(a, "tracer", true);
  const auto file =
      f.exchangePath() + "/" + evidence.result()->get("file_name")->s;
  const auto size = std::filesystem::file_size(file);
  f.importPositions(a, 2, *evidence.result());
  const auto imported = f.await(a, 2);
  REQUIRE(imported.flag("committed"));
  const auto batch = *imported.result()->get("batch_ref");
  auto held = f.edits->restorations().find(
      {batch.get("launch_id")->s, batch.get("batch_id")->s});
  REQUIRE(held);
  REQUIRE(f.rpc("vsr/detach", "{}", &a).status == 200);
  const auto b = f.attach();
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
  const std::vector<unsigned char> imageBefore(262144, 71),
      imageRows(262400, 0);
  image->setData(static_cast<const void *>(imageBefore.data()));
  writeInput(
      f.exchangePath() + "/fifo.png", inputPng(256, 256, 6, imageRows, {}));
  f.importImage(b,
      1,
      f.objectHandle(image.data()),
      *f.sceneIds->imageDataVersion(f.sceneIds->handleOf(image.data())),
      "fifo.png");
  const auto imageImport = f.await(b, 1);
  REQUIRE(imageImport.flag("committed"));
  f.restoreBatch(b, 2, batch);
  const auto expired = f.await(b, 2);
  CHECK(expired.str("outcome") == "outcome_unavailable");
  CHECK(expired.result()->get("reason")->s == "expired");
  CHECK_FALSE(expired.flag("committed"));
  CHECK(std::filesystem::file_size(file) == size);
  CHECK(std::filesystem::exists(f.exchangePath() + "/edited.vmesh"));
  f.tool(b, "vsr_discover", "{\"request_id\":3}");
  const auto discovery = f.await(b, 3);
  CHECK(discovery.result()
            ->get("items")
            ->a.at(0)
            .get("image_exchange")
            ->get("exchange_limits")
            ->get("available_global_retention_bytes")
            ->n
      < 140000);
  // The second Geometry still holds the original mesh. It is independently
  // addressable, not an approximation or rebinding of the expired target.
  f.geometry = f.other;
  f.exportMesh(b, 4, f.geometry.data());
  const auto fresh = f.await(b, 4);
  REQUIRE(fresh.str("outcome") == "ok");
  f.importPositions(b, 5, *fresh.result());
  const auto next = f.await(b, 5);
  held.reset();
  f.images->pump();
  REQUIRE(next.flag("committed"));
  f.restoreBatch(b, 6, *next.result()->get("batch_ref"));
  REQUIRE(f.await(b, 6).flag("committed"));
  f.restoreBatch(b, 7, batch);
  CHECK(f.await(b, 7).str("outcome") == "outcome_unavailable");
  CHECK(std::filesystem::file_size(file) == size);
}

SCENARIO("MCP mesh publication race cleanup never follows replacement links",
    "[integration][mcp][mesh-export][mesh-pressure][mesh-pressure-cleanup]")
{
  const auto race = GENERATE(0, 1, 2, 3, 4, 5);
  PositionFixture f;
  TempDir outside;
  const auto sentinel = outside.path + "/source.obj";
  std::ofstream(sentinel) << "sentinel";
  const auto s = f.attach();
  const auto directory = f.exchangePath();
  std::string name;
  std::atomic<bool> raceApplied{false};
  const operations::Credentials credentials{
      f.authority->launchCredential(), s.id, s.secret};
  struct ClearWriteHook
  {
    ~ClearWriteHook()
    {
      interceptExportWrite = false;
      afterExportWrite = {};
    }
  } clear;
  afterExportWrite = [&](int fd) {
    char text[4096]{};
    const auto link = "/proc/self/fd/" + std::to_string(fd);
    if (::readlink(link.c_str(), text, sizeof(text) - 1) <= 0)
      return;
    name = text;
    if (race == 0)
      raceApplied = ::unlink(name.c_str()) == 0
          && ::symlink(sentinel.c_str(), name.c_str()) == 0;
    else if (race == 1)
      raceApplied =
          ::link(name.c_str(), (directory + "/alias.vmesh").c_str()) == 0;
    else if (race == 2)
      raceApplied = ::ftruncate(fd, 1) == 0;
    else if (race == 3)
      raceApplied = ::fchmod(fd, 0644) == 0;
    else if (race == 4)
      raceApplied =
          ::rename(directory.c_str(), (directory + "-old").c_str()) == 0
          && ::symlink(outside.path.c_str(), directory.c_str()) == 0;
    else {
      raceApplied = true;
      f.engine->cancel(credentials, {s.id, 1});
    }
  };
  interceptExportWrite = true;
  f.exportMesh(s, 1, f.geometry.data());
  const auto rejected = f.await(s, 1);
  REQUIRE(raceApplied.load());
  CHECK_FALSE(interceptExportWrite.load());
  CHECK(
      rejected.str("outcome") == (race == 5 ? "cancelled" : "invalid_request"));
  CHECK_FALSE(rejected.flag("committed"));
  CHECK(std::filesystem::file_size(sentinel) == 8);
  std::ifstream source(sentinel);
  std::string bytes;
  source >> bytes;
  CHECK(bytes == "sentinel");
  if (race == 0)
    CHECK(std::filesystem::is_symlink(name));
  else if (race == 5) {
    // Cancellation can publish its receipt before the owned worker drains.
    // Cleanup must still complete at the ordinary owning-thread pump boundary.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::filesystem::exists(name)) {
      f.drain();
      REQUIRE(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_FALSE(std::filesystem::exists(name));
  }
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
}

SCENARIO(
    "MCP restore cancellation before and after publication preserves one-way truth",
    "[integration][mcp][mesh-import][mesh-pressure][mesh-pressure-restore-cancel]")
{
  const auto boundary = GENERATE(0, 1, 2);
  PositionFixture f;
  const auto s = f.attach();
  const auto evidence = f.working(s, "tracer", true);
  f.importPositions(s, 2, *evidence.result());
  const auto imported = f.await(s, 2);
  REQUIRE(imported.flag("committed"));
  const auto batch = *imported.result()->get("batch_ref");
  const operations::Credentials credentials{
      f.authority->launchCredential(), s.id, s.secret};
  auto *observer = f.scene.updateDelegate().emplace<ObserveMeshPublication>();
  if (boundary == 0)
    f.restores->setFaultInjector([&] {
      f.engine->cancel(credentials, {s.id, 3});
      return false;
    });
  else
    observer->observe = [&] {
      if (boundary == 1)
        f.engine->cancel(credentials, {s.id, 3});
      else
        f.authority->restrict(
            session::Target::scene(f.sceneId), session::Capability::READ);
    };
  f.restoreBatch(s, 3, batch);
  const auto result = f.await(s, 3);
  CHECK(result.flag("committed") == (boundary != 0));
  if (boundary == 0) {
    CHECK(result.str("outcome") == "cancelled");
    CHECK(
        f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
            ->size()
        == 4);
  } else {
    CHECK(
        f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
        == f.positions.data());
    if (boundary == 2) {
      CHECK_FALSE(result.flag("result_visible"));
      CHECK_FALSE(result.result());
      f.authority->relax(
          session::Target::scene(f.sceneId), session::Capability::READ);
    }
  }
  f.restores->setFaultInjector({});
  observer->observe = {};
  f.restoreBatch(s, 4, batch);
  const auto next = f.await(s, 4);
  CHECK(next.str("outcome") == (boundary == 0 ? "ok" : "already_restored"));
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK(f.restoreBatch(s, 3, batch).flag("replayed"));
  CHECK(f.status(s, s.id, 3).flag("committed") == (boundary != 0));
}

SCENARIO(
    "MCP decoder cancellation releases capacity without modifying working files",
    "[integration][mcp][mesh-import][mesh-pressure][mesh-pressure-worker]")
{
  PositionFixture f;
  const auto s = f.attach();
  const auto evidence = f.working(s, "tracer", true);
  const auto file = f.exchangePath() + "/edited.vmesh";
  const auto size = std::filesystem::file_size(file);
  const operations::Credentials credentials{
      f.authority->launchCredential(), s.id, s.secret};
  struct ClearReadHook
  {
    ~ClearReadHook()
    {
      interceptImportRead = false;
      afterImportRead = {};
    }
  } clear;
  afterImportRead = [&](int) { f.engine->cancel(credentials, {s.id, 2}); };
  interceptImportRead = true;
  f.importPositions(s, 2, *evidence.result());
  const auto cancelled = f.await(s, 2);
  CHECK_FALSE(interceptImportRead.load());
  CHECK(cancelled.str("outcome") == "cancelled");
  CHECK_FALSE(cancelled.flag("committed"));
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == 1);
  CHECK(std::filesystem::file_size(file) == size);
  CHECK(f.importPositions(s, 2, *evidence.result()).flag("replayed"));
  f.importPositions(s, 3, *evidence.result());
  const auto imported = f.await(s, 3);
  REQUIRE(imported.flag("committed"));
  f.restoreBatch(s, 4, *imported.result()->get("batch_ref"));
  REQUIRE(f.await(s, 4).flag("committed"));
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK(std::filesystem::file_size(file) == size);
}

SCENARIO(
    "MCP mesh capacity charges unique actual old storage rather than bindings",
    "[integration][mcp][mesh-import][mesh-pressure][mesh-pressure-accounting]")
{
  constexpr std::size_t COUNT = 12288;
  constexpr std::size_t PAYLOAD = COUNT * 12;
  std::vector<float> points(COUNT * 3);
  for (std::size_t i = 0; i < points.size(); ++i)
    points[i] = PositionFixture::POINTS[i % 9];
  std::vector<double> requirements;
  for (unsigned independent = 0; independent < 2; ++independent) {
    image::ExchangeLimits limits;
    limits.globalRetentionBytes = 4096;
    limits.hardRetentionBytes = 4096;
    PositionFixture f(limits);
    auto positions = f.scene.createArray(ANARI_FLOAT32_VEC3, COUNT);
    positions->setData(static_cast<const void *>(points.data()));
    auto normals = independent ? f.scene.createArray(ANARI_FLOAT32_VEC3, COUNT)
                               : positions;
    if (independent)
      normals->setData(static_cast<const void *>(points.data()));
    f.geometry->setParameterObject("vertex.position", *positions);
    f.geometry->setParameterObject("vertex.normal", *normals)
        ->setEnabled(false);
    f.geometry->setParameterObject("vertex.attribute0", *normals);
    f.geometry->setParameterObject("vertex.attribute1", *normals);
    const auto s = f.attach();
    const auto evidence = f.working(s, "split");
    const auto revision = f.sceneIds->sceneRevision();
    const auto objects = f.scene.numberOfObjects(ANARI_ARRAY);
    f.importPositions(s, 2, *evidence.result());
    const auto rejected = f.await(s, 2);
    REQUIRE(rejected.str("outcome") == "busy");
    REQUIRE(rejected.result()->get("required_capacity_bytes"));
    requirements.push_back(
        rejected.result()->get("required_capacity_bytes")->n);
    CHECK(requirements.back() > 2 * PAYLOAD);
    CHECK(rejected.result()->get("configured_capacity_bytes")->n == 4096);
    CHECK_FALSE(rejected.flag("committed"));
    CHECK(f.sceneIds->sceneRevision() == revision);
    CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == objects);
    CHECK(
        f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
        == positions.data());
    CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.normal")
        == normals.data());
    CHECK(std::memcmp(positions->data(), points.data(), PAYLOAD) == 0);
    CHECK(std::memcmp(normals->data(), points.data(), PAYLOAD) == 0);
  }
  // The second unique old Array costs its real bytes plus bounded lifetime
  // controls. Extra aliases of that Array cannot multiply or erase this cost.
  REQUIRE(requirements.size() == 2);
  CHECK(requirements[1] - requirements[0] > PAYLOAD);
  CHECK(requirements[1] - requirements[0] < PAYLOAD + 16384);
}

SCENARIO("MCP mesh import changes Helide pixels and restores identities",
    "[integration][mcp][mesh-import][mesh-topology][mesh-tool]")
{
  bool topology = false, retainedPrimitive = false;
  bool externalTool = false, independentlyAuthored = false;
  bool toolMaintained = false;
  SECTION("count-preserving positions") {}
  SECTION("count-changing topology, removal, addition and aliases")
  {
    topology = true;
  }
  SECTION("vertex count changes retain per-primitive rendered appearance")
  {
    topology = true;
    retainedPrimitive = true;
  }
  SECTION("Trimesh translation retains typed attributes and aliases")
  {
    externalTool = true;
    toolMaintained = true;
  }
  SECTION("actual Trimesh subdivision with explicit attribute loss")
  {
    topology = true;
    externalTool = true;
  }
  SECTION("independently authored Trimesh bundle without export provenance")
  {
    topology = true;
    externalTool = true;
    independentlyAuthored = true;
  }
  Helide helide;
  REQUIRE(helide.device);
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
  auto positions = f.scene.createArray(ANARI_FLOAT32_VEC3, 3);
  const float points[] = {-1, -1, 0, 1, -1, 0, 0, 1, 0};
  positions->setData(static_cast<const void *>(points));
  auto geometry = f.scene.createObject<vsr::scene::Geometry>("triangle");
  geometry->setName("editable triangle");
  geometry->setParameterObject("vertex.position", *positions);
  auto other = f.scene.createObject<vsr::scene::Geometry>("triangle");
  other->setParameterObject("vertex.position", *positions);
  auto material = f.scene.createObject<vsr::scene::Material>("matte");
  material->setParameter("color", vsr::math::float3(1, 0, 0));
  vsr::scene::ArrayRef priorColor;
  if (retainedPrimitive || externalTool) {
    priorColor = f.scene.createArray(ANARI_FLOAT32_VEC3, 1);
    const float red[] = {1, 0, 0};
    priorColor->setData(static_cast<const void *>(red));
    geometry->setParameterObject("primitive.color", *priorColor);
    other->setParameterObject("primitive.color", *priorColor);
    material->setParameter("color", "color");
  }
  auto first = f.scene.createSurface("first", geometry, material);
  auto second = f.scene.createSurface("second", geometry, material);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  const unsigned char texel[] = {23, 45, 67, 255};
  image->setData(static_cast<const void *>(texel));
  auto sampler = f.scene.createObject<vsr::scene::Sampler>("image2D");
  sampler->setParameterObject("image", *image);
  auto unrelatedMaterial = f.scene.getObject<vsr::scene::Material>(0);
  unrelatedMaterial->setParameterObject("color", *sampler);
  auto root = f.scene.defaultLayer()->root();
  auto leftMatrix = vsr::math::IDENTITY_MAT4;
  auto rightMatrix = vsr::math::IDENTITY_MAT4;
  leftMatrix[3].x = -1.5f;
  rightMatrix[3].x = 1.5f;
  auto leftNode = f.scene.insertChildTransformNode(root, leftMatrix);
  auto rightNode = f.scene.insertChildTransformNode(root, rightMatrix);
  f.scene.insertChildObjectNode(leftNode, first);
  f.scene.insertChildObjectNode(rightNode, second);
  auto indices = f.scene.createArray(ANARI_UINT32_VEC3, 1);
  const std::uint32_t triangle[] = {0, 1, 2};
  indices->setData(static_cast<const void *>(triangle));
  geometry->setParameterObject("primitive.index", *indices);
  auto coordinates = f.scene.createArray(ANARI_FLOAT32_VEC2, 3);
  const float uv[] = {0, 0, 1, 0, 0.5, 1};
  coordinates->setData(static_cast<const void *>(uv));
  geometry->setParameterObject("vertex.attribute0", *coordinates);
  vsr::scene::ArrayRef fixedColors;
  const unsigned char rgba[] = {0, 1, 254, 255, 0, 1, 254, 255, 0, 1, 254, 255};
  if (toolMaintained) {
    geometry->setParameterObject("vertex.attribute1", *coordinates);
    geometry->parameter("vertex.attribute1")->setEnabled(false);
    fixedColors = f.scene.createArray(ANARI_UFIXED8_VEC4, 3);
    fixedColors->setData(static_cast<const void *>(rgba));
    geometry->setParameterObject("vertex.color", *fixedColors);
    geometry->parameter("vertex.color")->setEnabled(false);
    geometry->setParameterObject("vertex.attribute2", *fixedColors);
    geometry->parameter("vertex.attribute2")->setEnabled(false);
  }
  geometry->setParameter("custom", 17.f);
  auto camera = f.scene.createObject<vsr::scene::Camera>("perspective");
  camera->setParameter("position", vsr::math::float3(0, 0, 5));
  camera->setParameter("direction", vsr::math::float3(0, 0, -1));
  camera->setParameter("up", vsr::math::float3(0, 1, 0));
  auto *index =
      f.scene.updateDelegate().emplace<vsr::rendering::RenderIndexAllLayers>(
          f.scene, vsr::core::Token("mesh-helide"), helide.device);
  index->populate();
  HelideBackend backend;
  backend.d = helide.device;
  backend.index = index;
  backend.cameraIndex = camera.index();
  backend.renderer =
      anari::newObject<anari::Renderer>(helide.device, "default");
  anari::setParameter(helide.device,
      backend.renderer,
      "background",
      vsr::math::float4(0, 0, 0, 1));
  anari::commitParameters(helide.device, backend.renderer);
  const auto pixels = [&] {
    REQUIRE(backend.begin(64, 48));
    anari::wait(helide.device, backend.frame);
    const auto image = backend.image();
    const auto *data = static_cast<const std::uint8_t *>(image.pixels);
    std::vector<std::uint8_t> result(data, data + 64 * 48 * 4);
    REQUIRE(backend.release());
    return result;
  };
  const auto colored =
      [](const std::vector<std::uint8_t> &image, int begin, int end) {
        std::size_t count = 0;
        for (int y = 0; y < 48; ++y)
          for (int x = begin; x < end; ++x) {
            const auto p = (y * 64 + x) * 4;
            if (image[p] > image[p + 1] + 10 && image[p] > image[p + 2] + 10)
              ++count;
          }
        return count;
      };
  const auto before = pixels();
  REQUIRE(colored(before, 0, 32) > 20);
  REQUIRE(colored(before, 32, 64) > 20);
  const auto s = f.attach();
  REQUIRE(f.exportMesh(s, 1, geometry.data()).str("kind") == "mesh_export");
  const auto exported = f.await(s, 1);
  REQUIRE(exported.str("outcome") == "ok");
  const auto path = f.exchangePath();
  // Independent authoring receives a nonexistent input path: it cannot derive
  // data or source metadata from the export artifact. Only the MCP caller
  // copies the fresh target/dependency evidence; that is not bundle provenance.
  const auto inputName = independentlyAuthored
      ? "never-exported.vmesh"
      : exported.result()->get("file_name")->s;
  const auto command = std::string(VSR_MESH_TEST_PYTHON) + " "
      + VSR_MESH_TEST_SOURCE
      + (externalTool    ? "/scripts/mesh-tool-replacement.py '"
              : topology ? "/scripts/test-mesh-topology.py '"
                         : "/scripts/test-mesh-position.py '")
      + path + "/" + inputName + "' '" + path + "/edited.vmesh'"
      + (retainedPrimitive ? " retain-primitive" : "")
      + (independentlyAuthored ? " --independent" : "")
      + (toolMaintained ? " --maintained" : "");
  REQUIRE(std::system(command.c_str()) == 0);
  const auto arguments = "{\"request_id\":2,\"scene_id\":"
      + std::to_string(f.sceneId) + ",\"handle\":"
      + json::quote(f.objectHandle(geometry.data())) + ",\"target_version\":"
      + json::write(*exported.result()->get("target_version"))
      + ",\"dependencies\":"
      + json::write(*exported.result()->get("dependencies"))
      + ",\"file_name\":\"edited.vmesh\"}";
  REQUIRE(f.tool(s, "vsr_mesh_import", arguments).str("kind") == "mesh_import");
  const auto imported = f.await(s, 2);
  REQUIRE(imported.flag("committed"));
  REQUIRE(imported.str("outcome") == "ok");
  auto replacement =
      geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position");
  REQUIRE(replacement != positions.data());
  CHECK(std::memcmp(positions->data(), points, sizeof(points)) == 0);
  CHECK(other->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == positions.data());
  CHECK(first->parameterValueAsObject<vsr::scene::Geometry>("geometry")
      == geometry.data());
  CHECK(second->parameterValueAsObject<vsr::scene::Geometry>("geometry")
      == geometry.data());
  CHECK(first->parameterValueAsObject<vsr::scene::Material>("material")
      == material.data());
  CHECK(second->parameterValueAsObject<vsr::scene::Material>("material")
      == material.data());
  CHECK(sampler->parameterValueAsObject<vsr::scene::Array>("image")
      == image.data());
  CHECK(unrelatedMaterial->parameterValueAsObject<vsr::scene::Sampler>("color")
      == sampler.data());
  CHECK(std::memcmp(image->data(), texel, sizeof(texel)) == 0);
  CHECK(geometry->name() == "editable triangle");
  if (topology) {
    CHECK(replacement->size()
        == (externalTool && !independentlyAuthored ? 6 : 4));
    CHECK(geometry->parameterValueAsObject<vsr::scene::Array>("primitive.index")
              ->size()
        == (externalTool && !independentlyAuthored ? 4
                : retainedPrimitive                ? 1
                                                   : 2));
    if (retainedPrimitive) {
      CHECK(
          geometry->parameterValueAsObject<vsr::scene::Array>("primitive.color")
          == priorColor.data());
      CHECK(other->parameterValueAsObject<vsr::scene::Array>("primitive.color")
          == priorColor.data());
      CHECK(material->parameter("color")->value().getString() == "color");
    }
    if (externalTool) {
      CHECK(material->parameter("color")->value().getString() == "color");
      CHECK(other->parameterValueAsObject<vsr::scene::Array>("primitive.color")
          == priorColor.data());
      const float red[] = {1, 0, 0};
      CHECK(std::memcmp(priorColor->data(), red, sizeof(red)) == 0);
    }
    CHECK_FALSE(geometry->parameter("vertex.attribute0"));
    CHECK(geometry->parameterValueAsObject<vsr::scene::Array>("primitive.color")
        == geometry->parameterValueAsObject<vsr::scene::Array>(
            "primitive.attribute1"));
    CHECK_FALSE(geometry->parameter("primitive.attribute1")->isEnabled());
  } else {
    CHECK(geometry->parameterValueAsObject<vsr::scene::Array>("primitive.index")
        == indices.data());
    CHECK(
        geometry->parameterValueAsObject<vsr::scene::Array>("vertex.attribute0")
        == coordinates.data());
  }
  if (toolMaintained) {
    CHECK(geometry->parameterValueAsObject<vsr::scene::Array>("primitive.color")
        == priorColor.data());
    CHECK(
        geometry->parameterValueAsObject<vsr::scene::Array>("vertex.attribute1")
        == coordinates.data());
    CHECK_FALSE(geometry->parameter("vertex.attribute1")->isEnabled());
    CHECK(geometry->parameterValueAsObject<vsr::scene::Array>("vertex.color")
        == fixedColors.data());
    CHECK(
        geometry->parameterValueAsObject<vsr::scene::Array>("vertex.attribute2")
        == fixedColors.data());
    CHECK(fixedColors->elementType() == ANARI_UFIXED8_VEC4);
    CHECK(std::memcmp(fixedColors->data(), rgba, sizeof(rgba)) == 0);
  }
  CHECK(std::memcmp(indices->data(), triangle, sizeof(triangle)) == 0);
  CHECK(std::memcmp(coordinates->data(), uv, sizeof(uv)) == 0);
  CHECK(geometry->parameter("custom")->value().get<float>() == 17.f);
  CHECK(leftNode->value().getTransform() == leftMatrix);
  CHECK(rightNode->value().getTransform() == rightMatrix);
  const auto changed = pixels();
  CHECK(changed != before);
  if (topology) {
    CHECK(colored(changed, 0, 32) > 0);
    CHECK(colored(changed, 32, 64) > 0);
    CHECK(colored(changed, 0, 32) < colored(before, 0, 32));
    CHECK(colored(changed, 32, 64) < colored(before, 32, 64));
  } else {
    CHECK(colored(changed, 0, 32) == 0);
    CHECK(colored(changed, 32, 64) == 0);
  }
  CHECK(f.tool(s, "vsr_mesh_import", arguments).flag("replayed"));
  CHECK(geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == replacement);
  REQUIRE(f.restoreBatch(s, 3, *imported.result()->get("batch_ref")).str("kind")
      == "restore");
  const auto restored = f.await(s, 3);
  REQUIRE(restored.flag("committed"));
  REQUIRE(restored.str("outcome") == "ok");
  CHECK(geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == positions.data());
  CHECK(std::memcmp(positions->data(), points, sizeof(points)) == 0);
  CHECK(geometry->parameterValueAsObject<vsr::scene::Array>("primitive.index")
      == indices.data());
  CHECK(geometry->parameterValueAsObject<vsr::scene::Array>("vertex.attribute0")
      == coordinates.data());
  if (retainedPrimitive || externalTool)
    CHECK(geometry->parameterValueAsObject<vsr::scene::Array>("primitive.color")
        == priorColor.data());
  else
    CHECK_FALSE(geometry->parameter("primitive.color"));
  CHECK_FALSE(geometry->parameter("primitive.attribute1"));
  CHECK_FALSE(geometry->parameter("faceVarying.attribute2"));
  CHECK_FALSE(geometry->parameter("vertex.tangent"));
  if (toolMaintained) {
    CHECK(geometry->parameterValueAsObject<vsr::scene::Array>("vertex.color")
        == fixedColors.data());
    CHECK(
        geometry->parameterValueAsObject<vsr::scene::Array>("vertex.attribute2")
        == fixedColors.data());
    CHECK(
        geometry->parameterValueAsObject<vsr::scene::Array>("vertex.attribute1")
        == coordinates.data());
    CHECK(std::memcmp(fixedColors->data(), rgba, sizeof(rgba)) == 0);
  }
  CHECK(sampler->parameterValueAsObject<vsr::scene::Array>("image")
      == image.data());
  CHECK(unrelatedMaterial->parameterValueAsObject<vsr::scene::Sampler>("color")
      == sampler.data());
  CHECK(std::memcmp(image->data(), texel, sizeof(texel)) == 0);
  CHECK(pixels() == before);
  anari::release(helide.device, backend.renderer);
  f.scene.updateDelegate().erase(index);
}

SCENARIO("MCP Mesh Export provides a READ-only lossless working bundle",
    "[integration][mcp][mesh-export]")
{
  Fixture f({}, false, false, false, 120., true, UINT64_MAX, {}, true);
  auto positions = f.scene.createArray(ANARI_FLOAT32_VEC3, 3);
  const float points[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
  positions->setData(static_cast<const void *>(points));
  auto geometry = f.scene.createObject<vsr::scene::Geometry>("triangle");
  geometry->setParameterObject("vertex.position", *positions);
  geometry->setParameterObject("vertex.normal", *positions);
  geometry->parameter("vertex.normal")->setEnabled(false);
  const auto identity = f.sceneIds->handleOf(geometry.data());
  char wire[33];
  std::snprintf(wire,
      sizeof(wire),
      "%016llx%016llx",
      (unsigned long long)identity.domain,
      (unsigned long long)identity.serial);
  const std::string handle = wire;
  const auto s = f.attach();
  const auto args = "{\"request_id\":1,\"scene_id\":"
      + std::to_string(f.sceneId) + ",\"handle\":" + json::quote(handle) + "}";
  REQUIRE(f.tool(s, "vsr_mesh_export", args).str("kind") == "mesh_export");
  auto done = f.await(s, 1);
  REQUIRE(done.str("outcome") == "ok");
  CHECK_FALSE(done.flag("committed"));
  REQUIRE(done.result()->get("file_name"));
  const auto path = f.exchangePath() + "/" + done.result()->get("file_name")->s;
  const auto command = std::string(VSR_MESH_TEST_PYTHON) + " "
      + VSR_MESH_TEST_SOURCE + "/scripts/test-mesh-export.py '" + path + "'";
  REQUIRE(std::system(command.c_str()) == 0);
  CHECK(std::memcmp(positions->data(), points, sizeof(points)) == 0);
  CHECK(f.tool(s, "vsr_mesh_export", args).flag("replayed"));
  // Helper changed the working copy; a historical replay must not rewrite it.
  REQUIRE(std::system((command + " --edited").c_str()) == 0);
}

SCENARIO("MCP Mesh Export preserves indexed typed attributes and associations",
    "[integration][mcp][mesh-export]")
{
  Fixture f({}, false, false, false, 120., true, UINT64_MAX, {}, true);
  auto geometry = f.scene.createObject<vsr::scene::Geometry>("triangle");
  auto positions = f.scene.createArray(ANARI_FLOAT64_VEC3, 4);
  const double points[] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 3, 4, 5};
  positions->setData(static_cast<const void *>(points));
  geometry->setParameterObject("vertex.position", *positions);
  auto indices = f.scene.createArray(ANARI_UINT64_VEC3, 1);
  const std::uint64_t triangle[] = {0, 1, 2};
  indices->setData(static_cast<const void *>(triangle));
  geometry->setParameterObject("primitive.index", *indices);
  auto uv = f.scene.createArray(ANARI_FLOAT32_VEC2, 3);
  const float coordinates[] = {0, 0, 1, 0, 0, 1};
  uv->setData(static_cast<const void *>(coordinates));
  geometry->setParameterObject("faceVarying.attribute0", *uv);
  geometry->setParameterObject("faceVarying.attribute1", *uv);
  auto primitive = f.scene.createArray(ANARI_INT16, 1);
  const std::int16_t value = -123;
  primitive->setData(static_cast<const void *>(&value));
  geometry->setParameterObject("primitive.attribute2", *primitive);
  auto colors = f.scene.createArray(ANARI_UFIXED8_VEC4, 4);
  const unsigned char rgba[] = {
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 252, 253, 254, 255};
  colors->setData(static_cast<const void *>(rgba));
  geometry->setParameterObject("vertex.color", *colors);
  auto tangents = f.scene.createArray(ANARI_FLOAT32_VEC4, 4);
  const float tangent[] = {1, 0, 0, -1, 1, 0, 0, 1, 1, 0, 0, -1, 1, 0, 0, 1};
  tangents->setData(static_cast<const void *>(tangent));
  geometry->setParameterObject("vertex.tangent", *tangents);
  auto independent = f.scene.createArray(ANARI_FLOAT32_VEC2, 3);
  independent->setData(static_cast<const void *>(coordinates));
  geometry->setParameterObject("faceVarying.attribute3", *independent);
  const auto s = f.attach();
  REQUIRE(f.exportMesh(s, 1, geometry.data()).str("kind") == "mesh_export");
  const auto done = f.await(s, 1);
  REQUIRE(done.str("outcome") == "ok");
  const auto path = f.exchangePath() + "/" + done.result()->get("file_name")->s;
  const auto command = std::string(VSR_MESH_TEST_PYTHON) + " "
      + VSR_MESH_TEST_SOURCE + "/scripts/test-mesh-export.py '" + path
      + "' --indexed";
  REQUIRE(std::system(command.c_str()) == 0);
  CHECK(geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == positions.data());
}

SCENARIO("MCP Mesh Export refuses complete unsupported or incoherent meshes",
    "[integration][mcp][mesh-export]")
{
  Fixture f({}, false, false, false, 120., true, UINT64_MAX, {}, true);
  auto geometry = f.scene.createObject<vsr::scene::Geometry>("triangle");
  auto positions = f.scene.createArray(ANARI_FLOAT32_VEC3, 3);
  const float points[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
  positions->setData(static_cast<const void *>(points));
  geometry->setParameterObject("vertex.position", *positions);
  std::string reason;
  SECTION("unknown disabled dependency")
  {
    geometry->setParameterObject("vertex.unsupported", *positions);
    geometry->parameter("vertex.unsupported")->setEnabled(false);
    reason = "unsupported_dependency";
  }
  SECTION("object reference dependency")
  {
    auto material = f.scene.createObject<vsr::scene::Material>("matte");
    geometry->setParameterObject("material", *material);
    reason = "unsupported_dependency";
  }
  SECTION("mapped storage")
  {
    positions->map();
    reason = "array_mapped";
  }
  SECTION("proxy")
  {
    auto proxy = f.scene.createArrayProxy(ANARI_FLOAT32_VEC3, 3);
    geometry->setParameterObject("vertex.position", *proxy);
    reason = "unsupported_storage";
  }
#if VSR_USE_CUDA
  SECTION("CUDA residency")
  {
    auto cuda = f.scene.createArrayCUDA(ANARI_FLOAT32_VEC3, 3);
    geometry->setParameterObject("vertex.position", *cuda);
    reason = "unsupported_storage";
  }
#endif
  SECTION("two dimensional numeric layout")
  {
    auto multidimensional = f.scene.createArray(ANARI_FLOAT32_VEC3, 3, 1);
    geometry->setParameterObject("vertex.position", *multidimensional);
    reason = "unsupported_layout";
  }
  SECTION("payload byte ceiling")
  {
    auto oversized = f.scene.createArray(ANARI_FLOAT32, (64u << 20) / 4 + 1);
    geometry->setParameterObject("vertex.attribute0", *oversized);
    reason = "resource_limit";
  }
  SECTION("wrong subtype")
  {
    geometry = f.scene.createObject<vsr::scene::Geometry>("quad");
    reason = "unsupported_geometry";
  }
  SECTION("missing positions")
  {
    geometry->removeParameter("vertex.position");
    reason = "missing_positions";
  }
  SECTION("invalid attribute cardinality")
  {
    auto attribute = f.scene.createArray(ANARI_FLOAT32_VEC2, 2);
    const float uv[] = {0, 0, 1, 1};
    attribute->setData(static_cast<const void *>(uv));
    geometry->setParameterObject("vertex.attribute0", *attribute);
    reason = "attribute_count_mismatch";
  }
  SECTION("nonfinite positions")
  {
    auto *mapped = positions->mapAs<vsr::math::float3>();
    mapped[0].x = std::numeric_limits<float>::infinity();
    positions->unmap();
    reason = "nonfinite_mesh_value";
  }
  SECTION("out of range index")
  {
    auto index = f.scene.createArray(ANARI_UINT32_VEC3, 1);
    const std::uint32_t triangle[] = {0, 1, 3};
    index->setData(static_cast<const void *>(triangle));
    geometry->setParameterObject("primitive.index", *index);
    reason = "index_out_of_range";
  }
  SECTION("corner normals require three components even with heap-backed names")
  {
    auto normals = f.scene.createArray(ANARI_FLOAT32_VEC2, 3);
    const float values[] = {0, 1, 0, 1, 0, 1};
    normals->setData(static_cast<const void *>(values));
    geometry->setParameterObject("faceVarying.normal", *normals);
    reason = "unsupported_element_type";
  }
  SECTION("tangents require four components")
  {
    geometry->setParameterObject("vertex.tangent", *positions);
    reason = "unsupported_element_type";
  }
  SECTION("colours require three or four components")
  {
    auto colors = f.scene.createArray(ANARI_UINT8, 3);
    const unsigned char values[] = {0, 1, 2};
    colors->setData(static_cast<const void *>(values));
    geometry->setParameterObject("vertex.color", *colors);
    reason = "unsupported_element_type";
  }
  SECTION("tracked numeric families outside the bundle profile fail closed")
  {
    const auto type = GENERATE(ANARI_FIXED64, ANARI_UFIXED64, ANARI_FLOAT16);
    auto attribute = f.scene.createArray(type, 3);
    geometry->setParameterObject("vertex.attribute0", *attribute);
    reason = "unsupported_element_type";
  }
  SECTION("unsupported numeric type")
  {
    auto reference = f.scene.createArray(ANARI_ARRAY1D, 3);
    geometry->setParameterObject("vertex.attribute0", *reference);
    reason = "unsupported_element_type";
  }
  const auto s = f.attach();
  f.exportMesh(s, 1, geometry.data());
  const auto done = f.await(s, 1);
  REQUIRE(done.str("outcome") != "ok");
  REQUIRE(done.result());
  CHECK(done.result()->get("reason")->s == reason);
  CHECK_FALSE(done.result()->get("file_name"));
  CHECK(std::filesystem::is_empty(f.exchangePath()));
  if (positions->isMapped())
    positions->unmap();
}

SCENARIO(
    "MCP Mesh Export evidence cannot revive after ordinary writes or binding changes",
    "[integration][mcp][mesh-export]")
{
  Fixture f({}, false, false, false, 120., true, UINT64_MAX, {}, true);
  auto geometry = f.scene.createObject<vsr::scene::Geometry>("triangle");
  auto positions = f.scene.createArray(ANARI_FLOAT32_VEC3, 3);
  const float points[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
  positions->setData(static_cast<const void *>(points));
  geometry->setParameterObject("vertex.position", *positions);
  const auto s = f.attach();
  f.exportMesh(s, 1, geometry.data());
  const auto first = f.await(s, 1);
  REQUIRE(first.str("outcome") == "ok");
  const auto evidence = json::write(*first.result()->get("dependencies"));
  const auto target = first.result()->get("target_version")->s;
  auto unrelated = f.scene.createObject<vsr::scene::Geometry>("sphere");
  unrelated->setParameter("radius", 1.f);
  f.exportMesh(s, 2, geometry.data());
  const auto second = f.await(s, 2);
  REQUIRE(second.str("outcome") == "ok");
  CHECK(json::write(*second.result()->get("dependencies")) == evidence);
  CHECK(second.result()->get("target_version")->s == target);
  positions->setData(static_cast<const void *>(points));
  f.exportMesh(s, 3, geometry.data());
  const auto third = f.await(s, 3);
  REQUIRE(third.str("outcome") == "ok");
  CHECK(json::write(*third.result()->get("dependencies")) != evidence);
  CHECK(third.result()->get("target_version")->s == target);
  geometry->parameter("vertex.position")->setEnabled(false);
  geometry->parameter("vertex.position")->setEnabled(true);
  f.exportMesh(s, 4, geometry.data());
  const auto fourth = f.await(s, 4);
  REQUIRE(fourth.str("outcome") == "ok");
  CHECK(fourth.result()->get("target_version")->s != target);
  auto replacement = f.scene.createArray(ANARI_FLOAT32_VEC3, 3);
  replacement->setData(static_cast<const void *>(points));
  geometry->setParameterObject("vertex.position", *replacement);
  geometry->setParameterObject("vertex.position", *positions);
  f.exportMesh(s, 5, geometry.data());
  const auto fifth = f.await(s, 5);
  REQUIRE(fifth.str("outcome") == "ok");
  CHECK(fifth.result()->get("target_version")->s
      != fourth.result()->get("target_version")->s);
  CHECK(json::write(*fifth.result()->get("dependencies"))
      == json::write(*fourth.result()->get("dependencies")));
  f.scene.removeObject(positions.data());
  auto recreated = f.scene.createArray(ANARI_FLOAT32_VEC3, 3);
  recreated->setData(static_cast<const void *>(points));
  geometry->setParameterObject("vertex.position", *recreated);
  f.exportMesh(s, 6, geometry.data());
  const auto sixth = f.await(s, 6);
  REQUIRE(sixth.str("outcome") == "ok");
  CHECK(json::write(*sixth.result()->get("dependencies"))
      != json::write(*fifth.result()->get("dependencies")));
}

SCENARIO(
    "MCP Mesh Export freezes bytes at capture and honors disabled agent edits",
    "[integration][mcp][mesh-export]")
{
  Fixture f({}, false, false, false, 120., true, UINT64_MAX, {}, true);
  auto geometry = f.scene.createObject<vsr::scene::Geometry>("triangle");
  auto positions = f.scene.createArray(ANARI_FLOAT32_VEC3, 3);
  const float original[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
  positions->setData(static_cast<const void *>(original));
  geometry->setParameterObject("vertex.position", *positions);
  geometry->setParameterObject("vertex.normal", *positions);
  geometry->parameter("vertex.normal")->setEnabled(false);
  const auto s = f.attach();
  f.authority->setAgentEditsDisabled(true);
  f.exportMesh(s, 1, geometry.data());
  REQUIRE(f.integration.service() > 0);
  const float later[] = {10, 0, 0, 11, 0, 0, 10, 1, 0};
  positions->setData(static_cast<const void *>(later));
  const auto done = f.await(s, 1);
  REQUIRE(done.str("outcome") == "ok");
  const auto path = f.exchangePath() + "/" + done.result()->get("file_name")->s;
  const auto command = std::string(VSR_MESH_TEST_PYTHON) + " "
      + VSR_MESH_TEST_SOURCE + "/scripts/test-mesh-export.py '" + path + "'";
  REQUIRE(std::system(command.c_str()) == 0);
  CHECK(std::memcmp(positions->data(), later, sizeof(later)) == 0);
}

SCENARIO(
    "MCP Mesh Export checks authority identity and strict request envelopes",
    "[integration][mcp][mesh-export]")
{
  Fixture f({}, false, false, false, 120., true, UINT64_MAX, {}, true);
  auto geometry = f.scene.createObject<vsr::scene::Geometry>("triangle");
  auto positions = f.scene.createArray(ANARI_FLOAT32_VEC3, 3);
  const float points[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
  positions->setData(static_cast<const void *>(points));
  geometry->setParameterObject("vertex.position", *positions);
  const auto s = f.attach();
  const auto base =
      "{\"request_id\":1,\"scene_id\":" + std::to_string(f.sceneId)
      + ",\"handle\":" + json::quote(f.objectHandle(geometry.data()));
  for (const auto &extra :
      {",\"file_name\":\"../asset.vmesh\"}", ",\"unknown\":true}"})
    CHECK(f.tool(s, "vsr_mesh_export", base + extra).errorCode()
        == "invalid_request");
  CHECK(f.tool(s,
             "vsr_mesh_export",
             "{\"request_id\":true,\"scene_id\":0,\"handle\":null}")
            .errorCode()
      == "invalid_request");
  const auto tools = f.rpc("tools/list", "{}");
  const auto listed = json::write(tools.json);
  CHECK(listed.find("vsr_mesh_export") != std::string::npos);
  CHECK(listed.find("vsr_mesh_import") != std::string::npos);
  json::Value metadata;
  REQUIRE(json::parse(f.images->metadata(f.sceneId), metadata));
  CHECK(metadata.get("mesh_export")->b);
  CHECK(metadata.get("mesh_import")->b);
  CHECK(metadata.get("mesh_field_cap")->n == 64);
  SECTION("READ denied at admission")
  {
    f.authority->restrict(
        session::Target::scene(f.sceneId), session::Capability::READ);
    CHECK(f.exportMesh(s, 1, geometry.data()).errorCode() == "forbidden");
  }
  SECTION("wrong kind")
  {
    const auto args =
        "{\"request_id\":1,\"scene_id\":" + std::to_string(f.sceneId)
        + ",\"handle\":" + json::quote(f.objectHandle(positions.data())) + "}";
    f.tool(s, "vsr_mesh_export", args);
    CHECK(f.await(s, 1).str("outcome") == "invalid_request");
  }
  SECTION("unknown handle")
  {
    f.tool(s,
        "vsr_mesh_export",
        "{\"request_id\":1,\"scene_id\":0,\"handle\":\"00000000000000000000000000000000\"}");
    CHECK(f.await(s, 1).str("outcome") == "invalid_request");
  }
  CHECK(std::filesystem::is_empty(f.exchangePath()));
}

SCENARIO("MCP Mesh Export removes unpublished output on late denial and races",
    "[integration][mcp][mesh-export]")
{
  ImageAllocator allocator;
  allocator.remaining = 1;
  image::ExchangeLimits limits;
  limits.allocator = &allocator;
  Fixture f({}, false, false, false, 120., true, UINT64_MAX, limits, true);
  auto geometry = f.scene.createObject<vsr::scene::Geometry>("triangle");
  auto positions = f.scene.createArray(ANARI_FLOAT32_VEC3, 3);
  const float points[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
  positions->setData(static_cast<const void *>(points));
  geometry->setParameterObject("vertex.position", *positions);
  const auto s = f.attach();
  const auto path = f.exchangePath();
  f.exportMesh(s, 1, geometry.data());
  REQUIRE(f.integration.service() > 0);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!allocator.deallocations.load()) {
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE_FALSE(std::filesystem::is_empty(path));
  const auto file = std::filesystem::directory_iterator(path)->path();
  std::string outcome = "invalid_request";
  SECTION("READ withdrawal")
  {
    f.authority->restrict(
        session::Target::scene(f.sceneId), session::Capability::READ);
    outcome = "forbidden";
  }
  SECTION("cancellation")
  {
    f.tool(s, "vsr_cancel", "{\"operation_ref\":" + ref(s.id, 1) + "}");
    outcome = "cancelled";
  }
  SECTION("deadline")
  {
    f.now += 31.;
    outcome = "timeout";
  }
  SECTION("working file changed")
  {
    std::ofstream(file, std::ios::binary | std::ios::trunc) << "changed";
  }
  SECTION("Geometry lifetime withdrawn")
  {
    f.scene.removeObject(geometry.data());
  }
  SECTION("directory identity replaced")
  {
    REQUIRE(::rename(path.c_str(), (path + "-old").c_str()) == 0);
    REQUIRE(::mkdir(path.c_str(), 0700) == 0);
  }
  const auto done = f.await(s, 1);
  CHECK(done.str("outcome") == outcome);
  CHECK_FALSE(done.flag("committed"));
  f.drain();
  CHECK_FALSE(std::filesystem::exists(file));
}

SCENARIO("MCP Mesh Export bounds physical allocations before file creation",
    "[integration][mcp][mesh-export]")
{
  ImageAllocator allocator;
  image::ExchangeLimits limits;
  SECTION("payload allocator failure")
  {
    limits.allocator = &allocator;
  }
  SECTION("transient ceiling")
  {
    limits.transientBytes = 1;
  }
  SECTION("physical ceiling")
  {
    auto memory = std::make_shared<vsr::rendering::FrameBudget>(1024);
    limits.reservePhysicalMemory = [memory](std::size_t bytes) {
      return memory->reserve(bytes);
    };
  }
  Fixture f({}, false, false, false, 120., true, UINT64_MAX, limits, true);
  auto geometry = f.scene.createObject<vsr::scene::Geometry>("triangle");
  auto positions = f.scene.createArray(ANARI_FLOAT32_VEC3, 3);
  const float points[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
  positions->setData(static_cast<const void *>(points));
  geometry->setParameterObject("vertex.position", *positions);
  const auto s = f.attach();
  f.exportMesh(s, 1, geometry.data());
  const auto done = f.await(s, 1);
  REQUIRE(done.str("outcome") == "busy");
  CHECK(done.result()->get("reason")->s == "resource_limit");
  CHECK_FALSE(done.flag("committed"));
  CHECK(std::filesystem::is_empty(f.exchangePath()));
}

SCENARIO("Mesh receipt and Activity copies retain bounded physical ownership",
    "[integration][mcp][mesh-export]")
{
  auto memory = std::make_shared<vsr::rendering::FrameBudget>(8u << 20);
  const auto baseline = memory->used();
  auto probe = memory->reserve(1);
  const auto overhead = memory->used() - baseline - 1;
  probe.reset();
  image::ExchangeLimits limits;
  limits.reservePhysicalMemory = [memory](std::size_t bytes) {
    return memory->reserve(bytes);
  };
  auto f = std::make_unique<Fixture>(inspection::PageLimits{},
      false,
      false,
      false,
      120.,
      true,
      UINT64_MAX,
      limits,
      true);
  auto geometry = f->scene.createObject<vsr::scene::Geometry>("triangle");
  auto positions = f->scene.createArray(ANARI_FLOAT32_VEC3, 3);
  const float points[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
  positions->setData(static_cast<const void *>(points));
  geometry->setParameterObject("vertex.position", *positions);
  const auto s = f->attach();
  f->exportMesh(s, 1, geometry.data());
  REQUIRE(f->await(s, 1).str("outcome") == "ok");
  const operations::Credentials credentials{
      f->authority->launchCredential(), s.id, s.secret};
  std::optional<operations::Status> held =
      f->engine->status(credentials, {s.id, 1});
  REQUIRE(held->terminal == Outcome::OK);
  const auto usage = memory->used();
  std::optional<operations::Status> copy = *held;
  CHECK(memory->used() == usage + 65536 + overhead);
  auto entries = f->imageActivity->snapshot();
  REQUIRE(entries.size() == 1);
  CHECK(entries[0].entry.kind == activity::OperationKind::MESH_EXPORT);
  std::optional<activity::Entry> entryCopy = entries[0].entry;
  REQUIRE_FALSE(entryCopy->batchRef);
  f.reset();
  CHECK(memory->used() == baseline + 4 * (65536 + overhead));
  CHECK(copy->result == held->result);
  held.reset();
  copy.reset();
  entries.clear();
  entryCopy.reset();
  CHECK(memory->used() == baseline);
}

SCENARIO(
    "MCP Mesh Export shares finite file quota without overwriting working copies",
    "[integration][mcp][mesh-export]")
{
  Fixture f({}, false, false, false, 120., true, UINT64_MAX, {}, true);
  auto geometry = f.scene.createObject<vsr::scene::Geometry>("triangle");
  auto positions = f.scene.createArray(ANARI_FLOAT32_VEC3, 3);
  const float points[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
  positions->setData(static_cast<const void *>(points));
  geometry->setParameterObject("vertex.position", *positions);
  const auto s = f.attach();
  SECTION("shared directory file ceiling")
  {
    const auto directory = f.exchangePath();
    for (unsigned i = 0; i < 1024; ++i)
      std::ofstream(directory + "/retained-" + std::to_string(i) + ".png")
          << "keep";
    f.exportMesh(s, 1, geometry.data());
    CHECK(f.await(s, 1).str("outcome") == "busy");
    CHECK(std::distance(std::filesystem::directory_iterator(directory),
              std::filesystem::directory_iterator{})
        == 1024);
  }
  SECTION("exclusive collision attempts")
  {
    MeshEntropyScope entropy;
    f.exportMesh(s, 1, geometry.data());
    const auto first = f.await(s, 1);
    REQUIRE(first.str("outcome") == "ok");
    const auto path =
        f.exchangePath() + "/" + first.result()->get("file_name")->s;
    std::ofstream(path, std::ios::binary | std::ios::trunc) << "keep";
    f.exportMesh(s, 2, geometry.data());
    CHECK(f.await(s, 2).str("outcome") == "busy");
    std::ifstream file(path);
    std::string content;
    file >> content;
    CHECK(content == "keep");
  }
}

SCENARIO("MCP Image Export captures sharing without mutating texels",
    "[integration][mcp][image-export]")
{
  Fixture f({}, true, false, false, 120., true);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 2, 2);
  image->setName("tracked-image");
  const unsigned char pixels[] = {
      17, 29, 41, 0, 3, 5, 7, 255, 101, 103, 107, 127, 211, 223, 227, 1};
  image->setData(static_cast<const void *>(pixels));
  auto sampler = f.scene.createObject<vsr::scene::Sampler>("image2D");
  sampler->setParameterObject("image", *image);
  auto light = f.scene.createObject<vsr::scene::Light>("hdri");
  light->setParameterObject("radiance", *image);
  auto s = f.attach();
  auto observed = f.inspectArray(s, 1);
  const auto &item = observed.result()->get("items")->a.at(0);
  const auto handle = item.get("handle")->s;
  const auto version = item.get("image_info")->get("data_version")->s;
  const auto args = "{\"request_id\":2,\"scene_id\":"
      + std::to_string(f.sceneId) + ",\"handle\":\"" + handle + "\"}";
  REQUIRE(f.tool(s, "vsr_image_export", args).str("kind") == "image_export");
  Reply done;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  do {
    f.drain();
    done = f.status(s, s.id, 2);
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (done.str("state") != "terminal");
  REQUIRE(done.str("outcome") == "ok");
  CHECK_FALSE(done.flag("committed"));
  const auto &result = *done.result();
  CHECK(result.get("data_version")->s == version);
  REQUIRE(result.get("affected_samplers")->a.size() == 1);
  REQUIRE(result.get("affected_users")->a.size() == 1);
  CHECK(result.get("affected_complete")->b);
  CHECK(std::memcmp(image->data(), pixels, sizeof(pixels)) == 0);
  auto after = f.inspectArray(s, 3);
  CHECK(after.result()
            ->get("items")
            ->a.at(0)
            .get("image_info")
            ->get("data_version")
            ->s
      == version);
  CHECK(f.tool(s, "vsr_image_export", args).flag("replayed"));
}

SCENARIO("MCP Image Export preserves every raw channel and Color Space",
    "[integration][mcp][image-export]")
{
  const anari::DataType types[] = {ANARI_UFIXED8,
      ANARI_UFIXED8_VEC2,
      ANARI_UFIXED8_VEC3,
      ANARI_UFIXED8_VEC4,
      ANARI_UFIXED8_R_SRGB,
      ANARI_UFIXED8_RA_SRGB,
      ANARI_UFIXED8_RGB_SRGB,
      ANARI_UFIXED8_RGBA_SRGB,
      ANARI_FIXED8,
      ANARI_FIXED8_VEC2,
      ANARI_FIXED8_VEC3,
      ANARI_FIXED8_VEC4};
  for (std::size_t format = 0; format < std::size(types); ++format) {
    Fixture f({}, true, false, false, 120., true);
    const std::size_t channels = format % 4 + 1;
    auto image = f.scene.createArray(types[format], 3, 2);
    image->setName("tracked-image");
    std::vector<unsigned char> samples(6 * channels);
    for (std::size_t i = 0; i < samples.size(); ++i)
      samples[i] = static_cast<unsigned char>(17 + i * 29);
    if (channels == 2 || channels == 4)
      samples[channels - 1] = 0; // Nonzero transparent color must survive.
    image->setData(static_cast<const void *>(samples.data()));
    const auto s = f.attach();
    auto inspection = f.inspectArray(s, 1);
    const auto &item = inspection.result()->get("items")->a.at(0);
    const auto handle = item.get("handle")->s;
    const auto version = item.get("image_info")->get("data_version")->s;
    REQUIRE(f.exportImage(s, 2, handle).str("kind") == "image_export");
    auto receipt = f.await(s, 2);
    REQUIRE(receipt.str("outcome") == "ok");
    const auto &r = *receipt.result();
    CHECK(r.get("channel_count")->n == channels);
    CHECK(r.get("color_space")->s
        == (format >= 4 && format < 8 ? "srgb" : "linear"));
    CHECK(r.get("row_order")->s == "top_down");
    CHECK(r.get("data_version")->s == version);
    const auto path = f.exchangePath() + "/" + r.get("file_name")->s;
    struct stat st{};
    REQUIRE(::stat(path.c_str(), &st) == 0);
    CHECK((st.st_mode & 0777) == 0600);
    CHECK(st.st_nlink == 1);
    std::ifstream input(path, std::ios::binary);
    std::vector<unsigned char> png((std::istreambuf_iterator<char>(input)), {});
    REQUIRE(png.size() >= 33);
    CHECK(png[24] == 8);
    const unsigned char colorTypes[] = {0, 4, 2, 6};
    CHECK(png[25] == colorTypes[channels - 1]);
    CHECK(png[28] == 0);
    // Only IHDR, IDAT and IEND: no color-management or ancillary chunks.
    std::size_t offset = 8;
    const char *chunks[] = {"IHDR", "IDAT", "IEND"};
    for (auto chunk : chunks) {
      REQUIRE(offset + 12 <= png.size());
      CHECK(std::string(
                reinterpret_cast<const char *>(png.data() + offset + 4), 4)
          == chunk);
      const std::size_t length = (std::size_t(png[offset]) << 24)
          | (std::size_t(png[offset + 1]) << 16)
          | (std::size_t(png[offset + 2]) << 8) | png[offset + 3];
      offset += length + 12;
    }
    CHECK(offset == png.size());
    int width{}, height{}, decodedChannels{};
    auto *decoded = stbi_load_from_memory(
        png.data(), int(png.size()), &width, &height, &decodedChannels, 0);
    REQUIRE(decoded);
    CHECK(width == 3);
    CHECK(height == 2);
    CHECK(decodedChannels == channels);
    CHECK(std::memcmp(decoded, samples.data(), samples.size()) == 0);
    stbi_image_free(decoded);
    const auto &limits = *r.get("exchange_limits");
    CHECK(limits.get("max_encoded_file_bytes")->n
        == 2 * (samples.size() + 2) + 65536);
    CHECK(limits.get("max_codec_work_bytes")->n
        == 4 * (samples.size() + 2) + 1048576);
    CHECK(limits.get("files_used")->n == 1);
    CHECK(limits.get("bytes_used")->n == png.size());
    CHECK(limits.get("usage_complete")->b);
    CHECK(limits.get("session_retained_physical_bytes")->n == 0);
    REQUIRE(::unlink(path.c_str()) == 0);
    CHECK(f.exportImage(s, 2, handle).flag("replayed"));
    CHECK(f.status(s, s.id, 2).result()->get("file_name")->s
        == r.get("file_name")->s);
    CHECK(::access(path.c_str(), F_OK) != 0); // Replay does not regenerate.
  }
}

SCENARIO("MCP Image Export rejects mapped Images and bounded resource pressure",
    "[integration][mcp][image-export]")
{
  image::ExchangeLimits limits;
  SECTION("small effective transient ceiling")
  {
    limits.transientBytes = 1024;
  }
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, limits);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  image->setName("tracked-image");
  const auto s = f.attach();
  const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
  const auto handle = item.get("handle")->s;
  const auto path = f.exchangePath();
  SECTION("mapping")
  {
    image->map();
    f.exportImage(s, 2, handle);
    auto result = f.await(s, 2);
    CHECK(result.str("outcome") == "busy");
    CHECK(result.result()->get("reason")->s == "image_mapped");
    CHECK(image->isMapped());
    image->unmap();
    return;
  }
  SECTION("file quota")
  {
    for (int i = 0; i < 1024; ++i) {
      std::ofstream file(path + "/agent-" + std::to_string(i) + ".png");
      file << 'x';
    }
  }
  SECTION("byte quota")
  {
    REQUIRE(::truncate((path + "/large.png").c_str(), 268435456) == -1);
    std::ofstream file(path + "/large.png");
    file.close();
    REQUIRE(::truncate((path + "/large.png").c_str(), 268435456) == 0);
  }
  SECTION("incomplete scan")
  {
    for (int i = 0; i < 4097; ++i)
      REQUIRE(::symlink("absent", (path + "/link-" + std::to_string(i)).c_str())
          == 0);
  }
  f.exportImage(s, 2, handle);
  auto result = f.await(s, 2);
  CHECK(result.str("outcome") == "busy");
  REQUIRE(result.result());
  CHECK(result.result()->get("reason")->s == "resource_limit");
  CHECK_FALSE(result.flag("committed"));
}

SCENARIO(
    "MCP Image Export serializes captures independently of write permission",
    "[integration][mcp][image-export]")
{
  Fixture f({}, true, false, false, 120., true, 2);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  image->setName("tracked-image");
  const unsigned char first[] = {17, 29, 41, 0};
  const unsigned char second[] = {101, 103, 107, 127};
  image->setData(static_cast<const void *>(first));
  const auto s = f.attach();
  const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
  const auto handle = item.get("handle")->s;
  const auto version = item.get("image_info")->get("data_version")->s;
  f.authority->setAgentEditsDisabled(true);
  REQUIRE(f.exportImage(s, 2, handle).str("state") == "pending");
  REQUIRE(f.integration.service() > 0); // Capture before the ordinary writer.
  image->setData(static_cast<const void *>(second));
  CHECK_THROWS(
      image->map()); // Exhausted launch-wide counter does not stop export.
  REQUIRE(f.exportImage(s, 3, handle).str("state") == "pending");
  auto a = f.await(s, 2);
  auto b = f.await(s, 3);
  REQUIRE(a.str("outcome") == "ok");
  REQUIRE(b.str("outcome") == "ok");
  CHECK(a.result()->get("file_name")->s != b.result()->get("file_name")->s);
  CHECK(a.result()->get("data_version")->s == version);
  CHECK(b.result()->get("data_version")->s != version);
  for (const auto *receipt : {a.result(), b.result()}) {
    std::ifstream input(f.exchangePath() + "/" + receipt->get("file_name")->s,
        std::ios::binary);
    std::vector<unsigned char> png((std::istreambuf_iterator<char>(input)), {});
    int width{}, height{}, channels{};
    auto *decoded = stbi_load_from_memory(
        png.data(), int(png.size()), &width, &height, &channels, 0);
    REQUIRE(decoded);
    CHECK(std::memcmp(decoded, receipt == a.result() ? first : second, 4) == 0);
    stbi_image_free(decoded);
  }
  f.authority->restrict(
      session::Target::scene(f.sceneId), session::Capability::READ);
  CHECK(f.exportImage(s, 4, handle).errorCode() == "forbidden");
}

SCENARIO(
    "MCP Image Export fails closed for foreign identity and replaced ancestry",
    "[integration][mcp][image-export]")
{
  Fixture f({}, true, false, false, 120., true);
  auto image = f.scene.createArray(ANARI_UFIXED8, 1, 1);
  image->setName("tracked-image");
  const auto s = f.attach();
  const auto handle =
      f.inspectArray(s, 1).result()->get("items")->a.at(0).get("handle")->s;
  SECTION("known foreign Array")
  {
    identity::SceneIdentity foreign(f.ids, f.hidden);
    f.images->attachScene(f.hiddenId, foreign);
    auto other = f.hidden.createArray(ANARI_UFIXED8, 1, 1);
    char text[33];
    auto h = foreign.handleOf(other.data());
    std::snprintf(text,
        sizeof(text),
        "%016llx%016llx",
        (unsigned long long)h.domain,
        (unsigned long long)h.serial);
    f.exportImage(s, 2, text);
    auto result = f.await(s, 2);
    CHECK(result.str("outcome") == "invalid_request");
    CHECK(result.result()->get("reason")->s == "foreign_scene");
  }
  SECTION("foreign non-Array retains invalid-handle diagnostics")
  {
    identity::SceneIdentity foreign(f.ids, f.hidden);
    f.images->attachScene(f.hiddenId, foreign);
    const auto material = f.hidden.createObject<vsr::scene::Material>("matte");
    const auto h = foreign.handleOf(material.data());
    char text[33];
    std::snprintf(text,
        sizeof(text),
        "%016llx%016llx",
        (unsigned long long)h.domain,
        (unsigned long long)h.serial);
    f.exportImage(s, 2, text);
    const auto result = f.await(s, 2);
    REQUIRE(result.str("outcome") == "invalid_request");
    CHECK(result.result()->get("reason")->s == "invalid_handle");
    CHECK(std::filesystem::is_empty(f.exchangePath()));
  }
  SECTION("unknown identity")
  {
    f.exportImage(s, 2, "00000000000000000000000000000000");
    const auto result = f.await(s, 2);
    CHECK(result.str("outcome") == "invalid_request");
    CHECK(result.result()->get("reason")->s == "invalid_handle");
    CHECK(std::filesystem::is_empty(f.exchangePath()));
  }
  SECTION("local non-Array")
  {
    f.tool(s,
        "vsr_inspect",
        "{\"request_id\":2,\"scene_id\":" + std::to_string(f.sceneId)
            + ",\"object_type\":\"ANARI_MATERIAL\"}");
    const auto observed = f.await(s, 2);
    REQUIRE(observed.str("outcome") == "ok");
    f.exportImage(
        s, 3, observed.result()->get("items")->a.at(0).get("handle")->s);
    const auto result = f.await(s, 3);
    REQUIRE(result.str("outcome") == "invalid_request");
    CHECK(result.result()->get("reason")->s == "invalid_handle");
    CHECK(std::filesystem::is_empty(f.exchangePath()));
  }
  SECTION("replaced exchange directory")
  {
    const auto path = f.exchangePath();
    REQUIRE(::rename(path.c_str(), (path + "-old").c_str()) == 0);
    REQUIRE(::mkdir(path.c_str(), 0700) == 0);
    f.exportImage(s, 2, handle);
    auto result = f.await(s, 2);
    CHECK(result.str("outcome") == "invalid_request");
    CHECK(result.result()->get("reason")->s == "file_rejected");
    json::Value metadata;
    REQUIRE(json::parse(f.images->metadata(f.sceneId), metadata));
    CHECK_FALSE(metadata.get("enabled")->b);
    const auto unavailable = f.inspectArray(s, 3);
    const auto &info =
        *unavailable.result()->get("items")->a.at(0).get("image_info");
    CHECK(info.get("state")->s == "unavailable");
    REQUIRE(info.get("reason"));
    CHECK(info.get("reason")->s == "file_rejected");
    CHECK_FALSE(info.get("data_version"));
    const auto version =
        f.sceneIds->imageDataVersion(f.sceneIds->handleOf(image.data()));
    const unsigned char changed[] = {123};
    image->setData(static_cast<const void *>(changed));
    REQUIRE(::rmdir(path.c_str()) == 0);
    REQUIRE(::rename((path + "-old").c_str(), path.c_str()) == 0);
    const auto available = f.inspectArray(s, 4);
    const auto &restoredInfo =
        *available.result()->get("items")->a.at(0).get("image_info");
    CHECK(restoredInfo.get("state")->s == "available");
    REQUIRE(version);
    CHECK(restoredInfo.get("data_version")->s != *version);
  }
  SECTION("symlinked exchange directory")
  {
    const auto path = f.exchangePath();
    REQUIRE(::rename(path.c_str(), (path + "-old").c_str()) == 0);
    REQUIRE(::symlink((path + "-old").c_str(), path.c_str()) == 0);
    f.exportImage(s, 2, handle);
    auto result = f.await(s, 2);
    CHECK(result.str("outcome") == "invalid_request");
    CHECK(result.result()->get("reason")->s == "file_rejected");
  }
  SECTION("cancel before capture")
  {
    f.exportImage(s, 2, handle);
    REQUIRE(f.tool(s, "vsr_cancel", "{\"operation_ref\":" + ref(s.id, 2) + "}")
            .errorCode()
            .empty());
    f.drain();
    CHECK(f.status(s, s.id, 2).str("outcome") == "cancelled");
  }
}

SCENARIO("MCP Image Export refuses raced leaf identity and link changes",
    "[integration][mcp][image-export][image-export-race]")
{
  Fixture f({}, true, false, false, 120., true);
  auto image = f.scene.createArray(ANARI_UFIXED8, 1, 1);
  image->setName("tracked-image");
  const unsigned char pixel[] = {87};
  image->setData(static_cast<const void *>(pixel));
  const auto s = f.attach();
  const auto handle =
      f.inspectArray(s, 1).result()->get("items")->a.at(0).get("handle")->s;
  const auto directory = f.exchangePath();
  TempDir outside;
  const auto sentinel = outside.path + "/keep.png";
  std::ofstream(sentinel) << "outside";
  std::string name;
  enum class Race
  {
    LINK,
    NAME,
    SIZE,
    MODE,
    ANCESTRY
  } race{Race::LINK};
  SECTION("hard link")
  {
    race = Race::LINK;
  }
  SECTION("replacement symlink")
  {
    race = Race::NAME;
  }
  SECTION("changed size")
  {
    race = Race::SIZE;
  }
  SECTION("changed mode")
  {
    race = Race::MODE;
  }
  SECTION("replaced ancestor")
  {
    race = Race::ANCESTRY;
  }
  struct HookScope
  {
    ~HookScope()
    {
      interceptExportWrite = false;
      afterExportWrite = {};
    }
  } hook;
  afterExportWrite = [&](int fd) {
    char text[4096]{};
    const auto link = "/proc/self/fd/" + std::to_string(fd);
    const auto length = ::readlink(link.c_str(), text, sizeof(text) - 1);
    if (length <= 0)
      return;
    name = text;
    switch (race) {
    case Race::LINK:
      ::link(name.c_str(), (directory + "/alias.png").c_str());
      break;
    case Race::NAME:
      ::unlink(name.c_str());
      ::symlink(sentinel.c_str(), name.c_str());
      break;
    case Race::SIZE:
      ::ftruncate(fd, 1);
      break;
    case Race::MODE:
      ::fchmod(fd, 0644);
      break;
    case Race::ANCESTRY:
      ::rename(directory.c_str(), (directory + "-old").c_str());
      ::symlink(outside.path.c_str(), directory.c_str());
      break;
    }
  };
  interceptExportWrite = true;
  f.exportImage(s, 2, handle);
  const auto result = f.await(s, 2);
  REQUIRE_FALSE(interceptExportWrite.load());
  REQUIRE(result.str("outcome") == "invalid_request");
  CHECK(result.result()->get("reason")->s == "file_rejected");
  CHECK_FALSE(result.flag("committed"));
  if (race == Race::NAME)
    CHECK(std::filesystem::is_symlink(
        name)); // Failure cleanup owns no replacement.
  CHECK(std::filesystem::file_size(sentinel) == 7);
}

SCENARIO("MCP Image Export rechecks receipt ownership and late withdrawal",
    "[integration][mcp][image-export][image-export-receipt]")
{
  ImageAllocator allocator;
  allocator.remaining = 2;
  image::ExchangeLimits limits;
  limits.allocator = &allocator;
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, limits);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 16, 16);
  image->setName("tracked-image");
  const std::vector<unsigned char> pixels(1024, 17);
  image->setData(static_cast<const void *>(pixels.data()));
  const auto s = f.attach();
  const auto handle =
      f.inspectArray(s, 1).result()->get("items")->a.at(0).get("handle")->s;
  const auto path = f.exchangePath();
  f.exportImage(s, 2, handle);
  REQUIRE(f.integration.service() > 0);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  // The host allocator observes actual output-buffer release: the worker has
  // closed/validated the file, but no owning-thread receipt has been serviced.
  while (!allocator.deallocations.load()) {
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(std::distance(std::filesystem::directory_iterator(path),
              std::filesystem::directory_iterator{})
      == 1);
  const auto file = std::filesystem::directory_iterator(path)->path();
  std::string outcome = "invalid_request";
  SECTION("file changed before receipt")
  {
    std::ofstream(file, std::ios::binary | std::ios::trunc) << "changed";
  }
  SECTION("directory replaced before receipt")
  {
    REQUIRE(::rename(path.c_str(), (path + "-old").c_str()) == 0);
    REQUIRE(::mkdir(path.c_str(), 0700) == 0);
  }
  SECTION("cancel before receipt")
  {
    f.tool(s, "vsr_cancel", "{\"operation_ref\":" + ref(s.id, 2) + "}");
    outcome = "cancelled";
  }
  SECTION("READ withdrawn before receipt")
  {
    f.authority->restrict(
        session::Target::scene(f.sceneId), session::Capability::READ);
    outcome = "forbidden";
  }
  SECTION("deadline before receipt")
  {
    f.now += 31.;
    outcome = "timeout";
  }
  const auto result = f.await(s, 2);
  CHECK(result.str("outcome") == outcome);
  CHECK_FALSE(result.flag("committed"));
  const auto cleanupDeadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (allocator.deallocations.load() < 2) {
    f.drain();
    REQUIRE(std::chrono::steady_clock::now() < cleanupDeadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK_FALSE(std::filesystem::exists(file));
}

SCENARIO("MCP Image Export bounds collision retries without overwriting files",
    "[integration][mcp][image-export]")
{
  Fixture f({}, true, false, false, 120., true);
  auto image = f.scene.createArray(ANARI_UFIXED8, 1, 1);
  image->setName("tracked-image");
  const auto s = f.attach();
  const auto handle =
      f.inspectArray(s, 1).result()->get("items")->a.at(0).get("handle")->s;
  struct EntropyScope
  {
    EntropyScope()
    {
      fixedExportEntropy = true;
    }
    ~EntropyScope()
    {
      fixedExportEntropy = false;
    }
  } entropy;
  f.exportImage(s, 2, handle);
  auto first = f.await(s, 2);
  REQUIRE(first.str("outcome") == "ok");
  const auto path =
      f.exchangePath() + "/" + first.result()->get("file_name")->s;
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << "agent-owned-replacement";
  file.close();
  f.exportImage(s, 3, handle);
  auto second = f.await(s, 3);
  CHECK(second.str("outcome") == "busy");
  CHECK(second.result()->get("reason")->s == "resource_limit");
  std::ifstream retained(path);
  std::string content;
  retained >> content;
  CHECK(content == "agent-owned-replacement");
  json::Value metadata;
  REQUIRE(json::parse(f.images->metadata(f.sceneId), metadata));
  CHECK(metadata.get("exchange_limits")->get("files_used")->n == 1);
}

SCENARIO("MCP Image Export binding result and work caps do not fail capture",
    "[integration][mcp][image-export]")
{
  Fixture f({}, true, false, false, 120., true);
  auto image = f.scene.createArray(ANARI_UFIXED8, 1, 1);
  image->setName("tracked-image");
  SECTION("combined result cap")
  {
    for (int i = 0; i < 300; ++i)
      f.scene.createObject<vsr::scene::Sampler>("image2D")->setParameterObject(
          "image", *image);
  }
  SECTION("reference work cap")
  {
    auto material = f.scene.createObject<vsr::scene::Material>("matte");
    for (int i = 0; i < 4200; ++i)
      material->setParameter(("extra" + std::to_string(i)).c_str(), 1.f);
  }
  const auto s = f.attach();
  const auto handle =
      f.inspectArray(s, 1).result()->get("items")->a.at(0).get("handle")->s;
  f.exportImage(s, 2, handle);
  auto result = f.await(s, 2);
  REQUIRE(result.str("outcome") == "ok");
  CHECK_FALSE(result.result()->get("affected_complete")->b);
  CHECK(result.result()->get("affected_samplers")->a.size()
          + result.result()->get("affected_users")->a.size()
      <= 256);
  CHECK(result.result()->get("exchange_limits")->get("usage_complete")->b);
}

SCENARIO(
    "MCP Image Export reserves shared host memory until physical buffers are released",
    "[integration][mcp][image-export][image-export-memory]")
{
  auto shared = std::make_shared<vsr::rendering::FrameBudget>(4u << 20);
  image::ExchangeLimits limits;
  limits.physicalByteCap = shared->limit();
  limits.reservePhysicalMemory = [shared](std::size_t bytes) {
    return shared->reserve(bytes);
  };
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, limits);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
  image->setName("tracked-image");
  const auto s = f.attach();
  const auto handle =
      f.inspectArray(s, 1).result()->get("items")->a.at(0).get("handle")->s;
  const std::vector<unsigned char> pixels(262144, 29);
  image->setData(static_cast<const void *>(pixels.data()));
  const auto baseline = shared->used();
  auto occupied = shared->reserve(2u << 20); // A different host consumer.
  REQUIRE(occupied);
  f.exportImage(s, 2, handle);
  const auto rejected = f.await(s, 2);
  REQUIRE(rejected.str("outcome") == "busy");
  CHECK(rejected.result()->get("reason")->s == "resource_limit");
  occupied.reset();
  CHECK(shared->used() == baseline);
  f.exportImage(s, 3, handle);
  REQUIRE(f.integration.service() > 0);
  CHECK(shared->used() > baseline + 2950655);
  const auto exported = f.await(s, 3);
  REQUIRE(exported.str("outcome") == "ok");
  CHECK(exported.result()
            ->get("exchange_limits")
            ->get("transient_image_byte_cap")
            ->n
      == 4194304);
  CHECK(shared->used() == baseline);
  f.exportImage(s, 4, handle);
  REQUIRE(f.integration.service() > 0);
  f.tool(s, "vsr_cancel", "{\"operation_ref\":" + ref(s.id, 4) + "}");
  f.await(s, 4);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (shared->used() != baseline) {
    f.drain();
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(shared->used() == baseline);
}

SCENARIO("MCP Image Export allocation failure releases admission capacity",
    "[integration][mcp][image-export]")
{
  ImageAllocator allocator;
  SECTION("capture allocation fails")
  {
    allocator.remaining = 0;
  }
  SECTION("encoded output allocation fails")
  {
    allocator.remaining = 1;
  }
  image::ExchangeLimits limits;
  limits.allocator = &allocator;
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, limits);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  image->setName("tracked-image");
  const auto s = f.attach();
  const auto handle =
      f.inspectArray(s, 1).result()->get("items")->a.at(0).get("handle")->s;
  REQUIRE(f.exportImage(s, 2, handle).str("state") == "pending");
  auto rejected = f.await(s, 2);
  CHECK(rejected.str("outcome") == "busy");
  REQUIRE(rejected.result());
  REQUIRE(rejected.result()->get("reason"));
  CHECK(rejected.result()->get("reason")->s == "resource_limit");
  allocator.remaining = 2;
  f.exportImage(s, 3, handle);
  auto result = f.await(s, 3);
  CHECK(result.str("outcome") == "ok");
}

SCENARIO("MCP Image Export cancellation cleans an active immutable capture",
    "[integration][mcp][image-export][image-export-cancel]")
{
  Fixture f({}, true, false, false, 120., true);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 2048, 2048);
  image->setName("tracked-image");
  const auto s = f.attach();
  const auto handle =
      f.inspectArray(s, 1).result()->get("items")->a.at(0).get("handle")->s;
  f.exportImage(s, 2, handle);
  REQUIRE(f.integration.service() > 0);
  REQUIRE(f.tool(s, "vsr_cancel", "{\"operation_ref\":" + ref(s.id, 2) + "}")
          .errorCode()
          .empty());
  f.drain();
  CHECK(f.status(s, s.id, 2).str("outcome") == "cancelled");
  f.exportImage(s, 3, handle);
  auto result = f.await(s, 3);
  REQUIRE(result.str("outcome") == "ok");
  CHECK(result.result()->get("exchange_limits")->get("files_used")->n == 1);
}

SCENARIO("MCP Image Export removes partial files after disk failure",
    "[integration][mcp][image-export]")
{
  Fixture f({}, true, false, false, 120., true);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 32, 32);
  image->setName("tracked-image");
  const auto s = f.attach();
  const auto handle =
      f.inspectArray(s, 1).result()->get("items")->a.at(0).get("handle")->s;
  struct FileLimit
  {
    struct rlimit previous{};
    decltype(std::signal(SIGXFSZ, SIG_IGN)) previousSignal{};
    FileLimit()
    {
      REQUIRE(::getrlimit(RLIMIT_FSIZE, &previous) == 0);
      previousSignal = std::signal(SIGXFSZ, SIG_IGN);
      struct rlimit limit = previous;
      limit.rlim_cur = 100;
      REQUIRE(::setrlimit(RLIMIT_FSIZE, &limit) == 0);
    }
    ~FileLimit()
    {
      ::setrlimit(RLIMIT_FSIZE, &previous);
      std::signal(SIGXFSZ, previousSignal);
    }
  };
  const auto path = f.exchangePath();
  {
    FileLimit limit;
    f.exportImage(s, 2, handle);
    auto result = f.await(s, 2);
    CHECK(result.str("outcome") == "invalid_request");
    CHECK(result.result()->get("reason")->s == "file_rejected");
    CHECK(std::filesystem::is_empty(path));
  }
  f.exportImage(s, 3, handle);
  auto result = f.await(s, 3);
  REQUIRE(result.str("outcome") == "ok");
  CHECK_FALSE(std::filesystem::is_empty(path));
  REQUIRE(f.rpc("vsr/detach", "{}", &s).status == 200);
  auto later = f.attach();
  auto historical = f.status(later, s.id, 3);
  CHECK(historical.str("kind") == "image_export");
  CHECK(historical.result()->get("file_name")->s
      == result.result()->get("file_name")->s);
  f.images.reset();
  CHECK_FALSE(std::filesystem::exists(path));
}

SCENARIO(
    "MCP Image Exchange construction fails closed without adopting old files",
    "[integration][mcp][image-export][image-export-runtime]")
{
  Fixture f({}, true, false, false, 120., true);
  auto image = f.scene.createArray(ANARI_UFIXED8, 1, 1);
  image->setName("tracked-image");
  const auto s = f.attach();
  const auto initial = f.inspectArray(s, 1);
  const auto version = initial.result()
                           ->get("items")
                           ->a.at(0)
                           .get("image_info")
                           ->get("data_version")
                           ->s;
  const auto path = f.exchangePath();
  std::ofstream(path + "/agent.png") << "retained";
  SECTION("existing launch must not be adopted")
  {
    image::ImageExchange duplicate(
        f.integration, *f.engine, f.authority->launchId(), f.dir);
    json::Value metadata;
    REQUIRE(json::parse(duplicate.metadata(f.sceneId), metadata));
    CHECK_FALSE(metadata.get("enabled")->b);
    CHECK(metadata.get("reason")->s == "unsafe_directory");
  }
  SECTION("invalid runtime does not expose tracking as exchange availability")
  {
    f.images.reset();
    descriptor::RuntimeDirectory invalid;
    f.images = std::make_unique<image::ImageExchange>(
        f.integration, *f.engine, f.authority->launchId(), invalid);
    f.images->attachScene(f.sceneId, *f.sceneIds);
    const auto unavailable = f.inspectArray(s, 2);
    const auto &info =
        *unavailable.result()->get("items")->a.at(0).get("image_info");
    CHECK(info.get("state")->s == "unavailable");
    CHECK(info.get("reason")->s == "unsafe_directory");
    CHECK_FALSE(info.get("data_version"));
    const unsigned char changed[] = {87};
    image->setData(static_cast<const void *>(changed));
    f.images.reset();
    f.images = std::make_unique<image::ImageExchange>(
        f.integration, *f.engine, f.authority->launchId(), f.dir);
    f.images->attachScene(f.sceneId, *f.sceneIds);
    const auto available = f.inspectArray(s, 3);
    const auto &current =
        *available.result()->get("items")->a.at(0).get("image_info");
    CHECK(current.get("state")->s == "available");
    CHECK(current.get("data_version")->s != version);
    return;
  }
  SECTION("held runtime renamed before construction")
  {
    const auto oldPath = f.tmp.path + "-held";
    REQUIRE(::rename(f.tmp.path.c_str(), oldPath.c_str()) == 0);
    REQUIRE(::mkdir(f.tmp.path.c_str(), 0700) == 0);
    {
      image::ImageExchange stale(
          f.integration, *f.engine, f.authority->launchId(), f.dir);
      json::Value metadata;
      REQUIRE(json::parse(stale.metadata(f.sceneId), metadata));
      CHECK_FALSE(metadata.get("enabled")->b);
      CHECK(metadata.get("reason")->s == "unsafe_directory");
      CHECK(std::filesystem::is_empty(f.tmp.path));
    }
    REQUIRE(::rmdir(f.tmp.path.c_str()) == 0);
    REQUIRE(::rename(oldPath.c_str(), f.tmp.path.c_str()) == 0);
  }
  CHECK(std::filesystem::file_size(path + "/agent.png") == 8);
  CHECK(f.inspectArray(s, 4)
            .result()
            ->get("items")
            ->a.at(0)
            .get("image_info")
            ->get("data_version")
            ->s
      == version);
}

SCENARIO("MCP Image Exchange construction races never redirect cleanup",
    "[integration][mcp][image-export][image-export-construction]")
{
  Fixture f({}, true, false, false, 120., true);
  const auto path = f.exchangePath();
  const auto launch = std::filesystem::path(path).parent_path();
  f.images.reset();
  TempDir outside;
  std::ofstream(outside.path + "/keep.png") << "outside";
  struct HookScope
  {
    ~HookScope()
    {
      failImageDirectoryOpen = false;
      failLaunchDirectoryOpen = false;
      interceptImageDirectoryCreate = false;
      afterImageDirectoryCreate = {};
    }
  } hook;
  bool raced = false;
  SECTION("directory open fails after creation")
  {
    failImageDirectoryOpen = true;
  }
  SECTION("launch open fails after creation")
  {
    failLaunchDirectoryOpen = true;
  }
  SECTION("launch replaced while creating images")
  {
    raced = true;
    afterImageDirectoryCreate = [&] {
      ::rename(launch.c_str(), (launch.string() + "-held").c_str());
      ::symlink(outside.path.c_str(), launch.c_str());
    };
    interceptImageDirectoryCreate = true;
  }
  {
    image::ImageExchange unavailable(
        f.integration, *f.engine, f.authority->launchId(), f.dir);
    json::Value metadata;
    REQUIRE(json::parse(unavailable.metadata(f.sceneId), metadata));
    CHECK_FALSE(metadata.get("enabled")->b);
    CHECK(metadata.get("reason")->s == "unsafe_directory");
  }
  if (!raced)
    CHECK_FALSE(std::filesystem::exists(launch));
  else
    CHECK(std::filesystem::is_symlink(launch));
  CHECK(std::filesystem::file_size(outside.path + "/keep.png") == 7);
}

SCENARIO("MCP Image Exchange normal teardown drains active backend jobs",
    "[integration][mcp][image-export][image-export-teardown]")
{
  ImageAllocator allocator;
  allocator.remaining = 2;
  image::ExchangeLimits limits;
  limits.allocator = &allocator;
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, limits);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 2048, 2048);
  image->setName("tracked-image");
  const auto s = f.attach();
  const auto handle =
      f.inspectArray(s, 1).result()->get("items")->a.at(0).get("handle")->s;
  const auto path = f.exchangePath();
  f.exportImage(s, 2, handle);
  REQUIRE(f.integration.service() > 0);
  const auto started = std::chrono::steady_clock::now();
  f.server.reset();
  f.images.reset();
  f.engine.reset();
  CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(5));
  CHECK_FALSE(std::filesystem::exists(path));
  CHECK(allocator.deallocations.load() >= 1);
}

SCENARIO(
    "MCP Image Export normal exit removes nested agent files without following links",
    "[integration][mcp][image-export][image-export-cleanup]")
{
  Fixture f({}, true, false, false, 120., true);
  const auto path = f.exchangePath();
  TempDir outside;
  std::ofstream(outside.path + "/keep.png") << "outside";
  REQUIRE(::mkdir((path + "/agent").c_str(), 0700) == 0);
  REQUIRE(::mkdir((path + "/agent/nested").c_str(), 0700) == 0);
  std::ofstream(path + "/agent/nested/work.png") << "work";
  REQUIRE(
      ::symlink(outside.path.c_str(), (path + "/agent/external").c_str()) == 0);
  f.images.reset();
  CHECK_FALSE(std::filesystem::exists(path));
  CHECK_FALSE(
      std::filesystem::exists(std::filesystem::path(path).parent_path()));
  CHECK(std::filesystem::exists(outside.path + "/keep.png"));
}

SCENARIO("MCP Image Export checks exact entropy bounds and strict schema",
    "[integration][mcp][image-export]")
{
  image::ExchangeLimits limits;
  SECTION("one byte below the conservative peak")
  {
    limits.transientBytes = 2950655;
  }
  SECTION("exact conservative peak")
  {
    limits.transientBytes = 2950656;
  }
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, limits);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
  image->setName("tracked-image");
  std::vector<unsigned char> pixels(262144);
  std::uint32_t entropy = 0x12345678;
  for (auto &sample : pixels) {
    entropy ^= entropy << 13;
    entropy ^= entropy >> 17;
    entropy ^= entropy << 5;
    sample = static_cast<unsigned char>(entropy);
  }
  image->setData(static_cast<const void *>(pixels.data()));
  const auto s = f.attach();
  const auto handle =
      f.inspectArray(s, 1).result()->get("items")->a.at(0).get("handle")->s;
  const auto base = "{\"request_id\":2,\"scene_id\":"
      + std::to_string(f.sceneId) + ",\"handle\":\"" + handle + "\"";
  CHECK(
      f.tool(s, "vsr_image_export", base + ",\"file_name\":\"arbitrary.png\"}")
          .errorCode()
      == "invalid_request");
  auto tools = f.rpc("tools/list", "{}", &s);
  bool found = false;
  for (const auto &tool : tools.json.get("result")->get("tools")->a) {
    CHECK(tool.get("name")->s != "vsr_image_import");
    if (tool.get("name")->s == "vsr_image_export") {
      found = true;
      CHECK_FALSE(tool.get("inputSchema")->get("additionalProperties")->b);
      CHECK(tool.get("inputSchema")->get("required")->a.size() == 3);
    }
  }
  REQUIRE(found);
  f.exportImage(s, 2, handle);
  auto result = f.await(s, 2);
  if (limits.transientBytes == 2950655) {
    CHECK(result.str("outcome") == "busy");
    CHECK(result.result()->get("reason")->s == "resource_limit");
    return;
  }
  REQUIRE(result.str("outcome") == "ok");
  CHECK(result.result()
            ->get("exchange_limits")
            ->get("transient_image_byte_cap")
            ->n
      == 2950656);
  std::ifstream input(
      f.exchangePath() + "/" + result.result()->get("file_name")->s,
      std::ios::binary);
  std::vector<unsigned char> png((std::istreambuf_iterator<char>(input)), {});
  CHECK(png.size() == 262488);
  int width{}, height{}, channels{};
  auto *decoded = stbi_load_from_memory(
      png.data(), int(png.size()), &width, &height, &channels, 0);
  REQUIRE(decoded);
  CHECK(std::memcmp(decoded, pixels.data(), pixels.size()) == 0);
  stbi_image_free(decoded);
}

SCENARIO(
    "Image Import stays unexposed without physical and retention callbacks",
    "[integration][mcp][image-import][image-import-qualification]")
{
  Fixture f({}, true, false, false, 120., true);
  f.images.reset();
  f.images = std::make_unique<image::ImageExchange>(
      f.integration, *f.engine, f.authority->launchId(), f.dir);
  f.images->attachScene(f.sceneId, *f.sceneIds);
  f.images->attachRestoration(*f.edits, *f.restores);
  CHECK_FALSE(f.images->importAvailable());
  json::Value metadata;
  REQUIRE(json::parse(f.images->metadata(f.sceneId), metadata));
  CHECK_FALSE(metadata.get("import")->b);
  std::vector<mcp::Tool> tools;
  mcp::tools::addImageTools(tools, {f.ids, *f.images});
  CHECK(std::none_of(tools.begin(), tools.end(), [](const auto &tool) {
    return tool.name == "vsr_image_import" || tool.name == "vsr_mesh_import";
  }));
  CHECK_FALSE(metadata.get("mesh_import")->b);
}

SCENARIO("MCP Image Import replaces shared samples and restores once",
    "[integration][mcp][image-import]")
{
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 2, 2);
  image->setName("tracked-image");
  const unsigned char before[] = {
      17, 29, 41, 0, 3, 5, 7, 255, 101, 103, 107, 127, 211, 223, 227, 1};
  const unsigned char after[] = {
      29, 17, 41, 0, 5, 3, 7, 255, 103, 101, 107, 127, 223, 211, 227, 1};
  image->setData(static_cast<const void *>(after));
  const auto s = f.attach();
  const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
  const auto handle = item.get("handle")->s;
  f.exportImage(s, 2, handle);
  const auto exported = f.await(s, 2);
  REQUIRE(exported.str("outcome") == "ok");
  const auto file = exported.result()->get("file_name")->s;
  image->setData(static_cast<const void *>(before));
  const auto version = f.inspectArray(s, 3)
                           .result()
                           ->get("items")
                           ->a.at(0)
                           .get("image_info")
                           ->get("data_version")
                           ->s;
  REQUIRE(
      f.importImage(s, 4, handle, version, file).str("kind") == "image_import");
  const auto imported = f.await(s, 4);
  REQUIRE(imported.str("outcome") == "ok");
  REQUIRE(imported.flag("committed"));
  REQUIRE(imported.result()->get("handle"));
  CHECK(imported.result()->get("handle")->s == handle);
  REQUIRE(imported.result()->get("data_version"));
  REQUIRE(imported.result()->get("scene_revision"));
  REQUIRE(imported.result()->get("affected_samplers"));
  REQUIRE(imported.result()->get("affected_users"));
  REQUIRE(imported.result()->get("affected_complete"));
  const auto *policy = imported.result()->get("restoration_policy");
  REQUIRE(policy);
  REQUIRE(policy->get("mode"));
  CHECK_FALSE(policy->get("session_snapshot_ceiling_bytes"));
  REQUIRE(policy->get("effective_global_limit_bytes"));
  REQUIRE(policy->get("eviction_possible"));
  CHECK(policy->get("mode")->s == "publication_fifo");
  CHECK(policy->get("effective_global_limit_bytes")->n == 4294967296.);
  CHECK(policy->get("eviction_possible")->b);
  CHECK_FALSE(imported.json.get("result")->get("isError")->b);
  bool foundImportActivity = false;
  for (const auto &record : f.imageActivity->snapshot())
    if (record.entry.ref && record.entry.ref->requestId == 4) {
      foundImportActivity = true;
      CHECK(std::string(activity::operationKindName(record.entry.kind))
          == "image_import");
      CHECK(record.entry.eligibility == activity::Eligibility::ELIGIBLE);
      REQUIRE(record.entry.batchRef);
    }
  CHECK(foundImportActivity);
  json::Value capacity;
  REQUIRE(json::parse(f.images->metadata(f.sceneId, s.id), capacity));
  CHECK(
      capacity.get("exchange_limits")->get("session_retained_physical_bytes")->n
      == 16);
  CHECK(std::memcmp(image->data(), after, sizeof(after)) == 0);
  REQUIRE(imported.result()->get("batch_ref"));
  const auto batch = *imported.result()->get("batch_ref");
  REQUIRE(::unlink((f.exchangePath() + "/" + file).c_str()) == 0);
  const auto replayed = f.importImage(s, 4, handle, version, file);
  CHECK(replayed.flag("replayed"));
  CHECK(json::write(*replayed.result()) == json::write(*imported.result()));
  f.restoreBatch(s, 5, batch);
  const auto restored = f.await(s, 5);
  REQUIRE(restored.str("outcome") == "ok");
  CHECK(restored.flag("committed"));
  REQUIRE(json::parse(f.images->metadata(f.sceneId, s.id), capacity));
  CHECK(
      capacity.get("exchange_limits")->get("session_retained_physical_bytes")->n
      == 0);
  CHECK(std::memcmp(image->data(), before, sizeof(before)) == 0);
  CHECK(restored.result()->get("versions")->a.empty());
  CHECK(restored.result()->get("node_versions")->a.empty());
  REQUIRE(restored.result()->get("image_versions")->a.size() == 1);
  const auto &imageVersion = restored.result()->get("image_versions")->a[0];
  REQUIRE(imageVersion.get("handle"));
  REQUIRE(imageVersion.get("data_version"));
  CHECK(imageVersion.get("handle")->s == handle);
  CHECK(imageVersion.get("data_version")->s
      != imported.result()->get("data_version")->s);
  CHECK_FALSE(restored.result()->get("matrix_versions"));
  CHECK(restored.result()->get("restored")->b);
  CHECK_FALSE(restored.json.get("result")->get("isError")->b);
  const auto observed = f.inspectArray(s, 6);
  CHECK(observed.result()
            ->get("items")
            ->a.at(0)
            .get("image_info")
            ->get("data_version")
            ->s
      == imageVersion.get("data_version")->s);
  f.restoreBatch(s, 7, batch);
  CHECK(f.await(s, 7).str("outcome") == "already_restored");
}

namespace {
void pngNumber(std::vector<unsigned char> &bytes, std::uint32_t value)
{
  for (int i = 3; i >= 0; --i)
    bytes.push_back(static_cast<unsigned char>(value >> (8 * i)));
}
void pngChunk(std::vector<unsigned char> &bytes,
    const std::string &type,
    const std::vector<unsigned char> &payload)
{
  pngNumber(bytes, std::uint32_t(payload.size()));
  const auto start = bytes.size();
  bytes.insert(bytes.end(), type.begin(), type.end());
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  pngNumber(bytes,
      std::uint32_t(
          crc32(0, bytes.data() + start, uInt(bytes.size() - start))));
}
std::vector<unsigned char> inputPng(std::size_t width,
    std::size_t height,
    unsigned char colorType,
    const std::vector<unsigned char> &filtered,
    const std::vector<std::pair<std::string, std::vector<unsigned char>>>
        &chunks = {})
{
  std::vector<unsigned char> bytes{137, 80, 78, 71, 13, 10, 26, 10};
  std::vector<unsigned char> header;
  pngNumber(header, std::uint32_t(width));
  pngNumber(header, std::uint32_t(height));
  header.insert(header.end(), {8, colorType, 0, 0, 0});
  pngChunk(bytes, "IHDR", header);
  for (const auto &chunk : chunks)
    pngChunk(bytes, chunk.first, chunk.second);
  std::vector<unsigned char> compressed(compressBound(filtered.size()));
  uLongf length = compressed.size();
  REQUIRE(
      compress2(compressed.data(), &length, filtered.data(), filtered.size(), 9)
      == Z_OK);
  compressed.resize(length);
  pngChunk(bytes, "IDAT", compressed);
  pngChunk(bytes, "IEND", {});
  return bytes;
}
void writeInput(
    const std::string &path, const std::vector<unsigned char> &bytes)
{
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  output.close();
  REQUIRE(output.good());
}
} // namespace

SCENARIO("MCP Image Import decodes every filter and bounded compressed rows",
    "[integration][mcp][image-import][image-import-decode]")
{
  SECTION("each filter has an independent literal sample oracle")
  {
    const std::vector<unsigned char> filtered[] = {
        {0, 10, 20, 30, 0, 40, 50, 60},
        {1, 10, 10, 10, 1, 40, 10, 10},
        {2, 10, 20, 30, 2, 30, 30, 30},
        {3, 10, 15, 20, 3, 35, 20, 20},
        {4, 10, 10, 10, 4, 30, 10, 10}};
    for (const auto &rows : filtered) {
      Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
      auto image = f.scene.createArray(ANARI_UFIXED8, 3, 2);
      image->setName("tracked-image");
      const unsigned char before[] = {1, 2, 3, 4, 5, 6};
      const unsigned char expected[] = {10, 20, 30, 40, 50, 60};
      image->setData(static_cast<const void *>(before));
      const auto s = f.attach();
      const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
      writeInput(f.exchangePath() + "/agent.png", inputPng(3, 2, 0, rows));
      f.importImage(s,
          2,
          item.get("handle")->s,
          item.get("image_info")->get("data_version")->s,
          "agent.png");
      const auto receipt = f.await(s, 2);
      REQUIRE(receipt.str("outcome") == "ok");
      CHECK(std::memcmp(image->data(), expected, sizeof(expected)) == 0);
    }
  }
  SECTION("high compression must drain buffered inflate output")
  {
    Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
    auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
    image->setName("tracked-image");
    std::vector<unsigned char> before(262144, 9);
    image->setData(static_cast<const void *>(before.data()));
    const auto s = f.attach();
    const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
    std::vector<unsigned char> rows(262400, 0);
    writeInput(f.exchangePath() + "/agent.png", inputPng(256, 256, 6, rows));
    f.importImage(s,
        2,
        item.get("handle")->s,
        item.get("image_info")->get("data_version")->s,
        "agent.png");
    const auto receipt = f.await(s, 2);
    REQUIRE(receipt.str("outcome") == "ok");
    const auto *samples = static_cast<const unsigned char *>(image->data());
    CHECK(
        std::all_of(samples, samples + 262144, [](auto c) { return c == 0; }));
  }
}

SCENARIO("MCP Image Import validates split streams and colour chunk order",
    "[integration][mcp][image-import][image-import-structure]")
{
  const auto base = inputPng(1, 1, 6, {0, 29, 17, 41, 0});
  const std::vector<unsigned char> compressed(
      base.begin() + 41, base.end() - 16);
  using Chunks =
      std::vector<std::pair<std::string, std::vector<unsigned char>>>;
  std::vector<std::pair<std::string, Chunks>> forms;
  Chunks split{{"IDAT", {}}};
  for (const auto byte : compressed) {
    split.push_back({"IDAT", {byte}});
    split.push_back({"IDAT", {}});
  }
  forms.push_back({"valid split IDAT", split});
  for (const auto *type : {"gAMA", "cHRM", "sRGB", "iCCP"}) {
    forms.push_back({std::string(type) + " after IDAT",
        {{"IDAT", compressed}, {type, {}}}});
    forms.push_back({std::string(type) + " after palette",
        {{"PLTE", {1, 2, 3}}, {type, {}}, {"IDAT", compressed}}});
  }
  auto corrupt = compressed;
  corrupt.back() ^= 1;
  forms.push_back({"Adler mismatch with valid CRC", {{"IDAT", corrupt}}});
  corrupt = compressed;
  corrupt.pop_back();
  forms.push_back({"truncated trailer", {{"IDAT", corrupt}}});
  corrupt = compressed;
  corrupt.insert(corrupt.end(), compressed.begin(), compressed.end());
  forms.push_back({"concatenated streams", {{"IDAT", corrupt}}});
  corrupt = compressed;
  corrupt[0] = 0x78;
  corrupt[1] = 0x20; // Valid header checksum, preset dictionary required.
  forms.push_back({"preset dictionary", {{"IDAT", corrupt}}});
  for (const auto &form : forms) {
    INFO(form.first);
    std::vector<unsigned char> bytes(base.begin(), base.begin() + 33);
    for (const auto &chunk : form.second)
      pngChunk(bytes, chunk.first, chunk.second);
    pngChunk(bytes, "IEND", {});
    Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
    auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
    image->setName("tracked-image");
    const unsigned char before[] = {17, 29, 41, 0};
    image->setData(static_cast<const void *>(before));
    const auto s = f.attach();
    const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
    writeInput(f.exchangePath() + "/agent.png", bytes);
    f.importImage(s,
        2,
        item.get("handle")->s,
        item.get("image_info")->get("data_version")->s,
        "agent.png");
    const auto result = f.await(s, 2);
    const bool accepted = form.first == "valid split IDAT";
    CHECK(result.str("outcome") == (accepted ? "ok" : "invalid_request"));
    CHECK(result.flag("committed") == accepted);
    if (!accepted) {
      const auto *reason = result.result()->get("reason");
      CHECK((reason && reason->s == "invalid_png"));
      CHECK(std::memcmp(image->data(), before, sizeof(before)) == 0);
      CHECK(image->dataWriteSequence() == 1);
      CHECK(f.edits->restorations().size() == 0);
    } else {
      const unsigned char after[] = {29, 17, 41, 0};
      CHECK(std::memcmp(image->data(), after, sizeof(after)) == 0);
    }
  }
}

SCENARIO("MCP Image Import applies individual encoded and ancillary ceilings",
    "[integration][mcp][image-import][image-import-limits]")
{
  for (int form = 0; form < 4; ++form) {
    INFO(form);
    Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
    const std::size_t width = form < 2 ? 1 : 64;
    auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, width, width);
    image->setName("tracked-image");
    std::vector<unsigned char> before(width * width * 4, 71);
    image->setData(static_cast<const void *>(before.data()));
    const auto s = f.attach();
    const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
    const std::vector<unsigned char> rows(before.size() + width, 0);
    const auto plain = inputPng(width, width, 6, rows);
    // For 1x1 RGBA, F=5 and the independent encoded ceiling is 65546.
    // The larger target separately tests exact 65536 ancillary payload bytes.
    const auto ancillary = form < 2 ? 65546 - plain.size() - 12 + form
                                    : std::size_t(65536 + form - 2);
    const auto png = inputPng(width,
        width,
        6,
        rows,
        {{"teXt", std::vector<unsigned char>(ancillary, 0)}});
    if (form < 2)
      REQUIRE(png.size() == std::size_t(65546 + form));
    writeInput(f.exchangePath() + "/agent.png", png);
    if (form == 0) {
      // Export quota pressure must not prevent a bounded individual Import.
      const auto oversized = f.exchangePath() + "/quota.png";
      const auto fd = ::open(oversized.c_str(), O_CREAT | O_WRONLY, 0600);
      REQUIRE(fd >= 0);
      REQUIRE(::ftruncate(fd, 268435457) == 0);
      REQUIRE(::close(fd) == 0);
    }
    f.importImage(s,
        2,
        item.get("handle")->s,
        item.get("image_info")->get("data_version")->s,
        "agent.png");
    const auto result = f.await(s, 2);
    const bool accepted = form == 0 || form == 2;
    CHECK(result.str("outcome") == (accepted ? "ok" : "invalid_request"));
    CHECK(result.flag("committed") == accepted);
    if (accepted) {
      const auto *bytes = static_cast<const unsigned char *>(image->data());
      CHECK(std::all_of(
          bytes, bytes + before.size(), [](auto v) { return v == 0; }));
      f.restoreBatch(s, 3, *result.result()->get("batch_ref"));
      REQUIRE(f.await(s, 3).str("outcome") == "ok");
    } else {
      CHECK(result.result()->get("reason")->s == "invalid_png");
      CHECK(image->dataWriteSequence() == 1);
    }
    CHECK(std::memcmp(image->data(), before.data(), before.size()) == 0);
  }
}

SCENARIO("MCP Image transient limits remain distinct from recovery policy",
    "[integration][mcp][image-import][image-import-limits]")
{
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
  // Above the old 64 MiB snapshot ceiling but within the default soft budget.
  // The unchanged transient-work safeguard, not session retention, wins.
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 16777217, 1);
  image->setName("tracked-image");
  const auto s = f.attach();
  const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
  f.importImage(s,
      2,
      item.get("handle")->s,
      item.get("image_info")->get("data_version")->s,
      "absent.png");
  const auto result = f.await(s, 2);
  CHECK(result.str("outcome") == "busy");
  CHECK(result.result()->get("reason")->s == "resource_limit");
  CHECK_FALSE(result.flag("committed"));
  CHECK_FALSE(image->isMapped());
  CHECK(image->dataWriteSequence() == 0);
  json::Value metadata;
  REQUIRE(json::parse(f.images->metadata(f.sceneId, s.id), metadata));
  const auto *limits = metadata.get("exchange_limits");
  CHECK(limits->get("effective_global_retention_byte_cap")->n == 4294967296.);
  CHECK(limits->get("session_retained_physical_bytes")->n == 0);
  CHECK(f.imageActivity->limits().entries == 1024);
  CHECK(f.imageActivity->limits().restorableBatches == 32);
  CHECK(activity::ENTRY_OVERHEAD == 256);
  CHECK(f.edits->restorations().capacity() == 32);
  CHECK(f.edits->restorations().size() == 0);
}

SCENARIO("MCP Image Import preserves raw channels and ignores colour profiles",
    "[integration][mcp][image-import][image-import-samples]")
{
  const anari::DataType types[] = {ANARI_UFIXED8,
      ANARI_UFIXED8_VEC2,
      ANARI_UFIXED8_VEC3,
      ANARI_UFIXED8_VEC4,
      ANARI_UFIXED8_R_SRGB,
      ANARI_UFIXED8_RA_SRGB,
      ANARI_UFIXED8_RGB_SRGB,
      ANARI_UFIXED8_RGBA_SRGB,
      ANARI_FIXED8,
      ANARI_FIXED8_VEC2,
      ANARI_FIXED8_VEC3,
      ANARI_FIXED8_VEC4};
  for (std::size_t i = 0; i < std::size(types); ++i) {
    Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
    const auto channels = i % 4 + 1;
    auto image = f.scene.createArray(types[i], 3, 2);
    image->setName("tracked-image");
    std::vector<unsigned char> samples(6 * channels);
    for (std::size_t n = 0; n < samples.size(); ++n)
      samples[n] = static_cast<unsigned char>(17 + 29 * n);
    if (channels == 2 || channels == 4)
      samples[channels - 1] = 0;
    std::vector<unsigned char> before(samples.size(), 211), rows;
    for (std::size_t row = 0; row < 2; ++row) {
      rows.push_back(0);
      rows.insert(rows.end(),
          samples.begin() + row * 3 * channels,
          samples.begin() + (row + 1) * 3 * channels);
    }
    image->setData(static_cast<const void *>(before.data()));
    const auto s = f.attach();
    const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
    const unsigned char colors[] = {0, 4, 2, 6};
    writeInput(f.exchangePath() + "/agent.png",
        inputPng(3,
            2,
            colors[channels - 1],
            rows,
            {{"gAMA", {0, 0, 0, 1}},
                {"sRGB", {0}},
                {"iCCP", {'x', 0, 0, 255, 255}},
                {"cHRM", std::vector<unsigned char>(32, 0)}}));
    f.importImage(s,
        2,
        item.get("handle")->s,
        item.get("image_info")->get("data_version")->s,
        "agent.png");
    auto imported = f.await(s, 2);
    REQUIRE(imported.str("outcome") == "ok");
    CHECK(std::memcmp(image->data(), samples.data(), samples.size()) == 0);
    f.restoreBatch(s, 3, *imported.result()->get("batch_ref"));
    REQUIRE(f.await(s, 3).str("outcome") == "ok");
    CHECK(std::memcmp(image->data(), before.data(), before.size()) == 0);
  }
}

SCENARIO(
    "MCP Image Import rejects malformed PNG without changing data or recovery",
    "[integration][mcp][image-import][image-import-invalid]")
{
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 3, 2);
  image->setName("tracked-image");
  const std::vector<unsigned char> before(24, 71), rows(26, 0);
  image->setData(static_cast<const void *>(before.data()));
  const auto s = f.attach();
  const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
  const auto handle = item.get("handle")->s;
  const auto version = item.get("image_info")->get("data_version")->s;
  for (int form = 0; form < 23; ++form) {
    INFO("rejected form " << form);
    auto png = inputPng(3, 2, 6, rows);
    if (form <= 8) {
      if (form == 0)
        png[24] = 16;
      if (form == 1)
        png[24] = 4;
      if (form == 2)
        png[25] = 3;
      if (form == 3)
        png[25] = 2;
      if (form == 4)
        png[26] = 1;
      if (form == 5)
        png[27] = 1;
      if (form == 6)
        png[28] = 1;
      if (form == 7)
        png[19] = 4;
      if (form == 8)
        png[23] = 3;
      std::vector<unsigned char> checksum;
      pngNumber(checksum, std::uint32_t(crc32(0, png.data() + 12, 17)));
      std::copy(checksum.begin(), checksum.end(), png.begin() + 29);
    } else if (form == 9)
      png[29] ^= 1;
    else if (form == 10)
      png.pop_back();
    else if (form == 11)
      png.push_back(0);
    else if (form == 12)
      png = inputPng(3, 2, 6, rows, {{"tRNS", {1, 2}}});
    else if (form == 13)
      png = inputPng(3, 2, 6, rows, {{"acTL", std::vector<unsigned char>(8)}});
    else if (form == 14)
      png = inputPng(3, 2, 6, rows, {{"fcTL", std::vector<unsigned char>(26)}});
    else if (form == 15)
      png = inputPng(3, 2, 6, rows, {{"fdAT", {0, 0, 0, 0}}});
    else if (form == 16)
      png = inputPng(3, 2, 6, rows, {{"ABCD", {1}}});
    else if (form == 17)
      png = inputPng(
          3, 2, 6, rows, {{"teXt", std::vector<unsigned char>(65537)}});
    else if (form == 18) {
      auto badRows = rows;
      badRows[0] = 5;
      png = inputPng(3, 2, 6, badRows);
    } else if (form == 19)
      png = inputPng(3, 2, 6, std::vector<unsigned char>(25));
    else if (form == 20)
      png = inputPng(3, 2, 6, std::vector<unsigned char>(27));
    else if (form == 21) {
      png.resize(8);
      pngChunk(png, "IEND", {});
    } else if (form == 22) {
      const std::vector<unsigned char> duplicate(
          png.begin() + 8, png.begin() + 33);
      png.insert(png.begin() + 33, duplicate.begin(), duplicate.end());
    }
    writeInput(f.exchangePath() + "/agent.png", png);
    f.importImage(s, 2 + form, handle, version, "agent.png");
    const auto rejected = f.await(s, 2 + form);
    REQUIRE(rejected.str("outcome") == "invalid_request");
    REQUIRE(rejected.result());
    CHECK(rejected.result()->get("reason")->s == "invalid_png");
    CHECK_FALSE(rejected.flag("committed"));
    CHECK(std::memcmp(image->data(), before.data(), before.size()) == 0);
    CHECK(f.sceneIds->imageDataVersion(f.sceneIds->handleOf(image.data()))
        == version);
    json::Value metadata;
    REQUIRE(json::parse(f.images->metadata(f.sceneId, s.id), metadata));
    CHECK(metadata.get("exchange_limits")
              ->get("session_retained_physical_bytes")
              ->n
        == 0);
    CHECK(f.edits->restorations().size() == 0);
  }
}

SCENARIO(
    "MCP Image Import retains one physical snapshot and unwinds late failures",
    "[integration][mcp][image-import][image-import-memory]")
{
  auto memory = std::make_shared<vsr::rendering::FrameBudget>(4u << 20);
  const auto baseline = memory->used();
  auto probe = memory->reserve(1);
  const auto leaseOverhead = memory->used() - baseline - 1;
  probe.reset();
  image::ExchangeLimits limits;
  limits.physicalByteCap = memory->limit();
  limits.reservePhysicalMemory = [memory](std::size_t bytes) {
    return memory->reserve(bytes);
  };
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, limits, true);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
  image->setName("tracked-image");
  const std::vector<unsigned char> before(262144, 71), rows(262400, 0);
  image->setData(static_cast<const void *>(before.data()));
  const auto s = f.attach();
  const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
  const auto handle = item.get("handle")->s;
  const auto version = item.get("image_info")->get("data_version")->s;
  writeInput(f.exchangePath() + "/agent.png", inputPng(256, 256, 6, rows));
  f.importImage(s, 2, handle, version, "agent.png");
  REQUIRE(f.integration.service() > 0);
  // One S lease; E/D/work and independently owned Job/store/Activity
  // controls partition W, rather than releasing retained controls with scratch.
  CHECK(memory->used() == baseline + 3212800 + 6 * leaseOverhead);
  std::size_t retained = 32768 + 65536 + 2 * leaseOverhead;
  SECTION("successful ownership transfer")
  {
    const auto committed = f.await(s, 2);
    REQUIRE(committed.str("outcome") == "ok");
    CHECK(memory->used()
        == baseline + 262144 + 32768 + 8192 + 65536 + 5 * leaseOverhead);
    f.restoreBatch(s, 3, *committed.result()->get("batch_ref"));
    REQUIRE(f.await(s, 3).str("outcome") == "ok");
    // Snapshot and scratch are gone; independent Engine, Activity, scope
    // history and RestoreResult owners remain covered until their eviction.
    retained = 32768 + 4096 + 5 * 65536 + 7 * leaseOverhead;
  }
  SECTION("late authority withdrawal")
  {
    f.authority->setAgentEditsDisabled(true);
    CHECK(f.await(s, 2).str("outcome") == "forbidden");
  }
  SECTION("late ordinary writer")
  {
    image->setData(static_cast<const void *>(before.data()));
    CHECK(f.await(s, 2).str("outcome") == "conflict");
  }
  SECTION("active cancellation")
  {
    f.tool(s, "vsr_cancel", "{\"operation_ref\":" + ref(s.id, 2) + "}");
    CHECK(f.await(s, 2).str("outcome") == "cancelled");
  }
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (memory->used() != baseline + retained) {
    f.drain();
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(memory->used() == baseline + retained);
}

SCENARIO("Image host teardown releases retained and active snapshot ownership",
    "[integration][mcp][image-import][image-import-teardown]")
{
  auto memory = std::make_shared<vsr::rendering::FrameBudget>(8u << 20);
  const auto baseline = memory->used();
  image::ExchangeLimits limits;
  limits.physicalByteCap = memory->limit();
  limits.reservePhysicalMemory = [memory](std::size_t bytes) {
    return memory->reserve(bytes);
  };
  std::string directory;
  const auto start = std::chrono::steady_clock::now();
  {
    Fixture f({}, true, false, false, 120., true, UINT64_MAX, limits, true);
    auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 256, 256);
    image->setName("tracked-image");
    const std::vector<unsigned char> before(262144, 71), rows(262400, 0);
    image->setData(static_cast<const void *>(before.data()));
    const auto s = f.attach();
    const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
    directory = f.exchangePath();
    writeInput(directory + "/agent.png", inputPng(256, 256, 6, rows));
    f.importImage(s,
        2,
        item.get("handle")->s,
        item.get("image_info")->get("data_version")->s,
        "agent.png");
    REQUIRE(f.integration.service() > 0);
    SECTION("retained after completed Import")
    {
      REQUIRE(f.await(s, 2).str("outcome") == "ok");
      CHECK(f.edits->restorations().size() == 1);
    }
    SECTION("active backend is joined before engine/store destruction")
    {
      CHECK(f.edits->restorations().size() == 0);
    }
    CHECK(memory->used() > baseline);
  }
  CHECK(memory->used() == baseline);
  CHECK_FALSE(std::filesystem::exists(directory));
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
}

SCENARIO("MCP Image Import refuses unsafe leaves and unstable input promptly",
    "[integration][mcp][image-import][image-import-files]")
{
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  image->setName("tracked-image");
  const unsigned char before[] = {17, 29, 41, 0};
  image->setData(static_cast<const void *>(before));
  const auto s = f.attach();
  const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
  const auto handle = item.get("handle")->s;
  const auto version = item.get("image_info")->get("data_version")->s;
  const auto path = f.exchangePath() + "/agent.png";
  writeInput(path, inputPng(1, 1, 6, {0, 29, 17, 41, 0}));
  SECTION("symbolic leaf")
  {
    REQUIRE(::rename(path.c_str(), (path + "-original").c_str()) == 0);
    REQUIRE(::symlink((path + "-original").c_str(), path.c_str()) == 0);
  }
  SECTION("hard link alias")
  {
    REQUIRE(::link(path.c_str(), (path + "-alias").c_str()) == 0);
  }
  SECTION("writerless FIFO")
  {
    REQUIRE(::unlink(path.c_str()) == 0);
    REQUIRE(::mkfifo(path.c_str(), 0600) == 0);
  }
  SECTION("directory")
  {
    REQUIRE(::unlink(path.c_str()) == 0);
    REQUIRE(::mkdir(path.c_str(), 0700) == 0);
  }
  SECTION("Unix socket")
  {
    REQUIRE(::unlink(path.c_str()) == 0);
    const auto fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    REQUIRE(fd >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    REQUIRE(path.size() < sizeof(address.sun_path));
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    REQUIRE(::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address))
        == 0);
    REQUIRE(::close(fd) == 0);
  }
  SECTION("hard link introduced after read")
  {
    afterImportRead = [path](int) {
      REQUIRE(::link(path.c_str(), (path + "-alias").c_str()) == 0);
    };
    interceptImportRead = true;
  }
  SECTION("unlinked input after read")
  {
    afterImportRead = [path](int) { REQUIRE(::unlink(path.c_str()) == 0); };
    interceptImportRead = true;
  }
  SECTION("changed size after read")
  {
    afterImportRead = [](int fd) {
      REQUIRE(
          ::truncate(("/proc/self/fd/" + std::to_string(fd)).c_str(), 0) == 0);
    };
    interceptImportRead = true;
  }
  SECTION("changed mode after read")
  {
    afterImportRead = [](int fd) { REQUIRE(::fchmod(fd, 0400) == 0); };
    interceptImportRead = true;
  }
  SECTION("replaced ancestry")
  {
    const auto directory = f.exchangePath();
    REQUIRE(::rename(directory.c_str(), (directory + "-retired").c_str()) == 0);
    REQUIRE(::mkdir(directory.c_str(), 0700) == 0);
  }
  const auto start = std::chrono::steady_clock::now();
  f.importImage(s, 2, handle, version, "agent.png");
  const auto rejected = f.await(s, 2);
  REQUIRE(rejected.str("outcome") == "invalid_request");
  CHECK(rejected.result()->get("reason")->s == "file_rejected");
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
  CHECK(std::memcmp(image->data(), before, sizeof(before)) == 0);
  CHECK_FALSE(image->isMapped());
  CHECK(f.edits->restorations().size() == 0);
  interceptImportRead = false;
  afterImportRead = {};
}

SCENARIO(
    "MCP Image Import and restore obey map version and authority precedence",
    "[integration][mcp][image-import][image-import-guards]")
{
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  image->setName("tracked-image");
  const unsigned char before[] = {17, 29, 41, 0};
  image->setData(static_cast<const void *>(before));
  const auto s = f.attach();
  const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
  const auto handle = item.get("handle")->s;
  const auto version = item.get("image_info")->get("data_version")->s;
  writeInput(
      f.exchangePath() + "/agent.png", inputPng(1, 1, 6, {0, 29, 17, 41, 0}));
  SECTION("map rejection precedes stale version and preserves the map")
  {
    REQUIRE(image->map());
    f.importImage(s, 2, handle, "stale", "agent.png");
    const auto rejected = f.await(s, 2);
    CHECK(rejected.str("outcome") == "busy");
    CHECK(rejected.result()->get("reason")->s == "image_mapped");
    CHECK(image->isMapped());
    image->unmap();
  }
  SECTION("two imports cannot commit one token")
  {
    f.importImage(s, 2, handle, version, "agent.png");
    f.importImage(s, 3, handle, version, "agent.png");
    const auto a = f.await(s, 2), b = f.await(s, 3);
    CHECK((a.str("outcome") == "ok") != (b.str("outcome") == "ok"));
    const auto &rejected = a.str("outcome") == "ok" ? b : a;
    CHECK(rejected.str("outcome") == "conflict");
    CHECK(rejected.result()->get("reason")->s == "stale_data_version");
  }
  SECTION("queued authority withdrawal wins over identity disclosure")
  {
    f.importImage(
        s, 2, "00000000000000000000000000000000", version, "agent.png");
    f.authority->setAgentEditsDisabled(true);
    CHECK(f.await(s, 2).str("outcome") == "forbidden");
    CHECK(std::memcmp(image->data(), before, sizeof(before)) == 0);
  }
}

SCENARIO("MCP Image restoration preserves prewrite authority and eligibility",
    "[integration][mcp][image-import][image-import-guards]")
{
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  image->setName("tracked-image");
  const unsigned char before[] = {17, 29, 41, 0};
  image->setData(static_cast<const void *>(before));
  const auto s = f.attach();
  const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
  const auto handle = item.get("handle")->s;
  const auto version = item.get("image_info")->get("data_version")->s;
  writeInput(
      f.exchangePath() + "/agent.png", inputPng(1, 1, 6, {0, 29, 17, 41, 0}));
  f.importImage(s, 2, handle, version, "agent.png");
  const auto imported = f.await(s, 2);
  REQUIRE(imported.str("outcome") == "ok");
  const auto batch = *imported.result()->get("batch_ref");
  SECTION("restore accepts no caller version or unknown batch fields")
  {
    const auto args = "{\"request_id\":3,\"scene_id\":"
        + std::to_string(f.sceneId) + ",\"target_batch_ref\":"
        + json::write(batch) + ",\"data_version\":\"caller-token\"}";
    CHECK(f.tool(s, "vsr_restore", args).errorCode() == "invalid_request");
    auto extra = batch;
    extra.o.emplace("data_version", json::Value{});
    const auto nested =
        "{\"request_id\":3,\"scene_id\":" + std::to_string(f.sceneId)
        + ",\"target_batch_ref\":" + json::write(extra) + "}";
    CHECK(f.tool(s, "vsr_restore", nested).errorCode() == "invalid_request");
    // Schema rejection burns no request ID and leaves the batch eligible.
    f.restoreBatch(s, 3, batch);
    CHECK(f.await(s, 3).str("outcome") == "ok");
  }
  SECTION("authority withdrawn during restore preparation prevents mapping")
  {
    const auto completed = image->dataWriteSequence();
    f.restores->setFaultInjector([&] {
      f.authority->setAgentEditsDisabled(true);
      return false;
    });
    f.restoreBatch(s, 3, batch);
    const auto rejected = f.await(s, 3);
    CHECK(rejected.str("outcome") == "forbidden");
    CHECK_FALSE(rejected.flag("committed"));
    CHECK(image->dataWriteSequence() == completed);
    CHECK_FALSE(image->isMapped());
    const unsigned char after[] = {29, 17, 41, 0};
    CHECK(std::memcmp(image->data(), after, sizeof(after)) == 0);
    CHECK(f.restores->eligible(
        {batch.get("launch_id")->s, batch.get("batch_id")->s}));
    f.restores->setFaultInjector({});
    f.authority->setAgentEditsDisabled(false);
    f.restoreBatch(s, 4, batch);
    CHECK(f.await(s, 4).str("outcome") == "ok");
  }
  SECTION("restore leaves an ordinary map open and eligible")
  {
    REQUIRE(image->map());
    f.restoreBatch(s, 3, batch);
    const auto rejected = f.await(s, 3);
    CHECK(rejected.str("outcome") == "busy");
    CHECK(rejected.result()->get("reason")->s == "image_mapped");
    CHECK(f.restores->eligible(
        {batch.get("launch_id")->s, batch.get("batch_id")->s}));
    image->unmap();
  }
  SECTION("later write including change back permanently conflicts")
  {
    const unsigned char after[] = {29, 17, 41, 0};
    image->setData(static_cast<const void *>(before));
    image->setData(static_cast<const void *>(after));
    f.restoreBatch(s, 3, batch);
    CHECK(f.await(s, 3).str("outcome") == "conflict");
    f.restoreBatch(s, 4, batch);
    CHECK(f.await(s, 4).str("outcome") == "conflict");
    CHECK(f.edits->restorations().size() == 0);
  }
  SECTION("expired Image history precedes a retired registration binding")
  {
    f.edits->restorations().expireUnless(
        [](const edit::BatchRef &) { return false; });
    f.sceneIds.reset();
    f.restoreBatch(s, 3, batch);
    const auto unavailable = f.await(s, 3);
    CHECK(unavailable.str("outcome") == "outcome_unavailable");
    REQUIRE(unavailable.result());
    const auto *reason = unavailable.result()->get("reason");
    CHECK((reason && reason->s == "expired"));
    CHECK_FALSE(unavailable.flag("committed"));
    const unsigned char after[] = {29, 17, 41, 0};
    CHECK(std::memcmp(image->data(), after, sizeof(after)) == 0);
  }
  SECTION("retired registration is safe even without a replacement binding")
  {
    f.sceneIds.reset();
    f.restoreBatch(s, 3, batch);
    CHECK(f.await(s, 3).str("outcome") == "conflict");
    CHECK(f.edits->restorations().size() == 0);
    const unsigned char after[] = {29, 17, 41, 0};
    CHECK(std::memcmp(image->data(), after, sizeof(after)) == 0);
  }
  SECTION("replacement Scene identity cannot inherit an eligible batch")
  {
    f.sceneIds.reset();
    f.sceneIds = std::make_unique<identity::SceneIdentity>(f.ids, f.scene);
    f.restores->attachScene(f.sceneId, *f.sceneIds);
    f.images->attachScene(f.sceneId, *f.sceneIds);
    f.restoreBatch(s, 3, batch);
    CHECK(f.await(s, 3).str("outcome") == "conflict");
    const unsigned char after[] = {29, 17, 41, 0};
    CHECK(std::memcmp(image->data(), after, sizeof(after)) == 0);
    identity::SceneIdentity other(f.ids, f.hidden);
    f.restores->attachScene(f.hiddenId, other);
    f.tool(s,
        "vsr_restore",
        "{\"request_id\":4,\"scene_id\":" + std::to_string(f.hiddenId)
            + ",\"target_batch_ref\":" + json::write(batch) + "}");
    const auto foreign = f.await(s, 4);
    CHECK(foreign.str("outcome") == "outcome_unavailable");
    CHECK(foreign.result()->get("reason")->s == "foreign_scene");
    f.restoreBatch(s, 5, batch);
    CHECK(f.await(s, 5).str("outcome") == "conflict");
  }
  SECTION("deleted and reused pool slot cannot revive recovery")
  {
    const auto oldIndex = image->index();
    f.scene.removeObject(image.data());
    auto replacement = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
    REQUIRE(replacement->index() == oldIndex);
    replacement->setData(static_cast<const void *>(before));
    f.restoreBatch(s, 3, batch);
    CHECK(f.await(s, 3).str("outcome") == "conflict");
    CHECK(std::memcmp(replacement->data(), before, sizeof(before)) == 0);
  }
  SECTION("sync failure commits import and consumes restoration once")
  {
    f.hookOk = false;
    f.restoreBatch(s, 3, batch);
    const auto restored = f.await(s, 3);
    REQUIRE(restored.str("outcome") == "applied_with_sync_failure");
    CHECK(restored.flag("committed"));
    CHECK(std::memcmp(image->data(), before, sizeof(before)) == 0);
    REQUIRE(restored.result()->get("image_versions")->a.size() == 1);
    f.restoreBatch(s, 4, batch);
    CHECK(f.await(s, 4).str("outcome") == "already_restored");
  }
  SECTION("wrong Scene precedes consumed and does not consume eligibility")
  {
    identity::SceneIdentity other(f.ids, f.hidden);
    f.restores->attachScene(f.hiddenId, other);
    const auto wrong = [&](std::uint64_t id) {
      f.tool(s,
          "vsr_restore",
          "{\"request_id\":" + std::to_string(id)
              + ",\"scene_id\":" + std::to_string(f.hiddenId)
              + ",\"target_batch_ref\":" + json::write(batch) + "}");
      const auto result = f.await(s, id);
      CHECK(result.str("outcome") == "outcome_unavailable");
      CHECK(result.result()->get("reason")->s == "foreign_scene");
    };
    wrong(3);
    f.restoreBatch(s, 4, batch);
    REQUIRE(f.await(s, 4).str("outcome") == "ok");
    wrong(5);
  }
}

#ifdef VSR_TEST_VIEWER_UI
SCENARIO(
    "Agent Activity exposes and locally restores disconnected Image batches",
    "[integration][mcp][image-import][image-import-activity]")
{
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  image->setName("tracked-image");
  const unsigned char before[] = {17, 29, 41, 0};
  image->setData(static_cast<const void *>(before));
  const auto s = f.attach();
  const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
  writeInput(
      f.exchangePath() + "/agent.png", inputPng(1, 1, 6, {0, 29, 17, 41, 0}));
  f.importImage(s,
      2,
      item.get("handle")->s,
      item.get("image_info")->get("data_version")->s,
      "agent.png");
  REQUIRE(f.await(s, 2).str("outcome") == "ok");
  vsr::ui::imgui::IntegrationServices services;
  services.integration = &f.integration;
  services.authority = f.authority.get();
  services.activity = f.imageActivity.get();
  services.restore = f.restores.get();
  services.scenes.emplace_back(f.sceneId, "scene");
  vsr::ui::imgui::IntegrationActivityModel model(std::move(services));
  std::optional<edit::BatchRef> batch;
  for (const auto &row : model.refresh().activity) {
    if (row.entry.kind != activity::OperationKind::IMAGE_IMPORT)
      continue;
    CHECK(row.entry.eligibility == activity::Eligibility::ELIGIBLE);
    REQUIRE(row.entry.batchRef);
    REQUIRE(row.entry.targets.size() == 1);
    CHECK(row.entry.targets[0].id == f.sceneId);
    batch = row.entry.batchRef;
  }
  REQUIRE(batch);
  REQUIRE(model.disconnect());
  REQUIRE(model.setAgentEditsDisabled(true));
  const auto &disconnected = model.refresh();
  CHECK_FALSE(disconnected.sessionOwned);
  CHECK(disconnected.agentEditsDisabled);
  CHECK(disconnected.usage.restorableBatches == 1);
  REQUIRE(disconnected.scenes.size() == 1);
  CHECK(disconnected.scenes[0].localEdit);
  const auto local = model.restore(f.sceneId, *batch);
  REQUIRE(local.code == Outcome::OK);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!f.restores->resultLocal(local.ref)) {
    f.drain();
    REQUIRE(std::chrono::steady_clock::now() < deadline);
  }
  const auto restored = f.restores->resultLocal(local.ref);
  REQUIRE(restored->outcome == Outcome::OK);
  CHECK(restored->committed);
  CHECK(restored->imageVersions.size() == 1);
  CHECK(std::memcmp(image->data(), before, sizeof(before)) == 0);
  CHECK(model.refresh().usage.restorableBatches == 0);
  CHECK(f.edits->restorations().size() == 0);
  bool restoredRow = false;
  for (const auto &row : model.state().activity)
    if (row.entry.kind == activity::OperationKind::IMAGE_IMPORT) {
      restoredRow = true;
      CHECK(row.entry.eligibility == activity::Eligibility::RESTORED);
    }
  CHECK(restoredRow);
}
#endif

SCENARIO("MCP Image notifications burn reservations without losing recovery",
    "[integration][mcp][image-import][image-import-notification]")
{
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  image->setName("tracked-image");
  const unsigned char before[] = {17, 29, 41, 0};
  const unsigned char after[] = {29, 17, 41, 0};
  image->setData(static_cast<const void *>(before));
  REQUIRE(image->dataWriteSequence() == 1);
  const auto s = f.attach();
  const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
  const auto handle = item.get("handle")->s;
  const auto version = item.get("image_info")->get("data_version")->s;
  writeInput(
      f.exchangePath() + "/agent.png", inputPng(1, 1, 6, {0, 29, 17, 41, 0}));
  auto *fault = f.scene.updateDelegate().emplace<WriteFaultObserver>();
  SECTION(
      "failed Import map burns exactly one reservation and releases charges")
  {
    fault->allocationFailure = true;
    f.importImage(s, 2, handle, version, "agent.png");
    const auto failed = f.await(s, 2);
    CHECK(failed.str("outcome") == "busy");
    CHECK_FALSE(failed.flag("committed"));
    CHECK_FALSE(image->isMapped());
    CHECK(image->dataWriteSequence() == 1);
    CHECK(std::memcmp(image->data(), before, sizeof(before)) == 0);
    CHECK(f.edits->restorations().size() == 0);
    json::Value metadata;
    REQUIRE(json::parse(f.images->metadata(f.sceneId, s.id), metadata));
    CHECK(metadata.get("exchange_limits")
              ->get("session_retained_physical_bytes")
              ->n
        == 0);
    fault->failMap = false;
    f.importImage(s, 3, handle, version, "agent.png");
    const auto imported = f.await(s, 3);
    REQUIRE(imported.str("outcome") == "ok");
    CHECK(image->dataWriteSequence() == 3);
    f.restoreBatch(s, 4, *imported.result()->get("batch_ref"));
    CHECK(f.await(s, 4).str("outcome") == "ok");
    CHECK(image->dataWriteSequence() == 4);
    CHECK(std::memcmp(image->data(), before, sizeof(before)) == 0);
  }
  SECTION("cancelled Import after burn leaves no completed token or snapshot")
  {
    const operations::Credentials credentials{
        f.authority->launchCredential(), s.id, s.secret};
    fault->afterBurn = [&] { f.engine->cancel(credentials, {s.id, 2}); };
    f.importImage(s, 2, handle, version, "agent.png");
    const auto failed = f.await(s, 2);
    CHECK(failed.str("outcome") == "cancelled");
    CHECK_FALSE(failed.flag("committed"));
    CHECK(image->dataWriteSequence() == 1);
    CHECK_FALSE(image->isMapped());
    CHECK(std::memcmp(image->data(), before, sizeof(before)) == 0);
    CHECK(f.edits->restorations().size() == 0);
    fault->afterBurn = {};
    fault->failMap = false;
    f.importImage(s, 3, handle, version, "agent.png");
    CHECK(f.await(s, 3).str("outcome") == "ok");
    CHECK(image->dataWriteSequence() == 3);
  }
  SECTION("cancelled Restore after burn keeps recovery and the value spent")
  {
    fault->failMap = false;
    f.importImage(s, 2, handle, version, "agent.png");
    const auto imported = f.await(s, 2);
    REQUIRE(imported.str("outcome") == "ok");
    const auto batch = *imported.result()->get("batch_ref");
    const operations::Credentials credentials{
        f.authority->launchCredential(), s.id, s.secret};
    fault->failMap = true;
    fault->afterBurn = [&] { f.engine->cancel(credentials, {s.id, 3}); };
    f.restoreBatch(s, 3, batch);
    const auto failed = f.await(s, 3);
    CHECK(failed.str("outcome") == "cancelled");
    CHECK_FALSE(failed.flag("committed"));
    CHECK(image->dataWriteSequence() == 2);
    CHECK(std::memcmp(image->data(), after, sizeof(after)) == 0);
    CHECK(f.restores->eligible(
        {batch.get("launch_id")->s, batch.get("batch_id")->s}));
    fault->afterBurn = {};
    fault->failMap = false;
    f.restoreBatch(s, 4, batch);
    CHECK(f.await(s, 4).str("outcome") == "ok");
    CHECK(image->dataWriteSequence() == 4);
    CHECK(std::memcmp(image->data(), before, sizeof(before)) == 0);
  }
  SECTION("failed Restore map keeps its snapshot and spent sequence")
  {
    fault->failMap = false;
    f.importImage(s, 2, handle, version, "agent.png");
    const auto imported = f.await(s, 2);
    REQUIRE(imported.str("outcome") == "ok");
    REQUIRE(image->dataWriteSequence() == 2);
    const auto batch = *imported.result()->get("batch_ref");
    fault->failMap = true;
    fault->allocationFailure = true;
    f.restoreBatch(s, 3, batch);
    const auto failed = f.await(s, 3);
    CHECK(failed.str("outcome") == "busy");
    CHECK_FALSE(failed.flag("committed"));
    CHECK(image->dataWriteSequence() == 2);
    CHECK_FALSE(image->isMapped());
    CHECK(std::memcmp(image->data(), after, sizeof(after)) == 0);
    CHECK(f.restores->eligible(
        {batch.get("launch_id")->s, batch.get("batch_id")->s}));
    fault->failMap = false;
    f.restoreBatch(s, 4, batch);
    CHECK(f.await(s, 4).str("outcome") == "ok");
    CHECK(image->dataWriteSequence() == 4);
    CHECK(std::memcmp(image->data(), before, sizeof(before)) == 0);
  }
  SECTION(
      "unmap notification failure still publishes import and consumes restore")
  {
    fault->failMap = false;
    fault->failUnmap = true;
    f.importImage(s, 2, handle, version, "agent.png");
    const auto imported = f.await(s, 2);
    REQUIRE(imported.str("outcome") == "applied_with_sync_failure");
    CHECK(imported.flag("committed"));
    CHECK(image->dataWriteSequence() == 2);
    CHECK_FALSE(image->isMapped());
    CHECK(std::memcmp(image->data(), after, sizeof(after)) == 0);
    const auto batch = *imported.result()->get("batch_ref");
    f.restoreBatch(s, 3, batch);
    const auto restored = f.await(s, 3);
    REQUIRE(restored.str("outcome") == "applied_with_sync_failure");
    CHECK(restored.flag("committed"));
    REQUIRE(restored.result()->get("image_versions")->a.size() == 1);
    CHECK(image->dataWriteSequence() == 3);
    CHECK(std::memcmp(image->data(), before, sizeof(before)) == 0);
    f.restoreBatch(s, 4, batch);
    CHECK(f.await(s, 4).str("outcome") == "already_restored");
  }
  f.scene.updateDelegate().erase(fault);
}

SCENARIO("MCP Image Import retains more than the legacy 64 MiB session ceiling",
    "[integration][mcp][image-import][image-import-session]")
{
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 128u << 20; // Explicit test host configuration.
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, limits, true);
  constexpr std::size_t T = 16u << 20;
  std::vector<unsigned char> before(T, 71), rows(T + 2048, 0);
  std::vector<vsr::scene::ArrayRef> images;
  for (int i = 0; i < 5; ++i) {
    auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 2048, 2048);
    image->setData(static_cast<const void *>(before.data()));
    images.push_back(image);
  }
  const auto a = f.attach();
  writeInput(f.exchangePath() + "/agent.png", inputPng(2048, 2048, 6, rows));
  json::Value lastBatch;
  for (std::uint64_t id = 1; id <= 5; ++id) {
    const auto handle = f.sceneIds->handleOf(images[id - 1].data());
    const auto wire =
        edit::detail::hex64(handle.domain) + edit::detail::hex64(handle.serial);
    f.importImage(
        a, id, wire, *f.sceneIds->imageDataVersion(handle), "agent.png");
    const auto imported = f.await(a, id);
    REQUIRE(imported.str("outcome") == "ok");
    lastBatch = *imported.result()->get("batch_ref");
  }
  json::Value metadata;
  REQUIRE(json::parse(f.images->metadata(f.sceneId, a.id), metadata));
  CHECK(
      metadata.get("exchange_limits")->get("session_retained_physical_bytes")->n
      == 5 * T);
  CHECK_FALSE(
      metadata.get("exchange_limits")->get("session_snapshot_byte_cap"));
  CHECK(f.edits->restorations().size() == 5);
  const std::vector<unsigned char> zero(T, 0);
  CHECK(std::memcmp(images[4]->data(), zero.data(), T) == 0);
  REQUIRE(f.rpc("vsr/detach", "{}", &a).status == 200);
  const auto b = f.attach();
  REQUIRE(b.id != a.id);
  REQUIRE(json::parse(f.images->metadata(f.sceneId, b.id), metadata));
  CHECK(
      metadata.get("exchange_limits")->get("session_retained_physical_bytes")->n
      == 0);
  REQUIRE(json::parse(f.images->metadata(f.sceneId, a.id), metadata));
  CHECK(
      metadata.get("exchange_limits")->get("session_retained_physical_bytes")->n
      == 5 * T);
  f.restoreBatch(b, 1, lastBatch);
  REQUIRE(f.await(b, 1).str("outcome") == "ok");
  CHECK(std::memcmp(images[4]->data(), before.data(), before.size()) == 0);
  REQUIRE(json::parse(f.images->metadata(f.sceneId, a.id), metadata));
  CHECK(
      metadata.get("exchange_limits")->get("session_retained_physical_bytes")->n
      == 4 * T);
  REQUIRE(json::parse(f.images->metadata(f.sceneId, b.id), metadata));
  CHECK(
      metadata.get("exchange_limits")->get("session_retained_physical_bytes")->n
      == 0);
}

SCENARIO(
    "MCP Image Import global eviction releases original ledger and reports expiry",
    "[integration][mcp][image-import][image-import-eviction]")
{
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 3u << 20;
  const auto physical = std::make_shared<vsr::rendering::FrameBudget>();
  bool refuseSnapshot = false;
  limits.reservePhysicalMemory = [&, physical](std::size_t bytes) {
    return refuseSnapshot && bytes == 1048576 ? std::shared_ptr<const void>{}
                                              : physical->reserve(bytes);
  };
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, limits, true);
  const std::vector<unsigned char> before(1048576, 71), rows(1048640, 0);
  std::vector<vsr::scene::ArrayRef> images;
  for (int i = 0; i < 3; ++i) {
    auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 4096, 64);
    image->setData(static_cast<const void *>(before.data()));
    images.push_back(image);
  }
  const auto a = f.attach();
  writeInput(f.exchangePath() + "/agent.png", inputPng(4096, 64, 6, rows));
  const auto import = [&](const Session &session,
                          std::uint64_t id,
                          std::size_t index) {
    const auto handle = f.sceneIds->handleOf(images[index].data());
    f.importImage(session,
        id,
        edit::detail::hex64(handle.domain) + edit::detail::hex64(handle.serial),
        *f.sceneIds->imageDataVersion(handle),
        "agent.png");
    auto imported = f.await(session, id);
    REQUIRE(imported.str("outcome") == "ok");
    return *imported.result()->get("batch_ref");
  };
  const auto first = import(a, 1, 0);
  const auto second = import(a, 2, 1);
  // Each failed attempt starts with a full recoverable pool.
  const auto failure = GENERATE(0, 1, 2, 3, 4);
  INFO("failure mode=" << failure);
  writeInput(f.exchangePath() + "/broken.png", {1, 2, 3});
  const auto thirdHandle = f.sceneIds->handleOf(images[2].data());
  const auto thirdWire = edit::detail::hex64(thirdHandle.domain)
      + edit::detail::hex64(thirdHandle.serial);
  const auto expectedVersion = *f.sceneIds->imageDataVersion(thirdHandle);
  if (failure == 1)
    images[2]->setData(static_cast<const void *>(before.data()));
  refuseSnapshot = failure == 4;
  f.importImage(a,
      3,
      thirdWire,
      expectedVersion,
      failure == 0 ? "broken.png" : "agent.png");
  if (failure == 2)
    f.authority->setAgentEditsDisabled(true);
  if (failure == 3)
    f.tool(a, "vsr_cancel", "{\"operation_ref\":" + ref(a.id, 3) + "}");
  const auto failed = f.await(a, 3);
  const char *outcomes[] = {
      "invalid_request", "conflict", "forbidden", "cancelled", "busy"};
  CHECK(failed.str("outcome") == outcomes[failure]);
  f.authority->setAgentEditsDisabled(false);
  refuseSnapshot = false;
  CHECK_FALSE(failed.flag("committed"));
  CHECK(f.edits->restorations().find(
      {first.get("launch_id")->s, first.get("batch_id")->s}));
  CHECK(f.edits->restorations().find(
      {second.get("launch_id")->s, second.get("batch_id")->s}));
  CHECK(std::memcmp(images[2]->data(), before.data(), before.size()) == 0);
  REQUIRE(f.rpc("vsr/detach", "{}", &a).status == 200);
  const auto b = f.attach();
  const auto newest = import(b, 1, 2);
  f.drain();
  json::Value metadata;
  REQUIRE(json::parse(f.images->metadata(f.sceneId, a.id), metadata));
  CHECK(
      metadata.get("exchange_limits")->get("session_retained_physical_bytes")->n
      == 1048576);
  f.restoreBatch(b, 3, first);
  auto expired = f.await(b, 3);
  REQUIRE(expired.str("outcome") == "outcome_unavailable");
  CHECK(expired.result()->get("reason")->s == "expired");
  f.restoreBatch(b, 4, second);
  REQUIRE(f.await(b, 4).flag("committed"));
  CHECK(std::memcmp(images[1]->data(), before.data(), before.size()) == 0);
  f.restoreBatch(b, 5, newest);
  REQUIRE(f.await(b, 5).flag("committed"));
  CHECK(std::memcmp(images[2]->data(), before.data(), before.size()) == 0);
}

SCENARIO(
    "MCP Image write exhaustion leaves recoverable bytes and read access intact",
    "[integration][mcp][image-import][image-import-exhaustion]")
{
  Fixture f({}, true, false, false, 120., true, 2, {}, true);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  image->setName("tracked-image");
  const unsigned char before[] = {17, 29, 41, 0};
  image->setData(static_cast<const void *>(before)); // First sequence.
  const auto s = f.attach();
  const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
  const auto handle = item.get("handle")->s;
  writeInput(
      f.exchangePath() + "/agent.png", inputPng(1, 1, 6, {0, 29, 17, 41, 0}));
  f.importImage(s,
      2,
      handle,
      item.get("image_info")->get("data_version")->s,
      "agent.png");
  const auto committed = f.await(s, 2); // Exactly one more sequence.
  REQUIRE(committed.str("outcome") == "ok");
  const auto batch = *committed.result()->get("batch_ref");
  f.restoreBatch(s, 3, batch);
  const auto rejected = f.await(s, 3);
  REQUIRE(rejected.str("outcome") == "busy");
  CHECK(rejected.result()->get("reason")->s == "resource_limit");
  CHECK_FALSE(rejected.flag("committed"));
  CHECK(f.restores->eligible(
      {batch.get("launch_id")->s, batch.get("batch_id")->s}));
  CHECK_FALSE(image->isMapped());
  CHECK(*f.sceneIds->imageDataVersion(f.sceneIds->handleOf(image.data()))
      == committed.result()->get("data_version")->s);
  f.exportImage(s, 4, handle);
  CHECK(f.await(s, 4).str("outcome") == "ok");
  f.tool(s,
      "vsr_inspect",
      "{\"request_id\":5,\"scene_id\":" + std::to_string(f.sceneId)
          + ",\"object_type\":\"ANARI_MATERIAL\"}");
  const auto material = f.await(s, 5).result()->get("items")->a.at(0);
  const auto target = "\"handle\":" + json::quote(material.get("handle")->s)
      + ",\"version\":"
      + std::to_string(std::uint64_t(material.get("version")->n));
  f.editOpacity(s, 6, target, 0.25);
  CHECK(f.await(s, 6).str("outcome") == "ok");
}

SCENARIO(
    "MCP Image restore cannot invent foreign Scene evidence after history eviction",
    "[integration][mcp][image-import][image-import-scope-expiry]")
{
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 262144;
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, limits, true);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  image->setName("tracked-image");
  const unsigned char before[] = {17, 29, 41, 0};
  image->setData(static_cast<const void *>(before));
  const auto s = f.attach();
  const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
  writeInput(
      f.exchangePath() + "/agent.png", inputPng(1, 1, 6, {0, 29, 17, 41, 0}));
  f.importImage(s,
      2,
      item.get("handle")->s,
      item.get("image_info")->get("data_version")->s,
      "agent.png");
  const auto imported = f.await(s, 2);
  REQUIRE(imported.str("outcome") == "ok");
  const auto batch = *imported.result()->get("batch_ref");
  f.restoreBatch(s, 3, batch);
  REQUIRE(f.await(s, 3).str("outcome") == "ok");
  // Consumed history is ordinary metadata, unlike eligible recovery.
  for (unsigned i = 0; i < 1024; ++i)
    REQUIRE(f.imageActivity->append(activity::Entry{}));
  CHECK_FALSE(f.imageActivity->eligibility(
      {batch.get("launch_id")->s, batch.get("batch_id")->s}));
  identity::SceneIdentity other(f.ids, f.hidden);
  f.restores->attachScene(f.hiddenId, other);
  f.tool(s,
      "vsr_restore",
      "{\"request_id\":4,\"scene_id\":" + std::to_string(f.hiddenId)
          + ",\"target_batch_ref\":" + json::write(batch) + "}");
  const auto absent = f.await(s, 4);
  CHECK(absent.str("outcome") == "outcome_unavailable");
  CHECK((!absent.result() || !absent.result()->get("reason")
      || absent.result()->get("reason")->s != "foreign_scene"));
}

SCENARIO(
    "MCP Image Import allocation failures release every pending reservation",
    "[integration][mcp][image-import][image-import-allocation]")
{
  for (int allocations = 0; allocations < 5; ++allocations) {
    INFO("successful allocations before failure " << allocations);
    ImageAllocator allocator;
    allocator.remaining = allocations;
    image::ExchangeLimits limits;
    limits.allocator = &allocator;
    Fixture f({}, true, false, false, 120., true, UINT64_MAX, limits, true);
    auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
    image->setName("tracked-image");
    const unsigned char before[] = {17, 29, 41, 0};
    image->setData(static_cast<const void *>(before));
    const auto s = f.attach();
    const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
    const auto handle = item.get("handle")->s;
    const auto version = item.get("image_info")->get("data_version")->s;
    writeInput(
        f.exchangePath() + "/agent.png", inputPng(1, 1, 6, {0, 29, 17, 41, 0}));
    f.importImage(s, 2, handle, version, "agent.png");
    const auto rejected = f.await(s, 2);
    REQUIRE(rejected.str("outcome") == "busy");
    CHECK(rejected.result()->get("reason")->s == "resource_limit");
    CHECK_FALSE(rejected.flag("committed"));
    CHECK_FALSE(image->isMapped());
    CHECK(std::memcmp(image->data(), before, sizeof(before)) == 0);
    CHECK(f.edits->restorations().size() == 0);
    json::Value metadata;
    REQUIRE(json::parse(f.images->metadata(f.sceneId, s.id), metadata));
    CHECK(metadata.get("exchange_limits")
              ->get("session_retained_physical_bytes")
              ->n
        == 0);
    allocator.remaining = 100;
    f.importImage(s, 3, handle, version, "agent.png");
    CHECK(f.await(s, 3).str("outcome") == "ok");
  }
}

SCENARIO(
    "MCP Image Import keeps shared cache users and disconnected local recovery",
    "[integration][mcp][image-import][image-import-cache]")
{
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
  vsr::io::ImageCache cache(&f.scene);
  const vsr::io::ImageSource source{
      "embedded:tracked", vsr::io::ColorSpace::LINEAR};
  const unsigned char before[] = {17, 29, 41, 0}, after[] = {29, 17, 41, 0};
  auto image = cache.acquireDecoded(
      source, ANARI_UFIXED8_VEC4, 1, 1, vsr::io::RowOrder::TOP_DOWN, before);
  image.texels->setName("tracked-image");
  auto sampler = vsr::io::makeImageSampler(cache, image, "first");
  auto other = vsr::io::makeImageSampler(cache, image, "second");
  auto light = f.scene.createObject<vsr::scene::Light>("hdri");
  light->setParameterObject("radiance", *image.texels);
  const auto s = f.attach();
  const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
  writeInput(
      f.exchangePath() + "/agent.png", inputPng(1, 1, 6, {0, 29, 17, 41, 0}));
  f.hookOk = false;
  f.importImage(s,
      2,
      item.get("handle")->s,
      item.get("image_info")->get("data_version")->s,
      "agent.png");
  const auto imported = f.await(s, 2);
  REQUIRE(imported.str("outcome") == "applied_with_sync_failure");
  CHECK(imported.flag("committed"));
  REQUIRE(imported.result()->get("affected_samplers")->a.size() == 2);
  REQUIRE(imported.result()->get("affected_users")->a.size() == 1);
  CHECK(imported.result()->get("affected_complete")->b);
  CHECK(std::memcmp(image.texels->data(), after, sizeof(after)) == 0);
  f.scene.removeObject(sampler.data());
  f.scene.removeObject(other.data());
  auto same = cache.acquireDecoded(
      source, ANARI_UFIXED8_VEC4, 1, 1, vsr::io::RowOrder::TOP_DOWN, before);
  CHECK(same.texels == image.texels);
  CHECK(std::memcmp(same.texels->data(), after, sizeof(after)) == 0);
  auto rebound = vsr::io::makeImageSampler(cache, same, "new-user");
  CHECK(rebound->parameterValueAsObject<vsr::scene::Array>("image")
      == image.texels.data());
  const auto batch = *imported.result()->get("batch_ref");
  REQUIRE(f.rpc("vsr/detach", "{}", &s).status == 200);
  f.authority->setAgentEditsDisabled(true);
  const auto local = f.restores->submitLocal(
      {f.sceneId, {batch.get("launch_id")->s, batch.get("batch_id")->s}});
  REQUIRE(local.code == Outcome::OK);
  f.drain();
  const auto restored = f.engine->statusLocal(local.ref);
  REQUIRE(restored.terminal == Outcome::APPLIED_WITH_SYNC_FAILURE);
  CHECK(restored.committed);
  auto restoredImage = cache.acquireDecoded(
      source, ANARI_UFIXED8_VEC4, 1, 1, vsr::io::RowOrder::TOP_DOWN, after);
  CHECK(restoredImage.texels == image.texels);
  CHECK(std::memcmp(restoredImage.texels->data(), before, sizeof(before)) == 0);
}

SCENARIO("Image result copies retain independent physical ownership",
    "[integration][mcp][image-import][image-import-owners]")
{
  auto memory = std::make_shared<vsr::rendering::FrameBudget>(8u << 20);
  const auto baseline = memory->used();
  auto probe = memory->reserve(1);
  const auto overhead = memory->used() - baseline - 1;
  probe.reset();
  image::ExchangeLimits limits;
  limits.physicalByteCap = memory->limit();
  limits.reservePhysicalMemory = [memory](std::size_t bytes) {
    return memory->reserve(bytes);
  };
  auto f = std::make_unique<Fixture>(inspection::PageLimits{},
      true,
      false,
      false,
      120.,
      true,
      UINT64_MAX,
      limits,
      true);
  auto image = f->scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  image->setName("tracked-image");
  const unsigned char before[] = {17, 29, 41, 0};
  image->setData(static_cast<const void *>(before));
  const auto s = f->attach();
  const auto item = f->inspectArray(s, 1).result()->get("items")->a.at(0);
  writeInput(
      f->exchangePath() + "/agent.png", inputPng(1, 1, 6, {0, 29, 17, 41, 0}));
  f->importImage(s,
      2,
      item.get("handle")->s,
      item.get("image_info")->get("data_version")->s,
      "agent.png");
  REQUIRE(f->await(s, 2).str("outcome") == "ok");
  const operations::Credentials credentials{
      f->authority->launchCredential(), s.id, s.secret};
  std::optional<operations::Status> held =
      f->engine->status(credentials, {s.id, 2});
  REQUIRE(held->terminal == Outcome::OK);
  auto usage = memory->used();
  std::optional<operations::Status> copy = *held;
  CHECK(memory->used() == usage + 65536 + overhead);
  usage = memory->used();
  auto entries = f->imageActivity->snapshot();
  CHECK(memory->used() == usage + 65536 + overhead);
  const auto imported =
      std::find_if(entries.begin(), entries.end(), [](const auto &entry) {
        return entry.entry.kind == activity::OperationKind::IMAGE_IMPORT;
      });
  REQUIRE(imported != entries.end());
  usage = memory->used();
  std::optional<activity::Entry> entryCopy = imported->entry;
  CHECK(memory->used() == usage + 65536 + overhead);
  REQUIRE(entryCopy->batchRef);
  const auto batch = *entryCopy->batchRef;
  json::Value batchJson;
  REQUIRE(json::parse(edit::detail::batchRefJson(batch), batchJson));
  f->restoreBatch(s, 3, batchJson);
  REQUIRE(f->await(s, 3).str("outcome") == "ok");
  std::optional<restore::RestoreResult> slot = f->restores->result({s.id, 3});
  REQUIRE(slot);
  usage = memory->used();
  std::optional<restore::RestoreResult> slotCopy = *slot;
  CHECK(memory->used() == usage + 65536 + overhead);
  REQUIRE(f->rpc("vsr/detach", "{}", &s).status == 200);
  f.reset();
  // Job/worker/snapshot/Engine/Activity/store die independently of held
  // public copies. The six copies retain only their six own charges.
  CHECK(memory->used() == baseline + 6 * (65536 + overhead));
  CHECK(copy->result == held->result);
  CHECK(entryCopy->result == held->result);
  held.reset();
  copy.reset();
  entries.clear();
  entryCopy.reset();
  slot.reset();
  slotCopy.reset();
  CHECK(memory->used() == baseline);
}

SCENARIO("Image scope history owns bounded consumed and expired controls",
    "[integration][mcp][image-import][image-import-history]")
{
  auto memory = std::make_shared<vsr::rendering::FrameBudget>(8u << 20);
  const auto baseline = memory->used();
  auto probe = memory->reserve(1);
  const auto overhead = memory->used() - baseline - 1;
  probe.reset();
  {
    edit::RestorationStore store;
    CHECK(store.capacity() == 32);
    for (unsigned i = 0; i < 1050; ++i) {
      auto snapshot = std::make_shared<edit::ImageSnapshot>();
      snapshot->scopeLease = memory->reserve(4096);
      snapshot->physicalLease = memory->reserve(1);
      REQUIRE(snapshot->scopeLease);
      REQUIRE(snapshot->physicalLease);
      snapshot->bytes.resize(1);
      edit::RestorationRecord record;
      record.ref = {"launch", std::to_string(i)};
      record.scene = 7;
      record.image = snapshot;
      const auto ref = record.ref;
      REQUIRE(store.prepareImage(std::move(record)));
      store.publishImage(ref);
      if (i % 2) {
        REQUIRE(store.setImageState(
            ref, edit::RestorationStore::ImageState::CONFLICTING));
      } else {
        REQUIRE(store.setImageState(
            ref, edit::RestorationStore::ImageState::RESTORED));
      }
      store.release(ref);
      snapshot.reset();
      CHECK(memory->used()
          == baseline + std::min<unsigned>(i + 1, 1024) * (4096 + overhead));
    }
    CHECK_FALSE(store.imageScope({"launch", "0"}));
    CHECK(store.imageScope({"launch", "1049"}) == 7);
    CHECK(store.imageState({"launch", "1049"})
        == edit::RestorationStore::ImageState::CONFLICTING);
    CHECK(store.size() == 0);
  }
  CHECK(memory->used() == baseline);
  {
    edit::RestorationStore store;
    for (unsigned i = 0; i < 1050; ++i) {
      auto snapshot = std::make_shared<edit::ImageSnapshot>();
      snapshot->scopeLease = memory->reserve(4096);
      REQUIRE(snapshot->scopeLease);
      edit::RestorationRecord record;
      record.ref = {"launch", std::to_string(i)};
      record.scene = 7;
      record.image = snapshot;
      REQUIRE(store.prepareImage(std::move(record)));
      store.publishImage({"launch", std::to_string(i)});
      store.expireUnless([](const edit::BatchRef &) { return false; });
      snapshot.reset();
      CHECK(memory->used()
          == baseline + std::min<unsigned>(i + 1, 1024) * (4096 + overhead));
    }
    CHECK_FALSE(store.imageScope({"launch", "1049"}));
    CHECK(store.expired({"launch", "1049"}));
    CHECK_FALSE(store.expired({"launch", "0"}));
  }
  CHECK(memory->used() == baseline);
}

SCENARIO("Image admission preserves default Activity and store ceilings",
    "[integration][mcp][image-import][image-import-defaults]")
{
  activity::ActivityLog log;
  CHECK(log.limits().bytes == 16777216);
  CHECK(log.limits().entries == 1024);
  CHECK(log.limits().restorableBatches == 32);
  CHECK(activity::ENTRY_OVERHEAD == 256);
  std::vector<activity::ActivityLog::Id> ids;
  for (unsigned i = 0; i < 1024; ++i) {
    activity::Entry entry;
    entry.active = true;
    const auto id = log.append(std::move(entry));
    REQUIRE(id);
    ids.push_back(*id);
  }
  CHECK_FALSE(log.append(activity::Entry{}));
  for (unsigned i = 0; i < 32; ++i)
    REQUIRE(log.reserveRestoration(ids[i], 4, {"launch", std::to_string(i)}));
  CHECK_FALSE(log.reserveRestoration(ids[32], 4, {"launch", "32"}));
  log.cancelRestoration(ids[0]);
  CHECK(log.reserveRestoration(ids[32], 4, {"launch", "32"}));
  edit::RestorationStore store;
  for (unsigned i = 0; i < 32; ++i) {
    edit::RestorationRecord record;
    record.ref = {"launch", std::to_string(i)};
    record.image = std::make_shared<edit::ImageSnapshot>();
    REQUIRE(store.prepareImage(std::move(record)));
  }
  edit::RestorationRecord extra;
  extra.ref = {"launch", "32"};
  extra.image = std::make_shared<edit::ImageSnapshot>();
  CHECK_FALSE(store.prepareImage(extra));
  store.cancelImage({"launch", "0"});
  CHECK(store.prepareImage(std::move(extra)));
}

SCENARIO(
    "Image peak reserves conservative E+D+W+S and retains one four-MiB snapshot",
    "[integration][mcp][image-import][image-import-peak]")
{
  auto memory = std::make_shared<vsr::rendering::FrameBudget>(40u << 20);
  const auto baseline = memory->used();
  auto probe = memory->reserve(1);
  const auto overhead = memory->used() - baseline - 1;
  probe.reset();
  image::ExchangeLimits limits;
  limits.physicalByteCap = memory->limit();
  limits.reservePhysicalMemory = [memory](std::size_t bytes) {
    return memory->reserve(bytes);
  };
  bool fits = true;
  SECTION("production conservative maximum exceeds illustrative 27 MiB")
  {
    limits.transientBytes = 27u << 20;
    fits = false;
  }
  SECTION("checked conservative maximum fits exactly")
  {
    limits.transientBytes = 34674688;
  }
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, limits, true);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1024, 1024);
  image->setName("tracked-image");
  const std::vector<unsigned char> before(4194304, 71), rows(4195328, 0);
  image->setData(static_cast<const void *>(before.data()));
  const auto s = f.attach();
  const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
  writeInput(f.exchangePath() + "/agent.png", inputPng(1024, 1024, 6, rows));
  f.importImage(s,
      2,
      item.get("handle")->s,
      item.get("image_info")->get("data_version")->s,
      "agent.png");
  REQUIRE(f.integration.service() > 0);
  if (fits) {
    // Independent worked oracle: E=8,456,192, D=S=4,194,304,
    // W=17,829,888. Public leases expose the single physical S charge.
    CHECK(memory->used() == baseline + 34674688 + 6 * overhead);
    json::Value metadata;
    REQUIRE(json::parse(f.images->metadata(f.sceneId, s.id), metadata));
    CHECK(metadata.get("exchange_limits")
              ->get("session_retained_physical_bytes")
              ->n
        == 4194304);
  }
  const auto completed = f.await(s, 2);
  if (fits) {
    REQUIRE(completed.str("outcome") == "ok");
    CHECK(memory->used()
        == baseline + 4194304 + 32768 + 8192 + 65536 + 5 * overhead);
    f.restoreBatch(s, 3, *completed.result()->get("batch_ref"));
    REQUIRE(f.await(s, 3).str("outcome") == "ok");
    CHECK(std::memcmp(image->data(), before.data(), before.size()) == 0);
  } else {
    CHECK(completed.str("outcome") == "busy");
    CHECK(completed.result()->get("reason")->s == "resource_limit");
    CHECK_FALSE(completed.flag("committed"));
    CHECK(std::memcmp(image->data(), before.data(), before.size()) == 0);
    CHECK(f.edits->restorations().size() == 0);
  }
}

SCENARIO(
    "Image copy and Restore slot reservations fail without uncharged owners",
    "[integration][mcp][image-import][image-import-copy-pressure]")
{
  auto memory = std::make_shared<vsr::rendering::FrameBudget>(8u << 20);
  bool allow = true;
  image::ExchangeLimits limits;
  limits.physicalByteCap = memory->limit();
  limits.reservePhysicalMemory = [memory, &allow](std::size_t bytes) {
    return allow ? memory->reserve(bytes) : std::shared_ptr<const void>{};
  };
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, limits, true);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  image->setName("tracked-image");
  const unsigned char before[] = {17, 29, 41, 0};
  image->setData(static_cast<const void *>(before));
  const auto s = f.attach();
  const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
  writeInput(
      f.exchangePath() + "/agent.png", inputPng(1, 1, 6, {0, 29, 17, 41, 0}));
  f.importImage(s,
      2,
      item.get("handle")->s,
      item.get("image_info")->get("data_version")->s,
      "agent.png");
  const auto imported = f.await(s, 2);
  REQUIRE(imported.str("outcome") == "ok");
  const auto batch = *imported.result()->get("batch_ref");
  const operations::Credentials credentials{
      f.authority->launchCredential(), s.id, s.secret};
  const auto held = f.engine->status(credentials, {s.id, 2});
  const auto entries = f.imageActivity->snapshot();
  const auto entry =
      std::find_if(entries.begin(), entries.end(), [](const auto &row) {
        return row.entry.kind == activity::OperationKind::IMAGE_IMPORT;
      });
  REQUIRE(entry != entries.end());
  f.restoreBatch(s, 3, batch);
  REQUIRE(f.await(s, 3).str("outcome") == "ok");
  const auto slot = f.restores->result({s.id, 3});
  REQUIRE(slot);
  const auto used = memory->used();
  const auto sequence = image->dataWriteSequence();
  allow = false;
  CHECK_THROWS_AS(operations::Status(held), std::bad_alloc);
  CHECK_THROWS_AS(activity::Entry(entry->entry), std::bad_alloc);
  CHECK_THROWS_AS(restore::RestoreResult(*slot), std::bad_alloc);
  const edit::BatchRef batchRef{
      batch.get("launch_id")->s, batch.get("batch_id")->s};
  CHECK(f.restores->submitLocal({f.sceneId, batchRef}).code == Outcome::BUSY);
  CHECK(memory->used() == used);
  CHECK(image->dataWriteSequence() == sequence);
  allow = true;
  const auto rejected =
      f.restores->submit({}, {s.id, 4}, {f.sceneId, batchRef});
  CHECK(rejected.code == Outcome::UNAUTHENTICATED);
  CHECK(memory->used() == used);
  const auto admitted = f.restores->submitLocal({f.sceneId, batchRef});
  REQUIRE(admitted.code == Outcome::OK);
  f.drain();
  CHECK(f.restores->resultLocal(admitted.ref)->outcome
      == Outcome::ALREADY_RESTORED);
}

SCENARIO("Restore histories bound rejected and cancelled slot ownership",
    "[integration][mcp][image-import][image-import-slots]")
{
  auto memory = std::make_shared<vsr::rendering::FrameBudget>(160u << 20);
  const auto baseline = memory->used();
  auto probe = memory->reserve(1);
  const auto overhead = memory->used() - baseline - 1;
  probe.reset();
  image::ExchangeLimits limits;
  limits.physicalByteCap = memory->limit();
  limits.reservePhysicalMemory = [memory](std::size_t bytes) {
    return memory->reserve(bytes);
  };
  {
    Fixture f({}, true, false, false, 120., true, UINT64_MAX, limits, true);
    const auto s = f.attach();
    const operations::Credentials credentials{
        f.authority->launchCredential(), s.id, s.secret};
    const restore::RestoreRequest absent{
        f.sceneId, {f.authority->launchId(), "absent"}};
    bool local = false;
    SECTION("external slots") {}
    SECTION("local slots")
    {
      local = true;
    }
    operations::LocalRef firstLocal;
    for (unsigned i = 1; i <= 1050; ++i) {
      if (local) {
        const auto admitted = f.restores->submitLocal(absent);
        REQUIRE(admitted.code == Outcome::OK);
        if (i == 1) {
          firstLocal = admitted.ref;
          REQUIRE(f.engine->cancelLocal(admitted.ref).code == Outcome::OK);
        }
      } else {
        REQUIRE(f.restores->submit(credentials, {s.id, i}, absent).code
            == Outcome::OK);
        if (i == 1)
          REQUIRE(f.engine->cancel(credentials, {s.id, i}).code == Outcome::OK);
      }
      f.drain();
      if (i > 1024)
        CHECK(memory->used() == baseline + 2048 * (65536 + overhead));
    }
    if (local) {
      CHECK_FALSE(f.restores->resultLocal(firstLocal));
    } else {
      CHECK_FALSE(f.restores->result({s.id, 1}));
      REQUIRE(f.restores->result({s.id, 1050}));
      CHECK(f.restores->result({s.id, 1050})->outcome
          == Outcome::OUTCOME_UNAVAILABLE);
    }
  }
  CHECK(memory->used() == baseline);
}

static std::string imageHandleText(identity::ObjectHandle handle)
{
  char text[33];
  std::snprintf(text,
      sizeof(text),
      "%016llx%016llx",
      (unsigned long long)handle.domain,
      (unsigned long long)handle.serial);
  return text;
}

SCENARIO("Image Import distinguishes foreign, unknown and wrong-kind targets",
    "[integration][mcp][image-import][image-import-identity]")
{
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
  identity::SceneIdentity other(f.ids, f.hidden);
  f.images->attachScene(f.hiddenId, other);
  auto foreign = f.hidden.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  auto material = f.scene.createObject<vsr::scene::Material>("matte");
  auto handle = other.handleOf(foreign.data());
  std::string reason = "foreign_scene";
  SECTION("authorized foreign Array") {}
  SECTION("unknown identity")
  {
    handle.serial += 100000;
    reason = "invalid_handle";
  }
  SECTION("wrong-kind object")
  {
    handle = f.sceneIds->handleOf(material.data());
    reason = "invalid_handle";
  }
  const auto s = f.attach();
  f.importImage(s, 1, imageHandleText(handle), "unknown-version", "absent.png");
  const auto rejected = f.await(s, 1);
  REQUIRE(rejected.str("outcome") == "invalid_request");
  CHECK(rejected.result()->get("reason")->s == reason);
  CHECK_FALSE(rejected.flag("committed"));
  CHECK(f.edits->restorations().size() == 0);
}

SCENARIO(
    "Image Import accepts cross-handle export without crossing cache keys or Scenes",
    "[integration][mcp][image-import][image-import-cross-cache]")
{
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
  vsr::io::ImageCache cache(&f.scene), otherCache(&f.hidden);
  const vsr::io::ImageSource source{
      "embedded:shared", vsr::io::ColorSpace::LINEAR};
  const vsr::io::ImageSource otherKey{
      "embedded:other", vsr::io::ColorSpace::LINEAR};
  const unsigned char before[] = {17, 29, 41, 0}, after[] = {29, 17, 41, 0};
  auto target = cache.acquireDecoded(
      source, ANARI_UFIXED8_VEC4, 1, 1, vsr::io::RowOrder::TOP_DOWN, before);
  auto different = cache.acquireDecoded(
      otherKey, ANARI_UFIXED8_VEC4, 1, 1, vsr::io::RowOrder::TOP_DOWN, after);
  auto foreign = otherCache.acquireDecoded(
      source, ANARI_UFIXED8_VEC4, 1, 1, vsr::io::RowOrder::TOP_DOWN, before);
  const auto targetHandle = f.sceneIds->handleOf(target.texels.data());
  const auto sourceHandle = f.sceneIds->handleOf(different.texels.data());
  const auto s = f.attach();
  f.exportImage(s, 1, imageHandleText(sourceHandle));
  const auto exported = f.await(s, 1);
  REQUIRE(exported.str("outcome") == "ok");
  const auto version = *f.sceneIds->imageDataVersion(targetHandle);
  f.importImage(s,
      2,
      imageHandleText(targetHandle),
      version,
      exported.result()->get("file_name")->s);
  const auto imported = f.await(s, 2);
  REQUIRE(imported.str("outcome") == "ok");
  CHECK(std::memcmp(target.texels->data(), after, 4) == 0);
  CHECK(std::memcmp(foreign.texels->data(), before, 4) == 0);
  CHECK(std::memcmp(different.texels->data(), after, 4) == 0);
  CHECK(cache
            .acquireDecoded(source,
                ANARI_UFIXED8_VEC4,
                1,
                1,
                vsr::io::RowOrder::TOP_DOWN,
                before)
            .texels
      == target.texels);
  f.restoreBatch(s, 3, *imported.result()->get("batch_ref"));
  REQUIRE(f.await(s, 3).str("outcome") == "ok");
  CHECK(std::memcmp(target.texels->data(), before, 4) == 0);
  CHECK(std::memcmp(foreign.texels->data(), before, 4) == 0);
  CHECK(std::memcmp(different.texels->data(), after, 4) == 0);
}

SCENARIO(
    "Image binding and filesystem incompleteness remain independent on Import",
    "[integration][mcp][image-import][image-import-incomplete]")
{
  Fixture f({}, true, false, false, 120., true, UINT64_MAX, {}, true);
  auto image = f.scene.createArray(ANARI_UFIXED8, 1, 1);
  image->setName("tracked-image");
  const unsigned char before = 17;
  image->setData(static_cast<const void *>(&before));
  bool bindingComplete = true;
  SECTION("binding cap alone")
  {
    bindingComplete = false;
    for (unsigned i = 0; i < 300; ++i)
      f.scene.createObject<vsr::scene::Sampler>("image2D")->setParameterObject(
          "image", *image);
  }
  SECTION("quota scan cap alone")
  {
    for (unsigned i = 0; i < 4100; ++i)
      writeInput(f.exchangePath() + "/extra-" + std::to_string(i), {});
  }
  const auto s = f.attach();
  const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
  const auto handle = item.get("handle")->s;
  writeInput(f.exchangePath() + "/agent.png", inputPng(1, 1, 0, {0, 29}));
  f.importImage(s,
      2,
      handle,
      item.get("image_info")->get("data_version")->s,
      "agent.png");
  const auto imported = f.await(s, 2);
  REQUIRE(imported.str("outcome") == "ok");
  CHECK(imported.result()->get("affected_complete")->b == bindingComplete);
  CHECK(*static_cast<const unsigned char *>(image->data()) == 29);
  f.exportImage(s, 3, handle);
  const auto exported = f.await(s, 3);
  if (bindingComplete) {
    CHECK(exported.str("outcome") == "busy");
    CHECK(exported.result()->get("reason")->s == "resource_limit");
    json::Value metadata;
    REQUIRE(json::parse(f.images->metadata(f.sceneId, s.id), metadata));
    CHECK_FALSE(metadata.get("exchange_limits")->get("usage_complete")->b);
  } else {
    REQUIRE(exported.str("outcome") == "ok");
    CHECK_FALSE(exported.result()->get("affected_complete")->b);
    CHECK(exported.result()->get("exchange_limits")->get("usage_complete")->b);
  }
}

SCENARIO("Engine and Activity evict Image owners independently",
    "[integration][mcp][image-import][image-import-eviction]")
{
  operations::Limits engineLimits;
  engineLimits.statusRecords = 2;
  Fixture f(
      {}, true, false, false, 120., true, UINT64_MAX, {}, true, engineLimits);
  auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
  image->setName("tracked-image");
  const unsigned char before[] = {17, 29, 41, 0};
  image->setData(static_cast<const void *>(before));
  const auto s = f.attach();
  const auto item = f.inspectArray(s, 1).result()->get("items")->a.at(0);
  writeInput(
      f.exchangePath() + "/agent.png", inputPng(1, 1, 6, {0, 29, 17, 41, 0}));
  f.importImage(s,
      2,
      item.get("handle")->s,
      item.get("image_info")->get("data_version")->s,
      "agent.png");
  const auto imported = f.await(s, 2);
  REQUIRE(imported.str("outcome") == "ok");
  const auto batchJson = *imported.result()->get("batch_ref");
  const edit::BatchRef batch{
      batchJson.get("launch_id")->s, batchJson.get("batch_id")->s};
  SECTION("Engine evicts while Activity keeps recovery")
  {
    f.engine->setActivitySink(nullptr);
    const operations::Credentials credentials{
        f.authority->launchCredential(), s.id, s.secret};
    for (unsigned i = 3; i < 5; ++i) {
      operations::Request request;
      request.kind = "inspect";
      request.target = session::Target::scene(f.sceneId);
      request.executor = [](operations::StepContext &) {
        return operations::Step{};
      };
      REQUIRE(f.engine->submit(credentials, {s.id, i}, std::move(request)).code
          == Outcome::OK);
      f.drain();
    }
    CHECK(f.engine->status(credentials, {s.id, 2}).code
        == Outcome::OUTCOME_UNAVAILABLE);
    CHECK(
        f.imageActivity->eligibility(batch) == activity::Eligibility::ELIGIBLE);
    f.engine->setActivitySink(f.imageActivitySink.get());
    const auto local = f.restores->submitLocal({f.sceneId, batch});
    REQUIRE(local.code == Outcome::OK);
    f.drain();
    REQUIRE(f.restores->resultLocal(local.ref)->outcome == Outcome::OK);
    CHECK(std::memcmp(image->data(), before, 4) == 0);
  }
  SECTION("Metadata pressure cannot evict eligible recovery")
  {
    for (unsigned i = 0; i < 1024; ++i)
      REQUIRE(f.imageActivity->append(activity::Entry{}));
    CHECK(
        f.imageActivity->eligibility(batch) == activity::Eligibility::ELIGIBLE);
    CHECK(f.status(s, s.id, 2).str("outcome") == "ok");
    json::Value metadata;
    REQUIRE(json::parse(f.images->metadata(f.sceneId, s.id), metadata));
    CHECK(metadata.get("exchange_limits")
              ->get("session_retained_physical_bytes")
              ->n
        == 4);
    const auto local = f.restores->submitLocal({f.sceneId, batch});
    REQUIRE(local.code == Outcome::OK);
    f.drain();
    CHECK(f.restores->resultLocal(local.ref)->outcome == Outcome::OK);
    CHECK(std::memcmp(image->data(), before, 4) == 0);
  }
}

SCENARIO("MCP mixed Image Mesh history has no separate session ceiling",
    "[integration][mcp][mesh-import][mesh-pressure][mesh-pressure-session]")
{
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 128u << 20;
  PositionFixture f(limits);
  const auto a = f.attach();
  const auto evidence = f.working(a, "tracer", true);
  constexpr std::size_t BYTES = 16u << 20;
  const std::vector<unsigned char> before(BYTES, 71), rows(BYTES + 2048, 0);
  writeInput(f.exchangePath() + "/agent.png", inputPng(2048, 2048, 6, rows));
  std::vector<vsr::scene::ArrayRef> images;
  json::Value firstImageBatch, imageBatch;
  for (std::uint64_t id = 2; id < 6; ++id) {
    auto image = f.scene.createArray(ANARI_UFIXED8_VEC4, 2048, 2048);
    image->setData(static_cast<const void *>(before.data()));
    images.push_back(image);
    const auto handle = f.sceneIds->handleOf(image.data());
    f.importImage(a,
        id,
        f.objectHandle(image.data()),
        *f.sceneIds->imageDataVersion(handle),
        "agent.png");
    const auto imported = f.await(a, id);
    REQUIRE(imported.flag("committed"));
    imageBatch = *imported.result()->get("batch_ref");
    if (id == 2)
      firstImageBatch = imageBatch;
  }
  f.tool(a, "vsr_discover", "{\"request_id\":6}");
  const auto discovery = f.await(a, 6);
  const auto *capacity = discovery.result()
                             ->get("items")
                             ->a.at(0)
                             .get("image_exchange")
                             ->get("exchange_limits");
  CHECK(capacity->get("effective_global_retention_byte_cap")->n == 134217728);
  CHECK(capacity->get("session_retained_physical_bytes")->n == 4 * BYTES);
  f.importPositions(a, 7, *evidence.result());
  const auto mesh = f.await(a, 7);
  REQUIRE(mesh.flag("committed"));
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      != f.positions.data());
  f.restoreBatch(a, 8, firstImageBatch);
  REQUIRE(f.await(a, 8).flag("committed"));
  CHECK(std::memcmp(images.front()->data(), before.data(), BYTES) == 0);
  f.restoreBatch(a, 10, *mesh.result()->get("batch_ref"));
  REQUIRE(f.await(a, 10).flag("committed"));
  f.tool(a, "vsr_discover", "{\"request_id\":11}");
  const auto recovered = f.await(a, 11);
  CHECK(recovered.result()
            ->get("items")
            ->a.at(0)
            .get("image_exchange")
            ->get("exchange_limits")
            ->get("session_retained_physical_bytes")
            ->n
      == 3 * BYTES);
  REQUIRE(f.rpc("vsr/detach", "{}", &a).status == 200);
  const auto b = f.attach();
  f.restoreBatch(b, 1, imageBatch);
  REQUIRE(f.await(b, 1).flag("committed"));
  CHECK(std::memcmp(images.back()->data(), before.data(), BYTES) == 0);
  f.tool(b, "vsr_discover", "{\"request_id\":2}");
  const auto discovered = f.await(b, 2);
  CHECK(discovered.result()
            ->get("items")
            ->a.at(0)
            .get("image_exchange")
            ->get("exchange_limits")
            ->get("session_retained_physical_bytes")
            ->n
      == 0);
  f.exportMesh(b, 3, f.geometry.data());
  const auto fresh = f.await(b, 3);
  REQUIRE(fresh.str("outcome") == "ok");
  f.importPositions(b, 4, *fresh.result());
  const auto imported = f.await(b, 4);
  REQUIRE(imported.flag("committed"));
  f.restoreBatch(b, 5, *imported.result()->get("batch_ref"));
  REQUIRE(f.await(b, 5).flag("committed"));
  CHECK(std::memcmp(f.positions->data(),
            PositionFixture::POINTS,
            sizeof(PositionFixture::POINTS))
      == 0);
}

SCENARIO("MCP compound mesh ownership failures leave no published staging",
    "[integration][mcp][mesh-import][mesh-pressure][mesh-pressure-failures]")
{
  const auto seam = GENERATE(4096u, 65536u, 262144u);
  bool failing = false, preparing = false, refused = false;
  const auto memory = std::make_shared<vsr::rendering::FrameBudget>(16u << 20);
  image::ExchangeLimits limits;
  limits.reservePhysicalMemory = [&, memory](std::size_t bytes) {
    if (failing && bytes == 4096)
      preparing = true;
    if (failing
        && (seam == 262144u ? bytes > 327680u && bytes < 524288u
                            : bytes == seam && (seam != 65536u || preparing))) {
      failing = false;
      refused = true;
      return std::shared_ptr<const void>{};
    }
    return memory->reserve(bytes);
  };
  PositionFixture f(limits);
  f.geometry->setParameter("custom", 17.f);
  const auto s = f.attach();
  const auto evidence = f.working(s, "tracer", true);
  const auto revision = f.sceneIds->sceneRevision();
  const auto objects = f.scene.numberOfObjects(ANARI_ARRAY);
  failing = true;
  const auto admission = f.importPositions(s, 2, *evidence.result());
  if (admission.errorCode().empty()) {
    const auto rejected = f.await(s, 2);
    CHECK(rejected.str("outcome") == "busy");
    CHECK_FALSE(rejected.flag("committed"));
  } else
    CHECK(admission.errorCode() == "busy");
  CHECK(refused);
  CHECK(f.sceneIds->sceneRevision() == revision);
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == objects);
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK_FALSE(f.geometry->parameter("primitive.index"));
  CHECK(f.geometry->parameter("custom")->value().get<float>() == 17.f);
  failing = false;
  f.importPositions(s, 3, *evidence.result());
  const auto imported = f.await(s, 3);
  REQUIRE(imported.flag("committed"));
  const auto batch = *imported.result()->get("batch_ref");
  f.restores->setFaultInjector([] { return true; });
  f.restoreBatch(s, 4, batch);
  const auto busy = f.await(s, 4);
  CHECK(busy.str("outcome") == "busy");
  CHECK_FALSE(busy.flag("committed"));
  f.restores->setFaultInjector({});
  f.restoreBatch(s, 5, batch);
  REQUIRE(f.await(s, 5).flag("committed"));
  f.exportMesh(s, 6, f.geometry.data());
  CHECK(f.await(s, 6).str("outcome") == "ok");
}

SCENARIO(
    "MCP mesh input races and oversized carriers preserve the complete Scene",
    "[integration][mcp][mesh-import][mesh-pressure][mesh-pressure-files]")
{
  PositionFixture f;
  const auto s = f.attach();
  const auto evidence = f.working(s, "tracer", true);
  const auto path = f.exchangePath() + "/edited.vmesh";
  const auto envelope = [&](const std::string &header) {
    std::vector<unsigned char> bytes{'V', 'M', 'E', 'S', 'H', '0', '1', '\n'};
    const auto length = std::uint32_t(header.size());
    for (unsigned i = 0; i < 4; ++i)
      bytes.push_back((length >> (8 * i)) & 255);
    bytes.insert(bytes.end(), header.begin(), header.end());
    writeInput(path, bytes);
  };
  bool race = false;
  bool metadataPressure = false;
  std::atomic<bool> raceApplied{false};
  SECTION("unlink and replace the opened name")
  {
    race = true;
    afterImportRead = [&, path](int) {
      raceApplied = ::rename(path.c_str(), (path + ".old").c_str()) == 0
          && ::symlink((path + ".old").c_str(), path.c_str()) == 0;
    };
  }
  SECTION("the opened inode gains an external hard link")
  {
    race = true;
    afterImportRead = [&, path](int) {
      raceApplied = ::link(path.c_str(), (path + ".link").c_str()) == 0;
    };
  }
  SECTION("private file permissions change during read")
  {
    race = true;
    afterImportRead = [&](int fd) { raceApplied = ::fchmod(fd, 0400) == 0; };
  }
  SECTION("sparse oversized carrier is rejected before decode")
  {
    REQUIRE(::truncate(path.c_str(), (64u << 20) + 65536 + 13) == 0);
  }
  SECTION("structural expansion is bounded before JSON allocation")
  {
    std::string header =
        "{\"revision\":1,\"byte_order\":\"little\",\"fields\":{},\"arrays\":{},\"source\":[0";
    for (unsigned i = 0; i < 5000; ++i)
      header += ",0";
    envelope(header + "]}");
    metadataPressure = true;
  }
  SECTION("an oversized declared header cannot expand a tiny carrier")
  {
    writeInput(
        path, {'V', 'M', 'E', 'S', 'H', '0', '1', '\n', 255, 255, 255, 255});
  }
  SECTION("payload shape cannot allocate its declared missing bytes")
  {
    envelope(
        "{\"revision\":1,\"byte_order\":\"little\",\"fields\":{\"vertex.position\":{\"action\":\"replace\",\"enabled\":true,\"association\":\"vertex\",\"payload\":\"a\"}},\"arrays\":{\"a\":{\"element_type\":\"ANARI_FLOAT64_VEC3\",\"count\":18446744073709551615,\"byte_length\":18446744073709551615}}}");
  }
  SECTION("an archive external member is not an exchange field")
  {
    const std::string header =
        "{\"revision\":1,\"byte_order\":\"little\",\"fields\":{},\"arrays\":{},\"members\":[\"../../source.obj\"]}";
    envelope(header);
  }
  struct ClearReadHook
  {
    ~ClearReadHook()
    {
      interceptImportRead = false;
      afterImportRead = {};
    }
  } clear;
  interceptImportRead = race;
  const auto revision = f.sceneIds->sceneRevision();
  const auto arrayCount = f.scene.numberOfObjects(ANARI_ARRAY);
  f.importPositions(s, 2, *evidence.result());
  const auto rejected = f.await(s, 2);
  REQUIRE(rejected.str("outcome") == "invalid_request");
  CHECK_FALSE(rejected.flag("committed"));
  CHECK_FALSE(interceptImportRead.load());
  if (race)
    REQUIRE(raceApplied.load());
  CHECK(f.sceneIds->sceneRevision() == revision);
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == arrayCount);
  CHECK(f.geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK(f.other->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == f.positions.data());
  CHECK(std::memcmp(f.positions->data(),
            PositionFixture::POINTS,
            sizeof(PositionFixture::POINTS))
      == 0);
  CHECK_FALSE(f.geometry->parameter("primitive.index"));
  if (!race && std::filesystem::file_size(path) > (64u << 20))
    CHECK(rejected.result()->get("reason")->s == "mesh_file_limit");
  if (metadataPressure)
    CHECK(rejected.result()->get("reason")->s == "mesh_metadata_limit");
}

#include "MeshDeviceQualification.inc"
// Image qualification reuses the device/lifetime guards, not Mesh pixels.
#include "ImageDeviceQualification.inc"
#include "RetentionPressure.inc"

// Approval cancellation reuses the bounded read gate declared above.
#include "HdriCreationMcp.inc"
#include "HdriOutsideUseMcp.inc"
#include "HdriPersistenceMcp.inc"
#include "HdriRestoreMcp.inc"
#include "HdriSafetyMcp.inc"
// Pressure/final-guard/teardown tests reuse the real HDRI fixture above.
#include "HdriLifecycleMcp.inc"
#include "HdriPressureMcp.inc"
#include "RecoveryApprovalBoundaries.inc"
#include "RecoveryApprovalFailure.inc"
#include "RecoveryApprovalGeneric.inc"
#include "RecoveryApprovalMapBoundary.inc"
#include "RecoveryApprovalRefusals.inc"
#include "RecoveryApprovalWorkflows.inc"
#include "RetentionDelivery.inc"

#endif

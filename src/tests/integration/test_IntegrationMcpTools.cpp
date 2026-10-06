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
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/mcp/McpServer.hpp"
#include "vsr/integration/mcp/tools/EditTools.hpp"
#include "vsr/integration/mcp/tools/FrameTools.hpp"
#include "vsr/integration/mcp/tools/LookupTools.hpp"
#include "vsr/integration/mcp/tools/QueryTools.hpp"
#include "vsr/integration/mcp/tools/RenderTools.hpp"
#include "vsr/integration/render/ControllerCamera.hpp"
#include "vsr/io/images/ImageCache.hpp"
#include "vsr/rendering/index/RenderIndexAllLayers.hpp"
#include "vsr/rendering/pipeline/ImagePipeline.h"
#include "vsr/scene/Scene.hpp"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
#include <sys/socket.h>
#include <unistd.h>

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

struct WriteFaultObserver : vsr::scene::EmptyUpdateDelegate
{
  void signalArrayMapped(const vsr::scene::Array *) override;
  void signalArrayUnmapped(const vsr::scene::Array *) override;
  bool failMap{true};
  bool failUnmap{false};
};

void WriteFaultObserver::signalArrayMapped(const vsr::scene::Array *)
{
  if (failMap)
    throw std::runtime_error("downstream map failure");
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
  std::unique_ptr<operations::OperationEngine> engine;
  std::unique_ptr<inspection::Inspector> inspector;
  std::unique_ptr<edit::EditService> edits;
  std::unique_ptr<edit::TranslationService> translations;
  std::unique_ptr<restore::RestoreService> restores;
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
      std::uint64_t writeLimit = UINT64_MAX)
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
    hiddenId = *integration.registerScene(h);
    REQUIRE(integration.activate().ok());
    sceneIds = std::make_unique<identity::SceneIdentity>(ids, scene);
    authority =
        session::SessionAuthority::create(integration, [this] { return now; });
    engine = operations::OperationEngine::create(
        integration, *authority, {}, [this] { return now; });
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
    restores = std::make_unique<restore::RestoreService>(*engine, *edits);
    restores->attachScene(sceneId, *sceneIds);

    mcp::Config cfg;
    mcp::tools::addQueryTools(cfg.tools, {*engine, *inspector});
    mcp::tools::addLookupTools(cfg.tools, *engine);
    mcp::tools::addEditTools(
        cfg.tools, {ids, *edits, *restores, translations.get()});
    const auto st =
        mcp::McpServer::start(cfg, *authority, *engine, dir, server);
    if (!st.ok())
      FAIL("start failed: " << st.reason);
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
    while (integration.service() > 0) {
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
    CHECK(tool.get("name")->s != "vsr_image_export");
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
  layer->editableName() = "";
  layer->root()->value().name() = "named-root";
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
  layer->editableName() = "wire-name";
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

#endif

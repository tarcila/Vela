// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// Thin loopback checks for the discovery/inspection and lookup/cancel MCP
// tools: happy path, code mapping, capability policy and cross-session
// gates. Business rules are covered by the headless suites. Assertions read
// only machine-readable fields, never English messages.

#if defined(VSR_USE_INTEGRATION_MCP) && defined(__linux__)

// catch
#include "catch.hpp"
// vsr
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/mcp/McpServer.hpp"
#include "vsr/integration/mcp/tools/EditTools.hpp"
#include "vsr/integration/mcp/tools/LookupTools.hpp"
#include "vsr/integration/mcp/tools/QueryTools.hpp"
#include "vsr/integration/mcp/tools/RenderTools.hpp"
#include "vsr/rendering/index/RenderIndexAllLayers.hpp"
#include "vsr/scene/Scene.hpp"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
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
    char tmpl[] = "/tmp/vsr-mcp-tools-XXXXXX";
    REQUIRE(::mkdtemp(tmpl) != nullptr);
    path = tmpl;
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
  std::unique_ptr<restore::RestoreService> restores;
  bool hookOk{true};
  TempDir tmp;
  descriptor::RuntimeDirectory dir;
  std::unique_ptr<mcp::McpServer> server;

  explicit Fixture(inspection::PageLimits limits = {},
      bool editable = false,
      bool nodeEdits = false)
  {
    for (int i = 0; i < 5; i++)
      scene.createObject<vsr::scene::Material>("matte")->setParameter(
          "opacity", 1.f);
    SceneRegistration s;
    s.name = "scene";
    s.scene = &scene;
    s.grants.read = true;
    s.nodeEdits = nodeEdits;
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
    restores = std::make_unique<restore::RestoreService>(*engine, *edits);
    restores->attachScene(sceneId, *sceneIds);

    mcp::Config cfg;
    mcp::tools::addQueryTools(cfg.tools, {*engine, *inspector});
    mcp::tools::addLookupTools(cfg.tools, *engine);
    mcp::tools::addEditTools(cfg.tools, {ids, *edits, *restores});
    const auto st =
        mcp::McpServer::start(cfg, *authority, *engine, dir, server);
    if (!st.ok())
      FAIL("start failed: " << st.reason);
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
  std::unique_ptr<inspection::Inspector> inspector;
  TempDir tmp;
  descriptor::RuntimeDirectory dir;
  std::unique_ptr<mcp::McpServer> server;

  RenderFixture()
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
    const auto st =
        mcp::McpServer::start(cfg, *authority, *engine, dir, server);
    if (!st.ok())
      FAIL("start failed: " << st.reason);
  }

  ~RenderFixture()
  {
    server.reset();
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

#endif

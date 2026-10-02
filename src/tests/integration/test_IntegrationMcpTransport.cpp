// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// Real loopback requests against the embedded MCP server. Compiled to
// nothing unless VSR_USE_INTEGRATION_MCP is on.
//
// Environment:
//   VSR_TEST_ALLOW_NO_IPV6=1  lets the IPv6 scenario skip (with a WARN) when
//                             the host has no IPv6 loopback; without it that
//                             scenario fails instead.

#if defined(VSR_USE_INTEGRATION_MCP) && defined(__linux__)

// catch
#include "catch.hpp"
// vsr
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/mcp/McpServer.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
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

namespace {

struct TempDir
{
  std::string path;
  TempDir()
  {
    char tmpl[] = "/tmp/vsr-mcp-test-XXXXXX";
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
  std::string body;
  mcp::json::Value json;
  bool connected{false};

  std::string errorCode() const
  {
    const auto *e = json.get("error");
    const auto *d = e ? e->get("data") : nullptr;
    const auto *c = d ? d->get("code") : nullptr;
    return c && c->isString() ? c->s : std::string();
  }
  const mcp::json::Value *result() const
  {
    return json.get("result");
  }
};

int connectTo(bool v6, std::uint16_t port)
{
  const int fd = ::socket(v6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;
  int rc;
  if (v6) {
    sockaddr_in6 a{};
    a.sin6_family = AF_INET6;
    a.sin6_addr = in6addr_loopback;
    a.sin6_port = htons(port);
    rc = ::connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a));
  } else {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(port);
    rc = ::connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a));
  }
  if (rc != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

// Bounds on one reply read by the harness; exceeding either fails the test.
constexpr int replyDeadlineMs = 15000;
constexpr std::size_t replyByteCap = 1 << 20;

// Reads until EOF within replyDeadlineMs and replyByteCap, then parses.
Reply readReply(int fd)
{
  Reply r;
  r.connected = true;
  using clock = std::chrono::steady_clock;
  const auto deadline = clock::now() + std::chrono::milliseconds(replyDeadlineMs);
  std::string in;
  char buf[4096];
  for (;;) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - clock::now())
                          .count();
    if (left <= 0) {
      ::close(fd);
      FAIL("reply deadline exceeded");
    }
    pollfd p{fd, POLLIN, 0};
    const int pr = ::poll(&p, 1, int(left));
    if (pr < 0 && errno == EINTR)
      continue;
    if (pr == 0)
      continue; // re-check deadline
    const ssize_t n = pr < 0 ? -1 : ::recv(fd, buf, sizeof(buf), 0);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      break;
    in.append(buf, std::size_t(n));
    if (in.size() > replyByteCap) {
      ::close(fd);
      FAIL("reply byte cap exceeded");
    }
  }
  ::close(fd);
  if (in.size() > 12)
    r.status = std::atoi(in.c_str() + 9);
  const auto split = in.find("\r\n\r\n");
  if (split != std::string::npos)
    r.body = in.substr(split + 4);
  if (!r.body.empty())
    mcp::json::parse(r.body, r.json);
  return r;
}

// One request per TCP connection (exercises reconnects too).
Reply post(bool v6,
    std::uint16_t port,
    const std::vector<std::string> &headers,
    const std::string &body,
    const std::string &path = "/mcp")
{
  Reply r;
  const int fd = connectTo(v6, port);
  if (fd < 0)
    return r;
  r.connected = true;
  std::string req = "POST " + path + " HTTP/1.1\r\n";
  for (const auto &h : headers)
    req += h + "\r\n";
  req += "Content-Type: application/json\r\nConnection: close\r\n";
  req += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
  ::send(fd, req.data(), req.size(), MSG_NOSIGNAL);
  return readReply(fd);
}

std::string rpc(const std::string &method, const std::string &params = "{}")
{
  return "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"" + method
      + "\",\"params\":" + params + "}";
}

bool confirmReady(SceneId)
{
  return true;
}

struct Fixture
{
  vsr::scene::Scene scene;
  Integration integration;
  SceneId sceneId{INVALID_SCENE_ID};
  double now{1000.0};
  std::unique_ptr<session::SessionAuthority> authority;
  std::unique_ptr<operations::OperationEngine> engine;
  TempDir tmp;
  descriptor::RuntimeDirectory dir;
  std::unique_ptr<mcp::McpServer> server;
  std::mutex logMutex;
  std::vector<std::string> logs;
  bool v6{false};

  explicit Fixture(std::string host = "127.0.0.1",
      operations::Limits limits = {},
      std::function<void(mcp::Config &)> tweak = {})
  {
    v6 = host == "::1";
    SceneRegistration s;
    s.name = "scene";
    s.scene = &scene;
    s.grants.read = true; // no edit grant: forbidden probe below
    s.normalSyncHook = confirmReady;
    sceneId = *integration.registerScene(s);
    REQUIRE(integration.activate().ok());
    authority =
        session::SessionAuthority::create(integration, [this] { return now; });
    REQUIRE(authority);
    engine = operations::OperationEngine::create(
        integration, *authority, limits, [this] { return now; });
    REQUIRE(engine);
    REQUIRE(descriptor::openDirectory(tmp.path, {}, dir).ok());

    mcp::Config cfg;
    cfg.host = host;
    cfg.log = [this](std::string_view l) {
      std::lock_guard<std::mutex> lock(logMutex);
      logs.emplace_back(l);
    };
    mcp::Tool probe;
    probe.name = "probe_edit";
    probe.description = "authorization probe";
    probe.handler = [this](const session::Credentials &c,
                        const mcp::json::Value &) {
      mcp::ToolResult r;
      r.code = operations::fromSessionCode(authority->authorize(
          c, session::Target::scene(sceneId), session::Capability::EDIT));
      return r;
    };
    cfg.tools.push_back(probe);
    if (tweak)
      tweak(cfg);
    const auto st = mcp::McpServer::start(
        cfg, *authority, *engine, dir, server);
    if (!st.ok())
      FAIL("start failed: " << st.reason);
  }

  std::string hostHeader() const
  {
    return "Host: " + server->authority();
  }
  std::string authHeader() const
  {
    return "Authorization: Bearer " + authority->launchCredential();
  }

  Reply call(const std::string &body,
      std::vector<std::string> extra = {},
      bool withHost = true,
      bool withAuth = true)
  {
    std::vector<std::string> h;
    if (withHost)
      h.push_back(hostHeader());
    if (withAuth)
      h.push_back(authHeader());
    for (auto &e : extra)
      h.push_back(e);
    return post(v6, server->port(), h, body);
  }

  struct Session
  {
    std::string id;
    std::string secret;
    std::vector<std::string> headers() const
    {
      return {"VSR-Session-ID: " + id,
          "VSR-Session-Proof: Bearer " + secret};
    }
  };

  Session attach()
  {
    auto r = call(rpc("vsr/attach"));
    REQUIRE(r.status == 200);
    REQUIRE(r.result());
    Session s;
    s.id = r.result()->get("session_id")->s;
    s.secret = r.result()->get("session_secret")->s;
    return s;
  }

  void requireNoSecretsLogged(const std::vector<std::string> &secrets)
  {
    std::lock_guard<std::mutex> lock(logMutex);
    for (const auto &l : logs)
      for (const auto &s : secrets)
        REQUIRE(l.find(s) == std::string::npos);
  }
};

} // namespace

SCENARIO("MCP listener publishes a descriptor matching the bound endpoint",
    "[integration]")
{
  Fixture f;
  descriptor::Descriptor d;
  REQUIRE(descriptor::read(f.dir, f.server->published().fileName(), d).ok());
  REQUIRE(d.host == "127.0.0.1");
  REQUIRE(d.port == f.server->port());
  REQUIRE(d.launchId == f.authority->launchId());
  REQUIRE(f.server->authority()
      == "127.0.0.1:" + std::to_string(f.server->port()));

  // IPv4-only: nothing listens on ::1 at this port.
  REQUIRE(connectTo(true, f.server->port()) < 0);

  auto r = f.call(rpc("initialize",
      "{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},"
      "\"clientInfo\":{\"name\":\"t\",\"version\":\"1\"}}"));
  REQUIRE(r.status == 200);
  REQUIRE(r.result()->get("protocolVersion")->s == mcp::protocolVersion);

  const std::string path = f.server->published().path();
  f.server->stop();
  REQUIRE(::access(path.c_str(), F_OK) != 0);
}

SCENARIO("MCP rejects invalid listener hosts before publishing",
    "[integration]")
{
  Fixture f; // for authority/engine/dir
  for (const char *h :
      {"0.0.0.0", "::", "localhost", "::ffff:127.0.0.1", "127.0.0.2"}) {
    mcp::Config cfg;
    cfg.host = h;
    std::unique_ptr<mcp::McpServer> s;
    auto st = mcp::McpServer::start(cfg, *f.authority, *f.engine, f.dir, s);
    REQUIRE(st.code == mcp::StartCode::INVALID_CONFIG);
    REQUIRE(!s);
  }
  // Same launch already published: a second start fails closed.
  mcp::Config cfg;
  std::unique_ptr<mcp::McpServer> s;
  auto st = mcp::McpServer::start(cfg, *f.authority, *f.engine, f.dir, s);
  REQUIRE(st.code == mcp::StartCode::PUBLISH_FAILED);
  REQUIRE(!s);
}

SCENARIO("MCP enforces exact Host authority and rejects Origin",
    "[integration]")
{
  Fixture f;
  const std::string port = std::to_string(f.server->port());
  const std::string body = rpc("ping");

  REQUIRE(f.call(body).status == 200);
  for (const std::string &bad : std::vector<std::string>{"localhost:" + port,
           "[::1]:" + port,
           std::string("127.0.0.1"),
           "127.0.0.1:" + port + "0",
           "[::ffff:127.0.0.1]:" + port,
           "127.1:" + port,
           "LOCALHOST:" + port}) {
    auto r = f.call(body, {"Host: " + bad}, false);
    REQUIRE(r.status == 400);
    REQUIRE(r.errorCode() == "invalid_host");
  }
  // Missing Host, duplicate Host.
  REQUIRE(f.call(body, {}, false).status == 400);
  REQUIRE(f.call(body, {f.hostHeader()}).status == 400);
  // Forwarded-host substitution.
  REQUIRE(f.call(body, {"X-Forwarded-Host: " + f.server->authority()}).status
      == 400);
  REQUIRE(f.call(body, {"Forwarded: host=example"}).status == 400);
  // Origin-bearing requests, any value, header name case-insensitive.
  auto o = f.call(body, {"oRiGiN: http://" + f.server->authority()});
  REQUIRE(o.status == 403);
  REQUIRE(o.errorCode() == "forbidden_origin");
  // Header name case-insensitivity on Host.
  REQUIRE(f.call(body, {"hOsT: " + f.server->authority()}, false).status
      == 200);
  // Only /mcp, never redirected.
  auto wrongPath = post(false,
      f.server->port(),
      {f.hostHeader(), f.authHeader()},
      body,
      "/other");
  REQUIRE(wrongPath.status == 404);
}

SCENARIO("MCP launch authentication and session proof carry frozen codes",
    "[integration]")
{
  Fixture f;
  // Missing/wrong launch credential, including for initialize.
  auto r = f.call(rpc("initialize"), {}, true, false);
  REQUIRE(r.status == 401);
  REQUIRE(r.errorCode() == "unauthenticated");
  r = f.call(rpc("vsr/attach"), {"Authorization: Bearer wrong"}, true, false);
  REQUIRE(r.errorCode() == "unauthenticated");

  auto s = f.attach();
  REQUIRE(s.id.size() == 32);
  REQUIRE(s.secret.size() == 43);

  // Second attach: coarse occupied without incumbent data.
  r = f.call(rpc("vsr/attach"));
  REQUIRE(r.errorCode() == "occupied");
  REQUIRE(r.body.find(s.id) == std::string::npos);

  // Session-scoped calls need the proof.
  r = f.call(rpc("vsr/renew"));
  REQUIRE(r.errorCode() == "invalid_session_proof");
  r = f.call(rpc("vsr/renew"),
      {"VSR-Session-ID: " + s.id, "VSR-Session-Proof: Bearer wrong"});
  REQUIRE(r.status == 401);
  REQUIRE(r.errorCode() == "invalid_session_proof");
  r = f.call(rpc("tools/call", "{\"name\":\"probe_edit\"}"));
  REQUIRE(r.errorCode() == "invalid_session_proof");

  // Valid proof, missing capability: forbidden.
  r = f.call(rpc("tools/call", "{\"name\":\"probe_edit\"}"), s.headers());
  REQUIRE(r.status == 403);
  REQUIRE(r.errorCode() == "forbidden");

  // The secret is private bootstrap data, never a tool.
  r = f.call(rpc("tools/list"));
  REQUIRE(r.status == 200);
  REQUIRE(r.body.find("attach") == std::string::npos);

  f.requireNoSecretsLogged({f.authority->launchCredential(), s.secret});
}

SCENARIO("MCP renew and detach across reconnects; old proof never revives",
    "[integration]")
{
  Fixture f;
  auto s = f.attach();
  // Each call is a fresh TCP connection.
  REQUIRE(f.call(rpc("vsr/renew"), s.headers()).status == 200);
  f.now += 100.0;
  REQUIRE(f.call(rpc("vsr/renew"), s.headers()).status == 200);
  f.now += 100.0; // within the renewed lease
  REQUIRE(f.call(rpc("vsr/renew"), s.headers()).status == 200);

  REQUIRE(f.call(rpc("vsr/detach"), s.headers()).status == 200);
  auto r = f.call(rpc("vsr/renew"), s.headers());
  REQUIRE(r.errorCode() == "invalid_session_proof");

  auto s2 = f.attach();
  REQUIRE(s2.id != s.id);
  REQUIRE(s2.secret != s.secret);
  REQUIRE(f.call(rpc("vsr/renew"), s.headers()).errorCode()
      == "invalid_session_proof");
  REQUIRE(f.call(rpc("vsr/renew"), s2.headers()).status == 200);

  // Ordinary traffic does not renew; expiry ends the session.
  f.now += 121.0;
  REQUIRE(f.call(rpc("ping"), s2.headers()).status == 200);
  r = f.call(rpc("vsr/renew"), s2.headers());
  REQUIRE(r.status == 401);
  REQUIRE(r.errorCode() == "session_expired");
}

SCENARIO("MCP renew and detach stay serviceable with the scene pool full",
    "[integration]")
{
  operations::Limits limits;
  limits.sceneSlots = 1;
  Fixture f("127.0.0.1", limits);
  auto s = f.attach();

  operations::Request q;
  q.kind = "test";
  q.target = session::Target::scene(f.sceneId);
  q.canonicalArguments = "{}";
  q.executor = [](operations::StepContext &) { return operations::Step{}; };
  REQUIRE(f.engine->submitLocal(q).code == Outcome::OK);
  REQUIRE(f.engine->submitLocal(q).code == Outcome::BUSY);
  REQUIRE(f.engine->slotsInUse() == 1);

  REQUIRE(f.call(rpc("vsr/renew"), s.headers()).status == 200);
  REQUIRE(f.call(rpc("vsr/detach"), s.headers()).status == 200);
  f.integration.service();
}

SCENARIO("MCP IPv6 listener is IPv6-only on ::1", "[integration]")
{
  // Fails where the host has no IPv6 loopback unless explicitly allowed.
  const int probe = ::socket(AF_INET6, SOCK_STREAM, 0);
  sockaddr_in6 a{};
  a.sin6_family = AF_INET6;
  a.sin6_addr = in6addr_loopback;
  const bool haveV6 = probe >= 0
      && ::bind(probe, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0;
  if (probe >= 0)
    ::close(probe);
  if (!haveV6) {
    const char *allow = std::getenv("VSR_TEST_ALLOW_NO_IPV6");
    if (allow && std::strcmp(allow, "1") == 0) {
      WARN("no IPv6 loopback; skipping (VSR_TEST_ALLOW_NO_IPV6=1)");
      return;
    }
    FAIL("no IPv6 loopback; set VSR_TEST_ALLOW_NO_IPV6=1 to skip");
  }

  Fixture f("::1");
  descriptor::Descriptor d;
  REQUIRE(descriptor::read(f.dir, f.server->published().fileName(), d).ok());
  REQUIRE(d.host == "::1");
  REQUIRE(f.server->authority() == "[::1]:" + std::to_string(f.server->port()));
  REQUIRE(connectTo(false, f.server->port()) < 0);
  REQUIRE(f.call(rpc("ping")).status == 200);
  auto r = f.call(rpc("ping"),
      {"Host: 127.0.0.1:" + std::to_string(f.server->port())},
      false);
  REQUIRE(r.status == 400);
}

SCENARIO("MCP JSON numbers are locale-independent and never throw",
    "[integration]")
{
  mcp::json::Value v;
  REQUIRE(mcp::json::parse("1.5", v));
  REQUIRE(v.isNumber());
  REQUIRE(v.n == 1.5);
  REQUIRE(mcp::json::parse("-2.5e-3", v));
  REQUIRE(v.n == -2.5e-3);
  REQUIRE(!mcp::json::parse("1e9999", v));
  REQUIRE(!mcp::json::parse("-1e9999", v));
  REQUIRE(!mcp::json::parse("[1,1e400]", v));

  Fixture f;
  auto r = f.call(
      "{\"jsonrpc\":\"2.0\",\"id\":1e9999,\"method\":\"ping\"}");
  REQUIRE(r.status == 400);
  REQUIRE(r.json.get("error"));
  REQUIRE(r.json.get("error")->get("code")->n == -32700);
  r = f.call(rpc("tools/call", "{\"name\":\"probe_edit\",\"arguments\":"
                              "{\"x\":1e9999}}"));
  REQUIRE(r.status == 400);
  REQUIRE(f.call(rpc("ping")).status == 200);
}

SCENARIO("MCP answers an oversized Content-Length with 413 before the body",
    "[integration]")
{
  Fixture f;
  const int fd = connectTo(false, f.server->port());
  REQUIRE(fd >= 0);
  // No body is sent: the server must answer without waiting for it.
  const std::string req = "POST /mcp HTTP/1.1\r\n" + f.hostHeader() + "\r\n"
      + f.authHeader()
      + "\r\nContent-Type: application/json\r\nContent-Length: "
      + std::to_string(64 * 1024 + 1) + "\r\n\r\n";
  ::send(fd, req.data(), req.size(), MSG_NOSIGNAL);
  const auto t0 = std::chrono::steady_clock::now();
  auto r = readReply(fd);
  REQUIRE(r.status == 413);
  REQUIRE(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(5));
  REQUIRE(f.call(rpc("ping")).status == 200);
}

SCENARIO("MCP releases slow-drip connections at the request deadline",
    "[integration]")
{
  constexpr std::size_t slots = 4;
  constexpr int deadlineMs = 500;
  Fixture f("127.0.0.1", {}, [](mcp::Config &c) {
    c.maxConnections = slots;
    c.requestDeadlineMs = deadlineMs;
  });

  // Occupy every slot with unauthenticated, never-finished headers.
  std::vector<int> fds;
  const std::string start = "POST /mcp HTTP/1.1\r\nX-Drip: ";
  for (std::size_t i = 0; i < slots; i++) {
    const int fd = connectTo(false, f.server->port());
    REQUIRE(fd >= 0);
    ::send(fd, start.data(), start.size(), MSG_NOSIGNAL);
    fds.push_back(fd);
  }
  // Slots are full: a legitimate client is turned away for now.
  REQUIRE(f.call(rpc("ping")).status == 503);

  // Trickle one byte per slot every 50 ms until the server drops them all.
  using clock = std::chrono::steady_clock;
  const auto t0 = clock::now();
  std::size_t open = fds.size();
  while (open && clock::now() - t0 < std::chrono::seconds(5)) {
    for (int &fd : fds) {
      if (fd < 0)
        continue;
      pollfd p{fd, POLLIN, 0};
      if (::poll(&p, 1, 0) > 0) { // reply or EOF: server released it
        ::close(fd);
        fd = -1;
        open--;
        continue;
      }
      const char c = 'a';
      ::send(fd, &c, 1, MSG_NOSIGNAL);
    }
    ::usleep(50 * 1000);
  }
  for (int fd : fds)
    if (fd >= 0)
      ::close(fd);
  REQUIRE(open == 0);
  REQUIRE(clock::now() - t0 < std::chrono::milliseconds(deadlineMs * 4));

  // The released slots serve a legitimate client again.
  REQUIRE(f.call(rpc("ping")).status == 200);
}

SCENARIO("MCP frees the only slot before a sequential client can reconnect",
    "[integration]")
{
  // Each call sends Connection: close, reads to EOF and reconnects at once.
  // Addendum 11: the slot is free no later than the close the client sees.
  Fixture f("127.0.0.1", {}, [](mcp::Config &c) { c.maxConnections = 1; });
  for (int i = 0; i < 50; i++) {
    INFO("round trip " << i);
    REQUIRE(f.call(rpc("ping")).status == 200);
  }
}

// Holds one connection whose client reads a large tools/list response one
// byte every 10 ms, so the server stays inside sendAll for a long time.
struct SlowReader
{
  using clock = std::chrono::steady_clock;
  int fd{-1};
  std::atomic<bool> stop{false};
  std::atomic<bool> firstByte{false};
  std::thread reader;

  static void configure(mcp::Config &c, int deadlineMs)
  {
    c.maxConnections = 1;
    c.responseDeadlineMs = deadlineMs;
    // Far larger than any loopback socket buffering, so the response can
    // only complete if the client actually reads it.
    mcp::Tool big;
    big.name = "big";
    big.description = std::string(32u << 20, 'x');
    big.handler = [](const session::Credentials &, const mcp::json::Value &) {
      return mcp::ToolResult{};
    };
    c.tools.push_back(std::move(big));
  }

  // Connects, sends the request and waits (bounded) for the first byte.
  bool start(Fixture &f)
  {
    fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
      return false;
    int rcvbuf = 4096;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(f.server->port());
    if (::connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0)
      return false;
    const std::string body = rpc("tools/list");
    const std::string req = "POST /mcp HTTP/1.1\r\n" + f.hostHeader() + "\r\n"
        + f.authHeader()
        + "\r\nContent-Type: application/json\r\nConnection: close\r\n"
          "Content-Length: "
        + std::to_string(body.size()) + "\r\n\r\n" + body;
    ::send(fd, req.data(), req.size(), MSG_NOSIGNAL);
    reader = std::thread([this] {
      const auto limit = clock::now() + std::chrono::seconds(90);
      while (!stop && clock::now() < limit) {
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, 10) > 0) {
          char c;
          if (::recv(fd, &c, 1, 0) <= 0)
            break;
          firstByte = true;
        }
        ::usleep(10 * 1000);
      }
    });
    // The first byte is in the client's receive buffer as soon as the server
    // thread's first send() lands, which needs no client progress; 10 s only
    // bounds a wedged server and is far above any loaded-host scheduling delay.
    // A longer wait would not help the short-deadline scenario anyway: once the
    // server's response deadline passes it closes the socket and the reader
    // thread exits without a first byte.
    const auto t0 = clock::now();
    while (!firstByte && clock::now() - t0 < std::chrono::seconds(10))
      ::usleep(5 * 1000);
    return firstByte;
  }

  ~SlowReader()
  {
    stop = true;
    if (reader.joinable())
      reader.join();
    if (fd >= 0)
      ::close(fd);
  }
};

SCENARIO("MCP rejects a healthy client while a slow send holds the only slot",
    "[integration]")
{
  // A deadline far beyond the test's runtime, so the slot is provably still
  // held when the ping arrives regardless of scheduling.
  Fixture f("127.0.0.1", {}, [](mcp::Config &c) {
    SlowReader::configure(c, 120000);
  });
  {
    SlowReader slow;
    REQUIRE(slow.start(f));
    REQUIRE(f.call(rpc("ping")).status == 503);
  }
  // Closing the slow client ends the send with an error and frees the slot.
  using clock = std::chrono::steady_clock;
  const auto t0 = clock::now();
  int status = 0;
  while (clock::now() - t0 < std::chrono::seconds(10)) {
    status = f.call(rpc("ping")).status;
    if (status == 200)
      break;
    ::usleep(50 * 1000);
  }
  REQUIRE(status == 200);
}

SCENARIO("MCP frees a slow-reader connection at the response deadline",
    "[integration]")
{
  constexpr int deadlineMs = 500;
  Fixture f("127.0.0.1", {}, [](mcp::Config &c) {
    SlowReader::configure(c, deadlineMs);
  });
  SlowReader slow;
  REQUIRE(slow.start(f));
  using clock = std::chrono::steady_clock;
  const auto sendStart = clock::now();

  // No held-slot probe here: the server's response deadline starts when it
  // begins writing, before the first byte reaches this client, so no client
  // clock can place a probe reliably before that deadline. "The slow send
  // holds the only slot" is asserted deterministically by the 120 s scenario
  // "MCP rejects a healthy client while a slow send holds the only slot".
  // This scenario asserts only the upper bound: the slot frees within the
  // response deadline (plus scheduling slack) while the reader is still
  // reading, which a 32 MiB body at one byte per 10 ms guarantees.
  int status = 0;
  while (clock::now() - sendStart < std::chrono::milliseconds(deadlineMs * 6)) {
    status = f.call(rpc("ping")).status;
    if (status == 200)
      break;
    ::usleep(50 * 1000);
  }
  INFO("elapsed ms: "
      << std::chrono::duration_cast<std::chrono::milliseconds>(
             clock::now() - sendStart)
             .count());
  REQUIRE(status == 200);
  // The slot is free as soon as the previous reply's close is visible
  // (addendum 11), so an immediate reconnect is served too.
  REQUIRE(f.call(rpc("ping")).status == 200);
}

#endif

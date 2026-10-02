// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsrMcpProxy driven over pipes against an in-process MCP server with a
// controlled lease clock, plus fake loopback endpoints for the proxy's
// local limits (byte cap, deadline, reattach failure, ownership). Compiled to nothing unless VSR_USE_INTEGRATION_MCP
// is on.

#if defined(VSR_USE_INTEGRATION_MCP) && defined(__linux__)

// catch
#include "catch.hpp"
// vsr
#include "../../apps/tools/McpProxy.hpp"
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
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>
// posix
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace vsr::integration;
namespace proxy = vsr::tools::mcp_proxy;

namespace {

struct TempDir
{
  std::string path;
  TempDir()
  {
    char tmpl[] = "/tmp/vsr-mcp-proxy-test-XXXXXX";
    REQUIRE(::mkdtemp(tmpl) != nullptr);
    path = tmpl;
  }
  ~TempDir()
  {
    std::string cmd = "rm -rf '" + path + "'";
    (void)std::system(cmd.c_str());
  }
};

bool confirmReady(SceneId)
{
  return true;
}

// Lease timing; the renewal test drives it with a fake clock.
constexpr double leaseSeconds = 0.6;
constexpr double renewalSeconds = 0.15;

struct Server
{
  vsr::scene::Scene scene;
  Integration integration;
  std::unique_ptr<session::SessionAuthority> authority;
  std::unique_ptr<operations::OperationEngine> engine;
  TempDir tmp;
  descriptor::RuntimeDirectory dir;
  std::unique_ptr<mcp::McpServer> server;

  explicit Server(session::MonotonicClock clock = session::steadyClock())
  {
    SceneRegistration s;
    s.name = "scene";
    s.scene = &scene;
    s.grants.read = true;
    s.normalSyncHook = confirmReady;
    REQUIRE(integration.registerScene(s));
    SessionTiming timing;
    timing.leaseSeconds = leaseSeconds;
    timing.renewalSeconds = renewalSeconds;
    REQUIRE(integration.activate(timing).ok());
    authority = session::SessionAuthority::create(integration, clock);
    REQUIRE(authority);
    engine = operations::OperationEngine::create(integration, *authority);
    REQUIRE(engine);
    REQUIRE(descriptor::openDirectory(tmp.path, {}, dir).ok());
    mcp::Config cfg;
    mcp::Tool t;
    t.name = "echo_probe";
    t.description = "session-scoped probe";
    t.handler = [](const session::Credentials &, const mcp::json::Value &) {
      mcp::ToolResult r;
      r.contentJson = "[{\"type\":\"text\",\"text\":\"ok\"}]";
      return r;
    };
    cfg.tools.push_back(t);
    const auto st =
        mcp::McpServer::start(cfg, *authority, *engine, dir, server);
    if (!st.ok())
      FAIL("start failed: " << st.reason);
  }

  std::string descriptorPath() const
  {
    return server->published().path();
  }
};

struct Pipe
{
  int r{-1}, w{-1};
  Pipe()
  {
    int p[2];
    REQUIRE(::pipe2(p, O_CLOEXEC) == 0);
    r = p[0];
    w = p[1];
  }
  ~Pipe()
  {
    closeRead();
    closeWrite();
  }
  void closeRead()
  {
    if (r >= 0)
      ::close(std::exchange(r, -1));
  }
  void closeWrite()
  {
    if (w >= 0)
      ::close(std::exchange(w, -1));
  }
};

// Proxy::run on its own thread, wired to pipes like a Pi child process.
struct Harness
{
  Server &server;
  Pipe in, out, err, wake;
  std::unique_ptr<proxy::Proxy> p;
  std::thread thread;
  int exitCode{-1};
  std::string outBuf;

  explicit Harness(Server &s, proxy::Options opts = {}) : server(s)
  {
    descriptor::Descriptor d;
    REQUIRE(descriptor::read(s.descriptorPath(), d).ok());
    p = std::make_unique<proxy::Proxy>(d, std::move(opts));
    thread = std::thread([this] {
      exitCode = p->run(in.r, out.w, err.w, wake.r);
      out.closeWrite();
      err.closeWrite();
    });
  }
  ~Harness()
  {
    in.closeWrite();
    if (thread.joinable())
      thread.join();
  }

  void send(const std::string &line)
  {
    const std::string l = line + "\n";
    REQUIRE(::write(in.w, l.data(), l.size()) == ssize_t(l.size()));
  }

  // Next stdout line, or empty after 5 s.
  std::string readLine()
  {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
      if (auto nl = outBuf.find('\n'); nl != std::string::npos) {
        std::string l = outBuf.substr(0, nl);
        outBuf.erase(0, nl + 1);
        return l;
      }
      if (std::chrono::steady_clock::now() > deadline)
        return {};
      pollfd pfd{out.r, POLLIN, 0};
      if (::poll(&pfd, 1, 100) <= 0)
        continue;
      char buf[4096];
      const ssize_t n = ::read(out.r, buf, sizeof(buf));
      if (n <= 0)
        return {};
      outBuf.append(buf, std::size_t(n));
    }
  }

  mcp::json::Value request(const std::string &line)
  {
    send(line);
    mcp::json::Value v;
    const auto l = readLine();
    REQUIRE(mcp::json::parse(l, v));
    return v;
  }

  // Close stdin (EOF), wait for exit, and return everything left on
  // stdout and stderr.
  std::pair<std::string, std::string> finish()
  {
    in.closeWrite();
    thread.join();
    std::string o = outBuf, e;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(out.r, buf, sizeof(buf))) > 0)
      o.append(buf, std::size_t(n));
    while ((n = ::read(err.r, buf, sizeof(buf))) > 0)
      e.append(buf, std::size_t(n));
    return {o, e};
  }
};

std::string rpc(int id, const std::string &method, const std::string &params = "{}")
{
  return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id)
      + ",\"method\":\"" + method + "\",\"params\":" + params + "}";
}

bool waitFor(const std::function<bool()> &cond)
{
  for (int i = 0; i < 100; i++) {
    if (cond())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return cond();
}

// A scripted loopback endpoint: one connection at a time; the handler sees
// each request body and returns the raw HTTP bytes to send, or nullopt to
// never answer. The connection is left open after the answer until the
// client closes it, so only Content-Length can end the read.
struct FakeEndpoint
{
  using Handler = std::function<std::optional<std::string>(const std::string &)>;
  int listenFd{-1};
  int port{0};
  Handler handler;
  std::atomic<bool> stop{false};
  std::atomic<int> connections{0};
  std::atomic<std::size_t> bytesReceived{0};
  std::mutex mutex;
  std::vector<std::string> bodies;
  std::thread thread;

  explicit FakeEndpoint(Handler h) : handler(std::move(h))
  {
    listenFd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    REQUIRE(listenFd >= 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(::bind(listenFd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0);
    REQUIRE(::listen(listenFd, 8) == 0);
    socklen_t len = sizeof(a);
    REQUIRE(::getsockname(listenFd, reinterpret_cast<sockaddr *>(&a), &len) == 0);
    port = ntohs(a.sin_port);
    thread = std::thread([this] { loop(); });
  }
  ~FakeEndpoint()
  {
    stop = true;
    thread.join();
    ::close(listenFd);
  }

  descriptor::Descriptor descriptor() const
  {
    descriptor::Descriptor d;
    d.launchId = "0123456789abcdef0123456789abcdef";
    d.host = "127.0.0.1";
    d.port = std::uint32_t(port);
    d.launchCredential = "fake-launch-credential-value";
    return d;
  }

  std::vector<std::string> received()
  {
    std::lock_guard<std::mutex> lock(mutex);
    return bodies;
  }

  static std::string reply(const std::string &body)
  {
    return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
           "Content-Length: "
        + std::to_string(body.size()) + "\r\n\r\n" + body;
  }

  static std::string attachOk()
  {
    return reply(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"session_id\":\"s\","
        "\"session_secret\":\"x\",\"lease_seconds\":120,"
        "\"renewal_seconds\":30}}");
  }

 private:
  bool readable(int fd)
  {
    while (!stop) {
      pollfd p{fd, POLLIN, 0};
      if (::poll(&p, 1, 20) > 0)
        return true;
    }
    return false;
  }

  void loop()
  {
    while (readable(listenFd)) {
      const int c = ::accept4(listenFd, nullptr, nullptr, SOCK_CLOEXEC);
      if (c < 0)
        continue;
      connections++;
      serve(c);
      ::close(c);
    }
  }

  void serve(int c)
  {
    std::string in;
    char buf[4096];
    std::size_t end = std::string::npos, want = 0;
    while (readable(c)) {
      const ssize_t n = ::recv(c, buf, sizeof(buf), 0);
      if (n <= 0)
        return;
      bytesReceived += std::size_t(n);
      in.append(buf, std::size_t(n));
      if (end == std::string::npos
          && (end = in.find("\r\n\r\n")) != std::string::npos) {
        const auto cl = in.find("Content-Length: ");
        want = end + 4
            + (cl < end ? std::stoul(in.substr(cl + 16)) : std::size_t(0));
      }
      if (end != std::string::npos && in.size() >= want)
        break;
    }
    if (stop)
      return;
    const std::string body = in.substr(end + 4);
    {
      std::lock_guard<std::mutex> lock(mutex);
      bodies.push_back(body);
    }
    if (auto out = handler(body)) {
      std::size_t done = 0;
      while (done < out->size()) {
        const ssize_t n = ::send(
            c, out->data() + done, out->size() - done, MSG_NOSIGNAL);
        if (n <= 0)
          return;
        done += std::size_t(n);
      }
    }
    // Hold the connection open until the client closes it (or stop).
    while (readable(c))
      if (::recv(c, buf, sizeof(buf), 0) <= 0)
        return;
  }
};

bool isAttach(const std::string &body)
{
  return body.find("\"vsr/attach\"") != std::string::npos;
}

std::string errorCode(const std::string &line)
{
  mcp::json::Value v;
  REQUIRE(mcp::json::parse(line, v));
  const auto *e = v.get("error");
  REQUIRE(e);
  return e->get("data")->get("code")->s;
}

} // namespace

SCENARIO("vsrMcpProxy forwards MCP, hides vsr/*, renews, detaches on EOF",
    "[integration]")
{
  // One fake monotonic clock drives both the session lease and the proxy's
  // renewal timer: no wall-clock sleeps decide the outcome.
  auto now = std::make_shared<std::atomic<double>>(1000.0);
  Server s([now] { return now->load(); });
  proxy::Options opts;
  opts.clock = [now] { return now->load(); };
  Harness h(s, opts);
  REQUIRE(waitFor([&] { return s.authority->hasSession(); }));
  std::string transcript;

  // initialize and tools/list pass through.
  auto v = h.request(rpc(1,
      "initialize",
      "{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},"
      "\"clientInfo\":{\"name\":\"t\",\"version\":\"1\"}}"));
  REQUIRE(v.get("result"));
  REQUIRE(v.get("result")->get("protocolVersion")->s == mcp::protocolVersion);
  transcript += mcp::json::write(v);
  h.send("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}");

  v = h.request(rpc(2, "tools/list"));
  const auto *tools = v.get("result")->get("tools");
  REQUIRE(tools);
  REQUIRE(tools->a.size() == 1);
  REQUIRE(tools->a[0].get("name")->s == "echo_probe");
  REQUIRE(v.get("id")->n == 2);
  transcript += mcp::json::write(v);

  // vsr/* is private to the proxy: refused locally, never forwarded.
  for (const char *m : {"vsr/attach", "vsr/renew", "vsr/detach"}) {
    v = h.request(rpc(3, m));
    REQUIRE(v.get("error"));
    REQUIRE(v.get("error")->get("code")->n == -32601);
    REQUIRE(!v.get("result"));
    transcript += mcp::json::write(v);
  }
  REQUIRE(s.authority->hasSession());

  // Idle for more than two leases: the timer alone keeps the session. Step
  // the clock one renewal period at a time and wait (bounded poll) for each
  // renewal before the next step.
  const int steps = int(2.5 * leaseSeconds / renewalSeconds) + 1;
  for (int i = 0; i < steps; i++) {
    const auto before = h.p->renewals();
    // A hair past one period, so float rounding cannot hold a renewal back.
    now->store(now->load() + renewalSeconds + 1e-6);
    h.p->nudge();
    REQUIRE(waitFor([&] { return h.p->renewals() == before + 1; }));
    s.authority->expireIfDue();
    REQUIRE(s.authority->hasSession());
  }
  REQUIRE(steps * renewalSeconds > 2.0 * leaseSeconds);
  REQUIRE(h.p->renewals() == std::uint64_t(steps));

  // Session-scoped call still succeeds with the held proof.
  v = h.request(rpc(4, "tools/call", "{\"name\":\"echo_probe\"}"));
  REQUIRE(v.get("result"));
  REQUIRE(v.get("result")->get("isError")->b == false);
  transcript += mcp::json::write(v);

  // EOF detaches.
  auto [out, err] = h.finish();
  REQUIRE(h.exitCode == 0);
  REQUIRE(!s.authority->hasSession());
  transcript += out + err;
  REQUIRE(err.find("detached") != std::string::npos);

  // No secret on stdout or stderr. The session secret is unknown to the
  // test, so check the launch credential and every proof-shaped token.
  REQUIRE(transcript.find(s.authority->launchCredential()) == std::string::npos);
  REQUIRE(transcript.find("session_secret") == std::string::npos);
  REQUIRE(transcript.find("session_id") == std::string::npos);
  REQUIRE(transcript.find("Bearer") == std::string::npos);
}

SCENARIO("vsrMcpProxy detaches on the wake signal and reattaches fresh",
    "[integration]")
{
  Server s;
  {
    Harness h(s);
    REQUIRE(waitFor([&] { return s.authority->hasSession(); }));

    // Host-side revocation: the proxy never revives the old session; the
    // next request attaches as a new one.
    REQUIRE(s.authority->revokeSession());
    // (The renewal timer may notice first; either way at most one request
    // fails before a fresh session serves the next.)
    auto v = h.request(rpc(1, "tools/call", "{\"name\":\"echo_probe\"}"));
    REQUIRE(v.get("id")->n == 1);
    if (v.get("error")) {
      REQUIRE(v.get("error")->get("data")->get("code")->s
          == "invalid_session_proof");
      v = h.request(rpc(2, "tools/call", "{\"name\":\"echo_probe\"}"));
    }
    REQUIRE(v.get("result"));
    REQUIRE(s.authority->hasSession());

    // Signal path: the wake fd ends the loop and detaches.
    const char c = 0;
    REQUIRE(::write(h.wake.w, &c, 1) == 1);
    h.thread.join();
    REQUIRE(h.exitCode == 0);
    REQUIRE(!s.authority->hasSession());
  }
}

SCENARIO("vsrMcpProxy honours Content-Length and caps response bytes",
    "[integration]")
{
  proxy::Options opts;
  opts.limits.responseSeconds = 5.0;
  opts.limits.maxResponseBytes = 4096;
  const std::string big(8192, 'a');

  GIVEN("a reply within the cap on a connection the server keeps open")
  {
    FakeEndpoint ep([](const std::string &b) -> std::optional<std::string> {
      if (isAttach(b))
        return FakeEndpoint::attachOk();
      return FakeEndpoint::reply("{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":{}}");
    });
    proxy::Proxy p(ep.descriptor(), opts);
    const auto t0 = std::chrono::steady_clock::now();
    const std::string out = p.handleLine(rpc(7, "tools/list"));
    // Content-Length ended the read, not the 5 s deadline.
    REQUIRE(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
    REQUIRE(out.find("\"result\"") != std::string::npos);
  }

  GIVEN("a declared Content-Length over the cap")
  {
    FakeEndpoint ep([&](const std::string &b) -> std::optional<std::string> {
      if (isAttach(b))
        return FakeEndpoint::attachOk();
      return FakeEndpoint::reply(big);
    });
    proxy::Proxy p(ep.descriptor(), opts);
    REQUIRE(errorCode(p.handleLine(rpc(8, "tools/list"))) == "response_too_large");
  }

  GIVEN("an undeclared body that streams past the cap")
  {
    FakeEndpoint ep([&](const std::string &b) -> std::optional<std::string> {
      if (isAttach(b))
        return FakeEndpoint::attachOk();
      return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n" + big;
    });
    proxy::Proxy p(ep.descriptor(), opts);
    REQUIRE(errorCode(p.handleLine(rpc(9, "tools/list"))) == "response_too_large");
  }
}

SCENARIO("vsrMcpProxy fails a request locally when the server never replies",
    "[integration]")
{
  FakeEndpoint ep([](const std::string &b) -> std::optional<std::string> {
    if (isAttach(b))
      return FakeEndpoint::attachOk();
    return std::nullopt; // accept, read, never answer
  });
  proxy::Options opts;
  opts.limits.responseSeconds = 0.2;
  proxy::Proxy p(ep.descriptor(), opts);
  const auto t0 = std::chrono::steady_clock::now();
  const std::string out = p.handleLine(rpc(3, "tools/list"));
  REQUIRE(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3));
  REQUIRE(errorCode(out) == "timeout");
  mcp::json::Value v;
  REQUIRE(mcp::json::parse(out, v));
  REQUIRE(v.get("id")->n == 3);
}

SCENARIO("vsrMcpProxy answers a failed reattach locally without forwarding",
    "[integration]")
{
  FakeEndpoint ep([](const std::string &) -> std::optional<std::string> {
    return FakeEndpoint::reply(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32000,"
        "\"message\":\"occupied\",\"data\":{\"code\":\"occupied\"}}}");
  });
  proxy::Proxy p(ep.descriptor());
  const std::string out = p.handleLine(rpc(5, "tools/list"));
  REQUIRE(errorCode(out) == "occupied");
  REQUIRE(!p.attached());
  // Only the attach reached the endpoint; the request itself never did.
  const auto seen = ep.received();
  REQUIRE(seen.size() == 1);
  REQUIRE(isAttach(seen[0]));
}

SCENARIO("vsrMcpProxy refuses an endpoint it cannot prove it owns",
    "[integration]")
{
  for (const auto verdict : {proxy::Ownership::FOREIGN, proxy::Ownership::UNKNOWN}) {
    FakeEndpoint ep([](const std::string &) -> std::optional<std::string> {
      return FakeEndpoint::attachOk();
    });
    proxy::Options opts;
    opts.ownership = [verdict](int) { return verdict; };
    proxy::Proxy p(ep.descriptor(), opts);
    Pipe in, out, err;
    const int code = p.run(in.r, out.w, err.w);
    err.closeWrite();
    std::string e;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(err.r, buf, sizeof(buf))) > 0)
      e.append(buf, std::size_t(n));
    REQUIRE(code == 1);
    REQUIRE(p.untrusted());
    REQUIRE(e.find("not owned by this user") != std::string::npos);
    REQUIRE(e.find(ep.descriptor().launchCredential) == std::string::npos);
    // The connection was made but not one byte (credential) was sent.
    REQUIRE(waitFor([&] { return ep.connections >= 1; }));
    REQUIRE(ep.bytesReceived == 0);
  }
}

SCENARIO("vsrMcpProxy's /proc/net ownership check accepts our own listener",
    "[integration]")
{
  FakeEndpoint ep([](const std::string &) -> std::optional<std::string> {
    return FakeEndpoint::attachOk();
  });
  // Default options: the real /proc/net/tcp check runs before the attach.
  proxy::Proxy p(ep.descriptor());
  REQUIRE(p.handleLine(rpc(1, "tools/list")).find("\"result\"")
      != std::string::npos);
  REQUIRE(!p.untrusted());
}

#endif

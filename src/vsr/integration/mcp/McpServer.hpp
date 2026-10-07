// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/descriptor/LaunchDescriptor.hpp"
#include "vsr/integration/inspection/RequestDiagnostics.hpp"
#include "vsr/integration/mcp/Json.hpp"
#include "vsr/integration/operations/Operations.hpp"
#include "vsr/integration/session/SessionAuthority.hpp"
// posix
#if defined(__linux__)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
// std
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace vsr::integration::mcp {

/*
 * Embedded MCP server: the loopback transport over the headless integration
 * API (spec §1, §3). Built only with VSR_USE_INTEGRATION_MCP.
 *
 * Stack: hand-written HTTP/1.1 + strict JSON (no third-party dependency),
 * MCP streamable-HTTP transport, protocol version 2025-06-18, JSON-RPC 2.0.
 * Each POST to /mcp carries one JSON-RPC request and gets one
 * application/json response; GET (server-initiated SSE) is not offered.
 *
 * Wire rules enforced for every request, before any body parsing:
 *  - Host equals the exact descriptor authority (127.0.0.1:<port> or
 *    [::1]:<port>); no alias, alternate family, mapped form, elided port or
 *    forwarded-host substitution. Origin-bearing requests are rejected.
 *  - Authorization: Bearer <launch_credential>.
 *  - Session-scoped methods also need VSR-Session-ID and
 *    VSR-Session-Proof: Bearer <session_secret>.
 * The server never redirects and never proxies.
 *
 * Attachment/renewal/detach are private JSON-RPC methods for trusted client
 * code (vsr/attach, vsr/renew, vsr/detach), never MCP tools, so the session
 * secret is not model-visible. Renew/detach go through the operation
 * engine's bounded control path and need no scene slot.
 *
 * Frozen outcome codes travel as error.data.code (e.g. "unauthenticated",
 * "invalid_session_proof", "forbidden"). Log lines are fixed strings: no
 * header values, credentials or request bodies.
 */

inline constexpr const char *protocolVersion = "2025-06-18";
inline constexpr const char *endpointPath = "/mcp";

// A tool call result from a registered tool handler (ticket 15 adds tools).
struct ToolResult
{
  operations::Outcome code{operations::Outcome::OK};
  std::string contentJson{"[]"}; // MCP content array, on success
  // Optional MCP structuredContent object, on success (hook added for the
  // tools/ module, ticket 103).
  std::string structuredJson;
  // Admitted work that ended in a non-ok terminal outcome (MCP tool error).
  bool isError{false};
  std::string semanticReason;
  inspection::RequestDiagnostics diagnostics;
  std::string diagnosticKind;
  // Optional owned-resource reservation, transferred to the HTTP response so
  // slow sends retain their budget until the socket drains or times out.
  std::shared_ptr<const void> resourceLease;
  // Optional qualified carrier envelope. Bounded tools reserve serialization
  // before the handler returns; reject oversized RPC ids/envelopes before the
  // protected response is serialized. Legacy tools leave this unset.
  std::size_t maxResponseBytes{0};
};

struct Tool
{
  std::string name;
  std::string description;
  std::string inputSchemaJson{"{\"type\":\"object\"}"};
  // Called on a network thread with authenticated session credentials; it
  // must not touch live scene state (submit work through the engine).
  std::function<ToolResult(
      const session::Credentials &, const json::Value &arguments)>
      handler;
};

struct Config
{
  std::string host{"127.0.0.1"}; // "127.0.0.1" or "::1" only
  std::uint16_t port{0}; // 0 picks a free port
  std::size_t maxConnections{16};
  std::size_t maxBodyBytes{64 * 1024};
  // Total time allowed to read one request (headers plus body), measured
  // from the start of the read; bounds slow-drip clients and idle
  // keep-alive connections alike.
  int requestDeadlineMs{10000};
  // Per-send timeout on every connection socket.
  int sendTimeoutMs{10000};
  // Total time allowed to write one whole response, measured from the
  // start of the write, including time spent waiting for the socket to
  // accept data. A slow reader that keeps making partial progress is
  // cut off here: the server stops sending, closes the connection and frees
  // its slot. 0 means "use sendTimeoutMs".
  int responseDeadlineMs{0};
  std::vector<Tool> tools;
  // Optional strict launch-wide Image Exchange metadata (no session ledger).
  std::string imageExchange;
  // Fixed-text diagnostics; never receives request data or secrets.
  std::function<void(std::string_view)> log;
};

enum class StartCode
{
  OK,
  UNSUPPORTED_PLATFORM,
  INVALID_CONFIG,
  LISTEN_FAILED,
  PUBLISH_FAILED
};

struct StartStatus
{
  StartCode code{StartCode::OK};
  std::string reason; // never contains secrets

  bool ok() const;
};

class McpServer
{
 public:
  // Binds the single-family listener, checks it, then publishes the launch
  // descriptor into `dir`. Nothing is published unless every check passed.
  static StartStatus start(Config config,
      session::SessionAuthority &authority,
      operations::OperationEngine &engine,
      const descriptor::RuntimeDirectory &dir,
      std::unique_ptr<McpServer> &out);

  ~McpServer();
  McpServer(const McpServer &) = delete;
  McpServer &operator=(const McpServer &) = delete;

  std::uint16_t port() const;
  const std::string &authority() const;
  const descriptor::PublishedDescriptor &published() const;

  void stop();

 private:
  struct HttpRequest
  {
    std::string method;
    std::string target;
    std::vector<std::pair<std::string, std::string>> headers; // lower names
    std::string body;
    bool keepAlive{true};
  };

  struct HttpResponse
  {
    int status{200};
    std::string body;
    std::shared_ptr<const void> resourceLease;
  };

  McpServer(Config config,
      session::SessionAuthority &authority,
      operations::OperationEngine &engine)
      : m_config(std::move(config)), m_authority(authority), m_engine(engine)
  {}

  void log(std::string_view line) const;

  void acceptLoop();
  void serve(int fd);
  // 0: request read; 1: clean close (or deadline with nothing buffered);
  // -1: malformed or deadline exceeded (respond 400 and close);
  // -2: Content-Length over maxBodyBytes (respond 413 and close)
  int readRequest(int fd, std::string &buffer, HttpRequest &req);
  HttpResponse handle(const HttpRequest &req);
  HttpResponse dispatch(const HttpRequest &req, const json::Value &msg);

  static const std::string *header(
      const HttpRequest &req, std::string_view name, int &count);
  static HttpResponse rpcError(int httpStatus,
      const std::string &id,
      int rpcCode,
      const char *codeName,
      const char *message);
  static HttpResponse outcomeError(
      const std::string &id, operations::Outcome outcome);
  static HttpResponse rpcResult(const std::string &id, const std::string &res);

  Config m_config;
  session::SessionAuthority &m_authority;
  operations::OperationEngine &m_engine;
  std::unique_ptr<descriptor::PublishedDescriptor> m_published;
  std::uint16_t m_port{0};
  std::string m_authorityText;
  int m_listenFd{-1};
  int m_wake[2]{-1, -1};
  std::atomic<bool> m_stopping{false};
  std::thread m_acceptThread;

  std::mutex m_connMutex;
  struct Conn
  {
    int fd{-1};
    std::thread thread;
    std::atomic<bool> done{false};
  };
  std::list<Conn> m_conns;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline bool StartStatus::ok() const
{
  return code == StartCode::OK;
}

inline std::uint16_t McpServer::port() const
{
  return m_port;
}

inline const std::string &McpServer::authority() const
{
  return m_authorityText;
}

inline const descriptor::PublishedDescriptor &McpServer::published() const
{
  return *m_published;
}

inline void McpServer::log(std::string_view line) const
{
  if (m_config.log)
    m_config.log(line);
}

namespace detail {

inline std::string lower(std::string_view s)
{
  std::string out(s);
  for (auto &c : out)
    c = char(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

inline int httpStatusFor(operations::Outcome o)
{
  using operations::Outcome;
  switch (o) {
  case Outcome::UNAUTHENTICATED:
  case Outcome::INVALID_SESSION_PROOF:
  case Outcome::SESSION_EXPIRED:
    return 401;
  case Outcome::CAMERA_CONTROL_DISABLED:
  case Outcome::FORBIDDEN:
    return 403;
  case Outcome::BUSY:
  case Outcome::OCCUPIED:
    return 200; // semantic outcomes, not carrier failures
  default:
    return 200;
  }
}

inline const char *statusText(int status)
{
  switch (status) {
  case 200:
    return "OK";
  case 202:
    return "Accepted";
  case 400:
    return "Bad Request";
  case 401:
    return "Unauthorized";
  case 403:
    return "Forbidden";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  case 413:
    return "Payload Too Large";
  case 415:
    return "Unsupported Media Type";
  case 503:
    return "Service Unavailable";
  }
  return "Error";
}

#if defined(__linux__)
// Sends all of `data` within `deadlineMs` in total, measured from the start
// of the call (so time spent waiting for the socket to accept the first byte
// counts against it).
// Returns false on error or when the deadline passes before the last byte.
inline bool sendAll(int fd, const std::string &data, int deadlineMs)
{
  using clock = std::chrono::steady_clock;
  const auto deadline = clock::now() + std::chrono::milliseconds(deadlineMs);
  std::size_t sent = 0;
  while (sent < data.size()) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - clock::now())
                          .count();
    if (left <= 0)
      return false;
    pollfd p{fd, POLLOUT, 0};
    const int pr = ::poll(&p, 1, int(left));
    if (pr < 0 && errno == EINTR)
      continue;
    if (pr <= 0)
      return false;
    const ssize_t n = ::send(fd,
        data.data() + sent,
        data.size() - sent,
        MSG_NOSIGNAL | MSG_DONTWAIT);
    if (n <= 0) {
      if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
        continue;
      return false;
    }
    sent += std::size_t(n);
  }
  return true;
}
#endif

} // namespace detail

inline StartStatus McpServer::start(Config config,
    session::SessionAuthority &authority,
    operations::OperationEngine &engine,
    const descriptor::RuntimeDirectory &dir,
    std::unique_ptr<McpServer> &out)
{
  out.reset();
#if defined(__linux__)
  const bool v4 = config.host == "127.0.0.1";
  const bool v6 = config.host == "::1";
  if (!v4 && !v6)
    return {StartCode::INVALID_CONFIG, "host must be exactly 127.0.0.1 or ::1"};
  if (config.maxConnections == 0 || config.maxBodyBytes == 0
      || config.requestDeadlineMs <= 0 || config.sendTimeoutMs <= 0
      || config.responseDeadlineMs < 0)
    return {StartCode::INVALID_CONFIG, "limits must be positive"};
  if (config.responseDeadlineMs == 0)
    config.responseDeadlineMs = config.sendTimeoutMs;

  std::unique_ptr<McpServer> s(
      new McpServer(std::move(config), authority, engine));
  const int fd =
      ::socket(v4 ? AF_INET : AF_INET6, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return {StartCode::LISTEN_FAILED, "cannot create socket"};
  s->m_listenFd = fd;

  int one = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  if (v6) {
    // IPv6-only must hold before binding, and must be verifiable.
    int check = 0;
    socklen_t len = sizeof(check);
    if (::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one)) != 0
        || ::getsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &check, &len) != 0
        || check != 1)
      return {StartCode::LISTEN_FAILED, "cannot enforce IPv6-only socket"};
    sockaddr_in6 a{};
    a.sin6_family = AF_INET6;
    a.sin6_addr = in6addr_loopback;
    a.sin6_port = htons(s->m_config.port);
    if (::bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0)
      return {StartCode::LISTEN_FAILED, "cannot bind ::1"};
  } else {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(s->m_config.port);
    if (::bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0)
      return {StartCode::LISTEN_FAILED, "cannot bind 127.0.0.1"};
  }
  if (::listen(fd, 16) != 0)
    return {StartCode::LISTEN_FAILED, "cannot listen"};

  // Validate what was actually bound: exact family, exact loopback address.
  sockaddr_storage bound{};
  socklen_t blen = sizeof(bound);
  if (::getsockname(fd, reinterpret_cast<sockaddr *>(&bound), &blen) != 0)
    return {StartCode::LISTEN_FAILED, "cannot read bound address"};
  if (v4) {
    const auto *a = reinterpret_cast<const sockaddr_in *>(&bound);
    if (bound.ss_family != AF_INET
        || a->sin_addr.s_addr != htonl(INADDR_LOOPBACK))
      return {StartCode::LISTEN_FAILED, "bound address mismatch"};
    s->m_port = ntohs(a->sin_port);
    s->m_authorityText = "127.0.0.1:" + std::to_string(s->m_port);
  } else {
    const auto *a = reinterpret_cast<const sockaddr_in6 *>(&bound);
    if (bound.ss_family != AF_INET6 || !IN6_IS_ADDR_LOOPBACK(&a->sin6_addr))
      return {StartCode::LISTEN_FAILED, "bound address mismatch"};
    s->m_port = ntohs(a->sin6_port);
    s->m_authorityText = "[::1]:" + std::to_string(s->m_port);
  }

  if (::pipe2(s->m_wake, O_CLOEXEC) != 0)
    return {StartCode::LISTEN_FAILED, "cannot create wake pipe"};

  // Listener and auth are ready: only now publish.
  descriptor::Descriptor d;
  d.launchId = authority.launchId();
  d.host = s->m_config.host;
  d.port = s->m_port;
  d.launchCredential = authority.launchCredential();
  d.imageExchange = s->m_config.imageExchange;
  if (auto ps = descriptor::publish(dir, d, s->m_published); !ps.ok())
    return {StartCode::PUBLISH_FAILED, ps.reason};

  s->m_acceptThread = std::thread([p = s.get()] { p->acceptLoop(); });
  out = std::move(s);
  return {};
#else
  (void)config;
  (void)authority;
  (void)engine;
  (void)dir;
  return {StartCode::UNSUPPORTED_PLATFORM,
      "the embedded MCP server is validated on Linux only"};
#endif
}

inline McpServer::~McpServer()
{
  stop();
#if defined(__linux__)
  if (m_listenFd >= 0)
    ::close(m_listenFd);
  for (int &w : m_wake)
    if (w >= 0)
      ::close(std::exchange(w, -1));
#endif
}

inline void McpServer::stop()
{
#if defined(__linux__)
  if (m_stopping.exchange(true))
    return;
  // Withdraw the descriptor first so no new client finds a dying endpoint.
  if (m_published)
    m_published->remove();
  if (m_wake[1] >= 0) {
    const char c = 0;
    [[maybe_unused]] auto n = ::write(m_wake[1], &c, 1);
  }
  if (m_acceptThread.joinable())
    m_acceptThread.join();
  std::list<Conn> conns;
  {
    std::lock_guard<std::mutex> lock(m_connMutex);
    // Only descriptors still owned by a live connection; a closed one is
    // -1 and its number may already belong to someone else.
    for (auto &c : m_conns)
      if (c.fd >= 0)
        ::shutdown(c.fd, SHUT_RDWR);
    conns.splice(conns.end(), m_conns);
  }
  for (auto &c : conns)
    if (c.thread.joinable())
      c.thread.join();
#endif
}

inline void McpServer::acceptLoop()
{
#if defined(__linux__)
  for (;;) {
    pollfd fds[2] = {{m_listenFd, POLLIN, 0}, {m_wake[0], POLLIN, 0}};
    if (::poll(fds, 2, -1) < 0) {
      if (errno == EINTR)
        continue;
      return;
    }
    if (fds[1].revents || m_stopping)
      return;
    if (!(fds[0].revents & POLLIN))
      continue;
    const int fd = ::accept4(m_listenFd, nullptr, nullptr, SOCK_CLOEXEC);
    if (fd < 0)
      continue;

    std::lock_guard<std::mutex> lock(m_connMutex);
    // Reap finished connection threads.
    for (auto it = m_conns.begin(); it != m_conns.end();) {
      if (it->done) {
        it->thread.join();
        it = m_conns.erase(it);
      } else
        ++it;
    }
    if (m_conns.size() >= m_config.maxConnections) {
      log("mcp: connection limit reached");
      detail::sendAll(fd,
          "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n"
          "Connection: close\r\n\r\n",
          m_config.responseDeadlineMs);
      ::close(fd);
      continue;
    }
    const auto toTimeval = [](int ms) {
      return timeval{ms / 1000, (ms % 1000) * 1000};
    };
    const timeval rcv = toTimeval(m_config.requestDeadlineMs);
    const timeval snd = toTimeval(m_config.sendTimeoutMs);
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcv, sizeof(rcv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd, sizeof(snd));
    m_conns.emplace_back();
    Conn &c = m_conns.back();
    c.fd = fd;
    c.thread = std::thread([this, &c, fd] {
      try {
        serve(fd);
      } catch (const std::bad_alloc &) {
        // A bounded frame response can still meet host-wide allocation
        // failure. Unwind its leases and close; never terminate the host or
        // allocate another protected/error response on the failing path.
      }
      // Addendum 11: the slot is free no later than the close is visible.
      std::lock_guard<std::mutex> l(m_connMutex);
      ::close(fd);
      c.fd = -1;
      c.done = true;
    });
  }
#endif
}

inline int McpServer::readRequest(int fd, std::string &buf, HttpRequest &req)
{
#if defined(__linux__)
  constexpr std::size_t maxHeader = 8192;
  using clock = std::chrono::steady_clock;
  const auto deadline =
      clock::now() + std::chrono::milliseconds(m_config.requestDeadlineMs);
  // One bounded recv: waits no later than the request deadline.
  // >0 bytes read; 0 peer closed; -1 error or deadline passed.
  const auto recvBounded = [&](char *tmp, std::size_t size) -> ssize_t {
    for (;;) {
      const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - clock::now())
                            .count();
      if (left <= 0)
        return -1;
      pollfd p{fd, POLLIN, 0};
      const int pr = ::poll(&p, 1, int(left));
      if (pr < 0 && errno == EINTR)
        continue;
      if (pr <= 0)
        return -1;
      const ssize_t n = ::recv(fd, tmp, size, 0);
      if (n < 0 && errno == EINTR)
        continue;
      return n;
    }
  };
  std::size_t end;
  while ((end = buf.find("\r\n\r\n")) == std::string::npos) {
    if (buf.size() > maxHeader)
      return -1;
    char tmp[4096];
    const ssize_t n = recvBounded(tmp, sizeof(tmp));
    if (n <= 0)
      return buf.empty() ? 1 : -1;
    buf.append(tmp, std::size_t(n));
  }
  if (end > maxHeader)
    return -1;

  const std::string head = buf.substr(0, end);
  buf.erase(0, end + 4);
  std::size_t lineEnd = head.find("\r\n");
  const std::string line = head.substr(0, lineEnd);
  const auto sp1 = line.find(' ');
  const auto sp2 = line.find(' ', sp1 == std::string::npos ? sp1 : sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos)
    return -1;
  req = HttpRequest{};
  req.method = line.substr(0, sp1);
  req.target = line.substr(sp1 + 1, sp2 - sp1 - 1);
  if (line.substr(sp2 + 1) != "HTTP/1.1")
    return -1;

  std::size_t pos = lineEnd == std::string::npos ? head.size() : lineEnd + 2;
  while (pos < head.size()) {
    std::size_t e = head.find("\r\n", pos);
    if (e == std::string::npos)
      e = head.size();
    const std::string h = head.substr(pos, e - pos);
    pos = e + 2;
    const auto colon = h.find(':');
    if (colon == std::string::npos || colon == 0 || h[0] == ' ' || h[0] == '\t')
      return -1; // no obs-fold
    std::string name = detail::lower(h.substr(0, colon));
    if (name.find_first_of(" \t") != std::string::npos)
      return -1;
    std::string value = h.substr(colon + 1);
    const auto b = value.find_first_not_of(" \t");
    const auto l = value.find_last_not_of(" \t");
    value = b == std::string::npos ? std::string() : value.substr(b, l - b + 1);
    req.headers.emplace_back(std::move(name), std::move(value));
  }

  int count = 0;
  if (header(req, "transfer-encoding", count) || count)
    return -1;
  std::size_t length = 0;
  if (const auto *cl = header(req, "content-length", count)) {
    if (count != 1 || cl->empty() || cl->size() > 9
        || cl->find_first_not_of("0123456789") != std::string::npos)
      return -1;
    length = std::stoul(*cl);
  }
  if (length > m_config.maxBodyBytes)
    return -2; // rejected before any body byte is read
  while (buf.size() < length) {
    char tmp[4096];
    const ssize_t n = recvBounded(tmp, sizeof(tmp));
    if (n <= 0)
      return -1;
    buf.append(tmp, std::size_t(n));
  }
  req.body = buf.substr(0, length);
  buf.erase(0, length);
  if (const auto *c = header(req, "connection", count))
    req.keepAlive = detail::lower(*c) != "close";
  return 0;
#else
  (void)fd;
  (void)buf;
  (void)req;
  return -1;
#endif
}

inline void McpServer::serve(int fd)
{
#if defined(__linux__)
  std::string buf;
  while (!m_stopping) {
    HttpRequest req;
    const int r = readRequest(fd, buf, req);
    if (r == 1)
      return;
    HttpResponse res;
    if (r == -2) {
      log("mcp: request body too large");
      res.status = 413;
      req.keepAlive = false;
    } else if (r < 0) {
      log("mcp: malformed HTTP request rejected");
      res.status = 400;
      req.keepAlive = false;
    } else
      res = handle(req);
    std::string out = "HTTP/1.1 " + std::to_string(res.status) + " "
        + detail::statusText(res.status) + "\r\n";
    if (!res.body.empty())
      out += "Content-Type: application/json\r\n";
    out += "Cache-Control: no-store\r\n";
    out += "Content-Length: " + std::to_string(res.body.size()) + "\r\n";
    out += req.keepAlive ? "\r\n" : "Connection: close\r\n\r\n";
    out += res.body;
    if (!detail::sendAll(fd, out, m_config.responseDeadlineMs)) {
      log("mcp: response send failed or exceeded the response deadline");
      return;
    }
    if (!req.keepAlive)
      return;
  }
#else
  (void)fd;
#endif
}

inline const std::string *McpServer::header(
    const HttpRequest &req, std::string_view name, int &count)
{
  const std::string *found = nullptr;
  count = 0;
  for (const auto &[n, v] : req.headers)
    if (n == name) {
      ++count;
      found = &v;
    }
  return found;
}

inline McpServer::HttpResponse McpServer::rpcError(int httpStatus,
    const std::string &id,
    int rpcCode,
    const char *codeName,
    const char *message)
{
  HttpResponse r;
  r.status = httpStatus;
  r.body = "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"error\":{\"code\":"
      + std::to_string(rpcCode) + ",\"message\":" + json::quote(message)
      + ",\"data\":{\"code\":" + json::quote(codeName) + "}}}";
  return r;
}

inline McpServer::HttpResponse McpServer::outcomeError(
    const std::string &id, operations::Outcome outcome)
{
  // Implementation-defined JSON-RPC server-error range; the frozen code
  // name in data.code is the contract.
  return rpcError(detail::httpStatusFor(outcome),
      id,
      -32000,
      operations::outcomeName(outcome),
      operations::outcomeName(outcome));
}

inline McpServer::HttpResponse McpServer::rpcResult(
    const std::string &id, const std::string &res)
{
  HttpResponse r;
  r.body = "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + res + "}";
  return r;
}

inline McpServer::HttpResponse McpServer::handle(const HttpRequest &req)
{
  using operations::Outcome;
  int count = 0;

  // Authority: exactly one Host, equal to the exact descriptor authority.
  const std::string *host = header(req, "host", count);
  if (!host || count != 1 || *host != m_authorityText) {
    log("mcp: request rejected: host authority mismatch");
    return rpcError(400, "null", -32600, "invalid_host", "invalid Host");
  }
  for (const char *h : {"forwarded",
           "x-forwarded-host",
           "x-forwarded-for",
           "x-forwarded-proto",
           "x-original-host"}) {
    if (header(req, h, count)) {
      log("mcp: request rejected: forwarding header");
      return rpcError(400, "null", -32600, "invalid_host", "invalid Host");
    }
  }
  if (header(req, "origin", count)) {
    log("mcp: request rejected: Origin present");
    return rpcError(403, "null", -32600, "forbidden_origin", "Origin denied");
  }
  if (req.target != endpointPath) {
    HttpResponse r;
    r.status = 404;
    return r;
  }
  if (req.method != "POST") {
    HttpResponse r;
    r.status = 405;
    return r;
  }

  // Launch authentication precedes everything else, including initialize.
  const std::string *auth = header(req, "authorization", count);
  constexpr std::string_view bearer = "Bearer ";
  if (!auth || count != 1 || auth->compare(0, bearer.size(), bearer) != 0
      || !session::constantTimeEqual(
          std::string_view(*auth).substr(bearer.size()),
          m_authority.launchCredential())) {
    log("mcp: request rejected: unauthenticated");
    return outcomeError("null", Outcome::UNAUTHENTICATED);
  }

  if (const auto *pv = header(req, "mcp-protocol-version", count);
      pv && (count != 1 || *pv != protocolVersion))
    return rpcError(
        400, "null", -32600, "invalid_request", "unsupported protocol version");
  const auto *ct = header(req, "content-type", count);
  if (!ct
      || detail::lower(ct->substr(0, ct->find(';'))) != "application/json") {
    HttpResponse r;
    r.status = 415;
    return r;
  }

  json::Value msg;
  if (!json::parse(req.body, msg) || !msg.isObject())
    return rpcError(400, "null", -32700, "invalid_request", "parse error");
  return dispatch(req, msg);
}

inline McpServer::HttpResponse McpServer::dispatch(
    const HttpRequest &req, const json::Value &msg)
{
  using operations::Outcome;
  const auto *ver = msg.get("jsonrpc");
  const auto *method = msg.get("method");
  const auto *idv = msg.get("id");
  if (!ver || !ver->isString() || ver->s != "2.0" || !method
      || !method->isString())
    return rpcError(400, "null", -32600, "invalid_request", "invalid request");
  if (idv && !idv->isString() && !idv->isNumber())
    return rpcError(400, "null", -32600, "invalid_request", "invalid id");

  const std::string &m = method->s;
  if (!idv) {
    // Notifications: acknowledged without a body.
    HttpResponse r;
    r.status = 202;
    return r;
  }
  const std::string id = json::write(*idv);

  if (m == "initialize") {
    return rpcResult(id,
        std::string("{\"protocolVersion\":") + json::quote(protocolVersion)
            + ",\"capabilities\":{\"tools\":{\"listChanged\":false}},"
              "\"serverInfo\":{\"name\":\"vsr\",\"version\":\"1\"}}");
  }
  if (m == "ping")
    return rpcResult(id, "{}");
  if (m == "tools/list") {
    std::string tools = "[";
    for (std::size_t i = 0; i < m_config.tools.size(); i++) {
      const auto &t = m_config.tools[i];
      if (i)
        tools += ',';
      tools += "{\"name\":" + json::quote(t.name)
          + ",\"description\":" + json::quote(t.description)
          + ",\"inputSchema\":" + t.inputSchemaJson + "}";
    }
    return rpcResult(id, "{\"tools\":" + tools + "]}");
  }
  if (m == "vsr/attach") {
    int count = 0;
    const std::string cred = header(req, "authorization", count)->substr(7);
    const auto a = m_authority.attach(cred);
    if (a.code != session::Code::OK)
      return outcomeError(id, operations::fromSessionCode(a.code));
    // Private bootstrap response for trusted client code only.
    char dur[96];
    std::snprintf(dur,
        sizeof(dur),
        ",\"lease_seconds\":%.17g,\"renewal_seconds\":%.17g}",
        a.leaseSeconds,
        a.renewalSeconds);
    return rpcResult(id,
        "{\"session_id\":" + json::quote(a.sessionId)
            + ",\"session_secret\":" + json::quote(a.sessionSecret) + dur);
  }

  // Everything below is session-scoped.
  session::Credentials creds;
  {
    int count = 0;
    creds.launchCredential = header(req, "authorization", count)->substr(7);
    const auto *sid = header(req, "vsr-session-id", count);
    if (sid && count == 1)
      creds.sessionId = *sid;
    const auto *proof = header(req, "vsr-session-proof", count);
    if (proof && count == 1 && proof->rfind("Bearer ", 0) == 0)
      creds.sessionProof = proof->substr(7);
  }

  if (m == "vsr/renew" || m == "vsr/detach") {
    const Outcome o =
        m == "vsr/renew" ? m_engine.renew(creds) : m_engine.detach(creds);
    if (o != Outcome::OK)
      return outcomeError(id, o);
    return rpcResult(id, "{}");
  }
  if (m == "tools/call") {
    const auto code = m_authority.authenticate(creds);
    if (code != session::Code::OK)
      return outcomeError(id, operations::fromSessionCode(code));
    const auto *params = msg.get("params");
    const auto *name = params ? params->get("name") : nullptr;
    if (!name || !name->isString())
      return rpcError(200, id, -32602, "invalid_request", "invalid params");
    for (const auto &t : m_config.tools) {
      if (t.name != name->s || !t.handler)
        continue;
      static const json::Value empty = [] {
        json::Value v;
        v.type = json::Value::Type::OBJECT;
        return v;
      }();
      const auto *args = params->get("arguments");
      const ToolResult r = t.handler(creds, args ? *args : empty);
      if (r.code != Outcome::OK) {
        auto response = outcomeError(id, r.code);
        if (r.semanticReason == "unsupported_view_camera")
          response.body.insert(response.body.size() - 3,
              ",\"reason\":\"unsupported_view_camera\"");
        if (r.diagnostics.count) {
          // All fields/reasons are closed syntax vocabulary. Kind is never
          // echoed unless recognized; no live identity or request values.
          const auto &kind = r.diagnosticKind;
          const char *safeKind =
              kind == "objects" || kind == "layers" || kind == "nodes"
              ? kind.c_str()
              : "unknown";
          std::string details = ",\"details\":[";
          for (std::size_t i = 0; i < r.diagnostics.count
              && i < inspection::RequestDiagnostics::MAX_ENTRIES;
              ++i) {
            const auto &d = r.diagnostics.entries[i];
            const auto entry = std::string(i ? "," : "") + "{\"field\":"
                + json::quote(inspection::diagnosticName(d.field))
                + ",\"kind\":" + json::quote(safeKind) + ",\"reason\":"
                + json::quote(inspection::diagnosticName(d.reason)) + "}";
            if (details.size() + entry.size() + 1
                > inspection::RequestDiagnostics::MAX_BYTES)
              break;
            details += entry;
          }
          details += "]";
          response.body.insert(response.body.size() - 3, details);
        }
        return response;
      }
      if (r.maxResponseBytes
          && (id.size() > 1024 || r.maxResponseBytes < 4096
              || r.contentJson.size() > r.maxResponseBytes - 4096
              || r.structuredJson.size()
                  > r.maxResponseBytes - 4096 - r.contentJson.size()))
        return outcomeError(id.size() > 1024 ? "null" : id, Outcome::BUSY);
      std::string res = "{\"content\":" + r.contentJson;
      if (!r.structuredJson.empty())
        res += ",\"structuredContent\":" + r.structuredJson;
      auto response = rpcResult(
          id, res + ",\"isError\":" + (r.isError ? "true" : "false") + "}");
      response.resourceLease = r.resourceLease;
      return response;
    }
    return rpcError(200, id, -32602, "invalid_request", "unknown tool");
  }
  return rpcError(200, id, -32601, "invalid_request", "method not found");
}

} // namespace vsr::integration::mcp

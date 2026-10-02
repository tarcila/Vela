// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr
#include "vsr/integration/descriptor/LaunchDescriptor.hpp"
#include "vsr/integration/mcp/Json.hpp"
// std
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
// posix
#if defined(__linux__)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace vsr::tools::mcp_proxy {

/*
 * vsrMcpProxy: the trusted stdio adapter between Pi and a VSR application's
 * loopback MCP endpoint (addendum 05 items 1–3).
 *
 * Stdio side: newline-delimited JSON-RPC 2.0 (MCP stdio transport). Each
 * request line is forwarded as one POST to the descriptor endpoint with the
 * launch credential and, once attached, the session ID and proof. The
 * server's JSON response is written back as one line.
 *
 * The proxy attaches privately (vsr/attach), keeps the session ID and secret
 * only in memory, renews on its own timer at the attachment-reported cadence
 * (never tied to stdio traffic), and detaches on stdin EOF or when the wake
 * fd becomes readable (signal). A lost session is replaced by a fresh attach;
 * old authority is never reused. vsr/* methods from the stdio client are
 * refused locally and never forwarded. A failed reattach answers the stdio
 * request locally with the attach's frozen code and never POSTs it.
 *
 * Every POST is bounded (Options::limits): a connect deadline, an overall
 * response deadline and a response byte cap, honouring Content-Length. A
 * breach fails that request locally with a JSON-RPC error. Before the launch
 * credential is sent on a new connection, the listening peer's owner must be
 * this process's effective UID (Linux /proc/net/tcp{,6}); otherwise the proxy
 * refuses the endpoint for good and run() exits 1 with a fixed diagnostic.
 * The renewal timer reads an injectable monotonic clock (Options::clock).
 *
 * Diagnostics go to the error fd as fixed strings only: no header values,
 * credentials, secrets or message bodies.
 */

inline constexpr const char *descriptorEnv = "VSR_MCP_DESCRIPTOR";
inline constexpr const char *protocolVersion = "2025-06-18";

// Why a request failed locally (nothing to do with the server's answer).
enum class Failure
{
  NONE,
  UNREACHABLE, // connect/send/recv failed or the reply was not HTTP/1.1
  TIMEOUT, // connect or overall response deadline passed
  TOO_LARGE, // response exceeded the byte cap
  FOREIGN_OWNER, // listening socket not owned by our effective UID
};

struct HttpReply
{
  int status{0}; // 0: local failure, see failure
  Failure failure{Failure::NONE};
  std::string body;
};

// Result of checking who owns the listening socket a connection reached.
enum class Ownership
{
  OWNED,
  FOREIGN,
  UNKNOWN, // the check could not be made: treated as FOREIGN
};

// Inspects the connected socket fd before the credential is sent.
using OwnershipCheck = std::function<Ownership(int fd)>;
// Monotonic seconds; drives the renewal timer only.
using Clock = std::function<double()>;

struct Limits
{
  double connectSeconds{5.0};
  double responseSeconds{30.0}; // send + full response, per request
  std::size_t maxResponseBytes{16u * 1024u * 1024u}; // headers + body
};

inline double steadySeconds()
{
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

inline Ownership procNetOwnership(int fd);

struct Options
{
  Limits limits;
  Clock clock{steadySeconds};
  OwnershipCheck ownership{procNetOwnership};
};

namespace detail {

inline bool writeAll(int fd, std::string_view data)
{
#if defined(__linux__)
  std::size_t done = 0;
  while (done < data.size()) {
    const ssize_t n = ::write(fd, data.data() + done, data.size() - done);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      return false;
    done += std::size_t(n);
  }
  return true;
#else
  (void)fd;
  (void)data;
  return false;
#endif
}

inline std::string rpcError(
    const std::string &id, int code, const char *codeName, const char *message)
{
  return "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"error\":{\"code\":"
      + std::to_string(code) + ",\"message\":"
      + integration::mcp::json::quote(message)
      + ",\"data\":{\"code\":" + integration::mcp::json::quote(codeName)
      + "}}}";
}

inline std::string errorCodeOf(const integration::mcp::json::Value &v)
{
  const auto *e = v.get("error");
  const auto *d = e ? e->get("data") : nullptr;
  const auto *c = d ? d->get("code") : nullptr;
  return c && c->isString() ? c->s : std::string();
}

} // namespace detail

namespace detail {

inline const char *failureCode(Failure f)
{
  switch (f) {
  case Failure::TIMEOUT:
    return "timeout";
  case Failure::TOO_LARGE:
    return "response_too_large";
  case Failure::FOREIGN_OWNER:
    return "untrusted_endpoint";
  default:
    return "unavailable";
  }
}

inline const char *failureMessage(Failure f)
{
  switch (f) {
  case Failure::TIMEOUT:
    return "VSR endpoint did not answer in time";
  case Failure::TOO_LARGE:
    return "VSR endpoint response exceeds the proxy limit";
  case Failure::FOREIGN_OWNER:
    return "VSR endpoint is not owned by this user";
  default:
    return "VSR endpoint unreachable";
  }
}

#if defined(__linux__)
// Remaining milliseconds until deadline (steady clock), clamped to >= 0.
inline int remainingMs(std::chrono::steady_clock::time_point deadline)
{
  const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
      deadline - std::chrono::steady_clock::now())
                        .count();
  return left <= 0 ? 0 : int(left > 60000 ? 60000 : left);
}

// Parses one /proc/net/tcp{,6} address field "HEX:PORT" into raw words as
// the kernel printed them (each word is the in-memory u32 of the address).
inline bool parseProcAddr(
    const std::string &field, std::uint32_t words[4], int &nWords, int &port)
{
  const auto colon = field.find(':');
  if (colon == std::string::npos)
    return false;
  const std::string hex = field.substr(0, colon);
  if (hex.size() != 8 && hex.size() != 32)
    return false;
  nWords = int(hex.size() / 8);
  for (int i = 0; i < nWords; i++)
    words[i] = std::uint32_t(std::stoul(hex.substr(std::size_t(i) * 8, 8), nullptr, 16));
  port = int(std::stoul(field.substr(colon + 1), nullptr, 16));
  return true;
}

// Raw words of a sockaddr, in the form /proc/net/tcp{,6} prints them.
inline bool sockWords(const sockaddr_storage &ss,
    std::uint32_t words[4],
    int &nWords,
    int &port)
{
  if (ss.ss_family == AF_INET) {
    const auto &a = reinterpret_cast<const sockaddr_in &>(ss);
    std::memcpy(words, &a.sin_addr, 4);
    nWords = 1;
    port = ntohs(a.sin_port);
    return true;
  }
  if (ss.ss_family == AF_INET6) {
    const auto &a = reinterpret_cast<const sockaddr_in6 &>(ss);
    std::memcpy(words, &a.sin6_addr, 16);
    nWords = 4;
    port = ntohs(a.sin6_port);
    return true;
  }
  return false;
}
#endif

} // namespace detail

// Linux ownership check: finds the peer's end of this connection (local ==
// our peer address, remote == our socket address) in /proc/net/tcp or
// /proc/net/tcp6 and compares its UID with our effective UID. An accepted
// socket carries its listener's owner. Anything unreadable is UNKNOWN.
inline Ownership procNetOwnership(int fd)
{
#if defined(__linux__)
  sockaddr_storage self{}, peer{};
  socklen_t ls = sizeof(self), lp = sizeof(peer);
  if (::getsockname(fd, reinterpret_cast<sockaddr *>(&self), &ls) != 0
      || ::getpeername(fd, reinterpret_cast<sockaddr *>(&peer), &lp) != 0)
    return Ownership::UNKNOWN;
  std::uint32_t pw[4], sw[4];
  int pn = 0, sn = 0, pport = 0, sport = 0;
  if (!detail::sockWords(peer, pw, pn, pport)
      || !detail::sockWords(self, sw, sn, sport))
    return Ownership::UNKNOWN;
  const char *file = pn == 1 ? "/proc/net/tcp" : "/proc/net/tcp6";
  std::ifstream in(file);
  if (!in)
    return Ownership::UNKNOWN;
  std::string line;
  std::getline(in, line); // header
  while (std::getline(in, line)) {
    std::istringstream ls2(line);
    std::string sl, local, remote, st, queues, timer, retr;
    long uid = -1;
    if (!(ls2 >> sl >> local >> remote >> st >> queues >> timer >> retr >> uid))
      continue;
    std::uint32_t lw[4], rw[4];
    int ln = 0, rn = 0, lport = 0, rport = 0;
    try {
      if (!detail::parseProcAddr(local, lw, ln, lport)
          || !detail::parseProcAddr(remote, rw, rn, rport))
        continue;
    } catch (...) {
      continue;
    }
    if (ln != pn || rn != sn || lport != pport || rport != sport
        || std::memcmp(lw, pw, std::size_t(pn) * 4) != 0
        || std::memcmp(rw, sw, std::size_t(sn) * 4) != 0)
      continue;
    return uid >= 0 && uid_t(uid) == ::geteuid() ? Ownership::OWNED
                                                  : Ownership::FOREIGN;
  }
  return Ownership::UNKNOWN;
#else
  (void)fd;
  return Ownership::UNKNOWN;
#endif
}

class Proxy
{
 public:
  explicit Proxy(integration::descriptor::Descriptor d, Options opts = {})
      : m_desc(std::move(d)), m_opts(std::move(opts))
  {
    if (!m_opts.clock)
      m_opts.clock = steadySeconds;
    if (!m_opts.ownership)
      m_opts.ownership = procNetOwnership;
    m_authority = m_desc.host == "::1"
        ? "[::1]:" + std::to_string(m_desc.port)
        : "127.0.0.1:" + std::to_string(m_desc.port);
  }

  ~Proxy()
  {
    stopRenewal();
    closeSelfPipe();
  }

  Proxy(const Proxy &) = delete;
  Proxy &operator=(const Proxy &) = delete;

  // Serves until stdin EOF or wakeFd readable (-1: none). Detaches before
  // returning. Returns a process exit code: 1 when the attach fails or the
  // endpoint is refused as not owned by this user.
  int run(int inFd, int outFd, int errFd, int wakeFd = -1)
  {
#if defined(__linux__)
    m_errFd = errFd;
    if (::pipe2(m_selfPipe, O_CLOEXEC | O_NONBLOCK) != 0)
      return 1;
    if (!attach().empty()) {
      diag(m_untrusted ? untrustedDiag : "vsrMcpProxy: attach failed");
      return 1;
    }
    diag("vsrMcpProxy: attached");
    m_renewer = std::thread([this] { renewLoop(); });

    std::string buf;
    bool open = true;
    while (open && !m_untrusted) {
      pollfd fds[3] = {
          {m_selfPipe[0], POLLIN, 0}, {inFd, POLLIN, 0}, {wakeFd, POLLIN, 0}};
      const int n = ::poll(fds, wakeFd >= 0 ? 3 : 2, -1);
      if (n < 0) {
        if (errno == EINTR)
          continue;
        break;
      }
      if (fds[0].revents || (wakeFd >= 0 && fds[2].revents))
        break;
      if (!fds[1].revents)
        continue;
      char tmp[4096];
      const ssize_t r = ::read(inFd, tmp, sizeof(tmp));
      if (r < 0 && (errno == EINTR || errno == EAGAIN))
        continue;
      if (r <= 0) {
        open = false;
        if (!buf.empty())
          buf.push_back('\n');
      } else
        buf.append(tmp, std::size_t(r));
      std::size_t nl;
      while (!m_untrusted && (nl = buf.find('\n')) != std::string::npos) {
        std::string line = buf.substr(0, nl);
        buf.erase(0, nl + 1);
        if (!line.empty() && line.back() == '\r')
          line.pop_back();
        if (line.find_first_not_of(" \t") == std::string::npos)
          continue;
        const std::string out = handleLine(line);
        if (!out.empty() && !detail::writeAll(outFd, out + "\n")) {
          open = false;
          break;
        }
      }
      if (buf.size() > maxLineBytes) {
        diag("vsrMcpProxy: oversized input line dropped");
        buf.clear();
      }
    }

    stopRenewal();
    if (m_untrusted) {
      // Never send the credential again, not even to detach.
      diag(untrustedDiag);
      return 1;
    }
    detach();
    diag("vsrMcpProxy: detached");
    return 0;
#else
    (void)inFd;
    (void)outFd;
    (void)errFd;
    (void)wakeFd;
    return 1;
#endif
  }

  // One stdio message in, zero or one stdio message out. Public for tests.
  std::string handleLine(const std::string &line)
  {
    namespace json = integration::mcp::json;
    json::Value msg;
    if (!json::parse(line, msg))
      return detail::rpcError("null", -32700, "invalid_request", "parse error");
    if (!msg.isObject())
      return detail::rpcError(
          "null", -32600, "invalid_request", "invalid request");
    const auto *method = msg.get("method");
    const auto *idv = msg.get("id");
    const std::string id = idv ? json::write(*idv) : "null";
    if (!method || !method->isString()) {
      // A response from the client (we send no requests): nothing to do.
      return idv && !msg.get("result") && !msg.get("error")
          ? detail::rpcError(id, -32600, "invalid_request", "invalid request")
          : std::string();
    }
    if (method->s.rfind("vsr/", 0) == 0) {
      // Private bootstrap stays private to the proxy.
      return idv ? detail::rpcError(
                       id, -32601, "invalid_request", "method not found")
                 : std::string();
    }

    // A lost session is replaced first; a failed reattach answers locally
    // with the attach's code and the request is never POSTed.
    if (!attached()) {
      const std::string code = attach();
      if (!code.empty())
        return idv ? detail::rpcError(
                         id, -32000, code.c_str(), "VSR reattach failed")
                   : std::string();
      diag("vsrMcpProxy: reattached as a new session");
    }

    HttpReply r = post(line, true);
    if (r.status == 0) {
      // Connection lost: the next request reattaches as a new session.
      // A timeout or oversized reply leaves the session alone: the server
      // may still hold it and renewal decides its fate.
      if (r.failure == Failure::UNREACHABLE)
        markLost();
      return idv ? detail::rpcError(id,
                       -32603,
                       detail::failureCode(r.failure),
                       detail::failureMessage(r.failure))
                 : std::string();
    }
    if (!idv)
      return {}; // notification: acknowledged, no stdio output
    json::Value res;
    if (r.body.empty() || !json::parse(r.body, res) || !res.isObject())
      return detail::rpcError(
          id, -32603, "unavailable", "unexpected response from VSR endpoint");
    const std::string code = detail::errorCodeOf(res);
    if (code == "session_expired" || code == "invalid_session_proof")
      markLost();
    // Carrier-level rejections answer with a null id; restore the client's.
    if (const auto *rid = res.get("id");
        !rid || rid->type == json::Value::Type::NUL) {
      res.o["id"] = *idv;
      return json::write(res);
    }
    return r.body;
  }

  bool attached() const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_sessionId.empty();
  }

  // True once an endpoint was refused as not owned by this user.
  bool untrusted() const
  {
    return m_untrusted;
  }

  std::uint64_t renewals() const
  {
    return m_renewals;
  }

  // Wakes the renewal timer so it re-reads the clock (tests advance an
  // injected clock and then nudge; the real clock needs no nudge).
  void nudge()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cv.notify_all();
  }

 private:
  static constexpr std::size_t maxLineBytes = 4 * 1024 * 1024;
  // Upper bound on one timer wait before the clock is re-read.
  static constexpr double timerPollSeconds = 0.25;
  static constexpr const char *untrustedDiag =
      "vsrMcpProxy: refusing endpoint: listening socket is not owned by this "
      "user (or ownership could not be verified)";

  void diag(const char *fixed)
  {
    if (m_errFd >= 0) {
      detail::writeAll(m_errFd, fixed);
      detail::writeAll(m_errFd, "\n");
    }
  }

  // Launch-authenticated attach. Replaces any held session. Returns "" on
  // success, else the frozen code (server's data.code, or a local one).
  std::string attach()
  {
    namespace json = integration::mcp::json;
    HttpReply r = post(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"vsr/attach\"}", false);
    if (r.status == 0)
      return detail::failureCode(r.failure);
    json::Value v;
    if (!json::parse(r.body, v) || !v.isObject())
      return "unavailable";
    if (r.status != 200 || v.get("error")) {
      const std::string code = detail::errorCodeOf(v);
      return code.empty() ? std::string("unavailable") : code;
    }
    const auto *res = v.get("result");
    const auto *sid = res ? res->get("session_id") : nullptr;
    const auto *sec = res ? res->get("session_secret") : nullptr;
    const auto *ren = res ? res->get("renewal_seconds") : nullptr;
    if (!sid || !sid->isString() || !sec || !sec->isString() || !ren
        || !ren->isNumber() || !(ren->n > 0.0))
      return "unavailable";
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessionId = sid->s;
    m_secret = sec->s;
    m_renewalSeconds = ren->n;
    m_nextRenewal = m_opts.clock() + m_renewalSeconds;
    m_cv.notify_all();
    return {};
  }

  void markLost()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessionId.clear();
    m_secret.clear();
  }

  void detach()
  {
    if (!attached())
      return;
    post("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"vsr/detach\"}", true);
    markLost();
  }

  // Renews whenever the injected clock reaches the due time. Each wait is
  // bounded by timerPollSeconds, so an injected clock is re-read promptly.
  void renewLoop()
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    while (!m_stopping && !m_untrusted) {
      const double left = m_nextRenewal - m_opts.clock();
      if (left > 0.0) {
        const double wait = left < timerPollSeconds ? left : timerPollSeconds;
        m_cv.wait_for(lock, std::chrono::duration<double>(wait));
        continue;
      }
      const bool have = !m_sessionId.empty();
      lock.unlock();
      if (have) {
        namespace json = integration::mcp::json;
        HttpReply r = post(
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"vsr/renew\"}", true);
        json::Value v;
        if (r.status == 200 && json::parse(r.body, v) && v.get("result"))
          m_renewals++;
        else if (r.failure == Failure::FOREIGN_OWNER)
          wakeRun();
        else {
          diag("vsrMcpProxy: renewal failed; session dropped");
          markLost();
        }
      }
      lock.lock();
      m_nextRenewal = m_opts.clock() + m_renewalSeconds;
    }
  }

  void stopRenewal()
  {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_stopping = true;
      m_cv.notify_all();
    }
    if (m_renewer.joinable())
      m_renewer.join();
  }

  void wakeRun()
  {
#if defined(__linux__)
    if (m_selfPipe[1] >= 0) {
      const char c = 0;
      [[maybe_unused]] auto n = ::write(m_selfPipe[1], &c, 1);
    }
#endif
  }

  void closeSelfPipe()
  {
#if defined(__linux__)
    for (int &fd : m_selfPipe)
      if (fd >= 0) {
        ::close(fd);
        fd = -1;
      }
#endif
  }

#if defined(__linux__)
  // Non-blocking connect bounded by the connect deadline. Returns the fd
  // (blocking mode not restored: all I/O below is poll-driven) or -1.
  int connectBounded(Failure &failure)
  {
    const bool v6 = m_desc.host == "::1";
    const int fd = ::socket(v6 ? AF_INET6 : AF_INET,
        SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
        0);
    failure = Failure::UNREACHABLE;
    if (fd < 0)
      return -1;
    int rc;
    if (v6) {
      sockaddr_in6 a{};
      a.sin6_family = AF_INET6;
      a.sin6_addr = in6addr_loopback;
      a.sin6_port = htons(std::uint16_t(m_desc.port));
      rc = ::connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a));
    } else {
      sockaddr_in a{};
      a.sin_family = AF_INET;
      a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      a.sin_port = htons(std::uint16_t(m_desc.port));
      rc = ::connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a));
    }
    if (rc != 0 && errno == EINPROGRESS) {
      const auto deadline = std::chrono::steady_clock::now()
          + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
              std::chrono::duration<double>(m_opts.limits.connectSeconds));
      pollfd p{fd, POLLOUT, 0};
      int n;
      do
        n = ::poll(&p, 1, detail::remainingMs(deadline));
      while (n < 0 && errno == EINTR);
      if (n == 0)
        failure = Failure::TIMEOUT;
      int err = 0;
      socklen_t len = sizeof(err);
      if (n == 1
          && ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0
          && err == 0)
        rc = 0;
    }
    if (rc != 0) {
      ::close(fd);
      return -1;
    }
    failure = Failure::NONE;
    return fd;
  }
#endif

  // One request per connection; only fixed headers plus held credentials.
  // Bounded by the connect deadline, the overall response deadline and the
  // response byte cap; honours Content-Length.
  HttpReply post(const std::string &body, bool withSession)
  {
    HttpReply reply;
#if defined(__linux__)
    if (m_untrusted) {
      reply.failure = Failure::FOREIGN_OWNER;
      return reply;
    }
    const int fd = connectBounded(reply.failure);
    if (fd < 0)
      return reply;

    // Verify the listener's owner before the credential leaves the process.
    if (m_opts.ownership(fd) != Ownership::OWNED) {
      ::close(fd);
      m_untrusted = true;
      reply.failure = Failure::FOREIGN_OWNER;
      wakeRun();
      return reply;
    }

    std::string req = "POST /mcp HTTP/1.1\r\nHost: " + m_authority
        + "\r\nAuthorization: Bearer " + m_desc.launchCredential
        + "\r\nContent-Type: application/json\r\nAccept: application/json"
          "\r\nMCP-Protocol-Version: "
        + protocolVersion + "\r\n";
    if (withSession) {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (!m_sessionId.empty())
        req += "VSR-Session-ID: " + m_sessionId
            + "\r\nVSR-Session-Proof: Bearer " + m_secret + "\r\n";
    }
    req += "Connection: close\r\nContent-Length: "
        + std::to_string(body.size()) + "\r\n\r\n" + body;

    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(m_opts.limits.responseSeconds));
    const std::size_t cap = m_opts.limits.maxResponseBytes;
    reply.failure = Failure::UNREACHABLE;
    auto waitFor = [&](short events) {
      pollfd p{fd, events, 0};
      for (;;) {
        const int n = ::poll(&p, 1, detail::remainingMs(deadline));
        if (n < 0 && errno == EINTR)
          continue;
        if (n == 0)
          reply.failure = Failure::TIMEOUT;
        return n > 0;
      }
    };

    std::string in;
    bool ok = true;
    std::size_t sent = 0;
    while (ok && sent < req.size()) {
      if (!waitFor(POLLOUT)) {
        ok = false;
        break;
      }
      const ssize_t n =
          ::send(fd, req.data() + sent, req.size() - sent, MSG_NOSIGNAL);
      if (n < 0 && (errno == EINTR || errno == EAGAIN))
        continue;
      if (n <= 0)
        ok = false;
      else
        sent += std::size_t(n);
    }

    std::size_t headerEnd = std::string::npos;
    std::size_t expected = std::string::npos; // total bytes, from headers
    bool complete = false;
    while (ok && !complete) {
      if (!waitFor(POLLIN)) {
        ok = false;
        break;
      }
      char buf[4096];
      const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
      if (n < 0 && (errno == EINTR || errno == EAGAIN))
        continue;
      if (n < 0) {
        ok = false;
        break;
      }
      if (n == 0) {
        // EOF: complete unless a declared length is still outstanding.
        complete = headerEnd != std::string::npos
            && (expected == std::string::npos || in.size() >= expected);
        ok = complete;
        break;
      }
      in.append(buf, std::size_t(n));
      if (in.size() > cap) {
        reply.failure = Failure::TOO_LARGE;
        ok = false;
        break;
      }
      if (headerEnd == std::string::npos) {
        headerEnd = in.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
          const long long len = contentLength(in.substr(0, headerEnd));
          if (len == -2) {
            ok = false; // malformed or duplicate Content-Length
            break;
          }
          if (len >= 0) {
            expected = headerEnd + 4 + std::size_t(len);
            if (expected > cap) {
              reply.failure = Failure::TOO_LARGE;
              ok = false;
              break;
            }
          }
        }
      }
      if (expected != std::string::npos && in.size() >= expected)
        complete = true;
    }
    ::close(fd);
    if (!ok)
      return reply;
    if (in.compare(0, 9, "HTTP/1.1 ") != 0 || in.size() < 12
        || headerEnd == std::string::npos)
      return reply;
    reply.status = std::atoi(in.c_str() + 9);
    if (reply.status <= 0) {
      reply.status = 0;
      return reply;
    }
    reply.failure = Failure::NONE;
    reply.body = in.substr(headerEnd + 4,
        expected == std::string::npos ? std::string::npos
                                      : expected - headerEnd - 4);
#else
    (void)body;
    (void)withSession;
#endif
    return reply;
  }

  // -1: absent; -2: malformed or repeated; else the value.
  static long long contentLength(const std::string &headers)
  {
    long long found = -1;
    std::size_t pos = headers.find("\r\n");
    while (pos != std::string::npos) {
      const std::size_t start = pos + 2;
      pos = headers.find("\r\n", start);
      const std::string h = headers.substr(
          start, pos == std::string::npos ? std::string::npos : pos - start);
      const auto colon = h.find(':');
      if (colon == std::string::npos)
        continue;
      std::string name = h.substr(0, colon);
      for (auto &c : name)
        c = char(std::tolower(static_cast<unsigned char>(c)));
      if (name != "content-length")
        continue;
      std::string v = h.substr(colon + 1);
      const auto b = v.find_first_not_of(" \t");
      const auto e = v.find_last_not_of(" \t");
      v = b == std::string::npos ? std::string() : v.substr(b, e - b + 1);
      if (found != -1 || v.empty() || v.size() > 15
          || v.find_first_not_of("0123456789") != std::string::npos)
        return -2;
      found = std::stoll(v);
    }
    return found;
  }

  integration::descriptor::Descriptor m_desc;
  Options m_opts;
  std::string m_authority;
  int m_errFd{-1};
  int m_selfPipe[2]{-1, -1};
  std::atomic<bool> m_untrusted{false};

  mutable std::mutex m_mutex;
  std::condition_variable m_cv;
  std::string m_sessionId; // in memory only
  std::string m_secret; // in memory only
  double m_renewalSeconds{30.0};
  double m_nextRenewal{0.0}; // on m_opts.clock
  bool m_stopping{false};
  std::atomic<std::uint64_t> m_renewals{0};
  std::thread m_renewer;
};

} // namespace vsr::tools::mcp_proxy

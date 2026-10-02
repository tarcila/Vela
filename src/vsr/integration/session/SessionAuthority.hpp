// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Header-only: the vsr_integration library globs only top-level sources, and
// this ticket may not edit that shared CMake file (see ticket 19).

#include "vsr/integration/Integration.hpp"
// std
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
// posix
#if defined(__linux__)
#include <sys/random.h>
#endif

namespace vsr::integration::session {

/*
 * Logical sessions, launch/session authentication and effective per-target
 * capabilities for one activation of an Integration.
 *
 * One SessionAuthority is created per activation; it generates a fresh launch
 * credential and public launch identity that are never reused. At most one
 * external logical session exists at a time. Local host callers use host
 * authority (the configured ceiling) and never consume the external slot.
 *
 * Restrictions (including Disable agent edits) belong to the application and
 * persist across detach, expiry and replacement attachments. Only the host
 * API can relax them, and never beyond the configured ceiling.
 *
 * No function here logs, and no returned text ever contains a secret.
 */

// Frozen semantic outcome codes (spec §9) produced by this module.
enum class Code
{
  OK,
  OCCUPIED,
  UNAUTHENTICATED,
  INVALID_SESSION_PROOF,
  FORBIDDEN,
  SESSION_EXPIRED
};

// Stable machine-readable name, e.g. "invalid_session_proof".
const char *codeName(Code code);
// Short human explanation; never contains request data or secrets.
const char *codeMessage(Code code);

enum class Capability
{
  READ,
  RENDER,
  EDIT,
  RESYNCHRONIZE
};

struct Target
{
  enum class Kind
  {
    SCENE,
    VIEW
  };

  Kind kind{Kind::SCENE};
  std::uint32_t id{0};

  static Target scene(SceneId id);
  static Target view(ViewId id);
};

struct EffectiveCapabilities
{
  bool read{false};
  bool render{false};
  bool edit{false};
  bool resynchronize{false};
};

// What a request presents. Transport adapters strip their carrier syntax
// (e.g. "Bearer ") before filling this in.
struct Credentials
{
  std::string launchCredential;
  std::string sessionId;
  std::string sessionProof;
};

struct Attachment
{
  Code code{Code::OK};
  std::string sessionId; // 32 lowercase hex; public correlation only
  std::string sessionSecret; // unpadded base64url of 32 random bytes
  double leaseSeconds{0.0};
  double renewalSeconds{0.0};
};

enum class EndReason
{
  DETACHED,
  EXPIRED,
  REVOKED
};

// Monotonic time in seconds.
using MonotonicClock = std::function<double()>;
// Session-ended signal for later tickets (cancellation, cursors).
using SessionEndedCallback =
    std::function<void(const std::string &sessionId, EndReason reason)>;

MonotonicClock steadyClock();

class SessionAuthority
{
 public:
  // Returns null when the integration is inactive or secure randomness is
  // unavailable.
  static std::unique_ptr<SessionAuthority> create(
      const Integration &integration, MonotonicClock clock = steadyClock());

  SessionAuthority(const SessionAuthority &) = delete;
  SessionAuthority &operator=(const SessionAuthority &) = delete;

  // Launch values (host only; for descriptor publication) //

  const std::string &launchId() const;
  const std::string &launchCredential() const;

  // External client calls //

  // Launch-authenticated; creates the single external logical session.
  Attachment attach(std::string_view launchCredential);
  // Launch + current session proof; no capability needed.
  Code authenticate(const Credentials &credentials);
  // Explicit renewal extends the lease from now.
  Code renew(const Credentials &credentials);
  Code detach(const Credentials &credentials);
  // Admission check for a session-scoped action on a target.
  Code authorize(
      const Credentials &credentials, Target target, Capability capability);
  // Safe-point re-check for work admitted under sessionId.
  Code authorizeExecution(
      std::string_view sessionId, Target target, Capability capability);
  EffectiveCapabilities effectiveCapabilities(Target target) const;

  // Host / local operator //

  // Local host authority: the configured ceiling, independent of the
  // external restriction mask. Consumes no session slot.
  bool authorizeLocal(Target target, Capability capability) const;
  // Local whole-session revocation (viewer Disconnect).
  bool revokeSession();
  void setAgentEditsDisabled(bool disabled);
  bool agentEditsDisabled() const;
  void restrict(Target target, Capability capability);
  // Relaxing can never exceed the configured ceiling.
  void relax(Target target, Capability capability);
  // Ends the current session if its lease has passed.
  void expireIfDue();
  bool hasSession() const;
  void setSessionEndedCallback(SessionEndedCallback callback);

 private:
  struct Session
  {
    std::string id;
    std::string secret;
    double expiresAt{0.0};
  };

  struct EndedSession
  {
    std::string id;
    std::string secret;
    EndReason reason{EndReason::DETACHED};
  };

  SessionAuthority(const Integration &integration, MonotonicClock clock);

  bool ceiling(Target target, Capability capability) const;
  bool restricted(Target target, Capability capability) const;
  bool effective(Target target, Capability capability) const;
  Code checkSession(const Credentials &credentials, double now);
  // Caller holds m_mutex; notify(ended) after unlocking.
  void endSession(EndReason reason, std::vector<EndedSession> &ended);
  void notify(const std::vector<EndedSession> &ended);

  const Integration &m_integration;
  MonotonicClock m_clock;
  std::string m_launchId;
  std::string m_launchCredential;

  mutable std::mutex m_mutex;
  std::unique_ptr<Session> m_session;
  EndedSession m_lastEnded;
  bool m_hasLastEnded{false};
  bool m_agentEditsDisabled{false};
  std::vector<std::array<bool, 4>> m_sceneRestrictions;
  std::vector<std::array<bool, 4>> m_viewRestrictions;
  SessionEndedCallback m_onEnded;
};

// Encoding helpers (exposed for tests and the descriptor ticket) //

bool secureRandom(std::uint8_t *out, std::size_t size);
std::string toLowerHex(const std::uint8_t *data, std::size_t size);
std::string toBase64Url(const std::uint8_t *data, std::size_t size);
bool constantTimeEqual(std::string_view a, std::string_view b);

// Inlined definitions ////////////////////////////////////////////////////////

inline const char *codeName(Code code)
{
  switch (code) {
  case Code::OK:
    return "ok";
  case Code::OCCUPIED:
    return "occupied";
  case Code::UNAUTHENTICATED:
    return "unauthenticated";
  case Code::INVALID_SESSION_PROOF:
    return "invalid_session_proof";
  case Code::FORBIDDEN:
    return "forbidden";
  case Code::SESSION_EXPIRED:
    return "session_expired";
  }
  return "unknown";
}

inline const char *codeMessage(Code code)
{
  switch (code) {
  case Code::OK:
    return "ok";
  case Code::OCCUPIED:
    return "the external session slot is occupied";
  case Code::UNAUTHENTICATED:
    return "missing or invalid launch credential";
  case Code::INVALID_SESSION_PROOF:
    return "missing, wrong or stale session proof";
  case Code::FORBIDDEN:
    return "capability absent or not exposed for this target";
  case Code::SESSION_EXPIRED:
    return "the session lease expired";
  }
  return "unknown";
}

inline Target Target::scene(SceneId id)
{
  return {Kind::SCENE, id};
}

inline Target Target::view(ViewId id)
{
  return {Kind::VIEW, id};
}

inline MonotonicClock steadyClock()
{
  return [] {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
  };
}

inline bool secureRandom(std::uint8_t *out, std::size_t size)
{
#if defined(__linux__)
  std::size_t filled = 0;
  while (filled < size) {
    auto n = getrandom(out + filled, size - filled, 0);
    if (n <= 0)
      return false;
    filled += std::size_t(n);
  }
  return true;
#else
  // Unvalidated platforms report unsupported rather than use a weak source.
  (void)out;
  (void)size;
  return false;
#endif
}

inline std::string toLowerHex(const std::uint8_t *data, std::size_t size)
{
  static const char *digits = "0123456789abcdef";
  std::string s;
  s.reserve(size * 2);
  for (std::size_t i = 0; i < size; i++) {
    s.push_back(digits[data[i] >> 4]);
    s.push_back(digits[data[i] & 0xf]);
  }
  return s;
}

inline std::string toBase64Url(const std::uint8_t *data, std::size_t size)
{
  static const char *alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string s;
  std::uint32_t acc = 0;
  int bits = 0;
  for (std::size_t i = 0; i < size; i++) {
    acc = (acc << 8) | data[i];
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      s.push_back(alphabet[(acc >> bits) & 0x3f]);
    }
  }
  if (bits > 0)
    s.push_back(alphabet[(acc << (6 - bits)) & 0x3f]);
  return s;
}

inline bool constantTimeEqual(std::string_view a, std::string_view b)
{
  if (a.size() != b.size())
    return false;
  unsigned char diff = 0;
  for (std::size_t i = 0; i < a.size(); i++)
    diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  return diff == 0;
}

namespace detail {

inline bool randomHex(std::string &out, std::size_t bytes)
{
  std::vector<std::uint8_t> buf(bytes);
  if (!secureRandom(buf.data(), bytes))
    return false;
  out = toLowerHex(buf.data(), bytes);
  return true;
}

inline bool randomBase64Url(std::string &out, std::size_t bytes)
{
  std::vector<std::uint8_t> buf(bytes);
  if (!secureRandom(buf.data(), bytes))
    return false;
  out = toBase64Url(buf.data(), bytes);
  return true;
}

inline std::size_t capIndex(Capability c)
{
  return static_cast<std::size_t>(c);
}

} // namespace detail

inline std::unique_ptr<SessionAuthority> SessionAuthority::create(
    const Integration &integration, MonotonicClock clock)
{
  if (!integration.active() || !clock)
    return {};
  std::unique_ptr<SessionAuthority> a(
      new SessionAuthority(integration, std::move(clock)));
  if (!detail::randomHex(a->m_launchId, 16)
      || !detail::randomBase64Url(a->m_launchCredential, 32))
    return {};
  return a;
}

inline SessionAuthority::SessionAuthority(
    const Integration &integration, MonotonicClock clock)
    : m_integration(integration), m_clock(std::move(clock))
{}

inline const std::string &SessionAuthority::launchId() const
{
  return m_launchId;
}

inline const std::string &SessionAuthority::launchCredential() const
{
  return m_launchCredential;
}

inline Attachment SessionAuthority::attach(std::string_view launchCredential)
{
  Attachment result;
  if (launchCredential.empty()
      || !constantTimeEqual(launchCredential, m_launchCredential)) {
    result.code = Code::UNAUTHENTICATED;
    return result;
  }

  std::vector<EndedSession> ended;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    const double now = m_clock();
    if (m_session && now >= m_session->expiresAt)
      endSession(EndReason::EXPIRED, ended);

    if (m_session) {
      result.code = Code::OCCUPIED;
    } else {
      auto s = std::make_unique<Session>();
      if (!detail::randomHex(s->id, 16)
          || !detail::randomBase64Url(s->secret, 32)) {
        // No secure randomness: refuse rather than weaken.
        result.code = Code::OCCUPIED;
      } else {
        const auto &timing = m_integration.timing();
        s->expiresAt = now + timing.leaseSeconds;
        result.sessionId = s->id;
        result.sessionSecret = s->secret;
        result.leaseSeconds = timing.leaseSeconds;
        result.renewalSeconds = timing.renewalSeconds;
        m_session = std::move(s);
      }
    }
  }
  notify(ended);
  return result;
}

inline Code SessionAuthority::checkSession(
    const Credentials &credentials, double now)
{
  if (credentials.launchCredential.empty()
      || !constantTimeEqual(credentials.launchCredential, m_launchCredential))
    return Code::UNAUTHENTICATED;
  if (credentials.sessionId.empty() || credentials.sessionProof.empty())
    return Code::INVALID_SESSION_PROOF;

  if (m_session && credentials.sessionId == m_session->id
      && constantTimeEqual(credentials.sessionProof, m_session->secret)) {
    if (now >= m_session->expiresAt)
      return Code::SESSION_EXPIRED; // caller ends the session
    return Code::OK;
  }

  if (m_hasLastEnded && m_lastEnded.reason == EndReason::EXPIRED
      && credentials.sessionId == m_lastEnded.id
      && constantTimeEqual(credentials.sessionProof, m_lastEnded.secret))
    return Code::SESSION_EXPIRED;

  return Code::INVALID_SESSION_PROOF;
}

inline Code SessionAuthority::authenticate(const Credentials &credentials)
{
  std::vector<EndedSession> ended;
  Code code;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    code = checkSession(credentials, m_clock());
    if (code == Code::SESSION_EXPIRED && m_session
        && credentials.sessionId == m_session->id)
      endSession(EndReason::EXPIRED, ended);
  }
  notify(ended);
  return code;
}

inline Code SessionAuthority::renew(const Credentials &credentials)
{
  std::vector<EndedSession> ended;
  Code code;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    const double now = m_clock();
    code = checkSession(credentials, now);
    if (code == Code::OK)
      m_session->expiresAt = now + m_integration.timing().leaseSeconds;
    else if (code == Code::SESSION_EXPIRED && m_session
        && credentials.sessionId == m_session->id)
      endSession(EndReason::EXPIRED, ended);
  }
  notify(ended);
  return code;
}

inline Code SessionAuthority::detach(const Credentials &credentials)
{
  std::vector<EndedSession> ended;
  Code code;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    code = checkSession(credentials, m_clock());
    if (code == Code::OK)
      endSession(EndReason::DETACHED, ended);
    else if (code == Code::SESSION_EXPIRED && m_session
        && credentials.sessionId == m_session->id)
      endSession(EndReason::EXPIRED, ended);
  }
  notify(ended);
  return code;
}

inline Code SessionAuthority::authorize(
    const Credentials &credentials, Target target, Capability capability)
{
  const Code code = authenticate(credentials);
  if (code != Code::OK)
    return code;
  std::lock_guard<std::mutex> lock(m_mutex);
  return effective(target, capability) ? Code::OK : Code::FORBIDDEN;
}

inline Code SessionAuthority::authorizeExecution(
    std::string_view sessionId, Target target, Capability capability)
{
  std::vector<EndedSession> ended;
  Code code = Code::OK;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_session && sessionId == m_session->id) {
      if (m_clock() >= m_session->expiresAt) {
        endSession(EndReason::EXPIRED, ended);
        code = Code::SESSION_EXPIRED;
      } else if (!effective(target, capability)) {
        code = Code::FORBIDDEN;
      }
    } else if (m_hasLastEnded && m_lastEnded.reason == EndReason::EXPIRED
        && sessionId == m_lastEnded.id) {
      code = Code::SESSION_EXPIRED;
    } else {
      code = Code::INVALID_SESSION_PROOF;
    }
  }
  notify(ended);
  return code;
}

inline EffectiveCapabilities SessionAuthority::effectiveCapabilities(
    Target target) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  EffectiveCapabilities e;
  e.read = effective(target, Capability::READ);
  e.render = effective(target, Capability::RENDER);
  e.edit = effective(target, Capability::EDIT);
  e.resynchronize = effective(target, Capability::RESYNCHRONIZE);
  return e;
}

inline bool SessionAuthority::authorizeLocal(
    Target target, Capability capability) const
{
  return ceiling(target, capability);
}

inline bool SessionAuthority::revokeSession()
{
  std::vector<EndedSession> ended;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_session)
      return false;
    endSession(EndReason::REVOKED, ended);
  }
  notify(ended);
  return true;
}

inline void SessionAuthority::setAgentEditsDisabled(bool disabled)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_agentEditsDisabled = disabled;
}

inline bool SessionAuthority::agentEditsDisabled() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_agentEditsDisabled;
}

inline void SessionAuthority::restrict(Target target, Capability capability)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto &table = target.kind == Target::Kind::SCENE ? m_sceneRestrictions
                                                   : m_viewRestrictions;
  if (table.size() <= target.id)
    table.resize(std::size_t(target.id) + 1, {false, false, false, false});
  table[target.id][detail::capIndex(capability)] = true;
}

inline void SessionAuthority::relax(Target target, Capability capability)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto &table = target.kind == Target::Kind::SCENE ? m_sceneRestrictions
                                                   : m_viewRestrictions;
  if (target.id < table.size())
    table[target.id][detail::capIndex(capability)] = false;
}

inline void SessionAuthority::expireIfDue()
{
  std::vector<EndedSession> ended;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_session && m_clock() >= m_session->expiresAt)
      endSession(EndReason::EXPIRED, ended);
  }
  notify(ended);
}

inline bool SessionAuthority::hasSession() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_session != nullptr;
}

inline void SessionAuthority::setSessionEndedCallback(
    SessionEndedCallback callback)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_onEnded = std::move(callback);
}

inline bool SessionAuthority::ceiling(
    Target target, Capability capability) const
{
  if (target.kind == Target::Kind::SCENE) {
    if (!m_integration.isRegisteredScene(target.id))
      return false;
    const auto g = m_integration.sceneGrants(target.id);
    switch (capability) {
    case Capability::READ:
      return g.read;
    case Capability::EDIT:
      return g.read && g.edit;
    case Capability::RESYNCHRONIZE:
      return g.read && g.resynchronize;
    case Capability::RENDER:
      return false; // scenes are not renderable targets
    }
    return false;
  }

  if (!m_integration.isRegisteredView(target.id))
    return false;
  const auto g = m_integration.viewGrants(target.id);
  switch (capability) {
  case Capability::READ:
    return g.read;
  case Capability::RENDER:
    return g.read && g.render;
  case Capability::EDIT:
  case Capability::RESYNCHRONIZE:
    return false; // views expose only read/render
  }
  return false;
}

inline bool SessionAuthority::restricted(
    Target target, Capability capability) const
{
  const auto &table = target.kind == Target::Kind::SCENE ? m_sceneRestrictions
                                                         : m_viewRestrictions;
  return target.id < table.size()
      && table[target.id][detail::capIndex(capability)];
}

inline bool SessionAuthority::effective(
    Target target, Capability capability) const
{
  if (!ceiling(target, capability) || restricted(target, capability))
    return false;
  if (capability == Capability::EDIT && m_agentEditsDisabled)
    return false;
  // Actions depend on current read; withdrawing read withdraws them.
  if (capability != Capability::READ && restricted(target, Capability::READ))
    return false;
  return true;
}

inline void SessionAuthority::endSession(
    EndReason reason, std::vector<EndedSession> &ended)
{
  m_lastEnded = {m_session->id, m_session->secret, reason};
  m_hasLastEnded = true;
  ended.push_back({m_session->id, {}, reason});
  m_session.reset();
}

inline void SessionAuthority::notify(const std::vector<EndedSession> &ended)
{
  if (ended.empty())
    return;
  SessionEndedCallback cb;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    cb = m_onEnded;
  }
  if (cb) {
    for (const auto &e : ended)
      cb(e.id, e.reason);
  }
}

} // namespace vsr::integration::session

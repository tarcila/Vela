// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/core/Logging.hpp"
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/session/SessionAuthority.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace vsr::integration;
using namespace vsr::integration::session;

namespace {

// Installs a global logging callback for one scope and resets it on exit,
// including when an assertion throws, so no later test logs into a
// destroyed capture. The logging API exposes no getter for the previous
// callback, so reset means setNoLogging().
struct ScopedLoggingCallback
{
  explicit ScopedLoggingCallback(vsr::core::LoggingCallback cb)
  {
    vsr::core::setLoggingCallback(std::move(cb));
  }
  ~ScopedLoggingCallback()
  {
    vsr::core::setNoLogging();
  }
  ScopedLoggingCallback(const ScopedLoggingCallback &) = delete;
  ScopedLoggingCallback &operator=(const ScopedLoggingCallback &) = delete;
};

bool isLowerHex(const std::string &s, std::size_t n)
{
  if (s.size() != n)
    return false;
  for (char c : s) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      return false;
  }
  return true;
}

bool isBase64Url(const std::string &s, std::size_t n)
{
  if (s.size() != n)
    return false;
  for (char c : s) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
        || (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok)
      return false;
  }
  return true;
}

bool confirmReady(SceneId)
{
  return true;
}

struct Fixture
{
  vsr::scene::Scene scene;
  vsr::scene::Scene otherScene;
  Integration integration;
  SceneId sceneId{INVALID_SCENE_ID};
  SceneId readOnlyScene{INVALID_SCENE_ID};
  ViewId viewId{INVALID_VIEW_ID};
  double now{1000.0};
  std::unique_ptr<SessionAuthority> authority;

  explicit Fixture(SessionTiming timing = {})
  {
    SceneRegistration s;
    s.name = "editable";
    s.scene = &scene;
    s.grants.read = true;
    s.grants.edit = true;
    s.normalSyncHook = confirmReady;
    sceneId = *integration.registerScene(s);

    SceneRegistration r;
    r.name = "readonly";
    r.scene = &otherScene;
    r.grants.read = true;
    readOnlyScene = *integration.registerScene(r);

    ViewRegistration v;
    v.name = "view";
    v.scene = sceneId;
    v.grants.read = true;
    v.grants.render = true;
    viewId = *integration.registerView(v);

    REQUIRE(integration.activate(timing).ok());
    authority = SessionAuthority::create(integration, [this] { return now; });
    REQUIRE(authority);
  }

  Credentials credentialsFor(const Attachment &a) const
  {
    return {authority->launchCredential(), a.sessionId, a.sessionSecret};
  }
};

} // namespace

SCENARIO("Each activation generates fresh launch values", "[integration]")
{
  Fixture a;
  Fixture b;

  THEN("the launch credential is 43-char unpadded base64url")
  {
    REQUIRE(isBase64Url(a.authority->launchCredential(), 43));
    REQUIRE(isBase64Url(b.authority->launchCredential(), 43));
  }
  THEN("the launch identity is 32 lowercase hex")
  {
    REQUIRE(isLowerHex(a.authority->launchId(), 32));
    REQUIRE(isLowerHex(b.authority->launchId(), 32));
  }
  THEN("nothing is reused across activations")
  {
    REQUIRE(a.authority->launchCredential() != b.authority->launchCredential());
    REQUIRE(a.authority->launchId() != b.authority->launchId());
  }
  THEN("another activation's credential is not accepted")
  {
    auto att = a.authority->attach(b.authority->launchCredential());
    REQUIRE(att.code == Code::UNAUTHENTICATED);
    REQUIRE(att.sessionId.empty());
    REQUIRE(att.sessionSecret.empty());
  }
}

SCENARIO("Sessions need an active integration", "[integration]")
{
  Integration integration;
  REQUIRE_FALSE(SessionAuthority::create(integration));
}

SCENARIO("Base64url encoding is canonical and unpadded", "[integration]")
{
  const std::uint8_t bytes[] = {0xfb, 0xff, 0xbf};
  REQUIRE(toBase64Url(bytes, 3) == "-_-_");
  REQUIRE(toBase64Url(bytes, 1) == "-w");
  REQUIRE(toBase64Url(bytes, 2) == "-_8");
  REQUIRE(toLowerHex(bytes, 3) == "fbffbf");
}

SCENARIO("Attachment creates the single external session", "[integration]")
{
  Fixture f;

  WHEN("attaching without the launch credential")
  {
    THEN("it is unauthenticated")
    {
      REQUIRE(f.authority->attach("").code == Code::UNAUTHENTICATED);
      REQUIRE(f.authority->attach("wrong").code == Code::UNAUTHENTICATED);
      REQUIRE_FALSE(f.authority->hasSession());
    }
  }

  WHEN("attaching with the launch credential")
  {
    auto att = f.authority->attach(f.authority->launchCredential());

    THEN("a fresh public ID and independent secret are returned")
    {
      REQUIRE(att.code == Code::OK);
      REQUIRE(isLowerHex(att.sessionId, 32));
      REQUIRE(isBase64Url(att.sessionSecret, 43));
      REQUIRE(att.sessionSecret != f.authority->launchCredential());
      REQUIRE(att.sessionId != f.authority->launchId());
    }
    THEN("configured lease and renewal durations are reported")
    {
      REQUIRE(att.leaseSeconds == 120.0);
      REQUIRE(att.renewalSeconds == 30.0);
    }
    THEN("a second attach is occupied with no incumbent data")
    {
      auto second = f.authority->attach(f.authority->launchCredential());
      REQUIRE(second.code == Code::OCCUPIED);
      REQUIRE(second.sessionId.empty());
      REQUIRE(second.sessionSecret.empty());
      REQUIRE(second.leaseSeconds == 0.0);
      REQUIRE(f.authority->authenticate(f.credentialsFor(att)) == Code::OK);
    }
    THEN("local host callers consume no slot")
    {
      REQUIRE(f.authority->authorizeLocal(
          Target::scene(f.sceneId), Capability::EDIT));
      REQUIRE(f.authority->attach(f.authority->launchCredential()).code
          == Code::OCCUPIED);
    }
  }

  WHEN("only local callers act")
  {
    REQUIRE(f.authority->authorizeLocal(
        Target::scene(f.sceneId), Capability::READ));
    THEN("the external slot remains free")
    {
      REQUIRE_FALSE(f.authority->hasSession());
      REQUIRE(f.authority->attach(f.authority->launchCredential()).code
          == Code::OK);
    }
  }
}

SCENARIO("Non-default durations reach the attaching client", "[integration]")
{
  Fixture f(SessionTiming{10.0, 4.0});
  auto att = f.authority->attach(f.authority->launchCredential());
  REQUIRE(att.leaseSeconds == 10.0);
  REQUIRE(att.renewalSeconds == 4.0);
}

SCENARIO(
    "Session-scoped calls need launch credential and proof", "[integration]")
{
  Fixture f;
  auto att = f.authority->attach(f.authority->launchCredential());
  REQUIRE(att.code == Code::OK);
  const auto good = f.credentialsFor(att);
  const auto read = Target::scene(f.sceneId);

  THEN("valid credentials authorize a granted capability")
  {
    REQUIRE(f.authority->authorize(good, read, Capability::READ) == Code::OK);
  }
  THEN("missing or invalid launch credential is unauthenticated")
  {
    auto c = good;
    c.launchCredential.clear();
    REQUIRE(f.authority->authorize(c, read, Capability::READ)
        == Code::UNAUTHENTICATED);
    c.launchCredential = att.sessionSecret;
    REQUIRE(f.authority->authorize(c, read, Capability::READ)
        == Code::UNAUTHENTICATED);
    REQUIRE(f.authority->renew(c) == Code::UNAUTHENTICATED);
    REQUIRE(f.authority->detach(c) == Code::UNAUTHENTICATED);
  }
  THEN("missing or wrong proof is invalid_session_proof")
  {
    auto c = good;
    c.sessionProof.clear();
    REQUIRE(f.authority->authorize(c, read, Capability::READ)
        == Code::INVALID_SESSION_PROOF);
    c.sessionProof = f.authority->launchCredential();
    REQUIRE(f.authority->authorize(c, read, Capability::READ)
        == Code::INVALID_SESSION_PROOF);
    c = good;
    c.sessionId.clear();
    REQUIRE(f.authority->authorize(c, read, Capability::READ)
        == Code::INVALID_SESSION_PROOF);
    c.sessionId = std::string(32, '0');
    REQUIRE(f.authority->renew(c) == Code::INVALID_SESSION_PROOF);
    REQUIRE(f.authority->detach(c) == Code::INVALID_SESSION_PROOF);
    REQUIRE(f.authority->hasSession());
  }
  THEN("valid proof without the capability is forbidden")
  {
    REQUIRE(f.authority->authorize(
                good, Target::scene(f.readOnlyScene), Capability::EDIT)
        == Code::FORBIDDEN);
    REQUIRE(f.authority->authorize(good, read, Capability::RENDER)
        == Code::FORBIDDEN);
    REQUIRE(f.authority->authorize(good, Target::scene(99), Capability::READ)
        == Code::FORBIDDEN);
  }
  THEN("the three rejection codes have distinct frozen names")
  {
    REQUIRE(std::string(codeName(Code::UNAUTHENTICATED)) == "unauthenticated");
    REQUIRE(std::string(codeName(Code::INVALID_SESSION_PROOF))
        == "invalid_session_proof");
    REQUIRE(std::string(codeName(Code::FORBIDDEN)) == "forbidden");
    REQUIRE(std::string(codeName(Code::OCCUPIED)) == "occupied");
    REQUIRE(std::string(codeName(Code::SESSION_EXPIRED)) == "session_expired");
  }
}

SCENARIO("Effective capabilities are grants narrowed by host restrictions",
    "[integration]")
{
  Fixture f;
  auto att = f.authority->attach(f.authority->launchCredential());
  const auto creds = f.credentialsFor(att);
  const auto scene = Target::scene(f.sceneId);
  const auto view = Target::view(f.viewId);

  THEN("attachment inherits the configured grants")
  {
    auto e = f.authority->effectiveCapabilities(scene);
    REQUIRE(e.read);
    REQUIRE(e.edit);
    REQUIRE_FALSE(e.resynchronize);
    REQUIRE_FALSE(e.render);
    auto v = f.authority->effectiveCapabilities(view);
    REQUIRE(v.read);
    REQUIRE(v.render);
    auto ro =
        f.authority->effectiveCapabilities(Target::scene(f.readOnlyScene));
    REQUIRE(ro.read);
    REQUIRE_FALSE(ro.edit);
  }

  WHEN("edit is admitted, then agent edits are disabled before execution")
  {
    REQUIRE(f.authority->authorize(creds, scene, Capability::EDIT) == Code::OK);
    f.authority->setAgentEditsDisabled(true);
    THEN("execution re-check forbids it while reads continue")
    {
      REQUIRE(f.authority->authorizeExecution(
                  att.sessionId, scene, Capability::EDIT)
          == Code::FORBIDDEN);
      REQUIRE(f.authority->authorizeExecution(
                  att.sessionId, scene, Capability::READ)
          == Code::OK);
      REQUIRE(
          f.authority->authorize(creds, view, Capability::RENDER) == Code::OK);
      REQUIRE(f.authority->hasSession());
    }
    THEN("local host authority is unaffected")
    {
      REQUIRE(f.authority->authorizeLocal(scene, Capability::EDIT));
    }
  }

  WHEN("read is withdrawn on a view")
  {
    f.authority->restrict(view, Capability::READ);
    THEN("dependent render is also unauthorized")
    {
      REQUIRE(f.authority->authorize(creds, view, Capability::READ)
          == Code::FORBIDDEN);
      REQUIRE(f.authority->authorize(creds, view, Capability::RENDER)
          == Code::FORBIDDEN);
      REQUIRE(f.authority->renew(creds) == Code::OK);
    }
    THEN("relaxing read restores the configured render grant")
    {
      f.authority->relax(view, Capability::READ);
      REQUIRE(
          f.authority->authorize(creds, view, Capability::RENDER) == Code::OK);
    }
  }

  WHEN("the host relaxes beyond the ceiling")
  {
    f.authority->relax(scene, Capability::RESYNCHRONIZE);
    f.authority->relax(Target::scene(f.readOnlyScene), Capability::EDIT);
    THEN("nothing outside the configured grants becomes available")
    {
      REQUIRE(f.authority->authorize(creds, scene, Capability::RESYNCHRONIZE)
          == Code::FORBIDDEN);
      REQUIRE(f.authority->authorize(
                  creds, Target::scene(f.readOnlyScene), Capability::EDIT)
          == Code::FORBIDDEN);
    }
  }
}

SCENARIO("Disable agent edits persists across session changes", "[integration]")
{
  Fixture f;
  const auto scene = Target::scene(f.sceneId);
  const auto launch = f.authority->launchCredential();

  auto first = f.authority->attach(launch);
  f.authority->setAgentEditsDisabled(true);

  WHEN("the session detaches and a replacement attaches")
  {
    REQUIRE(f.authority->detach(f.credentialsFor(first)) == Code::OK);
    auto second = f.authority->attach(launch);
    REQUIRE(second.code == Code::OK);
    THEN("edits remain disabled")
    {
      REQUIRE(f.authority->authorize(
                  f.credentialsFor(second), scene, Capability::EDIT)
          == Code::FORBIDDEN);
    }
  }

  WHEN("the session expires and a replacement attaches")
  {
    f.now += 121.0;
    auto second = f.authority->attach(launch);
    REQUIRE(second.code == Code::OK);
    THEN("edits remain disabled until the host relaxes them")
    {
      const auto c = f.credentialsFor(second);
      REQUIRE(f.authority->authorize(c, scene, Capability::EDIT)
          == Code::FORBIDDEN);
      f.authority->setAgentEditsDisabled(false);
      REQUIRE(f.authority->authorize(c, scene, Capability::EDIT) == Code::OK);
    }
  }

  WHEN("the session is revoked locally and a replacement attaches")
  {
    REQUIRE(f.authority->revokeSession());
    auto second = f.authority->attach(launch);
    THEN("edits remain disabled")
    {
      REQUIRE(f.authority->authorize(
                  f.credentialsFor(second), scene, Capability::EDIT)
          == Code::FORBIDDEN);
    }
  }
}

SCENARIO(
    "Leases renew only explicitly against the monotonic clock", "[integration]")
{
  Fixture f;
  auto att = f.authority->attach(f.authority->launchCredential());
  const auto creds = f.credentialsFor(att);
  const auto scene = Target::scene(f.sceneId);

  WHEN("only ordinary traffic occurs")
  {
    f.now += 100.0;
    REQUIRE(f.authority->authorize(creds, scene, Capability::READ) == Code::OK);
    f.now += 21.0;
    THEN("the lease still expires from attachment time")
    {
      REQUIRE(f.authority->authorize(creds, scene, Capability::READ)
          == Code::SESSION_EXPIRED);
      REQUIRE_FALSE(f.authority->hasSession());
      REQUIRE(f.authority->renew(creds) == Code::SESSION_EXPIRED);
    }
  }

  WHEN("the client renews")
  {
    f.now += 100.0;
    REQUIRE(f.authority->renew(creds) == Code::OK);
    THEN("the lease extends from renewal time")
    {
      f.now += 119.0;
      REQUIRE(f.authority->authenticate(creds) == Code::OK);
      f.now += 1.0;
      REQUIRE(f.authority->authenticate(creds) == Code::SESSION_EXPIRED);
    }
  }

  WHEN("the host checks expiry at a safe point")
  {
    std::vector<std::pair<std::string, EndReason>> ended;
    f.authority->setSessionEndedCallback(
        [&](const std::string &id, EndReason r) { ended.push_back({id, r}); });
    f.now += 120.0;
    f.authority->expireIfDue();
    THEN("the session ends with an expired signal and execution rejects")
    {
      REQUIRE_FALSE(f.authority->hasSession());
      REQUIRE(ended.size() == 1);
      REQUIRE(ended[0].first == att.sessionId);
      REQUIRE(ended[0].second == EndReason::EXPIRED);
      REQUIRE(f.authority->authorizeExecution(
                  att.sessionId, scene, Capability::READ)
          == Code::SESSION_EXPIRED);
    }
  }
}

SCENARIO("Ended sessions never revive", "[integration]")
{
  Fixture f;
  const auto launch = f.authority->launchCredential();
  const auto scene = Target::scene(f.sceneId);
  auto first = f.authority->attach(launch);
  const auto old = f.credentialsFor(first);

  std::vector<EndReason> reasons;
  f.authority->setSessionEndedCallback(
      [&](const std::string &, EndReason r) { reasons.push_back(r); });

  auto checkFresh = [&](EndReason expected) {
    REQUIRE(reasons.size() == 1);
    REQUIRE(reasons[0] == expected);
    REQUIRE(f.authority->authenticate(old) == Code::INVALID_SESSION_PROOF);
    REQUIRE(f.authority->authorizeExecution(
                first.sessionId, scene, Capability::READ)
        == Code::INVALID_SESSION_PROOF);
    auto next = f.authority->attach(launch);
    REQUIRE(next.code == Code::OK);
    REQUIRE(next.sessionId != first.sessionId);
    REQUIRE(next.sessionSecret != first.sessionSecret);
    REQUIRE(f.authority->authenticate(old) == Code::INVALID_SESSION_PROOF);
    REQUIRE(f.authority->authenticate(f.credentialsFor(next)) == Code::OK);
  };

  WHEN("the client detaches")
  {
    REQUIRE(f.authority->detach(old) == Code::OK);
    THEN("the old proof is stale and a new attach is fresh")
    {
      checkFresh(EndReason::DETACHED);
      REQUIRE(f.authority->detach(old) == Code::INVALID_SESSION_PROOF);
    }
  }

  WHEN("the host revokes the whole session")
  {
    REQUIRE(f.authority->revokeSession());
    THEN("the old proof is stale and a new attach is fresh")
    {
      checkFresh(EndReason::REVOKED);
      REQUIRE(f.authority->renew(old) == Code::INVALID_SESSION_PROOF);
    }
  }

  WHEN("the lease expires")
  {
    f.now += 200.0;
    auto next = f.authority->attach(launch);
    THEN("a new attach replaces it and the old secret stays dead")
    {
      REQUIRE(next.code == Code::OK);
      REQUIRE(next.sessionId != first.sessionId);
      REQUIRE(reasons.size() == 1);
      REQUIRE(reasons[0] == EndReason::EXPIRED);
      REQUIRE(f.authority->authenticate(old) != Code::OK);
      REQUIRE(f.authority->renew(old) != Code::OK);
    }
  }

  WHEN("no session exists")
  {
    REQUIRE(f.authority->detach(old) == Code::OK);
    THEN("revocation is a no-op")
    {
      REQUIRE_FALSE(f.authority->revokeSession());
    }
  }
}

SCENARIO("Secrets never appear in logs or error text", "[integration]")
{
  std::vector<std::string> logs;
  std::vector<std::string> texts;
  std::string launch;
  std::string sessionSecret;
  {
    ScopedLoggingCallback logging(
        [&](vsr::core::LogLevel, std::string msg) { logs.push_back(msg); });

    Fixture f;
    launch = f.authority->launchCredential();
    auto att = f.authority->attach(launch);
    sessionSecret = att.sessionSecret;
    auto creds = f.credentialsFor(att);
    f.authority->attach(launch);
    f.authority->authorize(
        creds, Target::scene(f.readOnlyScene), Capability::EDIT);
    auto bad = creds;
    bad.sessionProof = "x" + att.sessionSecret;
    f.authority->authorize(bad, Target::scene(f.sceneId), Capability::READ);
    f.authority->detach(creds);
    f.now += 500.0;
    f.authority->renew(creds);
  } // logging callback reset here

  REQUIRE_FALSE(launch.empty());
  REQUIRE_FALSE(sessionSecret.empty());
  texts = logs;
  for (auto c : {Code::OK,
           Code::OCCUPIED,
           Code::UNAUTHENTICATED,
           Code::INVALID_SESSION_PROOF,
           Code::FORBIDDEN,
           Code::SESSION_EXPIRED}) {
    texts.push_back(codeName(c));
    texts.push_back(codeMessage(c));
  }
  for (const auto &t : texts) {
    REQUIRE(t.find(launch) == std::string::npos);
    REQUIRE(t.find(sessionSecret) == std::string::npos);
  }
}

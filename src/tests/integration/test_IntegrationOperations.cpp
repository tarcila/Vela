// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/activity/Activity.hpp"
#include "vsr/integration/inspection/Inspection.hpp"
#include "vsr/integration/operations/Operations.hpp"
#include "vsr/integration/session/SessionAuthority.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace vsr::integration;
using namespace vsr::integration::operations;
using session::Attachment;
using session::Code;

namespace {

bool confirmReady(SceneId)
{
  return true;
}

// A generic test operation kind whose behaviour each test scripts.
struct FakeOp
{
  int runs{0};
  std::function<Step(StepContext &, int)> script;

  Executor executor()
  {
    return [this](StepContext &ctx) {
      ++runs;
      if (script)
        return script(ctx, runs);
      Step s;
      s.result = "done";
      return s;
    };
  }
};

struct Fixture
{
  vsr::scene::Scene scene;
  Integration integration;
  SceneId sceneId{INVALID_SCENE_ID};
  ViewId viewId{INVALID_VIEW_ID};
  double now{1000.0};
  std::unique_ptr<session::SessionAuthority> authority;
  std::unique_ptr<OperationEngine> engine;
  Attachment attachment;
  Credentials creds;
  std::vector<std::unique_ptr<FakeOp>> ops;

  explicit Fixture(Limits limits = {})
  {
    SceneRegistration s;
    s.name = "scene";
    s.scene = &scene;
    s.grants.read = true;
    s.grants.edit = true;
    s.normalSyncHook = confirmReady;
    sceneId = *integration.registerScene(s);

    ViewRegistration v;
    v.name = "view";
    v.scene = sceneId;
    v.grants.read = true;
    v.grants.render = true;
    viewId = *integration.registerView(v);

    REQUIRE(integration.activate().ok());
    authority =
        session::SessionAuthority::create(integration, [this] { return now; });
    REQUIRE(authority);
    engine = OperationEngine::create(
        integration, *authority, limits, [this] { return now; });
    REQUIRE(engine);
    attach();
  }

  void attach()
  {
    attachment = authority->attach(authority->launchCredential());
    REQUIRE(attachment.code == Code::OK);
    creds = {authority->launchCredential(),
        attachment.sessionId,
        attachment.sessionSecret};
  }

  OperationRef ref(std::uint64_t id) const
  {
    return {attachment.sessionId, id};
  }

  FakeOp &newOp()
  {
    ops.push_back(std::make_unique<FakeOp>());
    return *ops.back();
  }

  Request request(FakeOp &op,
      std::string args = "{}",
      double deadline = 0.0,
      Capability cap = Capability::READ)
  {
    Request r;
    r.kind = "test";
    r.target = session::Target::scene(sceneId);
    r.capability = cap;
    r.canonicalArguments = std::move(args);
    r.deadlineSeconds = deadline;
    r.executor = op.executor();
    return r;
  }

  Admission submit(std::uint64_t id, std::string args = "{}", double dl = 0.0)
  {
    return engine->submit(creds, ref(id), request(newOp(), args, dl));
  }

  Status status(std::uint64_t id)
  {
    return engine->status(creds, ref(id));
  }
};

} // namespace

SCENARIO("Request IDs are integers in [1, 2^53-1]", "[integration]")
{
  REQUIRE_FALSE(validRequestId(0));
  REQUIRE(validRequestId(1));
  REQUIRE(validRequestId(9007199254740991ull));
  REQUIRE_FALSE(validRequestId(9007199254740992ull));

  REQUIRE(requestIdFromNumber(1.0) == 1u);
  REQUIRE(requestIdFromNumber(9007199254740991.0) == 9007199254740991ull);
  REQUIRE_FALSE(requestIdFromNumber(0.0));
  REQUIRE_FALSE(requestIdFromNumber(-1.0));
  REQUIRE_FALSE(requestIdFromNumber(1.5));
  REQUIRE_FALSE(requestIdFromNumber(9007199254740992.0));
  REQUIRE_FALSE(requestIdFromNumber(std::nan("")));

  Fixture f;
  REQUIRE(f.submit(0).code == Outcome::INVALID_REQUEST);
  REQUIRE(f.submit(9007199254740992ull).code == Outcome::INVALID_REQUEST);
  REQUIRE(f.engine->highWater(f.attachment.sessionId) == 0);
  REQUIRE(f.submit(9007199254740991ull).code == Outcome::OK);
  // Never wraps: the maximum ID exhausts this session's namespace.
  REQUIRE(f.submit(1).code == Outcome::OUTCOME_UNAVAILABLE);
}

SCENARIO("Frozen outcome and progress names", "[integration]")
{
  REQUIRE(std::string(outcomeName(Outcome::BUSY)) == "busy");
  REQUIRE(std::string(outcomeName(Outcome::OUTCOME_UNAVAILABLE))
      == "outcome_unavailable");
  REQUIRE(std::string(outcomeName(Outcome::REQUEST_ID_CONFLICT))
      == "request_id_conflict");
  REQUIRE(std::string(outcomeName(Outcome::CANCELLED)) == "cancelled");
  REQUIRE(std::string(outcomeName(Outcome::TIMEOUT)) == "timeout");
  REQUIRE(std::string(outcomeName(Outcome::UNKNOWN)) == "unknown");
  REQUIRE(std::string(progressName(Progress::QUEUED)) == "queued");
  REQUIRE(std::string(progressName(Progress::RUNNING)) == "running");
  REQUIRE(std::string(progressName(Progress::YIELDED)) == "yielded");
  REQUIRE(
      std::string(progressName(Progress::SYNCHRONIZING)) == "synchronizing");
  REQUIRE(std::string(progressName(Progress::AWAITING_CLEANUP))
      == "awaiting_cleanup");
}

SCENARIO("One high-water mark per session with gaps and out-of-order sends",
    "[integration]")
{
  Fixture f;
  REQUIRE(f.submit(3).code == Outcome::OK);
  REQUIRE(f.engine->highWater(f.attachment.sessionId) == 3);
  REQUIRE(f.submit(10).code == Outcome::OK); // gaps allowed
  REQUIRE(
      f.submit(9).code == Outcome::OUTCOME_UNAVAILABLE); // 10 admitted first
  REQUIRE(f.status(9).code == Outcome::OUTCOME_UNAVAILABLE);
  REQUIRE(f.status(11).code == Outcome::UNKNOWN); // maybe in transit

  f.integration.service();
  auto s = f.status(10);
  REQUIRE(s.code == Outcome::OK);
  REQUIRE(s.kind == "test");
  REQUIRE(s.terminal == Outcome::OK);
  REQUIRE(s.result == "done");

  GIVEN("a session mismatch in the operation_ref")
  {
    auto a = f.engine->submit(f.creds,
        {"0123456789abcdef0123456789abcdef", 20},
        f.request(f.newOp()));
    REQUIRE(a.code == Outcome::INVALID_REQUEST);
    REQUIRE(f.engine->highWater(f.attachment.sessionId) == 10);
  }
}

SCENARIO("Pre-admission rejection consumes nothing (5 busy, 6 admitted, 5)",
    "[integration]")
{
  Limits limits;
  limits.sceneSlots = 1;
  Fixture f(limits);

  REQUIRE(f.submit(1).code == Outcome::OK);
  REQUIRE(f.engine->slotsInUse() == 1);

  auto busy = f.submit(5);
  REQUIRE(busy.code == Outcome::BUSY);
  REQUIRE(f.engine->highWater(f.attachment.sessionId) == 1);
  REQUIRE(f.status(5).code == Outcome::UNKNOWN);

  // Wrong proof is also rejected before admission.
  Credentials bad = f.creds;
  bad.sessionProof = "wrong";
  REQUIRE(f.engine->submit(bad, f.ref(5), f.request(f.newOp())).code
      == Outcome::INVALID_SESSION_PROOF);
  REQUIRE(f.engine->highWater(f.attachment.sessionId) == 1);

  f.integration.service();
  REQUIRE(f.engine->slotsInUse() == 0);
  REQUIRE(f.submit(6).code == Outcome::OK);
  auto retry = f.submit(5);
  REQUIRE(retry.code == Outcome::OUTCOME_UNAVAILABLE);
  f.integration.service();
  REQUIRE(f.ops.back()->runs == 0); // 5 never executed
}

SCENARIO("Identical retries replay; conflicting retries are rejected",
    "[integration]")
{
  Limits limits;
  limits.sceneSlots = 2;
  Fixture f(limits);

  auto &op = f.newOp();
  REQUIRE(f.engine->submit(f.creds, f.ref(1), f.request(op, "{\"a\":1}")).code
      == Outcome::OK);
  f.integration.service();
  REQUIRE(op.runs == 1);

  // Fill the pool.
  REQUIRE(f.submit(2).code == Outcome::OK);
  REQUIRE(f.submit(3).code == Outcome::OK);
  REQUIRE(f.submit(4).code == Outcome::BUSY);

  auto &again = f.newOp();
  auto replay =
      f.engine->submit(f.creds, f.ref(1), f.request(again, "{\"a\":1}"));
  REQUIRE(replay.code == Outcome::OK);
  REQUIRE(replay.replayed);
  REQUIRE(replay.status.terminal == Outcome::OK);
  REQUIRE(replay.status.result == "done");
  REQUIRE(f.engine->slotsInUse() == 2);

  auto conflict =
      f.engine->submit(f.creds, f.ref(1), f.request(again, "{\"a\":2}"));
  REQUIRE(conflict.code == Outcome::REQUEST_ID_CONFLICT);
  auto conflictCap = f.engine->submit(
      f.creds, f.ref(1), f.request(again, "{\"a\":1}", 0.0, Capability::EDIT));
  REQUIRE(conflictCap.code == Outcome::REQUEST_ID_CONFLICT);

  f.integration.service();
  REQUIRE(again.runs == 0);
  REQUIRE(op.runs == 1);
  REQUIRE(f.status(1).result == "done"); // record unchanged
}

SCENARIO("Evicted IDs at or below high-water never execute", "[integration]")
{
  Limits limits;
  limits.statusRecords = 2;
  Fixture f(limits);

  REQUIRE(f.submit(1).code == Outcome::OK);
  f.integration.service();
  REQUIRE(f.submit(2).code == Outcome::OK);
  f.integration.service();
  REQUIRE(f.submit(3).code == Outcome::OK); // evicts record 1
  f.integration.service();

  REQUIRE(f.status(1).code == Outcome::OUTCOME_UNAVAILABLE);
  auto &op = f.newOp();
  REQUIRE(f.engine->submit(f.creds, f.ref(1), f.request(op)).code
      == Outcome::OUTCOME_UNAVAILABLE);
  f.integration.service();
  REQUIRE(op.runs == 0);

  GIVEN("status storage full of active work")
  {
    Limits tight;
    tight.statusRecords = 1;
    Fixture g(tight);
    REQUIRE(g.submit(1).code == Outcome::OK);
    REQUIRE(g.submit(2).code == Outcome::BUSY); // cannot evict active work
    REQUIRE(g.engine->highWater(g.attachment.sessionId) == 1);
  }
}

SCENARIO(
    "Local callers share the pool with host-local correlation", "[integration]")
{
  Limits limits;
  limits.sceneSlots = 2;
  Fixture f(limits);

  auto &local = f.newOp();
  auto la = f.engine->submitLocal(f.request(local));
  REQUIRE(la.code == Outcome::OK);
  REQUIRE(f.engine->highWater(f.attachment.sessionId) == 0);

  REQUIRE(f.submit(1).code == Outcome::OK);
  // Pool full: both local and external callers are rejected busy.
  REQUIRE(f.engine->submitLocal(f.request(f.newOp())).code == Outcome::BUSY);
  REQUIRE(f.submit(2).code == Outcome::BUSY);

  REQUIRE(f.engine->statusLocal(la.ref).progress == Progress::QUEUED);
  f.integration.service();
  REQUIRE(local.runs == 1);
  auto s = f.engine->statusLocal(la.ref);
  REQUIRE(s.terminal == Outcome::OK);

  // Local correlation is not the session's request ID.
  auto second = f.engine->submitLocal(f.request(f.newOp()));
  REQUIRE(second.ref.id != la.ref.id);
  REQUIRE(f.status(1).terminal == Outcome::OK);

  // Local work needs no external session.
  REQUIRE(f.authority->revokeSession());
  REQUIRE(f.engine->submitLocal(f.request(f.newOp())).code == Outcome::OK);
}

SCENARIO("Control path is bounded, prioritized and coalesced", "[integration]")
{
  Limits limits;
  limits.sceneSlots = 1;
  Fixture f(limits);
  REQUIRE(f.engine->controlGate().capacity() == 8);

  REQUIRE(f.submit(1).code == Outcome::OK); // scene pool full
  REQUIRE(f.submit(2).code == Outcome::BUSY);

  // Control calls remain serviceable with the scene pool full.
  REQUIRE(f.engine->renew(f.creds) == Outcome::OK);
  REQUIRE(f.status(1).progress == Progress::QUEUED);

  auto &gate = f.engine->controlGate();
  std::vector<ControlGate::Permit> held;
  for (int i = 0; i < 7; ++i) {
    held.push_back(gate.acquire(ControlKind::LOOKUP, "poll"));
    REQUIRE(held.back().granted());
  }
  // Polling cannot take the last slot...
  REQUIRE(f.status(1).code == Outcome::BUSY);
  // ...which stays available for lifecycle handling.
  auto cancelPermit = gate.acquire(ControlKind::CANCEL, f.attachment.sessionId);
  REQUIRE(cancelPermit.granted());
  REQUIRE(gate.inFlight() == 8);
  // An identical lifecycle call in flight coalesces.
  auto dup = gate.acquire(ControlKind::CANCEL, f.attachment.sessionId);
  REQUIRE(dup.granted());
  REQUIRE(dup.coalesced());
  // Beyond the bound: busy.
  REQUIRE(f.engine->renew(f.creds) == Outcome::BUSY);

  held.clear();
  cancelPermit = {};
  dup = {};
  REQUIRE(gate.inFlight() == 0);
  REQUIRE(f.engine->renew(f.creds) == Outcome::OK);

  GIVEN("a smaller configured control bound")
  {
    Limits small;
    small.controlSlots = 2;
    Fixture g(small);
    auto a = g.engine->controlGate().acquire(ControlKind::LOOKUP, "x");
    REQUIRE(a.granted());
    REQUIRE(g.status(1).code == Outcome::BUSY);
    REQUIRE(g.engine->renew(g.creds) == Outcome::OK);
  }

  GIVEN("invalid limits")
  {
    Limits zero;
    zero.sceneSlots = 0;
    REQUIRE_FALSE(OperationEngine::create(f.integration, *f.authority, zero));
  }
}

SCENARIO("Default pool admits 32 scene operations", "[integration]")
{
  Fixture f;
  for (std::uint64_t id = 1; id <= 32; ++id)
    REQUIRE(f.submit(id).code == Outcome::OK);
  REQUIRE(f.submit(33).code == Outcome::BUSY);
  REQUIRE(f.engine->submitLocal(f.request(f.newOp())).code == Outcome::BUSY);
  REQUIRE(f.engine->slotsInUse() == 32);
  f.integration.service();
  REQUIRE(f.engine->slotsInUse() == 0);
  REQUIRE(f.submit(33).code == Outcome::OK);
}

SCENARIO("Progress states and slot retention across slices", "[integration]")
{
  Fixture f;
  auto &op = f.newOp();
  op.script = [](StepContext &, int run) {
    Step s;
    if (run == 1)
      s.status = Step::Status::YIELD;
    else if (run == 2) {
      s.status = Step::Status::SYNCHRONIZING;
      s.committed = true;
    } else
      s.result = "committed";
    return s;
  };
  REQUIRE(
      f.engine->submit(f.creds, f.ref(1), f.request(op)).code == Outcome::OK);
  REQUIRE(f.status(1).progress == Progress::QUEUED);
  f.integration.service();
  REQUIRE(f.status(1).progress == Progress::YIELDED);
  REQUIRE(f.engine->slotsInUse() == 1);
  f.integration.service();
  REQUIRE(f.status(1).progress == Progress::SYNCHRONIZING);
  // Cancelling committed work does not undo it.
  REQUIRE(f.engine->cancel(f.creds, f.ref(1)).committed);
  f.integration.service();
  auto s = f.status(1);
  REQUIRE_FALSE(s.progress);
  REQUIRE(s.terminal == Outcome::OK);
  REQUIRE(s.committed);
  REQUIRE(f.engine->slotsInUse() == 0);

  GIVEN("an executor observing progress while running")
  {
    auto &probe = f.newOp();
    Progress seen = Progress::QUEUED;
    probe.script = [&](StepContext &, int) {
      seen = *f.engine->statusLocal({1}).progress;
      return Step{};
    };
    auto la = f.engine->submitLocal(f.request(probe));
    REQUIRE(la.ref.id == 1);
    f.integration.service();
    REQUIRE(seen == Progress::RUNNING);
  }
}

SCENARIO("Deadlines start at admission and include queue wait", "[integration]")
{
  Fixture f;
  auto &op = f.newOp();
  REQUIRE(f.engine->submit(f.creds, f.ref(1), f.request(op, "{}", 5.0)).code
      == Outcome::OK);
  f.now += 4.0;
  REQUIRE(f.status(1).progress == Progress::QUEUED);
  f.now += 1.0;
  auto s = f.status(1);
  REQUIRE(s.terminal == Outcome::TIMEOUT);
  REQUIRE(f.engine->slotsInUse() == 0);
  f.integration.service();
  REQUIRE(op.runs == 0); // expired queued work never starts

  GIVEN("expiry noticed only at the safe point")
  {
    auto &late = f.newOp();
    REQUIRE(f.engine->submit(f.creds, f.ref(2), f.request(late, "{}", 1.0)).code
        == Outcome::OK);
    f.now += 2.0;
    f.integration.service();
    REQUIRE(late.runs == 0);
    REQUIRE(f.status(2).terminal == Outcome::TIMEOUT);
  }
}

SCENARIO("Cancelled or timed-out work stays terminal despite late results",
    "[integration]")
{
  Fixture f;
  auto &op = f.newOp();
  std::function<void()> wake;
  bool backendDone = false;
  op.script = [&](StepContext &ctx, int) {
    wake = ctx.wake;
    Step s;
    if (!backendDone) {
      s.status = Step::Status::BACKEND_PENDING;
      return s;
    }
    s.result = "late image";
    return s;
  };
  REQUIRE(f.engine->submit(f.creds, f.ref(1), f.request(op, "{}", 30.0)).code
      == Outcome::OK);
  f.integration.service();
  REQUIRE(f.status(1).progress == Progress::RUNNING);

  f.now += 30.0;
  auto s = f.status(1);
  REQUIRE(s.terminal == Outcome::TIMEOUT);
  REQUIRE(s.progress == Progress::AWAITING_CLEANUP);
  REQUIRE(s.heldSlots == 1);
  REQUIRE(f.engine->slotsInUse() == 1);

  backendDone = true;
  wake();
  f.integration.service();
  s = f.status(1);
  REQUIRE(s.terminal == Outcome::TIMEOUT);
  REQUIRE(s.result.empty());
  REQUIRE_FALSE(s.progress);
  REQUIRE(f.engine->slotsInUse() == 0);

  GIVEN("completion before cancel")
  {
    REQUIRE(f.submit(2).code == Outcome::OK);
    f.integration.service();
    auto c = f.engine->cancel(f.creds, f.ref(2));
    REQUIRE(c.terminal == Outcome::OK);
  }
  GIVEN("cancel of a reference not yet admitted")
  {
    REQUIRE(f.engine->cancel(f.creds, f.ref(50)).code == Outcome::UNKNOWN);
    REQUIRE(f.engine->cancel(f.creds, f.ref(1)).terminal == Outcome::TIMEOUT);
  }
  GIVEN("cancel of queued work")
  {
    auto &q = f.newOp();
    REQUIRE(
        f.engine->submit(f.creds, f.ref(3), f.request(q)).code == Outcome::OK);
    REQUIRE(f.engine->cancel(f.creds, f.ref(3)).terminal == Outcome::CANCELLED);
    f.integration.service();
    REQUIRE(q.runs == 0);
  }
}

SCENARIO("A never-releasing backend holds its slot in awaiting_cleanup",
    "[integration]")
{
  Limits limits;
  limits.sceneSlots = 1;
  Fixture f(limits);
  auto &op = f.newOp();
  op.script = [](StepContext &, int) {
    Step s;
    s.status = Step::Status::BACKEND_PENDING;
    return s;
  };
  REQUIRE(
      f.engine->submit(f.creds, f.ref(1), f.request(op)).code == Outcome::OK);
  f.integration.service();
  REQUIRE(f.engine->cancel(f.creds, f.ref(1)).terminal == Outcome::CANCELLED);

  for (int i = 0; i < 120; ++i) { // an hour, renewing every 30 s
    f.now += 30.0;
    REQUIRE(f.engine->renew(f.creds) == Outcome::OK);
  }
  auto s = f.status(1);
  REQUIRE(s.progress == Progress::AWAITING_CLEANUP);
  REQUIRE(s.cleanupElapsedSeconds == Approx(3600.0));
  REQUIRE(s.heldSlots == 1);
  REQUIRE_FALSE(s.backendFault); // elapsed time is not a fault
  REQUIRE(f.engine->awaitingCleanup() == 1);

  // No extra pool, no forced release: new scene work is busy...
  REQUIRE(f.submit(2).code == Outcome::BUSY);
  REQUIRE(f.engine->submitLocal(f.request(f.newOp())).code == Outcome::BUSY);
  // ...while control stays serviceable.
  REQUIRE(f.engine->renew(f.creds) == Outcome::OK);
  // Revocation does not reclaim the slot.
  REQUIRE(f.authority->revokeSession());
  REQUIRE(f.engine->slotsInUse() == 1);
  f.integration.service();
  REQUIRE(f.engine->slotsInUse() == 1);
}

SCENARIO("A backend-reported fault is shown distinctly", "[integration]")
{
  Fixture f;
  auto &op = f.newOp();
  op.script = [](StepContext &, int) {
    Step s;
    s.status = Step::Status::BACKEND_PENDING;
    s.backendFault = true;
    return s;
  };
  REQUIRE(
      f.engine->submit(f.creds, f.ref(1), f.request(op)).code == Outcome::OK);
  f.integration.service();
  REQUIRE(f.status(1).backendFault);
}

SCENARIO("Session end cancels outstanding work and blocks in-transit IDs",
    "[integration]")
{
  Fixture f;
  auto &queued = f.newOp();
  REQUIRE(f.engine->submit(f.creds, f.ref(1), f.request(queued)).code
      == Outcome::OK);
  const auto oldCreds = f.creds;
  const auto oldSession = f.attachment.sessionId;

  REQUIRE(f.authority->revokeSession());
  f.integration.service();
  REQUIRE(queued.runs == 0);
  REQUIRE(f.engine->slotsInUse() == 0);

  // A delayed request under the revoked session never admits.
  auto &delayed = f.newOp();
  REQUIRE(f.engine->submit(oldCreds, {oldSession, 2}, f.request(delayed)).code
      == Outcome::INVALID_SESSION_PROOF);

  // A new session has a new namespace and cannot reinterpret old numbers.
  f.attach();
  REQUIRE(f.attachment.sessionId != oldSession);
  REQUIRE(f.engine->highWater(f.attachment.sessionId) == 0);
  REQUIRE(f.status(1).code == Outcome::UNKNOWN);
  REQUIRE(
      f.engine->cancel(f.creds, {oldSession, 1}).code == Outcome::FORBIDDEN);
  REQUIRE(f.submit(1).code == Outcome::OK);
  f.integration.service();
  REQUIRE(f.status(1).terminal == Outcome::OK);

  GIVEN("detach through the control path")
  {
    auto &pending = f.newOp();
    REQUIRE(f.engine->submit(f.creds, f.ref(2), f.request(pending)).code
        == Outcome::OK);
    REQUIRE(f.engine->detach(f.creds) == Outcome::OK);
    f.integration.service();
    REQUIRE(pending.runs == 0);
    REQUIRE(f.engine->slotsInUse() == 0);
  }
}

SCENARIO("Withdrawn permission cancels queued work at the safe point",
    "[integration]")
{
  Fixture f;
  auto &op = f.newOp();
  REQUIRE(f.engine
              ->submit(
                  f.creds, f.ref(1), f.request(op, "{}", 0.0, Capability::EDIT))
              .code
      == Outcome::OK);
  f.authority->setAgentEditsDisabled(true);
  f.integration.service();
  REQUIRE(op.runs == 0);
  auto s = f.status(1);
  REQUIRE(s.terminal == Outcome::FORBIDDEN);
  REQUIRE(f.engine->slotsInUse() == 0);
}

SCENARIO("Lifecycle calls keep the reserved control slot (addendum 03 §2)",
    "[integration]")
{
  Fixture f;
  REQUIRE(f.submit(1).code == Outcome::OK);
  auto &gate = f.engine->controlGate();
  std::vector<ControlGate::Permit> held;
  for (int i = 0; i < 7; ++i) {
    held.push_back(gate.acquire(ControlKind::LOOKUP, "poll"));
    REQUIRE(held.back().granted());
  }
  // A further lookup never takes the last slot...
  REQUIRE_FALSE(gate.acquire(ControlKind::LOOKUP, "poll").granted());
  REQUIRE(f.status(1).code == Outcome::BUSY);
  // ...while each lifecycle call still gets through.
  REQUIRE(f.engine->renew(f.creds) == Outcome::OK);
  REQUIRE(f.engine->cancel(f.creds, f.ref(1)).code == Outcome::OK);

  // Different lifecycle calls do not share a slot; the excess is busy.
  auto renewing = gate.acquire(ControlKind::RENEW, f.attachment.sessionId);
  REQUIRE(renewing.granted());
  REQUIRE_FALSE(renewing.coalesced());
  REQUIRE(f.engine->cancel(f.creds, f.ref(1)).code == Outcome::BUSY);
  REQUIRE(f.engine->detach(f.creds) == Outcome::BUSY);
  // The identical one shares the in-flight slot.
  REQUIRE(f.engine->renew(f.creds) == Outcome::OK);
  REQUIRE(gate.inFlight() == 8);
}

SCENARIO("Withdrawn permission ends forbidden; session end ends cancelled",
    "[integration]")
{
  Fixture f;
  auto &edit = f.newOp();
  REQUIRE(
      f.engine
          ->submit(
              f.creds, f.ref(1), f.request(edit, "{}", 0.0, Capability::EDIT))
          .code
      == Outcome::OK);
  auto &read = f.newOp();
  REQUIRE(
      f.engine->submit(f.creds, f.ref(2), f.request(read)).code == Outcome::OK);
  f.authority->setAgentEditsDisabled(true);
  f.integration.service();
  REQUIRE(edit.runs == 0);
  auto forbidden = f.status(1);
  REQUIRE(forbidden.terminal == Outcome::FORBIDDEN);
  REQUIRE_FALSE(forbidden.committed);
  // Work the withdrawal does not touch still runs.
  REQUIRE(read.runs == 1);
  REQUIRE(f.status(2).terminal == Outcome::OK);

  auto &queued = f.newOp();
  REQUIRE(f.engine->submit(f.creds, f.ref(3), f.request(queued)).code
      == Outcome::OK);
  const auto oldSession = f.attachment.sessionId;
  REQUIRE(f.engine->detach(f.creds) == Outcome::OK);
  f.integration.service();
  REQUIRE(queued.runs == 0);
  f.attach();
  auto cancelled = f.engine->status(f.creds, {oldSession, 3});
  REQUIRE(cancelled.terminal == Outcome::CANCELLED);
  REQUIRE_FALSE(cancelled.committed);
}

SCENARIO("invalid_request rejects before admission and consumes no ID",
    "[integration]")
{
  Fixture f;
  auto noExecutor = f.request(f.newOp());
  noExecutor.executor = {};
  REQUIRE(f.engine->submit(f.creds, f.ref(1), std::move(noExecutor)).code
      == Outcome::INVALID_REQUEST);
  REQUIRE(f.submit(0).code == Outcome::INVALID_REQUEST);
  REQUIRE(f.engine
              ->submit(f.creds,
                  {"0123456789abcdef0123456789abcdef", 1},
                  f.request(f.newOp()))
              .code
      == Outcome::INVALID_REQUEST);
  REQUIRE(f.engine->highWater(f.attachment.sessionId) == 0);
  REQUIRE(f.status(1).code == Outcome::UNKNOWN);
  // The same ID is still free for a valid request.
  REQUIRE(f.submit(1).code == Outcome::OK);
  f.integration.service();
  REQUIRE(f.status(1).terminal == Outcome::OK);
}

SCENARIO("A step that cannot be queued finishes and releases its slot",
    "[integration]")
{
  vsr::scene::Scene scene;
  Integration integration(1); // one queued item at most
  SceneRegistration s;
  s.name = "scene";
  s.scene = &scene;
  s.grants.read = true;
  auto sceneId = *integration.registerScene(s);
  REQUIRE(integration.activate().ok());
  double now = 10.0;
  auto authority =
      session::SessionAuthority::create(integration, [&] { return now; });
  auto engine =
      OperationEngine::create(integration, *authority, {}, [&] { return now; });
  REQUIRE(engine);
  auto att = authority->attach(authority->launchCredential());
  Credentials creds{
      authority->launchCredential(), att.sessionId, att.sessionSecret};

  activity::ActivityLog log;
  activity::EngineActivitySink sink(log, integration);
  engine->setActivitySink(&sink);

  // Fill the Integration queue behind the engine's back.
  REQUIRE(integration.submit(sceneId, [](vsr::scene::Scene &) {}));

  FakeOp op;
  Request r;
  r.kind = "inspection";
  r.target = session::Target::scene(sceneId);
  r.executor = op.executor();
  auto a = engine->submit(creds, {att.sessionId, 1}, std::move(r));

  THEN("the operation is terminal, holds no slot and its activity is final")
  {
    REQUIRE(a.code == Outcome::OK);
    REQUIRE(a.status.terminal == Outcome::BUSY);
    REQUIRE(a.status.heldSlots == 0);
    REQUIRE(engine->slotsInUse() == 0);
    auto st = engine->status(creds, {att.sessionId, 1});
    REQUIRE(st.terminal == Outcome::BUSY);
    REQUIRE(log.usage().entries == 1);
    const auto records = log.snapshot();
    REQUIRE(records.size() == 1);
    REQUIRE(records[0].entry.terminal == Outcome::BUSY);
    REQUIRE_FALSE(records[0].entry.active);
    integration.service();
    REQUIRE(op.runs == 0);
  }

  GIVEN("a local submit")
  {
    FakeOp local;
    Request lr;
    lr.kind = "inspection";
    lr.target = session::Target::scene(sceneId);
    lr.executor = local.executor();
    auto la = engine->submitLocal(std::move(lr));
    REQUIRE(la.code == Outcome::OK);
    REQUIRE(engine->statusLocal(la.ref).terminal == Outcome::BUSY);
    REQUIRE(engine->slotsInUse() == 0);
  }

  GIVEN("a yielding step whose re-dispatch fails")
  {
    integration.service(); // drain the filler
    FakeOp y;
    y.script = [&](StepContext &, int run) {
      if (run == 1) // occupy the only queue entry before yielding
        REQUIRE(integration.submit(sceneId, [](vsr::scene::Scene &) {}));
      Step st;
      st.status = run == 1 ? Step::Status::YIELD : Step::Status::COMPLETE;
      return st;
    };
    Request yr;
    yr.kind = "inspection";
    yr.target = session::Target::scene(sceneId);
    yr.executor = y.executor();
    REQUIRE(engine->submit(creds, {att.sessionId, 2}, std::move(yr)).code
        == Outcome::OK);
    integration.service();
    REQUIRE(y.runs == 1);
    REQUIRE(engine->status(creds, {att.sessionId, 2}).terminal
        == Outcome::CANCELLED);
    REQUIRE(engine->slotsInUse() == 0);
  }
  engine->setActivitySink(nullptr);
}

SCENARIO("Session-wide work runs with zero registered scenes", "[integration]")
{
  Integration integration;
  REQUIRE(integration.activate().ok());
  double now = 10.0;
  auto authority =
      session::SessionAuthority::create(integration, [&] { return now; });
  auto engine =
      OperationEngine::create(integration, *authority, {}, [&] { return now; });
  REQUIRE(engine);
  auto att = authority->attach(authority->launchCredential());
  REQUIRE(att.code == Code::OK);
  Credentials creds{
      authority->launchCredential(), att.sessionId, att.sessionSecret};

  inspection::Inspector inspector(
      integration, *authority, [](SceneId) { return nullptr; });
  auto b = inspector.makeDiscovery(att.sessionId, {});
  REQUIRE(b.code == Outcome::OK);
  auto a = engine->submit(creds, {att.sessionId, 1}, std::move(b.request));
  REQUIRE(a.code == Outcome::OK);
  while (integration.service() > 0) {
  }
  auto s = engine->status(creds, {att.sessionId, 1});
  REQUIRE(s.code == Outcome::OK);
  REQUIRE(s.terminal == Outcome::OK);
  REQUIRE(s.result.find("\"returned\":0") != std::string::npos);
  REQUIRE(s.result.find("\"complete\":true") != std::string::npos);
  REQUIRE(engine->slotsInUse() == 0);

  // Non-session-wide work still needs a registered target.
  FakeOp op;
  Request r;
  r.kind = "test";
  r.target = session::Target::scene(0);
  r.executor = op.executor();
  REQUIRE(engine->submitLocal(std::move(r)).code == Outcome::INVALID_REQUEST);
}

SCENARIO("Concurrent RENDER-kind submits with a sink do not deadlock service()",
    "[integration]")
{
  Fixture f;
  activity::ActivityLog log;
  activity::EngineActivitySink sink(log, f.integration);
  f.engine->setActivitySink(&sink);

  std::atomic<bool> stop{false};
  std::atomic<int> completed{0};
  std::atomic<int> rejected{0};
  std::atomic<bool> hostDone{false};
  std::thread host([&] {
    while (!stop)
      f.integration.service();
    while (f.integration.service() > 0) {
    }
    hostDone = true;
  });

  const int threads = 4;
  const int perThread = 50;
  using clock = std::chrono::steady_clock;
  const auto deadline = clock::now() + std::chrono::seconds(30);
  std::atomic<int> timedOut{0};
  std::vector<std::thread> submitters;
  std::unique_ptr<std::atomic<bool>[]> submitterDone(
      new std::atomic<bool>[threads]);
  for (int t = 0; t < threads; t++)
    submitterDone[t] = false;
  for (int t = 0; t < threads; t++) {
    submitters.emplace_back([&, t] {
      struct MarkDone
      {
        std::atomic<bool> &flag;
        ~MarkDone()
        {
          flag = true;
        }
      } markDone{submitterDone[t]};
      for (int i = 0; i < perThread; i++) {
        Request r;
        r.kind = "render";
        r.target = session::Target::view(f.viewId);
        r.capability = Capability::RENDER;
        r.executor = [&](StepContext &ctx) {
          // Engine callbacks and Integration queries from inside the step.
          ctx.restorationRetained("l", "b");
          f.integration.sceneOfView(f.viewId);
          Step st;
          st.status = Step::Status::COMPLETE;
          completed++;
          return st;
        };
        LocalAdmission a;
        do {
          a = f.engine->submitLocal(r);
          if (a.code == Outcome::BUSY)
            std::this_thread::yield();
        } while (a.code == Outcome::BUSY && clock::now() < deadline);
        if (a.code == Outcome::BUSY) {
          timedOut++; // retry deadline passed; give up this thread
          return;
        }
        if (a.code != Outcome::OK)
          rejected++; // Catch assertions are not thread-safe
        else // Status and Integration queries under the engine lock.
          f.engine->statusLocal(a.ref);
        (void)t;
      }
    });
  }
  // A thread stuck past the deadline means a real deadlock: report it and
  // abort rather than hang (a Catch FAIL would unwind past joinable threads).
  const auto joinWithin = [&](std::thread &th,
                              const std::atomic<bool> &done,
                              const char *what,
                              clock::time_point limit) {
    while (!done && clock::now() < limit)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (!done) {
      std::fprintf(stderr,
          "deadlock scenario: %s did not finish before its deadline "
          "(completed steps: %d); aborting\n",
          what,
          completed.load());
      std::fflush(stderr);
      std::abort();
    }
    th.join();
  };
  const auto joinDeadline = deadline + std::chrono::seconds(10);
  for (int t = 0; t < threads; t++)
    joinWithin(
        submitters[t], submitterDone[t], "a submitter thread", joinDeadline);
  // Every admitted step either ran or was finished at dispatch failure.
  bool drained = false;
  while (!(drained = f.engine->slotsInUse() == 0) && clock::now() < deadline)
    std::this_thread::yield();
  stop = true;
  joinWithin(host,
      hostDone,
      "the host service() thread",
      clock::now() + std::chrono::seconds(10));
  INFO("slots still in use at deadline: " << f.engine->slotsInUse());
  REQUIRE(drained);
  INFO("submitters that hit the BUSY retry deadline: " << timedOut.load());
  REQUIRE(timedOut == 0);
  REQUIRE(rejected == 0);
  REQUIRE(completed <= threads * perThread);
  REQUIRE(completed > 0);
  REQUIRE(f.engine->slotsInUse() == 0);
  f.engine->setActivitySink(nullptr);
}

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/activity/Activity.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <functional>
#include <memory>
#include <string>

using namespace vsr::integration;
using namespace vsr::integration::activity;
using session::Attachment;
using session::Code;

namespace {

bool confirmReady(SceneId)
{
  return true;
}

struct Fixture
{
  vsr::scene::Scene scene;
  Integration integration;
  SceneId sceneId{INVALID_SCENE_ID};
  ViewId viewId{INVALID_VIEW_ID};
  double now{1000.0};
  std::unique_ptr<session::SessionAuthority> authority;
  Credentials creds;

  Fixture()
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
    attach();
  }

  void attach()
  {
    Attachment a = authority->attach(authority->launchCredential());
    REQUIRE(a.code == Code::OK);
    creds = {authority->launchCredential(), a.sessionId, a.sessionSecret};
  }

  void reattach()
  {
    REQUIRE(authority->detach(creds) == Code::OK);
    attach();
  }

  Entry edit(std::uint64_t id, std::string batch, std::size_t payload = 64)
  {
    Entry e;
    e.ref = OperationRef{creds.sessionId, id};
    e.kind = OperationKind::EDIT;
    e.targets = coveredTargets(e.kind, Target::scene(sceneId), integration);
    e.terminal = Outcome::OK;
    e.committed = true;
    e.batchRef = BatchRef{authority->launchId(), batch};
    e.result = "{\"material\":\"red\"}";
    e.restorationBytes = payload;
    e.eligibility = Eligibility::ELIGIBLE;
    return e;
  }

  Entry render(std::uint64_t id, std::size_t imageBytes)
  {
    Entry e;
    e.ref = OperationRef{creds.sessionId, id};
    e.kind = OperationKind::RENDER;
    e.targets = coveredTargets(e.kind, Target::view(viewId), integration);
    e.terminal = Outcome::OK;
    e.image.assign(imageBytes, 0x7f);
    return e;
  }
};

std::string hex(std::size_t i)
{
  std::string s(32, '0');
  auto t = std::to_string(i);
  s.replace(32 - t.size(), t.size(), t);
  return s;
}

} // namespace

SCENARIO("Default activity budgets match the spec", "[integration]")
{
  Limits l;
  REQUIRE(l.bytes == 16u * 1024 * 1024);
  REQUIRE(l.retentionBytes == UINT64_C(4294967296));
  REQUIRE(l.entries == 1024);
  REQUIRE(l.restorableBatches == 32);
  REQUIRE(l.imageBytes == 32u * 1024 * 1024);

  Fixture f;
  ActivityLog log;
  for (std::size_t i = 1; i <= 1100; ++i)
    REQUIRE(log.append(f.edit(i, hex(i), 8)));
  auto u = log.usage();
  REQUIRE(u.entries == 1024);
  REQUIRE(u.restorableBatches == 32);
  // Oldest records gone, newest retained.
  REQUIRE_FALSE(log.eligibility({f.authority->launchId(), hex(1)}));
  REQUIRE(log.eligibility({f.authority->launchId(), hex(1100)})
      == Eligibility::ELIGIBLE);
  // Batches beyond the newest 32 visibly expired while metadata remains.
  REQUIRE(log.eligibility({f.authority->launchId(), hex(1100 - 32)})
      == Eligibility::EXPIRED);
}

SCENARIO("Smaller limits: payload bytes count and oldest safe evicts first",
    "[integration]")
{
  Fixture f;
  Limits l;
  l.entries = 3;
  l.restorableBatches = 2;
  l.bytes = 4 * (ENTRY_OVERHEAD + 1024);
  ActivityLog log(l);
  const std::string launch = f.authority->launchId();

  auto a = log.append(f.edit(1, hex(1), 400));
  REQUIRE(a);
  REQUIRE(log.usage().retentionBytes == 400);
  REQUIRE(log.append(f.edit(2, hex(2), 400)));
  // A third restorable batch expires the oldest one, releasing its payload.
  const auto before = log.usage().bytes;
  REQUIRE(log.append(f.edit(3, hex(3), 400)));
  REQUIRE(log.eligibility({launch, hex(1)}) == Eligibility::EXPIRED);
  REQUIRE(log.get(*a)->restorationBytes == 0);
  REQUIRE(log.usage().restorableBatches == 2);
  REQUIRE(
      log.usage().bytes == before + ActivityLog::cost(f.edit(3, hex(3), 400)));
  REQUIRE(log.usage().retentionBytes == 800);

  // Metadata pressure discards only non-recovery records.
  Entry big = f.render(4, 0);
  big.result.assign(1500, 'x');
  REQUIRE(log.append(big));
  REQUIRE(log.usage().bytes <= l.bytes);
  REQUIRE_FALSE(log.eligibility({launch, hex(1)}));
  REQUIRE(log.eligibility({launch, hex(2)}) == Eligibility::ELIGIBLE);
}

TEST_CASE("Wide recovery accounting never preallocates the soft target",
    "[integration][retention-accounting]")
{
  if (sizeof(std::size_t) < 8)
    return;
  ActivityLog log;
  const auto first = log.append(Entry{});
  REQUIRE(first);
  REQUIRE(log.reserveRestoration(
      *first, std::size_t(UINT64_C(4294967296)), {"launch", "wide"}));
  CHECK(log.usage().retentionBytes == 0);
  CHECK(log.usage().reservedRetentionBytes == UINT64_C(4294967296));
  log.publishRestoration(*first);
  CHECK(log.usage().retentionBytes == UINT64_C(4294967296));
  const auto next = log.append(Entry{});
  REQUIRE(next);
  REQUIRE(log.reserveRestoration(*next, 1, {"launch", "new"}));
  CHECK(log.eligibility({"launch", "wide"}) == Eligibility::ELIGIBLE);
  log.publishRestoration(*next);
  CHECK(log.eligibility({"launch", "wide"}) == Eligibility::EXPIRED);
  CHECK(log.usage().retentionBytes == 1);
  CHECK(log.usage().reservedRetentionBytes == 0);
  CHECK(log.usage().bytes < 4096); // bookkeeping only, not GiB allocations
}

TEST_CASE("Recovery FIFO follows publication rather than preparation",
    "[integration][retention-accounting]")
{
  Limits limits;
  limits.retentionBytes = 1000;
  ActivityLog log(limits);
  auto early = log.append(Entry{});
  auto late = log.append(Entry{});
  REQUIRE(early);
  REQUIRE(late);
  REQUIRE(log.reserveRestoration(*early, 600, {"launch", "early"}));
  REQUIRE(log.reserveRestoration(*late, 600, {"launch", "late"}));
  CHECK(log.usage().retentionBytes == 0);
  CHECK(log.usage().reservedRetentionBytes == 1200);
  log.publishRestoration(*late);
  CHECK(log.eligibility({"launch", "late"}) == Eligibility::ELIGIBLE);
  REQUIRE(log.get(*late));
  log.publishRestoration(*early);
  CHECK(log.eligibility({"launch", "late"}) == Eligibility::EXPIRED);
  CHECK(log.eligibility({"launch", "early"}) == Eligibility::ELIGIBLE);
  CHECK(log.usage().retentionBytes == 600);
  CHECK(log.usage().reservedRetentionBytes == 0);
  const auto failed = log.append(Entry{});
  REQUIRE(failed);
  REQUIRE(log.reserveRestoration(*failed, 1000, {"launch", "failed"}));
  CHECK(log.eligibility({"launch", "early"}) == Eligibility::ELIGIBLE);
  log.cancelRestoration(*failed);
  CHECK(log.eligibility({"launch", "early"}) == Eligibility::ELIGIBLE);
  CHECK(log.usage().retentionBytes == 600);
  CHECK(log.usage().reservedRetentionBytes == 0);
}

SCENARIO("Active work is never evicted", "[integration]")
{
  Fixture f;
  Limits l;
  l.entries = 2;
  l.restorableBatches = 1;
  ActivityLog log(l);
  Entry active = f.edit(1, hex(1));
  active.active = true;
  active.eligibility = Eligibility::NONE;
  active.restorationBytes = 0;
  active.terminal.reset();
  active.progress = Progress::RUNNING;
  auto a = log.append(active);
  REQUIRE(a);
  // Pending active work is protected independently of the recovery count.
  Entry plain = f.render(3, 0);
  REQUIRE(log.append(plain));
  REQUIRE(log.append(f.render(4, 0))); // evicts 3, not the active one
  REQUIRE(log.get(*a));
  Entry more = f.render(5, 0);
  more.active = true;
  REQUIRE(log.append(more)); // evicts 4
  REQUIRE_FALSE(log.append(f.render(6, 0))); // both remaining are active
  REQUIRE(log.usage().entries == 2);
}

SCENARIO("Retained images use their own budget", "[integration]")
{
  Fixture f;
  Limits l;
  l.imageBytes = 1000;
  ActivityLog log(l);
  auto a = log.append(f.render(1, 600));
  auto bytesBefore = log.usage().bytes;
  REQUIRE(log.usage().imageBytes == 600);
  REQUIRE(bytesBefore == ActivityLog::cost(f.render(1, 0)));
  auto b = log.append(f.render(2, 600));
  REQUIRE(log.usage().imageBytes == 600);
  REQUIRE(log.get(*a)->imageEvicted);
  REQUIRE(log.get(*a)->image.empty());
  REQUIRE(log.get(*b)->image.size() == 600);
}

SCENARIO(
    "Restoration consumes eligibility and releases payload", "[integration]")
{
  Fixture f;
  ActivityLog log;
  auto id = log.append(f.edit(1, hex(1), 1000));
  REQUIRE(log.markRestored({f.authority->launchId(), hex(1)}));
  REQUIRE(log.eligibility({f.authority->launchId(), hex(1)})
      == Eligibility::RESTORED);
  REQUIRE(log.get(*id)->restorationBytes == 0);
  REQUIRE(log.usage().restorableBatches == 0);
  REQUIRE_FALSE(log.markRestored({f.authority->launchId(), hex(1)}));
  REQUIRE_FALSE(log.eligibility({"otherlaunch", hex(1)}));
}

SCENARIO("Owner receipts are minimal; payload needs read", "[integration]")
{
  Fixture f;
  ActivityLog log;
  Entry e = f.edit(1, hex(1));
  e.clientSummary = ClientSummary{"make it red"};
  log.append(e);

  auto full = log.lookup(*f.authority, f.creds, {f.creds.sessionId, 1});
  REQUIRE(full.code == Outcome::OK);
  REQUIRE(full.record);
  REQUIRE(full.record->batchRef);
  REQUIRE(full.record->clientSummary->text == "make it red");
  REQUIRE_FALSE(full.record->kind != OperationKind::EDIT);

  f.authority->restrict(Target::scene(f.sceneId), Capability::READ);
  auto minimal = log.lookup(*f.authority, f.creds, {f.creds.sessionId, 1});
  REQUIRE(minimal.code == Outcome::OK);
  REQUIRE_FALSE(minimal.record);
  REQUIRE(minimal.receipt);
  REQUIRE(minimal.receipt->ref.requestId == 1);
  REQUIRE(minimal.receipt->kind == OperationKind::EDIT);
  REQUIRE(minimal.receipt->committed);
  REQUIRE(minimal.receipt->terminal == Outcome::OK);

  // Wrong proof: no receipt at all.
  auto bad = f.creds;
  bad.sessionProof = "x";
  REQUIRE(log.lookup(*f.authority, bad, {f.creds.sessionId, 1}).code
      == Outcome::INVALID_SESSION_PROOF);
}

SCENARIO("New session looks up an older session's record", "[integration]")
{
  Fixture f;
  ActivityLog log;
  const std::string oldSession = f.creds.sessionId;
  log.append(f.edit(1, hex(1)));
  log.append(f.render(2, 100));

  // Records survive client disconnect.
  f.reattach();
  REQUIRE(f.creds.sessionId != oldSession);

  auto e = log.lookup(*f.authority, f.creds, {oldSession, 1});
  REQUIRE(e.code == Outcome::OK);
  REQUIRE(e.record);
  REQUIRE_FALSE(e.receipt); // not the caller's own work
  REQUIRE(e.record->kind == OperationKind::EDIT);
  REQUIRE(e.record->ref->sessionId == oldSession);

  // Render covers view + scene: read on the view alone is denied whole.
  REQUIRE(log.lookup(*f.authority, f.creds, {oldSession, 2}).record);
  f.authority->restrict(Target::scene(f.sceneId), Capability::READ);
  auto partial = log.lookup(*f.authority, f.creds, {oldSession, 2});
  REQUIRE(partial.code == Outcome::FORBIDDEN);
  REQUIRE_FALSE(partial.record);
  REQUIRE_FALSE(partial.receipt);
  // Own-work minimal receipts do not extend to foreign sessions.
  REQUIRE(log.lookup(*f.authority, f.creds, {oldSession, 1}).code
      == Outcome::FORBIDDEN);

  // Missing record: outcome_unavailable.
  REQUIRE(log.lookup(*f.authority, f.creds, {oldSession, 99}).code
      == Outcome::OUTCOME_UNAVAILABLE);
}

SCENARIO("Evicted records report outcome_unavailable", "[integration]")
{
  Fixture f;
  Limits l;
  l.entries = 1;
  ActivityLog log(l);
  log.append(f.render(1, 0));
  log.append(f.render(2, 0));
  REQUIRE(log.lookup(*f.authority, f.creds, {f.creds.sessionId, 1}).code
      == Outcome::OUTCOME_UNAVAILABLE);
  REQUIRE(log.lookup(*f.authority, f.creds, {f.creds.sessionId, 2}).code
      == Outcome::OK);
}

SCENARIO("Records do not survive restart", "[integration]")
{
  Fixture f;
  {
    ActivityLog log;
    log.append(f.render(1, 0));
  }
  ActivityLog restarted;
  REQUIRE(restarted.lookup(*f.authority, f.creds, {f.creds.sessionId, 1}).code
      == Outcome::OUTCOME_UNAVAILABLE);
}

SCENARIO("Covered targets are fixed per operation kind", "[integration]")
{
  Fixture f;
  auto s = Target::scene(f.sceneId);
  for (auto k : {OperationKind::INSPECTION,
           OperationKind::EDIT,
           OperationKind::RESTORE,
           OperationKind::RESYNCHRONIZE})
    REQUIRE(coveredTargets(k, s, f.integration).size() == 1);
  auto r = coveredTargets(
      OperationKind::RENDER, Target::view(f.viewId), f.integration);
  REQUIRE(r.size() == 2);
  REQUIRE(r[1].kind == Target::Kind::SCENE);
  auto d = coveredTargets(
      OperationKind::DISCOVERY, s, f.integration, {s, Target::view(f.viewId)});
  REQUIRE(d.size() == 2);
}

SCENARIO("Operation status: cross-session lookup needs read on the record",
    "[integration]")
{
  Fixture f;
  auto engine = operations::OperationEngine::create(
      f.integration, *f.authority, {}, [&f] { return f.now; });
  REQUIRE(engine);
  auto req = [&](Target t, Capability c) {
    operations::Request r;
    r.kind = "test";
    r.target = t;
    r.capability = c;
    r.canonicalArguments = "{}";
    r.executor = [](operations::StepContext &) {
      operations::Step s;
      s.result = "payload";
      return s;
    };
    return r;
  };
  const std::string old = f.creds.sessionId;
  REQUIRE(engine
              ->submit(f.creds,
                  {old, 1},
                  req(Target::scene(f.sceneId), Capability::READ))
              .code
      == Outcome::OK);
  REQUIRE(engine
              ->submit(f.creds,
                  {old, 2},
                  req(Target::view(f.viewId), Capability::RENDER))
              .code
      == Outcome::OK);
  f.integration.service();
  f.reattach();

  auto s = engine->status(f.creds, {old, 1});
  REQUIRE(s.code == Outcome::OK);
  REQUIRE(s.kind == "test");
  REQUIRE(s.result == "payload");
  REQUIRE(engine->status(f.creds, {old, 2}).result == "payload");

  f.authority->restrict(Target::scene(f.sceneId), Capability::READ);
  REQUIRE(engine->status(f.creds, {old, 1}).code == Outcome::FORBIDDEN);
  // Render record covers view and scene: partial read is denied whole.
  auto p = engine->status(f.creds, {old, 2});
  REQUIRE(p.code == Outcome::FORBIDDEN);
  REQUIRE(p.result.empty());
  REQUIRE(
      engine->status(f.creds, {old, 9}).code == Outcome::OUTCOME_UNAVAILABLE);
}

namespace {

struct EngineFixture : Fixture
{
  std::unique_ptr<operations::OperationEngine> engine;
  std::unique_ptr<ActivityLog> log;
  std::unique_ptr<EngineActivitySink> sink;
  std::vector<std::function<void()>> wakes;

  explicit EngineFixture(Limits limits)
  {
    engine = operations::OperationEngine::create(
        integration, *authority, {}, [this] { return now; });
    REQUIRE(engine);
    log = std::make_unique<ActivityLog>(limits);
    sink = std::make_unique<EngineActivitySink>(*log, integration);
    engine->setActivitySink(sink.get());
  }

  ~EngineFixture()
  {
    engine->setActivitySink(nullptr);
  }

  operations::Request request(std::function<operations::Step(int)> script)
  {
    auto runs = std::make_shared<int>(0);
    operations::Request r;
    r.kind = "inspection";
    r.target = Target::scene(sceneId);
    r.capability = Capability::READ;
    r.canonicalArguments = "{}";
    r.executor = [script, runs](
                     operations::StepContext &) { return script(++*runs); };
    return r;
  }

  // Holds backend resources until abandoned and woken.
  operations::Request pending()
  {
    operations::Request r = request(nullptr);
    r.executor = [this](operations::StepContext &ctx) {
      operations::Step s;
      if (!ctx.abandoned) {
        s.status = operations::Step::Status::BACKEND_PENDING;
        wakes.push_back(ctx.wake);
      }
      return s;
    };
    return r;
  }
};

} // namespace

SCENARIO(
    "Engine admission rejects busy when the activity log is full of "
    "active work",
    "[integration]")
{
  Limits l;
  l.entries = 2;
  EngineFixture f(l);
  REQUIRE(f.engine->submit(f.creds, {f.creds.sessionId, 1}, f.pending()).code
      == Outcome::OK);
  REQUIRE(f.engine->submit(f.creds, {f.creds.sessionId, 2}, f.pending()).code
      == Outcome::OK);
  f.integration.service();
  REQUIRE(f.log->usage().entries == 2);

  // Both entries are active: nothing may be evicted, nothing admitted.
  REQUIRE(f.engine->submit(f.creds, {f.creds.sessionId, 3}, f.pending()).code
      == Outcome::BUSY);
  REQUIRE(f.engine->submitLocal(f.pending()).code == Outcome::BUSY);
  REQUIRE(f.engine->slotsInUse() == 2);
  REQUIRE(f.engine->status(f.creds, {f.creds.sessionId, 3}).code
      == Outcome::UNKNOWN);

  // Cancelling backend-pending work keeps it active (awaiting_cleanup).
  f.engine->cancel(f.creds, {f.creds.sessionId, 1});
  REQUIRE(f.log->get(1)->active);
  REQUIRE(f.log->get(1)->progress == Progress::AWAITING_CLEANUP);
  REQUIRE(f.engine->submitLocal(f.pending()).code == Outcome::BUSY);

  // Once cleaned up, the oldest terminal entry is evicted first.
  f.engine->cancel(f.creds, {f.creds.sessionId, 2});
  for (auto &w : f.wakes)
    w();
  f.integration.service(); // abandoned steps release resources
  REQUIRE(f.engine->slotsInUse() == 0);
  REQUIRE_FALSE(f.log->get(1)->active);
  REQUIRE_FALSE(f.log->get(2)->active);
  auto local =
      f.engine->submitLocal(f.request([](int) { return operations::Step{}; }));
  REQUIRE(local.code == Outcome::OK);
  REQUIRE_FALSE(f.log->get(1));
  REQUIRE(f.log->get(2));
  auto e3 = f.log->get(3);
  REQUIRE(e3);
  REQUIRE(e3->localRef);
  REQUIRE(e3->localRef->id == local.ref.id);
  REQUIRE_FALSE(e3->ref);
}

SCENARIO("Engine activity entries mirror operation status", "[integration]")
{
  EngineFixture f(Limits{});
  const OperationRef ref{f.creds.sessionId, 7};
  auto a = f.engine->submit(f.creds, ref, f.request([](int run) {
    operations::Step s;
    if (run == 1) {
      s.status = operations::Step::Status::YIELD;
    } else {
      s.committed = true;
      s.result = "{\"answer\":42}";
    }
    return s;
  }));
  REQUIRE(a.code == Outcome::OK);
  f.now += 1.0;

  auto check = [&] {
    auto s = f.engine->status(f.creds, ref);
    auto e = f.log->get(1);
    REQUIRE(e);
    REQUIRE(e->ref);
    REQUIRE(e->ref->requestId == 7);
    REQUIRE(e->kind == OperationKind::INSPECTION);
    REQUIRE(e->targets.size() == 1);
    REQUIRE(e->progress == s.progress);
    REQUIRE(e->terminal == s.terminal);
    REQUIRE(e->committed == s.committed);
    REQUIRE(e->heldSlots == s.heldSlots);
    REQUIRE(e->admittedAt == 1000.0);
    REQUIRE_FALSE(e->clientSummary);
    return *e;
  };
  REQUIRE(check().progress == Progress::QUEUED);
  REQUIRE(check().active);

  f.integration.service();
  REQUIRE(check().progress == Progress::YIELDED);
  f.integration.service();
  auto done = check();
  REQUIRE(done.terminal == Outcome::OK);
  REQUIRE(done.committed);
  REQUIRE(done.result == "{\"answer\":42}");
  REQUIRE(done.finishedAt == 1001.0);
  REQUIRE_FALSE(done.active);

  auto lookup = f.log->lookup(*f.authority, f.creds, ref);
  REQUIRE(lookup.code == Outcome::OK);
  REQUIRE(lookup.receipt);
  REQUIRE(lookup.receipt->terminal == Outcome::OK);
}

SCENARIO("Snapshot enumerates retained entries oldest first", "[integration]")
{
  Fixture f;
  Limits l;
  l.entries = 4;
  ActivityLog log(l);
  REQUIRE(log.snapshot().empty());

  std::vector<ActivityLog::Id> ids;
  for (std::size_t i = 1; i <= 10; ++i)
    ids.push_back(*log.append(f.render(i, 0)));

  auto records = log.snapshot();
  REQUIRE(records.size() == 4);
  for (std::size_t i = 0; i < records.size(); ++i) {
    REQUIRE(records[i].id == ids[6 + i]);
    REQUIRE(records[i].entry.ref->requestId == 7 + i);
  }

  // Updates are reflected in place without reordering.
  Entry e = records[1].entry;
  e.result = "updated";
  REQUIRE(log.update(records[1].id, e));
  records = log.snapshot();
  REQUIRE(records[1].id == ids[7]);
  REQUIRE(records[1].entry.result == "updated");
}

SCENARIO("Discovery entries cover exactly the targets their page lists",
    "[integration]")
{
  EngineFixture f(Limits{});
  const OperationRef ref{f.creds.sessionId, 1};
  operations::Request r = f.request([&](int) {
    operations::Step s;
    s.result = "{\"returned\":1}";
    s.covered = {Target::view(f.viewId)};
    return s;
  });
  r.kind = "discovery";
  r.sessionWide = true;
  REQUIRE(f.engine->submit(f.creds, ref, r).code == Outcome::OK);

  auto pending = f.log->get(1);
  REQUIRE(pending);
  REQUIRE(pending->kind == OperationKind::DISCOVERY);
  REQUIRE(pending->targets.empty());

  f.integration.service();
  auto done = f.log->get(1);
  REQUIRE(done);
  REQUIRE(done->terminal == Outcome::OK);
  REQUIRE(done->targets.size() == 1);
  REQUIRE(done->targets[0].kind == Target::Kind::VIEW);
  REQUIRE(done->targets[0].id == f.viewId);

  // A later session without read on the unlisted scene still sees the
  // record: only the listed view is checked.
  f.reattach();
  f.authority->restrict(Target::scene(f.sceneId), Capability::READ);
  auto lookup = f.log->lookup(*f.authority, f.creds, ref);
  REQUIRE(lookup.code == Outcome::OK);
  REQUIRE(lookup.record);
  REQUIRE(lookup.record->result == "{\"returned\":1}");
  REQUIRE_FALSE(lookup.receipt);

  // Losing read on the listed view denies the whole record.
  f.authority->restrict(Target::view(f.viewId), Capability::READ);
  REQUIRE(f.log->lookup(*f.authority, f.creds, ref).code == Outcome::FORBIDDEN);
}

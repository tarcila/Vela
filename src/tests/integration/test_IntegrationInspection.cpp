// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/integration/inspection/Inspection.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <memory>
#include <string>
#include <vector>

using namespace vsr::integration;
using namespace vsr::integration::operations;
using namespace vsr::integration::inspection;

namespace {

bool confirmReady(SceneId)
{
  return true;
}

std::string field(const std::string &json, const std::string &key)
{
  const auto k = "\"" + key + "\":";
  auto p = json.find(k);
  if (p == std::string::npos)
    return {};
  p += k.size();
  if (json[p] == '"')
    return json.substr(p + 1, json.find('"', p + 1) - p - 1);
  auto e = json.find_first_of(",}", p);
  return json.substr(p, e - p);
}

struct Fixture
{
  vsr::scene::Scene scene;
  vsr::scene::Scene second;
  Integration integration;
  identity::IdentityAuthority ids;
  std::unique_ptr<identity::SceneIdentity> sceneIds;
  SceneId sceneId{};
  SceneId secondId{};
  ViewId viewId{};
  double now{100.0};
  std::unique_ptr<session::SessionAuthority> authority;
  std::unique_ptr<OperationEngine> engine;
  std::unique_ptr<Inspector> inspector;
  Credentials creds;
  std::uint64_t nextId{1};

  explicit Fixture(
      int materials = 10, PageLimits limits = {}, bool secondScene = false)
  {
    for (int i = 0; i < materials; i++)
      scene.createObject<vsr::scene::Material>("matte");
    SceneRegistration s;
    s.name = "scene";
    s.scene = &scene;
    s.grants.read = true;
    s.grants.edit = true;
    s.normalSyncHook = confirmReady;
    sceneId = *integration.registerScene(s);
    if (secondScene) {
      s.name = "second";
      s.scene = &second;
      secondId = *integration.registerScene(s);
    }
    ViewRegistration v;
    v.name = "view";
    v.scene = sceneId;
    v.grants.read = true;
    v.grants.render = true;
    viewId = *integration.registerView(v);
    REQUIRE(integration.activate().ok());
    sceneIds = std::make_unique<identity::SceneIdentity>(ids, scene);
    authority =
        session::SessionAuthority::create(integration, [this] { return now; });
    engine = OperationEngine::create(
        integration, *authority, {}, [this] { return now; });
    inspector = std::make_unique<Inspector>(
        integration,
        *authority,
        [this](SceneId id) {
          return id == sceneId ? sceneIds.get() : nullptr;
        },
        limits);
    attach();
  }

  void attach()
  {
    auto a = authority->attach(authority->launchCredential());
    REQUIRE(a.code == session::Code::OK);
    creds = {authority->launchCredential(), a.sessionId, a.sessionSecret};
  }

  Status discover(PageArgs args, std::uint64_t id)
  {
    auto a = submit(inspector->makeDiscovery(creds.sessionId, args), id);
    REQUIRE(a.code == Outcome::OK);
    return drain(id);
  }

  Admission submit(Built b, std::uint64_t id)
  {
    REQUIRE(b.code == Outcome::OK);
    return engine->submit(creds, {creds.sessionId, id}, std::move(b.request));
  }

  Status drain(std::uint64_t id)
  {
    while (integration.service() > 0) {
    }
    return engine->status(creds, {creds.sessionId, id});
  }

  Status inspect(PageArgs args, std::uint64_t id)
  {
    auto a = submit(inspector->makeInspection(creds.sessionId, sceneId, args), id);
    REQUIRE(a.code == Outcome::OK);
    return drain(id);
  }
};

} // namespace

SCENARIO("page_size defaults, bounds and rejection", "[integration]")
{
  Fixture f(300);
  auto &insp = *f.inspector;
  REQUIRE(insp.makeInspection(f.creds.sessionId, f.sceneId, {0, ""}).code
      == Outcome::INVALID_REQUEST);
  REQUIRE(insp.makeInspection(f.creds.sessionId, f.sceneId, {257, ""}).code
      == Outcome::INVALID_REQUEST);
  REQUIRE(insp.makeInspection(f.creds.sessionId, f.sceneId, {256, ""}).code
      == Outcome::OK);

  auto s = f.inspect({}, 1);
  REQUIRE(*s.terminal == Outcome::OK);
  CHECK(field(s.result, "returned") == "64");
  CHECK(field(s.result, "complete") == "false");
  CHECK(field(s.result, "stop") == "item");
  const auto cursor = field(s.result, "cursor");

  // Changing page size on the continuation keeps the cursor valid.
  auto s2 = f.inspect({200, cursor}, 2);
  REQUIRE(*s2.terminal == Outcome::OK);
  CHECK(field(s2.result, "returned") == "200");

  // Identical retry replays the retained page.
  auto b = f.inspector->makeInspection(f.creds.sessionId, f.sceneId, {200, cursor});
  auto replay = f.submit(std::move(b), 2);
  REQUIRE(replay.replayed);
  CHECK(replay.status.result == s2.result);

  auto s3 = f.inspect({256, field(s2.result, "cursor")}, 3);
  CHECK(field(s3.result, "complete") == "true");
  CHECK(field(s3.result, "returned") != "0");
  CHECK(field(s3.result, "cursor").empty());
  CHECK(f.engine->slotsInUse() == 0);
  CHECK(f.inspector->scratch().used() == 0);
}

SCENARIO("pages yield in slices while keeping their slot", "[integration]")
{
  Fixture f(1000);
  auto a = f.submit(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, {256, ""}), 1);
  REQUIRE(a.code == Outcome::OK);
  f.integration.service(); // one slice
  auto st = f.engine->status(f.creds, {f.creds.sessionId, 1});
  CHECK(st.progress == Progress::YIELDED);
  CHECK(st.heldSlots == 1);
  auto done = f.drain(1);
  CHECK(*done.terminal == Outcome::OK);
}

SCENARIO("work limit and deadline", "[integration]")
{
  PageLimits l;
  l.visits = 100;
  Fixture f(300, l);
  auto s = f.inspect({256, ""}, 1);
  CHECK(field(s.result, "stop") == "work");
  CHECK(std::stoi(field(s.result, "returned")) <= 100);
  CHECK(field(s.result, "visited") == "100");

  auto a = f.submit(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, {}), 2);
  REQUIRE(a.code == Outcome::OK);
  f.now += 6.0;
  auto t = f.drain(2);
  CHECK(*t.terminal == Outcome::TIMEOUT);
}

SCENARIO("cursors are stale on returned-field change and tamper-proof",
    "[integration]")
{
  Fixture f(100);
  auto s = f.inspect({10, ""}, 1);
  const auto cursor = field(s.result, "cursor");
  REQUIRE(!cursor.empty());

  std::string bad = cursor;
  bad[3] = bad[3] == 'a' ? 'b' : 'a';
  CHECK(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, {10, bad}).code
      == Outcome::INVALID_REQUEST);
  CHECK(f.inspector->makeInspection("other", f.sceneId, {10, cursor}).code
      == Outcome::INVALID_REQUEST);
  CHECK(f.inspector->makeDiscovery(f.creds.sessionId, {10, cursor}).code
      == Outcome::INVALID_REQUEST);

  f.scene.getObject<vsr::scene::Material>(0)->setName("renamed");
  auto st = f.inspect({10, cursor}, 2);
  CHECK(*st.terminal == Outcome::CURSOR_STALE);
}

SCENARIO("discovery lists readable targets with current capabilities",
    "[integration]")
{
  Fixture f;
  auto a = f.submit(f.inspector->makeDiscovery(f.creds.sessionId, {1, ""}), 1);
  REQUIRE(a.code == Outcome::OK);
  auto s = f.drain(1);
  CHECK(s.result.find("\"kind\":\"scene\"") != std::string::npos);
  CHECK(s.result.find("\"edit\":true") != std::string::npos);
  const auto cursor = field(s.result, "cursor");
  REQUIRE(!cursor.empty());

  f.authority->restrict(session::Target::scene(f.sceneId),
      session::Capability::EDIT);
  f.submit(f.inspector->makeDiscovery(f.creds.sessionId, {1, cursor}), 2);
  CHECK(*f.drain(2).terminal == Outcome::CURSOR_STALE);

  f.authority->restrict(session::Target::view(f.viewId),
      session::Capability::READ);
  f.submit(f.inspector->makeDiscovery(f.creds.sessionId, {}), 3);
  auto all = f.drain(3);
  CHECK(field(all.result, "returned") == "1");
  CHECK(all.result.find("\"kind\":\"view\"") == std::string::npos);
}

SCENARIO("optional text truncation is marked; oversize is item_too_large",
    "[integration]")
{
  PageLimits l;
  l.itemBytes = 6000;
  Fixture f(0, l);
  auto m = f.scene.createObject<vsr::scene::Material>("matte");
  m->setName(std::string(5000, 'x'));
  auto s = f.inspect({}, 1);
  CHECK(s.result.find("\"truncated\":true,\"original_length\":5000")
      != std::string::npos);

  PageLimits tiny;
  tiny.itemBytes = 64;
  Fixture g(1, tiny);
  CHECK(*g.inspect({}, 1).terminal == Outcome::ITEM_TOO_LARGE);
}

SCENARIO("inspection returns current values of editable parameters",
    "[integration]")
{
  Fixture f(0);
  auto m = f.scene.createObject<vsr::scene::Material>("matte");
  m->setParameter("color", vsr::core::math::float3(0.1f, 0.5f, 1.f));
  m->setParameter("opacity", 0.25f);
  auto l = f.scene.createObject<vsr::scene::Light>("directional");
  l->setParameter("color", vsr::core::math::float3(1.f, 0.f, 0.5f));
  l->setParameter("irradiance", 2.5f);
  l->setParameter("direction", vsr::core::math::float2(30.f, 45.f));
  auto s = f.inspect({256, ""}, 1);
  REQUIRE(*s.terminal == Outcome::OK);
  CHECK(s.result.find("\"values\":{\"color\":[0.10000000149011612,0.5,1],"
                      "\"opacity\":0.25}")
      != std::string::npos);
  CHECK(s.result.find("\"values\":{\"color\":[1,0,0.5],\"irradiance\":2.5,"
                      "\"direction\":[30,45]}")
      != std::string::npos);
  // Only matte materials and directional lights carry values.
  std::size_t n = 0;
  for (auto p = s.result.find("\"values\""); p != std::string::npos;
       p = s.result.find("\"values\"", p + 1))
    n++;
  CHECK(n == 3); // plus the scene's default matte material
}

SCENARIO("name and metadata changes stale cursors without a full refresh",
    "[integration]")
{
  Fixture f(100);
  auto s = f.inspect({10, ""}, 1);
  auto cursor = field(s.result, "cursor");
  f.scene.getObject<vsr::scene::Material>(50)->setMetadataValue("note", 1);
  CHECK(*f.inspect({10, cursor}, 2).terminal == Outcome::CURSOR_STALE);

  s = f.inspect({10, ""}, 3);
  cursor = field(s.result, "cursor");
  auto page = f.inspect({10, cursor}, 4);
  CHECK(*page.terminal == Outcome::OK);
  f.scene.getObject<vsr::scene::Material>(50)->setName("key");
  CHECK(*f.inspect({10, cursor}, 5).terminal == Outcome::CURSOR_STALE);
}

SCENARIO("inspection after mass removal is safe", "[integration]")
{
  // Ticket 55's crash held raw Object pointers from createObject() across
  // pool growth; removal through fresh lookups must always be safe.
  Fixture f(0);
  std::vector<std::size_t> made;
  for (int i = 0; i < 150; i++)
    made.push_back(f.scene.createObject<vsr::scene::Material>("matte").index());
  for (int i = 0; i < 149; i++)
    f.scene.removeObject(f.scene.getObject(ANARI_MATERIAL, made[i]));
  auto s = f.inspect({256, ""}, 1);
  REQUIRE(*s.terminal == Outcome::OK);
  CHECK(field(s.result, "complete") == "true");
  // default material + survivor + default camera
  CHECK(field(s.result, "returned") == "3");

  f.scene.removeUnusedObjects(true);
  f.scene.defragmentObjectStorage();
  auto t = f.inspect({256, ""}, 2);
  REQUIRE(*t.terminal == Outcome::OK);
  CHECK(field(t.result, "complete") == "true");
}

SCENARIO("discovery is admitted session-wide", "[integration]")
{
  GIVEN("only a non-first scene is readable")
  {
    Fixture f(1, {}, true);
    f.authority->restrict(session::Target::scene(f.sceneId),
        session::Capability::READ);
    f.authority->restrict(session::Target::view(f.viewId),
        session::Capability::READ);
    auto s = f.discover({}, 1);
    REQUIRE(*s.terminal == Outcome::OK);
    CHECK(field(s.result, "returned") == "1");
    CHECK(s.result.find("\"kind\":\"scene\",\"id\":"
                        + std::to_string(f.secondId))
        != std::string::npos);
  }
  GIVEN("no readable target")
  {
    Fixture f(1);
    f.authority->restrict(session::Target::scene(f.sceneId),
        session::Capability::READ);
    f.authority->restrict(session::Target::view(f.viewId),
        session::Capability::READ);
    auto s = f.discover({}, 1);
    REQUIRE(*s.terminal == Outcome::OK);
    CHECK(field(s.result, "returned") == "0");
    CHECK(field(s.result, "complete") == "true");
  }
  GIVEN("a page listing only the view, looked up from a later session")
  {
    Fixture f(1);
    f.authority->restrict(session::Target::scene(f.sceneId),
        session::Capability::READ);
    auto s = f.discover({}, 1);
    REQUIRE(field(s.result, "returned") == "1");
    const OperationRef old{f.creds.sessionId, 1};
    REQUIRE(f.engine->detach(f.creds) == Outcome::OK);
    f.attach();
    THEN("covered targets are exactly the listed view")
    {
      auto st = f.engine->status(f.creds, old);
      REQUIRE(st.code == Outcome::OK);
      CHECK(st.result == s.result);
    }
    THEN("withdrawing read on the listed view denies the foreign lookup")
    {
      f.authority->restrict(session::Target::view(f.viewId),
          session::Capability::READ);
      CHECK(f.engine->status(f.creds, old).code == Outcome::FORBIDDEN);
    }
  }
}

SCENARIO("scratch is released on every terminal path of a page",
    "[integration]")
{
  // Small slices so a partially built page holds scratch across safe points.
  PageLimits l;
  l.sliceVisits = 8;
  Fixture f(200, l);
  auto startPage = [&](std::uint64_t id) {
    auto a = f.submit(
        f.inspector->makeInspection(f.creds.sessionId, f.sceneId, {256, ""}),
        id);
    REQUIRE(a.code == Outcome::OK);
    f.integration.service(); // one slice: items appended, scratch reserved
    REQUIRE(!f.engine->status(f.creds, {f.creds.sessionId, id}).terminal);
    REQUIRE(f.inspector->scratch().used() > 0);
  };

  SECTION("cancelled")
  {
    startPage(1);
    f.engine->cancel(f.creds, {f.creds.sessionId, 1});
    auto s = f.drain(1);
    CHECK(*s.terminal == Outcome::CANCELLED);
  }
  SECTION("timed out")
  {
    startPage(1);
    f.now += 6.0;
    auto s = f.drain(1);
    CHECK(*s.terminal == Outcome::TIMEOUT);
  }
  SECTION("session ended")
  {
    startPage(1);
    REQUIRE(f.authority->revokeSession());
    while (f.integration.service() > 0) {
    }
  }
  CHECK(f.engine->slotsInUse() == 0);
  CHECK(f.inspector->scratch().used() == 0);
}

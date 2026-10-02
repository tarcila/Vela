// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/integration/inspection/Inspection.hpp"
#include "vsr/integration/mcp/Json.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <limits>
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
        [this](SceneId id) { return id == sceneId ? sceneIds.get() : nullptr; },
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
    auto a =
        submit(inspector->makeInspection(creds.sessionId, sceneId, args), id);
    REQUIRE(a.code == Outcome::OK);
    return drain(id);
  }
};

vsr::integration::mcp::json::Value page(const Status &status)
{
  REQUIRE(status.terminal == Outcome::OK);
  vsr::integration::mcp::json::Value parsed;
  REQUIRE(vsr::integration::mcp::json::parse(status.result, parsed));
  return parsed;
}

const vsr::integration::mcp::json::Value &parameter(
    const vsr::integration::mcp::json::Value &item, const char *name)
{
  const auto *params = item.get("parameters");
  REQUIRE(params);
  for (const auto &p : params->a)
    if (p.get("name")->s == name)
      return p;
  FAIL("missing parameter " << name);
  return item;
}

} // namespace

SCENARIO("ordinary read-only parameters retain ANARI types and stored values",
    "[integration][typed-inspection]")
{
  Fixture f(0);
  auto camera = f.scene.createObject<vsr::scene::Camera>("perspective");
  camera->setParameter("observedBool", true);
  const int32_t integer = -27;
  camera->setParameter("observedInt", ANARI_INT32, &integer);
  const double real = 1.2345678901234567;
  camera->setParameter("observedDouble", ANARI_FLOAT64, &real);
  camera->setParameter("observedString", "camera label");
  camera->setParameter("embeddedNull", std::string("a\0b", 3));
  const float vector[] = {1.25f, -2.5f, 3.75f};
  camera->setParameter("observedVector", ANARI_FLOAT32_VEC3, vector);
  const float matrix[] = {
      1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15, 4, 8, 12, 16};
  camera->setParameter("observedMatrix", ANARI_FLOAT32_MAT4, matrix);
  auto result = page(f.inspect({}, 1));
  const auto &item = result.get("items")->a.back();
  auto value = [&](const char *name) -> const auto & {
    const auto *v = parameter(item, name).get("value");
    REQUIRE(v);
    CHECK(v->get("state")->s == "available");
    return *v;
  };
  CHECK(value("observedBool").get("data")->b);
  CHECK(value("observedInt").get("type")->s == "ANARI_INT32");
  CHECK(value("observedInt").get("data")->n == -27);
  CHECK(value("observedDouble").get("data")->n == real);
  CHECK(value("observedString").get("data")->s == "camera label");
  CHECK(value("embeddedNull").get("data")->s == std::string("a\0b", 3));
  const auto &components = value("observedVector").get("data")->a;
  REQUIRE(components.size() == 3);
  CHECK(components[0].n == 1.25);
  CHECK(components[1].n == -2.5);
  CHECK(components[2].n == 3.75);
  const auto &rows = value("observedMatrix").get("data")->a;
  REQUIRE(rows.size() == 4);
  CHECK(rows[0].a[3].n == 4);
  CHECK(rows[2].a[0].n == 9);
  CHECK(rows[3].a[2].n == 15);
  CHECK_FALSE(findEditableField(
      camera->type(), camera->subtype(), vsr::core::Token("observedDouble")));
}

SCENARIO("large integers and non-finite components are lossless typed tags",
    "[integration][typed-inspection]")
{
  Fixture f(0);
  auto renderer = f.scene.createRenderer("helide", "default");
  const int64_t integers[] = {-9007199254740991ll,
      9007199254740992ll,
      std::numeric_limits<int64_t>::min()};
  renderer->setParameter("integers", ANARI_INT64_VEC3, integers);
  const uint64_t largest = std::numeric_limits<uint64_t>::max();
  renderer->setParameter("largest", ANARI_UINT64, &largest);
  const float floats[] = {std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::infinity(),
      -std::numeric_limits<float>::infinity(),
      -0.f};
  renderer->setParameter("floats", ANARI_FLOAT32_VEC4, floats);
  auto result = page(f.inspect({}, 1));
  const auto &item = result.get("items")->a.back();
  const auto &ints = parameter(item, "integers").get("value")->get("data")->a;
  CHECK(ints[0].n == -9007199254740991.0);
  CHECK(ints[1].get("encoding")->s == "decimal");
  CHECK(ints[1].get("type")->s == "ANARI_INT64_VEC3");
  CHECK(ints[1].get("data")->s == "9007199254740992");
  CHECK(ints[2].get("data")->s == "-9223372036854775808");
  CHECK(parameter(item, "largest").get("value")->get("data")->get("data")->s
      == "18446744073709551615");
  const auto &fs = parameter(item, "floats").get("value")->get("data")->a;
  CHECK(fs[0].get("encoding")->s == "non_finite");
  CHECK(fs[0].get("data")->s == "nan");
  CHECK(fs[1].get("data")->s == "positive_infinity");
  CHECK(fs[2].get("data")->s == "negative_infinity");
  CHECK(std::signbit(fs[3].n));

  auto matte = f.scene.createObject<vsr::scene::Material>("matte");
  matte->setParameter("opacity", std::numeric_limits<float>::infinity());
  // The legacy projection must also remain valid JSON, never fabricated zero.
  auto legacy = page(f.inspect({}, 2));
  REQUIRE(legacy.get("items"));
  CHECK(f.inspector->scratch().used() == 0);
}

SCENARIO(
    "object links distinguish valid null dangling and unmapped array summaries",
    "[integration][typed-inspection]")
{
  Fixture f(0);
  auto array = f.scene.createArrayProxy(ANARI_FLOAT32_VEC3, 7, 5);
  auto material = f.scene.createObject<vsr::scene::Material>("physicallyBased");
  const auto arrayIndex = array.index();
  const auto materialIndex = material.index();
  auto surface = f.scene.createSurface("links");
  surface->setParameterObject("material", *material);
  surface->setParameterObject("input", *array);
  const size_t nullIndex = vsr::core::Any::INVALID_INDEX;
  const size_t danglingIndex = 987654;
  surface->addParameter("nullLink")
      .setValue(vsr::core::Any(ANARI_GEOMETRY, nullIndex));
  surface->addParameter("danglingLink")
      .setValue(vsr::core::Any(ANARI_GEOMETRY, danglingIndex));
  auto result = page(f.inspect({}, 1));
  const auto &items = result.get("items")->a;
  const auto &item =
      items.at(1); // array then surface, before materials/cameras
  const auto *link = parameter(item, "material").get("value");
  REQUIRE(link);
  CHECK(link->get("state")->s == "valid");
  CHECK(link->get("type")->s == "ANARI_MATERIAL");
  CHECK(link->get("handle")->s == items.at(3).get("handle")->s);
  const auto *input = parameter(item, "input").get("value");
  CHECK(input->get("handle")->s == items.at(0).get("handle")->s);
  const auto *summary = input->get("summary");
  REQUIRE(summary);
  CHECK(summary->get("element_type")->s == "ANARI_FLOAT32_VEC3");
  CHECK(summary->get("dimensionality")->n == 2);
  CHECK(summary->get("dimensions")->a.at(0).n == 7);
  CHECK(summary->get("dimensions")->a.at(1).n == 5);
  CHECK(summary->get("element_count")->n == 35);
  CHECK_FALSE(summary->get("data"));
  CHECK(parameter(item, "nullLink").get("value")->get("state")->s == "null");
  CHECK(parameter(item, "danglingLink").get("value")->get("state")->s
      == "unavailable");
  CHECK(parameter(item, "danglingLink").get("value")->get("reason")->s
      == "dangling_reference");
  CHECK(f.scene.getObject<vsr::scene::Array>(arrayIndex)->isProxy());
  CHECK(f.scene.getObject<vsr::scene::Material>(materialIndex)->subtype()
      == vsr::core::Token("physicallyBased"));
}

SCENARIO("optional representations are bounded with explicit reasons",
    "[integration][typed-inspection]")
{
  PageLimits limits;
  limits.displayString = 4;
  Fixture f(0, limits);
  auto camera = f.scene.createObject<vsr::scene::Camera>("perspective");
  const std::string text = "ab\xe2\x82\xac" + std::string(1000000, 'x');
  camera->setParameter("longText", text);
  int sentinel = 42;
  camera->setParameter("pointer", ANARI_VOID_POINTER, &sentinel);
  auto result = page(f.inspect({}, 1));
  const auto &item = result.get("items")->a.back();
  const auto *preview = parameter(item, "longText").get("value");
  REQUIRE(preview);
  CHECK(preview->get("state")->s == "truncated");
  CHECK(preview->get("reason")->s == "display_string_limit");
  CHECK(preview->get("data")->s == "ab");
  CHECK(result.get("items")->a.back().get("handle")->s.size() == 32);
  const auto *unsupported = parameter(item, "pointer").get("value");
  CHECK(unsupported->get("state")->s == "unavailable");
  CHECK(unsupported->get("reason")->s == "unsupported_type");
  CHECK_FALSE(unsupported->get("data"));
  CHECK(f.inspector->scratch().used() == 0);

  PageLimits small;
  small.optionalBytes = 300;
  Fixture g(0, small);
  auto observed = g.scene.createObject<vsr::scene::Camera>("perspective");
  observed->setParameter("hugeString", std::string(1000000, 'y'));
  auto bounded = page(g.inspect({}, 1));
  const auto &limited = bounded.get("items")->a.back();
  CHECK(limited.get("parameters_truncated")->b);
  CHECK(limited.get("parameters_reason")->s == "optional_byte_limit");
  CHECK(limited.get("parameters_omitted")->n > 0);
  CHECK(bounded.get("items")->a.back().get("type")->s == "ANARI_CAMERA");
  CHECK(g.inspector->scratch().used() == 0);
}

SCENARIO("new observed values and relationships invalidate inspection cursors",
    "[integration][typed-inspection]")
{
  Fixture f(3);
  auto camera = f.scene.createObject<vsr::scene::Camera>("perspective");
  camera->setParameter("observed", 1.f);
  auto array = f.scene.createArrayProxy(ANARI_FLOAT32, 7);
  camera->setParameterObject("input", *array);
  const auto before = page(f.inspect({}, 1));
  const auto oldVersion = before.get("items")->a.back().get("version")->n;
  const auto first = page(f.inspect({1, ""}, 2));
  const auto cursor = first.get("cursor")->s;
  bool parameterChanged = true;

  SECTION("non-editable change back still invalidates")
  {
    camera->setParameter("observed", 2.f);
    camera->setParameter("observed", 1.f);
  }
  SECTION("parameter structure invalidates")
  {
    camera->removeParameter("observed");
  }
  SECTION("reference and summary changes invalidate")
  {
    auto other = f.scene.createArrayProxy(ANARI_UINT8, 4, 3, 2);
    camera->setParameterObject("input", *other);
    auto changed = page(f.inspect({}, 3));
    const auto *summary = parameter(changed.get("items")->a.back(), "input")
                              .get("value")
                              ->get("summary");
    CHECK(summary->get("element_type")->s == "ANARI_UINT8");
    CHECK(summary->get("dimensionality")->n == 3);
    CHECK(summary->get("element_count")->n == 24);
  }
  SECTION("referenced object lifetime invalidates")
  {
    parameterChanged = false;
    f.scene.removeObject(array.data());
    auto changed = page(f.inspect({}, 3));
    const auto *value =
        parameter(changed.get("items")->a.back(), "input").get("value");
    CHECK(value->get("state")->s == "unavailable");
    CHECK(value->get("reason")->s == "dangling_reference");
  }
  CHECK(f.inspect({1, cursor}, 4).terminal == Outcome::CURSOR_STALE);
  auto after = page(f.inspect({}, 5));
  CHECK(after.get("revision")->s != before.get("revision")->s);
  const auto newVersion = after.get("items")->a.back().get("version")->n;
  if (parameterChanged)
    CHECK(newVersion > oldVersion);
  else
    CHECK(newVersion == oldVersion);
}

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

  // Continuations retain the effective page policy.
  CHECK(insp.makeInspection(f.creds.sessionId, f.sceneId, {200, cursor}).code
      == Outcome::CURSOR_STALE);
  auto s2 = f.inspect({64, cursor}, 2);
  REQUIRE(*s2.terminal == Outcome::OK);
  CHECK(field(s2.result, "returned") == "64");

  // Identical retry replays the retained page.
  auto b =
      f.inspector->makeInspection(f.creds.sessionId, f.sceneId, {64, cursor});
  auto replay = f.submit(std::move(b), 2);
  REQUIRE(replay.replayed);
  CHECK(replay.status.result == s2.result);

  auto s3 = f.inspect({64, field(s2.result, "cursor")}, 3);
  for (std::uint64_t id = 4; field(s3.result, "complete") == "false"; id++) {
    REQUIRE(id < 10);
    s3 = f.inspect({64, field(s3.result, "cursor")}, id);
  }
  CHECK(field(s3.result, "complete") == "true");
  CHECK(field(s3.result, "returned") != "0");
  CHECK(field(s3.result, "cursor").empty());
  CHECK(f.engine->slotsInUse() == 0);
  CHECK(f.inspector->scratch().used() == 0);
}

SCENARIO("object filters intersect with literal case-sensitive names",
    "[integration][object-filters]")
{
  Fixture f(0);
  f.scene.createObject<vsr::scene::Material>("matte")->setName("Queen.*");
  f.scene.createObject<vsr::scene::Material>("matte")->setName("Queen.*");
  f.scene.createObject<vsr::scene::Material>("physicallyBased")
      ->setName("Queen.*");
  f.scene.createObject<vsr::scene::Light>("directional")->setName("Queen.*");
  f.scene.createObject<vsr::scene::Material>("matte")->setName("queen.*");

  PageArgs args;
  args.filters.name = "Queen.*";
  auto s = f.inspect(args, 1);
  REQUIRE(*s.terminal == Outcome::OK);
  CHECK(field(s.result, "returned") == "4");
  CHECK(field(s.result, "complete") == "true");

  args.filters.objectType = "ANARI_MATERIAL";
  args.filters.objectSubtype = "matte";
  s = f.inspect(args, 2);
  REQUIRE(*s.terminal == Outcome::OK);
  CHECK(field(s.result, "returned") == "2");
  const auto handle = field(s.result, "handle");

  args.filters.name.reset();
  args.filters.nameContains = ".*";
  s = f.inspect(args, 3);
  CHECK(field(s.result, "returned") == "3");
  args.filters.handle = handle;
  s = f.inspect(args, 4);
  CHECK(field(s.result, "returned") == "1");
  CHECK(field(s.result, "handle") == handle);
  args.filters.name = "queen.*";
  s = f.inspect(args, 5);
  CHECK(field(s.result, "returned") == "0");
  CHECK(field(s.result, "complete") == "true");
}

SCENARIO("object filter validation, handle lifetime and historical replay",
    "[integration][object-filters]")
{
  Fixture f(3);
  PageArgs args;
  args.filters.nameContains = "";
  args.pageSize = 1;
  auto s = f.inspect(args, 1);
  REQUIRE(*s.terminal == Outcome::OK);
  const auto cursor = field(s.result, "cursor");
  const auto handle = field(s.result, "handle");
  args.cursor = cursor;
  args.filters.name = "substituted";
  CHECK(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args).code
      == Outcome::CURSOR_STALE);
  args.cursor.clear();
  args.filters.name.reset();
  args.filters.handle = handle;
  auto direct = f.inspect(args, 2);
  CHECK(field(direct.result, "returned") == "1");
  auto *target = f.scene.getObject<vsr::scene::Material>(0).data();
  f.scene.removeObject(target);
  f.scene.createObject<vsr::scene::Material>("matte");
  CHECK(*f.inspect(args, 3).terminal == Outcome::CONFLICT);
  auto replay = f.submit(
      f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args), 2);
  REQUIRE(replay.replayed);
  CHECK(replay.status.result == direct.result);

  for (const char *bad : {"", "ANARI_FLOAT32", "material"}) {
    args = {};
    args.filters.objectType = bad;
    CHECK(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args).code
        == Outcome::INVALID_REQUEST);
  }
  args = {};
  args.filters.handle = "not-a-handle";
  CHECK(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args).code
      == Outcome::INVALID_REQUEST);
  args = {};
  args.filters.name = std::string(4097, 'x');
  CHECK(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args).code
      == Outcome::INVALID_REQUEST);
  args = {};
  args.kind = "nodes";
  CHECK(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args).code
      == Outcome::INVALID_REQUEST);
  args = {};
  args.filters.name = "Queen";
  CHECK(f.inspector->makeDiscovery(f.creds.sessionId, args).code
      == Outcome::INVALID_REQUEST);
}

SCENARIO("object filter policy consumes shared scratch even without matches",
    "[integration][object-filters]")
{
  PageLimits limits;
  limits.scratchBytes = 128;
  Fixture f(0, limits);
  PageArgs args;
  args.filters.name = std::string(4096, 'x');
  CHECK(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args).code
      == Outcome::BUSY);
  CHECK(f.inspector->scratch().used() == 0);
}

SCENARIO("sparse object filters retain bounded work and continuation",
    "[integration][object-filters]")
{
  PageLimits limits;
  limits.visits = 3;
  limits.sliceVisits = 1;
  Fixture f(8, limits);
  f.scene.getObject<vsr::scene::Material>(7)->setName("needle");
  PageArgs args;
  args.filters.name = "needle";
  auto ack = f.submit(
      f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args), 1);
  REQUIRE(ack.code == Outcome::OK);
  f.integration.service();
  CHECK_FALSE(f.engine->status(f.creds, {f.creds.sessionId, 1}).terminal);
  auto page = f.drain(1);
  CHECK(field(page.result, "stop") == "work");
  CHECK(field(page.result, "complete") == "false");
  CHECK(field(page.result, "returned") == "0");
  CHECK(field(page.result, "visited") == "3");
  int returned = 0;
  for (std::uint64_t id = 2; field(page.result, "complete") == "false"; id++) {
    REQUIRE(id < 8);
    args.cursor = field(page.result, "cursor");
    REQUIRE_FALSE(args.cursor.empty());
    page = f.inspect(args, id);
    REQUIRE(*page.terminal == Outcome::OK);
    returned += std::stoi(field(page.result, "returned"));
  }
  CHECK(returned == 1);
  CHECK(f.inspector->scratch().used() == 0);

  args = {};
  args.filters.nameContains = "absent";
  ack = f.submit(
      f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args), 10);
  REQUIRE(ack.code == Outcome::OK);
  f.now += 6.0;
  CHECK(*f.drain(10).terminal == Outcome::TIMEOUT);
  CHECK(f.inspector->scratch().used() == 0);
}

SCENARIO("pages yield in slices while keeping their slot", "[integration]")
{
  Fixture f(1000);
  auto a = f.submit(
      f.inspector->makeInspection(f.creds.sessionId, f.sceneId, {256, ""}), 1);
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

  auto a = f.submit(
      f.inspector->makeInspection(f.creds.sessionId, f.sceneId, {}), 2);
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
  CHECK(
      f.inspector->makeInspection(f.creds.sessionId, f.sceneId, {10, bad}).code
      == Outcome::INVALID_REQUEST);
  CHECK(f.inspector->makeInspection("other", f.sceneId, {10, cursor}).code
      == Outcome::CURSOR_STALE);
  CHECK(f.inspector->makeDiscovery(f.creds.sessionId, {10, cursor}).code
      == Outcome::CURSOR_STALE);

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
  CHECK(s.result.find("\"inspection\":{\"kinds\":[\"objects\"],"
                      "\"default_page_size\":64,\"max_page_size\":256}")
      != std::string::npos);
  CHECK(s.result.find("\"edit\":true") != std::string::npos);
  const auto cursor = field(s.result, "cursor");
  REQUIRE(!cursor.empty());

  f.authority->restrict(
      session::Target::scene(f.sceneId), session::Capability::EDIT);
  f.submit(f.inspector->makeDiscovery(f.creds.sessionId, {1, cursor}), 2);
  CHECK(*f.drain(2).terminal == Outcome::CURSOR_STALE);

  f.authority->restrict(
      session::Target::view(f.viewId), session::Capability::READ);
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

SCENARIO(
    "inspection returns current values of editable parameters", "[integration]")
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
    f.authority->restrict(
        session::Target::scene(f.sceneId), session::Capability::READ);
    f.authority->restrict(
        session::Target::view(f.viewId), session::Capability::READ);
    auto s = f.discover({}, 1);
    REQUIRE(*s.terminal == Outcome::OK);
    CHECK(field(s.result, "returned") == "1");
    CHECK(
        s.result.find("\"kind\":\"scene\",\"id\":" + std::to_string(f.secondId))
        != std::string::npos);
  }
  GIVEN("no readable target")
  {
    Fixture f(1);
    f.authority->restrict(
        session::Target::scene(f.sceneId), session::Capability::READ);
    f.authority->restrict(
        session::Target::view(f.viewId), session::Capability::READ);
    auto s = f.discover({}, 1);
    REQUIRE(*s.terminal == Outcome::OK);
    CHECK(field(s.result, "returned") == "0");
    CHECK(field(s.result, "complete") == "true");
  }
  GIVEN("a page listing only the view, looked up from a later session")
  {
    Fixture f(1);
    f.authority->restrict(
        session::Target::scene(f.sceneId), session::Capability::READ);
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
      f.authority->restrict(
          session::Target::view(f.viewId), session::Capability::READ);
      CHECK(f.engine->status(f.creds, old).code == Outcome::FORBIDDEN);
    }
  }
}

SCENARIO(
    "scratch is released on every terminal path of a page", "[integration]")
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

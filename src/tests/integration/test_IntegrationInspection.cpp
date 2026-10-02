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

SCENARIO("node edit tokens remember intervening ordinary writes",
    "[integration][node-version]")
{
  Fixture f(0);
  auto *layer = f.scene.defaultLayer();
  auto node = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, vsr::math::IDENTITY_MAT4, "target"));
  f.scene.signalLayerStructureChanged(layer);
  PageArgs args;
  args.kind = "nodes";
  args.filters.name = "target";
  auto token = [&] {
    const auto result = page(f.inspect(args, f.nextId++));
    const auto &items = result.get("items")->a;
    REQUIRE(items.size() == 1);
    const auto *version = items[0].get("node_edit_version");
    REQUIRE(version);
    REQUIRE(!version->s.empty());
    return version->s;
  };
  const auto before = token();
  auto moved = vsr::math::IDENTITY_MAT4;
  moved[3].x = 7.f;
  node->value().setAsTransform(moved);
  node->value().setAsTransform(vsr::math::IDENTITY_MAT4);
  // No publication between writes; one coalesced notification afterwards.
  f.scene.signalLayerTransformChanged(layer);
  const auto after = token();
  CHECK(after != before);
  CHECK(token() == after);
  node->value().setEnabled(false);
  node->value().setEnabled(true);
  // History exists even before publication (fresh safe-point query).
  CHECK(token() != after);
}

SCENARIO("ordinary payload writes and resets advance local edit tokens",
    "[integration][node-version]")
{
  Fixture f(0);
  auto *layer = f.scene.defaultLayer();
  auto baseline = vsr::math::IDENTITY_MAT4;
  baseline[3].y = 3.f;
  auto node = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, baseline, "target"));
  f.scene.signalLayerStructureChanged(layer);
  PageArgs args;
  args.kind = "nodes";
  args.filters.name = "target";
  auto result = page(f.inspect(args, f.nextId++));
  const auto &item = result.get("items")->a.at(0);
  REQUIRE(item.get("node_edit_version"));
  const auto before = item.get("node_edit_version")->s;
  args.filters.name.reset();
  args.filters.handle = item.get("handle")->s;
  auto moved = baseline;
  moved[3].x = 9.f;
  auto &data = node->value();
  SECTION("matrix with explicit default")
  {
    data.setAsTransform(moved, baseline);
    data.setAsTransform(baseline);
  }
  SECTION("SRT write and restoration")
  {
    auto srt = data.getTransformSRT();
    srt[2].x = 9.f;
    data.setAsTransform(srt);
    data.setAsTransform(baseline);
  }
  SECTION("reset preserves original default")
  {
    data.setAsTransform(moved);
    data.setToDefaultValue();
    CHECK(data.isDefaultValue());
    CHECK(data.getTransform()[3].y == 3.f);
    CHECK(data.getTransform()[3].x == 0.f);
  }
  SECTION("reset of already default is still a write")
  {
    data.setToDefaultValue();
  }
  SECTION("copy assignment does not import the source history")
  {
    const auto saved = data;
    data.setAsTransform(moved);
    data = saved;
    CHECK(data.isDefaultValue());
  }
  SECTION("move assignment does not import the source history")
  {
    auto saved = data;
    data.setAsTransform(moved);
    data = std::move(saved);
    CHECK(data.isDefaultValue());
  }
  SECTION("self copy assignment")
  {
    data = data;
  }
  SECTION("self move assignment")
  {
    auto &alias = data;
    data = std::move(alias);
  }
  SECTION("empty and restoration")
  {
    data.setEmpty();
    data.setAsTransform(baseline);
  }
  SECTION("object type change and restoration")
  {
    auto object = f.scene.createObject<vsr::scene::Material>("matte");
    data.setAsObject(object.data());
    data.setAsTransform(baseline);
  }
  SECTION("transform array type change and restoration")
  {
    auto array = f.scene.createArray(ANARI_FLOAT32_MAT4, 1);
    data.setAsTransformArray(array.data());
    data.setAsTransform(baseline);
  }
  SECTION("raw Any setter with usage counting")
  {
    data.setValueRaw(vsr::core::Any(moved));
    data.setValueRaw(vsr::core::Any(baseline));
  }
  SECTION("raw Any setter without usage counting")
  {
    data.setValueRaw(vsr::core::Any(moved), true);
    data.setValueRaw(vsr::core::Any(baseline), true);
  }
  f.scene.signalLayerTransformChanged(layer);
  result = page(f.inspect(args, f.nextId++));
  REQUIRE(result.get("items")->a.size() == 1);
  CHECK(result.get("items")->a[0].get("node_edit_version")->s != before);
}

SCENARIO("node edit versions exclude unrelated writes and storage relocation",
    "[integration][node-version]")
{
  Fixture f(0);
  auto *layer = f.scene.defaultLayer();
  auto parent = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, vsr::math::IDENTITY_MAT4, "parent"));
  auto node = parent->insert_last_child(
      vsr::scene::LayerNodeData(layer, vsr::math::IDENTITY_MAT4, "target"));
  f.scene.signalLayerStructureChanged(layer);
  PageArgs args;
  args.kind = "nodes";
  args.filters.name = "target";
  auto result = page(f.inspect(args, f.nextId++));
  const auto &item = result.get("items")->a.at(0);
  REQUIRE(item.get("node_edit_version"));
  const auto before = item.get("node_edit_version")->s;
  const auto handle = item.get("handle")->s;
  args.filters.name.reset();
  args.filters.handle = handle;
  SECTION("rename also stales cursors")
  {
    PageArgs all;
    all.kind = "nodes";
    all.pageSize = 1;
    auto first = page(f.inspect(all, f.nextId++));
    REQUIRE(first.get("cursor"));
    all.cursor = first.get("cursor")->s;
    node->value().name() = "renamed";
    f.scene.signalLayerStructureChanged(layer);
    CHECK(f.inspect(all, f.nextId++).terminal == Outcome::CURSOR_STALE);
  }
  SECTION("parent and sibling change back")
  {
    auto moved = vsr::math::IDENTITY_MAT4;
    moved[3].x = 9.f;
    parent->value().setAsTransform(moved);
    parent->value().setAsTransform(vsr::math::IDENTITY_MAT4);
    parent->value().setEnabled(false);
    parent->value().setEnabled(true);
    auto sibling = parent->insert_last_child(vsr::scene::LayerNodeData(layer));
    sibling->value().setEnabled(false);
    f.scene.signalLayerStructureChanged(layer);
  }
  SECTION("other layer activation and object parameters")
  {
    auto *other = f.scene.addLayer("other");
    other->root()->value().setEnabled(false);
    f.scene.setLayerActive("default", false);
    auto material = f.scene.createObject<vsr::scene::Material>("matte");
    material->setParameter("opacity", 0.5f);
    f.scene.signalLayerTransformChanged(other);
  }
  SECTION("baseline metadata is not current editable content")
  {
    node->value().setCurrentValueAsDefault();
  }
  SECTION("storage grows and reuses holes")
  {
    node->container()->reserve(layer->capacity() + 1000);
    for (int i = 0; i < 30; ++i) {
      auto temp = parent->insert_last_child(vsr::scene::LayerNodeData(layer));
      temp->erase_self();
    }
    f.scene.signalLayerStructureChanged(layer);
  }
  SECTION("subtree move keeps local token and clone is distinct")
  {
    auto clone = node->container()->copy_subtree(node, layer->root());
    const auto cloneHandle = f.sceneIds->handleOf(clone);
    const auto originalHandle = f.sceneIds->handleOf(node);
    REQUIRE(cloneHandle != originalHandle);
    CHECK(f.sceneIds->nodeEditVersion(cloneHandle)
        != f.sceneIds->nodeEditVersion(originalHandle));
    node->container()->move_subtree(node, layer->root());
    node->container()->move_subtree(node, parent);
    f.scene.signalLayerStructureChanged(layer);
  }
  result = page(f.inspect(args, f.nextId++));
  REQUIRE(result.get("items")->a.size() == 1);
  CHECK(result.get("items")->a[0].get("handle")->s == handle);
  CHECK(result.get("items")->a[0].get("node_edit_version")->s == before);
}

SCENARIO("nodes expose placement observations through public inspection",
    "[integration][node-inspection]")
{
  Fixture f(0);
  auto *layer = f.scene.defaultLayer();
  auto child = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, "placement"));
  child->value().setEnabled(false);
  f.scene.signalLayerStructureChanged(layer);
  PageArgs args;
  args.kind = "nodes";
  auto result = page(f.inspect(args, 1));
  REQUIRE(result.get("items")->a.size() == 2);
  const auto &root = result.get("items")->a[0];
  const auto &node = result.get("items")->a[1];
  CHECK(node.get("parent_handle")->s == root.get("handle")->s);
  CHECK(node.get("node_kind")->s == "empty");
  CHECK_FALSE(node.get("enabled")->b);
  CHECK_FALSE(node.get("effective_enabled")->b);
  args.filters.handle = node.get("handle")->s;
  CHECK(page(f.inspect(args, 2)).get("items")->a.size() == 1);
  layer->erase(child);
  layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, "placement"));
  f.scene.signalLayerStructureChanged(layer);
  CHECK(f.inspect(args, 3).terminal == Outcome::CONFLICT);
}

SCENARIO("node kinds filters and inactive ancestry preserve local observations",
    "[integration][node-inspection]")
{
  Fixture f(0);
  auto *layer = f.scene.defaultLayer();
  auto root = layer->root();
  auto matrix = vsr::math::IDENTITY_MAT4;
  matrix[0] = {1.f, 2.f, 3.f, 0.f};
  matrix[1] = {4.f, 5.f, 6.f, 0.f};
  matrix[2] = {7.f, 8.f, 9.f, 0.f};
  matrix[3] = {10.f, 11.f, 12.f, 1.f};
  auto transform = root->insert_last_child(
      vsr::scene::LayerNodeData(layer, matrix, "duplicate"));
  transform->value().setEnabled(false);
  auto *surface = f.scene.createObject(ANARI_SURFACE, "default");
  REQUIRE(surface);
  auto placement = transform->insert_last_child(
      vsr::scene::LayerNodeData(layer, surface, "duplicate"));
  root->insert_last_child(
      vsr::scene::LayerNodeData(layer, surface, "duplicate"));
  auto array = f.scene.createArrayProxy(ANARI_FLOAT32_MAT4, 1000000);
  root->insert_last_child(vsr::scene::LayerNodeData(layer, array, "array"));
  auto missing =
      root->insert_last_child(vsr::scene::LayerNodeData(layer, "missing"));
  missing->value().setAsObject(ANARI_SURFACE, 99999);
  auto *inactive = f.scene.addLayer("inactive");
  f.scene.setLayerActive("inactive", false);
  f.scene.signalLayerStructureChanged(layer);
  PageArgs args;
  args.kind = "nodes";
  auto status = f.inspect(args, 1);
  INFO(status.result);
  auto all = page(status);
  const auto &items = all.get("items")->a;
  REQUIRE(items.size() == 7);
  CHECK(items[0].get("root")->b);
  CHECK(items[0].get("parent_handle")->type == mcp::json::Value::Type::NUL);
  CHECK(items[1].get("local_matrix")->get("data")->a[0].a[1].n == 4);
  CHECK(items[1].get("local_matrix")->get("data")->a[2].a[3].n == 12);
  REQUIRE(items[2].get("object"));
  REQUIRE(items[3].get("object"));
  CHECK(items[2].get("object")->get("handle")->s
      == items[3].get("object")->get("handle")->s);
  CHECK(items[2].get("handle")->s != items[3].get("handle")->s);
  CHECK_FALSE(items[2].get("effective_enabled")->b);
  CHECK(items[3].get("effective_enabled")->b);
  REQUIRE(items[4].get("node_kind")->s == "transform_array");
  CHECK(items[4].get("array")->get("summary")->get("element_count")->n
      == 1000000);
  CHECK_FALSE(items[4].get("local_matrix"));
  CHECK(items[5].get("object")->get("state")->s == "unavailable");
  CHECK_FALSE(items[6].get("effective_enabled")->b);
  args.filters.name = "duplicate";
  args.filters.nameContains = "plic";
  args.filters.nodeKind = "object";
  args.filters.layerHandle = items[2].get("layer_handle")->s;
  args.filters.parentHandle = items[1].get("handle")->s;
  CHECK(page(f.inspect(args, 2)).get("items")->a.size() == 1);
  args.filters.nameContains = "Plic";
  CHECK(page(f.inspect(args, 3)).get("items")->a.empty());
  args.filters = {};
  args.filters.handle = items[2].get("parent_handle")->s;
  CHECK(page(f.inspect(args, 4)).get("items")->a[0].get("node_kind")->s
      == "transform");
  for (auto bad : {"unsupported", "Object"}) {
    args.filters.nodeKind = bad;
    CHECK(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args).code
        == Outcome::INVALID_REQUEST);
  }
  args.filters = {};
  args.filters.handle = items[2].get("layer_handle")->s;
  CHECK(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args).code
      == Outcome::INVALID_REQUEST);
  args.filters = {};
  args.filters.parentHandle = "bad";
  CHECK(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args).code
      == Outcome::INVALID_REQUEST);
  (void)placement;
  (void)inactive;
}

SCENARIO("node handles follow real copy move and destruction lifetimes",
    "[integration][node-inspection]")
{
  Fixture f(0);
  auto *layer = f.scene.defaultLayer();
  auto root = layer->root();
  auto parent =
      root->insert_last_child(vsr::scene::LayerNodeData(layer, "parent"));
  auto node = root->insert_last_child(vsr::scene::LayerNodeData(layer, "same"));
  PageArgs args;
  args.kind = "nodes";
  args.filters.name = "same";
  auto original = page(f.inspect(args, 1));
  auto handle = original.get("items")->a[0].get("handle")->s;
  auto copy = root->container()->copy_subtree(node, root);
  f.scene.signalLayerStructureChanged(layer);
  auto copied = page(f.inspect(args, 2));
  REQUIRE(copied.get("items")->a.size() == 2);
  CHECK(copied.get("items")->a[0].get("handle")->s == handle);
  CHECK(copied.get("items")->a[1].get("handle")->s != handle);
  root->container()->move_subtree(node, parent);
  f.scene.signalLayerStructureChanged(layer);
  args.filters = {};
  args.filters.handle = handle;
  auto moved = page(f.inspect(args, 3));
  CHECK(moved.get("items")->a[0].get("handle")->s == handle);
  node->value().name() = "renamed";
  node->value().setEnabled(false);
  f.scene.signalLayerTransformChanged(layer);
  CHECK(page(f.inspect(args, 4)).get("items")->a[0].get("handle")->s == handle);
  layer->erase(parent);
  f.scene.signalLayerStructureChanged(layer);
  CHECK(f.inspect(args, 5).terminal == Outcome::CONFLICT);
  args.filters.handle = copied.get("items")->a[1].get("handle")->s;
  layer->clear();
  CHECK(f.inspect(args, 6).terminal == Outcome::CONFLICT);
  auto replacement =
      root->insert_last_child(vsr::scene::LayerNodeData(layer, "same"));
  args.filters = {};
  args.filters.name = "same";
  auto fresh = page(f.inspect(args, 7));
  args.filters = {};
  args.filters.handle = fresh.get("items")->a[0].get("handle")->s;
  f.scene.removeAllLayers();
  CHECK(f.inspect(args, 8).terminal == Outcome::CONFLICT);
  layer = f.scene.addLayer("new");
  layer->root()->insert_last_child(vsr::scene::LayerNodeData(layer, "same"));
  args.filters = {};
  args.filters.name = "same";
  fresh = page(f.inspect(args, 9));
  args.filters = {};
  args.filters.handle = fresh.get("items")->a[0].get("handle")->s;
  f.scene.removeAllObjects();
  CHECK(f.inspect(args, 10).terminal == Outcome::CONFLICT);
  layer = f.scene.addLayer("replacement");
  layer->root()->insert_last_child(vsr::scene::LayerNodeData(layer, "same"));
  args.filters = {};
  args.filters.name = "same";
  fresh = page(f.inspect(args, 11));
  args.filters = {};
  args.filters.handle = fresh.get("items")->a[0].get("handle")->s;
  f.sceneIds = std::make_unique<identity::SceneIdentity>(f.ids, f.scene);
  CHECK(f.inspect(args, 12).terminal == Outcome::CONFLICT);
  (void)copy;
  (void)replacement;
}

SCENARIO("node cursor policy replay and stale scopes never redirect",
    "[integration][node-inspection]")
{
  Fixture f(0);
  auto *layer = f.scene.defaultLayer();
  auto parent = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, "parent"));
  parent->insert_last_child(vsr::scene::LayerNodeData(layer, "a"));
  parent->insert_last_child(vsr::scene::LayerNodeData(layer, "b"));
  PageArgs args;
  args.kind = "nodes";
  auto all = page(f.inspect(args, 1));
  const auto &items = all.get("items")->a;
  REQUIRE(items.size() == 4);
  CHECK(items[2].get("name")->get("text")->s == "a");
  CHECK(items[3].get("name")->get("text")->s == "b");
  args.pageSize = 1;
  auto original = f.inspect(args, 2);
  auto first = page(original);
  args.cursor = first.get("cursor")->s;
  std::uint64_t request = 3;
  for (int i = 0; i < 7; ++i) {
    auto changed = args;
    if (i == 0)
      changed.filters.name = "parent";
    if (i == 1)
      changed.filters.nameContains = "a";
    if (i == 2)
      changed.filters.handle = items[1].get("handle")->s;
    if (i == 3)
      changed.filters.nodeKind = "empty";
    if (i == 4)
      changed.filters.layerHandle = items[1].get("layer_handle")->s;
    if (i == 5)
      changed.filters.parentHandle = items[1].get("handle")->s;
    if (i == 6)
      changed.pageSize = 2;
    CHECK(
        f.inspector->makeInspection(f.creds.sessionId, f.sceneId, changed).code
        == Outcome::CURSOR_STALE);
  }
  auto changedKind = args;
  changedKind.kind = "objects";
  CHECK(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, changedKind)
            .code
      == Outcome::CURSOR_STALE);
  auto next = page(f.inspect(args, request++));
  CHECK(next.get("items")->a[0].get("handle")->s == items[1].get("handle")->s);
  parent->value().setEnabled(false);
  f.scene.signalLayerTransformChanged(layer);
  CHECK(f.inspect(args, request++).terminal == Outcome::CURSOR_STALE);
  args.cursor.clear();
  auto replay = f.submit(
      f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args), 2);
  REQUIRE(replay.replayed);
  CHECK(replay.status.result == original.result);
  args.pageSize.reset();
  args.filters.parentHandle = items[1].get("handle")->s;
  layer->erase(parent);
  layer->root()->insert_last_child(vsr::scene::LayerNodeData(layer, "parent"));
  f.scene.signalLayerStructureChanged(layer);
  CHECK(f.inspect(args, request++).terminal == Outcome::CONFLICT);
  args.filters = {};
  args.filters.layerHandle = items[1].get("layer_handle")->s;
  f.scene.removeAllLayers();
  CHECK(f.inspect(args, request++).terminal == Outcome::CONFLICT);
}

SCENARIO("node scans share budgets and release scratch on interruption",
    "[integration][node-inspection]")
{
  PageLimits limits;
  limits.sliceVisits = 1;
  limits.visits = 3;
  Fixture f(0, limits);
  auto *layer = f.scene.defaultLayer();
  for (int i = 0; i < 12; ++i)
    layer->root()->insert_last_child(
        vsr::scene::LayerNodeData(layer, "hidden"));
  PageArgs args;
  args.kind = "nodes";
  args.filters.name = "absent";
  auto result = page(f.inspect(args, 1));
  CHECK(result.get("visited")->n <= 3);
  CHECK(result.get("items")->a.empty());
  CHECK_FALSE(result.get("complete")->b);
  args.cursor = result.get("cursor")->s;
  CHECK(page(f.inspect(args, 2)).get("items")->a.empty());
  args.cursor.clear();
  REQUIRE(
      f.submit(
           f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args), 3)
          .code
      == Outcome::OK);
  f.integration.service();
  REQUIRE(f.inspector->scratch().used() > 0);
  SECTION("cancel")
  {
    f.engine->cancel(f.creds, {f.creds.sessionId, 3});
    CHECK(f.drain(3).terminal == Outcome::CANCELLED);
  }
  SECTION("deadline")
  {
    f.now += 6;
    CHECK(f.drain(3).terminal == Outcome::TIMEOUT);
  }
  SECTION("session end")
  {
    REQUIRE(f.authority->revokeSession());
    while (f.integration.service() > 0) {
    }
  }
  SECTION("read revoked")
  {
    f.authority->restrict(
        session::Target::scene(f.sceneId), session::Capability::READ);
    CHECK(f.drain(3).terminal == Outcome::FORBIDDEN);
  }
  CHECK(f.engine->slotsInUse() == 0);
  CHECK(f.inspector->scratch().used() == 0);
}

SCENARIO("deep node ancestry and preorder continuations are bounded",
    "[integration][node-inspection]")
{
  PageLimits limits;
  limits.visits = 7;
  limits.sliceVisits = 2;
  Fixture f(0, limits);
  auto *layer = f.scene.defaultLayer();
  auto node = layer->root();
  for (int i = 0; i < 24; ++i)
    node = node->insert_last_child(vsr::scene::LayerNodeData(layer, "deep"));
  node->value().name() = "target";
  PageArgs args;
  args.kind = "nodes";
  args.filters.name = "target";
  std::uint64_t request = 1;
  std::size_t returned = 0;
  bool complete = false;
  std::string firstCursor;
  for (int i = 0; i < 40; ++i) {
    auto result = page(f.inspect(args, request++));
    CHECK(result.get("visited")->n <= 7);
    returned += result.get("items")->a.size();
    if (result.get("complete")->b) {
      complete = true;
      break;
    }
    args.cursor = result.get("cursor")->s;
    if (firstCursor.empty())
      firstCursor = args.cursor;
  }
  CHECK(complete);
  CHECK(returned == 1);
  args.cursor = firstCursor;
  args.filters.nodeKind = "empty";
  CHECK(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args).code
      == Outcome::CURSOR_STALE);
  args.filters.nodeKind.reset();
  node->value().setEnabled(false);
  f.scene.signalLayerTransformChanged(layer);
  CHECK(f.inspect(args, request++).terminal == Outcome::CURSOR_STALE);
  CHECK(f.inspector->scratch().used() == 0);
}

SCENARIO("layers expose live roots and stored names in scene order",
    "[integration][layer-inspection]")
{
  Fixture f(0);
  f.scene.removeAllLayers();
  auto *first = f.scene.addLayer("first-key");
  auto *second = f.scene.addLayer("second-key");
  first->editableName() = "duplicate";
  second->editableName() = "duplicate";
  f.scene.signalLayerStructureChanged(first);
  f.scene.signalLayerStructureChanged(second);
  f.scene.setLayerActive("second-key", false);
  PageArgs args;
  args.kind = "layers";
  auto result = page(f.inspect(args, 1));
  const auto &items = result.get("items")->a;
  REQUIRE(items.size() == 2);
  CHECK(items[0].get("name")->get("text")->s == "duplicate");
  CHECK(items[1].get("name")->get("text")->s == "duplicate");
  CHECK(items[0].get("active")->b);
  CHECK_FALSE(items[1].get("active")->b);
  CHECK(items[0].get("handle")->s != items[1].get("handle")->s);
  CHECK(items[0].get("root_handle")->s != items[1].get("root_handle")->s);
  CHECK(items[0].get("handle")->s != items[0].get("root_handle")->s);
  args.filters.handle = items[1].get("handle")->s;
  auto direct = page(f.inspect(args, 2));
  REQUIRE(direct.get("items")->a.size() == 1);
  CHECK(direct.get("items")->a[0].get("handle")->s == *args.filters.handle);
  CHECK(direct.get("visited")->n == 2);
}

SCENARIO("layer name filters intersect on stored literal names",
    "[integration][layer-inspection][layer-filters]")
{
  Fixture f(0);
  f.scene.removeAllLayers();
  for (const auto &key : {"first-key", "second-key", "Queen.*"}) {
    auto *layer = f.scene.addLayer(key);
    layer->editableName() =
        std::string(key) == "Queen.*" ? "queen.*" : "Queen.*";
    f.scene.signalLayerStructureChanged(layer);
  }
  PageArgs args;
  args.kind = "layers";
  args.filters.name = "Queen.*";
  auto exact = page(f.inspect(args, 1));
  REQUIRE(exact.get("items")->a.size() == 2);
  const auto handle = exact.get("items")->a[1].get("handle")->s;
  args.filters.nameContains = ".*";
  args.filters.handle = handle;
  auto direct = page(f.inspect(args, 2));
  REQUIRE(direct.get("items")->a.size() == 1);
  CHECK(direct.get("items")->a[0].get("handle")->s == handle);
  args.filters.name = "queen.*";
  CHECK(page(f.inspect(args, 3)).get("items")->a.empty());
  args.filters.handle.reset();
  args.filters.name.reset();
  args.filters.nameContains = "Queen";
  CHECK(page(f.inspect(args, 4)).get("items")->a.size() == 2);
  args.filters.nameContains = "Q*";
  auto empty = page(f.inspect(args, 5));
  CHECK(empty.get("items")->a.empty());
  CHECK(empty.get("complete")->b);
  args.filters.nameContains.reset();
  args.filters.name = "first-key";
  CHECK(page(f.inspect(args, 6)).get("items")->a.empty());
  CHECK(f.inspector->scratch().used() == 0);
}

SCENARIO("filtered layer cursors bind every filter and replay historical pages",
    "[integration][layer-inspection][layer-filters]")
{
  Fixture f(0);
  f.scene.removeAllLayers();
  for (const auto &key : {"one", "two", "three"}) {
    auto *layer = f.scene.addLayer(key);
    layer->editableName() = "duplicate";
    f.scene.signalLayerStructureChanged(layer);
  }
  PageArgs args;
  args.kind = "layers";
  args.pageSize = 1;
  args.filters.name = "duplicate";
  args.filters.nameContains = "lic";
  const auto historical = f.inspect(args, 1);
  args.cursor = page(historical).get("cursor")->s;
  for (int i = 0; i < 4; i++) {
    auto changed = args;
    if (i == 0)
      changed.filters.name.reset();
    else if (i == 1)
      changed.filters.nameContains = "dup";
    else if (i == 2)
      changed.pageSize = 2;
    else
      changed.kind = "objects";
    CHECK(
        f.inspector->makeInspection(f.creds.sessionId, f.sceneId, changed).code
        == Outcome::CURSOR_STALE);
  }
  REQUIRE(page(f.inspect(args, 2)).get("items")->a.size() == 1);
  auto *layer = f.scene.layers().at_index(0).second.ptr.get();
  layer->editableName() = "changed";
  f.scene.signalLayerStructureChanged(layer);
  CHECK(f.inspect(args, 3).terminal == Outcome::CURSOR_STALE);
  args.cursor.clear();
  CHECK(f.inspect(args, 1).result == historical.result);
  CHECK(f.inspector->scratch().used() == 0);
}

SCENARIO(
    "sparse layer filters yield bounded pages and release terminal scratch",
    "[integration][layer-inspection][layer-filters]")
{
  PageLimits limits;
  limits.visits = 3;
  limits.sliceVisits = 1;
  Fixture f(0, limits);
  f.scene.removeAllLayers();
  for (int i = 0; i < 8; i++)
    f.scene.addLayer(i == 7 ? "needle" : "hay" + std::to_string(i));
  PageArgs args;
  args.kind = "layers";
  args.filters.nameContains = "needle";
  f.submit(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args), 1);
  f.integration.service();
  CHECK_FALSE(f.engine->status(f.creds, {f.creds.sessionId, 1}).terminal);
  auto first = page(f.drain(1));
  CHECK(first.get("visited")->n == 3);
  CHECK(first.get("items")->a.empty());
  CHECK(first.get("stop")->s == "work");
  auto scan = [&](PageArgs query, std::uint64_t id) {
    std::size_t returned = 0;
    for (int pages = 0; pages < 4; pages++) {
      auto result = page(f.inspect(query, id++));
      CHECK(result.get("visited")->n <= 3);
      returned += result.get("items")->a.size();
      CHECK(f.inspector->scratch().used() == 0);
      if (result.get("complete")->b)
        return returned;
      query.cursor = result.get("cursor")->s;
    }
    FAIL("bounded scan did not terminate");
    return returned;
  };
  args.cursor = first.get("cursor")->s;
  CHECK(scan(args, 2) == 1);
  args.cursor.clear();
  args.filters.nameContains = "absent";
  CHECK(scan(args, 10) == 0);
  f.submit(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args), 20);
  f.integration.service();
  REQUIRE(f.inspector->scratch().used() > 0);
  SECTION("cancel")
  {
    f.engine->cancel(f.creds, {f.creds.sessionId, 20});
    CHECK(f.drain(20).terminal == Outcome::CANCELLED);
  }
  SECTION("deadline")
  {
    f.now += 6;
    CHECK(f.drain(20).terminal == Outcome::TIMEOUT);
  }
  SECTION("session ended")
  {
    REQUIRE(f.authority->revokeSession());
    while (f.integration.service() > 0) {
    }
  }
  CHECK(f.engine->slotsInUse() == 0);
  CHECK(f.inspector->scratch().used() == 0);
}

SCENARIO("layer name policy rejects invalid queries without widening access",
    "[integration][layer-inspection][layer-filters]")
{
  Fixture f(0);
  PageArgs args;
  args.kind = "layers";
  args.filters.name = std::string(4097, 'x');
  CHECK(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args).code
      == Outcome::INVALID_REQUEST);
  args.filters.name = "default";
  args.filters.objectSubtype = "matte";
  CHECK(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args).code
      == Outcome::INVALID_REQUEST);
  args.filters.objectSubtype.reset();
  f.authority->restrict(
      session::Target::scene(f.sceneId), session::Capability::READ);
  auto denied = f.engine->submit(f.creds,
      {f.creds.sessionId, 1},
      f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args).request);
  CHECK(denied.code == Outcome::FORBIDDEN);
  CHECK(f.inspector->scratch().used() == 0);
}

SCENARIO("layer filters use stored names while sharing representation budgets",
    "[integration][layer-inspection][layer-filters]")
{
  PageLimits limits;
  SECTION("display previews do not become names")
  {
    limits.displayString = 4;
    Fixture f(0, limits);
    f.scene.removeAllLayers();
    f.scene.addLayer("abcd-suffix");
    PageArgs args;
    args.kind = "layers";
    args.filters.name = "abcd";
    CHECK(page(f.inspect(args, 1)).get("items")->a.empty());
    args.filters.name.reset();
    args.filters.nameContains = "fix";
    auto matched = page(f.inspect(args, 2));
    REQUIRE(matched.get("items")->a.size() == 1);
    CHECK(matched.get("items")->a[0].get("name")->get("text")->s == "abcd");
    CHECK(f.inspector->scratch().used() == 0);
  }
  SECTION("filters consume shared scratch even without matches")
  {
    limits.scratchBytes = 128;
    Fixture f(0, limits);
    PageArgs args;
    args.kind = "layers";
    args.filters.name = std::string(4096, 'x');
    CHECK(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args).code
        == Outcome::BUSY);
    CHECK(f.inspector->scratch().used() == 0);
  }
  SECTION("page byte limits preserve filtered continuation")
  {
    limits.pageBytes = 750;
    Fixture f(0, limits);
    f.scene.removeAllLayers();
    f.scene.addLayer("one");
    f.scene.addLayer("two");
    PageArgs args;
    args.kind = "layers";
    args.filters.nameContains = "o";
    auto first = page(f.inspect(args, 1));
    REQUIRE(first.get("items")->a.size() == 1);
    CHECK(first.get("stop")->s == "byte");
    args.cursor = first.get("cursor")->s;
    auto next = page(f.inspect(args, 2));
    REQUIRE(next.get("items")->a.size() == 1);
    CHECK(next.get("items")->a[0].get("name")->get("text")->s == "two");
    CHECK(next.get("complete")->b);
    CHECK(f.inspector->scratch().used() == 0);
  }
  SECTION("oversize layer items release scratch")
  {
    limits.itemBytes = 32;
    Fixture f(0, limits);
    f.scene.addLayer("oversize");
    PageArgs args;
    args.kind = "layers";
    args.filters.nameContains = "";
    CHECK(f.inspect(args, 1).terminal == Outcome::ITEM_TOO_LARGE);
    CHECK(f.inspector->scratch().used() == 0);
  }
  SECTION("stale handles conflict even when names cannot match")
  {
    Fixture f(0, limits);
    f.scene.removeAllLayers();
    f.scene.addLayer("old");
    PageArgs args;
    args.kind = "layers";
    args.filters.handle =
        page(f.inspect(args, 1)).get("items")->a[0].get("handle")->s;
    f.scene.removeLayer("old");
    f.scene.addLayer("old");
    args.filters.name = "absent";
    CHECK(f.inspect(args, 2).terminal == Outcome::CONFLICT);
    CHECK(f.inspector->scratch().used() == 0);
  }
}

SCENARIO("layer and root lifetimes survive notifications but never removal",
    "[integration][layer-inspection]")
{
  Fixture f(0);
  f.scene.removeAllLayers();
  auto *layer = f.scene.addLayer("live");
  PageArgs args;
  args.kind = "layers";
  auto original = page(f.inspect(args, 1));
  const auto handle = original.get("items")->a[0].get("handle")->s;
  const auto root = original.get("items")->a[0].get("root_handle")->s;
  f.scene.insertChildTransformNode(layer->root(), vsr::math::IDENTITY_MAT4);
  auto beforeClear = page(f.inspect(args, 2));
  layer->clear();
  REQUIRE(layer->size() == 1);
  REQUIRE(layer->root());
  auto cleared = page(f.inspect(args, 3));
  CHECK(
      cleared.get("scene_revision")->s != beforeClear.get("scene_revision")->s);
  CHECK(cleared.get("items")->a[0].get("handle")->s == handle);
  CHECK(cleared.get("items")->a[0].get("root_handle")->s == root);
  f.scene.signalLayerStructureChanged(layer);
  f.scene.signalLayerTransformChanged(layer);
  f.scene.setLayerActive("live", false);
  layer->editableName() = "renamed";
  f.scene.signalLayerStructureChanged(layer);
  auto changed = page(f.inspect(args, 4));
  CHECK(changed.get("items")->a[0].get("handle")->s == handle);
  CHECK(changed.get("items")->a[0].get("root_handle")->s == root);
  // Even a retained shared_ptr must not make a removed layer live in the scene.
  auto retained = f.scene.layers().at_index(0).second.ptr;
  f.scene.removeLayer("live");
  f.scene.addLayer("live");
  auto recreated = page(f.inspect(args, 5));
  CHECK(recreated.get("items")->a[0].get("handle")->s != handle);
  CHECK(recreated.get("items")->a[0].get("root_handle")->s != root);
  args.filters.handle = handle;
  CHECK(f.inspect(args, 6).terminal == Outcome::CONFLICT);
  auto replay = f.submit(f.inspector->makeInspection(f.creds.sessionId,
                             f.sceneId,
                             PageArgs{std::nullopt, {}, {}, "layers"}),
      1);
  REQUIRE(replay.replayed);
  CHECK(replay.status.result
      == f.engine->status(f.creds, {f.creds.sessionId, 1}).result);
  args.filters.handle = recreated.get("items")->a[0].get("handle")->s;
  f.scene.removeAllObjects();
  f.scene.addLayer("live");
  CHECK(f.inspect(args, 7).terminal == Outcome::CONFLICT);
}

SCENARIO("layer cursors bind identity and every supported policy",
    "[integration][layer-inspection]")
{
  Fixture f(0);
  f.scene.removeAllLayers();
  auto *layer = f.scene.addLayer("one");
  f.scene.addLayer("two");
  f.sceneIds = std::make_unique<identity::SceneIdentity>(f.ids, f.scene);
  PageArgs args;
  args.kind = "layers";
  args.pageSize = 1;
  auto first = page(f.inspect(args, 1));
  args.cursor = first.get("cursor")->s;
  auto continuation = page(f.inspect(args, 2));
  REQUIRE(continuation.get("items")->a.size() == 1);
  CHECK(continuation.get("items")->a[0].get("name")->get("text")->s == "two");
  auto substituted = args;
  substituted.kind = "objects";
  CHECK(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, substituted)
            .code
      == Outcome::CURSOR_STALE);
  substituted = args;
  substituted.pageSize = 2;
  CHECK(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, substituted)
            .code
      == Outcome::CURSOR_STALE);
  substituted = args;
  substituted.filters.handle = first.get("items")->a[0].get("handle")->s;
  CHECK(f.inspector->makeInspection(f.creds.sessionId, f.sceneId, substituted)
            .code
      == Outcome::CURSOR_STALE);
  SECTION("identity replacement with the same revision count")
  {
    f.sceneIds = std::make_unique<identity::SceneIdentity>(f.ids, f.scene);
    auto built =
        f.inspector->makeInspection(f.creds.sessionId, f.sceneId, args);
    CHECK(built.code == Outcome::CURSOR_STALE);
  }
  SECTION("notifying mutations")
  {
    layer->editableName() = "renamed";
    f.scene.signalLayerStructureChanged(layer);
    CHECK(f.inspect(args, 3).terminal == Outcome::CURSOR_STALE);
  }
  SECTION("root-preserving clear")
  {
    layer->clear();
    CHECK(f.inspect(args, 3).terminal == Outcome::CURSOR_STALE);
  }
  SECTION("activation change-back")
  {
    f.scene.setLayerActive("one", false);
    f.scene.setLayerActive("one", true);
    CHECK(f.inspect(args, 3).terminal == Outcome::CURSOR_STALE);
  }
  SECTION("membership change-back")
  {
    f.scene.removeLayer("two");
    f.scene.addLayer("two");
    CHECK(f.inspect(args, 3).terminal == Outcome::CURSOR_STALE);
  }
}

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
  args.kind = "unsupported";
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
  CHECK(s.result.find(
            "\"inspection\":{\"kinds\":[\"objects\",\"layers\",\"nodes\"],"
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

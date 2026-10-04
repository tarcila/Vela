// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_integration
#include "vsr/integration/identity/Identity.hpp"
// vsr_rendering
#include "vsr/rendering/index/RenderIndex.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// anari
#include <anari/anari_cpp.hpp>
// catch
#include "catch.hpp"
// std
#include <memory>
#include <optional>
#include <vector>

using namespace vsr::integration::identity;
using namespace vsr::scene;

namespace {

struct MembershipDelegate : EmptyUpdateDelegate
{
  void signalLayerStructureUpdated(const Layer *) override;
  void signalLayerRemoved(const Layer *) override;
  void signalObjectRemoved(const Object *) override;
  void signalObjectLayerUseCountZero(const Object *) override;
  void signalUpdateBatchBegin() override;
  void signalUpdateBatchEnd() override;

  int structure{0};
  int removed{0};
  int objectsRemoved{0};
  int zeroUses{0};
  int begin{0};
  int end{0};
};

void MembershipDelegate::signalLayerStructureUpdated(const Layer *)
{
  ++structure;
}

void MembershipDelegate::signalLayerRemoved(const Layer *)
{
  ++removed;
}

void MembershipDelegate::signalObjectRemoved(const Object *)
{
  ++objectsRemoved;
}

void MembershipDelegate::signalObjectLayerUseCountZero(const Object *)
{
  ++zeroUses;
}

void MembershipDelegate::signalUpdateBatchBegin()
{
  ++begin;
}

void MembershipDelegate::signalUpdateBatchEnd()
{
  ++end;
}

// A public RenderIndex consumer: exercise the real base batching driver,
// counting work rather than pretending a new dependency edit is a signal.
struct MembershipIndex : vsr::rendering::RenderIndex
{
  MembershipIndex(Scene &scene, anari::Device device);
  void signalObjectParameterUseCountZero(const Object *) override {}
  void signalObjectLayerUseCountZero(const Object *) override {}
  bool isFlat() const override;
  void signalLayerStructureUpdated(const Layer *) override;

  int work{0};

 protected:
  void updateWorld() override;
};

MembershipIndex::MembershipIndex(Scene &scene, anari::Device device)
    : RenderIndex(scene, "membership-test", device)
{}

bool MembershipIndex::isFlat() const
{
  return false;
}

void MembershipIndex::signalLayerStructureUpdated(const Layer *)
{
  requestWorldUpdate();
}

void MembershipIndex::updateWorld()
{
  ++work;
}

struct MembershipDevice
{
  MembershipDevice();
  ~MembershipDevice();
  VSR_NOT_COPYABLE(MembershipDevice)
  VSR_NOT_MOVEABLE(MembershipDevice)

  anari::Library library{nullptr};
  anari::Device device{nullptr};
};

MembershipDevice::MembershipDevice()
{
  library = anari::loadLibrary("helide", nullptr);
  if (library)
    device = anari::newDevice(library, "default");
}

MembershipDevice::~MembershipDevice()
{
  if (device)
    anari::release(device, device);
  if (library)
    anari::unloadLibrary(library);
}

struct Observations
{
  Observations(SceneIdentity &ids);
  void checkChanged(SceneIdentity &ids, bool inspectionFirst) const;
  void checkStable(SceneIdentity &ids, bool inspectionFirst) const;

  SceneRevision scene;
  InspectionRevision inspection;
};

Observations::Observations(SceneIdentity &ids)
    : scene(ids.sceneRevision()), inspection(ids.inspectionRevision())
{}

void Observations::checkChanged(SceneIdentity &ids, bool inspectionFirst) const
{
  if (inspectionFirst) {
    CHECK(ids.inspectionRevision() != inspection);
    CHECK(ids.sceneRevision() != scene);
  } else {
    CHECK(ids.sceneRevision() != scene);
    CHECK(ids.inspectionRevision() != inspection);
  }
  Observations current(ids);
  current.checkStable(ids, !inspectionFirst);
}

void Observations::checkStable(SceneIdentity &ids, bool inspectionFirst) const
{
  for (int i = 0; i < 3; ++i) {
    if (inspectionFirst) {
      CHECK(ids.inspectionRevision() == inspection);
      CHECK(ids.sceneRevision() == scene);
    } else {
      CHECK(ids.sceneRevision() == scene);
      CHECK(ids.inspectionRevision() == inspection);
    }
  }
}

} // namespace

SCENARIO(
    "public insertion attaches actual members and invalidates observations",
    "[integration][membership-validity]")
{
  const bool inspectionFirst = GENERATE(false, true);
  Scene scene;
  Scene other;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  SceneIdentity secondIds(authority, scene);
  SceneIdentity otherIds(authority, other);
  auto *layer = scene.defaultLayer();
  auto root = layer->root();
  auto *forest = root->container();
  auto object = scene.createObject<Light>("directional");
  auto array = scene.createArray(ANARI_FLOAT32_MAT4, 2);
  auto *delegate = scene.updateDelegate().emplace<MembershipDelegate>();
  const auto rootHandle = ids.handleOf(root);
  const auto rootLifetime = root->lifetime();
  const auto rootTopology = root->topologyVersion();
  const auto rootEdit = root->value().editVersion();
  const auto rootTransform = root->value().transformVersion();
  auto observeRoot = [&]() {
    auto walk = ids.beginNodeDependencies(rootHandle);
    REQUIRE(walk);
    while (walk->ancestorSlot)
      REQUIRE(ids.advanceNodeDependencies(*walk));
    return *walk;
  };
  const auto rootDependencies = observeRoot();
  Observations before(ids);
  Observations secondBefore(secondIds);
  Observations unrelated(otherIds);
  LayerNodeRef inserted;
  int expectedSignals = 0;
  int expectedObjectUses = 0;
  int expectedArrayUses = 0;

  SECTION("Scene empty helper with default name")
  {
    inserted = scene.insertChildNode(root);
  }
  SECTION("Scene empty helper with name")
  {
    inserted = scene.insertChildNode(root, "empty");
    CHECK(inserted->value().name() == "empty");
  }
  SECTION("Scene default transform helper")
  {
    inserted = scene.insertChildTransformNode(root);
    expectedSignals = 1;
    CHECK(inserted->value().getTransform() == vsr::math::IDENTITY_MAT4);
  }
  SECTION("Scene explicit transform helper")
  {
    inserted = scene.insertChildTransformNode(root,
        vsr::math::translation_matrix(vsr::math::float3(3.f, 4.f, 5.f)),
        "transform");
    expectedSignals = 1;
    CHECK(inserted->value().getTransform()[3].x == 3.f);
    CHECK(inserted->value().name() == "transform");
  }
  SECTION("Scene transform array helper")
  {
    const bool named = GENERATE(false, true);
    inserted = named
        ? scene.insertChildTransformArrayNode(root, array.data(), "instances")
        : scene.insertChildTransformArrayNode(root, array.data());
    CHECK(inserted->value().name() == (named ? "instances" : ""));
    expectedSignals = 1;
    expectedArrayUses = 1;
    CHECK(inserted->value().getObject() == array.data());
  }
  SECTION("Scene typed object helper")
  {
    const bool named = GENERATE(false, true);
    inserted = named ? scene.insertChildObjectNode(root, object, "light")
                     : scene.insertChildObjectNode(root, object);
    CHECK(inserted->value().name() == (named ? "light" : ""));
    expectedSignals = 1;
    expectedObjectUses = 1;
    CHECK(inserted->value().getObject() == object.data());
  }
  SECTION("Scene indexed object helper")
  {
    const bool named = GENERATE(false, true);
    inserted = named
        ? scene.insertChildObjectNode(
              root, ANARI_LIGHT, object.index(), "indexed")
        : scene.insertChildObjectNode(root, ANARI_LIGHT, object.index());
    CHECK(inserted->value().name() == (named ? "indexed" : ""));
    expectedSignals = 1;
    expectedObjectUses = 1;
  }
  SECTION("Scene create and insert helper")
  {
    const bool named = GENERATE(false, true);
    auto added = named
        ? scene.insertNewChildObjectNode<Light>(root, "point", "new")
        : scene.insertNewChildObjectNode<Light>(root, "point");
    inserted = added.first;
    expectedSignals = 1;
    CHECK(added.second->useCount(Object::UseKind::LAYER) == 1);
    CHECK(inserted->value().getObject() == added.second.data());
    CHECK(inserted->value().name() == (named ? "new" : ""));
  }
  SECTION("ForestNode first child")
  {
    inserted = root->insert_first_child(LayerNodeData(layer, object));
    expectedObjectUses = 1;
  }
  SECTION("ForestNode last child")
  {
    inserted = root->insert_last_child(LayerNodeData(layer, object));
    expectedObjectUses = 1;
  }
  SECTION("Forest first child binds a value without a Layer")
  {
    inserted =
        forest->insert_first_child(root, LayerNodeData(nullptr, "first"));
  }
  SECTION("Forest last child binds a value without a Layer")
  {
    inserted = forest->insert_last_child(root, LayerNodeData(nullptr, "last"));
  }
  SECTION("ForestNode chosen next slot")
  {
    inserted = root->insert_last_child_at(1, LayerNodeData(layer, object));
    expectedObjectUses = 1;
    CHECK(inserted.index() == 1);
  }
  SECTION("Forest chosen next slot")
  {
    inserted =
        forest->insert_last_child_at(root, 1, LayerNodeData(layer, object));
    expectedObjectUses = 1;
    CHECK(inserted.index() == 1);
  }
  SECTION("ForestNode chosen higher slot leaves gaps unattached")
  {
    inserted = root->insert_last_child_at(8, LayerNodeData(layer, object));
    expectedObjectUses = 1;
    CHECK(inserted.index() == 8);
    for (size_t i = 1; i < 8; ++i)
      CHECK_FALSE(layer->at(i));
  }
  SECTION("Forest chosen higher slot leaves gaps unattached")
  {
    inserted =
        forest->insert_last_child_at(root, 8, LayerNodeData(layer, object));
    expectedObjectUses = 1;
    CHECK(inserted.index() == 8);
    for (size_t i = 1; i < 8; ++i)
      CHECK_FALSE(layer->at(i));
  }

  REQUIRE(inserted);
  CHECK(layer->size() == 2);
  CHECK(root->next() == inserted);
  CHECK(inserted->parent() == root);
  CHECK(inserted->sibling() == root);
  CHECK(inserted->value().layer() == layer);
  CHECK(inserted->lifetime() != rootLifetime);
  CHECK(ids.handleOf(inserted) != rootHandle);
  CHECK(ids.handleOf(root) == rootHandle);
  CHECK(root->lifetime() == rootLifetime);
  CHECK(root->topologyVersion() == rootTopology);
  CHECK(root->value().editVersion() == rootEdit);
  CHECK(root->value().transformVersion() == rootTransform);
  CHECK(rootDependencies.matchesStructure(observeRoot()));
  CHECK(rootDependencies.matchesAncestry(observeRoot()));
  CHECK(object->useCount(Object::UseKind::LAYER) == expectedObjectUses);
  CHECK(array->useCount(Object::UseKind::LAYER) == expectedArrayUses);
  CHECK(delegate->structure == expectedSignals);
  CHECK(delegate->zeroUses == 0);
  before.checkChanged(ids, inspectionFirst);
  secondBefore.checkChanged(secondIds, !inspectionFirst);
  unrelated.checkStable(otherIds, inspectionFirst);

  Observations after(ids);
  inserted->value().setEnabled(false);
  after.checkChanged(ids, !inspectionFirst);
  CHECK(delegate->structure == expectedSignals);
}

SCENARIO("rejected insertion and storage allocation are not membership edits",
    "[integration][membership-validity][membership-ownership]")
{
  const bool inspectionFirst = GENERATE(false, true);
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto *layer = scene.defaultLayer();
  auto root = layer->root();
  auto *forest = root->container();
  auto object = scene.createObject<Light>("directional");
  auto occupied = root->insert_last_child(LayerNodeData(layer, object));
  auto *delegate = scene.updateDelegate().emplace<MembershipDelegate>();
  const auto handle = ids.handleOf(occupied);
  const auto lifetime = occupied->lifetime();
  const auto edit = occupied->value().editVersion();
  const auto topology = occupied->topologyVersion();
  Observations before(ids);

  SECTION("occupied node slot")
  {
    CHECK_FALSE(root->insert_last_child_at(
        occupied.index(), LayerNodeData(layer, object)));
  }
  SECTION("occupied Forest slot")
  {
    CHECK_FALSE(forest->insert_last_child_at(
        root, occupied.index(), LayerNodeData(layer, object)));
  }
  SECTION("root slot is occupied")
  {
    CHECK_FALSE(root->insert_last_child_at(0, LayerNodeData(layer, object)));
  }
  SECTION("invalid node slot")
  {
    CHECK_FALSE(root->insert_last_child_at(
        VSR_INVALID_INDEX, LayerNodeData(layer, object)));
  }
  SECTION("invalid Forest slot")
  {
    CHECK_FALSE(forest->insert_last_child_at(
        root, VSR_INVALID_INDEX, LayerNodeData(layer, object)));
  }
  SECTION("rejected object lookup")
  {
    CHECK_FALSE(
        scene.insertChildObjectNode(root, ANARI_LIGHT, VSR_INVALID_INDEX));
  }
  SECTION("rejected object type")
  {
    CHECK_FALSE(
        scene.insertChildObjectNode(root, ANARI_GEOMETRY, object.index()));
  }
  SECTION("reserve relocates attached values without inventing edits")
  {
    forest->reserve(1024);
    CHECK(forest->capacity() == 2);
  }
  SECTION("detached insertion-like temporaries are not members")
  {
    LayerNodeData value(layer, object);
    LayerNodeData copy(value);
    CHECK(object->useCount(Object::UseKind::LAYER) == 3);
    value.setEnabled(false);
    copy.setEmpty();
    CHECK(object->useCount(Object::UseKind::LAYER) == 2);
  }

  CHECK(layer->size() == 2);
  CHECK(layer->capacity() == 2);
  CHECK(root->next() == occupied);
  CHECK(ids.handleOf(occupied) == handle);
  CHECK(occupied->lifetime() == lifetime);
  CHECK(occupied->topologyVersion() == topology);
  CHECK(occupied->value().editVersion() == edit);
  CHECK(object->useCount(Object::UseKind::LAYER) == 1);
  CHECK(delegate->structure == 0);
  CHECK(delegate->zeroUses == 0);
  before.checkStable(ids, inspectionFirst);
}

SCENARIO("gap filling and erased slot reuse get fresh membership identity",
    "[integration][membership-validity][membership-ownership]")
{
  const bool inspectionFirst = GENERATE(false, true);
  const bool chosenReuse = GENERATE(false, true);
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto *layer = scene.defaultLayer();
  auto root = layer->root();
  auto object = scene.createObject<Light>("directional");
  auto original = root->insert_last_child_at(8, LayerNodeData(layer, object));
  const auto oldHandle = ids.handleOf(original);
  const auto oldVersion = ids.nodeEditVersion(oldHandle);
  const auto oldLifetime = original->lifetime();
  const auto oldTopology = original->topologyVersion();
  REQUIRE(oldVersion);
  CHECK(layer->size() == 2);
  CHECK(layer->capacity() == 9);
  CHECK(object->useCount(Object::UseKind::LAYER) == 1);
  auto *delegate = scene.updateDelegate().emplace<MembershipDelegate>();

  Observations beforeFill(ids);
  auto first = root->insert_first_child(LayerNodeData(layer, "first"));
  CHECK(first.index() == 7);
  CHECK(layer->size() == 3);
  CHECK(root->next() == first);
  CHECK(first->sibling() == original);
  CHECK(original->sibling() == root);
  for (size_t i = 1; i < 7; ++i)
    CHECK_FALSE(layer->at(i));
  beforeFill.checkChanged(ids, inspectionFirst);

  Observations beforeReuse(ids);
  original->erase_self();
  CHECK_FALSE(layer->at(8));
  CHECK(object->useCount(Object::UseKind::LAYER) == 0);
  LayerNodeRef replacement;
  if (chosenReuse)
    replacement = root->insert_last_child_at(8, LayerNodeData(layer, object));
  else
    replacement = root->insert_last_child(LayerNodeData(layer, object));
  REQUIRE(replacement);
  CHECK(replacement.index() == 8);
  CHECK(replacement->lifetime() != oldLifetime);
  CHECK(replacement->topologyVersion() > oldTopology);
  CHECK(ids.handleOf(replacement) != oldHandle);
  CHECK_FALSE(ids.resolve(oldHandle));
  CHECK_FALSE(ids.isCurrent(*oldVersion));
  CHECK(layer->size() == 3);
  CHECK(object->useCount(Object::UseKind::LAYER) == 1);
  CHECK(first->sibling() == replacement);
  CHECK(replacement->parent() == root);
  CHECK(replacement->value().layer() == layer);
  beforeReuse.checkChanged(ids, !inspectionFirst);
  CHECK(delegate->structure == 0);
  CHECK(delegate->zeroUses == 1);
  Observations attached(ids);
  replacement->value().setEnabled(false);
  attached.checkChanged(ids, inspectionFirst);
}

SCENARIO(
    "public destruction invalidates actual membership and preserves survivors",
    "[integration][membership-validity][membership-ownership]")
{
  const bool inspectionFirst = GENERATE(false, true);
  Scene scene;
  Scene other;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  SceneIdentity otherIds(authority, other);
  auto *layer = scene.defaultLayer();
  auto root = layer->root();
  auto *forest = root->container();
  auto object = scene.createObject<Light>("directional");
  auto parent = scene.insertChildNode(root, "parent");
  auto leaf = scene.insertChildObjectNode(parent, object);
  auto branch = scene.insertChildNode(parent, "branch");
  auto grandchild = scene.insertChildObjectNode(branch, object);
  auto sibling = scene.insertChildObjectNode(root, object);
  auto detached = std::make_unique<LayerNodeData>(grandchild->value());
  REQUIRE(layer->size() == 6);
  REQUIRE(object->useCount(Object::UseKind::LAYER) == 4);
  const auto rootHandle = ids.handleOf(root);
  const auto parentHandle = ids.handleOf(parent);
  const auto leafHandle = ids.handleOf(leaf);
  const auto grandchildHandle = ids.handleOf(grandchild);
  const auto siblingHandle = ids.handleOf(sibling);
  const auto rootEdit = ids.nodeEditVersion(rootHandle);
  const auto parentEdit = ids.nodeEditVersion(parentHandle);
  const auto rootTopology = root->topologyVersion();
  const auto parentTopology = parent->topologyVersion();
  auto observe = [&](const NodeHandle &handle) {
    auto walk = ids.beginNodeDependencies(handle);
    REQUIRE(walk);
    while (walk->ancestorSlot)
      REQUIRE(ids.advanceNodeDependencies(*walk));
    return *walk;
  };
  const auto rootDependencies = observe(rootHandle);
  const auto parentDependencies = observe(parentHandle);
  const auto siblingDependencies = observe(siblingHandle);
  const auto grandchildDependencies = observe(grandchildHandle);
  auto *delegate = scene.updateDelegate().emplace<MembershipDelegate>();
  Observations before(ids);
  Observations unrelated(otherIds);
  size_t expectedSize = 5;
  size_t expectedUses = 3;
  bool parentSurvives = true;
  bool grandchildSurvives = true;
  bool siblingSurvives = true;
  int expectedSignals = 0;

  SECTION("Forest leaf erase")
  {
    forest->erase(leaf);
  }
  SECTION("ForestNode leaf erase self")
  {
    leaf->erase_self();
  }
  SECTION("Layer leaf erase")
  {
    layer->erase(leaf);
  }
  SECTION("Scene leaf remove")
  {
    scene.removeNode(leaf);
    expectedSignals = 1;
  }
  SECTION("nonleaf erase via every public boundary")
  {
    const auto boundary = GENERATE(0, 1, 2, 3);
    if (boundary == 0)
      forest->erase(parent);
    else if (boundary == 1)
      parent->erase_self();
    else if (boundary == 2)
      layer->erase(parent);
    else {
      scene.removeNode(parent);
      expectedSignals = 1;
    }
    expectedSize = 2;
    expectedUses = 2;
    parentSurvives = false;
    grandchildSurvives = false;
  }
  SECTION("clear descendants but preserve subtree root")
  {
    const bool nodeBoundary = GENERATE(false, true);
    if (nodeBoundary)
      parent->erase_subtree();
    else
      forest->erase_subtree(parent);
    expectedSize = 3;
    expectedUses = 2;
    grandchildSurvives = false;
  }
  SECTION("whole hierarchy destruction preserves its root")
  {
    const auto boundary = GENERATE(0, 1, 2, 3, 4, 5, 6);
    if (boundary == 0)
      forest->erase(root);
    else if (boundary == 1)
      root->erase_self();
    else if (boundary == 2)
      root->erase_subtree();
    else if (boundary == 3)
      forest->erase_subtree(root);
    else if (boundary == 4)
      forest->clear();
    else if (boundary == 5) {
      layer->clear();
      expectedSignals = 1;
    } else
      layer->erase(root);
    expectedSize = 1;
    expectedUses = 1;
    parentSurvives = false;
    grandchildSurvives = false;
    siblingSurvives = false;
  }

  CHECK(layer->size() == expectedSize);
  CHECK(object->useCount(Object::UseKind::LAYER) == expectedUses);
  CHECK_FALSE(ids.resolve(leafHandle));
  CHECK(bool(ids.resolve(parentHandle)) == parentSurvives);
  CHECK(bool(ids.resolve(grandchildHandle)) == grandchildSurvives);
  CHECK(bool(ids.resolve(siblingHandle)) == siblingSurvives);
  CHECK(ids.handleOf(root) == rootHandle);
  CHECK(root->topologyVersion() == rootTopology);
  CHECK(rootDependencies.matchesStructure(observe(rootHandle)));
  CHECK(rootDependencies.matchesAncestry(observe(rootHandle)));
  if (siblingSurvives) {
    CHECK(siblingDependencies.matchesStructure(observe(siblingHandle)));
    CHECK(siblingDependencies.matchesAncestry(observe(siblingHandle)));
  }
  if (grandchildSurvives) {
    CHECK(grandchildDependencies.matchesStructure(observe(grandchildHandle)));
    CHECK(grandchildDependencies.matchesAncestry(observe(grandchildHandle)));
  }
  REQUIRE(rootEdit);
  CHECK(ids.isCurrent(*rootEdit));
  if (parentSurvives) {
    REQUIRE(parentEdit);
    CHECK(ids.isCurrent(*parentEdit));
    CHECK(parent->topologyVersion() == parentTopology);
    CHECK(parentDependencies.matchesStructure(observe(parentHandle)));
    CHECK(parentDependencies.matchesAncestry(observe(parentHandle)));
  }
  if (!siblingSurvives) {
    CHECK(root->isRoot());
    CHECK(root->isLeaf());
    CHECK(layer->empty());
  }
  CHECK(detached->getObject() == object.data());
  detached.reset();
  CHECK(object->useCount(Object::UseKind::LAYER) == expectedUses - 1);
  CHECK(delegate->structure == expectedSignals);
  CHECK(delegate->zeroUses == (expectedUses == 1 ? 1 : 0));
  before.checkChanged(ids, inspectionFirst);
  unrelated.checkStable(otherIds, !inspectionFirst);
}

SCENARIO(
    "empty destruction and root restrictions keep their established signals",
    "[integration][membership-validity]")
{
  const bool inspectionFirst = GENERATE(false, true);
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto *layer = scene.defaultLayer();
  auto root = layer->root();
  auto *forest = root->container();
  auto *delegate = scene.updateDelegate().emplace<MembershipDelegate>();
  Observations before(ids);
  bool explicitSignal = false;
  size_t expectedSize = 1;

  SECTION("empty Forest clear")
  {
    forest->clear();
  }
  SECTION("empty Forest root erase")
  {
    forest->erase(root);
  }
  SECTION("empty Forest root subtree clear")
  {
    forest->erase_subtree(root);
  }
  SECTION("empty ForestNode root erase")
  {
    root->erase_self();
  }
  SECTION("empty ForestNode root subtree clear")
  {
    root->erase_subtree();
  }
  SECTION("empty Layer root erase")
  {
    layer->erase(root);
  }
  SECTION("empty Layer clear still explicitly signals")
  {
    layer->clear();
    explicitSignal = true;
  }
  SECTION("Scene root removal is rejected even with descendants")
  {
    scene.insertChildNode(root);
    before = Observations(ids);
    scene.removeNode(root);
    scene.removeNode(root, true);
    expectedSize = 2;
  }
  SECTION("leaf subtree clear is a no-op")
  {
    auto leaf = scene.insertChildNode(root);
    before = Observations(ids);
    leaf->erase_subtree();
    forest->erase_subtree(leaf);
    expectedSize = 2;
  }
  SECTION("missing Layer name removal")
  {
    scene.removeLayer("absent");
  }
  SECTION("foreign Layer pointer removal")
  {
    Scene other;
    scene.removeLayer(other.defaultLayer());
  }
  SECTION("explicit structural signal without a membership edit")
  {
    scene.signalLayerStructureChanged(layer);
    explicitSignal = true;
  }

  CHECK(layer->size() == expectedSize);
  CHECK(layer->root() == root);
  CHECK(root->isRoot());
  CHECK(delegate->structure == (explicitSignal ? 1 : 0));
  CHECK(delegate->removed == 0);
  if (explicitSignal)
    before.checkChanged(ids, inspectionFirst);
  else
    before.checkStable(ids, inspectionFirst);
}

SCENARIO(
    "removed retained Layers resolve objects but cannot rejoin dependency tracking",
    "[integration][membership-validity][membership-ownership]")
{
  const bool inspectionFirst = GENERATE(false, true);
  const auto removal = GENERATE(0, 1, 2);
  Scene scene;
  Scene other;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  SceneIdentity otherIds(authority, other);
  auto *layer = scene.defaultLayer();
  auto retained = scene.layers().begin()->second.ptr;
  auto root = layer->root();
  auto object = scene.createObject<Light>("directional");
  auto node = scene.insertChildObjectNode(root, object);
  auto detached = std::make_unique<LayerNodeData>(node->value());
  const auto layerHandle = ids.handleOf(layer);
  const auto rootHandle = ids.handleOf(root);
  const auto nodeHandle = ids.handleOf(node);
  const auto rootLifetime = root->lifetime();
  const auto nodeLifetime = node->lifetime();
  auto *delegate = scene.updateDelegate().emplace<MembershipDelegate>();
  Observations before(ids);
  Observations unrelated(otherIds);
  if (removal == 0)
    scene.removeLayer("default");
  else if (removal == 1)
    scene.removeLayer(layer);
  else
    scene.removeAllLayers();
  CHECK(scene.layers().empty());
  CHECK(delegate->removed == 1);
  CHECK(delegate->structure == 0);
  CHECK_FALSE(ids.resolve(layerHandle));
  CHECK_FALSE(ids.resolve(rootHandle));
  CHECK_FALSE(ids.resolve(nodeHandle));
  CHECK_FALSE(ids.handleOf(layer).valid());
  CHECK(root->lifetime() == rootLifetime);
  CHECK(node->lifetime() == nodeLifetime);
  CHECK(node->value().getObject() == object.data());
  CHECK(detached->getObject() == object.data());
  CHECK(object->useCount(Object::UseKind::LAYER) == 2);
  before.checkChanged(ids, inspectionFirst);
  unrelated.checkStable(otherIds, !inspectionFirst);

  Observations removed(ids);
  auto added = root->insert_last_child_at(8, LayerNodeData(layer, object));
  CHECK(layer->size() == 3);
  CHECK(object->useCount(Object::UseKind::LAYER) == 3);
  CHECK(added->value().layer() == layer);
  node->value().setAsTransform(vsr::math::IDENTITY_MAT4);
  added->erase_self();
  CHECK(object->useCount(Object::UseKind::LAYER) == 1);
  root->container()->reserve(1024);
  root->container()->clear();
  CHECK(layer->size() == 1);
  removed.checkStable(ids, inspectionFirst);
  CHECK(delegate->removed == 1);
  CHECK(delegate->structure == 0);

  auto *replacementLayer = scene.addLayer("default");
  auto replacement = scene.insertChildNode(replacementLayer->root());
  CHECK(ids.handleOf(replacementLayer) != layerHandle);
  CHECK(ids.handleOf(replacement) != nodeHandle);
  CHECK_FALSE(ids.resolve(layerHandle));
  CHECK_FALSE(ids.resolve(nodeHandle));
  Observations replacementBefore(ids);
  root->insert_last_child(LayerNodeData(layer, "still removed"));
  root->value().setEnabled(false);
  replacementBefore.checkStable(ids, inspectionFirst);
  replacement->value().setEnabled(false);
  replacementBefore.checkChanged(ids, !inspectionFirst);

  retained.reset();
  CHECK(detached->layer() == nullptr);
  CHECK(detached->getObject() == object.data());
  CHECK(object->useCount(Object::UseKind::LAYER) == 1);
  detached.reset();
  CHECK(object->useCount(Object::UseKind::LAYER) == 0);
}

SCENARIO(
    "membership append relocation keeps existing identities and exact ownership",
    "[integration][membership-validity][membership-ownership]")
{
  const bool inspectionFirst = GENERATE(false, true);
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto *layer = scene.defaultLayer();
  auto root = layer->root();
  auto object = scene.createObject<Light>("directional");
  auto middle = scene.insertChildObjectNode(root, object, "middle");
  const auto handle = ids.handleOf(middle);
  const auto lifetime = middle->lifetime();
  const auto topology = middle->topologyVersion();
  const auto edit = ids.nodeEditVersion(handle);
  REQUIRE(edit);
  auto *delegate = scene.updateDelegate().emplace<MembershipDelegate>();
  Observations before(ids);

  auto last = root->insert_last_child_at(64, LayerNodeData(layer, object));
  auto first = root->insert_first_child(LayerNodeData(layer, "first"));
  CHECK(first.index() == 63);
  auto final = root->insert_last_child(LayerNodeData(layer, "final"));
  CHECK(final.index() == 62);
  CHECK(layer->size() == 5);
  CHECK(root->next() == first);
  CHECK(first->sibling() == middle);
  CHECK(middle->sibling() == last);
  CHECK(last->sibling() == final);
  CHECK(final->sibling() == root);
  CHECK(ids.handleOf(middle) == handle);
  CHECK(middle->lifetime() == lifetime);
  CHECK(middle->topologyVersion() == topology);
  CHECK(ids.isCurrent(*edit));
  CHECK(middle->value().getObject() == object.data());
  CHECK(last->value().getObject() == object.data());
  CHECK(object->useCount(Object::UseKind::LAYER) == 2);
  CHECK(delegate->structure == 0);
  before.checkChanged(ids, inspectionFirst);

  Observations reserve(ids);
  root->container()->reserve(2048);
  reserve.checkStable(ids, !inspectionFirst);
  CHECK(ids.isCurrent(*edit));
  CHECK(object->useCount(Object::UseKind::LAYER) == 2);
  middle->value().setEnabled(false);
  reserve.checkChanged(ids, inspectionFirst);
  CHECK(delegate->structure == 0);
}

SCENARIO("gaps failure reuse and root clear survive actual Scene teardown",
    "[integration][membership-validity][membership-ownership]")
{
  std::optional<Scene> scene(std::in_place);
  auto *layer = scene->defaultLayer();
  auto retained = scene->layers().begin()->second.ptr;
  auto root = layer->root();
  auto object = scene->createObject<Light>("directional");
  const auto objectIndex = object.index();
  root->value().setAsObject(object.data());
  auto node = root->insert_last_child_at(32, LayerNodeData(layer, object));
  auto detached = std::make_unique<LayerNodeData>(node->value());
  REQUIRE(object->useCount(Object::UseKind::LAYER) == 3);
  {
    IdentityAuthority authority;
    SceneIdentity ids(authority, *scene);
    const auto handle = ids.handleOf(node);
    const auto lifetime = node->lifetime();
    const auto rootHandle = ids.handleOf(root);
    const auto rootEdit = ids.nodeEditVersion(rootHandle);
    REQUIRE(rootEdit);
    Observations before(ids);
    CHECK_FALSE(root->insert_last_child_at(32, LayerNodeData(layer, object)));
    CHECK_FALSE(root->insert_last_child_at(
        VSR_INVALID_INDEX, LayerNodeData(layer, object)));
    CHECK(object->useCount(Object::UseKind::LAYER) == 3);
    before.checkStable(ids, true);
    root->erase_subtree();
    CHECK(layer->size() == 1);
    CHECK(ids.handleOf(root) == rootHandle);
    CHECK(ids.isCurrent(*rootEdit));
    CHECK(root->value().getObject() == object.data());
    CHECK(object->useCount(Object::UseKind::LAYER) == 2);
    before.checkChanged(ids, false);
    auto reused = root->insert_last_child_at(32, LayerNodeData(layer, object));
    CHECK(reused->lifetime() != lifetime);
    CHECK_FALSE(ids.resolve(handle));
    CHECK(object->useCount(Object::UseKind::LAYER) == 3);
    root->container()->reserve(4096);
    CHECK(object->useCount(Object::UseKind::LAYER) == 3);
  }
  // SceneIdentity obeys its normal owner lifetime contract, unlike survivors.
  scene.reset();
  CHECK(retained->scene() == nullptr);
  CHECK(root->value().getObject() == nullptr);
  CHECK(detached->getObject() == nullptr);
  CHECK(retained->at(32)->value().getObject() == nullptr);
  LayerNodeData expired(layer);
  expired.setAsObject(ANARI_LIGHT, objectIndex);
  root->insert_last_child_at(64, std::move(expired));
  root->container()->clear();
  CHECK(root->value().getObject() == nullptr);
  CHECK(retained->size() == 1);
  detached->setEnabled(false);
  scene.emplace();
  auto replacement = scene->createObject<Light>("directional");
  CHECK(replacement.index() == objectIndex);
  CHECK(root->value().getObject() == nullptr);
  CHECK(detached->getObject() == nullptr);
  CHECK(replacement->useCount(Object::UseKind::LAYER) == 0);
  retained.reset();
  CHECK(detached->layer() == nullptr);
  detached.reset();
  CHECK(replacement->useCount(Object::UseKind::LAYER) == 0);
}

SCENARIO("mixed membership batches preserve exact structural callback delivery",
    "[integration][membership-validity]")
{
  const bool inspectionFirst = GENERATE(false, true);
  const auto batching = GENERATE(0, 1, 2, 3);
  const bool structuralBatch = batching == 1 || batching == 3;
  const bool updateBatch = batching == 2 || batching == 3;
  MembershipDevice device;
  REQUIRE(device.device);
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto *layer = scene.defaultLayer();
  auto root = layer->root();
  auto *delegate = scene.updateDelegate().emplace<MembershipDelegate>();
  auto *index =
      scene.updateDelegate().emplace<MembershipIndex>(scene, device.device);
  if (structuralBatch)
    scene.beginLayerEditBatch();
  if (updateBatch) {
    scene.beginUpdateBatch();
    scene.beginUpdateBatch();
  }
  Observations before(ids);
  auto empty = scene.insertChildNode(root);
  before.checkChanged(ids, inspectionFirst);
  CHECK(delegate->structure == 0);
  auto transform = scene.insertChildTransformNode(root);
  scene.insertChildTransformNode(transform);
  auto direct = root->insert_first_child(LayerNodeData(layer, "direct"));
  direct->erase_self();
  scene.removeNode(empty);
  CHECK(layer->size() == 3);
  CHECK(delegate->structure == (structuralBatch ? 0 : 3));
  Observations destructive(ids);
  root->container()->clear();
  CHECK(layer->empty());
  destructive.checkChanged(ids, !inspectionFirst);
  CHECK(delegate->structure == (structuralBatch ? 0 : 3));
  layer->clear(); // Existing signal is preserved even though this is empty.
  CHECK(delegate->structure == (structuralBatch ? 0 : 4));
  CHECK(index->work == (structuralBatch || updateBatch ? 0 : 4));
  if (structuralBatch)
    scene.endLayerEditBatch();
  CHECK(delegate->structure == (structuralBatch ? 1 : 4));
  CHECK(delegate->begin == (updateBatch ? 2 : 0));
  CHECK(delegate->end == 0);
  if (updateBatch) {
    scene.endUpdateBatch();
    CHECK(index->work == 0);
    scene.endUpdateBatch();
  }
  CHECK(index->work == (structuralBatch || updateBatch ? 1 : 4));
  CHECK(delegate->end == (updateBatch ? 2 : 0));
  Observations done(ids);
  done.checkStable(ids, inspectionFirst);
}

SCENARIO("membership changed away and back cannot revive an old observation",
    "[integration][membership-validity]")
{
  const bool inspectionFirst = GENERATE(false, true);
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto *layer = scene.defaultLayer();
  auto root = layer->root();
  const auto rootHandle = ids.handleOf(root);
  const auto rootEdit = ids.nodeEditVersion(rootHandle);
  const auto topology = root->topologyVersion();
  auto *delegate = scene.updateDelegate().emplace<MembershipDelegate>();
  Observations before(ids);
  auto transient = root->insert_last_child(LayerNodeData(layer, "temporary"));
  transient->erase_self();
  CHECK(layer->size() == 1);
  CHECK(root->isLeaf());
  CHECK(ids.handleOf(root) == rootHandle);
  REQUIRE(rootEdit);
  CHECK(ids.isCurrent(*rootEdit));
  CHECK(root->topologyVersion() == topology);
  CHECK(delegate->structure == 0);
  before.checkChanged(ids, inspectionFirst);
}

SCENARIO(
    "Scene removal with referenced object deletion preserves surviving members",
    "[integration][membership-validity][membership-ownership]")
{
  const bool inspectionFirst = GENERATE(false, true);
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto *layer = scene.defaultLayer();
  auto root = layer->root();
  auto first = scene.createObject<Light>("directional");
  auto second = scene.createObject<Light>("point");
  auto survivor = scene.createObject<Light>("directional");
  auto parent = scene.insertChildNode(root);
  auto leaf = scene.insertChildObjectNode(parent, first);
  auto branch = scene.insertChildTransformNode(parent);
  auto grandchild = scene.insertChildObjectNode(branch, second);
  auto sibling = scene.insertChildObjectNode(root, first);
  auto survivingNode = scene.insertChildObjectNode(root, survivor);
  CHECK(first->useCount(Object::UseKind::LAYER) == 2);
  CHECK(second->useCount(Object::UseKind::LAYER) == 1);
  CHECK(survivor->useCount(Object::UseKind::LAYER) == 1);
  const auto parentHandle = ids.handleOf(parent);
  const auto leafHandle = ids.handleOf(leaf);
  const auto grandchildHandle = ids.handleOf(grandchild);
  const auto siblingHandle = ids.handleOf(sibling);
  const auto survivorHandle = ids.handleOf(survivingNode);
  const auto survivorEdit = ids.nodeEditVersion(survivorHandle);
  const auto survivorTopology = survivingNode->topologyVersion();
  auto *delegate = scene.updateDelegate().emplace<MembershipDelegate>();
  Observations before(ids);
  scene.removeNode(parent, true);
  CHECK(layer->size() == 2);
  CHECK(scene.objectDB().light.size() == 1);
  CHECK_FALSE(ids.resolve(parentHandle));
  CHECK_FALSE(ids.resolve(leafHandle));
  CHECK_FALSE(ids.resolve(grandchildHandle));
  CHECK_FALSE(ids.resolve(siblingHandle));
  CHECK(ids.resolve(survivorHandle) == survivingNode);
  REQUIRE(survivorEdit);
  CHECK(ids.isCurrent(*survivorEdit));
  CHECK(survivingNode->topologyVersion() == survivorTopology);
  CHECK(survivor->useCount(Object::UseKind::LAYER) == 1);
  CHECK(root->next() == survivingNode);
  CHECK(survivingNode->sibling() == root);
  CHECK(delegate->structure == 3);
  CHECK(delegate->objectsRemoved == 2);
  CHECK(delegate->zeroUses == 2);
  CHECK(delegate->removed == 0);
  before.checkChanged(ids, inspectionFirst);
}

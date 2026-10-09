// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_integration
#include "vsr/integration/identity/Identity.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// catch
#include "catch.hpp"
// std
#include <memory>
#include <optional>
#include <stdexcept>

using namespace vsr::integration::identity;
using namespace vsr::scene;

namespace {

struct TransferDelegate : EmptyUpdateDelegate
{
  void signalLayerStructureUpdated(const Layer *) override;
  void signalLayerTransformUpdated(const Layer *) override;
  void signalUpdateBatchBegin() override;
  void signalUpdateBatchEnd() override;

  int structure{0};
  int transform{0};
  int begin{0};
  int end{0};
};

void TransferDelegate::signalLayerStructureUpdated(const Layer *)
{
  ++structure;
}

void TransferDelegate::signalLayerTransformUpdated(const Layer *)
{
  ++transform;
}

void TransferDelegate::signalUpdateBatchBegin()
{
  ++begin;
}

void TransferDelegate::signalUpdateBatchEnd()
{
  ++end;
}

struct TransferObservations
{
  explicit TransferObservations(SceneIdentity &ids);
  void check(SceneIdentity &ids, bool changed, bool inspectionFirst) const;

  SceneRevision scene;
  InspectionRevision inspection;
};

TransferObservations::TransferObservations(SceneIdentity &ids)
    : scene(ids.sceneRevision()), inspection(ids.inspectionRevision())
{}

void TransferObservations::check(
    SceneIdentity &ids, bool changed, bool inspectionFirst) const
{
  if (inspectionFirst) {
    CHECK((ids.inspectionRevision() != inspection) == changed);
    CHECK((ids.sceneRevision() != scene) == changed);
  } else {
    CHECK((ids.sceneRevision() != scene) == changed);
    CHECK((ids.inspectionRevision() != inspection) == changed);
  }
  const TransferObservations current(ids);
  for (int i = 0; i < 3; ++i) {
    CHECK(ids.sceneRevision() == current.scene);
    CHECK(ids.inspectionRevision() == current.inspection);
  }
}

NodeDependencyWalk dependencies(SceneIdentity &ids, LayerNodeRef node)
{
  auto observation = ids.beginNodeDependencies(ids.handleOf(node));
  REQUIRE(observation);
  while (observation->ancestorSlot)
    REQUIRE(ids.advanceNodeDependencies(*observation));
  return *observation;
}

} // namespace

SCENARIO("effective reparent invalidates observations without coarse signals",
    "[integration][transfer-validity]")
{
  const bool inspectionFirst = GENERATE(false, true);
  const bool changeBack = GENERATE(false, true);
  Scene scene;
  Scene other;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  SceneIdentity secondIds(authority, scene);
  SceneIdentity otherIds(authority, other);
  auto *layer = scene.defaultLayer();
  auto root = layer->root();
  auto a = scene.insertChildTransformNode(
      root, vsr::math::translation_matrix(vsr::math::float3(2.f, 0.f, 0.f)));
  auto b = scene.insertChildTransformNode(
      root, vsr::math::translation_matrix(vsr::math::float3(5.f, 0.f, 0.f)));
  auto branch = scene.insertChildTransformNode(a);
  auto object = scene.createObject<Light>("directional");
  auto leaf = scene.insertChildObjectNode(branch, object);
  auto *delegate = scene.updateDelegate().emplace<TransferDelegate>();
  const auto handle = ids.handleOf(branch);
  const auto leafHandle = ids.handleOf(leaf);
  const auto lifetime = branch->lifetime();
  const auto topology = branch->topologyVersion();
  const auto leafTopology = leaf->topologyVersion();
  const auto edit = branch->value().editVersion();
  const auto transform = branch->value().transformVersion();
  const auto oldDependencies = dependencies(ids, leaf);
  const TransferObservations before(ids), secondBefore(secondIds);
  const TransferObservations unrelated(otherIds);

  root->container()->move_subtree(branch, b);
  if (changeBack)
    root->container()->move_subtree(branch, a);

  before.check(ids, true, inspectionFirst);
  secondBefore.check(secondIds, true, !inspectionFirst);
  unrelated.check(otherIds, false, inspectionFirst);
  CHECK(branch->parent() == (changeBack ? a : b));
  CHECK(a->isLeaf() == !changeBack);
  CHECK(b->isLeaf() == changeBack);
  CHECK(root->container()->size() == 5);
  CHECK(branch->lifetime() == lifetime);
  CHECK(branch->topologyVersion() > topology);
  CHECK(leaf->topologyVersion() == leafTopology);
  CHECK(branch->value().editVersion() == edit);
  CHECK(branch->value().transformVersion() == transform);
  CHECK(ids.handleOf(branch) == handle);
  CHECK(ids.handleOf(leaf) == leafHandle);
  CHECK(ids.resolve(handle) == branch);
  CHECK_FALSE(oldDependencies.matchesAncestry(dependencies(ids, leaf)));
  const auto currentDependencies = dependencies(ids, leaf);
  CHECK(currentDependencies.matchesAncestry(dependencies(ids, leaf)));
  CHECK(object->useCount(Object::UseKind::LAYER) == 1);
  CHECK(delegate->structure == 0);
  CHECK(delegate->transform == 0);
}

SCENARIO("reparent rejected and established no-op controls remain stable",
    "[integration][transfer-validity]")
{
  const bool inspectionFirst = GENERATE(false, true);
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto root = scene.defaultLayer()->root();
  auto branch = scene.insertChildTransformNode(root);
  auto leaf = scene.insertChildNode(branch);
  auto *forest = root->container();
  const auto topology = branch->topologyVersion();
  const auto oldDependencies = dependencies(ids, leaf);
  const TransferObservations before(ids);

  SECTION("same parent")
  {
    forest->move_subtree(branch, root);
  }
  SECTION("self")
  {
    forest->move_subtree(branch, branch);
  }
  SECTION("cycle")
  {
    forest->move_subtree(branch, leaf);
  }
  SECTION("root")
  {
    forest->move_subtree(root, leaf);
  }
  SECTION("null source")
  {
    forest->move_subtree({}, root);
  }
  SECTION("null destination")
  {
    forest->move_subtree(branch, {});
  }

  before.check(ids, false, inspectionFirst);
  CHECK(branch->parent() == root);
  CHECK(leaf->parent() == branch);
  CHECK(forest->size() == 3);
  CHECK(branch->topologyVersion() == topology);
  CHECK(oldDependencies.matchesAncestry(dependencies(ids, leaf)));
}

SCENARIO("public clones bind to their destination and preserve exact payloads",
    "[integration][transfer-validity][transfer-ownership]")
{
  const bool inspectionFirst = GENERATE(false, true);
  const bool crossLayer = GENERATE(false, true);
  const bool nonleaf = GENERATE(false, true);
  Scene scene;
  Scene other;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  SceneIdentity otherIds(authority, other);
  auto *sourceLayer = scene.defaultLayer();
  auto *destinationLayer =
      crossLayer ? scene.addLayer("destination") : sourceLayer;
  auto source = scene.insertChildTransformNode(sourceLayer->root());
  auto object = scene.createObject<Light>("directional");
  auto array = scene.createArray(ANARI_FLOAT32_MAT4, 2);
  auto matrix = vsr::math::IDENTITY_MAT4;
  matrix[0].x = 0.f;
  matrix[1].x = 0.75f;
  matrix[3].y = 8.f;
  const vsr::math::mat3 srt{vsr::math::float3(0.f, -2.f, 3.f),
      vsr::math::float3(12.f, 34.f, 56.f),
      vsr::math::float3(0.f, 8.f, 0.f)};
  source->value().setTransformState(matrix, srt);
  source->value().setEnabled(false);
  source->value().setName("branch");
  source->value().setInstanceParameter("id", vsr::core::Any(9));
  if (nonleaf) {
    scene.insertChildObjectNode(source, object, "light");
    scene.insertChildTransformArrayNode(source, array.data(), "instances");
    scene.insertChildNode(source, "empty");
  }
  const auto sourceHandle = ids.handleOf(source);
  const auto sourceEdit = source->value().editVersion();
  const auto sourceTransform = source->value().transformVersion();
  const auto sourceDependencies = dependencies(ids, source);
  auto *delegate = scene.updateDelegate().emplace<TransferDelegate>();
  const TransferObservations before(ids), unrelated(otherIds);
  auto copy = destinationLayer->root()->container()->copy_subtree(
      source, destinationLayer->root());
  REQUIRE(copy);
  before.check(ids, true, inspectionFirst);
  unrelated.check(otherIds, false, !inspectionFirst);
  CHECK(copy->parent() == destinationLayer->root());
  CHECK(copy->value().layer() == destinationLayer);
  CHECK(copy->value().getTransform() == matrix);
  CHECK(copy->value().getTransformSRT() == srt);
  CHECK(copy->value().name() == "branch");
  CHECK_FALSE(copy->value().isEnabled());
  CHECK(copy->value().hasDefault());
  CHECK_FALSE(copy->value().isDefaultValue());
  REQUIRE(copy->value().getInstanceParameters().size() == 1);
  CHECK(copy->value().getInstanceParameters().begin()->second.get<int>() == 9);
  CHECK(copy->value().editVersion() == sourceEdit);
  CHECK(copy->value().transformVersion() == sourceTransform);
  CHECK(ids.handleOf(copy) != sourceHandle);
  CHECK(sourceDependencies.matchesAncestry(dependencies(ids, source)));
  CHECK(destinationLayer->size()
      == (crossLayer ? (nonleaf ? 5 : 2) : (nonleaf ? 9 : 3)));
  if (!crossLayer)
    CHECK(copy->lifetime() != source->lifetime());
  if (nonleaf) {
    auto light = copy->next();
    auto instances = light->sibling();
    auto empty = instances->sibling();
    CHECK(light->value().layer() == destinationLayer);
    CHECK(light->value().getObject() == object.data());
    CHECK(light->value().name() == "light");
    CHECK(instances->value().layer() == destinationLayer);
    CHECK(instances->value().getTransformArray() == array.data());
    CHECK(instances->value().name() == "instances");
    CHECK(empty->value().layer() == destinationLayer);
    CHECK(empty->value().isEmpty());
    CHECK(empty->value().name() == "empty");
    CHECK(empty->sibling() == copy);
    CHECK(object->useCount(Object::UseKind::LAYER) == 2);
    CHECK(array->useCount(Object::UseKind::LAYER) == 2);
    CHECK(ids.handleOf(light) != ids.handleOf(source->next()));
  }
  source->value().setEnabled(true);
  source->value().setAsTransform(vsr::math::IDENTITY_MAT4);
  CHECK(copy->value().getTransform() == matrix);
  CHECK_FALSE(copy->value().isEnabled());
  source->erase_self();
  CHECK_FALSE(ids.resolve(sourceHandle));
  if (crossLayer)
    scene.removeLayer(sourceLayer);
  CHECK(copy->value().layer() == destinationLayer);
  CHECK(copy->value().getTransform() == matrix);
  CHECK(object->useCount(Object::UseKind::LAYER) == (nonleaf ? 1 : 0));
  CHECK(array->useCount(Object::UseKind::LAYER) == (nonleaf ? 1 : 0));
  const TransferObservations afterSourceDestruction(ids);
  copy->value().setToDefaultValue();
  afterSourceDestruction.check(ids, true, inspectionFirst);
  CHECK(copy->value().getTransform() == vsr::math::IDENTITY_MAT4);
  CHECK(copy->value().getTransformSRT()[0] == vsr::math::float3(1.f));
  CHECK(delegate->structure == 0);
  CHECK(delegate->transform == 0);
}

SCENARIO("subtree copying rejects foreign Scene ownership before insertion",
    "[integration][transfer-validity][transfer-ownership]")
{
  const bool inspectionFirst = GENERATE(false, true);
  Scene scene;
  Scene foreign;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene), foreignIds(authority, foreign);
  auto *destination = scene.defaultLayer();
  auto *sourceLayer = foreign.defaultLayer();
  auto localObject = scene.createObject<Light>("directional");
  auto foreignObject = foreign.createObject<Light>("directional");
  REQUIRE(localObject.index() == foreignObject.index());
  auto source = foreign.insertChildTransformNode(sourceLayer->root());
  SECTION("foreign object at the subtree root")
  {
    source->value().setAsObject(foreignObject.data());
  }
  SECTION("foreign object in a descendant after an empty sibling")
  {
    foreign.insertChildNode(source);
    foreign.insertChildObjectNode(source, foreignObject);
  }
  SECTION("foreign instance parameter on transform")
  {
    source->value().setInstanceParameter(
        "light", vsr::core::Any(ANARI_LIGHT, foreignObject.index()));
  }
  SECTION("foreign transform-only subtree is not a transfer API") {}
  const auto sourceHandle = foreignIds.handleOf(source);
  const auto sourceEdit = source->value().editVersion();
  const auto sourceSize = sourceLayer->size();
  const auto rootTopology = destination->root()->topologyVersion();
  const TransferObservations before(ids), foreignBefore(foreignIds);
  CHECK_THROWS_AS(destination->root()->container()->copy_subtree(
                      source, destination->root()),
      std::runtime_error);
  before.check(ids, false, inspectionFirst);
  foreignBefore.check(foreignIds, false, !inspectionFirst);
  CHECK(destination->size() == 1);
  CHECK(destination->root()->isLeaf());
  CHECK(destination->root()->topologyVersion() == rootTopology);
  CHECK(sourceLayer->size() == sourceSize);
  CHECK(source->value().editVersion() == sourceEdit);
  CHECK(foreignIds.resolve(sourceHandle) == source);
  CHECK(localObject->useCount(Object::UseKind::LAYER) == 0);
  CHECK(foreignObject->useCount(Object::UseKind::LAYER)
      == (source->value().isObject() || !source->isLeaf() ? 1 : 0));
}

SCENARIO("transfer controls do not mutate foreign Forest storage",
    "[integration][transfer-validity]")
{
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto *layer = scene.defaultLayer();
  auto *otherLayer = scene.addLayer("other");
  auto root = layer->root();
  auto otherRoot = otherLayer->root();
  auto node = scene.insertChildNode(root);
  auto otherNode = scene.insertChildNode(otherRoot);
  const auto oldDependencies = dependencies(ids, node);
  const TransferObservations before(ids);
  SECTION("copy root")
  {
    CHECK_FALSE(root->container()->copy_subtree(root, root));
  }
  SECTION("copy null source")
  {
    CHECK_FALSE(root->container()->copy_subtree({}, root));
  }
  SECTION("copy null parent")
  {
    CHECK_FALSE(root->container()->copy_subtree(node, {}));
  }
  SECTION("copy with a receiving Forest different from the parent")
  {
    CHECK_FALSE(root->container()->copy_subtree(node, otherRoot));
  }
  SECTION("reparent source belongs to another Forest")
  {
    root->container()->move_subtree(otherNode, root);
  }
  SECTION("reparent destination belongs to another Forest")
  {
    root->container()->move_subtree(node, otherRoot);
  }
  before.check(ids, false, true);
  CHECK(layer->size() == 2);
  CHECK(otherLayer->size() == 2);
  CHECK(node->parent() == root);
  CHECK(otherNode->parent() == otherRoot);
  CHECK(oldDependencies.matchesAncestry(dependencies(ids, node)));
}

SCENARIO("clone removal reuse and relocation preserve surviving identities",
    "[integration][transfer-validity][transfer-ownership]")
{
  const bool inspectionFirst = GENERATE(false, true);
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto *layer = scene.defaultLayer();
  auto root = layer->root();
  auto object = scene.createObject<Light>("directional");
  auto source = scene.insertChildObjectNode(root, object);
  const auto sourceHandle = ids.handleOf(source);
  const auto sourceDependencies = dependencies(ids, source);
  const TransferObservations before(ids);
  auto copy = root->container()->copy_subtree(source, source);
  REQUIRE(copy);
  CHECK(copy->parent() == root);
  CHECK(copy->index() == 2);
  CHECK(object->useCount(Object::UseKind::LAYER) == 2);
  const auto copyHandle = ids.handleOf(copy);
  const auto copyLifetime = copy->lifetime();
  const auto copyTopology = copy->topologyVersion();
  const auto copyEdit = *ids.nodeEditVersion(copyHandle);
  const auto copyDependencies = dependencies(ids, copy);
  const TransferObservations beforeReserve(ids);
  root->container()->reserve(8192);
  beforeReserve.check(ids, false, inspectionFirst);
  CHECK(copy->value().layer() == layer);
  CHECK(copy->lifetime() == copyLifetime);
  CHECK(copy->topologyVersion() == copyTopology);
  CHECK(ids.isCurrent(copyEdit));
  CHECK(copyDependencies.matchesAncestry(dependencies(ids, copy)));
  CHECK(sourceDependencies.matchesAncestry(dependencies(ids, source)));
  CHECK(object->useCount(Object::UseKind::LAYER) == 2);
  auto objectCopy = root->container()->copy_subtree(copy, root);
  REQUIRE(objectCopy);
  CHECK(object->useCount(Object::UseKind::LAYER) == 3);
  objectCopy->erase_self();
  const auto slot = copy.index();
  copy->erase_self();
  CHECK_FALSE(ids.resolve(copyHandle));
  CHECK_FALSE(ids.isCurrent(copyEdit));
  CHECK(object->useCount(Object::UseKind::LAYER) == 1);
  auto reused = root->insert_last_child_at(slot, LayerNodeData(layer, object));
  REQUIRE(reused);
  CHECK(reused.index() == 2);
  CHECK(reused->lifetime() != copyLifetime);
  CHECK(ids.handleOf(reused) != copyHandle);
  CHECK(reused->value().layer() == layer);
  CHECK(object->useCount(Object::UseKind::LAYER) == 2);
  reused->erase_self();
  before.check(ids, true, inspectionFirst);
  CHECK(layer->size() == 2);
  CHECK(ids.handleOf(source) == sourceHandle);
  CHECK(sourceDependencies.matchesAncestry(dependencies(ids, source)));
  CHECK(object->useCount(Object::UseKind::LAYER) == 1);
}

SCENARIO("copied payloads qualify source and destination owner destruction",
    "[integration][transfer-validity][transfer-ownership]")
{
  const bool sceneFirst = GENERATE(false, true);
  const bool retainDestination = GENERATE(false, true);
  std::optional<Scene> scene(std::in_place);
  auto *sourceLayer = scene->defaultLayer();
  auto *destinationLayer = scene->addLayer("destination");
  auto retained = scene->layers().at("destination")->ptr;
  auto object = scene->createObject<Light>("directional");
  auto source = scene->insertChildObjectNode(sourceLayer->root(), object);
  auto copy = destinationLayer->root()->container()->copy_subtree(
      source, destinationLayer->root());
  REQUIRE(copy);
  auto detached = std::make_unique<LayerNodeData>(copy->value());
  CHECK(object->useCount(Object::UseKind::LAYER) == 3);
  scene->removeLayer(sourceLayer);
  CHECK(object->useCount(Object::UseKind::LAYER) == 2);
  CHECK(copy->value().getObject() == object.data());
  CHECK(copy->value().layer() == destinationLayer);
  CHECK(detached->getObject() == object.data());
  if (sceneFirst) {
    scene.reset();
    CHECK(retained->scene() == nullptr);
    CHECK(copy->value().getObject() == nullptr);
    CHECK(detached->getObject() == nullptr);
    copy->value().setEnabled(false);
    copy->value().setInstanceParameter("id", vsr::core::Any(3));
    auto retainedClone =
        retained->root()->container()->copy_subtree(copy, retained->root());
    REQUIRE(retainedClone);
    CHECK(retainedClone->value().getObject() == nullptr);
    CHECK(retainedClone->value().layer() == retained.get());
    if (!retainDestination)
      retained.reset();
  } else {
    scene->removeLayer(destinationLayer);
    if (!retainDestination) {
      retained.reset();
      CHECK(detached->layer() == nullptr);
      CHECK(object->useCount(Object::UseKind::LAYER) == 1);
      CHECK(detached->getObject() == object.data());
      detached.reset();
      CHECK(object->useCount(Object::UseKind::LAYER) == 0);
      return;
    }
    CHECK(copy->value().getObject() == object.data());
    scene.reset();
    CHECK(retained->scene() == nullptr);
    CHECK(copy->value().getObject() == nullptr);
  }
  Scene replacement;
  auto replacementObject = replacement.createObject<Light>("directional");
  CHECK(detached->getObject() == nullptr);
  CHECK(replacementObject->useCount(Object::UseKind::LAYER) == 0);
  detached->setEnabled(false);
  detached.reset();
  retained.reset();
}

SCENARIO("transfer edits preserve distinct callback batching contracts",
    "[integration][transfer-validity]")
{
  const bool inspectionFirst = GENERATE(false, true);
  const int batching = GENERATE(0, 1, 2, 3);
  const bool structuralBatch = batching == 1 || batching == 3;
  const bool updateBatch = batching == 2 || batching == 3;
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto *layer = scene.defaultLayer();
  auto root = layer->root();
  auto a = scene.insertChildNode(root);
  auto b = scene.insertChildNode(root);
  auto node = scene.insertChildTransformNode(a);
  auto *delegate = scene.updateDelegate().emplace<TransferDelegate>();
  const TransferObservations before(ids);
  if (structuralBatch)
    scene.beginLayerEditBatch();
  if (updateBatch) {
    scene.beginUpdateBatch();
    scene.beginUpdateBatch();
  }
  auto copy = root->container()->copy_subtree(node, b);
  root->container()->move_subtree(node, b);
  copy->value().setEnabled(false);
  before.check(ids, true, inspectionFirst);
  CHECK(delegate->structure == 0);
  CHECK(delegate->transform == 0);
  scene.signalLayerStructureChanged(layer);
  scene.signalLayerStructureChanged(layer);
  CHECK(delegate->structure == (structuralBatch ? 0 : 2));
  if (structuralBatch)
    scene.endLayerEditBatch();
  CHECK(delegate->structure == (structuralBatch ? 1 : 2));
  if (updateBatch) {
    scene.endUpdateBatch();
    CHECK(delegate->end == 1);
    scene.endUpdateBatch();
  }
  CHECK(delegate->begin == (updateBatch ? 2 : 0));
  CHECK(delegate->end == (updateBatch ? 2 : 0));
  CHECK(delegate->transform == 0);
  CHECK(layer->size() == 5);
  CHECK(a->isLeaf());
  CHECK(node->parent() == b);
  CHECK(copy->parent() == b);
}

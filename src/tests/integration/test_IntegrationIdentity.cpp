// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_integration
#include "vsr/integration/identity/Identity.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// std
#include <memory>
#include <optional>
#include <stdexcept>

using namespace vsr::integration::identity;
using vsr::scene::Scene;

namespace {

struct PayloadDelegate : vsr::scene::EmptyUpdateDelegate
{
  void signalLayerStructureUpdated(const vsr::scene::Layer *) override;
  void signalLayerTransformUpdated(const vsr::scene::Layer *) override;
  void signalUpdateBatchBegin() override;
  void signalUpdateBatchEnd() override;
  void signalObjectLayerUseCountZero(const vsr::scene::Object *) override;

  int structure{0};
  int transform{0};
  int begin{0};
  int end{0};
  int zeroUses{0};
};

void PayloadDelegate::signalLayerStructureUpdated(const vsr::scene::Layer *)
{
  ++structure;
}

void PayloadDelegate::signalLayerTransformUpdated(const vsr::scene::Layer *)
{
  ++transform;
}

void PayloadDelegate::signalUpdateBatchBegin()
{
  ++begin;
}

void PayloadDelegate::signalUpdateBatchEnd()
{
  ++end;
}

void PayloadDelegate::signalObjectLayerUseCountZero(const vsr::scene::Object *)
{
  ++zeroUses;
}

} // namespace

SCENARIO("attached payload writes invalidate both observations without signals",
    "[integration][payload-validity]")
{
  using namespace vsr::scene;
  const bool inspectionFirst = GENERATE(false, true);
  Scene scene;
  Scene other;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  SceneIdentity otherIds(authority, other);
  SceneIdentity secondRegistration(authority, scene);
  auto *layer = scene.defaultLayer();
  auto parent = scene.insertChildTransformNode(layer->root());
  auto node = scene.insertChildTransformNode(parent);
  auto object = scene.createObject<Light>("directional");
  auto array = scene.createArray(ANARI_FLOAT32_MAT4, 2);
  node->value().setInstanceParameter("existing", vsr::core::Any(1.f));
  auto *delegate = scene.updateDelegate().emplace<PayloadDelegate>();
  const auto before = ids.sceneRevision();
  const auto inspected = ids.inspectionRevision();
  const auto unrelated = otherIds.sceneRevision();
  const auto unrelatedInspection = otherIds.inspectionRevision();
  const auto secondBefore = secondRegistration.sceneRevision();
  const auto secondInspected = secondRegistration.inspectionRevision();
  const auto nodeHandle = ids.handleOf(node);
  const auto nodeLifetime = node->lifetime();
  const auto topology = node->topologyVersion();
  const auto edit = node->value().editVersion();
  const auto transform = node->value().transformVersion();
  const auto translated =
      vsr::math::translation_matrix(vsr::math::float3(1.f, 2.f, 3.f));

  SECTION("local matrix")
  {
    node->value().setAsTransform(translated);
    CHECK(node->value().getTransform()[3].x == 1.f);
  }
  SECTION("ancestor matrix and change-back without reads")
  {
    parent->value().setAsTransform(translated);
    parent->value().setAsTransform(vsr::math::IDENTITY_MAT4);
    CHECK(node->value().editVersion() == edit);
  }
  SECTION("matrix with explicit default and restoration")
  {
    node->value().setAsTransform(translated, vsr::math::IDENTITY_MAT4);
    const auto intermediate = ids.sceneRevision();
    const auto intermediateInspection = ids.inspectionRevision();
    node->value().setToDefaultValue();
    CHECK(node->value().getTransform() == vsr::math::IDENTITY_MAT4);
    CHECK(ids.sceneRevision() != intermediate);
    CHECK(ids.inspectionRevision() != intermediateInspection);
  }
  SECTION("default restoration releases an intervening object reference")
  {
    node->value().setAsObject(object.data());
    CHECK(object->useCount(Object::UseKind::LAYER) == 1);
    node->value().setToDefaultValue();
    CHECK(node->value().getTransform() == vsr::math::IDENTITY_MAT4);
    CHECK(object->useCount(Object::UseKind::LAYER) == 0);
  }
  SECTION("default restoration is itself an intervening write")
  {
    node->value().setToDefaultValue();
  }
  SECTION("SRT")
  {
    auto srt = node->value().getTransformSRT();
    srt[2] = vsr::math::float3(1.f, 2.f, 3.f);
    node->value().setAsTransform(srt);
    CHECK(node->value().getTransform()[3].y == 2.f);
  }
  SECTION("prepared exact state")
  {
    auto srt = node->value().getTransformSRT();
    srt[2] = vsr::math::float3(1.f, 2.f, 3.f);
    node->value().setTransformState(translated, srt);
    CHECK(node->value().getTransformSRT()[2].z == 3.f);
  }
  SECTION("local enabled change-back")
  {
    node->value().setEnabled(false);
    node->value().setEnabled(true);
    CHECK(node->value().transformVersion() == transform);
  }
  SECTION("disabled ancestor")
  {
    parent->value().setEnabled(false);
    CHECK(node->value().editVersion() == edit);
  }
  SECTION("object pointer")
  {
    node->value().setAsObject(object.data());
    CHECK(node->value().getObject() == object.data());
    CHECK(object->useCount(Object::UseKind::LAYER) == 1);
  }
  SECTION("object type and index")
  {
    node->value().setAsObject(object->type(), object.index());
    CHECK(node->value().getObject() == object.data());
  }
  SECTION("transform array")
  {
    node->value().setAsTransformArray(array.data());
    CHECK(node->value().getTransformArray() == array.data());
  }
  SECTION("raw object value")
  {
    node->value().setValueRaw(vsr::core::Any(object->type(), object.index()));
    CHECK(object->useCount(Object::UseKind::LAYER) == 1);
  }
  SECTION("raw value without reference accounting")
  {
    node->value().setValueRaw(vsr::core::Any(translated), true);
  }
  SECTION("raw scalar value")
  {
    node->value().setValueRaw(vsr::core::Any(3.f));
    CHECK(node->value().type() == ANARI_FLOAT32);
  }
  SECTION("empty value")
  {
    node->value().setEmpty();
    CHECK(node->value().isEmpty());
  }
  SECTION("null object writes empty")
  {
    node->value().setAsObject(nullptr);
    CHECK(node->value().isEmpty());
  }
  SECTION("instance set does not advance precise edit or transform history")
  {
    node->value().setInstanceParameter("new", vsr::core::Any(2.f));
    CHECK(node->value().editVersion() == edit);
    CHECK(node->value().transformVersion() == transform);
  }
  SECTION("instance replacement and change-back")
  {
    node->value().setInstanceParameter("existing", vsr::core::Any(2.f));
    node->value().setInstanceParameter("existing", vsr::core::Any(1.f));
    CHECK(node->value().editVersion() == edit);
    CHECK(node->value().transformVersion() == transform);
  }
  SECTION("instance clear")
  {
    node->value().clearInstanceParameters();
    CHECK(node->value().getInstanceParameters().empty());
    CHECK(node->value().editVersion() == edit);
    CHECK(node->value().transformVersion() == transform);
  }

  if (inspectionFirst) {
    CHECK(ids.inspectionRevision() != inspected);
    CHECK(ids.sceneRevision() != before);
  } else {
    CHECK(ids.sceneRevision() != before);
    CHECK(ids.inspectionRevision() != inspected);
  }
  const auto settled = ids.sceneRevision();
  const auto settledInspection = ids.inspectionRevision();
  for (int i = 0; i < 4; ++i) {
    CHECK(ids.sceneRevision() == settled);
    CHECK(ids.inspectionRevision() == settledInspection);
  }
  CHECK(otherIds.sceneRevision() == unrelated);
  CHECK(otherIds.inspectionRevision() == unrelatedInspection);
  CHECK(secondRegistration.inspectionRevision() != secondInspected);
  CHECK(secondRegistration.sceneRevision() != secondBefore);
  CHECK(ids.handleOf(node) == nodeHandle);
  CHECK(node->lifetime() == nodeLifetime);
  CHECK(node->topologyVersion() == topology);
  CHECK(delegate->structure == 0);
  CHECK(delegate->transform == 0);
  scene.updateDelegate().erase(delegate);
}

SCENARIO("detached work and storage reserve are not attached edits",
    "[integration][payload-validity][payload-ownership]")
{
  using namespace vsr::scene;
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto *layer = scene.defaultLayer();
  auto node = scene.insertChildTransformNode(layer->root());
  auto object = scene.createObject<Light>("directional");
  const auto handle = ids.handleOf(node);
  const auto token = ids.nodeEditVersion(handle);
  const auto before = ids.sceneRevision();
  const auto inspected = ids.inspectionRevision();
  const auto rootEdit = layer->root()->value().editVersion();
  const auto rootTransform = layer->root()->value().transformVersion();
  {
    LayerNodeData detached(layer, object);
    auto copied = detached;
    auto moved = std::move(copied);
    moved.setInstanceParameter("id", vsr::core::Any(5));
    moved.setEnabled(false);
    CHECK(object->useCount(Object::UseKind::LAYER) == 2);
  }
  CHECK(object->useCount(Object::UseKind::LAYER) == 0);
  node->container()->reserve(4096);
  CHECK(ids.sceneRevision() == before);
  CHECK(ids.inspectionRevision() == inspected);
  REQUIRE(token);
  CHECK(ids.isCurrent(*token));
  CHECK(ids.handleOf(node) == handle);
  CHECK(layer->root()->value().editVersion() == rootEdit);
  CHECK(layer->root()->value().transformVersion() == rootTransform);
  CHECK_FALSE(scene.getObject<Light>(VSR_INVALID_INDEX));
  CHECK_FALSE(scene.insertChildObjectNode(
      layer->root(), ANARI_LIGHT, VSR_INVALID_INDEX));
  CHECK_FALSE(layer->root()->insert_last_child_at(
      node.index(), LayerNodeData(layer, object)));
  CHECK_FALSE(layer->root()->insert_last_child_at(
      VSR_INVALID_INDEX, LayerNodeData(layer, object)));
  CHECK(object->useCount(Object::UseKind::LAYER) == 0);
  CHECK(ids.sceneRevision() == before);
  CHECK(ids.inspectionRevision() == inspected);
  layer->root()->value().setEnabled(false);
  CHECK(ids.sceneRevision() != before);
  CHECK(ids.inspectionRevision() != inspected);
}

SCENARIO("ordinary chosen gap and reused storage bind safe payloads",
    "[integration][payload-validity][payload-ownership]")
{
  using namespace vsr::scene;
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto *layer = scene.defaultLayer();
  auto object = scene.createObject<Light>("directional");
  LayerNodeRef node;
  SECTION("ordinary first")
  {
    node = layer->root()->insert_first_child(LayerNodeData(layer, object));
  }
  SECTION("ordinary last")
  {
    node = layer->root()->insert_last_child(LayerNodeData(layer, object));
  }
  SECTION("chosen next")
  {
    node = layer->root()->insert_last_child_at(1, LayerNodeData(layer, object));
  }
  SECTION("chosen high with gaps and gap reuse")
  {
    node =
        layer->root()->insert_last_child_at(32, LayerNodeData(layer, object));
    CHECK(layer->size() == 2);
    CHECK_FALSE(layer->at(1));
    CHECK_FALSE(layer->at(31));
    auto gap =
        layer->root()->insert_last_child_at(5, LayerNodeData(layer, object));
    CHECK(layer->size() == 3);
    CHECK(gap->value().layer() == layer);
    layer->erase(gap);
    CHECK(object->useCount(Object::UseKind::LAYER) == 1);
  }
  SECTION("ordinary erased-slot reuse")
  {
    auto old = layer->root()->insert_last_child(LayerNodeData(layer, object));
    const auto index = old.index();
    const auto lifetime = old->lifetime();
    layer->erase(old);
    CHECK(object->useCount(Object::UseKind::LAYER) == 0);
    node = layer->root()->insert_last_child(LayerNodeData(layer, object));
    CHECK(node.index() == index);
    CHECK(node->lifetime() != lifetime);
  }
  SECTION("chosen erased-slot reuse")
  {
    auto old =
        layer->root()->insert_last_child_at(12, LayerNodeData(layer, object));
    const auto lifetime = old->lifetime();
    layer->erase(old);
    node =
        layer->root()->insert_last_child_at(12, LayerNodeData(layer, object));
    CHECK(node->lifetime() != lifetime);
  }
  REQUIRE(node);
  CHECK(node->value().layer() == layer);
  CHECK(node->value().getObject() == object.data());
  CHECK(object->useCount(Object::UseKind::LAYER) == 1);
  const auto token = ids.nodeEditVersion(ids.handleOf(node));
  const auto lifetime = node->lifetime();
  const auto transform = node->value().transformVersion();
  const auto topology = node->topologyVersion();
  // Grow beyond the unreserved storage, exercising append relocation too.
  for (int i = 0; i < 256; ++i)
    layer->root()->insert_last_child(LayerNodeData(layer, "append"));
  REQUIRE(token);
  CHECK(ids.isCurrent(*token));
  CHECK(node->value().transformVersion() == transform);
  CHECK(node->topologyVersion() == topology);
  CHECK(node->lifetime() == lifetime);
  CHECK(node->value().getObject() == object.data());
  CHECK(object->useCount(Object::UseKind::LAYER) == 1);
  const auto before = ids.sceneRevision();
  const auto inspected = ids.inspectionRevision();
  node->container()->reserve(8192);
  CHECK(ids.isCurrent(*token));
  CHECK(ids.sceneRevision() == before);
  CHECK(ids.inspectionRevision() == inspected);
  CHECK(object->useCount(Object::UseKind::LAYER) == 1);
  node->value().setEnabled(false);
  CHECK(ids.sceneRevision() != before);
  CHECK(ids.inspectionRevision() != inspected);
}

SCENARIO("storage relocation preserves exact payload and ancestor history",
    "[integration][payload-validity][payload-ownership]")
{
  using namespace vsr::scene;
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto *layer = scene.defaultLayer();
  auto parent = scene.insertChildTransformNode(layer->root());
  auto node = scene.insertChildTransformNode(parent);
  auto matrix = vsr::math::IDENTITY_MAT4;
  matrix[0].x = 0.f;
  matrix[1].x = 0.75f;
  matrix[3].y = 8.f;
  const vsr::math::mat3 srt{vsr::math::float3(0.f, -2.f, 3.f),
      vsr::math::float3(12.f, 34.f, 56.f),
      vsr::math::float3(0.f, 8.f, 0.f)};
  node->value().setTransformState(matrix, srt);
  node->value().name() = "exact payload";
  node->value().setEnabled(false);
  node->value().setInstanceParameter("id", vsr::core::Any(9));
  const auto handle = ids.handleOf(node);
  const auto edit = ids.nodeEditVersion(handle);
  auto observe = [&] {
    auto walk = ids.beginNodeDependencies(handle);
    REQUIRE(walk);
    while (walk->ancestorSlot)
      REQUIRE(ids.advanceNodeDependencies(*walk));
    return *walk;
  };
  const auto ancestors = observe();
  const auto rootEdit = layer->root()->value().editVersion();
  const auto parentEdit = parent->value().editVersion();
  for (int i = 0; i < 256; ++i)
    layer->root()->insert_last_child(LayerNodeData(layer, "grow"));
  const auto before = ids.sceneRevision();
  const auto inspected = ids.inspectionRevision();
  node->container()->reserve(8192);
  CHECK(node->value().getTransform() == matrix);
  CHECK(node->value().getTransformSRT() == srt);
  CHECK(node->value().name() == "exact payload");
  CHECK_FALSE(node->value().isEnabled());
  CHECK(node->value().hasDefault());
  CHECK_FALSE(node->value().isDefaultValue());
  REQUIRE(node->value().getInstanceParameters().size() == 1);
  CHECK(node->value().getInstanceParameters().begin()->second.get<int>() == 9);
  REQUIRE(edit);
  CHECK(ids.isCurrent(*edit));
  CHECK(ancestors.matchesStructure(observe()));
  CHECK(ancestors.matchesAncestry(observe()));
  CHECK(layer->root()->value().editVersion() == rootEdit);
  CHECK(parent->value().editVersion() == parentEdit);
  CHECK(ids.sceneRevision() == before);
  CHECK(ids.inspectionRevision() == inspected);
  node->value().setToDefaultValue();
  CHECK(node->value().getTransform() == vsr::math::IDENTITY_MAT4);
  CHECK_FALSE(node->value().isEnabled());
  CHECK(ids.sceneRevision() != before);
  CHECK(ids.inspectionRevision() != inspected);
}

SCENARIO("retained Layers and detached payloads qualify both owner lifetimes",
    "[integration][payload-ownership]")
{
  using namespace vsr::scene;
  std::optional<Scene> scene(std::in_place);
  auto *layer = scene->defaultLayer();
  auto retained = scene->layers().begin()->second.ptr;
  auto object = scene->createObject<Light>("directional");
  const auto index = object.index();
  auto node = scene->insertChildObjectNode(layer->root(), object);
  auto detached = std::make_unique<LayerNodeData>(node->value());
  CHECK(object->useCount(Object::UseKind::LAYER) == 2);
  SECTION("Layer dies first while Scene and reference counts remain live")
  {
    scene->removeLayer("default");
    CHECK(detached->getObject() == object.data());
    retained.reset();
    CHECK(detached->layer() == nullptr);
    CHECK(detached->getObject() == object.data());
    CHECK(object->useCount(Object::UseKind::LAYER) == 1);
    detached.reset();
    CHECK(object->useCount(Object::UseKind::LAYER) == 0);
  }
  SECTION("Layer dies then Scene dies with detached object reference")
  {
    scene->removeLayer("default");
    retained.reset();
    CHECK(detached->layer() == nullptr);
    CHECK(detached->getObject() == object.data());
    CHECK(object->useCount(Object::UseKind::LAYER) == 1);
    scene.reset();
    CHECK(detached->getObject() == nullptr);
    auto copy = *detached;
    auto moved = std::move(copy);
    CHECK(moved.getObject() == nullptr);
    CHECK(moved.layer() == nullptr);
    moved.setEnabled(false);
    detached.reset();
  }
  SECTION("object-containing root remains safe after Scene destruction")
  {
    retained->root()->value().setAsObject(object.data());
    CHECK(object->useCount(Object::UseKind::LAYER) == 3);
    scene.reset();
    CHECK(retained->root()->value().getObject() == nullptr);
    retained->root()->value().setEmpty();
    retained.reset();
    CHECK(detached->layer() == nullptr);
    CHECK(detached->getObject() == nullptr);
    detached.reset();
  }
  SECTION("Scene dies before retained object-containing Layer and payload")
  {
    scene.reset();
    CHECK(retained->scene() == nullptr);
    CHECK(retained->root()->value().layer() == retained.get());
    CHECK(retained->at(node.index())->value().getObject() == nullptr);
    CHECK(detached->getObject() == nullptr);
    retained->at(node.index())->value().setEnabled(false);
    retained->root()->value().setAsTransform(vsr::math::IDENTITY_MAT4);
    detached->setAsObject(ANARI_LIGHT, index);
    retained->clear();
    CHECK(retained->empty());
    retained.reset();
    CHECK(detached->layer() == nullptr);
    detached.reset();
  }
  SECTION("Scene storage reuse cannot revive an expired resolution context")
  {
    scene.reset();
    scene.emplace();
    auto replacement = scene->createObject<Light>("directional");
    CHECK(replacement.index() == index);
    CHECK(retained->scene() == nullptr);
    CHECK(detached->getObject() == nullptr);
    CHECK(replacement->useCount(Object::UseKind::LAYER) == 0);
    detached->setAsObject(ANARI_LIGHT, index);
    CHECK(detached->getObject() == nullptr);
    retained.reset();
    detached.reset();
    CHECK(replacement->useCount(Object::UseKind::LAYER) == 0);
  }
  SECTION("Layer storage reuse cannot revive an expired Layer association")
  {
    std::optional<Layer> local(std::in_place, &*scene, "local");
    LayerNodeData value(&*local, object);
    local.reset();
    local.emplace(&*scene, "replacement");
    CHECK(value.layer() == nullptr);
    CHECK(value.getObject() == object.data());
    CHECK(local->root()->value().layer() == &*local);
    CHECK(object->useCount(Object::UseKind::LAYER) == 3);
    value.setEmpty();
    CHECK(object->useCount(Object::UseKind::LAYER) == 2);
  }
}

SCENARIO(
    "attached assignment keeps the receiving Layer and exact reference counts",
    "[integration][payload-validity][payload-ownership]")
{
  using namespace vsr::scene;
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto *destination = scene.defaultLayer();
  auto *sourceLayer = scene.addLayer("source");
  auto first = scene.createObject<Light>("directional");
  auto second = scene.createObject<Light>("point");
  auto node = scene.insertChildObjectNode(destination->root(), first);
  auto source = scene.insertChildObjectNode(sourceLayer->root(), second);
  auto *delegate = scene.updateDelegate().emplace<PayloadDelegate>();
  const auto before = ids.sceneRevision();
  const auto inspected = ids.inspectionRevision();
  const auto lifetime = node->lifetime();
  SECTION("copy from live source then destroy source Layer")
  {
    node->value() = source->value();
    CHECK(first->useCount(Object::UseKind::LAYER) == 0);
    CHECK(second->useCount(Object::UseKind::LAYER) == 2);
    CHECK(delegate->zeroUses == 1);
  }
  SECTION("copy from detached value whose Layer is already dead")
  {
    auto detached = source->value();
    scene.removeLayer("source");
    CHECK(detached.layer() == nullptr);
    CHECK(detached.getObject() == second.data());
    node->value() = detached;
    CHECK(second->useCount(Object::UseKind::LAYER) == 2);
  }
  SECTION("copy detached transform without an owner")
  {
    LayerNodeData detached(nullptr, vsr::math::IDENTITY_MAT4);
    node->value() = detached;
    CHECK(node->value().getTransform() == vsr::math::IDENTITY_MAT4);
  }
  SECTION("move from live source leaves source safe and attached")
  {
    node->value() = std::move(source->value());
    CHECK(first->useCount(Object::UseKind::LAYER) == 0);
    CHECK(second->useCount(Object::UseKind::LAYER) == 1);
    CHECK(source->value().layer() == sourceLayer);
    CHECK(source->value().isEmpty());
    const auto moved = ids.sceneRevision();
    const auto movedInspection = ids.inspectionRevision();
    source->value().setEnabled(false);
    CHECK(ids.sceneRevision() != moved);
    CHECK(ids.inspectionRevision() != movedInspection);
    CHECK(delegate->zeroUses == 1);
  }
  SECTION("move detached object reference")
  {
    auto detached = source->value();
    node->value() = std::move(detached);
    CHECK(second->useCount(Object::UseKind::LAYER) == 2);
    CHECK(detached.isEmpty());
  }
  SECTION("copy self-assignment preserves established history and counts")
  {
    const auto edit = node->value().editVersion();
    node->value() = node->value();
    CHECK(node->value().editVersion() == edit + 1);
    CHECK(first->useCount(Object::UseKind::LAYER) == 1);
    CHECK(delegate->zeroUses == 0);
  }
  SECTION("move self-assignment preserves established history and counts")
  {
    const auto edit = node->value().editVersion();
    node->value() = std::move(node->value());
    CHECK(node->value().editVersion() == edit + 1);
    CHECK(first->useCount(Object::UseKind::LAYER) == 1);
    CHECK(delegate->zeroUses == 0);
  }
  CHECK(ids.inspectionRevision() != inspected);
  CHECK(ids.sceneRevision() != before);
  CHECK(node->lifetime() == lifetime);
  CHECK(node->value().layer() == destination);
  CHECK(delegate->transform == 0);
  // Assignment itself is not structural. The source-removal section has the
  // one established explicit removal signal, not a new setter notification.
  CHECK(delegate->structure == 0);
  scene.updateDelegate().erase(delegate);
  scene.removeLayer("source");
  CHECK(node->value().layer() == destination);
  if (node->value().isObject()) {
    CHECK(node->value().getObject() != nullptr);
    CHECK(node->value().getObject()->useCount(Object::UseKind::LAYER) == 1);
  }
  const auto afterRemoval = ids.sceneRevision();
  const auto afterRemovalInspection = ids.inspectionRevision();
  node->value().setEnabled(false);
  CHECK(ids.sceneRevision() != afterRemoval);
  CHECK(ids.inspectionRevision() != afterRemovalInspection);
}

SCENARIO("attached assignment never reinterprets foreign object indices",
    "[integration][payload-validity][payload-ownership]")
{
  using namespace vsr::scene;
  Scene scene;
  Scene foreign;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  SceneIdentity foreignIds(authority, foreign);
  auto object = scene.createObject<Light>("directional");
  auto otherObject = foreign.createObject<Light>("directional");
  auto node = scene.insertChildObjectNode(scene.defaultLayer()->root(), object);
  auto source =
      foreign.insertChildTransformNode(foreign.defaultLayer()->root());
  SECTION("object payload")
  {
    source->value().setAsObject(otherObject.data());
  }
  SECTION("instance object reference")
  {
    source->value().setInstanceParameter(
        "light", vsr::core::Any(otherObject->type(), otherObject.index()));
  }
  const auto before = ids.sceneRevision();
  const auto inspected = ids.inspectionRevision();
  const auto foreignBefore = foreignIds.sceneRevision();
  const auto edit = node->value().editVersion();
  CHECK_THROWS_AS(node->value() = source->value(), std::runtime_error);
  CHECK_THROWS_AS(
      node->value() = std::move(source->value()), std::runtime_error);
  CHECK(node->value().getObject() == object.data());
  CHECK(node->value().editVersion() == edit);
  CHECK(object->useCount(Object::UseKind::LAYER) == 1);
  CHECK(ids.sceneRevision() == before);
  CHECK(ids.inspectionRevision() == inspected);
  CHECK(foreignIds.sceneRevision() == foreignBefore);
}

SCENARIO("genuine move-out is detached but the changed source stays attached",
    "[integration][payload-validity][payload-ownership]")
{
  using namespace vsr::scene;
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto *layer = scene.defaultLayer();
  auto object = scene.createObject<Light>("directional");
  auto node = scene.insertChildObjectNode(layer->root(), object);
  const auto before = ids.sceneRevision();
  const auto inspected = ids.inspectionRevision();
  const auto edit = node->value().editVersion();
  auto moved = std::move(node->value());
  CHECK(ids.sceneRevision() != before);
  CHECK(ids.inspectionRevision() != inspected);
  CHECK(node->value().editVersion() == edit + 1);
  CHECK(moved.editVersion() == edit);
  CHECK(node->value().layer() == layer);
  CHECK(node->value().isEmpty());
  CHECK(moved.getObject() == object.data());
  CHECK(object->useCount(Object::UseKind::LAYER) == 1);
  const auto detachedBefore = ids.sceneRevision();
  const auto detachedInspection = ids.inspectionRevision();
  moved.setEnabled(false);
  moved.setInstanceParameter("id", vsr::core::Any(3));
  CHECK(ids.sceneRevision() == detachedBefore);
  CHECK(ids.inspectionRevision() == detachedInspection);
  node->value().setEnabled(false);
  CHECK(ids.sceneRevision() != detachedBefore);
  CHECK(ids.inspectionRevision() != detachedInspection);
  node->value() = std::move(moved);
  CHECK(node->value().getObject() == object.data());
  CHECK(object->useCount(Object::UseKind::LAYER) == 1);
}

SCENARIO(
    "existing object array active Layer and filter dependencies remain valid",
    "[integration][payload-validity]")
{
  using namespace vsr::scene;
  const bool inspectionFirst = GENERATE(false, true);
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto first = scene.createObject<Geometry>("sphere");
  auto second = scene.createObject<Geometry>("sphere");
  auto surface = scene.createSurface("surface", first);
  auto array = scene.createArray(ANARI_FLOAT32_VEC3, 2);
  scene.insertChildObjectNode(scene.defaultLayer()->root(), surface);
  const auto before = ids.sceneRevision();
  const auto inspected = ids.inspectionRevision();
  SECTION("geometry parameter")
  {
    first->setParameter("radius", 2.f);
  }
  SECTION("object reference parameter")
  {
    surface->setParameterObject("geometry", *second);
    CHECK(
        surface->parameterValueAsObject<Geometry>("geometry") == second.data());
  }
  SECTION("array write and unmap")
  {
    auto *values = array->mapAs<vsr::math::float3>();
    values[0] = vsr::math::float3(1.f, 2.f, 3.f);
    array->unmap();
  }
  SECTION("active Layer")
  {
    scene.setLayerActive("default", false);
  }
  SECTION("explicit object filtering")
  {
    scene.updateDelegate().signalObjectFilteringChanged();
  }
  if (inspectionFirst) {
    CHECK(ids.inspectionRevision() != inspected);
    CHECK(ids.sceneRevision() != before);
  } else {
    CHECK(ids.sceneRevision() != before);
    CHECK(ids.inspectionRevision() != inspected);
  }
  const auto settled = ids.sceneRevision();
  const auto settledInspection = ids.inspectionRevision();
  CHECK(ids.sceneRevision() == settled);
  CHECK(ids.inspectionRevision() == settledInspection);
}

SCENARIO(
    "automatic dependencies preserve explicit signals and distinct batches",
    "[integration][payload-validity]")
{
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto *layer = scene.defaultLayer();
  auto node = scene.insertChildTransformNode(layer->root());
  auto *delegate = scene.updateDelegate().emplace<PayloadDelegate>();
  const auto before = ids.sceneRevision();
  const auto inspected = ids.inspectionRevision();
  SECTION("explicit controls remain effective")
  {
    scene.signalLayerTransformChanged(layer);
    CHECK(delegate->transform == 1);
    CHECK(delegate->structure == 0);
  }
  SECTION("structural Layer batching coalesces only existing helper signals")
  {
    scene.beginLayerEditBatch();
    scene.insertChildTransformNode(layer->root());
    scene.insertChildTransformNode(layer->root());
    node->value().setEnabled(false);
    CHECK(delegate->structure == 0);
    CHECK(ids.sceneRevision() != before);
    CHECK(ids.inspectionRevision() != inspected);
    scene.endLayerEditBatch();
    CHECK(delegate->structure == 1);
    CHECK(delegate->transform == 0);
    CHECK(delegate->begin == 0);
    CHECK(delegate->end == 0);
  }
  SECTION("update batching delivers helper signals immediately")
  {
    scene.beginUpdateBatch();
    scene.insertChildTransformNode(layer->root());
    scene.insertChildTransformNode(layer->root());
    node->value().setAsTransform(vsr::math::IDENTITY_MAT4);
    CHECK(delegate->structure == 2);
    CHECK(delegate->transform == 0);
    CHECK(delegate->begin == 1);
    CHECK(delegate->end == 0);
    CHECK(ids.sceneRevision() != before);
    scene.endUpdateBatch();
    CHECK(delegate->end == 1);
  }
  CHECK(ids.sceneRevision() != before);
  CHECK(ids.inspectionRevision() != inspected);
  scene.updateDelegate().erase(delegate);
}

SCENARIO(
    "effective configuration and closed policy tokens have distinct scopes",
    "[integration][view-info]")
{
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  ViewRevisionTracker first(authority, ids);
  ViewRevisionTracker second(authority, ids);
  const auto policy = first.capturePolicyRevision();
  CHECK(policy.domain != first.revision().domain);
  CHECK(policy.domain != ids.sceneRevision().domain);
  CHECK(policy != second.capturePolicyRevision());
  first.observeConfiguration("camera-1/device-1/renderer-1/pipeline-1");
  const auto original = first.revision();
  first.observeConfiguration("camera-1/device-1/renderer-1/pipeline-1");
  CHECK(original == first.revision());
  first.observeConfiguration("camera-2/device-1/renderer-1/pipeline-1");
  first.observeConfiguration("camera-1/device-1/renderer-1/pipeline-1");
  CHECK(original != first.revision());
  CHECK(policy == first.capturePolicyRevision());
  IdentityAuthority nextActivation;
  SceneIdentity nextIds(nextActivation, scene);
  ViewRevisionTracker nextView(nextActivation, nextIds);
  CHECK(policy != nextView.capturePolicyRevision());
}

SCENARIO("incremental node dependencies are bound to live domains and chains",
    "[integration][node-dependency]")
{
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto *layer = scene.defaultLayer();
  auto parent = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, vsr::math::IDENTITY_MAT4, "parent"));
  auto node =
      parent->insert_last_child(vsr::scene::LayerNodeData(layer, "target"));
  auto handle = ids.handleOf(node);
  auto observe = [&] {
    auto walk = ids.beginNodeDependencies(handle);
    REQUIRE(walk);
    while (walk->ancestorSlot)
      REQUIRE(ids.advanceNodeDependencies(*walk));
    return *walk;
  };
  const auto before = observe();
  CHECK(before.matchesStructure(observe()));
  CHECK(before.matchesAncestry(observe()));
  SECTION("ancestor change-back and enabled-only writes stay distinct")
  {
    parent->value().setEnabled(false);
    CHECK(before.matchesAncestry(observe()));
    parent->value().setAsTransform(vsr::math::IDENTITY_MAT4);
    CHECK(before.matchesStructure(observe()));
    CHECK_FALSE(before.matchesAncestry(observe()));
  }
  SECTION("clone and relocation do not alias dependencies")
  {
    auto clone = node->container()->copy_subtree(node, parent);
    auto cloneWalk = ids.beginNodeDependencies(ids.handleOf(clone));
    REQUIRE(cloneWalk);
    while (cloneWalk->ancestorSlot)
      REQUIRE(ids.advanceNodeDependencies(*cloneWalk));
    CHECK_FALSE(before.matchesStructure(*cloneWalk));
    node->container()->reserve(1024);
    CHECK(before.matchesAncestry(observe()));
  }
  SECTION("wrong kinds and registrations cannot redirect")
  {
    auto wrong = handle;
    wrong.domain = ids.handleOf(layer).domain;
    CHECK_FALSE(ids.beginNodeDependencies(wrong));
    SceneIdentity other(authority, scene);
    CHECK_FALSE(other.beginNodeDependencies(handle));
    IdentityAuthority launch;
    SceneIdentity otherLaunch(launch, scene);
    CHECK_FALSE(otherLaunch.beginNodeDependencies(handle));
  }
  SECTION("removal and recreation permanently retire the dependency target")
  {
    layer->erase(parent);
    auto replacement = layer->root()->insert_last_child(
        vsr::scene::LayerNodeData(layer, vsr::math::IDENTITY_MAT4, "parent"));
    auto newNode = replacement->insert_last_child(
        vsr::scene::LayerNodeData(layer, "target"));
    CHECK_FALSE(ids.beginNodeDependencies(handle));
    auto retired = before;
    CHECK_FALSE(ids.advanceNodeDependencies(retired));
    handle = ids.handleOf(newNode);
    CHECK_FALSE(before.matchesStructure(observe()));
  }
}

SCENARIO("node edit tokens bind live targets and authority domains",
    "[integration][node-version]")
{
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto *layer = scene.defaultLayer();
  auto node = layer->root()->insert_last_child(
      vsr::scene::LayerNodeData(layer, vsr::math::IDENTITY_MAT4, "target"));
  const auto handle = ids.handleOf(node);
  const auto token = ids.nodeEditVersion(handle);
  REQUIRE(token);
  REQUIRE(ids.isCurrent(*token));
  SECTION("every same-value enabled write is intervening")
  {
    node->value().setEnabled(true);
    CHECK_FALSE(ids.isCurrent(*token));
  }
  SECTION("copy and move construction leave the original destination history")
  {
    auto copy = node->value();
    auto moved = std::move(copy);
    CHECK(ids.isCurrent(*token));
    node->value() = std::move(moved);
    CHECK_FALSE(ids.isCurrent(*token));
  }
  SECTION("moving out of live payload is an intervening source write")
  {
    auto moved = std::move(node->value());
    CHECK_FALSE(ids.isCurrent(*token));
    node->value() = std::move(moved);
    CHECK_FALSE(ids.isCurrent(*token));
  }
  SECTION("erase and reuse never revives a token")
  {
    const auto oldIndex = node.index();
    const auto oldLifetime = node->lifetime();
    REQUIRE_FALSE(node->isRoot());
    node->erase_self();
    CHECK_FALSE(layer->at(oldIndex));
    auto replacement = layer->root()->insert_last_child(
        vsr::scene::LayerNodeData(layer, vsr::math::IDENTITY_MAT4, "target"));
    CHECK(replacement->lifetime() != oldLifetime);
    CHECK(ids.handleOf(replacement) != handle);
    CHECK_FALSE(ids.nodeEditVersion(handle));
    CHECK_FALSE(ids.isCurrent(*token));
  }
  SECTION("layer removal retires token")
  {
    scene.removeLayer("default");
    CHECK_FALSE(ids.isCurrent(*token));
  }
  SECTION("scene reset retires token")
  {
    scene.removeAllObjects();
    scene.removeAllLayers();
    CHECK_FALSE(ids.isCurrent(*token));
  }
  SECTION("foreign registration and launch cannot validate token")
  {
    SceneIdentity registration(authority, scene);
    IdentityAuthority otherAuthority;
    SceneIdentity launch(otherAuthority, scene);
    CHECK_FALSE(registration.isCurrent(*token));
    CHECK_FALSE(launch.isCurrent(*token));
    auto wrongKind = handle;
    wrongKind.domain = ids.handleOf(layer).domain;
    CHECK_FALSE(ids.nodeEditVersion(wrongKind));
  }
}

SCENARIO("Handles never redirect to another object", "[integration]")
{
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);

  auto a = scene.createObject<vsr::scene::Material>("matte");
  auto b = scene.createObject<vsr::scene::Material>("matte");
  const auto ha = ids.handleOf(a.data());
  const auto hb = ids.handleOf(b.data());
  REQUIRE(ha.valid());
  REQUIRE(hb.valid());
  REQUIRE(ha != hb);
  REQUIRE(ids.resolve(ha) == a.data());

  WHEN("an object is removed and its storage index reused")
  {
    const auto oldIndex = a.index();
    scene.removeObject(a.data());
    REQUIRE(ids.resolve(ha) == nullptr);
    REQUIRE_FALSE(ids.parameterVersion(ha));

    auto c = scene.createObject<vsr::scene::Material>("matte");
    REQUIRE(c.index() == oldIndex);
    THEN("the old handle stays invalid and the newcomer gets a fresh one")
    {
      REQUIRE(ids.resolve(ha) == nullptr);
      REQUIRE(ids.handleOf(c.data()).valid());
      REQUIRE(ids.handleOf(c.data()) != ha);
      REQUIRE(ids.resolve(hb) == b.data());
    }
  }

  WHEN("the scene is defragmented")
  {
    b->setName("survivor");
    const auto oldIndexB = b.index();
    scene.removeObject(a.data());
    const auto tokenB = *ids.parameterVersion(hb);
    const auto inspection = ids.inspectionRevision();
    scene.defragmentObjectStorage();
    vsr::scene::Material *moved = nullptr;
    const auto &pool = scene.objectDB().material;
    for (size_t i = 0; i < pool.capacity(); i++) {
      if (auto ref = pool.at(i); ref && ref->name() == "survivor")
        moved = ref.data();
    }
    REQUIRE(moved);
    REQUIRE(moved->index() != oldIndexB);
    THEN("surviving handles remap to the same object")
    {
      REQUIRE(ids.resolve(hb) == moved);
      REQUIRE(ids.handleOf(moved) == hb);
      REQUIRE(ids.isCurrent(tokenB));
      REQUIRE(ids.resolve(ha) == nullptr);
      REQUIRE(ids.inspectionRevision() != inspection);
    }
  }

  WHEN("every object is removed and the scene refilled")
  {
    scene.removeAllObjects();
    auto c = scene.createObject<vsr::scene::Material>("matte");
    THEN("no old handle addresses a new object")
    {
      REQUIRE(ids.resolve(ha) == nullptr);
      REQUIRE(ids.resolve(hb) == nullptr);
      REQUIRE(ids.handleOf(c.data()) != ha);
      REQUIRE(ids.handleOf(c.data()) != hb);
    }
  }

  WHEN("a handle is presented to a different scene's identity")
  {
    Scene other;
    auto o = other.createObject<vsr::scene::Material>("matte");
    SceneIdentity otherIds(authority, other);
    THEN("it does not resolve, even at the same storage index")
    {
      REQUIRE(o.index() == a.index());
      REQUIRE(otherIds.resolve(ha) == nullptr);
      REQUIRE(ids.resolve(otherIds.handleOf(o.data())) == nullptr);
    }
  }
}

SCENARIO("Scene replacement leaves no obsolete handle", "[integration]")
{
  IdentityAuthority authority;
  auto first = std::make_unique<Scene>();
  auto m = first->createObject<vsr::scene::Material>("matte");
  auto ids = std::make_unique<SceneIdentity>(authority, *first);
  const auto oldHandle = ids->handleOf(m.data());
  const auto oldRevision = ids->sceneRevision();
  const auto oldToken = *ids->parameterVersion(oldHandle);
  // Capture everything needed from the first scene's objects before it is
  // destroyed; `m` must not be touched afterwards.
  const auto oldIndex = m.index();
  m = {};

  ids.reset();
  first.reset();
  Scene replacement;
  auto n = replacement.createObject<vsr::scene::Material>("matte");
  SceneIdentity replacementIds(authority, replacement);

  REQUIRE(n.index() == oldIndex);
  REQUIRE(replacementIds.resolve(oldHandle) == nullptr);
  REQUIRE_FALSE(replacementIds.isCurrent(oldToken));
  REQUIRE(replacementIds.handleOf(n.data()) != oldHandle);
  REQUIRE(replacementIds.sceneRevision() != oldRevision);
}

SCENARIO("Earlier-activation identity never authorizes later activations",
    "[integration]")
{
  Scene scene;
  auto m = scene.createObject<vsr::scene::Material>("matte");

  ObjectHandle earlierHandle;
  ParameterVersionToken earlierToken;
  SceneRevision earlierRevision;
  {
    IdentityAuthority earlier;
    SceneIdentity ids(earlier, scene);
    earlierHandle = ids.handleOf(m.data());
    earlierToken = *ids.parameterVersion(earlierHandle);
    earlierRevision = ids.sceneRevision();
  }

  IdentityAuthority later;
  SceneIdentity ids(later, scene);
  REQUIRE(ids.resolve(earlierHandle) == nullptr);
  REQUIRE_FALSE(ids.isCurrent(earlierToken));
  REQUIRE(ids.handleOf(m.data()) != earlierHandle);
  REQUIRE(ids.sceneRevision() != earlierRevision);
}

SCENARIO(
    "Parameter-version tokens track every parameter change", "[integration]")
{
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);

  auto light = scene.createObject<vsr::scene::Light>("directional");
  auto other = scene.createObject<vsr::scene::Light>("directional");
  light->setParameter("irradiance", 1.f);
  light->setParameter("direction", vsr::math::float3(0.f, 0.f, -1.f));
  const auto h = ids.handleOf(light.data());
  const auto original = *ids.parameterVersion(h);

  WHEN("the same parameter changes")
  {
    light->setParameter("irradiance", 2.f);
    THEN("the token is stale")
    {
      REQUIRE_FALSE(ids.isCurrent(original));
    }
  }
  WHEN("another parameter of the same object changes")
  {
    light->setParameter("direction", vsr::math::float3(0.f, -1.f, 0.f));
    THEN("the token is stale")
    {
      REQUIRE_FALSE(ids.isCurrent(original));
    }
  }
  WHEN("a value changes and changes back to the original")
  {
    const auto sceneBefore = ids.sceneRevision();
    light->setParameter("irradiance", 2.f);
    light->setParameter("irradiance", 1.f);
    THEN("neither the token nor the scene revision looks unchanged")
    {
      REQUIRE_FALSE(ids.isCurrent(original));
      REQUIRE(*ids.parameterVersion(h) != original);
      REQUIRE(ids.sceneRevision() != sceneBefore);
    }
  }
  WHEN("a parameter is removed")
  {
    light->removeParameter("direction");
    THEN("the token is stale")
    {
      REQUIRE_FALSE(ids.isCurrent(original));
    }
  }
  WHEN("only another object changes")
  {
    other->setParameter("irradiance", 5.f);
    THEN("the token stays current")
    {
      REQUIRE(ids.isCurrent(original));
    }
  }
  WHEN("only metadata or the label changes")
  {
    const auto sceneBefore = ids.sceneRevision();
    const auto inspectionBefore = ids.inspectionRevision();
    light->setName("key light");
    light->setMetadataValue("note", 1);
    THEN("it is signalled incrementally without a refresh")
    {
      REQUIRE(ids.inspectionRevision() != inspectionBefore);
      REQUIRE_FALSE(ids.refreshMetadata(h));
    }
    THEN("it is not a parameter change but invalidates inspection")
    {
      REQUIRE(ids.isCurrent(original));
      REQUIRE(ids.sceneRevision() == sceneBefore);
      REQUIRE(ids.inspectionRevision() != inspectionBefore);
    }
    THEN("refreshing again without changes is a no-op")
    {
      const auto settled = ids.inspectionRevision();
      REQUIRE_FALSE(ids.refreshAllMetadata());
      REQUIRE(ids.inspectionRevision() == settled);
    }
  }
}

SCENARIO("Scene revisions cover render-relevant content", "[integration]")
{
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto *layer = scene.defaultLayer();
  const auto before = ids.sceneRevision();

  SECTION("object creation")
  {
    scene.createObject<vsr::scene::Geometry>("sphere");
    REQUIRE(ids.sceneRevision() != before);
  }
  SECTION("layer structure")
  {
    scene.insertChildTransformNode(layer->root());
    REQUIRE(ids.sceneRevision() != before);
  }
  SECTION("adding and deactivating layers")
  {
    scene.addLayer("extra");
    const auto added = ids.sceneRevision();
    REQUIRE(added != before);
    scene.setLayerActive("extra", false);
    REQUIRE(ids.sceneRevision() != added);
  }
  SECTION("camera and renderer changes belong to views, not the scene")
  {
    auto camera = scene.createObject<vsr::scene::Camera>("perspective");
    const auto withCamera = ids.sceneRevision();
    camera->setParameter("fovy", 0.5f);
    REQUIRE(ids.sceneRevision() == withCamera);
  }
}

SCENARIO("View revisions cover camera, renderer, dimensions and pipeline",
    "[integration]")
{
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto camera = scene.createObject<vsr::scene::Camera>("perspective");
  auto camera2 = scene.createObject<vsr::scene::Camera>("perspective");
  auto renderer = scene.createRenderer("default", "default");
  auto material = scene.createObject<vsr::scene::Material>("matte");

  ViewRevisionTracker view(authority, ids);
  view.setCamera(ids.handleOf(camera.data()));
  view.setRenderer(ids.handleOf(renderer.get()));
  view.setDimensions(64, 32);
  const auto base = view.revision();
  REQUIRE(view.revision() == base);

  SECTION("camera parameter change, including change-back")
  {
    camera->setParameter("fovy", 0.5f);
    const auto r1 = view.revision();
    REQUIRE(r1 != base);
    camera->setParameter("fovy", 0.5f);
    REQUIRE(view.revision() != r1);
  }
  SECTION("switching camera")
  {
    view.setCamera(ids.handleOf(camera2.data()));
    REQUIRE(view.revision() != base);
  }
  SECTION("renderer parameter change")
  {
    renderer->setParameter("pixelSamples", 4);
    REQUIRE(view.revision() != base);
  }
  SECTION("resize")
  {
    view.setDimensions(64, 64);
    REQUIRE(view.revision() != base);
  }
  SECTION("unchanged dimensions do not bump")
  {
    view.setDimensions(64, 32);
    REQUIRE(view.revision() == base);
  }
  SECTION("pipeline configuration")
  {
    view.notePipelineChanged();
    REQUIRE(view.revision() != base);
  }
  SECTION("unrelated scene content does not bump the view")
  {
    material->setParameter("opacity", 0.5f);
    REQUIRE(view.revision() == base);
  }
}

SCENARIO("Inspection revisions change independently of edit and render",
    "[integration]")
{
  Scene scene;
  IdentityAuthority authority;
  SceneIdentity ids(authority, scene);
  auto m = scene.createObject<vsr::scene::Material>("matte");
  const auto h = ids.handleOf(m.data());
  const auto token = *ids.parameterVersion(h);
  const auto sceneBefore = ids.sceneRevision();
  const auto inspectBefore = ids.inspectionRevision();

  SECTION("registration change")
  {
    ids.noteRegistrationChanged();
    REQUIRE(ids.inspectionRevision() != inspectBefore);
    REQUIRE(ids.sceneRevision() == sceneBefore);
    REQUIRE(ids.isCurrent(token));
  }
  SECTION("metadata change")
  {
    m->setMetadataValue("tag", 3);
    REQUIRE(ids.refreshAllMetadata());
    REQUIRE(ids.inspectionRevision() != inspectBefore);
    REQUIRE(ids.sceneRevision() == sceneBefore);
    REQUIRE(ids.isCurrent(token));
  }
  SECTION("returned parameter values and membership also change it")
  {
    m->setParameter("opacity", 0.25f);
    const auto afterValue = ids.inspectionRevision();
    REQUIRE(afterValue != inspectBefore);
    scene.createObject<vsr::scene::Material>("matte");
    REQUIRE(ids.inspectionRevision() != afterValue);
  }
  SECTION("revision kinds from different scopes never compare equal")
  {
    Scene other;
    SceneIdentity otherIds(authority, other);
    REQUIRE(otherIds.sceneRevision() != ids.sceneRevision());
  }
}

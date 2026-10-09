// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_integration
#include "vsr/integration/content/HdriRemoval.hpp"
// catch
#include "catch.hpp"
// std
#include <functional>

using namespace vsr;

namespace {
struct RemovalFixture
{
  explicit RemovalFixture(bool ownLayer = false);
  scene::Scene scene;
  integration::identity::IdentityAuthority authority;
  integration::identity::SceneIdentity identity{authority, scene};
  integration::content::HdriHandles handles;
  integration::content::HdriRemovalEvidence evidence;
};

RemovalFixture::RemovalFixture(bool ownLayer)
{
  scene.defaultLayer();
  REQUIRE(
      scene.trackImageWrites(std::make_shared<scene::ArrayWriteSequence>()));
  const core::math::float3 data[] = {{8.f, 4.f, 2.f}};
  integration::content::PreparedHdriPublication prepared(identity,
      {},
      "Content Root",
      1,
      1,
      data,
      {},
      ownLayer ? "owned" : scene::Token{});
  handles = prepared.handles();
  REQUIRE(prepared.commit());
  REQUIRE(evidence.seal(identity, handles, ownLayer));
  prepared.notify();
}

struct RemovedObserver : scene::EmptyUpdateDelegate
{
  RemovedObserver(integration::identity::SceneIdentity *ids,
      integration::content::HdriHandles handles)
      : ids(ids), handles(handles)
  {}
  void signalObjectRemoved(const scene::Object *) override;
  integration::identity::SceneIdentity *ids{nullptr};
  integration::content::HdriHandles handles;
  int calls{0};
  bool coherent{true};
  bool fail{false};
};

void RemovedObserver::signalObjectRemoved(const scene::Object *)
{
  ++calls;
  coherent = coherent && !ids->resolve(handles.light)
      && !ids->resolve(handles.radiance) && !ids->resolve(handles.contentRoot)
      && !ids->resolve(handles.lightNode);
  if (fail)
    throw std::runtime_error("removed consumer failure");
}
} // namespace

TEST_CASE("Exact HDRI removal preserves borrowed labels camera and lighting",
    "[integration][content][removal]")
{
  const bool owned = GENERATE(false, true);
  RemovalFixture f(owned);
  auto *baseline = f.scene.defaultLayer();
  auto existing =
      f.scene.createObject<scene::Light>(scene::tokens::light::point);
  auto camera =
      f.scene.createObject<scene::Camera>(scene::tokens::camera::perspective);
  const auto baselineHandle = f.identity.handleOf(existing.data());
  camera->setParameter("position", core::math::float3(1.f, 2.f, 3.f));
  baseline->setName("borrowed label");
  baseline->root()->value().setName("borrowed parent label");
  auto *observer =
      f.scene.updateDelegate().emplace<RemovedObserver>(&f.identity, f.handles);
  REQUIRE(f.evidence.check(f.identity) == nullptr);
  integration::content::PreparedHdriRemoval removal(f.identity, f.evidence);
  REQUIRE(removal.isCurrent());
  REQUIRE(removal.commit());
  CHECK_FALSE(removal.commit());
  removal.notify();
  CHECK(observer->calls == 2);
  CHECK(observer->coherent);
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 1);
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == 0);
  CHECK(f.scene.numberOfLayers() == 1);
  CHECK(baseline->size() == 1);
  CHECK(baseline->name() == "borrowed label");
  CHECK(camera->parameterValueAs<core::math::float3>("position")
      == core::math::float3(1.f, 2.f, 3.f));
  CHECK(f.identity.resolve(baselineHandle) == existing.data());
  CHECK_FALSE(f.identity.resolve(f.handles.contentRoot));
  CHECK_FALSE(f.identity.resolve(f.handles.lightNode));
}

TEST_CASE("Owned HDRI edits permanently conflict without manual signals",
    "[integration][content][removal]")
{
  const int mutation = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
  RemovalFixture f(true);
  auto root = f.identity.resolve(f.handles.contentRoot);
  auto node = f.identity.resolve(f.handles.lightNode);
  auto *light = f.identity.resolve(f.handles.light);
  auto *array =
      dynamic_cast<scene::Array *>(f.identity.resolve(f.handles.radiance));
  auto *layer = f.identity.resolve(f.handles.layer);
  switch (mutation) {
  case 0:
    light->setParameter("scale", 2.f);
    light->setParameter("scale", 1.f);
    break;
  case 1:
    light->setName("new");
    light->setName("");
    break;
  case 2:
    root->value().setName("new");
    root->value().setName("Content Root");
    break;
  case 3:
    root->value().setEnabled(false);
    root->value().setEnabled(true);
    break;
  case 4:
    node->value().setInstanceParameter("user", core::Any(1.f));
    node->value().clearInstanceParameters();
    break;
  case 5:
    layer->setName("new");
    layer->setName("owned");
    break;
  case 6:
    layer->root()->value().setName("new");
    layer->root()->value().setName("root");
    break;
  case 7:
    array->mapAs<core::math::float3>()[0] = {8.f, 4.f, 2.f};
    array->unmap(); // even same-byte ordinary write spends history
    break;
  case 8:
    f.scene.setLayerActive(f.scene.getLayerName(layer), false);
    f.scene.setLayerActive(f.scene.getLayerName(layer), true);
    break;
  case 9:
    auto moved = core::math::IDENTITY_MAT4;
    moved[3].x = 2.f;
    root->value().setAsTransform(moved);
    root->value().setAsTransform(core::math::IDENTITY_MAT4);
    break;
  }
  CHECK(f.evidence.check(f.identity) != nullptr);
  CHECK_THROWS(
      integration::content::PreparedHdriRemoval(f.identity, f.evidence));
  CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 1);
  CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == 1);
  CHECK(layer->size() == 3);
}

TEST_CASE("HDRI removal refuses added children reparenting and outside uses",
    "[integration][content][removal]")
{
  const int mutation = GENERATE(0, 1, 2, 3, 4);
  RemovalFixture f;
  auto root = f.identity.resolve(f.handles.contentRoot);
  auto node = f.identity.resolve(f.handles.lightNode);
  auto *light = f.identity.resolve(f.handles.light);
  auto *array = f.identity.resolve(f.handles.radiance);
  switch (mutation) {
  case 0:
    f.scene.insertChildNode(root, "new child");
    break;
  case 1:
    f.scene.insertChildNode(node, "new child");
    break;
  case 2:
    root->container()->move_subtree(node, root->parent());
    root->container()->move_subtree(node, root);
    break;
  case 3:
    f.scene.insertChildObjectNode(
        root->parent(), light->type(), light->index());
    break;
  case 4:
    f.scene.createObject<scene::Light>(scene::tokens::light::hdri)
        ->setParameterObject("radiance", *array);
    break;
  }
  CHECK(f.evidence.check(f.identity) != nullptr);
  CHECK_THROWS(
      integration::content::PreparedHdriRemoval(f.identity, f.evidence));
  CHECK(f.identity.resolve(f.handles.radiance));
  CHECK(f.identity.resolve(f.handles.light));
}

TEST_CASE("HDRI removal evidence survives physical pool defragmentation",
    "[integration][content][removal][removal-defrag]")
{
  scene::Scene scene;
  scene.defaultLayer();
  REQUIRE(
      scene.trackImageWrites(std::make_shared<scene::ArrayWriteSequence>()));
  auto arrayGap = scene.createArray(ANARI_FLOAT32_VEC3, 1, 1);
  auto lightGap = scene.createObject<scene::Light>(scene::tokens::light::point);
  integration::identity::IdentityAuthority authority;
  integration::identity::SceneIdentity identity(authority, scene);
  const core::math::float3 values[] = {{8.f, 4.f, 2.f}};
  integration::content::PreparedHdriPublication prepared(
      identity, {}, "root", 1, 1, values);
  REQUIRE(prepared.commit());
  integration::content::HdriRemovalEvidence evidence;
  REQUIRE(evidence.seal(identity, prepared.handles(), false));
  prepared.notify();
  scene.removeObject(arrayGap.data());
  scene.removeObject(lightGap.data());
  scene.defragmentObjectStorage();
  REQUIRE(evidence.check(identity) == nullptr);
  integration::content::PreparedHdriRemoval removal(identity, evidence);
  REQUIRE(removal.commit());
  removal.notify();
  CHECK(scene.numberOfObjects(ANARI_LIGHT) == 0);
  CHECK(scene.numberOfObjects(ANARI_ARRAY) == 0);
}

TEST_CASE("HDRI removal evidence and prepared plans revoke on owner teardown",
    "[integration][content][removal]")
{
  integration::content::HdriRemovalEvidence retained;
  std::unique_ptr<integration::content::PreparedHdriRemoval> removal;
  {
    RemovalFixture f(true);
    retained = f.evidence;
    removal = std::make_unique<integration::content::PreparedHdriRemoval>(
        f.identity, retained);
    CHECK(removal->isCurrent());
  }
  CHECK_FALSE(removal->isCurrent());
  CHECK_FALSE(removal->commit());
  CHECK_NOTHROW(removal->notify());
  RemovalFixture next(true);
  CHECK(retained.check(next.identity) != nullptr);
  CHECK(next.evidence.check(next.identity) == nullptr);
}

TEST_CASE("Reused HDRI object and node slots cannot inherit removal authority",
    "[integration][content][removal]")
{
  RemovalFixture f;
  auto root = f.identity.resolve(f.handles.contentRoot);
  const auto index = root.index();
  f.scene.removeNode(root, true);
  auto replacement = f.scene.defaultLayer()->root()->insert_last_child_at(index,
      scene::LayerNodeData(
          f.scene.defaultLayer(), core::math::IDENTITY_MAT4, "Content Root"));
  REQUIRE(replacement);
  CHECK(f.identity.handleOf(replacement) != f.handles.contentRoot);
  CHECK(f.evidence.check(f.identity) != nullptr);
  CHECK_THROWS(
      integration::content::PreparedHdriRemoval(f.identity, f.evidence));
  CHECK(replacement);
}

TEST_CASE("HDRI removal remains committed after notification failure",
    "[integration][content][removal]")
{
  RemovalFixture f(true);
  auto *first =
      f.scene.updateDelegate().emplace<RemovedObserver>(&f.identity, f.handles);
  first->fail = true;
  auto *last =
      f.scene.updateDelegate().emplace<RemovedObserver>(&f.identity, f.handles);
  integration::content::PreparedHdriRemoval removal(f.identity, f.evidence);
  REQUIRE(removal.commit());
  CHECK_THROWS(removal.notify());
  CHECK(removal.committed());
  CHECK(first->calls == 2);
  CHECK(last->calls == 2);
  CHECK(last->coherent);
  CHECK_NOTHROW(removal.notify());
  CHECK(last->calls == 2);
  CHECK(f.scene.numberOfLayers() == 1);
}

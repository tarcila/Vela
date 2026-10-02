// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/integration/identity/Identity.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <memory>

using namespace vsr::integration::identity;
using vsr::scene::Scene;

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

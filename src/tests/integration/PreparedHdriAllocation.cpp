// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_integration
#include "vsr/integration/content/HdriRemoval.hpp"
#include "vsr/integration/content/PreparedHdriPublication.hpp"
#include "vsr/integration/edit/AgentEdit.hpp"
// vsr_scene
#include "vsr/scene/detail/PreparedHdriContent.hpp"
// std
#include <cstdlib>
#include <iostream>
#include <new>

namespace {
bool forbidAllocation = false;
int allocationCountdown = -1;
} // namespace

void *operator new(std::size_t size)
{
  if (forbidAllocation || allocationCountdown == 0)
    throw std::bad_alloc();
  if (allocationCountdown > 0)
    --allocationCountdown;
  if (auto *p = std::malloc(size ? size : 1))
    return p;
  throw std::bad_alloc();
}

void *operator new[](std::size_t size)
{
  return ::operator new(size);
}

void operator delete(void *p) noexcept
{
  std::free(p);
}

void operator delete[](void *p) noexcept
{
  std::free(p);
}

void operator delete(void *p, std::size_t) noexcept
{
  std::free(p);
}

void operator delete[](void *p, std::size_t) noexcept
{
  std::free(p);
}

int main()
{
  using namespace vsr::scene;
  Scene scene;
  auto *layer = scene.defaultLayer();
  vsr::integration::identity::IdentityAuthority authority;
  vsr::integration::identity::SceneIdentity identity(authority, scene);
  const math::float3 data[] = {{8.f, 4.f, 2.f}};
  auto camera = scene.createObject<Camera>(tokens::camera::perspective);
  camera->setParameter("position", math::float3(1.f, 2.f, 3.f));
  const auto cameraHandle = identity.handleOf(camera.data());
  const auto version = identity.parameterVersion(cameraHandle);
  const auto revision = identity.sceneRevision();
  const auto inspection = identity.inspectionRevision();
  int failures = 0;
  int successes = 0;
  for (int allocation = 0; allocation < 160; ++allocation) {
    try {
      allocationCountdown = allocation;
      vsr::integration::content::PreparedHdriPublication abandoned(
          identity, {}, "abandoned", 1, 1, data, {}, "private Layer");
      allocationCountdown = -1;
      ++successes;
    } catch (const std::bad_alloc &) {
      allocationCountdown = -1;
      ++failures;
    }
    if (scene.numberOfLayers() != 1 || scene.numberOfObjects(ANARI_LIGHT) != 0
        || scene.numberOfObjects(ANARI_ARRAY) != 0
        || identity.sceneRevision() != revision
        || identity.inspectionRevision() != inspection
        || identity.parameterVersion(cameraHandle) != version) {
      std::cerr << "Failed HDRI identity preparation changed live evidence\n";
      return 5;
    }
  }
  if (!failures || !successes)
    return 6;
  std::cout << "HDRI preparation allocation sweep: " << failures
            << " failures, " << successes << " abandoned successes\n";
  vsr::integration::content::PreparedHdriPublication prepared(
      identity, layer->root(), "root", 1, 1, data);
  bool committed = false;
  try {
    forbidAllocation = true;
    committed = prepared.commit();
    prepared.notify();
    (void)identity.handleOf(identity.resolve(prepared.handles().contentRoot));
    (void)identity.handleOf(identity.resolve(prepared.handles().lightNode));
    forbidAllocation = false;
  } catch (...) {
    forbidAllocation = false;
    std::cerr << "HDRI commit attempted allocation\n";
    return 1;
  }
  if (!committed || !prepared.committed())
    return 2;
  // An explicit owned Layer swaps only prebuilt ownership, even when the
  // live pools have sparse slots and an unrelated Light remains present.
  auto unrelated = scene.createObject<Light>(tokens::light::point);
  const auto erased = scene.createObject<Light>(tokens::light::point);
  scene.removeObject(erased.data());
  vsr::integration::content::PreparedHdriPublication separate(
      identity, {}, "new root", 1, 1, data, {}, "owned Layer");
  try {
    forbidAllocation = true;
    committed = separate.commit();
    separate.notify();
    (void)identity.handleOf(identity.resolve(separate.handles().contentRoot));
    (void)identity.handleOf(identity.resolve(separate.handles().lightNode));
    forbidAllocation = false;
  } catch (...) {
    forbidAllocation = false;
    std::cerr << "Owned-Layer HDRI commit attempted allocation\n";
    return 3;
  }
  if (!committed || !unrelated
      || !identity.resolve(separate.handles().contentRoot))
    return 4;
  auto sequence = std::make_shared<ArrayWriteSequence>();
  if (!scene.trackImageWrites(sequence))
    return 8;
  vsr::integration::content::HdriRemovalEvidence evidence;
  if (!evidence.seal(identity, prepared.handles(), false))
    return 9;
  vsr::integration::content::PreparedHdriRemoval removal(identity, evidence);
  vsr::integration::edit::RestorationStore store;
  vsr::integration::edit::RestorationRecord record;
  record.ref = {std::string(64, 'a'), std::string(32, 'b')};
  record.image = std::make_shared<vsr::integration::edit::ImageSnapshot>();
  const auto batch = record.ref;
  if (!store.prepareImage(std::move(record)))
    return 11;
  store.publishImage(batch);
  try {
    forbidAllocation = true;
    if (!removal.commit())
      return 10;
    store.setImageState(
        batch, vsr::integration::edit::RestorationStore::ImageState::RESTORED);
    store.release(batch);
    removal.notify();
    forbidAllocation = false;
  } catch (...) {
    forbidAllocation = false;
    std::cerr
        << "HDRI removal attempted allocation or callback before commit\n";
    return 7;
  }
  vsr::integration::content::HdriRemovalEvidence ownedEvidence;
  if (!ownedEvidence.seal(identity, separate.handles(), true))
    return 12;
  vsr::integration::content::PreparedHdriRemoval ownedRemoval(
      identity, ownedEvidence);
  try {
    forbidAllocation = true;
    if (!ownedRemoval.commit())
      return 13;
    ownedRemoval.notify();
    forbidAllocation = false;
  } catch (...) {
    forbidAllocation = false;
    std::cerr << "Owned-Layer HDRI removal attempted allocation\n";
    return 14;
  }
  if (identity.resolve(separate.handles().layer)
      || identity.resolve(separate.handles().contentRoot)
      || !identity.resolve(cameraHandle))
    return 15;
  return 0;
}

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_integration
#include "vsr/integration/content/SurfaceRemoval.hpp"
#include "vsr/integration/content/detail/PreparedSurfaceGraph.hpp"
#include "vsr/rendering/view/FrameObservation.h"
// std
#include <cstdlib>
#include <iostream>
#include <new>

namespace {
int allocationCountdown = -1;
size_t requestedBytes = 0;

std::pmr::vector<std::uint8_t> input()
{
  const std::string header =
      R"({"revision":1,"byte_order":"little","arrays":{"p":{"element_type":"ANARI_FLOAT32_VEC3","count":3,"byte_length":36}},"fields":{"vertex.position":{"action":"preserve","payload":"p","enabled":true,"association":"vertex"}}})";
  std::pmr::vector<std::uint8_t> bytes{'V', 'M', 'E', 'S', 'H', '0', '1', '\n'};
  for (unsigned i = 0; i < 4; ++i)
    bytes.push_back((header.size() >> (i * 8)) & 255);
  bytes.insert(bytes.end(), header.begin(), header.end());
  const float points[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  const auto *data = reinterpret_cast<const std::uint8_t *>(points);
  bytes.insert(bytes.end(), data, data + sizeof(points));
  return bytes;
}
} // namespace

void *operator new(std::size_t size)
{
  requestedBytes += size;
  if (allocationCountdown == 0)
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
  using namespace vsr;
  using namespace integration::content::detail;
  scene::Scene live;
  const auto revision = live.dependencyRevision();
  int failures = 0, successes = 0;
  for (int allocation = 0; allocation < 256; ++allocation) {
    std::unique_ptr<PreparedSurfaceGraph> prepared;
    if (prepareSurfaceGraph(input(), "Prior", {}, prepared))
      return 1;
    const auto *prior = prepared.get();
    auto bytes = input();
    bool failed = false;
    try {
      allocationCountdown = allocation;
      const auto *reason =
          prepareSurfaceGraph(std::move(bytes), "Candidate", {}, prepared);
      allocationCountdown = -1;
      if (reason)
        return 2;
      ++successes;
    } catch (const std::bad_alloc &) {
      allocationCountdown = -1;
      ++failures;
      failed = true;
    }
    if (failed
        && (prepared.get() != prior
            || prepared->root()->value().name() != "Prior"))
      return 3;
    if (!failed && prepared->root()->value().name() != "Candidate")
      return 4;
    if (live.dependencyRevision() != revision
        || live.numberOfObjects(ANARI_SURFACE)
        || live.numberOfObjects(ANARI_ARRAY)
        || live.numberOfObjects(ANARI_GEOMETRY))
      return 5;
  }
  if (!failures || !successes)
    return 6;
  std::cout << "Private Surface ordinary allocation sweep: " << failures
            << " failures, " << successes << " abandoned successes\n";
  std::unique_ptr<PreparedSurfaceGraph> graph;
  if (prepareSurfaceGraph(input(), "Adopted", {}, graph))
    return 7;
  auto budget = std::make_shared<rendering::FrameBudget>(64u << 20);
  const auto reserve = integration::operations::Reservation::from(budget);
  int adoptionFaults = 0, abandoned = 0;
  for (int allocation = 0; allocation < 256; ++allocation) {
    scene::Scene destination;
    destination.defaultLayer();
    if (!destination.trackImageWrites(
            std::make_shared<scene::ArrayWriteSequence>()))
      return 8;
    integration::identity::IdentityAuthority authority;
    integration::identity::SceneIdentity ids(authority, destination);
    const auto revision = ids.sceneRevision();
    try {
      allocationCountdown = allocation;
      integration::content::PreparedSurfacePublication candidate(
          ids, {}, *graph, reserve);
      allocationCountdown = -1;
      ++abandoned;
    } catch (const std::bad_alloc &) {
      allocationCountdown = -1;
      ++adoptionFaults;
    }
    if (destination.numberOfObjects(ANARI_ARRAY)
        || destination.numberOfObjects(ANARI_GEOMETRY)
        || destination.numberOfObjects(ANARI_SURFACE)
        || ids.sceneRevision() != revision)
      return 9;
  }
  std::cout << "Surface adoption allocation sweep: " << adoptionFaults
            << " failures, " << abandoned << " abandoned successes\n";
  if (!adoptionFaults || !abandoned)
    return 10;
  scene::Scene destination;
  destination.defaultLayer();
  if (!destination.trackImageWrites(
          std::make_shared<scene::ArrayWriteSequence>()))
    return 11;
  integration::identity::IdentityAuthority authority;
  integration::identity::SceneIdentity ids(authority, destination);
  core::StorageProfile profile(64u << 20);
  if (!integration::content::PreparedSurfacePublication::profileStorage(
          ids, {}, *graph, false, profile))
    return 18;
  requestedBytes = 0;
  integration::content::PreparedSurfacePublication published(
      ids, {}, *graph, reserve);
  const auto actualPreparation = requestedBytes;
  if (actualPreparation > profile.bytes())
    return 19;
  std::cout << "Surface preparation ordinary-new requested bytes="
            << actualPreparation << ", conservative profile=" << profile.bytes()
            << '\n';
  integration::content::SurfaceRemovalEvidence evidence;
  allocationCountdown = 0;
  if (!published.commit() || !evidence.seal(ids, published.handles(), false))
    return 12;
  published.notify();
  if (!ids.resolve(published.handles().surface)
      || ids.handleOf(ids.resolve(published.handles().geometry))
          != published.handles().geometry
      || ids.handleOf(ids.resolve(published.handles().material))
          != published.handles().material
      || ids.handleOf(ids.resolve(published.handles().contentRoot))
          != published.handles().contentRoot
      || ids.handleOf(ids.resolve(published.handles().surfaceNode))
          != published.handles().surfaceNode
      || ids.handleOf(ids.resolve(published.handles().arrays[0]))
          != published.handles().arrays[0])
    return 13;
  allocationCountdown = -1;
  int removalFaults = 0, removalAbandoned = 0;
  for (int allocation = 0; allocation < 128; ++allocation) {
    try {
      allocationCountdown = allocation;
      integration::content::PreparedSurfaceRemoval candidate(
          ids, evidence, reserve);
      allocationCountdown = -1;
      ++removalAbandoned;
    } catch (const std::bad_alloc &) {
      allocationCountdown = -1;
      ++removalFaults;
    }
    if (evidence.check(ids) || !ids.resolve(published.handles().surface))
      return 14;
  }
  if (!removalFaults || !removalAbandoned)
    return 15;
  integration::content::PreparedSurfaceRemoval removal(ids, evidence, reserve);
  allocationCountdown = 0;
  if (!removal.commit())
    return 16;
  removal.notify();
  if (ids.resolve(published.handles().surface)
      || ids.resolve(published.handles().contentRoot))
    return 17;
  allocationCountdown = -1;
  std::cout
      << "Surface removal allocation sweep: " << removalFaults << " failures, "
      << removalAbandoned
      << " abandoned successes; publication/seal/notify/lookup/removal allocation-forbidden passed\n";
}

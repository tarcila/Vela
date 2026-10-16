// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_integration
#include "vsr/integration/edit/AgentEdit.hpp"
#include "vsr/rendering/view/FrameObservation.h"
// std
#include <cstdlib>
#include <iostream>
#include <new>

namespace {
// Observe actual ID allocation destruction, including vector erase's move
// overwrite and Store teardown. This observes ownership, not a guessed size.
const void *watchedId = nullptr;
bool idFreed = false;
bool releasedEarly = false;
int releases = 0;
struct ScopeCharge
{
  ~ScopeCharge();
};
ScopeCharge::~ScopeCharge()
{
  releasedEarly = releasedEarly || !idFreed;
  ++releases;
}
} // namespace

void *operator new(std::size_t size)
{
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
  if (p && p == watchedId)
    idFreed = true;
  std::free(p);
}
void operator delete[](void *p) noexcept
{
  ::operator delete(p);
}
void operator delete(void *p, std::size_t) noexcept
{
  ::operator delete(p);
}
void operator delete[](void *p, std::size_t) noexcept
{
  ::operator delete(p);
}

static bool control(int kind, bool ageTombstone)
{
  using namespace vsr::integration;
  using namespace edit;
  watchedId = nullptr;
  idFreed = false;
  releasedEarly = false;
  releases = 0;
  auto budget = std::make_shared<vsr::rendering::FrameBudget>(8u << 20);
  std::weak_ptr<const void> scope;
  std::weak_ptr<ImageSnapshot> snapshot;
  const BatchRef first{"launch", std::string(32, 'a')};
  {
    RestorationStore store(4);
    RestorationRecord record;
    record.ref = first;
    record.scene = 1;
    if (kind == 0) {
      record.hdri = std::make_shared<HdriSnapshot>();
      record.hdri->reserveRemoval = operations::Reservation::from(budget);
      record.image = record.hdri;
    } else if (kind == 1) {
      record.image = std::make_shared<ImageSnapshot>();
    } else {
      record.mesh = std::make_shared<MeshSnapshot>();
      record.image = record.mesh;
    }
    record.image->scopeLease = std::make_shared<ScopeCharge>();
    scope = record.image->scopeLease;
    snapshot = record.image;
    watchedId = record.ref.batchId.data();
    if (!store.prepareImage(std::move(record)))
      return false;
    store.publishImage(first);
    // One long-lived eligible record; each newer record is released at once.
    // No live Scene, radiance, giant payload or 1025-live-record fixture.
    for (int i = 0; i != 1025; ++i) {
      RestorationRecord next;
      next.ref = {"launch", std::string(32, 'b') + std::to_string(i)};
      const auto ref = next.ref;
      next.scene = 1;
      next.image = std::make_shared<ImageSnapshot>();
      if (!store.prepareImage(std::move(next)))
        return false;
      store.publishImage(ref);
      store.release(ref);
    }
    if (store.size() != 1 || !store.find(first) || store.imageState(first)
        || scope.expired() || snapshot.expired())
      return false;
    store.expireUnless([](const BatchRef &) { return false; });
    if (!store.expired(first) || store.find(first) || !snapshot.expired()
        || scope.expired() || releases || idFreed) {
      std::cerr << "aged scope expired=" << store.expired(first)
                << " charge_alive=" << !scope.expired()
                << " snapshot_dead=" << snapshot.expired() << '\n';
      return false;
    }
    if (ageTombstone) {
      for (int i = 0; i != 1024; ++i) {
        RestorationRecord next;
        next.ref = {"launch", std::string(32, 'c') + std::to_string(i)};
        if (!store.reserve() || !store.prepareRecord(next.ref))
          return false;
        store.publish(std::move(next), true);
        store.expireUnless([](const BatchRef &) { return false; });
        if (i != 1023 && (!store.expired(first) || scope.expired()))
          return false;
      }
      if (store.expired(first) || !scope.expired() || releases != 1 || !idFreed
          || releasedEarly)
        return false;
    }
  }
  if (!scope.expired() || releases != 1 || !idFreed || releasedEarly)
    return false;
  watchedId = nullptr;
  return true;
}

int main()
{
  for (int kind = 0; kind != 3; ++kind)
    for (bool ageTombstone : {false, true}) {
      if (!control(kind, ageTombstone)) {
        std::cerr << "tombstone kind=" << kind << " age=" << ageTombstone
                  << " released_early=" << releasedEarly << '\n';
        return 1;
      }
      std::cout << "tombstone kind=" << kind << " age=" << ageTombstone
                << " aged_scope_covered=true id_destroyed_before_charge=true\n";
    }
  return 0;
}

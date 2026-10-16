// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_integration
#include "vsr/integration/content/AllocationProfile.hpp"
#include "vsr/integration/content/ContentCreation.hpp"
// std
#include <iostream>

namespace {
struct ReservationService
{
  std::shared_ptr<const void> reserve(size_t) const;
};
std::shared_ptr<const void> ReservationService::reserve(size_t) const
{
  return std::make_shared<int>(1);
}
} // namespace

int main()
{
  using namespace vsr::integration;
  static_assert(!content::allocation_profile::AVAILABLE);
  static_assert(content::allocation_profile::asyncBytes<void>() == 0);
  static_assert(content::allocation_profile::sharedControlBytes<int,
                    std::allocator<int>>()
      == 0);
  vsr::scene::Scene scene;
  Integration integration;
  SceneRegistration registration;
  registration.name = "unsupported profile control";
  registration.scene = &scene;
  registration.grants = {true, true};
  registration.normalSyncHook = [](SceneId) { return true; };
  const auto id = integration.registerScene(registration);
  if (!id || !integration.activate().ok())
    return 1;
  auto authority = session::SessionAuthority::create(integration);
  auto engine = operations::OperationEngine::create(integration, *authority);
  edit::EditService edits(integration, *engine, authority->launchId());
  activity::ActivityLog activity;
  content::ContentCreation creator(integration,
      *engine,
      edits,
      activity,
      operations::Reservation::from(std::make_shared<ReservationService>()));
  mcp::json::Value metadata;
  if (!mcp::json::parse(creator.metadata(), metadata) || !metadata.get("kinds")
      || !metadata.get("kinds")->a.empty())
    return 2;
  content::HdriRequest request;
  request.scene = *id;
  request.fileName = "must-not-be-read.hdr";
  const auto rejected =
      creator.submit({}, {"old session", 1}, std::move(request));
  if (rejected.code != operations::Outcome::INVALID_REQUEST
      || scene.numberOfObjects(ANARI_LIGHT) != 0)
    return 3;
  std::cout
      << "Unsupported HDRI allocation profile compiles, unadvertises and refuses before preparation\n";
}

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_integration
#include "vsr/integration/approval/RecoveryApproval.hpp"
// std
#include <array>
#include <thread>

TEST_CASE("Trusted recovery approval binds exact contents and commits once",
    "[integration][recovery-approval]")
{
  namespace ra = vsr::integration::approval;
  double now = 100.;
  std::size_t held = 0;
  ra::ProposalStore store([&] { return now; }, 8192);
  const auto reserve = [&](std::size_t bytes) -> std::shared_ptr<const void> {
    struct Lease
    {
      std::size_t *held{nullptr};
      std::size_t bytes{0};
      ~Lease()
      {
        *held -= bytes;
      }
    };
    auto lease = std::make_shared<Lease>();
    lease->held = &held;
    lease->bytes = bytes;
    held += bytes;
    return lease;
  };
  ra::Identity identity{"launch", "session", 7, "image_import", 3};
  auto registration = std::make_shared<int>(0);
  REQUIRE(store.offer(identity,
      "typed-layout\0raw"sv,
      4096,
      1024,
      2048,
      registration,
      reserve));
  REQUIRE(store.snapshot());
  CHECK(store.snapshot()->requiredBytes == 4096);
  CHECK(held > 0);
  CHECK_FALSE(store.approve(identity, 4095));
  REQUIRE(store.approve(identity, 4096));
  CHECK_FALSE(store.claim(identity, "typed-layout\0bad"sv, 4096));
  auto claim = store.claim(identity, "typed-layout\0raw"sv, 4096);
  REQUIRE(claim);
  CHECK_FALSE(store.claim(identity, "typed-layout\0raw"sv, 4096));
  CHECK(claim->valid(4096));
  claim.reset(); // precommit cancellation releases the exclusive claim
  claim = store.claim(identity, "typed-layout\0raw"sv, 4096);
  REQUIRE(claim);
  REQUIRE(claim->consume(4096));
  CHECK_FALSE(claim->consume(4096));
  CHECK_FALSE(store.claim(identity, "typed-layout\0raw"sv, 4096));
  CHECK_FALSE(store.snapshot());
  CHECK(held > 0); // claimed storage stays physically charged until release
  claim.reset();
  CHECK(held == 0);
}

TEST_CASE("Trusted proposals expire, revoke and release bounded storage",
    "[integration][recovery-approval]")
{
  namespace ra = vsr::integration::approval;
  double now = 0.;
  ra::ProposalStore store([&] { return now; }, 8192);
  const auto reserve = [](std::size_t) {
    return std::static_pointer_cast<const void>(std::make_shared<int>(0));
  };
  ra::Identity identity{"launch", "session", 7, "mesh_import", 3};
  auto registration = std::make_shared<int>(0);
  REQUIRE(store.offer(
      identity, "canonical", 4096, 1024, 2048, registration, reserve));
  auto foreign = identity;
  foreign.sessionId = "foreign";
  CHECK_FALSE(store.approve(foreign, 4096));
  REQUIRE(store.approve(identity, 4096));
  now = 300.;
  CHECK_FALSE(store.claim(identity, "canonical", 4096));
  CHECK_FALSE(store.snapshot());
  REQUIRE(store.offer(
      identity, "canonical", 4096, 1024, 2048, registration, reserve));
  REQUIRE(store.approve(identity, 4096));
  auto claim = store.claim(identity, "canonical", 4096);
  REQUIRE(claim);
  REQUIRE(store.revoke(identity));
  CHECK_FALSE(claim->valid(4096));
  CHECK_FALSE(claim->consume(4096));
  claim.reset();
  CHECK_FALSE(store.offer(identity,
      std::string(8192, 'x'),
      4096,
      1024,
      2048,
      registration,
      reserve));
  REQUIRE(store.offer(
      identity, "canonical", 4096, 1024, 2048, registration, reserve));
  REQUIRE(store.approve(identity, 4096));
  registration.reset();
  CHECK_FALSE(store.snapshot());
  CHECK_FALSE(store.claim(identity, "canonical", 4096));
}

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

TEST_CASE("Only one concurrent recovery claim can reach commit",
    "[integration][recovery-approval]")
{
  namespace ra = vsr::integration::approval;
  ra::ProposalStore store;
  auto lifetime = std::make_shared<int>(0);
  const ra::Identity identity{"launch", "session", 7, "mesh_import", 3};
  const auto reserve = [lifetime](std::size_t) {
    return std::static_pointer_cast<const void>(lifetime);
  };
  REQUIRE(store.offer(identity, "exact", 4096, 1024, 2048, lifetime, reserve));
  CHECK_FALSE(
      store.offer(identity, "another", 4096, 1024, 2048, lifetime, reserve));
  REQUIRE(store.approve(identity, 4096));
  std::array<std::shared_ptr<ra::ProposalStore::Claim>, 2> claims;
  std::thread left([&] { claims[0] = store.claim(identity, "exact", 4096); });
  std::thread right([&] { claims[1] = store.claim(identity, "exact", 4096); });
  left.join();
  right.join();
  REQUIRE(bool(claims[0]) != bool(claims[1]));
  auto &winner = claims[0] ? claims[0] : claims[1];
  CHECK_FALSE(winner->consume(4097));
  REQUIRE(winner->consume(4096));
  CHECK_FALSE(store.claim(identity, "exact", 4096));
}

TEST_CASE("Activity admits an exact operation permit without raising policy",
    "[integration][recovery-approval]")
{
  namespace ra = vsr::integration::approval;
  activity::Limits limits;
  limits.retentionBytes = 1024;
  limits.hardRetentionBytes = 2048;
  activity::ActivityLog log(limits);
  activity::Entry old;
  old.batchRef = edit::BatchRef{"launch", "old"};
  old.restorationBytes = 1024;
  old.eligibility = activity::Eligibility::ELIGIBLE;
  REQUIRE(log.append(old));
  activity::Entry pending;
  pending.ref = operations::OperationRef{"session", 4};
  pending.kind = activity::OperationKind::IMAGE_IMPORT;
  pending.targets = {session::Target::scene(7)};
  const auto id = log.append(pending);
  REQUIRE(id);
  CHECK_FALSE(log.reserveRestoration(*id, 4096, {"launch", "new"}));
  auto lifetime = std::make_shared<int>(0);
  const ra::Identity identity{"launch", "session", 7, "image_import", 3};
  REQUIRE(log.approvals().offer(
      identity, "exact", 4096, 1024, 2048, lifetime, [lifetime](std::size_t) {
        return std::static_pointer_cast<const void>(lifetime);
      }));
  REQUIRE(log.approvals().approve(identity, 4096));
  const auto permit = log.approvals().claim(identity, "exact", 4096);
  REQUIRE(permit);
  CHECK_FALSE(log.reserveRestoration(*id, 4097, {"launch", "new"}, permit));
  CHECK_FALSE(log.reserveRestoration(*id, 4096, {"foreign", "new"}, permit));
  REQUIRE(log.reserveRestoration(*id, 4096, {"launch", "new"}, permit));
  CHECK(log.eligibility({"launch", "old"}) == activity::Eligibility::ELIGIBLE);
  const auto duplicate = log.append(pending);
  REQUIRE(duplicate);
  CHECK_FALSE(log.reserveRestoration(
      *duplicate, 4096, {"launch", "duplicate"}, permit));
  REQUIRE(permit->consume(4096));
  log.publishRestoration(*id);
  CHECK(log.eligibility({"launch", "old"}) == activity::Eligibility::EXPIRED);
  CHECK(log.eligibility({"launch", "new"}) == activity::Eligibility::ELIGIBLE);
  CHECK(log.usage().restorableBatches == 1);
  CHECK(log.limits().hardRetentionBytes == 2048);
  CHECK(log.limits().retentionBytes == 1024);
}

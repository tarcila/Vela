// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_app
#include "vsr/app/AgentOptions.hpp"
// vsr_integration
#include "vsr/integration/activity/Activity.hpp"
#include "vsr/rendering/view/FrameObservation.h"
// catch
#include <catch.hpp>
// std
#include <limits>
#include <string>

TEST_CASE("Host restoration option is strict and launch-local",
    "[integration][host-options]")
{
  using vsr::app::AgentOptions;
  using vsr::app::parseAgentOptions;
  AgentOptions options;
  const char *baseline[] = {"viewer",
      "--agentIntegration",
      "-gltf",
      "board.gltf",
      "--unknown",
      "other.vsr"};
  REQUIRE(parseAgentOptions(6, baseline, options));
  REQUIRE(options.restorationBytes
      == vsr::integration::activity::Limits{}.retentionBytes);
  CHECK(options.restorationHardBytes == UINT64_C(8589934592));
  CHECK(vsr::integration::activity::Limits{}.hardRetentionBytes
      == UINT64_C(8589934592));
  REQUIRE(
      options.arguments == std::vector<const char *>(baseline, baseline + 6));

  const char *explicitLimit[] = {"viewer",
      "-gltf",
      "board.gltf",
      "--agentRestorationBytes",
      "67108864",
      "--agentIntegration"};
  REQUIRE(parseAgentOptions(6, explicitLimit, options));
  REQUIRE(options.restorationBytes == 67108864);
  REQUIRE(options.arguments
      == std::vector<const char *>({explicitLimit[0],
          explicitLimit[1],
          explicitLimit[2],
          explicitLimit[5]}));

  const std::string maximum =
      std::to_string(std::numeric_limits<std::uint64_t>::max());
  const char *invalidPair[] = {
      "host", "--agentRestorationBytes", maximum.c_str()};
  CHECK_FALSE(parseAgentOptions(3, invalidPair, options));
  const char *maxArgs[] = {"host",
      "--agentRestorationBytes",
      maximum.c_str(),
      "--agentRestorationHardBytes",
      maximum.c_str()};
  REQUIRE(parseAgentOptions(5, maxArgs, options));
  REQUIRE(
      options.restorationBytes == std::numeric_limits<std::uint64_t>::max());
  for (const auto *value : {"0",
           "-1",
           "+1",
           "",
           " 64",
           "64 ",
           "1.5",
           "1e6",
           "64MiB",
           "--agentIntegration",
           "18446744073709551616"}) {
    const char *args[] = {
        "host", "--agentRestorationBytes", value, "board.gltf"};
    INFO(value);
    REQUIRE_FALSE(parseAgentOptions(4, args, options));
  }
  const char *missing[] = {"host", "--agentRestorationBytes"};
  REQUIRE_FALSE(parseAgentOptions(2, missing, options));
  const char *duplicate[] = {
      "host", "--agentRestorationBytes", "1", "--agentRestorationBytes", "2"};
  REQUIRE_FALSE(parseAgentOptions(5, duplicate, options));
  const char *equals[] = {"host", "--agentRestorationBytes=64"};
  REQUIRE_FALSE(parseAgentOptions(2, equals, options));
  const char *one[] = {"host", "--agentRestorationBytes", "1"};
  REQUIRE(parseAgentOptions(3, one, options));
  REQUIRE(options.restorationBytes == 1);
  REQUIRE(parseAgentOptions(6, baseline, options));
  REQUIRE(options.restorationBytes == UINT64_C(4294967296));
  const char *wide[] = {"host", "--agentRestorationBytes", "5000000000"};
  REQUIRE(parseAgentOptions(3, wide, options));
  CHECK(options.restorationBytes == UINT64_C(5000000000));
  vsr::integration::activity::ActivityLog log;
  CHECK(log.usage().retentionBytes == 0);
  CHECK(log.usage().reservedRetentionBytes == 0);
  const auto physicalCap = vsr::app::agentPhysicalByteCap(UINT64_C(4294967296));
  if (sizeof(std::size_t) >= 8)
    CHECK(physicalCap == UINT64_C(17448304640));
  const auto memory =
      std::make_shared<vsr::rendering::FrameBudget>(physicalCap);
  CHECK(memory->used() < 1024);
  CHECK(memory->limit() == physicalCap);
  CHECK(vsr::app::agentPhysicalByteCap(UINT64_MAX)
      == std::numeric_limits<std::size_t>::max());
  const char *hardFirst[] = {"host",
      "--agentRestorationHardBytes",
      "600000",
      "--agentRestorationBytes",
      "120000",
      "board.gltf"};
  REQUIRE(parseAgentOptions(6, hardFirst, options));
  CHECK(options.restorationBytes == 120000);
  CHECK(options.restorationHardBytes == 600000);
  CHECK(options.arguments.size() == 2);
  CHECK(vsr::app::agentPhysicalByteCap(120000, 600000, 3212800) == 4412800);
  for (const auto *value : {"0", "-1", "1.5", "18446744073709551616"}) {
    const char *args[] = {"host", "--agentRestorationHardBytes", value};
    CHECK_FALSE(parseAgentOptions(3, args, options));
  }
  const char *belowSoft[] = {"host", "--agentRestorationHardBytes", "1"};
  CHECK_FALSE(parseAgentOptions(3, belowSoft, options));
  const char *duplicateHard[] = {"host",
      "--agentRestorationHardBytes",
      "8589934592",
      "--agentRestorationHardBytes",
      "8589934592"};
  CHECK_FALSE(parseAgentOptions(5, duplicateHard, options));
  const char *equalsHard[] = {"host", "--agentRestorationHardBytes=8589934592"};
  CHECK_FALSE(parseAgentOptions(2, equalsHard, options));
}

TEST_CASE("Host asset roots are trusted launch options only",
    "[integration][host-options][content-roots]")
{
  using vsr::app::AgentOptions;
  using vsr::app::parseAgentOptions;
  AgentOptions options;
  const char *none[] = {"host", "--agentIntegration"};
  REQUIRE(parseAgentOptions(2, none, options));
  CHECK(options.assetRoots.empty());

  const char *two[] = {"host",
      "--agentIntegration",
      "--agentAssetRoot",
      "/srv/hdri",
      "-gltf",
      "board.gltf",
      "--agentAssetRoot",
      "/home/user/assets"};
  REQUIRE(parseAgentOptions(8, two, options));
  REQUIRE(options.assetRoots == std::vector<const char *>({two[3], two[7]}));
  // Root options are consumed by the host, never handed to the scene parser.
  CHECK(options.arguments
      == std::vector<const char *>({two[0], two[1], two[4], two[5]}));

  for (const auto *value : {"",
           "relative/dir",
           ".",
           "/",
           "/srv/../etc",
           "/srv/./hdri",
           "/srv//hdri",
           "/srv/hdri/",
           "--agentIntegration"}) {
    const char *args[] = {"host", "--agentAssetRoot", value};
    INFO(value);
    CHECK_FALSE(parseAgentOptions(3, args, options));
  }
  const std::string deep = "/" + std::string(4096, 'a');
  const char *tooLong[] = {"host", "--agentAssetRoot", deep.c_str()};
  CHECK_FALSE(parseAgentOptions(3, tooLong, options));
  const char *missing[] = {"host", "--agentAssetRoot"};
  CHECK_FALSE(parseAgentOptions(2, missing, options));
  const char *equals[] = {"host", "--agentAssetRoot=/srv/hdri"};
  CHECK_FALSE(parseAgentOptions(2, equals, options));
  const char *duplicate[] = {
      "host", "--agentAssetRoot", "/srv/hdri", "--agentAssetRoot", "/srv/hdri"};
  CHECK_FALSE(parseAgentOptions(5, duplicate, options));
  const char *many[] = {"host",
      "--agentAssetRoot",
      "/a1",
      "--agentAssetRoot",
      "/a2",
      "--agentAssetRoot",
      "/a3",
      "--agentAssetRoot",
      "/a4",
      "--agentAssetRoot",
      "/a5",
      "--agentAssetRoot",
      "/a6",
      "--agentAssetRoot",
      "/a7",
      "--agentAssetRoot",
      "/a8"};
  REQUIRE(parseAgentOptions(17, many, options));
  CHECK(options.assetRoots.size() == AgentOptions::MAX_ASSET_ROOTS);
  const char *tooMany[] = {"host",
      "--agentAssetRoot",
      "/a1",
      "--agentAssetRoot",
      "/a2",
      "--agentAssetRoot",
      "/a3",
      "--agentAssetRoot",
      "/a4",
      "--agentAssetRoot",
      "/a5",
      "--agentAssetRoot",
      "/a6",
      "--agentAssetRoot",
      "/a7",
      "--agentAssetRoot",
      "/a8",
      "--agentAssetRoot",
      "/a9"};
  CHECK_FALSE(parseAgentOptions(19, tooMany, options));
}

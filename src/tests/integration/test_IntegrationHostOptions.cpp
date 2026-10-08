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

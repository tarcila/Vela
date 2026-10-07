// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_app
#include "vsr/app/AgentOptions.hpp"
// vsr_integration
#include "vsr/integration/activity/Activity.hpp"
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
  REQUIRE(
      options.restorationBytes == vsr::integration::activity::Limits{}.bytes);
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
      std::to_string(std::numeric_limits<std::size_t>::max());
  const char *maxArgs[] = {"host", "--agentRestorationBytes", maximum.c_str()};
  REQUIRE(parseAgentOptions(3, maxArgs, options));
  REQUIRE(options.restorationBytes == std::numeric_limits<std::size_t>::max());
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
  REQUIRE(options.restorationBytes == (16u << 20));
}

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_app
#include "vsr/app/AgentCommandLine.hpp"
// catch
#include "catch.hpp"

TEST_CASE("Branch CLI registers only available real host options",
    "[integration][host-options][cli]")
{
  using namespace vsr::app;
  for (const bool integration : {false, true}) {
    for (const bool mcp : {false, true}) {
      AgentOptions options;
      bool requested = false;
      const char *enable[] = {
          "viewer", "--agentIntegration", "-obj", "one", "two"};
      const auto error = parseAgentViewerCommandLine(
          5, enable, options, requested, integration, mcp);
      CHECK(error.empty() == integration);
      if (integration) {
        CHECK(requested);
        REQUIRE(options.arguments.size() == 4);
        CHECK(std::string(options.arguments[1]) == "-obj");
        CHECK(std::string(options.arguments[3]) == "two");
      }
      const char *roots[] = {
          "viewer", "--agentIntegration", "--agentAssetRoot", "/assets"};
      CHECK(parseAgentViewerCommandLine(
                4, roots, options, requested, integration, mcp)
                .empty()
          == (integration && mcp));
      const auto help =
          viewerCommandLineHelp(agentCommandLineOptions(integration, mcp));
      CHECK((help.find("--agentIntegration") != std::string::npos)
          == integration);
      CHECK((help.find("--agentAssetRoot") != std::string::npos)
          == (integration && mcp));
    }
  }
}

TEST_CASE("Branch CLI reuses typed bounds and refuses dormant configuration",
    "[integration][host-options][cli]")
{
  using namespace vsr::app;
  AgentOptions options;
  bool requested = false;
  const char *valid[] = {"viewer",
      "--agentIntegration",
      "--agentRestorationBytes",
      "12",
      "--agentRestorationHardBytes",
      "24",
      "--agentAssetRoot",
      "/assets",
      "--agentAssetRoot",
      "/other"};
  REQUIRE(parseAgentViewerCommandLine(10, valid, options, requested, true, true)
          .empty());
  CHECK(options.restorationBytes == 12);
  CHECK(options.restorationHardBytes == 24);
  CHECK(options.assetRoots.size() == 2);
  for (const auto *number :
      {"0", "-1", "1x", "18446744073709551616", "", "1.5"}) {
    const char *invalid[] = {
        "viewer", "--agentIntegration", "--agentRestorationBytes", number};
    CHECK_FALSE(
        parseAgentViewerCommandLine(4, invalid, options, requested, true, true)
            .empty());
  }
  for (const auto *root :
      {"relative", "/", "/assets/", "/assets/../other", "/assets//other"}) {
    const char *invalid[] = {
        "viewer", "--agentIntegration", "--agentAssetRoot", root};
    CHECK_FALSE(
        parseAgentViewerCommandLine(4, invalid, options, requested, true, true)
            .empty());
  }
  const char *inert[] = {"viewer", "--agentAssetRoot", "/assets"};
  CHECK_FALSE(
      parseAgentViewerCommandLine(3, inert, options, requested, true, true)
          .empty());
  const char *missing[] = {"viewer", "--agentIntegration", "--agentAssetRoot"};
  CHECK_FALSE(
      parseAgentViewerCommandLine(3, missing, options, requested, true, true)
          .empty());
  const char *unknown[] = {"viewer", "--agentIntegration", "--agentUnknown"};
  CHECK_FALSE(
      parseAgentViewerCommandLine(3, unknown, options, requested, true, true)
          .empty());
  const char *pair[] = {
      "viewer", "--agentIntegration", "--agentRestorationHardBytes", "1"};
  CHECK_FALSE(
      parseAgentViewerCommandLine(4, pair, options, requested, true, true)
          .empty());
  const char *duplicate[] = {"viewer",
      "--agentIntegration",
      "--agentAssetRoot",
      "/assets",
      "--agentAssetRoot",
      "/assets"};
  CHECK_FALSE(
      parseAgentViewerCommandLine(6, duplicate, options, requested, true, true)
          .empty());
}

TEST_CASE("Delimiter protects literal host-option filenames in every pass",
    "[integration][host-options][cli]")
{
  using namespace vsr::app;
  const char *args[] = {"viewer",
      "-obj",
      "--",
      "--agentIntegration",
      "--agentAssetRoot",
      "--agentRestorationBytes",
      "-h"};
  AgentOptions options;
  bool requested = true;
  REQUIRE(parseAgentViewerCommandLine(7, args, options, requested, false, false)
          .empty());
  CHECK_FALSE(requested);
  CHECK(options.assetRoots.empty());
  REQUIRE(options.arguments.size() == 7);
  CHECK(std::string(options.arguments[3]) == "--agentIntegration");
  CHECK_FALSE(viewerHelpRequested(7, args));
  const char *baseline[] = {"viewer", "--agentIntegration"};
  CHECK_FALSE(validateViewerCommandLine(2, baseline).empty());
}

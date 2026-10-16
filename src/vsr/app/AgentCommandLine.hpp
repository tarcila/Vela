// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_app
#include "AgentOptions.hpp"
#include "ViewerCommandLine.hpp"

namespace vsr::app {

#ifdef VSR_USE_INTEGRATION
inline constexpr bool AGENT_CLI_INTEGRATION = true;
#else
inline constexpr bool AGENT_CLI_INTEGRATION = false;
#endif
#ifdef VSR_USE_INTEGRATION_MCP
inline constexpr bool AGENT_CLI_MCP = true;
#else
inline constexpr bool AGENT_CLI_MCP = false;
#endif

inline std::vector<CommandLineOption> agentCommandLineOptions(
    bool integration = AGENT_CLI_INTEGRATION, bool mcp = AGENT_CLI_MCP)
{
  std::vector<CommandLineOption> options;
  if (!integration)
    return options;
  options = {{"--agentIntegration",
                 false,
                 "Enable launch-local agent integration (viewer opt-in)"},
      {"--agentRestorationBytes",
          true,
          "Positive decimal soft retention bytes (default 4294967296); requires --agentIntegration in viewer"},
      {"--agentRestorationHardBytes",
          true,
          "Positive decimal single-entry cap (default 8589934592), >= soft; requires --agentIntegration in viewer"}};
  if (mcp)
    options.push_back({"--agentAssetRoot",
        true,
        "Trusted host absolute read directory; repeat up to 8, no duplicates; requires --agentIntegration in viewer"});
  return options;
}

inline std::string agentCommandLineHelpNotes()
{
  return "\nHost options use separate values, not --key=value. Retention options\n"
         "may each occur once; positive decimal uint64 bytes, hard >= soft.\n"
         "Asset roots are trusted host grants, not agent-provided paths. An agent\n"
         "can select registered root ids, never grant directory access. Ordinary\n"
         "operator folders do not need chmod 0700 and are never chmod'd; only\n"
         "the launch-private working-input namespace requires private 0700 mode.\n"
         "Root arguments must be absolute, <= 4096 bytes, without empty, . or ..\n"
         "components or a trailing slash. Startup also checks the actual directory.\n";
}

inline std::string agentOptionAvailabilityError(
    int argc, const char *const argv[], bool integration, bool mcp)
{
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--")
      break;
    if (!integration
        && (arg == "--agentIntegration" || arg == "--agentRestorationBytes"
            || arg == "--agentRestorationHardBytes"
            || arg == "--agentAssetRoot"))
      return std::string(arg)
          + " is unavailable: built without agent integration";
    if (!mcp && arg == "--agentAssetRoot")
      return "--agentAssetRoot is unavailable: built without MCP content tools";
  }
  return {};
}

inline std::string agentOptionsValueError()
{
  return "Invalid host options: --agentRestorationBytes and "
         "--agentRestorationHardBytes each require one positive decimal uint64 "
         "byte count, hard >= soft; --agentAssetRoot requires one absolute "
         "directory (up to 8, no duplicates, no trailing slash or . / .. components)";
}

// The baseline owns recognition/order; AgentOptions remains authoritative for
// root and byte-count semantics. No filesystem or application side effects.
inline std::string parseAgentViewerCommandLine(int argc,
    const char *const argv[],
    AgentOptions &options,
    bool &requested,
    bool integration = AGENT_CLI_INTEGRATION,
    bool mcp = AGENT_CLI_MCP)
{
  if (const auto error =
          agentOptionAvailabilityError(argc, argv, integration, mcp);
      !error.empty())
    return error;
  if (const auto error = validateViewerCommandLine(
          argc, argv, agentCommandLineOptions(integration, mcp));
      !error.empty())
    return error;
  AgentOptions parsed;
  if (!parseAgentOptions(argc, argv, parsed))
    return agentOptionsValueError();
  bool enabled = false, literal = false;
  std::vector<const char *> arguments;
  for (std::size_t i = 0; i < parsed.arguments.size(); ++i) {
    const std::string_view arg(parsed.arguments[i]);
    if (i > 0 && !literal && arg == "--agentIntegration")
      enabled = true;
    else {
      arguments.push_back(parsed.arguments[i]);
      if (i > 0 && arg == "--")
        literal = true;
    }
  }
  // Refuse inert host configuration rather than silently accepting it.
  if (!enabled && parsed.arguments.size() != std::size_t(argc))
    return "Host root/retention options require --agentIntegration in vsrViewer";
  parsed.arguments = std::move(arguments);
  options = std::move(parsed);
  requested = enabled;
  return {};
}

inline std::string agentHeadlessCommandLineHelp(bool mcp = AGENT_CLI_MCP)
{
  std::string text =
      "Usage: vsrTutorialIntegrationHeadless [host-options]\n"
      "  --help, -h  Print help before Scene/device/runtime creation\n"
      "  --exit-after-activation  Activate the isolated host then exit\n";
  for (const auto &option : agentCommandLineOptions(true, mcp)) {
    if (option.name == "--agentIntegration")
      continue;
    text += "  " + std::string(option.name) + " <value>  "
        + std::string(option.description) + '\n';
  }
  return text
      + "Integration is implicit in this headless host; no scene-file arguments.\n"
      + agentCommandLineHelpNotes();
}

} // namespace vsr::app

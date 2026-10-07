// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstddef>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace vsr::app {

// Host-only, launch-local configuration. Filter before any scene-file parser;
// leave unrelated arguments untouched for the application's existing parser.
struct AgentOptions
{
  std::size_t restorationBytes{16u << 20};
  std::vector<const char *> arguments;
};

inline bool parseAgentOptions(
    int argc, const char *const argv[], AgentOptions &options)
{
  AgentOptions parsed;
  bool seen = false;
  for (int i = 0; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (i == 0 || arg != "--agentRestorationBytes") {
      // Refuse equal-sign forms of our option, not baseline flags.
      if (i > 0 && arg.substr(0, 24) == "--agentRestorationBytes=")
        return false;
      parsed.arguments.push_back(argv[i]);
      continue;
    }
    if (seen || ++i == argc)
      return false;
    seen = true;
    const std::string_view value(argv[i]);
    std::size_t bytes = 0;
    if (value.empty())
      return false;
    for (const char digit : value) {
      if (digit < '0' || digit > '9')
        return false;
      const auto n = std::size_t(digit - '0');
      if (bytes > (std::numeric_limits<std::size_t>::max() - n) / 10)
        return false;
      bytes = bytes * 10 + n;
    }
    if (bytes == 0)
      return false;
    parsed.restorationBytes = bytes;
  }
  options = std::move(parsed);
  return true;
}

} // namespace vsr::app

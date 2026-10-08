// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace vsr::app {

// Host-only, launch-local configuration. Filter before any scene-file parser;
// leave unrelated arguments untouched for the application's existing parser.
struct AgentOptions
{
  std::uint64_t restorationBytes{UINT64_C(4294967296)};
  std::vector<const char *> arguments;
};

// Independent physical ownership headroom for retained + prepared recovery
// and the unchanged 256 MiB transient work allowance. Saturate to addressable
// size on narrow hosts; policy/configuration itself remains 64-bit.
inline std::size_t agentPhysicalByteCap(std::uint64_t softBytes)
{
  constexpr std::size_t HEADROOM = 256u << 20;
  constexpr auto MAX = std::numeric_limits<std::size_t>::max();
  return softBytes > (MAX - HEADROOM) / 2
      ? MAX
      : std::size_t(softBytes) * 2 + HEADROOM;
}

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
    std::uint64_t bytes = 0;
    if (value.empty())
      return false;
    for (const char digit : value) {
      if (digit < '0' || digit > '9')
        return false;
      const auto n = std::uint64_t(digit - '0');
      if (bytes > (std::numeric_limits<std::uint64_t>::max() - n) / 10)
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

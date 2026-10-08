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
  std::uint64_t restorationHardBytes{UINT64_C(8589934592)};
  std::vector<const char *> arguments;
};

// Independent physical ownership headroom for retained + prepared recovery,
// including two exceptional entries, plus transient work. This is a lease
// ceiling, not an allocation or a promise that pinned owners can always fit.
// Saturate to addressable size on narrow hosts; policy remains 64-bit.
inline std::size_t agentPhysicalByteCap(std::uint64_t softBytes,
    std::uint64_t hardBytes = UINT64_C(8589934592),
    std::size_t transientBytes = 256u << 20)
{
  constexpr auto MAX = std::numeric_limits<std::size_t>::max();
  const auto entryBytes = softBytes > hardBytes ? softBytes : hardBytes;
  return entryBytes > (MAX - transientBytes) / 2
      ? MAX
      : std::size_t(entryBytes) * 2 + transientBytes;
}

inline bool parseAgentOptions(
    int argc, const char *const argv[], AgentOptions &options)
{
  AgentOptions parsed;
  bool seenSoft = false, seenHard = false;
  for (int i = 0; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    const bool hard = arg == "--agentRestorationHardBytes";
    if (i == 0 || (arg != "--agentRestorationBytes" && !hard)) {
      // Refuse equal-sign forms of our options, not baseline flags.
      if (i > 0
          && (arg.substr(0, 24) == "--agentRestorationBytes="
              || arg.substr(0, 28) == "--agentRestorationHardBytes="))
        return false;
      parsed.arguments.push_back(argv[i]);
      continue;
    }
    auto &seen = hard ? seenHard : seenSoft;
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
    (hard ? parsed.restorationHardBytes : parsed.restorationBytes) = bytes;
  }
  if (parsed.restorationHardBytes < parsed.restorationBytes)
    return false;
  options = std::move(parsed);
  return true;
}

} // namespace vsr::app

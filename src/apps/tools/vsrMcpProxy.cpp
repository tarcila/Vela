// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// Trusted stdio MCP adapter for Pi; see McpProxy.hpp and README.md.

#include "McpProxy.hpp"
// std
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
// posix
#include <fcntl.h>
#include <unistd.h>

namespace {

int g_wake[2] = {-1, -1};

void onSignal(int)
{
  const char c = 0;
  [[maybe_unused]] auto n = ::write(g_wake[1], &c, 1);
}

void usage(const char *argv0)
{
  std::fprintf(stderr,
      "usage: %s [<descriptor.json>]\n"
      "  The descriptor path may also come from $%s.\n",
      argv0,
      vsr::tools::mcp_proxy::descriptorEnv);
}

} // namespace

int main(int argc, const char *argv[])
{
  namespace desc = vsr::integration::descriptor;
  std::string path;
  if (argc == 2 && std::strcmp(argv[1], "--help") != 0
      && std::strcmp(argv[1], "-h") != 0)
    path = argv[1];
  else if (argc == 1) {
    if (const char *e = std::getenv(vsr::tools::mcp_proxy::descriptorEnv))
      path = e;
  }
  if (path.empty()) {
    usage(argv[0]);
    return 2;
  }

  desc::Descriptor d;
  if (auto s = desc::read(path, d); !s.ok()) {
    // Reasons never carry descriptor values.
    std::fprintf(stderr,
        "vsrMcpProxy: invalid descriptor (%s): %s\n",
        desc::codeName(s.code),
        s.reason.c_str());
    return 1;
  }

  if (::pipe2(g_wake, O_CLOEXEC | O_NONBLOCK) != 0)
    return 1;
  struct sigaction sa{};
  sa.sa_handler = onSignal;
  ::sigemptyset(&sa.sa_mask);
  ::sigaction(SIGINT, &sa, nullptr);
  ::sigaction(SIGTERM, &sa, nullptr);
  ::sigaction(SIGHUP, &sa, nullptr);
  std::signal(SIGPIPE, SIG_IGN);

  vsr::tools::mcp_proxy::Proxy proxy(std::move(d));
  return proxy.run(STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, g_wake[0]);
}

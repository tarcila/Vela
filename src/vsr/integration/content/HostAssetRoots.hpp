// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/content/AssetRoots.hpp"
#include "vsr/integration/descriptor/LaunchDescriptor.hpp"
// std
#include <string>
#include <vector>

namespace vsr::integration::content {

/* Trusted host startup wiring for Content Creation read authority.
 *
 * Two kinds of root, both host controls:
 *  - HOST_STARTUP: absolute directories named once at launch (the application's
 *    repeatable `--agentAssetRoot`). They are ordinary operator folders: their
 *    mode and ownership are read, never changed, and they are NOT required to
 *    be 0700.
 *  - LAUNCH_PRIVATE: this launch's own working input namespace, created mode
 *    0700 beneath the validated per-user runtime directory and removed with
 *    the launch. It is private by construction and validated as such.
 *
 * Neither kind is reachable from an agent request: no MCP field, environment
 * variable or asset metadata can add, rename or widen a root. The agent may
 * only select an already-approved root id plus a relative file name, which the
 * kernel resolves beneath the pinned directory fd.
 */
struct HostAssetRootReport
{
  std::vector<std::string> failures; // bounded operator diagnostics, no paths
  size_t startupApproved{0};
  bool launchPrivateApproved{false};
  std::string launchPrivatePath; // host-side operator information only
  descriptor::RuntimeDirectory launchPrivate; // pinned until host teardown
};

// Best-effort teardown of this launch's own empty working input directory.
// Operator content is never deleted: a non-empty directory is left in place.
inline void releaseLaunchPrivateInput(HostAssetRootReport &report)
{
#if defined(__linux__)
  const auto path = report.launchPrivatePath;
  report.launchPrivate = {};
  report.launchPrivatePath.clear();
  report.launchPrivateApproved = false;
  if (!path.empty())
    (void)::rmdir(path.c_str());
#else
  (void)report;
#endif
}

// `runtime` must already be an opened, validated private runtime directory.
inline HostAssetRootReport configureHostAssetRoots(AssetRoots &roots,
    const std::vector<const char *> &startupRoots,
    const descriptor::RuntimeDirectory &runtime,
    const std::string &launchId)
{
  HostAssetRootReport report;
  size_t index = 0;
  for (const auto *directory : startupRoots) {
    const std::string path(directory ? directory : "");
    const auto id = roots.approve(path,
        AssetRoots::Kind::HOST_STARTUP,
        "startup-" + std::to_string(index));
    if (id)
      report.startupApproved++;
    else
      report.failures.push_back(
          "asset root " + std::to_string(index) + " refused");
    index++;
  }
  if (!runtime.valid() || launchId.empty())
    return report;
  descriptor::DirectoryOptions options;
  options.create = true;
  descriptor::RuntimeDirectory working;
  const auto path = runtime.path() + "/content-" + launchId;
  if (auto status = descriptor::openDirectory(path, options, working);
      !status.ok()) {
    report.failures.push_back(
        "launch-private input directory: " + status.reason);
    return report;
  }
  // openDirectory already requires mode 0700, effective-user ownership and no
  // ACL grants; approve() independently rechecks both for LAUNCH_PRIVATE.
  if (roots.approve(
          working.path(), AssetRoots::Kind::LAUNCH_PRIVATE, "launch-private")) {
    report.launchPrivateApproved = true;
    report.launchPrivatePath = working.path();
    report.launchPrivate = std::move(working);
  } else {
    report.failures.push_back("launch-private input directory refused");
  }
  return report;
}

} // namespace vsr::integration::content

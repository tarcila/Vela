// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_integration
#include "vsr/integration/content/HostAssetRoots.hpp"
// catch
#include <catch.hpp>
// posix
#include <sys/stat.h>
#include <unistd.h>
// std
#include <cstdlib>
#include <fstream>
#include <string>

using namespace vsr::integration;

namespace {

struct RootsTempDir
{
  std::string path;
  RootsTempDir()
  {
    char tmpl[] = "/tmp/vsr-content-roots-XXXXXX";
    REQUIRE(::mkdtemp(tmpl) != nullptr);
    path = tmpl; // mkdtemp creates 0700
  }
  ~RootsTempDir()
  {
    const auto cmd = "rm -rf '" + path + "'";
    (void)std::system(cmd.c_str());
  }
  std::string sub(const std::string &name, mode_t mode) const
  {
    const auto p = path + "/" + name;
    REQUIRE(::mkdir(p.c_str(), 0700) == 0);
    REQUIRE(::chmod(p.c_str(), mode) == 0);
    return p;
  }
};

void writeHdr(const std::string &path)
{
  std::ofstream file(path, std::ios::binary);
  file << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n";
  const unsigned char texel[4] = {128, 64, 32, 129};
  file.write(reinterpret_cast<const char *>(texel), 4);
}

} // namespace

SCENARIO("Trusted host asset roots do not require private operator folders",
    "[integration][content][content-roots]")
{
  RootsTempDir tmp;
  // A perfectly ordinary, group-readable operator asset folder.
  const auto shared = tmp.sub("assets", 0755);
  writeHdr(shared + "/sky.hdr");
  struct stat before{};
  REQUIRE(::stat(shared.c_str(), &before) == 0);

  content::AssetRoots roots;
  const auto id = roots.approve(
      shared, content::AssetRoots::Kind::HOST_STARTUP, "startup-0");
  REQUIRE(id);
  struct stat after{};
  REQUIRE(::stat(shared.c_str(), &after) == 0);
  // Approval neither chmods nor chowns the operator's own directory.
  CHECK((after.st_mode & 07777) == (before.st_mode & 07777));
  CHECK(after.st_uid == before.st_uid);
  REQUIRE(roots.read(*id, "sky.hdr"));

  // The launch-private kind is the one that must be private. Use a distinct
  // shared directory so this refusal cannot come from duplicate detection.
  const auto sharedWorking = tmp.sub("shared-working", 0755);
  CHECK_FALSE(roots.approve(sharedWorking,
      content::AssetRoots::Kind::LAUNCH_PRIVATE,
      "launch-private"));
  // ... and the same directory is still acceptable as an ordinary host root.
  REQUIRE(roots.approve(
      sharedWorking, content::AssetRoots::Kind::HOST_STARTUP, "startup-1"));
  const auto priv = tmp.sub("private", 0700);
  const auto privateId = roots.approve(
      priv, content::AssetRoots::Kind::LAUNCH_PRIVATE, "launch-private");
  REQUIRE(privateId);

  const auto profiles = roots.profiles();
  REQUIRE(profiles.size() == 3);
  CHECK(profiles[0].id == *id);
  CHECK(profiles[0].kind == content::AssetRoots::Kind::HOST_STARTUP);
  CHECK(profiles[0].label == "startup-0");
  CHECK(profiles[0].approved);
  CHECK(profiles[1].kind == content::AssetRoots::Kind::HOST_STARTUP);
  CHECK(profiles[2].kind == content::AssetRoots::Kind::LAUNCH_PRIVATE);
  REQUIRE(roots.revoke(*privateId));
  CHECK_FALSE(roots.profiles()[*privateId].approved);
  CHECK(roots.profiles()[0].approved);
  // Labels are bounded host strings; no path is ever published.
  CHECK_FALSE(roots.approve(tmp.sub("other", 0755),
      content::AssetRoots::Kind::HOST_STARTUP,
      std::string(65, 'x')));
}

SCENARIO("Host startup wiring pins startup roots and a private working input",
    "[integration][content][content-roots]")
{
  RootsTempDir tmp;
  const auto runtimePath = tmp.sub("runtime", 0700);
  const auto shared = tmp.sub("assets", 0755);
  writeHdr(shared + "/sky.hdr");
  descriptor::RuntimeDirectory runtime;
  REQUIRE(descriptor::openDirectory(runtimePath, {}, runtime).ok());

  content::AssetRoots roots;
  std::vector<const char *> startup{shared.c_str()};
  auto report =
      content::configureHostAssetRoots(roots, startup, runtime, "abcd1234");
  for (const auto &failure : report.failures)
    INFO(failure);
  CHECK(report.failures.empty());
  REQUIRE(report.startupApproved == 1);
  REQUIRE(report.launchPrivateApproved);
  REQUIRE(roots.size() == 2);

  // The launch-private namespace exists, is 0700 and belongs to this launch.
  struct stat working{};
  REQUIRE(::stat(report.launchPrivatePath.c_str(), &working) == 0);
  CHECK((working.st_mode & 07777) == 0700);
  CHECK(working.st_uid == ::geteuid());
  CHECK(report.launchPrivatePath.find("content-abcd1234") != std::string::npos);

  // A file placed in the private namespace is readable through its root id;
  // nothing outside either root is reachable, by traversal or absolute path.
  writeHdr(report.launchPrivatePath + "/work.hdr");
  REQUIRE(roots.read(1, "work.hdr"));
  CHECK_FALSE(roots.read(1, "../assets/sky.hdr"));
  CHECK_FALSE(roots.read(0, shared + "/sky.hdr"));
  CHECK_FALSE(roots.read(0, "../private/work.hdr"));
  REQUIRE(::symlink((shared + "/sky.hdr").c_str(),
              (report.launchPrivatePath + "/link.hdr").c_str())
      == 0);
  CHECK_FALSE(roots.read(1, "link.hdr"));
  CHECK(::unlink((report.launchPrivatePath + "/link.hdr").c_str()) == 0);

  // Teardown releases the launch's own empty namespace, never operator data.
  CHECK(::unlink((report.launchPrivatePath + "/work.hdr").c_str()) == 0);
  const auto path = report.launchPrivatePath;
  content::releaseLaunchPrivateInput(report);
  CHECK_FALSE(report.launchPrivateApproved);
  struct stat gone{};
  CHECK(::stat(path.c_str(), &gone) != 0);
  struct stat operatorFolder{};
  REQUIRE(::stat(shared.c_str(), &operatorFolder) == 0);
  CHECK((operatorFolder.st_mode & 07777) == 0755);
}

SCENARIO("Refused startup roots are reported, not silently dropped",
    "[integration][content][content-roots]")
{
  RootsTempDir tmp;
  const auto runtimePath = tmp.sub("runtime", 0700);
  descriptor::RuntimeDirectory runtime;
  REQUIRE(descriptor::openDirectory(runtimePath, {}, runtime).ok());
  content::AssetRoots roots;
  const auto missing = tmp.path + "/absent";
  std::vector<const char *> startup{missing.c_str()};
  auto report =
      content::configureHostAssetRoots(roots, startup, runtime, "abcd1234");
  CHECK(report.startupApproved == 0);
  REQUIRE(report.failures.size() == 1);
  // Diagnostics identify the option position, never the operator's path.
  CHECK(report.failures.front().find(tmp.path) == std::string::npos);
  CHECK(report.launchPrivateApproved);
  content::releaseLaunchPrivateInput(report);
}

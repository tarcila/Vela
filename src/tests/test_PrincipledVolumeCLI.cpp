// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
#include "vsr/app/Context.h"
#include "vsr/scene/AnariHandleCache.hpp"
#include <fstream>

TEST_CASE("Principled CLI blocks group one medium and snapshot layers", "[App]")
{
  vsr::app::Context c;
  std::vector<std::string> args = {"app",
      "-volume",
      "legacy.raw",
      "-l",
      "smoke",
      "-principled_volume",
      "density=smoke.nvdb",
      "density:value=0.5",
      "anisotropy=-0.3",
      "-v",
      "temperature=heat.nvdb",
      "-l",
      "other",
      "-principled_volume",
      "bounds=0,0,0,1,1,1",
      "-volume_animation",
      "a.raw",
      "b.raw"};
  c.parseCommandLine(args);
  REQUIRE(c.commandLine.sceneInputs.size() == 3);
  auto &a = std::get<vsr::app::PrincipledVolumeSceneImport>(
      c.commandLine.sceneInputs[1]);
  REQUIRE(a.layerName == "smoke");
  REQUIRE(a.options.fields.at("temperature").file == "heat.nvdb");
  REQUIRE(std::get<float>(a.options.parameters.at("density")) == 0.5f);
  REQUIRE(std::get<float>(a.options.parameters.at("anisotropy")) == -0.3f);
  REQUIRE(std::get<vsr::app::PrincipledVolumeSceneImport>(
              c.commandLine.sceneInputs[2])
              .layerName
      == "other");
  REQUIRE(c.commandLine.animationFilenames[0].second.size() == 2);
  REQUIRE_NOTHROW(c.setupSceneFromCommandLine(true));
}

TEST_CASE("Principled CLI rejects incomplete and ambiguous blocks", "[App]")
{
  for (const auto &tail :
      std::vector<std::vector<std::string>>{{"-principled_volume"},
          {"-principled_volume_json"},
          {"-principled_volume", "density=x", "density:field=y"},
          {"-principled_volume", "-not-an-option"},
          {"-principled_volume", "smoke.nvdb"}}) {
    vsr::app::Context c;
    std::vector<std::string> args{"app"};
    args.insert(args.end(), tail.begin(), tail.end());
    REQUIRE_THROWS(c.parseCommandLine(args));
  }
}

TEST_CASE("Principled CLI JSON creates a complete standalone medium", "[App]")
{
  auto file =
      std::filesystem::temp_directory_path() / "vsr-principled-cli.json";
  std::ofstream(file)
      << R"({"bounds":[0,0,0,1,1,1],"parameters":{"density":0.5}})";
  vsr::app::Context c;
  std::vector<std::string> args{
      "app", "-principled_volume_json", file.string()};
  c.parseCommandLine(args);
  REQUIRE(c.loadCommandLineSceneInputs());
  REQUIRE(c.vsr.scene.numberOfObjects(ANARI_VOLUME) == 1);
  vsr::app::Context bad;
  args.push_back("density=1");
  REQUIRE_THROWS(bad.parseCommandLine(args));
  std::filesystem::remove(file);
}

TEST_CASE(
    "Principled CLI reports import failure without creating a medium", "[App]")
{
  vsr::app::Context c;
  std::vector<std::string> args{
      "app", "-principled_volume", "density=does-not-exist.nvdb"};
  c.parseCommandLine(args);
  REQUIRE_FALSE(c.loadCommandLineSceneInputs());
  REQUIRE(c.vsr.scene.numberOfObjects(ANARI_VOLUME) == 0);
}

TEST_CASE("Unsupported principled backend does not create a substitute volume",
    "[PrincipledVolumeImport]")
{
  auto library = anari::loadLibrary("helide", nullptr, nullptr);
  if (!library) {
    WARN("helide unavailable");
    return;
  }
  auto device = anari::newDevice(library, "default");
  anari::commitParameters(device, device);
  vsr::scene::Scene scene;
  auto v = scene.createObject<vsr::scene::Volume>("principledVolume");
  auto handle = v->makeANARIObject(device);
  REQUIRE(handle == nullptr);
  anari::release(device, handle);
  anari::release(device, device);
  anari::unloadLibrary(library);
}

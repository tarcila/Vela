// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
#include "vsr/io/PrincipledVolumeImport.hpp"
#include <filesystem>
#include <fstream>

using namespace vsr::io;
namespace {
struct DescriptionFixture
{
  std::filesystem::path root = std::filesystem::temp_directory_path()
      / ("vsr-principled-"
          + std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count()));
  DescriptionFixture()
  {
    std::filesystem::create_directories(root / "elsewhere");
  }
  ~DescriptionFixture()
  {
    std::filesystem::remove_all(root);
  }
  std::filesystem::path write(const std::string &text)
  {
    auto file = root / "medium.json";
    std::ofstream(file) << text;
    return file;
  }
};
} // namespace

TEST_CASE("Principled assignments keep field and multiplier distinct",
    "[PrincipledVolumeImport]")
{
  PrincipledVolumeImportOptions o;
  applyPrincipledVolumeAssignment(o, "density=smoke.nvdb");
  applyPrincipledVolumeAssignment(o, "density=0.5");
  applyPrincipledVolumeAssignment(o, "scattering=0.8,0.9,1");
  applyPrincipledVolumeAssignment(o, "temperature:field=123");
  applyPrincipledVolumeAssignment(o, "emission:field=a=b.nvdb");
  applyPrincipledVolumeAssignment(o, "visible=false");
  applyPrincipledVolumeAssignment(o, "id=4294967295");
  REQUIRE(o.fields.at("density").file == "smoke.nvdb");
  REQUIRE(std::get<float>(o.parameters.at("density")) == 0.5f);
  REQUIRE(std::get<vsr::math::float3>(o.parameters.at("scattering")).y == 0.9f);
  REQUIRE(o.fields.at("temperature").file == "123");
  REQUIRE(o.fields.at("emission").file == "a=b.nvdb");
  REQUIRE_FALSE(std::get<bool>(o.parameters.at("visible")));
  REQUIRE(std::get<uint32_t>(o.parameters.at("id")) == UINT32_MAX);
  REQUIRE_THROWS(applyPrincipledVolumeAssignment(o, "density:value=1"));
  REQUIRE_THROWS(
      applyPrincipledVolumeAssignment(o, "density:field=another.nvdb"));
}

TEST_CASE(
    "Principled assignments reject invalid intent", "[PrincipledVolumeImport]")
{
  for (auto text : {"oops=1",
           "density:constant=1",
           "scatteringColor=1,1,1",
           "scatteringColor:field=color.nvdb",
           "anisotropy:field=a.nvdb",
           "density:value=1junk",
           "density:value=nan",
           "emission:value=1,2",
           "density=",
           "=1",
           "density",
           "visible=1",
           "id=-1",
           "id=4294967296",
           "bounds=0,0,0,0,1,1",
           "bounds=0,0,0,1,inf,1",
           "name:field=foo"}) {
    INFO(text);
    PrincipledVolumeImportOptions o;
    REQUIRE_THROWS(applyPrincipledVolumeAssignment(o, text));
  }
  PrincipledVolumeImportOptions o;
  applyPrincipledVolumeAssignment(o, "name=smoke");
  applyPrincipledVolumeAssignment(o, "bounds=0,0,0,10,20,30");
  REQUIRE(o.name == "smoke");
  REQUIRE(o.bounds->upper.z == 30.f);
  REQUIRE_THROWS(applyPrincipledVolumeAssignment(o, "bounds=0,0,0,1,1,1"));
}

TEST_CASE(
    "Principled constraints reject invalid values without forbidding cold temperatures",
    "[PrincipledVolumeImport]")
{
  for (auto text : {"density=-1",
           "scattering=1.1,0,0",
           "transmissionColor=0,1,1",
           "transmissionColor=1.1,1,1",
           "anisotropy=1.1",
           "secondaryAnisotropy=-1.1",
           "secondaryWeight=1.1",
           "blackbodyIntensity=-1",
           "blackbodyTint=-1,1,1",
           "emission=-1,0,0"}) {
    INFO(text);
    PrincipledVolumeImportOptions o;
    REQUIRE_THROWS(applyPrincipledVolumeAssignment(o, text));
  }
  PrincipledVolumeImportOptions o;
  REQUIRE_NOTHROW(applyPrincipledVolumeAssignment(o, "temperature=-10"));
  REQUIRE_NOTHROW(validatePrincipledVolumeOptions(o));
}

TEST_CASE(
    "Principled distance validation depends on complete extinction settings",
    "[PrincipledVolumeImport]")
{
  for (auto distance : {"0", "-1"}) {
    for (bool distanceFirst : {false, true}) {
      for (auto transparent : {"density=0", "transmissionColor=1,1,1"}) {
        PrincipledVolumeImportOptions o;
        const auto assignment = std::string("transmissionDistance=") + distance;
        if (distanceFirst)
          REQUIRE_NOTHROW(applyPrincipledVolumeAssignment(o, assignment));
        applyPrincipledVolumeAssignment(o, transparent);
        if (!distanceFirst)
          REQUIRE_NOTHROW(applyPrincipledVolumeAssignment(o, assignment));
        o.fields["density"] = {"smoke.nvdb"};
        REQUIRE_NOTHROW(validatePrincipledVolumeOptions(o));
      }
    }
    PrincipledVolumeImportOptions o;
    applyPrincipledVolumeAssignment(
        o, std::string("transmissionDistance=") + distance);
    REQUIRE_THROWS(validatePrincipledVolumeOptions(o));
    o.parameters["transmissionColor"] = float3(1.f, 0.5f, 1.f);
    REQUIRE_THROWS(validatePrincipledVolumeOptions(o));
  }
  DescriptionFixture f;
  REQUIRE_NOTHROW(readPrincipledVolumeDescription(
      f.write(R"({"parameters":{"transmissionDistance":0,"density":0}})")));
  REQUIRE_THROWS(readPrincipledVolumeDescription(
      f.write(R"({"parameters":{"transmissionDistance":0}})")));
}

TEST_CASE(
    "Principled JSON preserves resolved sources across export directories",
    "[PrincipledVolumeImport]")
{
  DescriptionFixture f;
  auto o = readPrincipledVolumeDescription(f.write(
      R"({"name":"smoke","fields":{"density":{"file":"smoke.nvdb"}},"parameters":{"density":0.5,"scattering":[0.8,0.9,1],"visible":false,"id":42},"bounds":[0,0,0,1,2,3]})"));
  REQUIRE(o.fields.at("density").file == f.root / "smoke.nvdb");
  auto output = f.root / "elsewhere" / "export.json";
  writePrincipledVolumeDescription(o, output);
  auto copy = readPrincipledVolumeDescription(output);
  REQUIRE(copy.fields.at("density").file == f.root / "smoke.nvdb");
  REQUIRE(copy.name == "smoke");
  REQUIRE(std::get<float3>(copy.parameters.at("scattering")).y == 0.9f);
  REQUIRE(std::get<float>(copy.parameters.at("density")) == 0.5f);
  REQUIRE(std::get<uint32_t>(copy.parameters.at("id")) == 42);
  REQUIRE_FALSE(std::get<bool>(copy.parameters.at("visible")));
  REQUIRE(copy.bounds->upper.z == 3.f);
}

TEST_CASE("Principled JSON rejects ambiguous or malformed descriptions",
    "[PrincipledVolumeImport]")
{
  DescriptionFixture f;
  for (auto text : {R"({"unknown":1})",
           R"({"fields":{"density":{"file":""}}})",
           R"({"fields":{"density":{"file":"x","sequence":[]}}})",
           R"({"parameters":{"density":"1"}})",
           R"({"parameters":{"id":1.5}})",
           R"({"parameters":{"visible":1}})",
           R"({"parameters":{"emission":[1,2]}})",
           R"({"parameters":{"density":1,"density":2}})",
           R"({"name":1})",
           R"({"bounds":[0,0,0,0,1,1]})",
           "[]",
           "{broken"}) {
    INFO(text);
    REQUIRE_THROWS(readPrincipledVolumeDescription(f.write(text)));
  }
}

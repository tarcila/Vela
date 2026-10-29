// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_io
#include "vsr/io/archives/SceneArchive.hpp"
#include "vsr/io/importers.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// vsr_scripting
#include "vsr/scripting/LuaContext.hpp"
// nanovdb
#include <nanovdb/io/IO.h>
#include <nanovdb/tools/CreateNanoGrid.h>
#include <nanovdb/tools/GridBuilder.h>
// std
#include <chrono>
#include <cstring>
#include <filesystem>

namespace {

struct VolumeTestFiles
{
  std::filesystem::path directory;
  VolumeTestFiles();
  ~VolumeTestFiles();
};

VolumeTestFiles::VolumeTestFiles()
{
  directory = std::filesystem::temp_directory_path()
      / ("vsr_lua_volume_"
          + std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(directory);
}

VolumeTestFiles::~VolumeTestFiles()
{
  std::error_code error;
  std::filesystem::remove_all(directory, error);
}

} // namespace

TEST_CASE("Lua BOX3 parameters preserve type and endpoints", "[LuaVolume]")
{
  VolumeTestFiles files;
  vsr::scripting::LuaContext context;
  auto *scene = context.createOwnedScene();
  nanovdb::tools::build::Grid<float> densityBuilder(0.f);
  densityBuilder.getAccessor().setValue(nanovdb::Coord(0), 2.f);
  auto densityGrid = nanovdb::tools::createNanoGrid(densityBuilder);
  const auto densityPath = (files.directory / "density.nvdb").string();
  nanovdb::io::writeGrid(densityPath, densityGrid);
  nanovdb::tools::build::Grid<nanovdb::Vec3f> emissionBuilder(
      nanovdb::Vec3f(0.f));
  emissionBuilder.getAccessor().setValue(
      nanovdb::Coord(0), nanovdb::Vec3f(1.f, 2.f, 3.f));
  auto emissionGrid = nanovdb::tools::createNanoGrid(emissionBuilder);
  const auto emissionPath = (files.directory / "emission.nvdb").string();
  nanovdb::io::writeGrid(emissionPath, emissionGrid);
  auto paths = context.executeString("densityPath = [[" + densityPath
      + "]]; emissionPath = [[" + emissionPath + "]]");
  REQUIRE(paths.success);
  auto result = context.executeString(R"lua(
    volume = scene:createVolume("principledVolume")
    local bounds = vsr.box3.new(
        vsr.float3(-1, -2, 0), vsr.float3(1, 2, 3))
    volume:setParameter("bounds", bounds)
    local copy = volume:getParameter("bounds")
    assert(copy.lower.x == -1 and copy.lower.y == -2 and copy.lower.z == 0)
    assert(copy.upper.x == 1 and copy.upper.y == 2 and copy.upper.z == 3)
    volume:setParameter("copy", copy)
    volume:setParameter("reversed", vsr.box3.new(
        vsr.float3(3, 2, 1), vsr.float3(0, 2, -1)))
    assert(not pcall(function()
      volume:setParameter("bounds", {-1, -2, 0, 1, 2, 3})
    end))
    volume:setParameter("integer", 1)
    volume:setParameter("float", 1.0)
    volume:setParameter("fraction", 0.5)
    local density = vsr.io.importNVDB(scene, densityPath)
    local emission = vsr.io.importNVDB(scene, emissionPath)
    assert(density:valid() and emission:valid())
    volume:setParameter("density", density)
    volume:setParameter("emission", emission)
  )lua");
  INFO(result.error);
  REQUIRE(result.success);
  auto volume = scene->getObject<vsr::scene::Volume>(0);
  REQUIRE(volume->parameter("bounds")->value().type() == ANARI_FLOAT32_BOX3);
  REQUIRE(volume->parameter("copy")->value().type() == ANARI_FLOAT32_BOX3);
  REQUIRE(volume->parameter("integer")->value().type() == ANARI_INT32);
  REQUIRE(volume->parameter("float")->value().type() == ANARI_FLOAT32);
  REQUIRE(volume->parameter("fraction")->value().type() == ANARI_FLOAT32);

  const auto archive = (files.directory / "scene.vsr").string();
  REQUIRE(vsr::io::save_SceneArchive(*scene, archive.c_str()));
  vsr::scene::Scene restored;
  REQUIRE(vsr::io::load_SceneArchive(restored, archive.c_str()));
  auto loaded = restored.getObject<vsr::scene::Volume>(0);
  REQUIRE(loaded);
  REQUIRE(loaded->subtype() == "principledVolume");
  REQUIRE(loaded->parameter("bounds")->value().type() == ANARI_FLOAT32_BOX3);
  const auto bounds =
      loaded->parameter("bounds")->value().get<vsr::math::box3>();
  REQUIRE(bounds.lower == vsr::math::float3(-1, -2, 0));
  REQUIRE(bounds.upper == vsr::math::float3(1, 2, 3));
  const auto reversed =
      loaded->parameter("reversed")->value().get<vsr::math::box3>();
  REQUIRE(reversed.lower == vsr::math::float3(3, 2, 1));
  REQUIRE(reversed.upper == vsr::math::float3(0, 2, -1));
  size_t expectedFieldIndex = 0;
  for (const auto *name : {"density", "emission"}) {
    const auto &value = loaded->parameter(name)->value();
    REQUIRE(value.type() == ANARI_SPATIAL_FIELD);
    REQUIRE(value.getAsObjectIndex() == expectedFieldIndex++);
    auto field =
        restored.getObject<vsr::scene::SpatialField>(value.getAsObjectIndex());
    REQUIRE(field);
    auto data = restored.getObject<vsr::scene::Array>(
        field->parameter("data")->value().getAsObjectIndex());
    REQUIRE(data);
    const auto &grid =
        std::string(name) == "density" ? densityGrid : emissionGrid;
    REQUIRE(data->size() == grid.bufferSize());
    REQUIRE(std::memcmp(data->data(), grid.data(), grid.bufferSize()) == 0);
  }
  context.bindScene(&restored);
  result = context.executeString(R"lua(
    local v = scene:getVolume(0)
    local b = v:getParameter("bounds")
    assert(b.lower.x == -1 and b.upper.z == 3)
    v:setParameter("bounds", b)
  )lua");
  INFO(result.error);
  REQUIRE(result.success);
}

TEST_CASE("RGB NanoVDB import preserves active black voxels and bytes",
    "[LuaVolume][Importers]")
{
  VolumeTestFiles files;
  const auto rgb = GENERATE(nanovdb::Vec3f(0.f), nanovdb::Vec3f(1.f, 2.f, 3.f));
  nanovdb::tools::build::Grid<nanovdb::Vec3f> builder(nanovdb::Vec3f(0.f));
  auto accessor = builder.getAccessor();
  accessor.setValue(nanovdb::Coord(0, 0, 0), rgb);
  auto grid = nanovdb::tools::createNanoGrid(builder);
  REQUIRE(grid.grid<nanovdb::Vec3f>()->activeVoxelCount() == 1);
  const auto path = (files.directory / "rgb.nvdb").string();
  nanovdb::io::writeGrid(path, grid);

  vsr::scene::Scene scene;
  auto field = vsr::io::import_NVDB(scene, path.c_str());
  REQUIRE(field);
  REQUIRE(field->parameter("range") == nullptr);
  const auto dataIndex = field->parameter("data")->value().getAsObjectIndex();
  auto data = scene.getObject<vsr::scene::Array>(dataIndex);
  REQUIRE(data);
  REQUIRE(data->size() == grid.bufferSize());
  REQUIRE(std::memcmp(data->data(), grid.data(), grid.bufferSize()) == 0);

  auto volume = scene.createObject<vsr::scene::Volume>("principledVolume");
  volume->setParameterObject("emission", *field);
  const auto archive = (files.directory / "rgb.vsr").string();
  REQUIRE(vsr::io::save_SceneArchive(scene, archive.c_str()));
  vsr::scene::Scene restored;
  REQUIRE(vsr::io::load_SceneArchive(restored, archive.c_str()));
  auto restoredVolume = restored.getObject<vsr::scene::Volume>(0);
  REQUIRE(restoredVolume);
  const auto fieldIndex =
      restoredVolume->parameter("emission")->value().getAsObjectIndex();
  auto restoredField = restored.getObject<vsr::scene::SpatialField>(fieldIndex);
  REQUIRE(restoredField);
  REQUIRE(restoredField->parameter("range") == nullptr);
  auto restoredData = restored.getObject<vsr::scene::Array>(
      restoredField->parameter("data")->value().getAsObjectIndex());
  REQUIRE(restoredData);
  REQUIRE(restoredData->size() == grid.bufferSize());
  REQUIRE(
      std::memcmp(restoredData->data(), grid.data(), grid.bufferSize()) == 0);
}

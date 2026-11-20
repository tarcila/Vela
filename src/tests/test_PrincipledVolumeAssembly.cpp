// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
#include "vsr/core/DataTree.hpp"
#include "vsr/io/PrincipledVolumeImport.hpp"
#include "vsr/io/archives/ObjectArchive.hpp"
#include <nanovdb/io/IO.h>
#include <nanovdb/tools/CreateNanoGrid.h>
#include <nanovdb/tools/GridBuilder.h>
#include <fstream>

using namespace vsr::io;
using namespace vsr::scene;
using namespace vsr::math;

namespace {
struct RawFixture
{
  std::filesystem::path path = std::filesystem::temp_directory_path()
      / "vsr-principled_2x2x2_float32.raw";
  RawFixture()
  {
    const float values[8] = {2, 3, 4, 5, 6, 7, 8, 9};
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char *>(values), sizeof(values));
  }
  ~RawFixture()
  {
    std::filesystem::remove(path);
  }
};
} // namespace
TEST_CASE("Principled import binds original values with extension defaults",
    "[PrincipledVolumeImport]")
{
  RawFixture file;
  Scene s;
  auto *layer = s.addLayer("smoke-layer");
  auto parent = s.insertChildNode(layer->root(), "parent");
  PrincipledVolumeImportOptions o;
  o.fields["density"] = {file.path};
  o.fields["temperature"] = {file.path};
  o.parameters["density"] = 0.5f;
  auto v = import_principledVolume(s, o, parent);
  REQUIRE(v);
  REQUIRE(v->subtype() == "principled");
  REQUIRE(v->parameterValueAs<float>("density").value() == 0.5f);
  REQUIRE(v->parameterValueAs<float>("temperature").value() == 1.f);
  REQUIRE(v->parameterValueAs<float>("blackbodyIntensity").value() == 0.f);
  REQUIRE(v->parameterValueAs<float3>("emission")->x == 0.f);
  REQUIRE(v->parameterValueAs<float3>("transmissionColor")->x
      == Approx(0.36787944f));
  REQUIRE(v->parameterValueAs<float>("transmissionDistance").value() == 1.f);
  REQUIRE(v->parameterValueAs<float>("anisotropy").value() == 0.f);
  REQUIRE(v->parameterValueAs<float>("secondaryAnisotropy").value() == 0.f);
  REQUIRE(v->parameterValueAs<float>("secondaryWeight").value() == 0.f);
  REQUIRE(v->parameterValueAs<float3>("scattering").has_value());
  REQUIRE(v->parameterValueAs<float3>("scattering")->x == 1.f);
  REQUIRE(v->parameterValueAs<float3>("blackbodyTint")->x == 1.f);
  REQUIRE(v->parameterValueAs<bool>("visible").value());
  auto *f = v->parameterValueAsObject<SpatialField>("density.field");
  REQUIRE(f);
  REQUIRE(f->parameterValueAsObject<Array>("data")->dataAs<float>()[0] == 2.f);
  REQUIRE(v->parameterValueAs<box3>("bounds")->upper.x == 1.f);
  REQUIRE(v->parameter("color") == nullptr);
  REQUIRE_FALSE(parent->isLeaf());
  vsr::core::DataTree tree;
  REQUIRE(serialize_ObjectArchive(*v, tree.root()));
  Scene copy;
  auto *restored = deserialize_ObjectArchive(copy, tree.root());
  REQUIRE(restored);
  REQUIRE(restored->subtype() == "principled");
  REQUIRE(restored->parameterValueAsObject<SpatialField>("density.field"));
}

TEST_CASE(
    "Principled RGB parameters accept scalar fields without changing multipliers",
    "[PrincipledVolumeImport]")
{
  RawFixture file;
  Scene s;
  PrincipledVolumeImportOptions o;
  const auto property = GENERATE("emission", "scattering");
  o.fields[property] = {file.path};
  auto v = import_principledVolume(s, o);
  REQUIRE(v->parameterValueAs<float3>("scattering")->x == 1.f);
  REQUIRE(v->parameterValueAs<float3>("emission")->x == 0.f);
  {
    auto *field = v->parameterValueAsObject<SpatialField>(
        (std::string(property) + ".field").c_str());
    REQUIRE(field);
    REQUIRE(field->parameterValueAsObject<Array>("data")->dataAs<float>()[0]
        == 2.f);
  }
  o.parameters["scattering"] = float3(0.2f, 0.4f, 0.6f);
  o.parameters["emission"] = float3(1.f, 2.f, 3.f);
  v = import_principledVolume(s, o);
  REQUIRE(v->parameterValueAs<float3>("scattering")->y == 0.4f);
  REQUIRE(v->parameterValueAs<float3>("emission")->z == 3.f);
  vsr::core::DataTree tree;
  REQUIRE(serialize_ObjectArchive(*v, tree.root()));
  Scene copy;
  auto *restored = deserialize_ObjectArchive(copy, tree.root());
  REQUIRE(restored->parameterValueAsObject<SpatialField>(
      (std::string(property) + ".field").c_str()));
  REQUIRE(restored->parameterValueAs<float3>("scattering")->y == 0.4f);
}

TEST_CASE("Principled import failures leave destination objects intact",
    "[PrincipledVolumeImport]")
{
  RawFixture file;
  Scene s;
  auto keep = s.createArray(ANARI_FLOAT32, 5);
  auto keepIndex = keep.index();
  const auto fields = s.numberOfObjects(ANARI_SPATIAL_FIELD);
  const auto arrays = s.numberOfObjects(ANARI_ARRAY);
  const auto volumes = s.numberOfObjects(ANARI_VOLUME);
  const auto wasLeaf = s.defaultLayer()->root()->isLeaf();
  PrincipledVolumeImportOptions o;
  o.fields["density"] = {file.path};
  o.fields["temperature"] = {"does-not-exist.nvdb"};
  REQUIRE_THROWS(import_principledVolume(s, o));
  o.fields.erase("temperature");
  o.fields["density"] = {"does-not-exist.nvdb"};
  REQUIRE_THROWS(import_principledVolume(s, o));
  REQUIRE(s.numberOfObjects(ANARI_SPATIAL_FIELD) == fields);
  REQUIRE(s.numberOfObjects(ANARI_ARRAY) == arrays);
  REQUIRE(s.numberOfObjects(ANARI_VOLUME) == volumes);
  REQUIRE(s.defaultLayer()->root()->isLeaf() == wasLeaf);
  REQUIRE(s.getObject<Array>(keepIndex).data() == keep.data());
}

TEST_CASE("Principled RGB fields bind only to RGB parameters transactionally",
    "[PrincipledVolumeImport]")
{
  nanovdb::tools::build::Grid<nanovdb::Vec3f> grid(nanovdb::Vec3f(0));
  grid.getAccessor().setValue(nanovdb::Coord(0), nanovdb::Vec3f(1, 2, 3));
  grid.getAccessor().setValue(nanovdb::Coord(1), nanovdb::Vec3f(4, 5, 6));
  auto handle = nanovdb::tools::createNanoGrid(grid);
  const auto path = std::filesystem::temp_directory_path()
      / "vsr-principled-assembly-rgb.nvdb";
  nanovdb::io::writeGrid(path.string(), handle);
  struct Cleanup
  {
    std::filesystem::path path;
    ~Cleanup()
    {
      std::filesystem::remove(path);
    }
  } cleanup{path};
  Scene s;
  for (auto name : {"scattering", "emission"}) {
    PrincipledVolumeImportOptions o;
    o.fields[name] = {path};
    auto v = import_principledVolume(s, o);
    REQUIRE(v->parameterValueAsObject<SpatialField>(
        (std::string(name) + ".field").c_str()));
  }
  const auto fields = s.numberOfObjects(ANARI_SPATIAL_FIELD);
  const auto arrays = s.numberOfObjects(ANARI_ARRAY);
  const auto volumes = s.numberOfObjects(ANARI_VOLUME);
  for (auto name : {"density", "temperature"}) {
    PrincipledVolumeImportOptions o;
    o.fields[name] = {path};
    REQUIRE_THROWS(import_principledVolume(s, o));
    REQUIRE(s.numberOfObjects(ANARI_SPATIAL_FIELD) == fields);
    REQUIRE(s.numberOfObjects(ANARI_ARRAY) == arrays);
    REQUIRE(s.numberOfObjects(ANARI_VOLUME) == volumes);
  }
}

TEST_CASE("Principled uniform medium requires explicit positive bounds",
    "[PrincipledVolumeImport]")
{
  Scene s;
  PrincipledVolumeImportOptions o;
  REQUIRE_THROWS(import_principledVolume(s, o));
  o.bounds = box3(float3(-1.f), float3(2.f));
  auto v = import_principledVolume(s, o);
  REQUIRE(v->parameterValueAs<box3>("bounds")->lower.x == -1.f);
  REQUIRE(v->parameterValueAs<float>("density").value() == 1.f);
}

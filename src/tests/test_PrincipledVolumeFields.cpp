// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
#include "vsr/io/importers.hpp"
#include "vsr/io/importers/detail/PrincipledVolumeField.hpp"
#include <nanovdb/io/IO.h>
#include <nanovdb/tools/CreateNanoGrid.h>
#include <nanovdb/tools/GridBuilder.h>
#include <filesystem>
#include <fstream>

using namespace vsr::io;
using namespace vsr::scene;
using namespace vsr::math;
namespace {
namespace detail = vsr::io::detail;

TEST_CASE("Principled structured domains respect coordinates and centering",
    "[PrincipledVolumeImport]")
{
  Scene s;
  auto f =
      s.createObject<SpatialField>(tokens::spatial_field::structuredRegular);
  auto a = s.createArray(ANARI_FLOAT32, 2, 3, 4);
  f->setParameterObject("data", *a);
  f->setParameter("origin", float3(10, 20, 30));
  f->setParameter("spacing", float3(2, 3, 4));
  auto info = detail::inspectPrincipledVolumeField(*f);
  REQUIRE_FALSE(info.rgb);
  REQUIRE(info.bounds->lower.x == 10);
  REQUIRE(info.bounds->upper.x == 12);
  REQUIRE(info.bounds->upper.y == 26);
  REQUIRE(info.bounds->upper.z == 42);
  f->setParameter("dataCentering", "cell");
  info = detail::inspectPrincipledVolumeField(*f);
  REQUIRE(info.bounds->upper.x == 14);
  REQUIRE(info.bounds->upper.z == 46);
  f->setParameter("roi", box3(float3(11, 21, 31), float3(13, 25, 40)));
  info = detail::inspectPrincipledVolumeField(*f);
  REQUIRE(info.bounds->lower.x == 11);
  REQUIRE(info.bounds->upper.z == 40);
  auto rgb = s.createArray(ANARI_FLOAT32_VEC3, 2, 3, 4);
  f->setParameterObject("data", *rgb);
  REQUIRE(detail::inspectPrincipledVolumeField(*f).rgb);
  auto bad = s.createArray(ANARI_FLOAT32_VEC2, 2, 3, 4);
  f->setParameterObject("data", *bad);
  REQUIRE_THROWS(detail::inspectPrincipledVolumeField(*f));
}

TEST_CASE(
    "Principled rectilinear and unstructured domains retain physical positions",
    "[PrincipledVolumeImport]")
{
  Scene s;
  auto f = s.createObject<SpatialField>(
      tokens::spatial_field::structuredRectilinear);
  auto data = s.createArray(ANARI_FLOAT32, 2, 2, 2);
  f->setParameterObject("data", *data);
  for (auto name : {"coordsX", "coordsY", "coordsZ"}) {
    auto axis = s.createArray(ANARI_FLOAT32, 2);
    const float values[] = {10, 40};
    axis->setData(values, 2);
    f->setParameterObject(name, *axis);
  }
  auto info = detail::inspectPrincipledVolumeField(*f);
  REQUIRE(info.bounds->lower.x == 10);
  REQUIRE(info.bounds->upper.z == 40);
  auto u = s.createObject<SpatialField>(tokens::spatial_field::unstructured);
  auto positions = s.createArray(ANARI_FLOAT32_VEC3, 2);
  const float3 points[] = {float3(-2, 3, 4), float3(5, 6, 7)};
  positions->setData(points, 2);
  u->setParameterObject("vertex.position", *positions);
  u->setParameterObject("vertex.data", *data);
  info = detail::inspectPrincipledVolumeField(*u);
  REQUIRE(info.bounds->lower.x == -2);
  REQUIRE(info.bounds->upper.z == 7);
  auto amr = s.createObject<SpatialField>(tokens::spatial_field::amr);
  amr->setParameterObject("data", *data);
  // Missing block geometry cannot silently turn into unit bounds.
  REQUIRE_FALSE(detail::inspectPrincipledVolumeField(*amr).bounds);
}

TEST_CASE(
    "Principled NanoVDB field inspection preserves transforms and RGB type",
    "[PrincipledVolumeImport]")
{
  Scene s;
  nanovdb::tools::build::Grid<float> grid(0.f);
  grid.getAccessor().setValue(nanovdb::Coord(0, 0, 0), 2.f);
  grid.getAccessor().setValue(nanovdb::Coord(1, 2, 3), 4.f);
  auto handle = nanovdb::tools::createNanoGrid(grid);
  auto path =
      std::filesystem::temp_directory_path() / "vsr-principled-field.nvdb";
  nanovdb::io::writeGrid(path.string(), handle);
  auto f = import_NVDB(s, path.string().c_str());
  std::filesystem::remove(path);
  REQUIRE(f);
  auto info = detail::inspectPrincipledVolumeField(*f);
  REQUIRE_FALSE(info.rgb);
  REQUIRE(info.bounds->upper.x > info.bounds->lower.x);
  // Nonidentity NanoVDB map, not array byte dimensions, controls bounds.
  auto a = f->parameterValueAsObject<Array>("data");
  auto *g = reinterpret_cast<nanovdb::NanoGrid<float> *>(a->map());
  const double mat[3][3] = {{2, 0, 0}, {0, 3, 0}, {0, 0, 4}};
  const double inv[3][3] = {{.5, 0, 0}, {0, 1. / 3, 0}, {0, 0, .25}};
  const double translation[3] = {10, 20, 30};
  g->mMap.set(mat, inv, translation);
  a->unmap();
  info = detail::inspectPrincipledVolumeField(*f);
  REQUIRE(info.bounds->lower.x == 10);
  REQUIRE(info.bounds->upper.x == 14);
  REQUIRE(info.bounds->upper.z == 46);
  nanovdb::tools::build::Grid<nanovdb::Vec3f> colors(nanovdb::Vec3f(0));
  colors.getAccessor().setValue(nanovdb::Coord(0), nanovdb::Vec3f(1));
  auto rgbHandle = nanovdb::tools::createNanoGrid(colors);
  auto bytes = s.createArray(ANARI_UINT8, rgbHandle.bufferSize());
  bytes->setData(rgbHandle.data());
  f->setParameterObject("data", *bytes);
  REQUIRE(detail::inspectPrincipledVolumeField(*f).rgb);
}

TEST_CASE("Principled VTI spatial import does not orphan extra fields",
    "[PrincipledVolumeImport]")
{
#if VSR_USE_VTK
  auto path = std::filesystem::temp_directory_path() / "vsr-principled.vti";
  std::ofstream(path)
      << R"(<?xml version="1.0"?><VTKFile type="ImageData" version="0.1" byte_order="LittleEndian"><ImageData WholeExtent="0 1 0 1 0 1" Origin="0 0 0" Spacing="1 1 1"><Piece Extent="0 1 0 1 0 1"><PointData Scalars="density"><DataArray type="Float32" Name="density" format="ascii">1 2 3 4 5 6 7 8</DataArray><DataArray type="Float32" Name="heat" format="ascii">8 7 6 5 4 3 2 1</DataArray></PointData><CellData/></Piece></ImageData></VTKFile>)";
  Scene s;
  auto f = import_spatial_field(s, path.string().c_str());
  std::filesystem::remove(path);
  REQUIRE(f);
  REQUIRE(s.numberOfObjects(ANARI_SPATIAL_FIELD) == 1);
  REQUIRE(s.numberOfObjects(ANARI_ARRAY3D) == 1);
#endif
}
} // namespace

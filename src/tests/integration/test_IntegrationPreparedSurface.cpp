// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_integration
#include "vsr/integration/content/detail/PreparedSurfaceGraph.hpp"
// catch
#include "catch.hpp"
// std
#include <cstring>
#include <limits>

using namespace vsr;

namespace {
std::pmr::vector<std::uint8_t> carrier(const std::string &header)
{
  std::pmr::vector<std::uint8_t> bytes;
  for (auto c : std::string("VMESH01\n"))
    bytes.push_back(c);
  for (unsigned i = 0; i < 4; ++i)
    bytes.push_back((header.size() >> (i * 8)) & 255);
  bytes.insert(bytes.end(), header.begin(), header.end());
  return bytes;
}

template <typename T, size_t N>
void append(std::pmr::vector<std::uint8_t> &bytes, const T (&values)[N])
{
  const auto *data = reinterpret_cast<const std::uint8_t *>(values);
  bytes.insert(bytes.end(), data, data + sizeof(values));
}

std::pmr::vector<std::uint8_t> triangleBundle()
{
  const std::string header =
      R"({"revision":1,"byte_order":"little","source":{"target":"not-authority"},"arrays":{"points":{"element_type":"ANARI_FLOAT32_VEC3","count":3,"byte_length":36}},"fields":{"vertex.position":{"action":"preserve","payload":"points","enabled":true,"association":"vertex"},"vertex.attribute0":{"action":"replace","payload":"points","enabled":false,"association":"vertex"}}})";
  auto bytes = carrier(header);
  const float points[] = {10, 20, 30, 12, 20, 30, 10, 23, 30};
  append(bytes, points);
  return bytes;
}
} // namespace

TEST_CASE("Private Surface preparation builds a complete independent closure",
    "[integration][content][prepared-surface]")
{
  std::unique_ptr<integration::content::detail::PreparedSurfaceGraph> graph;
  REQUIRE(integration::content::detail::prepareSurfaceGraph(
              triangleBundle(), "Authored mesh", {}, graph)
      == nullptr);
  REQUIRE(graph);
  const auto &scene = graph->scene();
  CHECK(scene.numberOfObjects(ANARI_ARRAY) == 1);
  CHECK(scene.numberOfObjects(ANARI_GEOMETRY) == 1);
  CHECK(scene.numberOfObjects(ANARI_SURFACE) == 1);
  CHECK(scene.numberOfObjects(ANARI_MATERIAL) == 1);
  const auto &geometry = graph->geometry();
  const auto *position =
      geometry.parameterValueAsObject<scene::Array>("vertex.position");
  REQUIRE(position);
  CHECK(position
      == geometry.parameterValueAsObject<scene::Array>("vertex.attribute0"));
  CHECK_FALSE(geometry.parameter("vertex.attribute0")->isEnabled());
  CHECK(position->elementType() == ANARI_FLOAT32_VEC3);
  const float expected[] = {10, 20, 30, 12, 20, 30, 10, 23, 30};
  CHECK(std::memcmp(position->data(), expected, sizeof(expected)) == 0);
  CHECK(graph->surface().geometry() == &geometry);
  CHECK(graph->surface().material() == &graph->material());
  CHECK(graph->material().parameter("color")->value().get<core::math::float3>()
      == core::math::float3(0.8f));
  CHECK(graph->material().parameter("opacity")->value().get<float>() == 1.f);
  CHECK(graph->material().parameter("alphaMode")->value().getStringView()
      == "opaque");
  CHECK(graph->material().parameter("alphaMode")->stringSelection() == 0);
  REQUIRE(graph->root());
  CHECK(graph->root()->value().name() == "Authored mesh");
  size_t children = 0;
  core::foreach_child(graph->root(), [&](auto) { ++children; });
  CHECK(children == 1);
}

TEST_CASE(
    "Private indexed Surface preserves typed associated data without dedup",
    "[integration][content][prepared-surface]")
{
  const auto header = R"({"revision":1,"byte_order":"little","arrays":{
    "p":{"element_type":"ANARI_FLOAT64_VEC3","count":3,"byte_length":72},
    "i":{"element_type":"ANARI_UINT64_VEC3","count":1,"byte_length":24},
    "n":{"element_type":"ANARI_INT8_VEC3","count":3,"byte_length":9},
    "t":{"element_type":"ANARI_FLOAT32_VEC4","count":3,"byte_length":48},
    "c":{"element_type":"ANARI_UFIXED8_VEC4","count":1,"byte_length":4},
    "v":{"element_type":"ANARI_INT64","count":3,"byte_length":24},
    "f":{"element_type":"ANARI_FLOAT64_VEC2","count":3,"byte_length":48},
    "q":{"element_type":"ANARI_FLOAT64_VEC3","count":3,"byte_length":72}
  },"fields":{
    "vertex.position":{"action":"replace","payload":"p","enabled":true,"association":"vertex"},
    "primitive.index":{"action":"preserve","payload":"i","enabled":true,"association":"index"},
    "vertex.normal":{"action":"replace","payload":"n","enabled":true,"association":"vertex"},
    "vertex.tangent":{"action":"replace","payload":"t","enabled":true,"association":"vertex"},
    "primitive.color":{"action":"replace","payload":"c","enabled":true,"association":"primitive"},
    "vertex.attribute0":{"action":"replace","payload":"v","enabled":false,"association":"vertex"},
    "faceVarying.attribute1":{"action":"replace","payload":"f","enabled":true,"association":"corner"},
    "vertex.attribute2":{"action":"replace","payload":"q","enabled":true,"association":"vertex"},
    "vertex.attribute3":{"action":"preserve","payload":"p","enabled":true,"association":"vertex"},
    "primitive.normal":{"action":"remove","enabled":false,"association":"primitive"}
  }})";
  auto bytes = carrier(header);
  const double points[] = {1.25, 2, 3, 4, 5, 6, 7, 8, 9};
  const std::uint64_t indices[] = {2, 0, 1};
  const std::int8_t normals[] = {-1, 0, 1, 2, 3, 4, 5, 6, 7};
  const float tangents[] = {1, 0, 0, -1, 0, 1, 0, 1, 0, 0, 1, -1};
  const std::uint8_t color[] = {12, 34, 56, 255};
  const std::int64_t values[] = {-9007199254740993LL, 17, 9007199254740993LL};
  const double corners[] = {0, 1, 2, 3, 4, 5};
  append(bytes, points);
  append(bytes, indices);
  append(bytes, normals);
  append(bytes, tangents);
  append(bytes, color);
  append(bytes, values);
  append(bytes, corners);
  append(bytes, points); // Equal bytes, distinct authored payload identity.
  integration::content::detail::SurfaceGraphSettings settings;
  settings.color = core::math::float3(0.2f, 0.3f, 0.4f);
  settings.transform[3] = core::math::float4(11, 12, 13, 1);
  std::unique_ptr<integration::content::detail::PreparedSurfaceGraph> graph;
  REQUIRE(integration::content::detail::prepareSurfaceGraph(
              std::move(bytes), "Indexed", settings, graph)
      == nullptr);
  const auto &g = graph->geometry();
  const auto check = [&](const char *name,
                         anari::DataType type,
                         const void *expected,
                         size_t size) {
    const auto *a = g.parameterValueAsObject<scene::Array>(name);
    REQUIRE(a);
    CHECK(a->elementType() == type);
    CHECK(a->size() * a->elementSize() == size);
    CHECK(std::memcmp(a->data(), expected, size) == 0);
    return a;
  };
  const auto *position =
      check("vertex.position", ANARI_FLOAT64_VEC3, points, sizeof(points));
  check("primitive.index", ANARI_UINT64_VEC3, indices, sizeof(indices));
  check("vertex.normal", ANARI_INT8_VEC3, normals, sizeof(normals));
  check("vertex.tangent", ANARI_FLOAT32_VEC4, tangents, sizeof(tangents));
  check("primitive.color", ANARI_UFIXED8_VEC4, color, sizeof(color));
  check("vertex.attribute0", ANARI_INT64, values, sizeof(values));
  check("faceVarying.attribute1", ANARI_FLOAT64_VEC2, corners, sizeof(corners));
  CHECK(position
      != check(
          "vertex.attribute2", ANARI_FLOAT64_VEC3, points, sizeof(points)));
  CHECK(
      position == g.parameterValueAsObject<scene::Array>("vertex.attribute3"));
  CHECK_FALSE(g.parameter("vertex.attribute0")->isEnabled());
  CHECK(g.parameter("primitive.normal") == nullptr);
  CHECK(graph->scene().numberOfObjects(ANARI_ARRAY) == 8);
  CHECK(graph->scene().numberOfObjects(ANARI_CAMERA) == 0);
  CHECK(graph->root()->value().getTransform()[3]
      == core::math::float4(11, 12, 13, 1));
  CHECK(graph->material().parameter("color")->value().get<core::math::float3>()
      == core::math::float3(0.2f, 0.3f, 0.4f));
}

TEST_CASE(
    "Private Surface preparation rejects malformed labels without replacing prior ownership",
    "[integration][content][prepared-surface]")
{
  std::unique_ptr<integration::content::detail::PreparedSurfaceGraph> graph;
  REQUIRE(integration::content::detail::prepareSurfaceGraph(
              triangleBundle(), "Prior", {}, graph)
      == nullptr);
  const auto *prior = graph.get();
  CHECK(std::string(integration::content::detail::prepareSurfaceGraph(
            triangleBundle(), "bad\xff", {}, graph))
      == "invalid_content_root_name");
  CHECK(graph.get() == prior);
  CHECK(graph->root()->value().name() == "Prior");
}

TEST_CASE("Private Surface failures and cancellation preserve previous closure",
    "[integration][content][prepared-surface]")
{
  std::unique_ptr<integration::content::detail::PreparedSurfaceGraph> graph;
  REQUIRE(integration::content::detail::prepareSurfaceGraph(
              triangleBundle(), "Prior", {}, graph)
      == nullptr);
  const auto *prior = graph.get();
  SECTION("nonfinite authored data is not repaired")
  {
    auto bytes = triangleBundle();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(bytes.data() + bytes.size() - 4, &nan, 4);
    CHECK(std::string(integration::content::detail::prepareSurfaceGraph(
              std::move(bytes), "Invalid", {}, graph))
        == "nonfinite_mesh_value");
  }
  SECTION("unsupported metadata is not ignored")
  {
    auto bytes = triangleBundle();
    bytes[12] = '[';
    CHECK(integration::content::detail::prepareSurfaceGraph(
              std::move(bytes), "Invalid", {}, graph)
        != nullptr);
  }
  SECTION("invalid appearance is rejected")
  {
    integration::content::detail::SurfaceGraphSettings settings;
    settings.color.x = -0.1f;
    CHECK(std::string(integration::content::detail::prepareSurfaceGraph(
              triangleBundle(), "Invalid", settings, graph))
        == "invalid_matte_color");
  }
  SECTION("projective placement is rejected")
  {
    integration::content::detail::SurfaceGraphSettings settings;
    settings.transform[0][3] = 1;
    CHECK(std::string(integration::content::detail::prepareSurfaceGraph(
              triangleBundle(), "Invalid", settings, graph))
        == "invalid_surface_transform");
  }
  SECTION("every cooperative boundary abandons private work")
  {
    size_t checks = 0;
    std::unique_ptr<integration::content::detail::PreparedSurfaceGraph> control;
    REQUIRE(integration::content::detail::prepareSurfaceGraph(triangleBundle(),
                "Control",
                {},
                control,
                [&] {
                  ++checks;
                  return false;
                })
        == nullptr);
    REQUIRE(checks >= 5);
    for (size_t stop = 1; stop <= checks; ++stop) {
      size_t visited = 0;
      CHECK(std::string(integration::content::detail::prepareSurfaceGraph(
                triangleBundle(),
                "Cancelled",
                {},
                graph,
                [&] { return ++visited == stop; }))
          == "cancelled");
      CHECK(graph.get() == prior);
    }
  }
  CHECK(graph.get() == prior);
  CHECK(graph->root()->value().name() == "Prior");
}

TEST_CASE("New mesh planning cannot bypass an existing Geometry inventory",
    "[integration][content][prepared-surface]")
{
  scene::Scene live;
  auto geometry =
      live.createObject<scene::Geometry>(scene::tokens::geometry::triangle);
  integration::mesh::Bundle bundle;
  REQUIRE(integration::mesh::decodeBundle(triangleBundle(), bundle) == nullptr);
  integration::mesh::MeshReplacement plan;
  CHECK(
      std::string(integration::mesh::prepareMesh(*geometry, bundle, plan, true))
      == "invalid_new_geometry");
  CHECK(geometry->numParameters() == 0);
  CHECK(live.numberOfObjects(ANARI_ARRAY) == 0);
}

TEST_CASE("New Geometry validation rejects inconsistent complete structures",
    "[integration][content][prepared-surface]")
{
  std::unique_ptr<integration::content::detail::PreparedSurfaceGraph> graph;
  const std::string points =
      R"("p":{"element_type":"ANARI_FLOAT32_VEC3","count":3,"byte_length":36})";
  const std::string position =
      R"("vertex.position":{"action":"replace","payload":"p","enabled":true,"association":"vertex"})";
  std::string extra, field, expected;
  SECTION("disabled attribute is still structurally validated")
  {
    extra =
        R"("a":{"element_type":"ANARI_FLOAT32_VEC3","count":1,"byte_length":12})";
    field =
        R"(,"vertex.normal":{"action":"replace","payload":"a","enabled":false,"association":"vertex"})";
    expected = "attribute_count_mismatch";
  }
  SECTION("index range is not repaired")
  {
    extra =
        R"("a":{"element_type":"ANARI_UINT32_VEC3","count":1,"byte_length":12})";
    field =
        R"(,"primitive.index":{"action":"replace","payload":"a","enabled":true,"association":"index"})";
    expected = "index_out_of_range";
  }
  SECTION("unreferenced payload is not silently discarded")
  {
    extra =
        R"("a":{"element_type":"ANARI_UINT32_VEC3","count":1,"byte_length":12})";
    expected = "unreferenced_mesh_payload";
  }
  auto bytes = carrier("{\"revision\":1,\"byte_order\":\"little\",\"arrays\":{"
      + points + "," + extra + "},\"fields\":{" + position + field + "}}");
  const float p[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  const std::uint32_t a[] = {0, 1, 3};
  append(bytes, p);
  append(bytes, a);
  const auto *reason = integration::content::detail::prepareSurfaceGraph(
      std::move(bytes), "Invalid", {}, graph);
  REQUIRE(reason);
  CHECK(std::string(reason) == expected);
  CHECK_FALSE(graph);
}

TEST_CASE("Separate private Surface preparations never share object identities",
    "[integration][content][prepared-surface]")
{
  std::unique_ptr<integration::content::detail::PreparedSurfaceGraph> first,
      second;
  REQUIRE(integration::content::detail::prepareSurfaceGraph(
              triangleBundle(), "Same name", {}, first)
      == nullptr);
  REQUIRE(integration::content::detail::prepareSurfaceGraph(
              triangleBundle(), "Same name", {}, second)
      == nullptr);
  CHECK(&first->geometry() != &second->geometry());
  CHECK(&first->material() != &second->material());
  CHECK(
      first->geometry().parameterValueAsObject<scene::Array>("vertex.position")
      != second->geometry().parameterValueAsObject<scene::Array>(
          "vertex.position"));
  first.reset();
  CHECK(second->surface().geometry() == &second->geometry());
  CHECK(second->root()->value().name() == "Same name");
}

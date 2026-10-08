// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_integration
#include "vsr/integration/mesh/MeshBundle.hpp"
#include "vsr/integration/mesh/MeshReplacement.hpp"
// std
#include <cstring>
#include <limits>
#include <memory_resource>
#include <string>
#include <vector>

namespace {
using namespace vsr::integration;

std::pmr::vector<std::uint8_t> carrier(
    const std::string &header, const std::vector<float> &samples)
{
  std::pmr::vector<std::uint8_t> bytes;
  bytes.insert(bytes.end(), {'V', 'M', 'E', 'S', 'H', '0', '1', '\n'});
  for (int i = 0; i < 4; ++i)
    bytes.push_back(std::uint8_t(header.size() >> (8 * i)));
  bytes.insert(bytes.end(), header.begin(), header.end());
  if (!samples.empty()) {
    const auto *data = reinterpret_cast<const std::uint8_t *>(samples.data());
    bytes.insert(bytes.end(), data, data + samples.size() * sizeof(float));
  }
  return bytes;
}
} // namespace

TEST_CASE("Mesh bundle decoding preserves declaration order and exact types",
    "[integration][mesh-bundle]")
{
  const std::string header =
      R"({"revision":1,"byte_order":"little","fields":{"vertex.position":{"action":"replace","payload":"z","enabled":true,"association":"vertex"},"vertex.normal":{"action":"preserve","payload":"a","enabled":false,"association":"vertex"}},"arrays":{"z":{"element_type":"ANARI_FLOAT32_VEC3","count":3,"byte_length":36},"a":{"element_type":"ANARI_FLOAT32_VEC3","count":3,"byte_length":36}}})";
  auto bytes = carrier(header,
      {-1.f,
          -1.f,
          0.f,
          1.f,
          -1.f,
          0.f,
          0.f,
          1.f,
          0.f,
          0.f,
          0.f,
          1.f,
          0.f,
          0.f,
          1.f,
          0.f,
          0.f,
          1.f});
  mesh::Bundle bundle;
  REQUIRE(mesh::decodeBundle(std::move(bytes), bundle) == nullptr);
  REQUIRE(bundle.arrays.size() == 2);
  REQUIRE(bundle.arrays[0].id == "z");
  REQUIRE(bundle.arrays[1].id == "a");
  REQUIRE(bundle.arrays[0].elementType == ANARI_FLOAT32_VEC3);
  REQUIRE(bundle.arrays[0].count == 3);
  REQUIRE(bundle.fields.size() == 2);
  REQUIRE(bundle.fields[0].name == "vertex.normal");
  REQUIRE_FALSE(bundle.fields[0].enabled);
  REQUIRE(bundle.fields[1].action == mesh::Action::REPLACE);
  float first{};
  std::memcpy(&first, bundle.data(bundle.arrays[0]), sizeof(first));
  REQUIRE(first == -1.f);
  std::memcpy(&first, bundle.data(bundle.arrays[1]), sizeof(first));
  REQUIRE(first == 0.f);
}

TEST_CASE("Mesh payload identifiers are local keys, never external paths",
    "[integration][mesh-bundle]")
{
  const std::string header =
      R"({"source":{"target_version":"foreign","path":"/nonexistent/source"},"revision":1,"byte_order":"little","fields":{"vertex.position":{"action":"replace","payload":"../z\"{","enabled":true,"association":"vertex"},"vertex.normal":{"action":"preserve","payload":"a","enabled":true,"association":"vertex"}},"ar\u0072ays":{"../z\"{":{"element_type":"ANARI_FLOAT32_VEC3","count":3,"byte_length":36},"a":{"element_type":"ANARI_FLOAT32_VEC3","count":3,"byte_length":36}}})";
  auto bytes = carrier(
      header, {-1, -1, 0, 1, -1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1});
  mesh::Bundle bundle;
  REQUIRE(mesh::decodeBundle(std::move(bytes), bundle) == nullptr);
  REQUIRE(bundle.arrays[0].id == "../z\"{");
  REQUIRE(bundle.arrays[1].id == "a");
  REQUIRE(bundle.fields[1].payload == 0);
  REQUIRE(bundle.arrays[1].offset == bundle.arrays[0].offset + 36);
}

TEST_CASE("Mesh decoding bounds metadata before building a JSON tree",
    "[integration][mesh-bundle]")
{
  std::string source = "[0";
  for (std::size_t i = 0; i < 4096; ++i)
    source += ",0";
  source += "]";
  const auto header =
      "{\"revision\":1,\"byte_order\":\"little\","
      "\"fields\":{},\"arrays\":{},\"source\":{\"ignored\":"
      + source + "}}";
  auto bytes = carrier(header, {});
  mesh::Bundle bundle;
  const auto *reason = mesh::decodeBundle(std::move(bytes), bundle);
  REQUIRE(reason != nullptr);
  REQUIRE(std::string(reason) == "mesh_metadata_limit");
  REQUIRE(bundle.bytes.empty());
  REQUIRE(bundle.arrays.empty());
  REQUIRE(bundle.fields.empty());
}

TEST_CASE("Mesh replacement validates the complete proposed triangle",
    "[integration][mesh-bundle]")
{
  vsr::scene::Scene scene;
  auto geometry = scene.createObject<vsr::scene::Geometry>(
      vsr::scene::tokens::geometry::triangle);
  const std::vector<float> original = {
      -1.f, -1.f, 0.f, 1.f, -1.f, 0.f, 0.f, 1.f, 0.f};
  auto positions = scene.createArray(ANARI_FLOAT32_VEC3, 3);
  positions->setData(original.data());
  geometry->setParameterObject("vertex.position", *positions);
  auto normals = scene.createArray(ANARI_FLOAT32_VEC3, 3);
  const std::vector<float> normalData = {
      0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
  normals->setData(normalData.data());
  geometry->setParameterObject("vertex.normal", *normals);

  const std::string header =
      R"({"revision":1,"byte_order":"little","fields":{"vertex.position":{"action":"replace","payload":"p","enabled":true,"association":"vertex"}},"arrays":{"p":{"element_type":"ANARI_FLOAT32_VEC3","count":3,"byte_length":36}}})";
  auto bytes = carrier(
      header, {-0.25f, -0.25f, 0.f, 0.25f, -0.25f, 0.f, 0.f, 0.25f, 0.f});
  mesh::Bundle bundle;
  REQUIRE(mesh::decodeBundle(std::move(bytes), bundle) == nullptr);
  mesh::MeshReplacement replacement;
  REQUIRE(mesh::prepareMesh(*geometry, bundle, replacement) == nullptr);
  REQUIRE(replacement.needed[0]);
  REQUIRE(replacement.changed);
  REQUIRE(geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position")
      == positions.data());
  REQUIRE(geometry->parameterValueAsObject<vsr::scene::Array>("vertex.normal")
      == normals.data());
  REQUIRE(std::memcmp(positions->data(), original.data(), 36) == 0);

  SECTION("a retained attribute with invalid cardinality rejects")
  {
    auto shortNormals = scene.createArray(ANARI_FLOAT32_VEC3, 2);
    shortNormals->setData(normalData.data());
    geometry->setParameterObject("vertex.normal", *shortNormals);
    REQUIRE(std::string(mesh::prepareMesh(*geometry, bundle, replacement))
        == "attribute_count_mismatch");
  }
  SECTION("a count change without matching bytes rejects")
  {
    bundle.arrays[0].count = 6;
    REQUIRE(std::string(mesh::prepareMesh(*geometry, bundle, replacement))
        == "invalid_payload_length");
  }
  SECTION("an optional replacement shares its declared position payload")
  {
    bundle.fields.push_back({"vertex.normal", mesh::Action::REPLACE, true, 0});
    REQUIRE(mesh::prepareMesh(*geometry, bundle, replacement) == nullptr);
    REQUIRE(replacement.newCount == 1);
  }
  SECTION("disabled stored attributes remain part of final validation")
  {
    geometry->parameter("vertex.normal")->setEnabled(false);
    auto nonfinite = normalData;
    nonfinite[0] = std::numeric_limits<float>::infinity();
    normals->setData(nonfinite.data());
    REQUIRE(std::string(mesh::prepareMesh(*geometry, bundle, replacement))
        == "nonfinite_mesh_value");
  }
  SECTION("indices are checked against the proposed vertex count")
  {
    auto indices = scene.createArray(ANARI_UINT32_VEC3, 1);
    const std::uint32_t invalid[] = {0, 1, 3};
    indices->setData(static_cast<const void *>(invalid));
    geometry->setParameterObject("primitive.index", *indices);
    REQUIRE(std::string(mesh::prepareMesh(*geometry, bundle, replacement))
        == "index_out_of_range");
  }
  SECTION("an equivalent deliberately split payload retains split intent")
  {
    auto exact = carrier(header, original);
    REQUIRE(mesh::decodeBundle(std::move(exact), bundle) == nullptr);
    geometry->setParameterObject("vertex.normal", *positions);
    REQUIRE(mesh::prepareMesh(*geometry, bundle, replacement) == nullptr);
    REQUIRE(replacement.changed);
    REQUIRE(geometry->parameterValueAsObject<vsr::scene::Array>("vertex.normal")
        == positions.data());
  }
  SECTION("replaced bad samples are not retained in the proposed mesh")
  {
    auto nonfinite = original;
    nonfinite[0] = std::numeric_limits<float>::infinity();
    positions->setData(nonfinite.data());
    REQUIRE(mesh::prepareMesh(*geometry, bundle, replacement) == nullptr);
    // Splitting an alias does not repair an unchanged field that still uses
    // the old samples.
    geometry->setParameterObject("vertex.normal", *positions);
    REQUIRE(std::string(mesh::prepareMesh(*geometry, bundle, replacement))
        == "nonfinite_mesh_value");
  }
  SECTION("mapping any retained dependency refuses preflight")
  {
    normals->map();
    REQUIRE(std::string(mesh::prepareMesh(*geometry, bundle, replacement))
        == "array_mapped");
    normals->unmap();
  }
}

TEST_CASE("Mesh bundle decoder refuses malformed or expansive envelopes",
    "[integration][mesh-bundle]")
{
  const std::string good =
      R"({"revision":1,"byte_order":"little","fields":{"vertex.position":{"action":"replace","payload":"p","enabled":true,"association":"vertex"}},"arrays":{"p":{"element_type":"ANARI_FLOAT32_VEC3","count":3,"byte_length":36}}})";
  std::string header = good;
  const auto replace = [&](const std::string &from, const std::string &to) {
    const auto offset = header.find(from);
    REQUIRE(offset != std::string::npos);
    header.replace(offset, from.size(), to);
  };
  SECTION("duplicate declarations")
  {
    replace("\"revision\":1", "\"revision\":1,\"revision\":1");
  }
  SECTION("unknown revision")
  {
    replace("\"revision\":1", "\"revision\":2");
  }
  SECTION("external array storage")
  {
    replace("\"count\":3", "\"path\":\"/etc/passwd\",\"count\":3");
  }
  SECTION("arbitrary object closure")
  {
    replace("\"fields\":", "\"objects\":[],\"fields\":");
  }
  SECTION("unknown field")
  {
    replace("vertex.position", "surface.geometry");
  }
  SECTION("unknown action")
  {
    replace("replace", "add");
  }
  SECTION("unknown payload")
  {
    replace("\"payload\":\"p\"", "\"payload\":\"q\"");
  }
  SECTION("wrong association")
  {
    replace("\"association\":\"vertex\"", "\"association\":\"primitive\"");
  }
  SECTION("wrong enabled type")
  {
    replace("\"enabled\":true", "\"enabled\":1");
  }
  SECTION("unsupported position type")
  {
    replace("ANARI_FLOAT32_VEC3", "ANARI_INT32_VEC3");
  }
  SECTION("reference payload")
  {
    replace("ANARI_FLOAT32_VEC3", "ANARI_GEOMETRY");
  }
  SECTION("fractional count")
  {
    replace("\"count\":3", "\"count\":3.5");
  }
  SECTION("overflow count")
  {
    replace("\"count\":3", "\"count\":18446744073709551615");
  }
  SECTION("empty payload")
  {
    replace("\"count\":3", "\"count\":0");
  }
  SECTION("wrong byte length")
  {
    replace("\"byte_length\":36", "\"byte_length\":35");
  }
  SECTION("truncated payload")
  {
    replace("\"count\":3", "\"count\":4");
    replace("\"byte_length\":36", "\"byte_length\":48");
  }
  SECTION("invalid UTF-8")
  {
    header.insert(header.size() - 1, ",\"source\":{\"text\":\"");
    header.insert(header.size() - 1, 1, char(0xff));
    header.insert(header.size() - 1, "\"}");
  }
  auto bytes = carrier(header, {-1, -1, 0, 1, -1, 0, 0, 1, 0});
  mesh::Bundle result;
  REQUIRE(mesh::decodeBundle(std::move(bytes), result) != nullptr);
  REQUIRE(result.bytes.empty());
  REQUIRE(result.arrays.empty());
  REQUIRE(result.fields.empty());
}

TEST_CASE("Mesh decoding keeps result ownership on rejection and cancellation",
    "[integration][mesh-bundle]")
{
  const std::string header =
      R"({"revision":1,"byte_order":"little","fields":{"vertex.position":{"action":"replace","payload":"p","enabled":true,"association":"vertex"}},"arrays":{"p":{"element_type":"ANARI_FLOAT32_VEC3","count":3,"byte_length":36}}})";
  mesh::Bundle result;
  auto exact = carrier(header, {-1, -1, 0, 1, -1, 0, 0, 1, 0});
  const auto expected = exact;
  REQUIRE(mesh::decodeBundle(std::move(exact), result) == nullptr);
  auto invalid = expected;
  SECTION("bad magic")
  {
    invalid[0] = 'X';
  }
  SECTION("oversized header")
  {
    invalid[8] = invalid[9] = invalid[10] = invalid[11] = 255;
  }
  SECTION("trailing data")
  {
    invalid.push_back(0);
  }
  SECTION("nonfinite position")
  {
    const float value = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(invalid.data() + invalid.size() - 36, &value, sizeof(value));
  }
  SECTION("cancellation")
  {
    REQUIRE(std::string(mesh::decodeBundle(std::move(invalid), result, [] {
      return true;
    })) == "cancelled");
    REQUIRE(result.bytes == expected);
    return;
  }
  SECTION("cancellation during sample validation")
  {
    auto largeHeader = header;
    largeHeader.replace(largeHeader.find("\"count\":3"), 9, "\"count\":3000");
    largeHeader.replace(
        largeHeader.find("\"byte_length\":36"), 16, "\"byte_length\":36000");
    auto large = carrier(largeHeader, std::vector<float>(9000, 0.f));
    unsigned observations = 0;
    REQUIRE(std::string(mesh::decodeBundle(std::move(large), result, [&] {
      return ++observations == 4;
    })) == "cancelled");
    REQUIRE(result.bytes == expected);
    return;
  }
  REQUIRE(mesh::decodeBundle(std::move(invalid), result) != nullptr);
  REQUIRE(result.bytes == expected);
  REQUIRE(result.arrays.size() == 1);
  REQUIRE(result.fields.size() == 1);
}

TEST_CASE("Mesh decoder preserves all supported numeric representations",
    "[integration][mesh-bundle]")
{
  std::size_t accepted = 0;
  for (int t = ANARI_INT8; t <= ANARI_FLOAT64_VEC4; ++t) {
    const auto type = anari::DataType(t);
    const auto repr = mesh::representation(type);
    if (!repr.base)
      continue;
    const auto spelling = std::string(repr.base)
        + (repr.components == 1 ? ""
                                : "_VEC" + std::to_string(repr.components));
    const auto length = repr.scalarBytes * repr.components;
    const auto header =
        "{\"revision\":1,\"byte_order\":\"little\",\"fields\":{"
        "\"vertex.attribute0\":{\"action\":\"preserve\",\"payload\":\"p\","
        "\"enabled\":false,\"association\":\"vertex\"}},\"arrays\":{\"p\":{"
        "\"element_type\":\""
        + spelling + "\",\"count\":1,\"byte_length\":" + std::to_string(length)
        + "}}}";
    auto bytes = carrier(header, {});
    bytes.resize(bytes.size() + length, 0);
    mesh::Bundle bundle;
    REQUIRE(mesh::decodeBundle(std::move(bytes), bundle) == nullptr);
    REQUIRE(bundle.arrays[0].elementType == type);
    REQUIRE(bundle.arrays[0].byteLength == length);
    REQUIRE_FALSE(bundle.fields[0].enabled);
    ++accepted;
  }
  REQUIRE(accepted == 64);
}

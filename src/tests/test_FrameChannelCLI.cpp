// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// stb_image
#include "stb_image.h"
// std
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef VSR_OFFLINE_EXECUTABLE
namespace {
struct OfflineRun
{
  std::filesystem::path directory;
  int result;
  std::string diagnostic;

  OfflineRun(const std::string &name,
      const std::string &options,
      const std::string &environment = "");
  void checkGrayscale(const std::vector<unsigned char> &expected) const;
};

OfflineRun::OfflineRun(const std::string &name,
    const std::string &options,
    const std::string &environment)
{
  directory = std::filesystem::path(CHANNEL_TEST_DIRECTORY) / name;
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  const std::string command = "cd \"" + directory.string() + "\" && "
      + environment + " \"" + VSR_OFFLINE_EXECUTABLE
      + "\" --lib channel_test --renderer diagnostic --campos 0 0 3 -w 2 -h 2 -s 1 -o image.png "
      + options + " > run.log 2>&1";
  result = std::system(command.c_str());
  std::ifstream log(directory / "run.log");
  diagnostic.assign(std::istreambuf_iterator<char>(log), {});
}

void OfflineRun::checkGrayscale(
    const std::vector<unsigned char> &expected) const
{
  INFO(diagnostic);
  REQUIRE(result == 0);
  int width = 0, height = 0, channels = 0;
  auto *pixels = stbi_load(
      (directory / "image.png").c_str(), &width, &height, &channels, 4);
  REQUIRE(pixels);
  std::vector<unsigned char> image(pixels, pixels + width * height * 4);
  stbi_image_free(pixels);
  REQUIRE(width == 2);
  REQUIRE(height == 2);
  for (size_t i = 0; i < expected.size(); ++i) {
    CHECK(image[4 * i] == expected[i]);
    CHECK(image[4 * i + 1] == expected[i]);
    CHECK(image[4 * i + 2] == expected[i]);
    CHECK(image[4 * i + 3] == 255);
  }
}
} // namespace

TEST_CASE("Offline writes native fixed16 signed direction pixels",
    "[FrameChannelCLI][DirectionCLI]")
{
  const auto options = GENERATE("--channel shadingNormal",
      "--channel CustomDirection --visualization normal");
  OfflineRun run(
      "fixed16-direction", options, "VSR_CHANNEL_TEST_METADATA=directions");
  INFO(run.diagnostic);
  REQUIRE(run.result == 0);
  int width = 0, height = 0, channels = 0;
  auto *pixels = stbi_load(
      (run.directory / "image.png").c_str(), &width, &height, &channels, 4);
  REQUIRE(pixels);
  std::vector<unsigned char> image(pixels, pixels + width * height * 4);
  stbi_image_free(pixels);
  REQUIRE(width == 2);
  REQUIRE(height == 2);
  // File rows are inverted relative to the ANARI frame. Normal has no gamma.
  const std::vector<unsigned char> expected{
      159, 96, 223, 255, 128, 128, 128, 255, 0, 0, 128, 255, 255, 191, 64, 255};
  CHECK(image == expected);
}

TEST_CASE("Offline writes standard identity colors through shared defaults",
    "[FrameChannelCLI][IdentityChannels]")
{
  const auto channel = GENERATE("objectId", "primitiveId", "instanceId");
  OfflineRun run("identity-colors",
      std::string("--channel ") + channel,
      "VSR_CHANNEL_TEST_METADATA=identities");
  INFO(run.diagnostic);
  REQUIRE(run.result == 0);
  int width = 0, height = 0, channels = 0;
  auto *pixels = stbi_load(
      (run.directory / "image.png").c_str(), &width, &height, &channels, 4);
  REQUIRE(pixels);
  std::vector<unsigned char> image(pixels, pixels + width * height * 4);
  stbi_image_free(pixels);
  REQUIRE(width == 2);
  REQUIRE(height == 2);
  const std::vector<unsigned char> expected{
      95, 58, 208, 255, 0, 0, 0, 255, 159, 152, 222, 255, 210, 145, 15, 255};
  for (size_t i = 0; i < expected.size(); ++i)
    CHECK(std::abs(int(image[i]) - int(expected[i])) <= 1);
}

TEST_CASE(
    "Offline renders objectId Edges and rejects incompatible identity modes",
    "[FrameChannelCLI][IdentityChannels]")
{
  SECTION("Edges uses objectId, not an invented channel")
  {
    OfflineRun run("identity-edges",
        "--channel objectId --visualization edges",
        "VSR_CHANNEL_TEST_METADATA=identities");
    run.checkGrayscale({255, 0, 255, 255});
  }
  SECTION("incompatible modes provide listing guidance")
  {
    const auto options = GENERATE("--channel primitiveId --visualization edges",
        "--channel instanceId --visualization edges",
        "--channel objectId --visualization color",
        "--channel Temperature_RAW --visualization id-colors",
        "--channel edges");
    OfflineRun run(
        "identity-invalid", options, "VSR_CHANNEL_TEST_METADATA=identities");
    CHECK(run.result != 0);
    CHECK(run.diagnostic.find("--list-channels") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(run.directory / "image.png"));
  }
  SECTION("explicit failed IDs never save background as success")
  {
    OfflineRun run("identity-failed",
        "--channel objectId",
        "VSR_CHANNEL_TEST_METADATA=identities VSR_CHANNEL_TEST_MAP=identity-null");
    CHECK(run.result != 0);
    CHECK(run.diagnostic.find("objectId") != std::string::npos);
    CHECK(run.diagnostic.find("channel_test") != std::string::npos);
    CHECK(run.diagnostic.find("diagnostic") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(run.directory / "image.png"));
  }
}

TEST_CASE(
    "Offline writes exact unprefixed scalar selection in file orientation",
    "[FrameChannelCLI]")
{
  OfflineRun run("scalar-auto", "--channel Temperature_RAW");
  run.checkGrayscale({170, 255, 0, 85});
}

TEST_CASE("Offline applies explicit Fixed bounds and numeric UINT32 defaults",
    "[FrameChannelCLI]")
{
  SECTION("Fixed FLOAT32 clips without beauty transformation")
  {
    OfflineRun run("scalar-fixed",
        "--channel Temperature_RAW --visualization grayscale --range fixed --range-min 1 --range-max 2");
    run.checkGrayscale({255, 255, 0, 0});
  }
  SECTION("UINT32 has numeric Auto range")
  {
    OfflineRun run("integer-auto",
        "--channel Temperature_RAW --range auto",
        "VSR_CHANNEL_TEST_SCALAR=uint32");
    run.checkGrayscale({170, 255, 0, 85});
  }
  SECTION("UINT32 Fixed uses both explicit bounds")
  {
    OfflineRun run("integer-fixed",
        "--channel Temperature_RAW --range-max 30 --range fixed --range-min 20",
        "VSR_CHANNEL_TEST_SCALAR=uint32");
    run.checkGrayscale({255, 255, 0, 0});
  }
}

TEST_CASE("Offline visualizes custom vector components magnitude and Color",
    "[FrameChannelCLI]")
{
  SECTION("vec2 component uses file orientation")
  {
    OfflineRun run("vector-component",
        "--channel motionVectors --visualization component-y");
    run.checkGrayscale({0, 255, 0, 255});
  }
  SECTION("vec2 magnitude")
  {
    OfflineRun run("vector-magnitude",
        "--channel motionVectors --visualization magnitude");
    run.checkGrayscale({0, 255, 0, 255});
  }
  SECTION("vec2 documented default")
  {
    OfflineRun run("vector-default", "--channel motionVectors");
    run.checkGrayscale({0, 255, 0, 255});
  }
  SECTION("vec3 default Color")
  {
    OfflineRun run("vector-color",
        "--channel motionVectors",
        "VSR_CHANNEL_TEST_VECTOR=vec3");
    INFO(run.diagnostic);
    REQUIRE(run.result == 0);
    int width = 0, height = 0, channels = 0;
    auto *pixels = stbi_load(
        (run.directory / "image.png").c_str(), &width, &height, &channels, 4);
    REQUIRE(pixels);
    std::vector<unsigned char> image(pixels, pixels + width * height * 4);
    stbi_image_free(pixels);
    REQUIRE(width == 2);
    REQUIRE(height == 2);
    CHECK(image
        == std::vector<unsigned char>{255,
            255,
            0,
            255,
            255,
            255,
            255,
            255,
            0,
            255,
            255,
            255,
            255,
            0,
            255,
            255});
  }
}

TEST_CASE("Offline rejects unusable requests without saving substitute images",
    "[FrameChannelCLI]")
{
  const auto options = GENERATE("--channel motionVectors --visualization color",
      "--channel motionVectors --visualization component-z",
      "--channel motionVectors --visualization component-w",
      "--channel motionVectors --visualization grayscale",
      "--channel temperature_raw",
      "--channel channel.Temperature_RAW",
      "--channel unusual",
      "--channel Temperature_RAW --visualization color",
      "--channel Temperature_RAW --visualization GRAYSCALE",
      "--channel Temperature_RAW --range fixed",
      "--channel Temperature_RAW --range fixed --range-min 0",
      "--channel Temperature_RAW --range fixed --range-min 1 --range-max 1",
      "--channel Temperature_RAW --range fixed --range-min 2 --range-max 1",
      "--channel Temperature_RAW --range fixed --range-min nan --range-max 1",
      "--channel Temperature_RAW --range fixed --range-min 0 --range-max inf",
      "--channel Temperature_RAW --range fixed --range-min 0x --range-max 1",
      "--channel Temperature_RAW --range auto --range-min 0 --range-max 1",
      "--channel Temperature_RAW --range invalid",
      "--channel Temperature_RAW --visualization",
      "--channel Temperature_RAW --range-min");
  OfflineRun run("invalid-request", options);
  INFO(run.diagnostic);
  CHECK(run.result != 0);
  CHECK(run.diagnostic.find("Frame Channel '") != std::string::npos);
  CHECK(run.diagnostic.find("--list-channels") != std::string::npos);
  CHECK_FALSE(std::filesystem::exists(run.directory / "image.png"));
}

TEST_CASE("Offline reports failed selected maps with device and renderer",
    "[FrameChannelCLI]")
{
  const auto map = GENERATE("null", "wrong-type", "wrong-size");
  OfflineRun run("failed-map",
      "--channel Temperature_RAW",
      std::string("VSR_CHANNEL_TEST_MAP=") + map);
  INFO(run.diagnostic);
  CHECK(run.result != 0);
  CHECK(run.diagnostic.find("Temperature_RAW") != std::string::npos);
  CHECK(run.diagnostic.find("channel_test") != std::string::npos);
  CHECK(run.diagnostic.find("diagnostic") != std::string::npos);
  CHECK(run.diagnostic.find("--list-channels") != std::string::npos);
  CHECK_FALSE(std::filesystem::exists(run.directory / "image.png"));
}

TEST_CASE(
    "Offline preserves default Color and file orientation", "[FrameChannelCLI]")
{
  const auto options = GENERATE("", "--channel color --visualization color");
  OfflineRun run("default-color", options);
  INFO(run.diagnostic);
  REQUIRE(run.result == 0);
  int width = 0, height = 0, channels = 0;
  auto *pixels = stbi_load(
      (run.directory / "image.png").c_str(), &width, &height, &channels, 4);
  REQUIRE(pixels);
  std::vector<unsigned char> image(pixels, pixels + width * height * 4);
  stbi_image_free(pixels);
  REQUIRE(width == 2);
  REQUIRE(height == 2);
  CHECK(image
      == std::vector<unsigned char>{
          7, 8, 9, 255, 10, 11, 12, 255, 1, 2, 3, 255, 4, 5, 6, 255});
}

TEST_CASE("Offline negotiates float-only Color and resolves its defaults",
    "[FrameChannelCLI][ChannelReview]")
{
  OfflineRun component("offline-float-color-component",
      "--channel color --visualization component-x",
      "VSR_CHANNEL_TEST_VECTOR=vec4");
  component.checkGrayscale({128, 128, 128, 128});
  OfflineRun beauty(
      "offline-float-color-default", "", "VSR_CHANNEL_TEST_VECTOR=vec4");
  INFO(beauty.diagnostic);
  REQUIRE(beauty.result == 0);
  int width = 0, height = 0, channels = 0;
  auto *pixels = stbi_load(
      (beauty.directory / "image.png").c_str(), &width, &height, &channels, 4);
  REQUIRE(pixels);
  const std::vector<unsigned char> image(pixels, pixels + width * height * 4);
  stbi_image_free(pixels);
  CHECK(image
      == std::vector<unsigned char>{0,
          255,
          255,
          255,
          0,
          255,
          255,
          255,
          0,
          255,
          255,
          255,
          0,
          255,
          255,
          255});
}

TEST_CASE(
    "Offline retains earlier animation images but never saves a failed frame",
    "[FrameChannelCLI]")
{
  OfflineRun run("animation-failure",
      "--channel Temperature_RAW --anim-out-dir . --num-frames 2 --anim-prefix sample_",
      "VSR_CHANNEL_TEST_MAP=after-first");
  INFO(run.diagnostic);
  CHECK(run.result != 0);
  CHECK(std::filesystem::exists(run.directory / "sample_0000.png"));
  CHECK_FALSE(std::filesystem::exists(run.directory / "sample_0001.png"));
  CHECK_FALSE(std::filesystem::exists(run.directory / "image.png"));
  CHECK(run.diagnostic.find("Temperature_RAW") != std::string::npos);
}

TEST_CASE("Offline does not label pending selection as a failed map",
    "[FrameChannelCLI]")
{
  OfflineRun run("pending-selection", "--channel Temperature_RAW -s 0");
  INFO(run.diagnostic);
  CHECK(run.result != 0);
  CHECK(run.diagnostic.find("still pending") != std::string::npos);
  CHECK_FALSE(std::filesystem::exists(run.directory / "image.png"));
}

TEST_CASE("Offline honors manual eye and look-at at the ANARI boundary",
    "[FrameChannelCLI][OfflineCamera]")
{
  struct CameraCase
  {
    const char *options;
    std::array<float, 3> eye;
    std::array<float, 3> target;
  };
  const CameraCase cases[] = {
      {"--campos 0 0 3 --lookpos 0 0 0", {0, 0, 3}, {0, 0, 0}},
      {"--campos 0 0 -3 --lookpos 0 0 0", {0, 0, -3}, {0, 0, 0}},
      {"--campos 2 3 6 --lookpos 0 0 0", {2, 3, 6}, {0, 0, 0}},
      {"--campos 3 5 9 --lookpos 1 2 3", {3, 5, 9}, {1, 2, 3}}};
  const auto index = GENERATE(0, 1, 2, 3);
  const auto &pose = cases[index];
  OfflineRun run("manual-camera-" + std::to_string(index),
      pose.options,
      "VSR_CHANNEL_TEST_CAMERA_LOG=camera.txt");
  struct Cleanup
  {
    std::filesystem::path directory;
    ~Cleanup()
    {
      std::filesystem::remove_all(directory);
    }
  } cleanup{run.directory};
  INFO(run.diagnostic);
  INFO(pose.options);
  REQUIRE(run.result == 0);
  std::ifstream capture(run.directory / "camera.txt");
  std::array<float, 3> eye{}, direction{};
  for (auto &value : eye)
    REQUIRE(bool(capture >> value));
  for (auto &value : direction)
    REQUIRE(bool(capture >> value));

  // Expected viewing direction is independently derived from the CLI points,
  // not from the application's Euler conversion or Manipulator convention.
  float distanceSquared = 0.f;
  for (size_t i = 0; i < 3; ++i) {
    const float delta = pose.target[i] - pose.eye[i];
    distanceSquared += delta * delta;
  }
  const float distance = std::sqrt(distanceSquared);
  for (size_t i = 0; i < 3; ++i) {
    CHECK(eye[i] == Approx(pose.eye[i]).margin(1e-5));
    CHECK(direction[i]
        == Approx((pose.target[i] - pose.eye[i]) / distance).margin(1e-5));
  }
}
#endif

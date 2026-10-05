// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include "catch.hpp"
#include "stb_image.h"
#include "vsr/app/ApplicationDump.h"
#include "vsr/app/Context.h"

#ifdef VSR_RENDER_EXECUTABLE
namespace {
struct RenderRun
{
  std::filesystem::path directory;
  int result{0};
  std::string diagnostic;

  RenderRun(const std::string &name,
      const std::string &options,
      const std::function<void(vsr::core::DataNode &)> &edit = {},
      const std::string &environment = "");
  std::vector<unsigned char> image() const;
  void checkGrayscale(const std::vector<unsigned char> &expected) const;
  void checkFailure(const std::string &context) const;
};

RenderRun::RenderRun(const std::string &name,
    const std::string &options,
    const std::function<void(vsr::core::DataNode &)> &edit,
    const std::string &environment)
{
  directory = std::filesystem::path(CHANNEL_TEST_DIRECTORY) / name;
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  vsr::app::Context context;
  context.offline.frame.width = 2;
  context.offline.frame.height = 2;
  context.offline.frame.samples = 2;
  context.offline.renderer.libraryName = "channel_test";
  context.offline.renderer.activeRenderer = 0;
  vsr::scene::Object renderer(ANARI_RENDERER, "diagnostic");
  // The saved object name is not the ANARI renderer subtype.
  renderer.setName("Saved Renderer");
  context.offline.renderer.rendererObjects.push_back(std::move(renderer));
  context.view.poses.emplace_back();
  vsr::rendering::FrameChannelSelection selection;
  selection.deviceName = "Temperature_RAW";
  selection.visualization = "grayscale";
  context.offline.channelSelection = selection;
  vsr::core::DataTree tree;
  REQUIRE(vsr::app::serialize_ApplicationDump(context, tree.root()));
  if (edit)
    edit(tree.root()["offlineRendering"]);
  REQUIRE(tree.save((directory / "state.vsr").c_str()));
  const std::string command = "cd \"" + directory.string() + "\" && "
      + environment + " VSR_CHANNEL_TEST_RENDERER=diagnostic \""
      + VSR_RENDER_EXECUTABLE + "\" state.vsr " + options + " > run.log 2>&1";
  result = std::system(command.c_str());
  std::ifstream log(directory / "run.log");
  diagnostic.assign(std::istreambuf_iterator<char>(log), {});
}

std::vector<unsigned char> RenderRun::image() const
{
  INFO(diagnostic);
  REQUIRE(result == 0);
  int width = 0, height = 0, channels = 0;
  auto *pixels = stbi_load((directory / "vsrRender_0000.png").c_str(),
      &width,
      &height,
      &channels,
      4);
  REQUIRE(pixels);
  std::vector<unsigned char> output(pixels, pixels + width * height * 4);
  stbi_image_free(pixels);
  REQUIRE(width == 2);
  REQUIRE(height == 2);
  return output;
}

void RenderRun::checkGrayscale(const std::vector<unsigned char> &expected) const
{
  const auto output = image();
  for (size_t i = 0; i < expected.size(); ++i) {
    CHECK(output[4 * i] == expected[i]);
    CHECK(output[4 * i + 1] == expected[i]);
    CHECK(output[4 * i + 2] == expected[i]);
    CHECK(output[4 * i + 3] == 255);
  }
}

void RenderRun::checkFailure(const std::string &context) const
{
  INFO(diagnostic);
  REQUIRE(result != 0);
  CHECK(diagnostic.find(context) != std::string::npos);
  for (const auto &entry : std::filesystem::directory_iterator(directory))
    CHECK(entry.path().extension() != ".png");
}
} // namespace

TEST_CASE("Render preserves saved custom scalar pixels and orientation",
    "[RenderChannels]")
{
  RenderRun run("render-saved-scalar", "");
  run.checkGrayscale({170, 255, 0, 85});
}

TEST_CASE("Render overrides saved range and visualization", "[RenderChannels]")
{
  RenderRun fixed("render-fixed", "--range fixed --range-min 0 --range-max 6");
  fixed.checkGrayscale({85, 128, 0, 43});
  RenderRun vector(
      "render-vector", "--channel motionVectors --visualization component-y");
  vector.checkGrayscale({0, 255, 0, 255});
  RenderRun magnitude(
      "render-magnitude", "--channel motionVectors --visualization magnitude");
  magnitude.checkGrayscale({0, 255, 0, 255});
  RenderRun defaults("render-default", "--channel motionVectors");
  const auto image = defaults.image();
  CHECK(image[0] == 0);
  CHECK(image[1] == 0);
  CHECK(image[4] == 255);
  CHECK(image[5] == 255);
}

TEST_CASE(
    "Render preserves compatible saved component settings", "[RenderChannels]")
{
  RenderRun run("render-compatible", "--channel normal", [](auto &root) {
    auto &named = root["channelSelection"];
    named["deviceName"] = "channel.motionVectors";
    named["visualization"] = "component-y";
    named["rangePolicy"] = "fixed";
    named["rangeMin"] = 0.f;
    named["rangeMax"] = 4.f;
  });
  run.checkGrayscale({191, 128, 64, 0});
}

TEST_CASE("Render lists saved renderer without producing an image",
    "[RenderChannels]")
{
  RenderRun run(
      "render-list", "--list-channels", {}, "VSR_CHANNEL_TEST_FORBID_RENDER=1");
  INFO(run.diagnostic);
  REQUIRE(run.result == 0);
  CHECK(run.diagnostic.find("Temperature_RAW") != std::string::npos);
  CHECK(run.diagnostic.find("motionVectors") != std::string::npos);
  CHECK(run.diagnostic.find("component-y") != std::string::npos);
  CHECK_FALSE(std::filesystem::exists(run.directory / "vsrRender_0000.png"));
}

TEST_CASE("Render migrates legacy depth and rejects unusable saved intent",
    "[RenderChannels]")
{
  RenderRun depth(
      "render-legacy-depth",
      "",
      [](auto &root) {
        root.remove("channelSelection");
        root["aov"]["aovType"] = int(vsr::rendering::AOVType::DEPTH);
        root["aov"]["depthMin"] = 0.f;
        root["aov"]["depthMax"] = 6.f;
      },
      "VSR_CHANNEL_TEST_METADATA=depth");
  depth.checkGrayscale({85, 128, 0, 43});
  RenderRun unknown("render-legacy-unknown", "", [](auto &root) {
    root.remove("channelSelection");
    root["aov"]["aovType"] = 987;
  });
  unknown.checkFailure("unknown saved legacy AOV");
  RenderRun unavailable("render-saved-unavailable", "", [](auto &root) {
    root["channelSelection"]["deviceName"] = "channel.absent";
  });
  unavailable.checkFailure("absent");
  RenderRun recovered(
      "render-saved-override", "--channel Temperature_RAW", [](auto &root) {
        root["channelSelection"]["deviceName"] = "channel.absent";
      });
  recovered.checkGrayscale({170, 255, 0, 85});
}

TEST_CASE(
    "Render writes identity and migrated inverted edges", "[RenderChannels]")
{
  RenderRun ids("render-ids",
      "--channel objectId --visualization id-colors",
      {},
      "VSR_CHANNEL_TEST_METADATA=identities");
  const auto image = ids.image();
  const std::vector<unsigned char> expected{
      95, 58, 208, 255, 0, 0, 0, 255, 159, 152, 222, 255, 210, 145, 15, 255};
  for (size_t i = 0; i < expected.size(); ++i)
    CHECK(std::abs(int(image[i]) - int(expected[i])) <= 1);
  RenderRun edges(
      "render-edges",
      "",
      [](auto &root) {
        root.remove("channelSelection");
        root["aov"]["aovType"] = int(vsr::rendering::AOVType::EDGES);
        root["aov"]["edgeInvert"] = true;
      },
      "VSR_CHANNEL_TEST_METADATA=identities");
  edges.checkGrayscale({0, 0, 0, 0});
}

TEST_CASE(
    "Render negotiates advertised Color storage for diagnostics and beauty",
    "[RenderChannels][ChannelReview]")
{
  const auto colorState = [](auto &root) {
    root["channelSelection"]["deviceName"] = "channel.color";
    root["channelSelection"]["visualization"] = "color";
  };
  RenderRun diagnostic(
      "render-color-component", "--visualization component-x", colorState);
  diagnostic.checkGrayscale({170, 255, 0, 85});
  RenderRun floatOnly("render-color-float-component",
      "--visualization component-x",
      colorState,
      "VSR_CHANNEL_TEST_VECTOR=vec4");
  floatOnly.checkGrayscale({128, 128, 128, 128});
  RenderRun beauty("render-color-float-beauty",
      "",
      colorState,
      "VSR_CHANNEL_TEST_VECTOR=vec4");
  CHECK(beauty.image()
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

TEST_CASE("Render repairs invalid saved range only with valid overrides",
    "[RenderChannels][ChannelReview]")
{
  const auto invalidRange = [](auto &root) {
    root["channelSelection"]["rangePolicy"] = "fixed";
    root["channelSelection"]["rangeMin"] = 4.f;
    root["channelSelection"]["rangeMax"] = 2.f;
  };
  RenderRun automatic(
      "render-invalid-range-auto", "--range auto", invalidRange);
  automatic.checkGrayscale({170, 255, 0, 85});
  RenderRun fixed("render-invalid-range-fixed",
      "--range fixed --range-min 0 --range-max 6",
      invalidRange);
  fixed.checkGrayscale({85, 128, 0, 43});
  RenderRun unchanged("render-invalid-range-unchanged", "", invalidRange);
  unchanged.checkFailure("Fixed range");
  RenderRun malformed("render-invalid-field", "--range auto", [](auto &root) {
    root["channelSelection"]["visualization"] = 123;
  });
  malformed.checkFailure("visualization");
}

TEST_CASE("Render parser errors name the requested channel and listing",
    "[RenderChannels][ChannelReview]")
{
  const auto options = GENERATE("--channel Temperature_RAW --range-min nan",
      "--range-min nan --channel Temperature_RAW",
      "--channel Temperature_RAW --range invalid",
      "--channel Temperature_RAW --visualization",
      "--channel Temperature_RAW --visualization --range auto",
      "--visualization --range auto --channel Temperature_RAW");
  RenderRun invalid("render-contextual-error", options);
  invalid.checkFailure("Temperature_RAW");
  CHECK(invalid.diagnostic.find("--list-channels") != std::string::npos);
  RenderRun merged("render-merged-range-error",
      "--channel color --range fixed --range-min 2 --range-max 1");
  merged.checkFailure("Frame Channel 'color'");
}

TEST_CASE("Render failures never write misleading output", "[RenderChannels]")
{
  const auto options = GENERATE("--channel absent",
      "--visualization edges",
      "--channel unusual",
      "--range fixed --range-min 2 --range-max 1",
      "--range fixed --range-min 0",
      "--range auto --range-min 0 --range-max 1",
      "--range-min nan",
      "--channel");
  RenderRun invalid("render-invalid", options);
  invalid.checkFailure("Error:");
  RenderRun failed("render-map-failed", "", {}, "VSR_CHANNEL_TEST_MAP=null");
  failed.checkFailure("Temperature_RAW");
  CHECK(failed.diagnostic.find("channel_test") != std::string::npos);
  CHECK(failed.diagnostic.find("diagnostic") != std::string::npos);
  RenderRun second("render-second-sample-failed",
      "",
      {},
      "VSR_CHANNEL_TEST_MAP=after-first");
  second.checkFailure("Temperature_RAW");
}

TEST_CASE(
    "Render legacy default retains ordinary Color output", "[RenderChannels]")
{
  RenderRun run("render-color", "", [](auto &root) {
    root.remove("channelSelection");
    root["aov"]["aovType"] = int(vsr::rendering::AOVType::NONE);
  });
  CHECK(run.image()
      == std::vector<unsigned char>{
          7, 8, 9, 255, 10, 11, 12, 255, 1, 2, 3, 255, 4, 5, 6, 255});
}

TEST_CASE("Render preserves named vector identity and new-field precedence",
    "[RenderChannels]")
{
  RenderRun vector("render-saved-vector", "", [](auto &root) {
    auto &named = root["channelSelection"];
    named["deviceName"] = "channel.motionVectors";
    named["visualization"] = "component-y";
    named["rangePolicy"] = "fixed";
    named["rangeMin"] = 0.f;
    named["rangeMax"] = 6.f;
    root["aov"]["aovType"] = 999;
  });
  vector.checkGrayscale({43, 128, 43, 128});
  RenderRun ids(
      "render-saved-ids",
      "",
      [](auto &root) {
        root["channelSelection"]["deviceName"] = "channel.objectId";
        root["channelSelection"]["visualization"] = "id-colors";
        root["aov"]["aovType"] = int(vsr::rendering::AOVType::DEPTH);
      },
      "VSR_CHANNEL_TEST_METADATA=identities");
  const auto image = ids.image();
  CHECK(image[0] == Approx(95).margin(1));
  CHECK(image[4] == 0);
  RenderRun autoRange("render-range-override", "--range auto", [](auto &root) {
    root["channelSelection"]["rangePolicy"] = "fixed";
    root["channelSelection"]["rangeMin"] = 0.f;
    root["channelSelection"]["rangeMax"] = 6.f;
  });
  autoRange.checkGrayscale({170, 255, 0, 85});
}
#endif

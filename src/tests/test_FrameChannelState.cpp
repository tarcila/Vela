// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include "catch.hpp"
#include "vsr/app/ApplicationDump.h"
#include "vsr/app/Context.h"
#include "vsr/app/FrameChannelOptions.h"
#include "vsr/app/FrameChannelState.h"

TEST_CASE(
    "Application Dumps round trip named channel intent", "[FrameChannelState]")
{
  vsr::app::Context source;
  vsr::rendering::FrameChannelSelection selection;
  selection.deviceName = "channel.motionVectors";
  selection.visualization = "component-y";
  selection.rangePolicy = vsr::rendering::ChannelRangePolicy::FIXED;
  selection.rangeMin = -2.f;
  selection.rangeMax = 8.f;
  selection.invertEdges = true;
  source.offline.channelSelection = selection;
  vsr::core::DataTree tree;
  REQUIRE(vsr::app::serialize_ApplicationDump(source, tree.root()));
  vsr::app::Context restored;
  REQUIRE(vsr::app::deserialize_ApplicationDump(restored, tree.root()));
  REQUIRE(restored.offline.channelSelection);
  const auto &saved = *restored.offline.channelSelection;
  CHECK(saved.deviceName == "channel.motionVectors");
  CHECK(saved.visualization == "component-y");
  CHECK(saved.rangePolicy == vsr::rendering::ChannelRangePolicy::FIXED);
  CHECK(saved.rangeMin == -2.f);
  CHECK(saved.rangeMax == 8.f);
  CHECK(saved.invertEdges);
  CHECK(restored.offline.channelSelectionError.empty());
  CHECK_FALSE(tree.root().child("uiState"));
}

TEST_CASE("Legacy channel modes migrate by meaning", "[FrameChannelState]")
{
  using namespace vsr::rendering;
  const auto mode = GENERATE(AOVType::NONE,
      AOVType::DEPTH,
      AOVType::ALBEDO,
      AOVType::NORMAL,
      AOVType::EDGES,
      AOVType::OBJECT_ID,
      AOVType::PRIMITIVE_ID,
      AOVType::INSTANCE_ID);
  const std::vector<std::string> channels{"channel.color",
      "channel.depth",
      "channel.albedo",
      "channel.normal",
      "channel.objectId",
      "channel.objectId",
      "channel.primitiveId",
      "channel.instanceId"};
  const std::vector<std::string> modes{"color",
      "grayscale",
      "color",
      "normal",
      "edges",
      "id-colors",
      "id-colors",
      "id-colors"};
  vsr::core::DataTree tree;
  auto &legacy = tree.root()["legacy"];
  legacy["aovType"] = int(mode);
  legacy["depthMin"] = -2.f;
  legacy["depthMax"] = 8.f;
  legacy["edgeInvert"] = true;
  FrameChannelSelection selection;
  std::string error;
  REQUIRE(
      vsr::app::loadFrameChannelSelection(nullptr, &legacy, selection, error));
  CHECK(selection.deviceName == channels[size_t(mode)]);
  CHECK(selection.visualization == modes[size_t(mode)]);
  CHECK(selection.invertEdges);
  if (mode == AOVType::DEPTH) {
    CHECK(selection.rangePolicy == ChannelRangePolicy::FIXED);
    CHECK(selection.rangeMin == -2.f);
    CHECK(selection.rangeMax == 8.f);
    legacy.remove("depthMin");
    legacy.remove("depthMax");
    REQUIRE(vsr::app::loadFrameChannelSelection(
        nullptr, &legacy, selection, error));
    CHECK(selection.rangePolicy == ChannelRangePolicy::AUTO);
  }
}

TEST_CASE("Application Dump migration preserves absent legacy bounds",
    "[FrameChannelState]")
{
  vsr::core::DataTree tree;
  vsr::app::Context source;
  REQUIRE(vsr::app::serialize_ApplicationDump(source, tree.root()));
  auto &offline = tree.root()["offlineRendering"];
  offline.remove("channelSelection");
  offline["aov"].reset();
  offline["aov"]["aovType"] = int(vsr::rendering::AOVType::DEPTH);
  vsr::app::Context restored;
  REQUIRE(vsr::app::deserialize_ApplicationDump(restored, tree.root()));
  REQUIRE(restored.offline.channelSelection);
  CHECK(restored.offline.channelSelection->rangePolicy
      == vsr::rendering::ChannelRangePolicy::AUTO);
}

TEST_CASE("Named intent wins over invalid legacy input in either scope",
    "[FrameChannelState]")
{
  using namespace vsr::rendering;
  vsr::core::DataTree tree;
  FrameChannelSelection saved;
  saved.deviceName = "Temperature_RAW";
  saved.visualization = "grayscale";
  saved.rangePolicy = ChannelRangePolicy::FIXED;
  saved.rangeMin = -3.f;
  saved.rangeMax = 7.f;
  saved.invertEdges = true;
  vsr::app::saveFrameChannelSelection(
      tree.root()["uiState"]["selection"], saved);
  auto &legacy = tree.root()["legacy"];
  legacy["aovType"] = 999;
  FrameChannelSelection selection;
  std::string error;
  REQUIRE(vsr::app::loadFrameChannelSelection(
      tree.root()["uiState"].child("selection"), &legacy, selection, error));
  CHECK(selection.deviceName == saved.deviceName);
  CHECK(selection.rangeMin == -3.f);
  CHECK(selection.rangeMax == 7.f);
  CHECK(selection.invertEdges);
  CHECK_FALSE(
      vsr::app::loadFrameChannelSelection(nullptr, &legacy, selection, error));
  CHECK(error.find("999") != std::string::npos);
  CHECK(selection.deviceName == saved.deviceName);
  auto &named = tree.root()["uiState"]["selection"];
  named["rangeMax"] = -4.f;
  CHECK_FALSE(
      vsr::app::loadFrameChannelSelection(&named, &legacy, selection, error));
  named["rangeMax"] = 7.f;
  named["rangePolicy"] = "unknown";
  CHECK_FALSE(
      vsr::app::loadFrameChannelSelection(&named, nullptr, selection, error));
  named["rangePolicy"] = "fixed";
  named["deviceName"] = 2;
  CHECK_FALSE(
      vsr::app::loadFrameChannelSelection(&named, nullptr, selection, error));
}

TEST_CASE("Interactive unavailable intent visibly resolves to Color",
    "[FrameChannelState]")
{
  using namespace vsr::rendering;
  FrameChannelCatalog catalog;
  catalog.channels = {
      {"channel.color", "color", {ANARI_UFIXED8_RGBA_SRGB}, {"color"}},
      {"channel.opaque_DATA",
          "opaque_DATA",
          {ANARI_FLOAT64},
          {},
          "advertised pixel types have no supported visualization"}};
  FrameChannelSelection selection;
  selection.deviceName = GENERATE("channel.missing", "channel.opaque_DATA");
  selection.visualization = "grayscale";
  std::string error;
  CHECK_FALSE(vsr::app::resolveInteractiveFrameChannelSelection(
      catalog, selection, error));
  CHECK(selection.deviceName == "channel.color");
  CHECK(selection.visualization == "color");
  CHECK(error.find("Color") != std::string::npos);
  CHECK(error.find("unavailable") != std::string::npos);
  selection.deviceName = "channel.opaque_DATA";
  CHECK_FALSE(
      vsr::app::applyFrameChannelOptions({}, catalog, selection, error));
  CHECK(selection.deviceName == "channel.opaque_DATA");
}

TEST_CASE("Local UI State migrates legacy keys without entering a dump",
    "[FrameChannelState]")
{
  using namespace vsr::rendering;
  vsr::core::DataTree tree;
  auto &viewport = tree.root()["windows"]["Viewport"];
  viewport["visualizeAOV"] = int(AOVType::DEPTH);
  viewport["depthVisualMinimum"] = -4.f;
  viewport["depthVisualMaximum"] = 12.f;
  viewport["edgeInvert"] = true;
  const vsr::app::LegacyFrameChannelKeys keys{
      "visualizeAOV", "depthVisualMinimum", "depthVisualMaximum", "edgeInvert"};
  FrameChannelSelection selection;
  std::string error;
  REQUIRE(vsr::app::loadFrameChannelSelection(
      viewport.child("channelSelection"), &viewport, selection, error, keys));
  CHECK(selection.deviceName == "channel.depth");
  CHECK(selection.visualization == "grayscale");
  CHECK(selection.rangePolicy == ChannelRangePolicy::FIXED);
  CHECK(selection.rangeMin == -4.f);
  CHECK(selection.rangeMax == 12.f);
  CHECK(selection.invertEdges);
  viewport["visualizeAOV"] = int(AOVType::EDGES);
  REQUIRE(vsr::app::loadFrameChannelSelection(
      nullptr, &viewport, selection, error, keys));
  CHECK(selection.deviceName == "channel.objectId");
  CHECK(selection.visualization == "edges");
  CHECK(selection.invertEdges);
  vsr::app::saveFrameChannelSelection(viewport["channelSelection"], selection);
  viewport["visualizeAOV"] = 9000;
  REQUIRE(vsr::app::loadFrameChannelSelection(
      viewport.child("channelSelection"), &viewport, selection, error, keys));
  CHECK(selection.visualization == "edges");
  CHECK_FALSE(vsr::app::loadFrameChannelSelection(
      nullptr, &viewport, selection, error, keys));
  CHECK(error.find("9000") != std::string::npos);
  vsr::app::Context context;
  vsr::core::DataTree dump;
  REQUIRE(vsr::app::serialize_ApplicationDump(context, dump.root()));
  CHECK_FALSE(dump.root().child("windows"));
  CHECK_FALSE(dump.root().child("uiState"));
}

TEST_CASE(
    "Local controls follow the resolved visualization", "[FrameChannelState]")
{
  using namespace vsr::rendering;
  FrameChannelSelection selection;
  CHECK(vsr::app::frameChannelShowsBeauty(selection));
  CHECK_FALSE(vsr::app::frameChannelUsesRange(selection));
  FrameChannelCatalog catalog;
  catalog.channels = {{"channel.normal",
                          "normal",
                          {ANARI_FLOAT32_VEC3},
                          {"color", "component-y", "magnitude", "normal"}},
      {"channel.motionVectors",
          "motionVectors",
          {ANARI_FLOAT32_VEC2},
          {"component-x", "component-y", "magnitude"}},
      {"channel.depth", "depth", {ANARI_FLOAT32}, {"grayscale"}}};
  std::string error;
  vsr::app::FrameChannelOptions options;
  options.channel = "normal";
  REQUIRE(
      vsr::app::applyFrameChannelOptions(options, catalog, selection, error));
  CHECK(
      selection.visualization == "color"); // retain compatible explicit choice
  CHECK_FALSE(vsr::app::frameChannelShowsBeauty(selection));
  CHECK_FALSE(vsr::app::frameChannelUsesRange(selection));
  options.visualization = "normal";
  REQUIRE(
      vsr::app::applyFrameChannelOptions(options, catalog, selection, error));
  options.visualization.reset();
  options.channel = "motionVectors";
  REQUIRE(
      vsr::app::applyFrameChannelOptions(options, catalog, selection, error));
  CHECK(selection.deviceName == "channel.motionVectors");
  CHECK(selection.visualization == "component-x");
  CHECK(vsr::app::frameChannelUsesRange(selection));
  options.visualization = "component-y";
  REQUIRE(
      vsr::app::applyFrameChannelOptions(options, catalog, selection, error));
  options.visualization.reset();
  options.channel = "normal";
  REQUIRE(
      vsr::app::applyFrameChannelOptions(options, catalog, selection, error));
  CHECK(selection.visualization == "component-y");
  options.channel = "depth";
  REQUIRE(
      vsr::app::applyFrameChannelOptions(options, catalog, selection, error));
  CHECK(selection.visualization == "grayscale");
  options.channel = "motion-vectors";
  CHECK_FALSE(
      vsr::app::applyFrameChannelOptions(options, catalog, selection, error));
  CHECK(selection.deviceName == "channel.depth");
}

TEST_CASE("Named selection survives catalog reorder and channel overrides",
    "[FrameChannelState]")
{
  using namespace vsr::rendering;
  FrameChannelCatalog catalog;
  catalog.channels = {
      {"Temperature_RAW", "Temperature_RAW", {ANARI_FLOAT32}, {"grayscale"}},
      {"channel.normal",
          "normal",
          {ANARI_FLOAT32_VEC3},
          {"normal", "component-y", "color"}}};
  FrameChannelSelection selection;
  selection.deviceName = "Temperature_RAW";
  selection.visualization = "grayscale";
  std::string error;
  REQUIRE(vsr::app::applyFrameChannelOptions({}, catalog, selection, error));
  std::reverse(catalog.channels.begin(), catalog.channels.end());
  REQUIRE(vsr::app::applyFrameChannelOptions({}, catalog, selection, error));
  CHECK(selection.deviceName == "Temperature_RAW");
  vsr::app::FrameChannelOptions options;
  options.channel = "normal";
  REQUIRE(
      vsr::app::applyFrameChannelOptions(options, catalog, selection, error));
  CHECK(selection.visualization == "normal");
  options.visualization = "grayscale";
  CHECK_FALSE(
      vsr::app::applyFrameChannelOptions(options, catalog, selection, error));
}

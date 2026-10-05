// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_app
#include "vsr/app/ApplicationDump.h"
#include "vsr/app/Context.h"
// vsr_core
#include "vsr/core/TypeMacros.hpp"
// vsr_rendering
#include "vsr/rendering/pipeline/FrameChannelCatalog.h"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef VSR_OFFLINE_EXECUTABLE
TEST_CASE(
    "Offline lists channels without producing an image", "[FrameChannelCLI]")
{
  const auto directory =
      std::filesystem::path(CHANNEL_TEST_DIRECTORY) / "offline";
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  const std::string command = "cd \"" + directory.string()
      + "\" && VSR_CHANNEL_TEST_FORBID_RENDER=1 VSR_CHANNEL_TEST_RENDERER=diagnostic \""
      + VSR_OFFLINE_EXECUTABLE
      + "\" --lib channel_test --renderer diagnostic --list-channels -o forbidden.png > listing.txt 2>&1";
  REQUIRE(std::system(command.c_str()) == 0);
  std::ifstream file(directory / "listing.txt");
  const std::string output((std::istreambuf_iterator<char>(file)), {});
  CHECK(output.find("motionVectors [device name: channel.motionVectors")
      != std::string::npos);
  CHECK(output.find("Temperature_RAW [device name: Temperature_RAW")
      != std::string::npos);
  CHECK(output.find("mapping not verified") != std::string::npos);
  CHECK_FALSE(std::filesystem::exists(directory / "forbidden.png"));
  CHECK_FALSE(std::filesystem::exists(directory / "vsrOffline.png"));
  const std::string failure = "cd \"" + directory.string() + "\" && \""
      + VSR_OFFLINE_EXECUTABLE
      + "\" --lib channel_test --renderer absent --list-channels > failure.txt 2>&1";
  CHECK(std::system(failure.c_str()) != 0);
  std::ifstream failureFile(directory / "failure.txt");
  const std::string diagnostic(
      (std::istreambuf_iterator<char>(failureFile)), {});
  CHECK(diagnostic.find("cannot initialize renderer 'absent'")
      != std::string::npos);
  CHECK(diagnostic.find("channel_test") != std::string::npos);
}

TEST_CASE("Render lists the saved device and renderer without images",
    "[FrameChannelCLI]")
{
  const auto directory =
      std::filesystem::path(CHANNEL_TEST_DIRECTORY) / "render";
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  vsr::app::Context context;
  context.offline.renderer.libraryName = "channel_test";
  context.offline.renderer.activeRenderer = 0;
  vsr::scene::Object renderer(ANARI_RENDERER, "diagnostic");
  renderer.setName("diagnostic");
  context.offline.renderer.rendererObjects.push_back(std::move(renderer));
  vsr::core::DataTree state;
  REQUIRE(vsr::app::serialize_ApplicationDump(context, state.root()));
  REQUIRE(state.save((directory / "state.vsr").c_str()));
  const std::string command = "cd \"" + directory.string()
      + "\" && VSR_CHANNEL_TEST_FORBID_RENDER=1 VSR_CHANNEL_TEST_RENDERER=diagnostic \""
      + VSR_RENDER_EXECUTABLE
      + "\" state.vsr --list-channels > listing.txt 2>&1";
  REQUIRE(std::system(command.c_str()) == 0);
  std::ifstream file(directory / "listing.txt");
  const std::string output((std::istreambuf_iterator<char>(file)), {});
  CHECK(output.find("motionVectors [device name: channel.motionVectors")
      != std::string::npos);
  CHECK(output.find("Temperature_RAW [device name: Temperature_RAW")
      != std::string::npos);
  CHECK(output.find("mapping not verified") != std::string::npos);
  CHECK_FALSE(std::filesystem::exists(directory / "vsrRender_0000.png"));
  context.offline.renderer.activeRenderer = 7;
  vsr::core::DataTree invalidState;
  REQUIRE(vsr::app::serialize_ApplicationDump(context, invalidState.root()));
  REQUIRE(invalidState.save((directory / "invalid.vsr").c_str()));
  const std::string invalidCommand = "cd \"" + directory.string() + "\" && \""
      + VSR_RENDER_EXECUTABLE
      + "\" --list-channels invalid.vsr > failure.txt 2>&1";
  CHECK(std::system(invalidCommand.c_str()) != 0);
  std::ifstream failureFile(directory / "failure.txt");
  const std::string diagnostic(
      (std::istreambuf_iterator<char>(failureFile)), {});
  CHECK(
      diagnostic.find("invalid saved renderer selection") != std::string::npos);
  CHECK(diagnostic.find("channel_test") != std::string::npos);
  for (const auto &entry : std::filesystem::directory_iterator(directory))
    CHECK(entry.path().extension() != ".png");
}
#endif

TEST_CASE("Named selection preserves exact identity and compatible storage",
    "[FrameChannelCatalog]")
{
  auto library = anari::loadLibrary("channel_test");
  REQUIRE(library);
  auto device = anari::newDevice(library, "default");
  REQUIRE(device);
  const auto catalog = vsr::rendering::discoverFrameChannels(device);
  vsr::rendering::FrameChannelSelection selection;
  std::string error;
  REQUIRE(vsr::rendering::resolveFrameChannelSelection(
      catalog, "Temperature_RAW", selection, error));
  CHECK(selection.deviceName == "Temperature_RAW");
  CHECK(selection.visualization == "grayscale");
  CHECK(selection.pixelType == ANARI_FLOAT32);
  CHECK_FALSE(vsr::rendering::resolveFrameChannelSelection(
      catalog, "temperature_raw", selection, error));
  CHECK_FALSE(error.empty());
  REQUIRE(vsr::rendering::resolveFrameChannelSelection(
      catalog, "color", selection, error));
  CHECK(selection.visualization == "color");
  CHECK(selection.pixelType == ANARI_UFIXED8_RGBA_SRGB);
  selection.visualization = "edges";
  CHECK_FALSE(vsr::rendering::resolveFrameChannelSelection(
      catalog, "Temperature_RAW", selection, error, true));
  selection.rangePolicy = vsr::rendering::ChannelRangePolicy::FIXED;
  selection.rangeMin = 1.f;
  selection.rangeMax = 1.f;
  CHECK_FALSE(vsr::rendering::resolveFrameChannelSelection(
      catalog, "Temperature_RAW", selection, error));
  selection.rangeMin = -2.f;
  selection.rangeMax = 5.f;
  REQUIRE(vsr::rendering::resolveFrameChannelSelection(
      catalog, "Temperature_RAW", selection, error));
  CHECK(selection.rangeMin == -2.f);
  CHECK(selection.rangeMax == 5.f);
  anari::release(device, device);
  anari::unloadLibrary(library);
}

namespace {
struct CatalogDevice
{
  CatalogDevice();
  ~CatalogDevice();
  VSR_NOT_COPYABLE(CatalogDevice)
  VSR_NOT_MOVEABLE(CatalogDevice)
  void metadata(const char *mode);
  anari::Library library{nullptr};
  anari::Device device{nullptr};
};
CatalogDevice::CatalogDevice()
{
  library = anari::loadLibrary("channel_test");
  if (library)
    device = anari::newDevice(library, "default");
  if (device)
    anari::commitParameters(device, device);
}
CatalogDevice::~CatalogDevice()
{
  if (device)
    anari::release(device, device);
  if (library)
    anari::unloadLibrary(library);
}
void CatalogDevice::metadata(const char *mode)
{
  anari::setParameter(device, device, "test.metadata", mode);
  anari::commitParameters(device, device);
}
} // namespace

TEST_CASE("Catalog reports fallback, incomplete and ambiguous metadata",
    "[FrameChannelCatalog]")
{
  CatalogDevice fixture;
  REQUIRE(fixture.device);
  SECTION("Missing metadata supplements only standard supported channels")
  {
    fixture.metadata("missing");
    const auto catalog = vsr::rendering::discoverFrameChannels(fixture.device);
    REQUIRE(catalog.usable());
    REQUIRE(catalog.find("color"));
    CHECK_FALSE(catalog.find("color")->advertised);
    REQUIRE(catalog.find("depth"));
    CHECK(catalog.find("depth")->pixelTypes
        == std::vector<ANARIDataType>{ANARI_FLOAT32});
    REQUIRE(catalog.find("objectId"));
    CHECK(catalog.find("objectId")->pixelTypes
        == std::vector<ANARIDataType>{ANARI_UINT32});
    CHECK_FALSE(catalog.find("motionVectors"));
    CHECK_FALSE(catalog.find("normal"));
    CHECK_FALSE(catalog.diagnostics.empty());
  }
  SECTION("Explicit unknown types are not replaced by fallback")
  {
    fixture.metadata("incomplete");
    const auto catalog = vsr::rendering::discoverFrameChannels(fixture.device);
    REQUIRE(catalog.find("depth"));
    CHECK(catalog.find("depth")->advertised);
    CHECK(catalog.find("depth")->pixelTypes.empty());
    CHECK_FALSE(catalog.find("depth")->unavailableReason.empty());
    REQUIRE(catalog.find("color"));
    CHECK_FALSE(catalog.find("color")->advertised);
  }
  SECTION("Distinct raw names with the same presentation cannot resolve")
  {
    fixture.metadata("ambiguous");
    const auto catalog = vsr::rendering::discoverFrameChannels(fixture.device);
    CHECK_FALSE(catalog.find("Case"));
    int ambiguous = 0;
    for (const auto &channel : catalog.channels) {
      if (channel.presentationName == "Case") {
        ++ambiguous;
        CHECK(channel.ambiguous);
        CHECK_FALSE(channel.unavailableReason.empty());
      }
    }
    CHECK(ambiguous == 2);
  }
  SECTION("Mixed alternatives retain unsupported type explanations")
  {
    fixture.metadata("mixed");
    const auto catalog = vsr::rendering::discoverFrameChannels(fixture.device);
    REQUIRE(catalog.find("unusual"));
    const auto &channel = *catalog.find("unusual");
    CHECK(channel.pixelTypes
        == std::vector<ANARIDataType>{ANARI_FLOAT64, ANARI_FLOAT32});
    CHECK(channel.visualizations == std::vector<std::string>{"grayscale"});
    std::ostringstream listing;
    vsr::rendering::printFrameChannels(listing, catalog);
    CHECK(listing.str().find("FLOAT64 (unsupported visualization type)")
        != std::string::npos);
  }
  SECTION("Explicit unsupported standard types defeat fallback")
  {
    fixture.metadata("unsupported");
    const auto catalog = vsr::rendering::discoverFrameChannels(fixture.device);
    CHECK_FALSE(catalog.usable());
    REQUIRE(catalog.find("color"));
    CHECK(catalog.find("color")->pixelTypes
        == std::vector<ANARIDataType>{ANARI_FLOAT64});
    CHECK(catalog.find("color")->visualizations.empty());
    CHECK_FALSE(catalog.find("color")->unavailableReason.empty());
  }
}

TEST_CASE("Installed devices expose usable catalogs when available",
    "[FrameChannelSmoke]")
{
  for (const char *name : {"helide", "barney", "visrtx"}) {
    INFO("ANARI library: " << name);
    auto library = anari::loadLibrary(name);
    if (!library) {
      std::cout << "unavailable optional ANARI library: " << name << '\n';
      continue;
    }
    auto device = anari::newDevice(library, "default");
    if (!device) {
      std::cout << "unavailable optional ANARI device: " << name << '\n';
      anari::unloadLibrary(library);
      continue;
    }
    anari::commitParameters(device, device);
    const auto catalog = vsr::rendering::discoverFrameChannels(device);
    CHECK(catalog.usable());
    CHECK(catalog.find("color"));
    for (const auto &channel : catalog.channels)
      CHECK_FALSE(channel.mappingVerified);
    std::cout << "discovered " << catalog.channels.size()
              << " Frame Channels on " << name << '\n';
    anari::release(device, device);
    anari::unloadLibrary(library);
  }
}

TEST_CASE("Frame metadata preserves custom channel identities",
    "[FrameChannelCatalog]")
{
  auto library = anari::loadLibrary("channel_test");
  REQUIRE(library);
  auto device = anari::newDevice(library, "default");
  REQUIRE(device);
  anari::commitParameters(device, device);
  const auto catalog = vsr::rendering::discoverFrameChannels(device);
  const auto *vectors = catalog.find("motionVectors");
  REQUIRE(vectors);
  CHECK(vectors->deviceName == "channel.motionVectors");
  CHECK(vectors->advertised);
  CHECK(vectors->pixelTypes == std::vector<ANARIDataType>{ANARI_FLOAT32_VEC2});
  const auto *temperature = catalog.find("Temperature_RAW");
  REQUIRE(temperature);
  CHECK(temperature->deviceName == "Temperature_RAW");
  CHECK_FALSE(temperature->mappingVerified);
  REQUIRE(catalog.find("channel.Case"));
  CHECK(catalog.find("channel.Case")->deviceName == "channel.channel.Case");
  CHECK_FALSE(catalog.find("Channel.Case"));
  size_t vectorEntries = 0;
  for (const auto &channel : catalog.channels)
    if (channel.deviceName == "channel.motionVectors")
      ++vectorEntries;
  CHECK(vectorEntries == 1);
  REQUIRE(catalog.find("color"));
  CHECK(catalog.find("color")->pixelTypes
      == std::vector<ANARIDataType>{
          ANARI_UFIXED8_RGBA_SRGB, ANARI_FLOAT32_VEC4});
  REQUIRE(catalog.find("unusual"));
  CHECK(catalog.find("unusual")->pixelTypes
      == std::vector<ANARIDataType>{ANARI_FLOAT64});
  CHECK(catalog.find("unusual")->visualizations.empty());
  CHECK_FALSE(catalog.find("unusual")->unavailableReason.empty());
  anari::release(device, device);
  anari::unloadLibrary(library);
}

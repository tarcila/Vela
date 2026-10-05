// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_app
// vsr_core
#include "vsr/core/TypeMacros.hpp"
// vsr_rendering
#include "vsr/rendering/overlay/WorldBoundsOverlay.h"
#include "vsr/rendering/pipeline/ImagePipeline.h"
#include "vsr/rendering/pipeline/passes/AutoExposurePass.h"
#include "vsr/rendering/pipeline/passes/ChannelVisualizationPass.h"
#include "vsr/rendering/pipeline/passes/OutlineRenderPass.h"
#include "vsr/rendering/pipeline/passes/OutputTransformPass.h"
#include "vsr/rendering/pipeline/passes/PrimitiveOutlineRenderPass.h"
#include "vsr/rendering/pipeline/passes/ToneMapPass.h"
// vsr_scene
#include "vsr/scene/ObjectId.hpp"
// cuda
#ifdef VSR_ALGORITHMS_HAS_CUDA
#include <cuda_runtime_api.h>
#endif
// std
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace rendering = vsr::rendering;

namespace {
struct ChannelDevice
{
  ChannelDevice();
  ~ChannelDevice();
  VSR_NOT_COPYABLE(ChannelDevice)
  VSR_NOT_MOVEABLE(ChannelDevice)
  anari::Library library{nullptr};
  anari::Device device{nullptr};
};
ChannelDevice::ChannelDevice()
{
  library = anari::loadLibrary("channel_test");
  if (library)
    device = anari::newDevice(library, "default");
}
ChannelDevice::~ChannelDevice()
{
  if (device)
    anari::release(device, device);
  if (library)
    anari::unloadLibrary(library);
}
struct IdentityConsumer : rendering::ImagePass
{
  std::vector<uint32_t> objects;
  std::vector<uint32_t> primitives;
  rendering::ImageChannels requiredChannels() const override;
  void render(
      rendering::ImageBuffers &buffers, rendering::FrameState &) override;
};

rendering::ImageChannels IdentityConsumer::requiredChannels() const
{
  return rendering::ImageChannels::OBJECT_ID
      | rendering::ImageChannels::PRIMITIVE_ID;
}

void IdentityConsumer::render(
    rendering::ImageBuffers &buffers, rendering::FrameState &)
{
#ifdef ENABLE_CUDA
  if (buffers.stream)
    REQUIRE(cudaStreamSynchronize(buffers.stream) == cudaSuccess);
#endif
  const auto size = dimensions();
  const size_t count = size_t(size.x) * size.y;
  objects.assign(buffers.objectId, buffers.objectId + count);
  primitives.assign(buffers.primitiveId, buffers.primitiveId + count);
}

struct BackendObserver : rendering::ImagePass
{
  void render(
      rendering::ImageBuffers &buffers, rendering::FrameState &) override;
};

void BackendObserver::render(
    rendering::ImageBuffers &buffers, rendering::FrameState &)
{
#ifdef VSR_ALGORITHMS_HAS_CUDA
  REQUIRE(buffers.stream);
  REQUIRE(cudaStreamSynchronize(static_cast<cudaStream_t>(buffers.stream))
      == cudaSuccess);
#else
  CHECK_FALSE(buffers.stream);
#endif
}

struct SharedConsumer : rendering::ImagePass
{
  std::vector<rendering::FrameChannelRequest> requiredNamedChannels()
      const override;
  void render(rendering::ImageBuffers &b, rendering::FrameState &) override;
};

std::vector<rendering::FrameChannelRequest>
SharedConsumer::requiredNamedChannels() const
{
  return {{"Temperature_RAW", ANARI_FLOAT32}};
}

void SharedConsumer::render(rendering::ImageBuffers &b, rendering::FrameState &)
{
  REQUIRE_FALSE(b.namedChannels.empty());
  CHECK(b.namedChannels.front().status == rendering::FrameChannelStatus::VALID);
}

struct ExposureReader : rendering::ImagePass
{
  float seen{0.f};
  void render(rendering::ImageBuffers &, rendering::FrameState &frame) override;
};

void ExposureReader::render(
    rendering::ImageBuffers &, rendering::FrameState &frame)
{
  seen = frame.exposure;
}

void checkGray(
    const rendering::ImagePipeline &pipeline, size_t index, uint8_t expected)
{
  uint8_t pixel[4];
  std::memcpy(pixel, pipeline.getColorBuffer() + index, 4);
  CHECK(pixel[0] == expected);
  CHECK(pixel[1] == expected);
  CHECK(pixel[2] == expected);
  CHECK(pixel[3] == 255);
}
} // namespace

TEST_CASE("HDR Color diagnostics negotiate storage and restore beauty",
    "[ChannelVisualization][ChannelReview]")
{
  ChannelDevice fixture;
  REQUIRE(fixture.device);
  rendering::ImagePipeline pipeline(2, 2);
  auto *source =
      pipeline.setSource<rendering::AnariSceneRenderPass>(fixture.device);
  source->setRunAsync(false);
  source->setColorFormat(ANARI_FLOAT32_VEC4);
  auto *pass = pipeline.addPass<rendering::ChannelVisualizationPass>();
  pass->setUseCUDA(GENERATE(false, true));
  pipeline.addPass<BackendObserver>();
  auto *tone = pipeline.addPass<rendering::ToneMapPass>();
  tone->setHDREnabled(true);
  tone->setExposure(2.f);
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  const std::vector<uint32_t> beauty(
      pipeline.getColorBuffer(), pipeline.getColorBuffer() + 4);
  rendering::FrameChannelSelection selection;
  selection.visualization = GENERATE("component-x", "magnitude");
  std::string error;
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), "color", selection, error, true));
  REQUIRE(pass->setSelection(selection));
  pipeline.render();
  INFO(pass->error());
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  REQUIRE(pipeline.channelResult("channel.color"));
  CHECK(pipeline.channelResult("channel.color")->pixelType
      == selection.pixelType);
  checkGray(pipeline, 0, 0);
  checkGray(pipeline, 3, 255);
  REQUIRE(pass->setSelection({}));
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  CHECK(rendering::hasChannels(
      source->supportedChannels(), rendering::ImageChannels::HDR_COLOR));
  CHECK(source->namedChannels().empty());
  CHECK(std::vector<uint32_t>(
            pipeline.getColorBuffer(), pipeline.getColorBuffer() + 4)
      == beauty);
}

TEST_CASE("Custom vector components produce finished pixels",
    "[ChannelVisualization]")
{
  ChannelDevice fixture;
  REQUIRE(fixture.device);
  rendering::ImagePipeline pipeline(2, 2);
  auto *source =
      pipeline.setSource<rendering::AnariSceneRenderPass>(fixture.device);
  source->setRunAsync(false);
  auto *pass = pipeline.addPass<rendering::ChannelVisualizationPass>();
  pass->setUseCUDA(GENERATE(false, true));
  rendering::FrameChannelSelection selection;
  selection.visualization = "component-y";
  std::string error;
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), "motionVectors", selection, error, true));
  REQUIRE(pass->setSelection(selection));
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  checkGray(pipeline, 0, 0);
  checkGray(pipeline, 1, 255);
  checkGray(pipeline, 2, 0);
  checkGray(pipeline, 3, 255);
}

TEST_CASE("Vector Color and standard semantic defaults use compatible storage",
    "[ChannelVisualization]")
{
  ChannelDevice fixture;
  REQUIRE(fixture.device);
  const auto type = GENERATE(ANARI_FLOAT32_VEC3,
      ANARI_FLOAT32_VEC4,
      ANARI_UFIXED8_VEC3,
      ANARI_UFIXED8_VEC4,
      ANARI_UFIXED8_RGB_SRGB,
      ANARI_UFIXED8_RGBA_SRGB);
  anari::setParameter(fixture.device, fixture.device, "test.type", type);
  rendering::ImagePipeline pipeline(2, 2);
  auto *source =
      pipeline.setSource<rendering::AnariSceneRenderPass>(fixture.device);
  source->setRunAsync(false);
  auto *pass = pipeline.addPass<rendering::ChannelVisualizationPass>();
  const bool useCUDA = GENERATE(false, true);
  pass->setUseCUDA(useCUDA);
  INFO("type=" << int(type) << " CUDA=" << useCUDA);
  rendering::FrameChannelSelection selection;
  std::string error;
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), "motionVectors", selection, error));
  CHECK(selection.visualization == "color");
  REQUIRE(pass->setSelection(selection));
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  uint8_t pixel[4];
  std::memcpy(pixel, pipeline.getColorBuffer(), 4);
  CHECK(pixel[0] == 0);
  if (type == ANARI_FLOAT32_VEC3 || type == ANARI_FLOAT32_VEC4) {
    CHECK(pixel[1] == 255);
    CHECK(pixel[2] == 255);
    CHECK(pixel[3] == 255);
  } else if (type == ANARI_UFIXED8_VEC3 || type == ANARI_UFIXED8_VEC4) {
    CHECK(pixel[1] == 137);
    CHECK(int(pixel[2]) == 188);
    CHECK(pixel[3] == (type == ANARI_UFIXED8_VEC4 ? 192 : 255));
  } else {
    CHECK(pixel[1] == 64);
    CHECK(pixel[2] == 128);
    CHECK(pixel[3] == (type == ANARI_UFIXED8_RGBA_SRGB ? 192 : 255));
  }
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), "normal", selection, error));
  CHECK(selection.pixelType == ANARI_FLOAT32_VEC3);
  CHECK(selection.visualization == "normal");
  REQUIRE(pass->setSelection(selection));
  pipeline.render();
  std::memcpy(pixel, pipeline.getColorBuffer(), 4);
  CHECK(int(pixel[0]) == 128);
  CHECK(pixel[1] == 255);
  CHECK(pixel[2] == 255);
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), "albedo", selection, error));
  CHECK(selection.pixelType == ANARI_FLOAT32_VEC3);
  CHECK(selection.visualization == "color");
  REQUIRE(pass->setSelection(selection));
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  std::memcpy(pixel, pipeline.getColorBuffer(), 4);
  CHECK(pixel[0] == 0);
  CHECK(pixel[1] == 255);
  CHECK(pixel[2] == 255);
  CHECK(pixel[3] == 255);
}

TEST_CASE("Vector scalar views range finite current samples across shapes",
    "[ChannelVisualization]")
{
  ChannelDevice fixture;
  REQUIRE(fixture.device);
  const auto type =
      GENERATE(ANARI_FLOAT32_VEC2, ANARI_FLOAT32_VEC3, ANARI_FLOAT32_VEC4);
  anari::setParameter(fixture.device, fixture.device, "test.type", type);
  rendering::ImagePipeline pipeline(2, 2);
  auto *source =
      pipeline.setSource<rendering::AnariSceneRenderPass>(fixture.device);
  source->setRunAsync(false);
  auto *pass = pipeline.addPass<rendering::ChannelVisualizationPass>();
  pass->setUseCUDA(GENERATE(false, true));
  rendering::FrameChannelSelection selection;
  std::string error;
  const int components = type == ANARI_FLOAT32_VEC2 ? 2
      : type == ANARI_FLOAT32_VEC3                  ? 3
                                                    : 4;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  vsr::math::float4 samples(-2.f, 0.f, 2.f, nan);
  anari::setParameter(fixture.device, fixture.device, "test.samples", samples);
  for (int c = 0; c < components; ++c) {
    selection.visualization = std::string("component-") + "xyzw"[c];
    REQUIRE(rendering::resolveFrameChannelSelection(
        source->channelCatalog(), "motionVectors", selection, error, true));
    REQUIRE(pass->setSelection(selection));
    pipeline.render();
    REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
    checkGray(pipeline, 0, 0);
    checkGray(pipeline, 1, 128);
    checkGray(pipeline, 2, 255);
    checkGray(pipeline, 3, 0);
  }
  selection.visualization = "magnitude";
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), "motionVectors", selection, error, true));
  REQUIRE(pass->setSelection(selection));
  pipeline.render();
  checkGray(pipeline, 0, 255);
  checkGray(pipeline, 1, 0);
  checkGray(pipeline, 2, 255);
  checkGray(pipeline, 3, 0);
  selection.rangePolicy = rendering::ChannelRangePolicy::FIXED;
  selection.rangeMin = 0.f;
  selection.rangeMax = 20.f;
  REQUIRE(pass->setSelection(selection));
  pipeline.render();
  checkGray(pipeline, 0, components == 2 ? 57 : components == 3 ? 95 : 140);
  selection.rangePolicy = rendering::ChannelRangePolicy::AUTO;
  for (auto sample : {3.f, nan, std::numeric_limits<float>::infinity()}) {
    samples = vsr::math::float4(sample);
    anari::setParameter(
        fixture.device, fixture.device, "test.samples", samples);
    REQUIRE(pass->setSelection(selection));
    pipeline.render();
    for (size_t i = 0; i < 4; ++i)
      checkGray(pipeline, i, sample == 3.f ? 128 : 0);
  }
  samples = vsr::math::float4(-2.f, 0.f, 2.f, 4.f);
  anari::setParameter(fixture.device, fixture.device, "test.samples", samples);
  pipeline.setDimensions(3, 1);
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  checkGray(pipeline, 0, 255);
  checkGray(pipeline, 1, 0);
  checkGray(pipeline, 2, 255);
  selection.visualization = "component-x";
  selection.rangePolicy = rendering::ChannelRangePolicy::FIXED;
  selection.rangeMin = -1.f;
  selection.rangeMax = 1.f;
  REQUIRE(pass->setSelection(selection));
  pipeline.render();
  checkGray(pipeline, 0, 0);
  checkGray(pipeline, 1, 128);
  checkGray(pipeline, 2, 255);
}

TEST_CASE("Normalized byte components and magnitude are numeric diagnostics",
    "[ChannelVisualization]")
{
  ChannelDevice fixture;
  REQUIRE(fixture.device);
  const auto type = GENERATE(ANARI_UFIXED8_VEC3,
      ANARI_UFIXED8_VEC4,
      ANARI_UFIXED8_RGB_SRGB,
      ANARI_UFIXED8_RGBA_SRGB);
  anari::setParameter(fixture.device, fixture.device, "test.type", type);
  rendering::ImagePipeline pipeline(2, 2);
  auto *source =
      pipeline.setSource<rendering::AnariSceneRenderPass>(fixture.device);
  source->setRunAsync(false);
  auto *pass = pipeline.addPass<rendering::ChannelVisualizationPass>();
  pass->setUseCUDA(GENERATE(false, true));
  rendering::FrameChannelSelection selection;
  selection.visualization = "component-y";
  selection.rangePolicy = rendering::ChannelRangePolicy::FIXED;
  std::string error;
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), "motionVectors", selection, error, true));
  REQUIRE(pass->setSelection(selection));
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  checkGray(pipeline, 0, 64);
  selection.visualization = "magnitude";
  selection.rangeMax = 2.f;
  REQUIRE(pass->setSelection(selection));
  pipeline.render();
  checkGray(pipeline,
      0,
      type == ANARI_UFIXED8_VEC3 || type == ANARI_UFIXED8_RGB_SRGB ? 72 : 120);
}

TEST_CASE("Standard identity defaults preserve hashed colors and background",
    "[ChannelVisualization][IdentityChannels]")
{
  ChannelDevice fixture;
  REQUIRE(fixture.device);
  anari::setParameter(
      fixture.device, fixture.device, "test.metadata", "identities");
  rendering::ImagePipeline pipeline(2, 2);
  auto *source =
      pipeline.setSource<rendering::AnariSceneRenderPass>(fixture.device);
  source->setRunAsync(false);
  auto *pass = pipeline.addPass<rendering::ChannelVisualizationPass>();
  pass->setUseCUDA(GENERATE(false, true));
  const auto channel = GENERATE("objectId", "primitiveId", "instanceId");
  rendering::FrameChannelSelection selection;
  std::string error;
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), channel, selection, error));
  REQUIRE(selection.visualization == "id-colors");
  REQUIRE(pass->setSelection(selection));
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  // Legacy CUDA conversion rounds; the host conversion truncates.
  const uint32_t expected[] = {0xffde989fu, 0xff0f91d2u, 0xffd03a5fu};
  for (size_t i = 0; i < 3; ++i) {
    uint8_t actualBytes[4], expectedBytes[4];
    std::memcpy(actualBytes, pipeline.getColorBuffer() + i, 4);
    std::memcpy(expectedBytes, expected + i, 4);
    for (size_t c = 0; c < 4; ++c)
      CHECK(std::abs(int(actualBytes[c]) - int(expectedBytes[c])) <= 1);
  }
  CHECK(pipeline.getColorBuffer()[3] == 0xff000000u);
  REQUIRE(pipeline.channelResult(selection.deviceName));
  const auto *ids = static_cast<const uint32_t *>(
      pipeline.channelResult(selection.deviceName)->data);
  CHECK(ids[2] == 0x80000001u);
  CHECK(ids[3] == ~0u);
}

TEST_CASE("Object identity Edges preserve inversion and black background",
    "[ChannelVisualization][IdentityChannels]")
{
  ChannelDevice fixture;
  REQUIRE(fixture.device);
  anari::setParameter(
      fixture.device, fixture.device, "test.metadata", "identities");
  rendering::ImagePipeline pipeline(5, 1);
  auto *source =
      pipeline.setSource<rendering::AnariSceneRenderPass>(fixture.device);
  source->setRunAsync(false);
  auto *pass = pipeline.addPass<rendering::ChannelVisualizationPass>();
  pass->setUseCUDA(GENERATE(false, true));
  const vsr::math::uint4 ids(1u, 1u, 1u, ~0u);
  anari::setParameter(
      fixture.device, fixture.device, "test.integerSamples", ids);
  rendering::FrameChannelSelection selection;
  selection.visualization = "edges";
  selection.invertEdges = GENERATE(false, true);
  std::string error;
  REQUIRE_FALSE(source->channelCatalog().find("edges"));
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), "objectId", selection, error, true));
  REQUIRE(pass->setSelection(selection));
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  // Row: 1 1 1 background 1; only the latter two object pixels are edges.
  checkGray(pipeline, 0, selection.invertEdges ? 255 : 0);
  checkGray(pipeline, 1, selection.invertEdges ? 255 : 0);
  checkGray(pipeline, 2, selection.invertEdges ? 0 : 255);
  checkGray(pipeline, 3, 0);
  checkGray(pipeline, 4, selection.invertEdges ? 0 : 255);
}

TEST_CASE(
    "Identity demand composes with outlines and bounds and survives removal",
    "[ChannelVisualization][IdentityChannels]")
{
  ChannelDevice fixture;
  REQUIRE(fixture.device);
  anari::setParameter(
      fixture.device, fixture.device, "test.metadata", "identities");
  rendering::ImagePipeline pipeline(2, 2);
  auto *source =
      pipeline.setSource<rendering::AnariSceneRenderPass>(fixture.device);
  source->setRunAsync(false);
  auto *pass = pipeline.addPass<rendering::ChannelVisualizationPass>();
  pass->setUseCUDA(GENERATE(false, true));
  auto *outline = pipeline.addPass<rendering::OutlineRenderPass>();
  outline->setOutlineId(7u);
  auto *primitive = pipeline.addPass<rendering::PrimitiveOutlineRenderPass>();
  rendering::WorldBoundsOverlay bounds(pipeline);
  bounds.setShown(true);
  const vsr::math::box3 box{{-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}};
  const auto view = rendering::CameraView::perspective({0.f, 0.f, 5.f},
      {0.f, 0.f, -1.f},
      {0.f, 1.f, 0.f},
      vsr::math::radians(60.f));
  bounds.update(box, view);
  auto *consumer = pipeline.addPass<IdentityConsumer>();
  rendering::FrameChannelSelection selection;
  std::string error;
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), "objectId", selection, error));
  REQUIRE(pass->setSelection(selection));
  auto requested = [&](const char *name) {
    bool value = false;
    REQUIRE(anariGetProperty(fixture.device,
        source->getFrame(),
        name,
        ANARI_BOOL,
        &value,
        sizeof(value),
        ANARI_WAIT));
    return value;
  };
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  CHECK(requested("test.requested.channel.objectId"));
  CHECK(requested("test.requested.channel.primitiveId"));
  CHECK(requested("test.requested.channel.depth"));
  const auto *ids = static_cast<const uint32_t *>(
      pipeline.channelResult("channel.objectId")->data);
  CHECK(ids[2] == vsr::scene::encodeObjectId(ANARI_VOLUME, 1));
  CHECK(ids[3] == vsr::scene::NO_OBJECT_ID);
  CHECK(consumer->objects == std::vector<uint32_t>(ids, ids + 4));
  CHECK(consumer->primitives == consumer->objects);
  // A failed optional map clears standard consumers but fails the diagnostic.
  anari::setParameter(
      fixture.device, fixture.device, "test.map", "identity-null");
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::FAILED);
  CHECK(
      consumer->objects == std::vector<uint32_t>(4, vsr::scene::NO_OBJECT_ID));
  CHECK(consumer->primitives == consumer->objects);
  anari::setParameter(fixture.device, fixture.device, "test.map", "valid");
  source->setRenderer(nullptr);
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  // Removing display demand must not remove the outlines' shared channel.
  pass->setEnabled(false);
  pipeline.render();
  CHECK_FALSE(pipeline.channelResult("channel.objectId"));
  CHECK(requested("test.requested.channel.objectId"));
  pass->setEnabled(true);
  outline->setEnabled(false);
  primitive->setEnabled(false);
  bounds.setShown(false);
  bounds.update(box, view);
  consumer->setEnabled(false);
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  CHECK(requested("test.requested.channel.objectId"));
  CHECK_FALSE(requested("test.requested.channel.primitiveId"));
  CHECK_FALSE(requested("test.requested.channel.depth"));
  // Failed selected IDs are a diagnostic failure, not successful background.
  anari::setParameter(
      fixture.device, fixture.device, "test.map", "identity-null");
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::FAILED);
  CHECK(pass->error().find("channel.objectId") != std::string::npos);
  for (size_t i = 0; i < 4; ++i)
    checkGray(pipeline, i, 0);
  pass->setEnabled(false);
  pipeline.render();
  CHECK_FALSE(requested("test.requested.channel.objectId"));
}

TEST_CASE(
    "Identity display does not change isolated picks or display accumulation",
    "[ChannelVisualization][IdentityChannels]")
{
  ChannelDevice fixture;
  REQUIRE(fixture.device);
  anari::setParameter(
      fixture.device, fixture.device, "test.metadata", "identities");
  rendering::ImagePipeline pipeline(2, 2);
  auto *source =
      pipeline.setSource<rendering::AnariSceneRenderPass>(fixture.device);
  source->setRunAsync(false);
  auto camera = anari::newObject<anari::Camera>(fixture.device, "perspective");
  auto renderer =
      anari::newObject<anari::Renderer>(fixture.device, "diagnostic");
  auto world = anari::newObject<anari::World>(fixture.device);
  source->setCamera(camera);
  source->setRenderer(renderer);
  source->setWorld(world);
  anari::release(fixture.device, camera);
  anari::release(fixture.device, renderer);
  anari::release(fixture.device, world);
  auto *pass = pipeline.addPass<rendering::ChannelVisualizationPass>();
  pass->setUseCUDA(GENERATE(false, true));
  rendering::FrameChannelSelection selection;
  selection.visualization = GENERATE("id-colors", "edges");
  std::string error;
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), "objectId", selection, error, true));
  REQUIRE(pass->setSelection(selection));
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  const auto frame = source->getFrame();
  std::vector<uint32_t> pixels(
      pipeline.getColorBuffer(), pipeline.getColorBuffer() + 4);
  auto renders = [&]() {
    uint32_t count = 0;
    REQUIRE(anariGetProperty(fixture.device,
        frame,
        "test.renders",
        ANARI_UINT32,
        &count,
        sizeof(count),
        ANARI_WAIT));
    return count;
  };
  const auto before = renders();
  const auto sample = source->renderPickSample({0, 1});
  REQUIRE(sample);
  CHECK(sample->objectId == vsr::scene::encodeObjectId(ANARI_VOLUME, 1));
  CHECK(sample->primitiveId == 0x80000001u);
  CHECK(sample->instanceId == 0x80000001u);
  CHECK(sample->depth == 2.f);
  CHECK(source->getFrame() == frame);
  CHECK(renders() == before);
  CHECK(std::vector<uint32_t>(
            pipeline.getColorBuffer(), pipeline.getColorBuffer() + 4)
      == pixels);
  pass->setSelection(rendering::FrameChannelSelection{});
  pipeline.render();
  const auto colorPick = source->renderPickSample({0, 1});
  REQUIRE(colorPick);
  CHECK(colorPick->objectId == sample->objectId);
  CHECK(colorPick->depth == sample->depth);
}

TEST_CASE("Named scalar visualization produces finished grayscale pixels",
    "[ChannelVisualization]")
{
  ChannelDevice fixture;
  REQUIRE(fixture.device);
  rendering::ImagePipeline pipeline(2, 2);
  auto *source =
      pipeline.setSource<rendering::AnariSceneRenderPass>(fixture.device);
  source->setRunAsync(false);
  auto *pass = pipeline.addPass<rendering::ChannelVisualizationPass>();
  rendering::FrameChannelSelection selection;
  std::string error;
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), "Temperature_RAW", selection, error));
  REQUIRE(pass->setSelection(selection));
  pass->setUseCUDA(GENERATE(false, true));
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  REQUIRE(pipeline.channelResult("Temperature_RAW"));
  CHECK(pipeline.channelResult("Temperature_RAW")->status
      == rendering::FrameChannelStatus::VALID);
  checkGray(pipeline, 0, 0);
  checkGray(pipeline, 1, 85);
  checkGray(pipeline, 2, 170);
  checkGray(pipeline, 3, 255);
}

TEST_CASE("Scalar range uses current finite samples and clips Fixed bounds",
    "[ChannelVisualization]")
{
  ChannelDevice fixture;
  REQUIRE(fixture.device);
  rendering::ImagePipeline pipeline(2, 2);
  auto *source =
      pipeline.setSource<rendering::AnariSceneRenderPass>(fixture.device);
  source->setRunAsync(false);
  auto *pass = pipeline.addPass<rendering::ChannelVisualizationPass>();
  pass->setUseCUDA(GENERATE(false, true));
  rendering::FrameChannelSelection selection;
  std::string error;
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), "Temperature_RAW", selection, error));
  const float inf = std::numeric_limits<float>::infinity();
  const float nan = std::numeric_limits<float>::quiet_NaN();
  vsr::math::float4 samples(-2.f, 0.f, 2.f, nan);
  uint8_t expected[] = {0, 128, 255, 0};
  SECTION("negative values and NaN") {}
  SECTION("constant values differ from nonfinite background")
  {
    samples = vsr::math::float4(-7.f, -7.f, inf, nan);
    expected[0] = 128;
    expected[2] = 0;
  }
  SECTION("no finite samples")
  {
    samples = vsr::math::float4(nan, inf, -inf, nan);
    expected[1] = expected[2] = 0;
  }
  SECTION("Fixed clips and preserves numeric midpoint")
  {
    selection.rangePolicy = rendering::ChannelRangePolicy::FIXED;
    selection.rangeMin = -1.f;
    selection.rangeMax = 1.f;
  }
  SECTION("extreme finite floats do not overflow normalization")
  {
    const float max = std::numeric_limits<float>::max();
    samples = vsr::math::float4(-max, 0.f, max, inf);
  }
  REQUIRE(pass->setSelection(selection));
  anari::setParameter(fixture.device, fixture.device, "test.samples", samples);
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  for (size_t i = 0; i < 4; ++i)
    checkGray(pipeline, i, expected[i]);

  // Auto range must not retain bounds from the previous completed frame.
  samples = vsr::math::float4(10.f, 20.f, 30.f, inf);
  selection.rangePolicy = rendering::ChannelRangePolicy::AUTO;
  REQUIRE(pass->setSelection(selection));
  anari::setParameter(fixture.device, fixture.device, "test.samples", samples);
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  checkGray(pipeline, 0, 0);
  checkGray(pipeline, 1, 128);
  checkGray(pipeline, 2, 255);
  checkGray(pipeline, 3, 0);
}

TEST_CASE("Unfamiliar integers are numeric even at UINT32 limits",
    "[ChannelVisualization]")
{
  ChannelDevice fixture;
  REQUIRE(fixture.device);
  anari::setParameter(
      fixture.device, fixture.device, "test.type", ANARI_UINT32);
  rendering::ImagePipeline pipeline(2, 2);
  auto *source =
      pipeline.setSource<rendering::AnariSceneRenderPass>(fixture.device);
  source->setRunAsync(false);
  auto *pass = pipeline.addPass<rendering::ChannelVisualizationPass>();
  pass->setUseCUDA(GENERATE(false, true));
  rendering::FrameChannelSelection selection;
  std::string error;
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), "Temperature_RAW", selection, error));
  REQUIRE(selection.visualization == "grayscale");
  REQUIRE(selection.pixelType == ANARI_UINT32);
  const vsr::math::uint4 samples(0u, 2147483648u, 4294967294u, 4294967295u);
  anari::setParameter(
      fixture.device, fixture.device, "test.integerSamples", samples);
  REQUIRE(pass->setSelection(selection));
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  checkGray(pipeline, 0, 0);
  checkGray(pipeline, 1, 128);
  checkGray(pipeline, 2, 255);
  checkGray(pipeline, 3, 255);
  // Distinct large integers must not collapse into a constant FLOAT32 frame.
  const vsr::math::uint4 close(
      4294967292u, 4294967293u, 4294967294u, 4294967295u);
  anari::setParameter(
      fixture.device, fixture.device, "test.integerSamples", close);
  pipeline.render();
  checkGray(pipeline, 0, 0);
  checkGray(pipeline, 1, 85);
  checkGray(pipeline, 2, 170);
  checkGray(pipeline, 3, 255);
}

TEST_CASE("Visualization status never labels pending or failed data successful",
    "[ChannelVisualization]")
{
  ChannelDevice fixture;
  REQUIRE(fixture.device);
  rendering::ImagePipeline pipeline(2, 2);
  auto *source =
      pipeline.setSource<rendering::AnariSceneRenderPass>(fixture.device);
  source->setRunAsync(false);
  auto *pass = pipeline.addPass<rendering::ChannelVisualizationPass>();
  pass->setUseCUDA(GENERATE(false, true));
  rendering::FrameChannelSelection selection;
  std::string error;
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), "Temperature_RAW", selection, error));
  REQUIRE(pass->setSelection(selection));
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  SECTION("advertised maps can fail after success")
  {
    const char *failure = GENERATE("null", "wrong-size", "wrong-type");
    anari::setParameter(fixture.device, fixture.device, "test.map", failure);
    pipeline.render();
    REQUIRE(pass->status() == rendering::FrameChannelStatus::FAILED);
    CHECK(pass->error().find("Temperature_RAW") != std::string::npos);
    REQUIRE(pipeline.channelResult("Temperature_RAW"));
    CHECK_FALSE(pipeline.channelResult("Temperature_RAW")->data);
    for (size_t i = 0; i < 4; ++i)
      checkGray(pipeline, i, 0);
    anariSetParameter(
        fixture.device, fixture.device, "test.map", ANARI_STRING, "");
    // Failed production is sticky until an explicit source refresh.
    source->setRenderer(nullptr);
    pipeline.render();
    INFO(pass->error());
    REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
    checkGray(pipeline, 3, 255);
  }
  SECTION("asynchronous transition is pending, not failed")
  {
    source->setRunAsync(true);
    anari::setParameter(fixture.device, fixture.device, "test.pending", true);
    pipeline.setDimensions(3, 1);
    REQUIRE(pass->status() == rendering::FrameChannelStatus::PENDING);
    pipeline.render();
    REQUIRE(pass->status() == rendering::FrameChannelStatus::PENDING);
    CHECK(pass->error().empty());
    for (size_t i = 0; i < 3; ++i)
      checkGray(pipeline, i, 0);
    anari::setParameter(fixture.device, fixture.device, "test.pending", false);
    pipeline.render();
    REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
    checkGray(pipeline, 2, 255);
  }
  SECTION("disabled demand is removed, standard demands remain")
  {
    auto *outline = pipeline.addPass<rendering::OutlineRenderPass>();
    outline->setOutlineId(7u);
    auto *tone = pipeline.addPass<rendering::ToneMapPass>();
    source->setColorFormat(ANARI_FLOAT32_VEC4);
    tone->setHDREnabled(true);
    pass->setEnabled(false);
    pipeline.render();
    CHECK_FALSE(pipeline.channelResult("Temperature_RAW"));
    CHECK(source->namedChannels().empty());
    CHECK(rendering::hasChannels(
        source->channels(), rendering::ImageChannels::OBJECT_ID));
    bool requested = false;
    REQUIRE(anariGetProperty(fixture.device,
        source->getFrame(),
        "test.requested.channel.objectId",
        ANARI_BOOL,
        &requested,
        sizeof(requested),
        ANARI_WAIT));
    CHECK(requested);
    CHECK(rendering::hasChannels(
        source->channels(), rendering::ImageChannels::HDR_COLOR));
    CHECK(pass->status() == rendering::FrameChannelStatus::PENDING);
    pass->setEnabled(true);
    tone->setEnabled(false);
    pipeline.render();
    REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
    checkGray(pipeline, 3, 255);
    selection = rendering::FrameChannelSelection{};
    REQUIRE(pass->setSelection(selection));
    pipeline.render();
    CHECK_FALSE(pipeline.channelResult("Temperature_RAW"));
    CHECK(source->namedChannels().empty());
    REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  }
}

TEST_CASE(
    "Pending channel transitions retain the completed display without success",
    "[ChannelVisualization][ChannelLifecycle]")
{
  ChannelDevice fixture;
  REQUIRE(fixture.device);
  rendering::ImagePipeline pipeline(2, 2);
  auto *source =
      pipeline.setSource<rendering::AnariSceneRenderPass>(fixture.device);
  source->setRunAsync(false);
  auto *pass = pipeline.addPass<rendering::ChannelVisualizationPass>();
  pass->setUseCUDA(GENERATE(false, true));
  rendering::FrameChannelSelection selection;
  std::string error;
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), "Temperature_RAW", selection, error));
  REQUIRE(pass->setSelection(selection));
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  const std::vector<uint32_t> completed(
      pipeline.getColorBuffer(), pipeline.getColorBuffer() + 4);
  source->setRunAsync(true);
  anari::setParameter(fixture.device, fixture.device, "test.pending", true);
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), "motionVectors", selection, error));
  REQUIRE(pass->setSelection(selection));
  for (int i = 0; i < 3; ++i) {
    pipeline.render();
    CHECK(pass->status() == rendering::FrameChannelStatus::PENDING);
    CHECK(pass->error().empty());
    REQUIRE(pipeline.channelResult("channel.motionVectors"));
    CHECK_FALSE(pipeline.channelResult("channel.motionVectors")->data);
    CHECK(std::vector<uint32_t>(
              pipeline.getColorBuffer(), pipeline.getColorBuffer() + 4)
        == completed);
  }
  anari::setParameter(fixture.device, fixture.device, "test.pending", false);
  pipeline.render();
  CHECK(pass->status() == rendering::FrameChannelStatus::VALID);
  CHECK(pipeline.channelResult("channel.motionVectors")->data);
}

TEST_CASE("Color production exposes pending and failed completed frames",
    "[ChannelVisualization][ChannelLifecycle]")
{
  ChannelDevice fixture;
  REQUIRE(fixture.device);
  rendering::ImagePipeline pipeline(2, 2);
  auto *source =
      pipeline.setSource<rendering::AnariSceneRenderPass>(fixture.device);
  auto *pass = pipeline.addPass<rendering::ChannelVisualizationPass>();
  anari::setParameter(fixture.device, fixture.device, "test.pending", true);
  pipeline.render();
  CHECK(pass->status() == rendering::FrameChannelStatus::PENDING);
  pipeline.render();
  CHECK(pass->status() == rendering::FrameChannelStatus::PENDING);
  anari::setParameter(fixture.device, fixture.device, "test.pending", false);
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  const std::vector<uint32_t> completed(
      pipeline.getColorBuffer(), pipeline.getColorBuffer() + 4);
  auto renderer =
      anari::newObject<anari::Renderer>(fixture.device, "diagnostic");
  REQUIRE(renderer);
  source->setRenderer(renderer);
  anari::release(fixture.device, renderer);
  anari::setParameter(fixture.device, fixture.device, "test.pending", true);
  pipeline.render();
  CHECK(pass->status() == rendering::FrameChannelStatus::PENDING);
  CHECK(std::vector<uint32_t>(
            pipeline.getColorBuffer(), pipeline.getColorBuffer() + 4)
      == completed);
  anari::setParameter(fixture.device, fixture.device, "test.pending", false);
  anari::setParameter(fixture.device,
      fixture.device,
      "test.map",
      GENERATE("null", "wrong-size", "wrong-type"));
  pipeline.render();
  CHECK(pass->status() == rendering::FrameChannelStatus::FAILED);
  CHECK(pass->error().find("channel.color") != std::string::npos);
}

TEST_CASE(
    "Resize and Color release only unused demand while Pick Requests stay isolated",
    "[ChannelVisualization][ChannelLifecycle]")
{
  ChannelDevice fixture;
  REQUIRE(fixture.device);
  rendering::ImagePipeline pipeline(2, 2);
  auto *source =
      pipeline.setSource<rendering::AnariSceneRenderPass>(fixture.device);
  source->setRunAsync(false);
  auto camera = anari::newObject<anari::Camera>(fixture.device, "perspective");
  auto renderer =
      anari::newObject<anari::Renderer>(fixture.device, "diagnostic");
  auto world = anari::newObject<anari::World>(fixture.device);
  source->setCamera(camera);
  source->setRenderer(renderer);
  source->setWorld(world);
  anari::release(fixture.device, camera);
  anari::release(fixture.device, renderer);
  anari::release(fixture.device, world);
  auto *pass = pipeline.addPass<rendering::ChannelVisualizationPass>();
  auto *consumer = pipeline.addPass<SharedConsumer>();
  auto *outline = pipeline.addPass<rendering::OutlineRenderPass>();
  outline->setOutlineId(7u);
  rendering::WorldBoundsOverlay bounds(pipeline);
  bounds.setShown(true);
  const vsr::math::box3 box{{-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}};
  const auto view = rendering::CameraView::perspective({0.f, 0.f, 5.f},
      {0.f, 0.f, -1.f},
      {0.f, 1.f, 0.f},
      vsr::math::radians(60.f));
  bounds.update(box, view);
  rendering::FrameChannelSelection selection;
  std::string error;
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), "Temperature_RAW", selection, error));
  REQUIRE(pass->setSelection(selection));
  pipeline.render();
  pipeline.setDimensions(3, 1);
  CHECK(pipeline.channelResult("Temperature_RAW")->status
      == rendering::FrameChannelStatus::PENDING);
  CHECK_FALSE(pipeline.channelResult("Temperature_RAW")->data);
  pipeline.render();
  CHECK(pipeline.channelResult("Temperature_RAW")->width == 3);
  const std::vector<uint32_t> pixels(
      pipeline.getColorBuffer(), pipeline.getColorBuffer() + 3);
  uint32_t before = 0, after = 0;
  REQUIRE(anari::getProperty(
      fixture.device, source->getFrame(), "test.renders", before, ANARI_WAIT));
  rendering::PickRequest request;
  request.pixel = {1, 0};
  const auto hit = rendering::pick(*source, request);
  REQUIRE(hit);
  CHECK(hit->depth == 1.f);
  REQUIRE(anari::getProperty(
      fixture.device, source->getFrame(), "test.renders", after, ANARI_WAIT));
  CHECK(after == before);
  CHECK(std::vector<uint32_t>(
            pipeline.getColorBuffer(), pipeline.getColorBuffer() + 3)
      == pixels);
  REQUIRE(pass->setSelection({}));
  pipeline.render();
  REQUIRE(pipeline.channelResult("Temperature_RAW"));
  CHECK(pipeline.channelResult("Temperature_RAW")->status
      == rendering::FrameChannelStatus::VALID);
  consumer->setEnabled(false);
  pipeline.render();
  CHECK_FALSE(pipeline.channelResult("Temperature_RAW"));
  auto requested = [&](const char *name) {
    bool value = false;
    REQUIRE(anari::getProperty(
        fixture.device, source->getFrame(), name, value, ANARI_WAIT));
    return value;
  };
  CHECK_FALSE(requested("test.requested.Temperature_RAW"));
  CHECK(requested("test.requested.channel.objectId"));
  CHECK(requested("test.requested.channel.depth"));
  outline->setEnabled(false);
  bounds.setShown(false);
  bounds.update(box, view);
  pipeline.render();
  CHECK_FALSE(requested("test.requested.channel.objectId"));
  CHECK_FALSE(requested("test.requested.channel.depth"));
}

TEST_CASE("Visualization rejects unimplemented modes and invalid ranges",
    "[ChannelVisualization]")
{
  ChannelDevice fixture;
  REQUIRE(fixture.device);
  rendering::ImagePipeline pipeline(2, 2);
  auto *source =
      pipeline.setSource<rendering::AnariSceneRenderPass>(fixture.device);
  source->setRunAsync(false);
  auto *pass = pipeline.addPass<rendering::ChannelVisualizationPass>();
  rendering::FrameChannelSelection selection;
  std::string error;
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), "Temperature_RAW", selection, error));
  SECTION("Fixed bounds must be finite and ordered")
  {
    selection.rangePolicy = rendering::ChannelRangePolicy::FIXED;
    selection.rangeMin = GENERATE(1.f,
        2.f,
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN());
    selection.rangeMax = 1.f;
  }
  SECTION("scalar Color is incompatible")
  {
    selection.visualization = "color";
  }
  SECTION("unsupported storage")
  {
    selection.pixelType = ANARI_FLOAT64;
  }
  SECTION("nonexistent vector component")
  {
    REQUIRE(rendering::resolveFrameChannelSelection(
        source->channelCatalog(), "motionVectors", selection, error));
    selection.visualization = "component-z";
  }
  REQUIRE_FALSE(pass->setSelection(selection));
  REQUIRE(pass->status() == rendering::FrameChannelStatus::FAILED);
  CHECK_FALSE(pass->error().empty());
  pipeline.render();
  CHECK(source->namedChannels().empty());
  REQUIRE(pass->status() == rendering::FrameChannelStatus::FAILED);
  for (size_t i = 0; i < 4; ++i)
    checkGray(pipeline, i, 0);
}

TEST_CASE(
    "Scalar diagnostics bypass beauty transforms and preserve exposure exchange",
    "[ChannelVisualization]")
{
  ChannelDevice fixture;
  REQUIRE(fixture.device);
  rendering::ImagePipeline pipeline(2, 2);
  auto *source =
      pipeline.setSource<rendering::AnariSceneRenderPass>(fixture.device);
  source->setRunAsync(false);
  source->setColorFormat(ANARI_FLOAT32_VEC4);
  auto *exposure = pipeline.addPass<rendering::AutoExposurePass>();
  exposure->setHDREnabled(true);
  auto *tone = pipeline.addPass<rendering::ToneMapPass>();
  tone->setHDREnabled(true);
  tone->setAutoExposureEnabled(true);
  tone->setExposure(3.f);
  auto *output = pipeline.addPass<rendering::OutputTransformPass>();
  output->setColorFormat(ANARI_FLOAT32_VEC4);
  auto *pass = pipeline.addPass<rendering::ChannelVisualizationPass>();
  pass->setUseCUDA(GENERATE(false, true));
  auto *reader = pipeline.addPass<ExposureReader>();
  pipeline.render();
  uint32_t beauty[4];
  std::memcpy(beauty, pipeline.getColorBuffer(), sizeof(beauty));
  CHECK(reader->seen == Approx(exposure->currentExposure()));
  CHECK(reader->seen != 0.f);
  rendering::FrameChannelSelection selection;
  std::string error;
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), "Temperature_RAW", selection, error));
  REQUIRE(pass->setSelection(selection));
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  checkGray(pipeline, 0, 0);
  checkGray(pipeline, 1, 85);
  checkGray(pipeline, 2, 170);
  checkGray(pipeline, 3, 255);
  CHECK(reader->seen == Approx(exposure->currentExposure()));
  selection.visualization = "component-x";
  REQUIRE(rendering::resolveFrameChannelSelection(
      source->channelCatalog(), "motionVectors", selection, error, true));
  REQUIRE(pass->setSelection(selection));
  pipeline.render();
  REQUIRE(pass->status() == rendering::FrameChannelStatus::VALID);
  checkGray(pipeline, 0, 0);
  checkGray(pipeline, 1, 255);
  checkGray(pipeline, 2, 0);
  checkGray(pipeline, 3, 255);
  selection = rendering::FrameChannelSelection{};
  REQUIRE(pass->setSelection(selection));
  pipeline.render();
  for (size_t i = 0; i < 4; ++i)
    CHECK(pipeline.getColorBuffer()[i] == beauty[i]);
}

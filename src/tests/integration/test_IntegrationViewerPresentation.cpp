// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_rendering
#include "vsr/rendering/pipeline/ImagePipeline.h"
#include "vsr/rendering/pipeline/passes/AnariSceneRenderPass.h"
#include "vsr/rendering/pipeline/passes/ExternalFrameSource.h"
#include "vsr/rendering/pipeline/passes/CopyToSDLTexturePass.h"
// catch
#include "catch.hpp"
// std
#include <array>
#include <cstring>

using namespace vsr::rendering;

TEST_CASE("Viewer upload is not a checked display submission",
    "[integration][viewer-presentation]")
{
  auto *surface = SDL_CreateSurface(8, 6, SDL_PIXELFORMAT_RGBA32);
  REQUIRE(surface);
  auto *renderer = SDL_CreateSoftwareRenderer(surface);
  REQUIRE(renderer);
  {
    ImagePipeline pipeline(4, 3, ImageStorage::HOST);
    // Bottom-left source, asymmetric corners: red, green, blue and yellow.
    const std::array<std::uint32_t, 12> source{0xff0000ff,
        0xff0000ff,
        0xff00ff00,
        0xff00ff00,
        0xff0000ff,
        0xff0000ff,
        0xff00ff00,
        0xff00ff00,
        0xffff0000,
        0xffff0000,
        0xff00ffff,
        0xff00ffff};
    std::vector<std::uint8_t> bytes(sizeof(source));
    std::memcpy(bytes.data(), source.data(), sizeof(source));
    pipeline.setSource<ExternalFrameSource>()->setFrame(&bytes);
    auto *output = pipeline.addSink<CopyToSDLTexturePass>(renderer);
    REQUIRE_FALSE(output->presentation());
    std::shared_ptr<const void> acquired;
    output->setReadyPixelsProvider([&](const std::uint32_t *pixels,
                                       std::uint32_t width,
                                       std::uint32_t height,
                                       const char *path) {
      REQUIRE(width == pipeline.dimensions().x);
      REQUIRE(height == pipeline.dimensions().y);
      REQUIRE(std::string(path) == "host_sdl_upload");
      auto copy = std::make_shared<const std::vector<std::uint32_t>>(
          pixels, pixels + width * height);
      acquired = copy;
      return std::static_pointer_cast<const void>(copy);
    });
    pipeline.render();
    REQUIRE(acquired);
    REQUIRE_FALSE(output->presentedPixelsState());
    REQUIRE(output->upload());
    REQUIRE_FALSE(output->presentation());
    REQUIRE(output->draw({0.f, 0.f, 8.f, 6.f}, {0, 0, 8, 6}));
    REQUIRE_FALSE(output->presentation());
    // A failed final submission cannot certify a queued texture draw.
    output->completePresentation(false);
    REQUIRE_FALSE(output->presentation());
    REQUIRE(output->draw({0.f, 0.f, 8.f, 6.f}, {0, 0, 8, 6}));
    REQUIRE(SDL_RenderPresent(renderer));
    output->completePresentation(true);
    const auto receipt = output->presentation();
    REQUIRE(receipt);
    REQUIRE(output->presentedPixelsState() == acquired);
    const auto held = output->presentedPixelsState();
    REQUIRE(receipt->upload == output->upload()->identity);
    REQUIRE(receipt->width == 4);
    REQUIRE(receipt->height == 3);
    REQUIRE(receipt->displayWidth == 8.f);
    REQUIRE(receipt->displayHeight == 6.f);
    REQUIRE(receipt->framebufferScaleX == 1.f);
    REQUIRE(receipt->framebufferScaleY == 1.f);
    const auto pixel = [&](int x, int y) {
      std::uint32_t value;
      std::memcpy(&value,
          static_cast<const std::uint8_t *>(surface->pixels)
              + y * surface->pitch + x * 4,
          4);
      return value;
    };
    REQUIRE(pixel(0, 0) == 0xffff0000);
    REQUIRE(pixel(7, 0) == 0xff00ffff);
    REQUIRE(pixel(0, 5) == 0xff0000ff);
    REQUIRE(pixel(7, 5) == 0xff00ff00);
    // Scaling belongs to the successful draw, not an older upload's binding.
    REQUIRE(output->draw({0.f, 0.f, 8.f, 6.f}, {0, 0, 8, 6}, 2.f, 1.5f));
    REQUIRE(SDL_RenderPresent(renderer));
    output->completePresentation(true);
    REQUIRE(output->presentation()->framebufferScaleX == 2.f);
    REQUIRE(output->presentation()->framebufferScaleY == 1.5f);
    REQUIRE(output->presentation()->upload == receipt->upload);
    const auto scaledIdentity = output->presentation()->identity;
    // Producing/uploading newer pixels cannot advance last submitted identity.
    pipeline.render();
    REQUIRE(output->upload()->identity > receipt->upload);
    REQUIRE(output->presentation()->identity == scaledIdentity);
    REQUIRE(output->presentedPixelsState() == held);
    REQUIRE(acquired != held);
    output->completePresentation(true); // no queued viewport draw
    REQUIRE(output->presentation()->identity == scaledIdentity);
    // A draw wholly outside the clip is produced-but-unshown, even when SDL
    // would accept the empty draw and the application's submission succeeds.
    REQUIRE_FALSE(output->draw({20.f, 20.f, 8.f, 6.f}, {0, 0, 8, 6}));
    output->completePresentation(true);
    REQUIRE(output->presentation()->identity == scaledIdentity);
    // Resize retires the texture source without reusing its upload identity.
    pipeline.setDimensions(2, 2);
    bytes.resize(2 * 2 * 4);
    REQUIRE_FALSE(output->upload());
    REQUIRE_FALSE(output->presentation());
    REQUIRE_FALSE(output->presentedPixelsState());
    REQUIRE(
        std::static_pointer_cast<const std::vector<std::uint32_t>>(held)->at(0)
        == 0xff0000ff);
    pipeline.render();
    REQUIRE(output->upload()->identity > receipt->upload);
    REQUIRE_FALSE(output->draw({0.f, 0.f, 0.f, 6.f}, {0, 0, 8, 6}));
    output->completePresentation(true);
    REQUIRE_FALSE(output->presentation());
  }
  SDL_DestroyRenderer(renderer);
  SDL_DestroySurface(surface);
}

TEST_CASE("Async viewer pixels retain their actual ANARI start state",
    "[integration][viewer-presentation]")
{
  auto library = anari::loadLibrary("helide", nullptr, nullptr);
  REQUIRE(library);
  auto device = anari::newDevice(library, "default");
  REQUIRE(device);
  anari::commitParameters(device, device);
  auto camera = anari::newObject<anari::Camera>(device, "perspective");
  auto renderer = anari::newObject<anari::Renderer>(device, "default");
  auto world = anari::newObject<anari::World>(device);
  anari::commitParameters(device, camera);
  anari::commitParameters(device, renderer);
  anari::commitParameters(device, world);
  {
    ImagePipeline pipeline(32, 16);
    auto *pass = pipeline.setSource<AnariSceneRenderPass>(device);
    pass->setCamera(camera);
    pass->setRenderer(renderer);
    pass->setWorld(world);
    auto first = std::make_shared<const int>(11);
    auto second = std::make_shared<const int>(22);
    std::shared_ptr<const void> current = first;
    int starts = 0;
    pass->setFrameStateProvider([&] {
      ++starts;
      return current;
    });
    REQUIRE_FALSE(pass->frameState());
    pass->setRunAsync(false);
    pipeline.render();
    REQUIRE(starts == 1);
    REQUIRE(pass->frameState() == first);
    // Changing effective state must not relabel the completed old image.
    current = second;
    pass->setRunAsync(true);
    pipeline.render();
    REQUIRE(starts == 2);
    REQUIRE(pass->frameState() == first);
    pass->waitForCompletion();
    pipeline.render();
    REQUIRE(starts == 3);
    REQUIRE(pass->frameState() == second);
    // Missing producer attestation remains missing, never the latest setting.
    pass->setFrameStateProvider({});
    pass->waitForCompletion();
    pipeline.render();
    pass->waitForCompletion();
    pipeline.render();
    REQUIRE_FALSE(pass->frameState());
    pass->setFrameStateProvider(
        []() -> AnariSceneRenderPass::FrameState { throw std::bad_alloc(); });
    pass->setRunAsync(false);
    const auto produced = pipeline.production();
    pipeline.render();
    REQUIRE(pipeline.production() == produced + 1);
    REQUIRE_FALSE(pass->frameState());
  }
  anari::release(device, world);
  anari::release(device, renderer);
  anari::release(device, camera);
  anari::release(device, device);
  anari::unloadLibrary(library);
}

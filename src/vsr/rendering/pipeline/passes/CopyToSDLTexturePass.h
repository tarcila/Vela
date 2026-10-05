// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifndef ENABLE_SDL
#define ENABLE_SDL 1
#endif

#if ENABLE_SDL
#include "ImagePass.h"
// SDL3
#include <SDL3/SDL.h>
// std
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace vsr::rendering {

// Fixed-size owning-thread receipts, never pointers to mutable pipeline data.
// Upload success alone is NOT presentation, readback or source qualification.
struct SDLTextureUpload
{
  std::uint64_t identity{0};
  std::uint32_t width{0}, height{0};
  const char *path{"unknown"};
  double durationMs{0.};
};

struct SDLTexturePresentation
{
  std::uint64_t identity{0}, upload{0};
  std::uint32_t width{0}, height{0};
  float displayWidth{0.f}, displayHeight{0.f};
  double submittedAt{0.};
  float displayX{0.f}, displayY{0.f};
  float framebufferScaleX{1.f}, framebufferScaleY{1.f};
};

struct CopyToSDLTexturePass : public ImageSink
{
  CopyToSDLTexturePass(SDL_Renderer *renderer);
  ~CopyToSDLTexturePass() override;
  const char *name() const override;

  SDL_Texture *getTexture() const;
  std::optional<SDLTextureUpload> upload() const;
  std::optional<SDLTexturePresentation> presentation() const;
  std::optional<SDLTexturePresentation> queuedDraw() const;
  // Queue the actual vertically flipped viewport quad, checking SDL success.
  // The application must then report its actual SDL_RenderPresent result.
  bool draw(const SDL_FRect &destination,
      const SDL_Rect &clip,
      float framebufferScaleX = 1.f,
      float framebufferScaleY = 1.f);
  void completePresentation(bool submitted);
  // Optional bounded owning-thread observation after the natural SDL upload.
  // Host/managed-host pixels are CPU-ready here and not reused until return.
  // Interop is explicitly unsupported: no observation-induced driver readback.
  using ReadyPixelsProvider = std::function<std::shared_ptr<const void>(
      const std::uint32_t *, std::uint32_t, std::uint32_t, const char *)>;
  void setReadyPixelsProvider(ReadyPixelsProvider provider);
  std::shared_ptr<const void> presentedPixelsState() const;

 private:
  void checkGLInterop() const;
  void render(const ImageBuffers &b) override;
  void updateSize() override;

  struct CopyToSDLTexturePassImpl;
  CopyToSDLTexturePassImpl *m_impl{nullptr};
};

} // namespace vsr::rendering
#endif

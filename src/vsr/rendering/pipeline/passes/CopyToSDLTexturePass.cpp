// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#if ENABLE_SDL

#include "CopyToSDLTexturePass.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// cuda + SDL
#ifdef ENABLE_CUDA
#include <SDL3/SDL_opengl.h>
#include <cuda_gl_interop.h>
#include <cuda_runtime_api.h>
#endif
// std
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>

namespace vsr::rendering {

struct CopyToSDLTexturePass::CopyToSDLTexturePassImpl
{
  SDL_Renderer *renderer{nullptr};
  SDL_Texture *texture{nullptr};
  std::optional<bool> glInteropAvailable;
  std::optional<SDLTextureUpload> upload;
  std::optional<SDLTexturePresentation> queued;
  std::optional<SDLTexturePresentation> presented;
  std::uint64_t uploads{0}, presentations{0};
  ReadyPixelsProvider readyPixelsProvider;
  std::shared_ptr<const void> uploadedState, presentedState;
#ifdef ENABLE_CUDA
  cudaGraphicsResource_t graphicsResource{nullptr};
#endif
};

CopyToSDLTexturePass::CopyToSDLTexturePass(SDL_Renderer *renderer)
{
  m_impl = new CopyToSDLTexturePassImpl;
  m_impl->renderer = renderer;
}

CopyToSDLTexturePass::~CopyToSDLTexturePass()
{
#ifdef ENABLE_CUDA
  if (m_impl->graphicsResource)
    cudaGraphicsUnregisterResource(m_impl->graphicsResource);
#endif
  SDL_DestroyTexture(m_impl->texture);
  delete m_impl;
  m_impl = nullptr;
}

const char *CopyToSDLTexturePass::name() const
{
  return "Copy-To-SDL";
}

SDL_Texture *CopyToSDLTexturePass::getTexture() const
{
  return m_impl->texture;
}

std::optional<SDLTextureUpload> CopyToSDLTexturePass::upload() const
{
  return m_impl->upload;
}

std::optional<SDLTexturePresentation> CopyToSDLTexturePass::presentation() const
{
  return m_impl->presented;
}

std::optional<SDLTexturePresentation> CopyToSDLTexturePass::queuedDraw() const
{
  return m_impl->queued;
}

void CopyToSDLTexturePass::setReadyPixelsProvider(ReadyPixelsProvider provider)
{
  m_impl->readyPixelsProvider = std::move(provider);
  m_impl->uploadedState.reset();
  m_impl->presentedState.reset();
}

std::shared_ptr<const void> CopyToSDLTexturePass::presentedPixelsState() const
{
  return m_impl->presentedState;
}

bool CopyToSDLTexturePass::draw(const SDL_FRect &destination,
    const SDL_Rect &clip,
    float framebufferScaleX,
    float framebufferScaleY)
{
  m_impl->queued.reset();
  if (!m_impl->texture || !m_impl->upload || clip.w <= 0 || clip.h <= 0
      || !std::isfinite(destination.x) || !std::isfinite(destination.y)
      || !std::isfinite(destination.w) || !std::isfinite(destination.h)
      || destination.w <= 0.f || destination.h <= 0.f
      || !std::isfinite(framebufferScaleX) || !std::isfinite(framebufferScaleY)
      || framebufferScaleX <= 0.f || framebufferScaleY <= 0.f)
    return false;
  // Same white, bottom-left UV quad as ImGui::Image(...,{0,1},{1,0}).
  // Unlike ImGui's generic SDL backend, preserve the actual draw return value.
  const SDL_FColor white{1.f, 1.f, 1.f, 1.f};
  const float right = destination.x + destination.w;
  const float bottom = destination.y + destination.h;
  if (!std::isfinite(right) || !std::isfinite(bottom)
      || double(right) <= double(clip.x) || double(bottom) <= double(clip.y)
      || double(destination.x) >= double(clip.x) + double(clip.w)
      || double(destination.y) >= double(clip.y) + double(clip.h))
    return false;
  const SDL_Vertex vertices[] = {
      {{destination.x, destination.y}, white, {0.f, 1.f}},
      {{right, destination.y}, white, {1.f, 1.f}},
      {{right, bottom}, white, {1.f, 0.f}},
      {{destination.x, bottom}, white, {0.f, 0.f}}};
  const int indices[] = {0, 1, 2, 0, 2, 3};
  if (!SDL_SetRenderClipRect(m_impl->renderer, &clip)
      || !SDL_RenderGeometry(
          m_impl->renderer, m_impl->texture, vertices, 4, indices, 6))
    return false;
  const auto &upload = *m_impl->upload;
  m_impl->queued = SDLTexturePresentation{0,
      upload.identity,
      upload.width,
      upload.height,
      destination.w,
      destination.h,
      0.,
      destination.x,
      destination.y,
      framebufferScaleX,
      framebufferScaleY};
  return true;
}

void CopyToSDLTexturePass::completePresentation(bool submitted)
{
  if (submitted && m_impl->queued && m_impl->upload
      && m_impl->queued->upload == m_impl->upload->identity
      && m_impl->presentations != std::numeric_limits<std::uint64_t>::max()) {
    m_impl->queued->identity = ++m_impl->presentations;
    m_impl->queued->submittedAt = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch())
                                      .count();
    m_impl->presented = m_impl->queued;
    // Null state on a newer successful presentation must retire the old copy.
    m_impl->presentedState = m_impl->uploadedState;
  }
  m_impl->queued.reset();
}

void CopyToSDLTexturePass::checkGLInterop() const
{
#ifdef ENABLE_CUDA
  unsigned int numDevices = 0;
  int cudaDevices[8]; // Assuming max 8 devices for simplicity

  cudaError_t err =
      cudaGLGetDevices(&numDevices, cudaDevices, 8, cudaGLDeviceListAll);
  if (err != cudaSuccess) {
    vsr::core::logWarning(
        "[ImagePipeline] failed to get CUDA GL devices -- reason: %s",
        cudaGetErrorString(err));
    cudaGetLastError(); // Clear the error so it is not captured by subsequent
                        // calls.
    m_impl->glInteropAvailable = false;
    return;
  }

  if (numDevices > 0) {
    int currentDevice = 0;
    cudaGetDevice(&currentDevice);
    for (unsigned int i = 0; i < numDevices; ++i) {
      if (currentDevice == cudaDevices[i]) {
        vsr::core::logStatus("[ImagePipeline] using CUDA-GL interop via SDL3");
        m_impl->glInteropAvailable = true;
        return;
      }
    }
  }
#endif

  vsr::core::logWarning(
      "[ImagePipeline] unable to use CUDA-GL interop via SDL3");
  m_impl->glInteropAvailable = false;
}

void CopyToSDLTexturePass::render(const ImageBuffers &b)
{
  const auto start = std::chrono::steady_clock::now();
  // A failed newer transfer cannot be stamped with an older upload's identity.
  m_impl->upload.reset();
  m_impl->queued.reset();
  m_impl->uploadedState.reset();
  const auto size = dimensions();
  if (!b.color || !size.x || !size.y || !m_impl->texture
      || size.x > std::uint32_t(std::numeric_limits<int>::max() / 4)
      || m_impl->uploads == std::numeric_limits<std::uint64_t>::max())
    return;
#ifdef ENABLE_CUDA
  if (b.stream && !m_impl->glInteropAvailable) {
    checkGLInterop();
    if (*m_impl->glInteropAvailable)
      updateSize();
  }
#endif
  bool copied = false;
  const char *path = "host_sdl_upload";
#ifdef ENABLE_CUDA
  if (b.stream && m_impl->graphicsResource) {
    path = "cuda_gl_interop";
    const bool mapped =
        cudaGraphicsMapResources(1, &m_impl->graphicsResource, b.stream)
        == cudaSuccess;
    cudaArray_t array = nullptr;
    if (mapped) {
      copied = cudaGraphicsSubResourceGetMappedArray(
                   &array, m_impl->graphicsResource, 0, 0)
              == cudaSuccess
          && cudaMemcpy2DToArrayAsync(array,
                 0,
                 0,
                 b.color,
                 size.x * sizeof(b.color[0]),
                 size.x * sizeof(b.color[0]),
                 size.y,
                 cudaMemcpyDeviceToDevice,
                 b.stream)
              == cudaSuccess;
      const auto unmapped =
          cudaGraphicsUnmapResources(1, &m_impl->graphicsResource, b.stream);
      const auto completed = cudaStreamSynchronize(b.stream);
      copied = copied && unmapped == cudaSuccess && completed == cudaSuccess;
    }
  } else {
    // The normal display fallback already reads managed memory. Explicitly
    // complete preceding pipeline kernels before SDL touches it. This is NOT
    // a bounded observation readback and does not qualify MCP acquisition.
    const bool ready =
        !b.stream || cudaStreamSynchronize(b.stream) == cudaSuccess;
    if (b.stream)
      path = "managed_host_sdl_upload";
    copied = ready
        && SDL_UpdateTexture(m_impl->texture,
            nullptr,
            b.color,
            int(size.x * sizeof(b.color[0])));
  }
#else
  copied = SDL_UpdateTexture(
      m_impl->texture, nullptr, b.color, int(size.x * sizeof(b.color[0])));
#endif
  if (copied) {
    if (m_impl->readyPixelsProvider) {
      try {
        // SDL_UpdateTexture has just consumed these CPU-ready bytes. No extra
        // CUDA copy, query, wait or retained mutable pointer is introduced.
        // Interop would require a separately qualified acquisition path.
        m_impl->uploadedState = m_impl->readyPixelsProvider(
            path == std::string_view("cuda_gl_interop") ? nullptr : b.color,
            size.x,
            size.y,
            path);
      } catch (...) {
        m_impl->uploadedState.reset(); // observation cannot stop normal display
      }
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start)
                             .count();
    m_impl->upload =
        SDLTextureUpload{++m_impl->uploads, size.x, size.y, path, elapsed};
  }
}

void CopyToSDLTexturePass::updateSize()
{
  m_impl->upload.reset();
  m_impl->queued.reset();
  m_impl->presented.reset();
  m_impl->uploadedState.reset();
  m_impl->presentedState.reset();
#ifdef ENABLE_CUDA
  if (m_impl->graphicsResource) {
    cudaGraphicsUnregisterResource(m_impl->graphicsResource);
    m_impl->graphicsResource = nullptr;
  }
#endif

  if (m_impl->texture)
    SDL_DestroyTexture(m_impl->texture);
  auto newSize = dimensions();
  m_impl->texture = SDL_CreateTexture(m_impl->renderer,
      SDL_PIXELFORMAT_RGBA32,
      SDL_TEXTUREACCESS_STREAMING,
      newSize.x,
      newSize.y);

#ifdef ENABLE_CUDA
  if (m_impl->texture && m_impl->glInteropAvailable
      && *m_impl->glInteropAvailable) {
    SDL_PropertiesID propID = SDL_GetTextureProperties(m_impl->texture);
    Sint64 texID = SDL_GetNumberProperty(
        propID, SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER, -1);

    if (texID > 0) {
      if (cudaGraphicsGLRegisterImage(&m_impl->graphicsResource,
              static_cast<GLuint>(texID),
              GL_TEXTURE_2D,
              cudaGraphicsRegisterFlagsWriteDiscard)
          != cudaSuccess) {
        m_impl->graphicsResource = nullptr;
        m_impl->glInteropAvailable = false;
      }
    } else {
      vsr::core::logWarning(
          "[ImagePipeline] could not get SDL texture number!");
    }
  }
#endif
}

} // namespace vsr::rendering

#endif

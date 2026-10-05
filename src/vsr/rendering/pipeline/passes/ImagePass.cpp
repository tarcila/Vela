// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ImagePass.h"
// vsr_algorithms
#include "vsr/algorithms/cpu/convertColorBuffer.hpp"
#ifdef VSR_ALGORITHMS_HAS_CUDA
#include "vsr/algorithms/cuda/convertColorBuffer.hpp"
#endif
// std
#include <algorithm>
#include <cstdlib>
#include <cstring>

#if USE_CUDA
#include "cuda_runtime.h"
#endif

namespace vsr::rendering {

const char *toString(ImageStageRole role)
{
  switch (role) {
  case ImageStageRole::SOURCE:
    return "source";
  case ImageStageRole::PASS:
    return "pass";
  case ImageStageRole::SINK:
    return "sink";
  }
  return "unknown";
}

// ImageStage definitions /////////////////////////////////////////////////////

ImageStage::ImageStage() = default;

ImageStage::~ImageStage() = default;

bool ImageStage::isEnabled() const
{
  return m_enabled;
}

vsr::math::uint2 ImageStage::dimensions() const
{
  return m_size;
}

const char *ImageStage::name() const
{
  return "ImageStage";
}

void ImageStage::setEnabled(bool enabled)
{
  m_enabled = enabled;
}

void ImageStage::updateSize()
{
  // no-op
}

void ImageStage::setDimensions(uint32_t width, uint32_t height)
{
  if (m_size.x == width && m_size.y == height)
    return;

  m_size.x = width;
  m_size.y = height;

  updateSize();
}

// Role definitions ///////////////////////////////////////////////////////////

ImageStageRole ImageSource::role() const
{
  return ImageStageRole::SOURCE;
}

ImageChannels ImageSource::supportedChannels() const
{
  return ImageChannels::NONE;
}

ImageChannels ImageSource::channels() const
{
  return m_channels;
}

void ImageSource::updateChannels()
{
  // no-op
}

void ImageSource::setChannels(ImageChannels channels)
{
  if (channels == m_channels)
    return;
  m_channels = channels;
  updateChannels();
}

ImageStageRole ImagePass::role() const
{
  return ImageStageRole::PASS;
}

ImageChannels ImagePass::requiredChannels() const
{
  return ImageChannels::NONE;
}

ImageStageRole ImageSink::role() const
{
  return ImageStageRole::SINK;
}

// Utility functions //////////////////////////////////////////////////////////

namespace detail {

void *allocate_(size_t numBytes)
{
#ifdef ENABLE_CUDA
  void *ptr = nullptr;
  cudaMallocManaged(&ptr, numBytes);
  return ptr;
#else
  return std::malloc(numBytes);
#endif
}

void free_(void *ptr)
{
#ifdef ENABLE_CUDA
  cudaFree(ptr);
#else
  std::free(ptr);
#endif
}

void memcpy_(void *dst, const void *src, size_t numBytes)
{
#ifdef ENABLE_CUDA
  cudaMemcpy(dst, src, numBytes, cudaMemcpyDefault);
#else
  std::memcpy(dst, src, numBytes);
#endif
}

template <typename T>
static void updateChannelBuffer(
    T *&buffer,
    size_t count,
    bool wanted,
    bool had,
    T background,
    bool hostStorage)
{
  if (wanted && had)
    return;
  if (hostStorage)
    std::free(buffer);
  else
    detail::free(buffer);
  buffer = nullptr;
  if (!wanted)
    return;
  buffer = hostStorage ? static_cast<T *>(std::malloc(count * sizeof(T)))
                       : detail::allocate<T>(count);
  std::fill(buffer, buffer + count, background);
}

ImageChannels updateImageBuffers(ImageBuffers &b,
    size_t numPixels,
    ImageChannels current,
    ImageChannels channels,
    bool hostStorage)
{
  if (!b.color)
    current = ImageChannels::NONE; // nothing backed yet
  if (numPixels == 0) {
    freeImageBuffers(b, hostStorage);
    return ImageChannels::NONE;
  }

  if (!b.color) {
    b.color = hostStorage
        ? static_cast<uint32_t *>(std::malloc(numPixels * sizeof(uint32_t)))
        : detail::allocate<uint32_t>(numPixels);
    std::fill(b.color, b.color + numPixels, 0u);
  }

  auto update = [&](auto *&buffer, size_t count, ImageChannels c, auto bg) {
    updateChannelBuffer(
        buffer,
        count,
        hasChannels(channels, c),
        hasChannels(current, c),
        bg,
        hostStorage);
  };

  update(b.hdrColor, numPixels * 4, ImageChannels::HDR_COLOR, 0.f);
  update(b.depth, numPixels, ImageChannels::DEPTH, vsr::math::inf);
  update(b.objectId, numPixels, ImageChannels::OBJECT_ID, ~0u);
  update(b.primitiveId, numPixels, ImageChannels::PRIMITIVE_ID, ~0u);
  update(b.instanceId, numPixels, ImageChannels::INSTANCE_ID, ~0u);
  update(b.albedo, numPixels, ImageChannels::ALBEDO, vsr::math::float3(0.f));
  update(b.normal, numPixels, ImageChannels::NORMAL, vsr::math::float3(0.f));

  return channels;
}

void freeImageBuffers(ImageBuffers &b, bool hostStorage)
{
  auto release = [hostStorage](void *ptr) {
    if (hostStorage)
      std::free(ptr);
    else
      detail::free_(ptr);
  };
  release(b.color);
  release(b.hdrColor);
  release(b.depth);
  release(b.objectId);
  release(b.primitiveId);
  release(b.instanceId);
  release(b.albedo);
  release(b.normal);

  const auto stream = b.stream;
  b = {};
  b.stream = stream;
}

void convertFloatColorBuffer_(
    ComputeStream stream, const float *v, uint8_t *out, size_t totalSize)
{
#ifdef VSR_ALGORITHMS_HAS_CUDA
  if (stream) {
    vsr::algorithms::cuda::convertFloatToUint8(stream, v, out, totalSize);
    return;
  }
#endif
  vsr::algorithms::cpu::convertFloatToUint8(v, out, totalSize);
}

} // namespace detail

} // namespace vsr::rendering

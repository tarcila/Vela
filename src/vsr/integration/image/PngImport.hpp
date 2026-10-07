// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstdint>
#include <functional>
#include <memory_resource>
#include <vector>

namespace vsr::integration::image {

using ImageBytes = std::pmr::vector<std::uint8_t>;
enum class DecodeResult
{
  OK,
  INVALID_PNG,
  RESOURCE_LIMIT,
  CANCELLED
};

/*
 * Decode a private bounded PNG snapshot, preserving raw straight-alpha samples.
 * Caller reserves E + T + W before allocating these buffers. This decoder owns
 * F+1 filtered bytes and a fixed 128 KiB inflate arena, plus constant controls;
 * no profile decoding, concatenated IDAT copy, or growing codec allocation.
 * With the exchange worker's 576 KiB stack this remains below 4F+1 MiB.
 */
DecodeResult decodePng(const ImageBytes &encoded,
    std::size_t width,
    std::size_t height,
    std::size_t channels,
    ImageBytes &decoded,
    const std::function<bool()> &abandoned);

} // namespace vsr::integration::image

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_integration
#include "vsr/integration/content/HdriDecode.hpp"
// std
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <string_view>

namespace vsr::integration::content {

namespace {
HdriDecodeResult failure(HdriDecodeError error)
{
  HdriDecodeResult result;
  result.error = error;
  return result;
}

template <typename Allocator>
bool line(const std::vector<std::uint8_t, Allocator> &bytes,
    size_t &offset,
    std::string_view &out)
{
  const auto start = offset;
  while (offset < bytes.size() && bytes[offset] != '\n') {
    if (offset - start >= 1024 || !bytes[offset])
      return false;
    ++offset;
  }
  if (offset == bytes.size() || offset > (64u << 10))
    return false;
  out = std::string_view(
      reinterpret_cast<const char *>(bytes.data() + start), offset - start);
  ++offset;
  if (!out.empty() && out.back() == '\r')
    out.remove_suffix(1);
  return true;
}

core::math::float3 linearRgb(const std::uint8_t *rgbe)
{
  if (!rgbe[3])
    return {0.f, 0.f, 0.f};
  const float scale = std::ldexp(1.f, int(rgbe[3]) - 136);
  return {rgbe[0] * scale, rgbe[1] * scale, rgbe[2] * scale};
}

template <typename Allocator>
HdriDecodeResult decodeBytes(const std::vector<std::uint8_t, Allocator> &bytes,
    const HdriDecodeLimits &limits,
    const std::function<bool()> &cancelled,
    std::shared_ptr<CapacityArena> arena)
{
  if (bytes.empty())
    return failure(HdriDecodeError::MALFORMED);
  if (bytes.size() > limits.inputBytes
      || bytes.capacity() > limits.workingBytes)
    return failure(HdriDecodeError::LIMIT);
  size_t offset = 0;
  std::string_view text;
  if (!line(bytes, offset, text) || (text != "#?RADIANCE" && text != "#?RGBE"))
    return failure(HdriDecodeError::UNSUPPORTED);
  bool format = false;
  while (true) {
    if (!line(bytes, offset, text))
      return failure(HdriDecodeError::MALFORMED);
    if (text.empty())
      break;
    if (text.rfind("FORMAT=", 0) == 0) {
      if (format || text != "FORMAT=32-bit_rle_rgbe")
        return failure(HdriDecodeError::UNSUPPORTED);
      format = true;
    }
  }
  if (!format || !line(bytes, offset, text))
    return failure(HdriDecodeError::MALFORMED);
  // No locale, stream, token or string allocations before payload admission.
  const auto field = [&text]() {
    const auto start = text.find_first_not_of(" \t\r\v\f");
    if (start == std::string_view::npos) {
      text = {};
      return std::string_view{};
    }
    text.remove_prefix(start);
    const auto end = text.find_first_of(" \t\r\v\f");
    const auto value = text.substr(0, end);
    text.remove_prefix(value.size());
    return value;
  };
  const auto y = field();
  const auto heightText = field();
  const auto x = field();
  const auto widthText = field();
  size_t width = 0, height = 0;
  const auto number = [](std::string_view value, size_t &out) {
    if (value.empty())
      return false;
    const auto result =
        std::from_chars(value.data(), value.data() + value.size(), out);
    return result.ec == std::errc{}
    && result.ptr == value.data() + value.size();
  };
  if (!number(heightText, height) || !number(widthText, width)
      || !field().empty())
    return failure(HdriDecodeError::MALFORMED);
  if (y != "-Y" || x != "+X")
    return failure(HdriDecodeError::UNSUPPORTED);
  if (!width || !height || width > limits.dimension || height > limits.dimension
      || width > limits.pixels / height)
    return failure(HdriDecodeError::LIMIT);
  const auto pixels = width * height;
  constexpr size_t PIXEL_WORK = 2 * sizeof(core::math::float3);
  // Parser views and from_chars are allocation-free; stack controls need no
  // arbitrary heap headroom reservation.
  constexpr size_t CONTROL_BYTES = 0;
  const auto headroom = limits.workingBytes - bytes.capacity();
  if (headroom < CONTROL_BYTES)
    return failure(HdriDecodeError::LIMIT);
  const auto available = headroom - CONTROL_BYTES;
  if (width > available / 4 || pixels > (available - width * 4) / PIXEL_WORK)
    return failure(HdriDecodeError::LIMIT);

  HdriDecodeResult result;
  result.image.width = width;
  result.image.height = height;
  result.image.radiance =
      DecodedHdri::Radiance(CapacityAllocator<core::math::float3>(arena));
  result.image.radiance.resize(pixels);
  AssetRoots::Bytes row(width * 4, 0, CapacityAllocator<std::uint8_t>(arena));
  if (result.image.radiance.capacity() > available / PIXEL_WORK
      || row.capacity()
          > available - result.image.radiance.capacity() * PIXEL_WORK)
    return failure(HdriDecodeError::LIMIT);
  for (size_t j = 0; j < height; ++j) {
    if (cancelled && cancelled())
      return failure(HdriDecodeError::CANCELLED);
    if (bytes.size() - offset < 4)
      return failure(HdriDecodeError::MALFORMED);
    const bool rle = width >= 8 && width < 32768 && bytes[offset] == 2
        && bytes[offset + 1] == 2 && !(bytes[offset + 2] & 128);
    if (rle) {
      if (size_t(bytes[offset + 2]) * 256 + bytes[offset + 3] != width)
        return failure(HdriDecodeError::MALFORMED);
      offset += 4;
      for (size_t channel = 0; channel < 4; ++channel) {
        size_t i = 0;
        while (i < width) {
          if (offset == bytes.size())
            return failure(HdriDecodeError::MALFORMED);
          const auto code = bytes[offset++];
          const size_t count = code > 128 ? code - 128 : code;
          if (!count || count > width - i)
            return failure(HdriDecodeError::MALFORMED);
          if (code > 128) {
            if (offset == bytes.size())
              return failure(HdriDecodeError::MALFORMED);
            const auto value = bytes[offset++];
            for (size_t k = 0; k < count; ++k)
              row[(i + k) * 4 + channel] = value;
          } else {
            if (bytes.size() - offset < count)
              return failure(HdriDecodeError::MALFORMED);
            for (size_t k = 0; k < count; ++k)
              row[(i + k) * 4 + channel] = bytes[offset++];
          }
          i += count;
        }
      }
    } else {
      if (bytes.size() - offset < width * 4)
        return failure(HdriDecodeError::MALFORMED);
      for (size_t i = 0; i < width; ++i) {
        const auto *pixel = bytes.data() + offset + i * 4;
        if (pixel[0] == 1 && pixel[1] == 1 && pixel[2] == 1)
          return failure(HdriDecodeError::UNSUPPORTED);
      }
      std::copy_n(bytes.data() + offset, row.size(), row.data());
      offset += row.size();
    }
    for (size_t i = 0; i < width; ++i)
      result.image.radiance[(height - j - 1) * width + i] =
          linearRgb(row.data() + i * 4);
  }
  if (offset != bytes.size())
    return failure(HdriDecodeError::MALFORMED);
  return result;
}
} // namespace

HdriDecodeResult decodeHdri(const std::vector<std::uint8_t> &bytes,
    const HdriDecodeLimits &limits,
    const std::function<bool()> &cancelled,
    std::shared_ptr<CapacityArena> arena)
{
  return decodeBytes(bytes, limits, cancelled, std::move(arena));
}

HdriDecodeResult decodeHdri(const AssetRoots::Bytes &bytes,
    const HdriDecodeLimits &limits,
    const std::function<bool()> &cancelled,
    std::shared_ptr<CapacityArena> arena)
{
  return decodeBytes(bytes, limits, cancelled, std::move(arena));
}

} // namespace vsr::integration::content

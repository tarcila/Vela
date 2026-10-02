// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace vsr::integration::image {

/*
 * Normalization of an Image Pipeline color buffer to the integration's
 * result format: an in-memory PNG, 8-bit RGBA, sRGB-encoded RGB, straight
 * alpha, top-left origin (x rightward, y downward).
 *
 * The pipeline owns exposure and tone mapping; nothing here tone maps.
 * Mapping rules:
 *   - Linear color: clamp to [0,1] (NaN -> 0), sRGB transfer function, then
 *     round(value * 255) with exact halves rounding up.
 *   - Alpha: clamp to [0,1] (NaN -> 0), quantize by the same rule.
 *   - An 8-bit sRGB straight-alpha source passes through byte-for-byte.
 *   - Premultiplied sources are unpremultiplied in linear space (8-bit sRGB
 *     input is decoded first); RGB is 0 wherever alpha is 0.
 *
 * The returned image owns its bytes: the source buffer may be reused or
 * released as soon as normalizeToPng() returns.
 *
 * Example:
 *   SourceImage src;
 *   src.width = w; src.height = h;
 *   src.encoding = ChannelEncoding::LINEAR_FLOAT;
 *   src.alpha = AlphaMode::PREMULTIPLIED;
 *   src.origin = RowOrigin::BOTTOM_LEFT; // ANARI frame convention
 *   src.pixels = mappedFrame;
 *   auto r = normalizeToPng(src);
 *   if (r.ok()) send(r.image.png);
 */

enum class ChannelEncoding
{
  LINEAR_FLOAT, // 4 x float32 per pixel, linear display-referred RGB + alpha
  SRGB_UINT8 // 4 x uint8 per pixel, sRGB-encoded RGB + linear alpha
};

enum class AlphaMode
{
  STRAIGHT,
  PREMULTIPLIED
};

enum class RowOrigin
{
  TOP_LEFT, // first row in memory is the top row
  BOTTOM_LEFT // first row in memory is the bottom row
};

struct SourceImage
{
  std::uint32_t width{0};
  std::uint32_t height{0};
  ChannelEncoding encoding{ChannelEncoding::LINEAR_FLOAT};
  AlphaMode alpha{AlphaMode::STRAIGHT};
  RowOrigin origin{RowOrigin::TOP_LEFT};
  const void *pixels{nullptr}; // tightly packed interleaved RGBA
};

struct NormalizedImage
{
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::vector<std::uint8_t> png; // complete PNG file bytes
};

enum class NormalizeError
{
  NONE,
  INVALID_DIMENSIONS, // zero width/height or byte size overflow
  NO_PIXELS
};

struct NormalizeResult
{
  NormalizeError error{NormalizeError::NONE};
  NormalizedImage image;

  bool ok() const
  {
    return error == NormalizeError::NONE;
  }
};

// Color mapping primitives ///////////////////////////////////////////////////

// Clamp to [0,1]; NaN maps to 0.
inline double clampUnit(double v)
{
  if (!(v > 0.0)) // also catches NaN
    return 0.0;
  return v < 1.0 ? v : 1.0;
}

// round(v * 255) with exact halves rounding up, after clampUnit().
inline std::uint8_t quantizeUnit(double v)
{
  const double scaled = clampUnit(v) * 255.0;
  const double q = std::floor(scaled + 0.5);
  return static_cast<std::uint8_t>(q > 255.0 ? 255.0 : q);
}

inline double srgbEncode(double linear)
{
  return linear <= 0.0031308 ? linear * 12.92
                             : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}

inline double srgbDecode(double encoded)
{
  return encoded <= 0.04045 ? encoded / 12.92
                            : std::pow((encoded + 0.055) / 1.055, 2.4);
}

// Linear color channel -> 8-bit sRGB (clamp, encode, quantize).
inline std::uint8_t linearToSrgb8(double linear)
{
  return quantizeUnit(srgbEncode(clampUnit(linear)));
}

// Normalization to RGBA8 /////////////////////////////////////////////////////

namespace detail {

inline bool pixelCount(const SourceImage &src, std::size_t &count)
{
  if (src.width == 0 || src.height == 0)
    return false;
  const std::size_t w = src.width;
  const std::size_t h = src.height;
  const std::size_t maxBytes = std::numeric_limits<std::size_t>::max() / 16;
  if (w > maxBytes / h)
    return false;
  count = w * h;
  return true;
}

// Straight linear RGBA (alpha already clamped) -> sRGB straight RGBA8.
inline void writeStraight(
    std::uint8_t *out, double r, double g, double b, double a)
{
  out[0] = linearToSrgb8(r);
  out[1] = linearToSrgb8(g);
  out[2] = linearToSrgb8(b);
  out[3] = quantizeUnit(a);
}

// Premultiplied linear RGB with clamped alpha -> straight sRGB RGBA8.
inline void writeUnpremultiplied(
    std::uint8_t *out, double r, double g, double b, double a)
{
  if (a <= 0.0) {
    out[0] = out[1] = out[2] = out[3] = 0;
    return;
  }
  // Clamp premultiplied color first so NaN/negative cannot survive division;
  // the result is clamped again by linearToSrgb8().
  writeStraight(out, clampUnit(r) / a, clampUnit(g) / a, clampUnit(b) / a, a);
}

inline void convertPixel(
    const SourceImage &src, std::size_t index, std::uint8_t *out)
{
  if (src.encoding == ChannelEncoding::SRGB_UINT8) {
    const auto *in = static_cast<const std::uint8_t *>(src.pixels) + index * 4;
    if (src.alpha == AlphaMode::STRAIGHT) {
      std::memcpy(out, in, 4);
      return;
    }
    const double a = in[3] / 255.0;
    writeUnpremultiplied(out,
        srgbDecode(in[0] / 255.0),
        srgbDecode(in[1] / 255.0),
        srgbDecode(in[2] / 255.0),
        a);
    return;
  }

  const auto *in = static_cast<const float *>(src.pixels) + index * 4;
  const double a = clampUnit(in[3]);
  if (src.alpha == AlphaMode::STRAIGHT)
    writeStraight(out, in[0], in[1], in[2], a);
  else
    writeUnpremultiplied(out, in[0], in[1], in[2], a);
}

} // namespace detail

// Returns top-left-origin straight sRGB RGBA8, or empty on invalid input.
inline std::vector<std::uint8_t> normalizeToRgba8(const SourceImage &src)
{
  std::size_t count = 0;
  if (!src.pixels || !detail::pixelCount(src, count))
    return {};

  std::vector<std::uint8_t> out(count * 4);
  const std::size_t w = src.width;
  const std::size_t h = src.height;
  for (std::size_t y = 0; y < h; ++y) {
    const std::size_t srcRow =
        src.origin == RowOrigin::TOP_LEFT ? y : (h - 1 - y);
    for (std::size_t x = 0; x < w; ++x)
      detail::convertPixel(src, srcRow * w + x, out.data() + (y * w + x) * 4);
  }
  return out;
}

// PNG encoding ///////////////////////////////////////////////////////////////

// Encodes top-left-origin RGBA8 rows as a PNG (color type 6, bit depth 8)
// with deflate-compressed image data. An sRGB chunk marks the RGB channels
// as sRGB-encoded. Output is byte-deterministic for a given input. Returns
// an empty vector when the image is too large to encode.
std::vector<std::uint8_t> encodePngRgba8(
    std::uint32_t width, std::uint32_t height, const std::uint8_t *rgba);

// Full normalization: source buffer -> owned PNG bytes.
inline NormalizeResult normalizeToPng(const SourceImage &src)
{
  NormalizeResult result;
  std::size_t count = 0;
  if (!detail::pixelCount(src, count)) {
    result.error = NormalizeError::INVALID_DIMENSIONS;
    return result;
  }
  if (!src.pixels) {
    result.error = NormalizeError::NO_PIXELS;
    return result;
  }
  const auto rgba = normalizeToRgba8(src);
  auto png = encodePngRgba8(src.width, src.height, rgba.data());
  if (png.empty()) {
    result.error = NormalizeError::INVALID_DIMENSIONS;
    return result;
  }
  result.image.width = src.width;
  result.image.height = src.height;
  result.image.png = std::move(png);
  return result;
}

} // namespace vsr::integration::image

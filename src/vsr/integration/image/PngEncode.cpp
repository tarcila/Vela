// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ImageNormalization.hpp"
// std
#include <array>
#include <cstdlib>
#include <limits>
#include <memory>

// Implemented by the vendored stb writer (vela_ext_stb_image). Declared here
// rather than through stbi_write_png_to_mem(), which honours the process-wide
// stbi_flip_vertically_on_write() flag other code toggles and omits sRGB.
// The returned buffer is allocated with malloc().
extern "C" unsigned char *stbi_zlib_compress(
    unsigned char *data, int data_len, int *out_len, int quality);

namespace vsr::integration::image {

namespace {

constexpr int ZLIB_QUALITY = 8; // stb's default PNG level

std::uint32_t crc32(
    const std::uint8_t *data, std::size_t size, std::uint32_t crc = 0)
{
  static const auto table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t n = 0; n < 256; ++n) {
      std::uint32_t c = n;
      for (int k = 0; k < 8; ++k)
        c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[n] = c;
    }
    return t;
  }();
  crc = ~crc;
  for (std::size_t i = 0; i < size; ++i)
    crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
  return ~crc;
}

void putU32(std::vector<std::uint8_t> &out, std::uint32_t v)
{
  out.push_back(std::uint8_t(v >> 24));
  out.push_back(std::uint8_t(v >> 16));
  out.push_back(std::uint8_t(v >> 8));
  out.push_back(std::uint8_t(v));
}

void putChunk(std::vector<std::uint8_t> &out,
    const char type[4],
    const std::uint8_t *data,
    std::size_t size)
{
  putU32(out, std::uint32_t(size));
  const std::size_t typeAt = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data, data + size);
  putU32(out, crc32(out.data() + typeAt, 4 + size));
}

std::uint8_t paeth(int a, int b, int c)
{
  const int p = a + b - c;
  const int pa = std::abs(p - a);
  const int pb = std::abs(p - b);
  const int pc = std::abs(p - c);
  if (pa <= pb && pa <= pc)
    return std::uint8_t(a);
  return std::uint8_t(pb <= pc ? b : c);
}

// Every row uses the Paeth filter: fixed choice keeps output deterministic
// and suits the smooth content renders produce.
std::vector<std::uint8_t> filterRows(
    std::uint32_t width, std::uint32_t height, const std::uint8_t *rgba)
{
  const std::size_t rowBytes = std::size_t(width) * 4;
  std::vector<std::uint8_t> raw((rowBytes + 1) * height);
  for (std::size_t y = 0; y < height; ++y) {
    const std::uint8_t *row = rgba + y * rowBytes;
    const std::uint8_t *up = y > 0 ? row - rowBytes : nullptr;
    std::uint8_t *out = raw.data() + y * (rowBytes + 1);
    *out++ = 4; // filter: Paeth
    for (std::size_t i = 0; i < rowBytes; ++i) {
      const int a = i >= 4 ? row[i - 4] : 0;
      const int b = up ? up[i] : 0;
      const int c = (up && i >= 4) ? up[i - 4] : 0;
      out[i] = std::uint8_t(row[i] - paeth(a, b, c));
    }
  }
  return raw;
}

} // namespace

std::vector<std::uint8_t> encodePngRgba8(
    std::uint32_t width, std::uint32_t height, const std::uint8_t *rgba)
{
  const std::size_t rowBytes = std::size_t(width) * 4;
  if (width == 0 || height == 0 || !rgba
      || (rowBytes + 1) > std::size_t(std::numeric_limits<int>::max()) / height)
    return {};

  auto raw = filterRows(width, height, rgba);
  int zlen = 0;
  // Owned exception-safely: the vector growth below may throw.
  std::unique_ptr<unsigned char, void (*)(void *)> z(
      stbi_zlib_compress(raw.data(), int(raw.size()), &zlen, ZLIB_QUALITY),
      std::free);
  if (!z)
    return {};

  std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  png.reserve(png.size() + 25 + 13 + 12 + std::size_t(zlen) + 12);

  std::vector<std::uint8_t> ihdr;
  putU32(ihdr, width);
  putU32(ihdr, height);
  ihdr.push_back(8); // bit depth
  ihdr.push_back(6); // color type: RGBA
  ihdr.push_back(0); // compression
  ihdr.push_back(0); // filter
  ihdr.push_back(0); // interlace
  putChunk(png, "IHDR", ihdr.data(), ihdr.size());
  const std::uint8_t srgb = 0; // perceptual rendering intent
  putChunk(png, "sRGB", &srgb, 1);
  putChunk(png, "IDAT", z.get(), std::size_t(zlen));
  putChunk(png, "IEND", nullptr, 0);
  return png;
}

} // namespace vsr::integration::image

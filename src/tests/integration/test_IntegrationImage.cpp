// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/integration/image/ImageNormalization.hpp"
// Independent PNG decoder for the oracle. Private to this TU (static) and
// included by path so the test needs no shared CMake change.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../../../external/vela_stb_image/stb_image.h"
// std
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace vsr::integration::image;

namespace {

struct Decoded
{
  int width{0};
  int height{0};
  int channels{0};
  std::vector<std::uint8_t> rgba;
};

Decoded decodePng(const std::vector<std::uint8_t> &png)
{
  Decoded d;
  unsigned char *pixels = stbi_load_from_memory(
      png.data(), int(png.size()), &d.width, &d.height, &d.channels, 4);
  REQUIRE(pixels != nullptr);
  d.rgba.assign(pixels, pixels + std::size_t(d.width) * d.height * 4);
  stbi_image_free(pixels);
  return d;
}

std::array<std::uint8_t, 4> pixelAt(const Decoded &d, int x, int y)
{
  const auto *p = d.rgba.data() + (std::size_t(y) * d.width + x) * 4;
  return {p[0], p[1], p[2], p[3]};
}

// Normalize a single float pixel and return its decoded RGBA8.
std::array<std::uint8_t, 4> oneFloatPixel(
    float r, float g, float b, float a, AlphaMode mode = AlphaMode::STRAIGHT)
{
  const float px[4] = {r, g, b, a};
  SourceImage src;
  src.width = 1;
  src.height = 1;
  src.encoding = ChannelEncoding::LINEAR_FLOAT;
  src.alpha = mode;
  src.pixels = px;
  auto res = normalizeToPng(src);
  REQUIRE(res.ok());
  return pixelAt(decodePng(res.image.png), 0, 0);
}

std::array<std::uint8_t, 4> oneSrgbPixel(std::uint8_t r,
    std::uint8_t g,
    std::uint8_t b,
    std::uint8_t a,
    AlphaMode mode)
{
  const std::uint8_t px[4] = {r, g, b, a};
  SourceImage src;
  src.width = 1;
  src.height = 1;
  src.encoding = ChannelEncoding::SRGB_UINT8;
  src.alpha = mode;
  src.pixels = px;
  auto res = normalizeToPng(src);
  REQUIRE(res.ok());
  return pixelAt(decodePng(res.image.png), 0, 0);
}

// Reference sRGB encode for the oracle, independent of the implementation.
int refSrgb8(double linear)
{
  const double e = linear <= 0.0031308
      ? 12.92 * linear
      : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
  return int(std::floor(e * 255.0 + 0.5));
}

double refDecode(int v)
{
  const double e = v / 255.0;
  return e <= 0.04045 ? e / 12.92 : std::pow((e + 0.055) / 1.055, 2.4);
}

} // namespace

TEST_CASE("PNG result is RGBA8 with top-left origin", "[integration][image]")
{
  // 3x2 asymmetric image, distinct pixel per position (8-bit passthrough).
  // Bottom-left-origin source: first memory row is the bottom row.
  const std::uint8_t bottomUp[2][3][4] = {
      {{10, 0, 0, 255}, {20, 0, 0, 255}, {30, 0, 0, 255}}, // bottom row
      {{0, 40, 0, 255}, {0, 50, 0, 255}, {0, 60, 0, 128}}, // top row
  };
  SourceImage src;
  src.width = 3;
  src.height = 2;
  src.encoding = ChannelEncoding::SRGB_UINT8;
  src.alpha = AlphaMode::STRAIGHT;

  SECTION("bottom-left source is flipped to top-left output")
  {
    src.origin = RowOrigin::BOTTOM_LEFT;
    src.pixels = bottomUp;
    auto res = normalizeToPng(src);
    REQUIRE(res.ok());
    CHECK(res.image.width == 3);
    CHECK(res.image.height == 2);
    auto d = decodePng(res.image.png);
    CHECK(d.width == 3);
    CHECK(d.height == 2);
    CHECK(d.channels == 4);
    // y downward: row 0 is the top row.
    CHECK(pixelAt(d, 0, 0) == std::array<std::uint8_t, 4>{0, 40, 0, 255});
    CHECK(pixelAt(d, 2, 0) == std::array<std::uint8_t, 4>{0, 60, 0, 128});
    CHECK(pixelAt(d, 0, 1) == std::array<std::uint8_t, 4>{10, 0, 0, 255});
    CHECK(pixelAt(d, 2, 1) == std::array<std::uint8_t, 4>{30, 0, 0, 255});
  }

  SECTION("top-left source keeps row order")
  {
    src.origin = RowOrigin::TOP_LEFT;
    src.pixels = bottomUp;
    auto d = decodePng(normalizeToPng(src).image.png);
    CHECK(pixelAt(d, 0, 0) == std::array<std::uint8_t, 4>{10, 0, 0, 255});
    CHECK(pixelAt(d, 1, 1) == std::array<std::uint8_t, 4>{0, 50, 0, 255});
  }

  SECTION("PNG header declares 8-bit RGBA and sRGB")
  {
    src.pixels = bottomUp;
    const auto png = normalizeToPng(src).image.png;
    REQUIRE(png.size() > 33);
    CHECK(png[0] == 0x89);
    CHECK(png[1] == 'P');
    CHECK(png[24] == 8); // bit depth
    CHECK(png[25] == 6); // color type RGBA
    const std::string body(png.begin(), png.end());
    CHECK(body.find("sRGB") != std::string::npos);
  }
}

TEST_CASE("Invalid sources are rejected", "[integration][image]")
{
  const float px[4] = {0, 0, 0, 1};
  SourceImage src;
  src.pixels = px;
  src.width = 0;
  src.height = 1;
  CHECK(normalizeToPng(src).error == NormalizeError::INVALID_DIMENSIONS);
  src.width = 1;
  src.height = 0;
  CHECK(normalizeToPng(src).error == NormalizeError::INVALID_DIMENSIONS);
  src.height = 1;
  src.pixels = nullptr;
  CHECK(normalizeToPng(src).error == NormalizeError::NO_PIXELS);
}

TEST_CASE("Float linear color is clamped, sRGB-encoded and rounded",
    "[integration][image]")
{
  SECTION("out-of-range and NaN")
  {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(oneFloatPixel(-1.f, 2.f, nan, 1.f)
        == std::array<std::uint8_t, 4>{0, 255, 0, 255});
    CHECK(oneFloatPixel(inf, -inf, 1.f, 1.f)
        == std::array<std::uint8_t, 4>{255, 0, 255, 255});
  }

  SECTION("linear mid-gray goes through the sRGB transfer function")
  {
    const auto p = oneFloatPixel(0.5f, 0.5f, 0.5f, 1.f);
    CHECK(int(p[0]) == refSrgb8(0.5));
    CHECK(int(p[0]) == 188); // not 128: encoded, not linear
    CHECK(p[1] == p[0]);
    CHECK(p[2] == p[0]);
    // Linear segment near black.
    CHECK(int(oneFloatPixel(0.001f, 0, 0, 1)[0]) == refSrgb8(0.001));
  }

  SECTION("exact halves round up")
  {
    // Encoded values landing exactly on k + 0.5 after scaling.
    CHECK(quantizeUnit(0.5 / 255.0) == 1);
    CHECK(quantizeUnit(127.5 / 255.0) == 128);
    CHECK(quantizeUnit(254.5 / 255.0) == 255);
    CHECK(quantizeUnit(127.49 / 255.0) == 127);
  }

  SECTION("encode/decode primitives agree with reference")
  {
    for (int v = 0; v <= 255; ++v)
      CHECK(int(linearToSrgb8(refDecode(v))) == v);
  }
}

TEST_CASE("Alpha is clamped and quantized", "[integration][image]")
{
  const float nan = std::numeric_limits<float>::quiet_NaN();
  CHECK(oneFloatPixel(1, 1, 1, nan)[3] == 0);
  CHECK(oneFloatPixel(1, 1, 1, -0.5f)[3] == 0);
  CHECK(oneFloatPixel(1, 1, 1, 7.f)[3] == 255);
  // Quantization midpoint: 0.5 * 255 = 127.5 -> 128 (halves round up).
  CHECK(oneFloatPixel(1, 1, 1, 0.5f)[3] == 128);
  CHECK(oneFloatPixel(1, 1, 1, 0.25f)[3] == 64); // 63.75 -> 64
  CHECK(oneFloatPixel(1, 1, 1, 0.1f)[3] == 26); // 25.5 (+eps) -> 26
  // Straight alpha leaves color alone.
  CHECK(oneFloatPixel(0.5f, 0, 0, 0.25f)[0] == 188);
}

TEST_CASE("8-bit sRGB straight-alpha source passes through unchanged",
    "[integration][image]")
{
  std::vector<std::uint8_t> px;
  for (int i = 0; i < 256; ++i) {
    px.push_back(std::uint8_t(i));
    px.push_back(std::uint8_t(255 - i));
    px.push_back(std::uint8_t(i * 7));
    px.push_back(std::uint8_t(i * 13)); // alpha included, incl. 0 with RGB
  }
  SourceImage src;
  src.width = 16;
  src.height = 16;
  src.encoding = ChannelEncoding::SRGB_UINT8;
  src.alpha = AlphaMode::STRAIGHT;
  src.pixels = px.data();
  auto d = decodePng(normalizeToPng(src).image.png);
  CHECK(d.rgba == px);
}

TEST_CASE("Premultiplied sources become straight alpha in linear space",
    "[integration][image]")
{
  SECTION("float premultiplied")
  {
    // Straight linear 0.5 at alpha 0.5 is premultiplied 0.25.
    const auto p =
        oneFloatPixel(0.25f, 0.5f, 0.f, 0.5f, AlphaMode::PREMULTIPLIED);
    CHECK(int(p[0]) == refSrgb8(0.5));
    CHECK(p[1] == 255);
    CHECK(p[2] == 0);
    CHECK(p[3] == 128);
  }

  SECTION("RGB is 0 where alpha is 0")
  {
    CHECK(oneFloatPixel(0.7f, 0.2f, 1.f, 0.f, AlphaMode::PREMULTIPLIED)
        == std::array<std::uint8_t, 4>{0, 0, 0, 0});
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(oneFloatPixel(0.7f, 0.2f, 1.f, nan, AlphaMode::PREMULTIPLIED)
        == std::array<std::uint8_t, 4>{0, 0, 0, 0});
    CHECK(oneSrgbPixel(200, 100, 50, 0, AlphaMode::PREMULTIPLIED)
        == std::array<std::uint8_t, 4>{0, 0, 0, 0});
  }

  SECTION("8-bit sRGB premultiplied is decoded before unpremultiplying")
  {
    // Premultiplied linear 0.25 at alpha 128/255; sRGB of 0.25 is 137.
    const std::uint8_t enc = std::uint8_t(refSrgb8(0.25));
    const auto p = oneSrgbPixel(enc, enc, 0, 128, AlphaMode::PREMULTIPLIED);
    const double straight = refDecode(enc) / (128 / 255.0);
    CHECK(int(p[0]) == refSrgb8(straight));
    CHECK(p[3] == 128);
    // Unpremultiplying in encoded space would give round(137/0.502)=255+;
    // linear space gives ~0.498 -> ~187.
    CHECK(int(p[0]) >= 185);
    CHECK(int(p[0]) <= 190);
    CHECK(p[2] == 0);
  }

  SECTION("opaque premultiplied equals straight")
  {
    CHECK(oneSrgbPixel(10, 128, 250, 255, AlphaMode::PREMULTIPLIED)
        == std::array<std::uint8_t, 4>{10, 128, 250, 255});
  }
}

TEST_CASE("Result owns its bytes independently of the source buffer",
    "[integration][image]")
{
  NormalizeResult res;
  {
    auto buf = std::make_unique<float[]>(2 * 2 * 4);
    for (int i = 0; i < 16; ++i)
      buf[i] = 1.f;
    SourceImage src;
    src.width = 2;
    src.height = 2;
    src.encoding = ChannelEncoding::LINEAR_FLOAT;
    src.pixels = buf.get();
    res = normalizeToPng(src);
    // Reuse the live buffer, then release it.
    for (int i = 0; i < 16; ++i)
      buf[i] = 0.f;
  }
  REQUIRE(res.ok());
  auto d = decodePng(res.image.png);
  for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 2; ++x)
      CHECK(
          pixelAt(d, x, y) == std::array<std::uint8_t, 4>{255, 255, 255, 255});
}

TEST_CASE("PNG image data is deflate-compressed and deterministic",
    "[integration][image]")
{
  // 1024x1024 smooth float gradient, bottom-left origin like an ANARI frame.
  const std::uint32_t n = 1024;
  std::vector<float> px(std::size_t(n) * n * 4);
  for (std::uint32_t y = 0; y < n; ++y)
    for (std::uint32_t x = 0; x < n; ++x) {
      float *p = px.data() + (std::size_t(y) * n + x) * 4;
      p[0] = float(x) / (n - 1);
      p[1] = float(y) / (n - 1);
      p[2] = 0.5f * (p[0] + p[1]);
      p[3] = 1.f;
    }
  SourceImage src;
  src.width = n;
  src.height = n;
  src.encoding = ChannelEncoding::LINEAR_FLOAT;
  src.origin = RowOrigin::BOTTOM_LEFT;
  src.pixels = px.data();

  auto first = normalizeToPng(src);
  REQUIRE(first.ok());
  const std::size_t raw = std::size_t(n) * n * 4;
  // Stored deflate blocks would be slightly above raw (4 MiB).
  CHECK(first.image.png.size() < raw / 4);

  auto second = normalizeToPng(src);
  REQUIRE(second.ok());
  CHECK(first.image.png == second.image.png);

  // Compression is lossless: the decoded image matches the RGBA8 mapping.
  auto d = decodePng(first.image.png);
  REQUIRE(d.width == int(n));
  REQUIRE(d.height == int(n));
  CHECK(d.rgba == normalizeToRgba8(src));
}

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_integration
#include "vsr/integration/image/PngImport.hpp"
// zlib
#include <zlib.h>
// std
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace vsr::integration::image {
namespace {
constexpr std::size_t ARENA_BYTES = 128u << 10;
std::uint32_t be32(const std::uint8_t *p)
{
  return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16)
      | (std::uint32_t(p[2]) << 8) | p[3];
}
struct InflateArena
{
  explicit InflateArena(std::pmr::memory_resource *allocator)
      : bytes(ARENA_BYTES / sizeof(std::max_align_t),
            std::max_align_t{},
            allocator)
  {}
  std::pmr::vector<std::max_align_t> bytes;
  std::size_t used{0};
  static void *allocate(void *self, unsigned count, unsigned size);
  static void release(void *, void *) {}
};
void *InflateArena::allocate(void *self, unsigned count, unsigned size)
{
  auto &arena = *static_cast<InflateArena *>(self);
  const auto alignment = alignof(std::max_align_t);
  const auto start = (arena.used + alignment - 1) & ~(alignment - 1);
  if (start > ARENA_BYTES || (size && count > (ARENA_BYTES - start) / size))
    return nullptr;
  arena.used = start + std::size_t(count) * size;
  return reinterpret_cast<std::uint8_t *>(arena.bytes.data()) + start;
}
struct Inflater
{
  z_stream stream{};
  bool initialized{false};
  ~Inflater();
};
Inflater::~Inflater()
{
  if (initialized)
    inflateEnd(&stream);
}
int paeth(int a, int b, int c)
{
  const int p = a + b - c;
  const auto x = std::abs(p - a), y = std::abs(p - b), z = std::abs(p - c);
  return x <= y && x <= z ? a : y <= z ? b : c;
}
} // namespace

DecodeResult decodePng(const ImageBytes &encoded,
    std::size_t width,
    std::size_t height,
    std::size_t channels,
    ImageBytes &decoded,
    const std::function<bool()> &abandoned)
{
  constexpr auto MAX = std::numeric_limits<std::size_t>::max();
  if (!width || !height || !channels || channels > 4 || width > UINT32_MAX
      || height > UINT32_MAX || width > MAX / height
      || width * height > MAX / channels)
    return DecodeResult::RESOURCE_LIMIT;
  const auto texels = width * height * channels;
  if (texels > (MAX - 1048576) / 4 || height > (MAX - 1048576) / 4 - texels)
    return DecodeResult::RESOURCE_LIMIT;
  const auto filteredSize = texels + height;
  const auto encodedLimit =
      std::min<std::size_t>(67108864, 2 * filteredSize + 65536);
  constexpr std::uint8_t SIGNATURE[] = {137, 80, 78, 71, 13, 10, 26, 10};
  if (encoded.size() < 57 || encoded.size() > encodedLimit
      || std::memcmp(encoded.data(), SIGNATURE, 8))
    return DecodeResult::INVALID_PNG;
  bool header = false, idat = false, idatEnded = false, end = false,
       palette = false;
  std::size_t ancillary = 0;
  for (std::size_t offset = 8; offset < encoded.size();) {
    if (abandoned && abandoned())
      return DecodeResult::CANCELLED;
    if (encoded.size() - offset < 12)
      return DecodeResult::INVALID_PNG;
    const auto length = be32(encoded.data() + offset);
    if (length > encoded.size() - offset - 12)
      return DecodeResult::INVALID_PNG;
    const auto *type = encoded.data() + offset + 4;
    const auto *payload = type + 4;
    for (int i = 0; i < 4; ++i)
      if (!((type[i] >= 'A' && type[i] <= 'Z')
              || (type[i] >= 'a' && type[i] <= 'z')))
        return DecodeResult::INVALID_PNG;
    if (type[2] & 32)
      return DecodeResult::INVALID_PNG;
    uLong checksum = crc32(0, Z_NULL, 0);
    for (std::size_t i = 0; i < std::size_t(length) + 4;) {
      if (abandoned && abandoned())
        return DecodeResult::CANCELLED;
      const auto count = std::min<std::size_t>(65536, length + 4u - i);
      checksum = crc32(checksum, type + i, uInt(count));
      i += count;
    }
    if (checksum != be32(payload + length))
      return DecodeResult::INVALID_PNG;
    const auto is = [&](const char *name) {
      return std::memcmp(type, name, 4) == 0;
    };
    if (!header && !is("IHDR"))
      return DecodeResult::INVALID_PNG;
    if (is("IHDR")) {
      constexpr std::uint8_t TYPES[] = {0, 0, 4, 2, 6};
      if (header || length != 13 || be32(payload) != width
          || be32(payload + 4) != height || payload[8] != 8
          || payload[9] != TYPES[channels] || payload[10] || payload[11]
          || payload[12])
        return DecodeResult::INVALID_PNG;
      header = true;
    } else if (is("IDAT")) {
      if (idatEnded)
        return DecodeResult::INVALID_PNG;
      idat = true;
    } else if (is("IEND")) {
      if (!idat || length || offset + 12 != encoded.size())
        return DecodeResult::INVALID_PNG;
      end = true;
    } else {
      if (idat)
        idatEnded = true;
      if (is("tRNS") || is("acTL") || is("fcTL") || is("fdAT"))
        return DecodeResult::INVALID_PNG;
      if (is("PLTE")) {
        if (palette || idat || channels < 3 || !length || length > 768
            || length % 3)
          return DecodeResult::INVALID_PNG;
        palette = true;
      } else if (!(type[0] & 32))
        return DecodeResult::INVALID_PNG;
      else {
        if (length > 65536 - ancillary)
          return DecodeResult::INVALID_PNG;
        ancillary += length;
      }
    }
    offset += std::size_t(length) + 12;
  }
  if (!end)
    return DecodeResult::INVALID_PNG;
  try {
    auto *allocator = decoded.get_allocator().resource();
    ImageBytes filtered(filteredSize + 1, 0, allocator);
    InflateArena arena(allocator);
    Inflater inflater;
    auto &stream = inflater.stream;
    stream.opaque = &arena;
    stream.zalloc = InflateArena::allocate;
    stream.zfree = InflateArena::release;
    const auto initialized = inflateInit(&stream);
    if (initialized != Z_OK)
      return initialized == Z_MEM_ERROR ? DecodeResult::RESOURCE_LIMIT
                                        : DecodeResult::INVALID_PNG;
    inflater.initialized = true;
    std::size_t produced = 0;
    bool finished = false;
    for (std::size_t offset = 8; offset < encoded.size();) {
      const auto length = be32(encoded.data() + offset);
      if (!std::memcmp(encoded.data() + offset + 4, "IDAT", 4)) {
        if (finished && length)
          return DecodeResult::INVALID_PNG;
        stream.next_in = const_cast<Bytef *>(encoded.data() + offset + 8);
        stream.avail_in = length;
        while (stream.avail_in) {
          if (abandoned && abandoned())
            return DecodeResult::CANCELLED;
          stream.next_out = filtered.data() + produced;
          stream.avail_out =
              uInt(std::min<std::size_t>(65536, filtered.size() - produced));
          const auto input = stream.avail_in, output = stream.avail_out;
          const auto status = inflate(&stream, Z_NO_FLUSH);
          produced += output - stream.avail_out;
          if (produced > filteredSize || status == Z_DATA_ERROR
              || status == Z_NEED_DICT || status == Z_STREAM_ERROR
              || status == Z_BUF_ERROR)
            return DecodeResult::INVALID_PNG;
          if (status == Z_MEM_ERROR)
            return DecodeResult::RESOURCE_LIMIT;
          if (status == Z_STREAM_END) {
            if (stream.avail_in)
              return DecodeResult::INVALID_PNG;
            finished = true;
            break;
          }
          if (input == stream.avail_in && output == stream.avail_out)
            return DecodeResult::INVALID_PNG;
        }
      }
      offset += std::size_t(length) + 12;
    }
    if (!finished || produced != filteredSize)
      return DecodeResult::INVALID_PNG;
    decoded.resize(texels);
    const auto stride = width * channels;
    for (std::size_t row = 0; row < height; ++row) {
      const auto filter = filtered[row * (stride + 1)];
      if (filter > 4)
        return DecodeResult::INVALID_PNG;
      for (std::size_t column = 0; column < stride; ++column) {
        if (column % 65536 == 0 && abandoned && abandoned())
          return DecodeResult::CANCELLED;
        const auto index = row * stride + column;
        const int a = column >= channels ? decoded[index - channels] : 0;
        const int b = row ? decoded[index - stride] : 0;
        const int c =
            row && column >= channels ? decoded[index - stride - channels] : 0;
        const int predictor = filter == 1 ? a
            : filter == 2                 ? b
            : filter == 3                 ? (a + b) / 2
            : filter == 4                 ? paeth(a, b, c)
                                          : 0;
        decoded[index] =
            std::uint8_t(filtered[row * (stride + 1) + column + 1] + predictor);
      }
    }
    return DecodeResult::OK;
  } catch (const std::bad_alloc &) {
    return DecodeResult::RESOURCE_LIMIT;
  }
}

} // namespace vsr::integration::image

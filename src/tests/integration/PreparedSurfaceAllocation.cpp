// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_integration
#include "vsr/integration/content/detail/PreparedSurfaceGraph.hpp"
// std
#include <cstdlib>
#include <iostream>
#include <new>

namespace {
int allocationCountdown = -1;

std::pmr::vector<std::uint8_t> input()
{
  const std::string header =
      R"({"revision":1,"byte_order":"little","arrays":{"p":{"element_type":"ANARI_FLOAT32_VEC3","count":3,"byte_length":36}},"fields":{"vertex.position":{"action":"preserve","payload":"p","enabled":true,"association":"vertex"}}})";
  std::pmr::vector<std::uint8_t> bytes{'V', 'M', 'E', 'S', 'H', '0', '1', '\n'};
  for (unsigned i = 0; i < 4; ++i)
    bytes.push_back((header.size() >> (i * 8)) & 255);
  bytes.insert(bytes.end(), header.begin(), header.end());
  const float points[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  const auto *data = reinterpret_cast<const std::uint8_t *>(points);
  bytes.insert(bytes.end(), data, data + sizeof(points));
  return bytes;
}
} // namespace

void *operator new(std::size_t size)
{
  if (allocationCountdown == 0)
    throw std::bad_alloc();
  if (allocationCountdown > 0)
    --allocationCountdown;
  if (auto *p = std::malloc(size ? size : 1))
    return p;
  throw std::bad_alloc();
}

void *operator new[](std::size_t size)
{
  return ::operator new(size);
}

void operator delete(void *p) noexcept
{
  std::free(p);
}

void operator delete[](void *p) noexcept
{
  std::free(p);
}

void operator delete(void *p, std::size_t) noexcept
{
  std::free(p);
}

void operator delete[](void *p, std::size_t) noexcept
{
  std::free(p);
}

int main()
{
  using namespace vsr;
  using namespace integration::content::detail;
  scene::Scene live;
  const auto revision = live.dependencyRevision();
  int failures = 0, successes = 0;
  for (int allocation = 0; allocation < 256; ++allocation) {
    std::unique_ptr<PreparedSurfaceGraph> prepared;
    if (prepareSurfaceGraph(input(), "Prior", {}, prepared))
      return 1;
    const auto *prior = prepared.get();
    auto bytes = input();
    bool failed = false;
    try {
      allocationCountdown = allocation;
      const auto *reason =
          prepareSurfaceGraph(std::move(bytes), "Candidate", {}, prepared);
      allocationCountdown = -1;
      if (reason)
        return 2;
      ++successes;
    } catch (const std::bad_alloc &) {
      allocationCountdown = -1;
      ++failures;
      failed = true;
    }
    if (failed
        && (prepared.get() != prior
            || prepared->root()->value().name() != "Prior"))
      return 3;
    if (!failed && prepared->root()->value().name() != "Candidate")
      return 4;
    if (live.dependencyRevision() != revision
        || live.numberOfObjects(ANARI_SURFACE)
        || live.numberOfObjects(ANARI_ARRAY)
        || live.numberOfObjects(ANARI_GEOMETRY))
      return 5;
  }
  if (!failures || !successes)
    return 6;
  std::cout << "Private Surface ordinary allocation sweep: " << failures
            << " failures, " << successes << " abandoned successes\n";
}

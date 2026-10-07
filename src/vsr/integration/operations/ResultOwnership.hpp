// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <functional>
#include <memory>
#include <new>
#include <utility>

namespace vsr::integration::operations {

/* A bounded payload's reservation follows its owner, not its producer.
 * Place this member before the covered storage so destruction frees storage
 * first. Value copies acquire independent coverage before copying payloads.
 */
struct ResultOwnership
{
  using Reserve = std::function<std::shared_ptr<const void>()>;
  ResultOwnership() = default;
  explicit ResultOwnership(Reserve reserve);
  ResultOwnership(const ResultOwnership &other);
  ResultOwnership &operator=(const ResultOwnership &other);
  ResultOwnership(ResultOwnership &&) noexcept = default;
  ResultOwnership &operator=(ResultOwnership &&) noexcept = default;

  Reserve reserve;
  std::shared_ptr<const void> lease;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline ResultOwnership::ResultOwnership(Reserve acquire)
    : reserve(std::move(acquire)), lease(reserve ? reserve() : nullptr)
{
  if (reserve && !lease)
    throw std::bad_alloc();
}

inline ResultOwnership::ResultOwnership(const ResultOwnership &other)
    : ResultOwnership(other.reserve)
{}

inline ResultOwnership &ResultOwnership::operator=(const ResultOwnership &other)
{
  if (this != &other) {
    ResultOwnership copy(other);
    *this = std::move(copy);
  }
  return *this;
}

} // namespace vsr::integration::operations

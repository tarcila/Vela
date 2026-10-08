// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/scene/Object.hpp"
// std
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>

namespace vsr::scene {

struct Array;
namespace detail {
class PreparedImageWrite;
}

/* Non-owning, relocation-safe identity. Retirement clears the pointer even
 * when a caller temporarily holds a locked weak reference. */
struct ArrayLifetime
{
  Array *array{nullptr};
};

/* Owning-thread write admission clock shared by tracked host Arrays in one
 * host launch. A smaller ceiling tests exhaustion, never wraps the clock.
 * Reservations are burned; publication belongs to Array. */
class ArrayWriteSequence
{
 public:
  explicit ArrayWriteSequence(std::uint64_t ceiling = UINT64_MAX);
  std::optional<std::uint64_t> reserve();
  bool isOwningThread() const;
  bool isEnabled() const;
  void stopWrites();

 private:
  std::thread::id m_owner;
  std::uint64_t m_ceiling;
  std::uint64_t m_last{0};
  bool m_enabled{true};
};

/*
 * Fixed-size typed data buffer that wraps host memory, CUDA device memory, or
 * a proxy placeholder; maps to an ANARI array object and supports 1-3
 * dimensions.
 *
 * Example:
 *   auto arr = scene.createArray(ANARI_FLOAT32_VEC3, 1024);
 *   auto *ptr = arr->mapAs<float3>();
 *   // fill ptr[0..1023] ...
 *   arr->unmap();
 *
 * Mutations, maps, unmaps and deletion belong to the Scene's owning thread,
 * serialized with integration safe-point transactions. Nested maps and an
 * unmap without a successful map are API misuse. Retaining a map across a
 * safe point is permitted; that transaction must observe isMapped() and must
 * never unmap another writer's map. data() is read-only: raw-pointer writes
 * outside map/unmap and arbitrary worker-thread writes are unsupported.
 */
struct Array : public Object
{
  // clang-format off
  enum class MemoryKind {
    HOST, // Memory allocated on the host (main memory)
    CUDA, // Memory allocated on the GPU (device memory)
    PROXY // No memory allocated, only a placeholder object (not mappable)
  };
  // clang-format on

  Array(
      anari::DataType type, size_t items0, MemoryKind kind = MemoryKind::HOST);
  Array(anari::DataType type,
      size_t items0,
      size_t items1,
      MemoryKind kind = MemoryKind::HOST);
  Array(anari::DataType type,
      size_t items0,
      size_t items1,
      size_t items2,
      MemoryKind kind = MemoryKind::HOST);

  Array() = default;
  ~Array() override;

  size_t size() const;
  size_t elementSize() const;
  anari::DataType elementType() const;
  size_t dim(size_t d) const;
  bool isEmpty() const;

  MemoryKind kind() const;
  bool isHost() const;
  bool isCUDA() const;
  bool isProxy() const;
  bool isMapped() const;
  bool isImage() const;
  bool supportsHostWriteTracking() const;
  std::weak_ptr<ArrayLifetime> lifetime() const;
  std::optional<std::uint64_t> dataWriteSequence() const;
  // Historical API name; enrolls both supported Images and host mesh Arrays.
  bool trackImageWrites(const std::shared_ptr<ArrayWriteSequence> &sequence);

  void convertProxyToHost();

  void *map();
  template <typename T>
  T *mapAs();
  void unmap();

  const void *data() const;
  template <typename T>
  const T *dataAs() const;

  const void *elementAt(size_t i) const;

  template <typename T>
  void setData(const T *data, size_t size, size_t startOffset = 0);
  template <typename T>
  void setData(const std::vector<T> &data, size_t startOffset = 0);
  void setData(const void *data, size_t byteOffset = 0);
  size_t setData(std::FILE *stream);

  ObjectPoolRef<Array> self() const;

  anari::Object makeANARIObject(anari::Device d) const override;

  // Movable, not copyable
  VSR_NOT_COPYABLE(Array)
  Array(Array &&);
  Array &operator=(Array &&);

 private:
  friend class detail::PreparedImageWrite;
  Array(anari::DataType arrayType,
      anari::DataType type,
      size_t items0,
      size_t items1,
      size_t items2,
      MemoryKind kind);

  void *m_data{nullptr};
  void *m_dataUsageDescriptor{nullptr};
  MemoryKind m_kind{MemoryKind::HOST};
  anari::DataType m_elementType{ANARI_UNKNOWN};
  size_t m_dim0{0};
  size_t m_dim1{0};
  size_t m_dim2{0};
  bool m_mapped{false};
  std::shared_ptr<ArrayLifetime> m_lifetime;
  std::shared_ptr<ArrayWriteSequence> m_writeSequence;
  std::uint64_t m_completedWrite{0};
  std::uint64_t m_reservedWrite{0};
};

using ArrayRef = ObjectPoolRef<Array>;

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T>
inline T *Array::mapAs()
{
  assert(sizeof(T) == anari::sizeOf(elementType()));
  return reinterpret_cast<T *>(map());
}

template <typename T>
inline const T *Array::dataAs() const
{
  assert(sizeof(T) == anari::sizeOf(elementType()));
  return reinterpret_cast<const T *>(data());
}

template <typename T>
inline void Array::setData(const T *data, size_t size, size_t startOffset)
{
  if (startOffset > this->size() || size > this->size() - startOffset
      || (size && !data))
    throw std::runtime_error("Array::setData() - invalid range");
  auto *d = mapAs<T>();
  if (!d)
    return;
  std::memcpy(d + startOffset, data, size * sizeof(T));
  unmap();
}

template <typename T>
inline void Array::setData(const std::vector<T> &data, size_t startOffset)
{
  setData(data.data(), data.size(), startOffset);
}

} // namespace vsr::scene
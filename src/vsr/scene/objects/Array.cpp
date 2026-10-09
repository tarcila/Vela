// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef VSR_USE_CUDA
#define VSR_USE_CUDA 1
#endif

#include "vsr/scene/objects/Array.hpp"

#include "vsr/core/Logging.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <atomic>
#include <limits>
#include <stdexcept>
#if VSR_USE_CUDA
// cuda
#include <cuda_runtime.h>
#endif

namespace vsr::scene {

// Helper types + functions ///////////////////////////////////////////////////

struct ArrayUsageDescriptor
{
  std::atomic<size_t> numReferences{1};
};

static bool isDevicePointer(const void *ptr)
{
#if VSR_USE_CUDA
  cudaPointerAttributes attr;
  cudaError_t err = cudaPointerGetAttributes(&attr, ptr);

  // If the call fails, it's likely not a CUDA-recognized pointer
  if (err != cudaSuccess) {
    cudaGetLastError(); // clear error state
    return false;
  }

  return (attr.type == cudaMemoryTypeDevice);
#else
  return false;
#endif
}

static void vsrDeleter(const void *_usrPtr, const void *_mem)
{
  if (!_mem || !_usrPtr)
    return;

  auto *descriptor =
      static_cast<ArrayUsageDescriptor *>(const_cast<void *>(_usrPtr));
  auto *mem = const_cast<void *>(_mem);

  auto result = descriptor->numReferences.fetch_sub(1);

  if (result == 1) {
#if VSR_USE_CUDA
    if (isDevicePointer(mem))
      cudaFree(mem);
    else
#endif
      std::free(mem);

    delete descriptor;
  }
}

ArrayWriteSequence::ArrayWriteSequence(std::uint64_t ceiling)
    : m_owner(std::this_thread::get_id()), m_ceiling(ceiling)
{}

std::optional<std::uint64_t> ArrayWriteSequence::reserve()
{
  if (!isOwningThread() || !m_enabled || m_last == m_ceiling)
    return {};
  return ++m_last;
}

bool ArrayWriteSequence::isOwningThread() const
{
  return m_owner == std::this_thread::get_id();
}

bool ArrayWriteSequence::isEnabled() const
{
  return m_enabled;
}

void ArrayWriteSequence::stopWrites()
{
  if (!isOwningThread())
    throw std::runtime_error("ArrayWriteSequence - wrong owning thread");
  m_enabled = false;
}

// Array definitions //////////////////////////////////////////////////////////

Array::Array(anari::DataType type, size_t items0, Array::MemoryKind kind)
    : Array(ANARI_ARRAY1D, type, items0, 1, 1, kind)
{}

Array::Array(
    anari::DataType type, size_t items0, size_t items1, Array::MemoryKind kind)
    : Array(ANARI_ARRAY2D, type, items0, items1, 1, kind)
{}

Array::Array(anari::DataType type,
    size_t items0,
    size_t items1,
    size_t items2,
    Array::MemoryKind kind)
    : Array(ANARI_ARRAY3D, type, items0, items1, items2, kind)
{}

Array::~Array()
{
  if (m_lifetime)
    m_lifetime->array = nullptr;
  vsrDeleter(m_dataUsageDescriptor, m_data);
}

size_t Array::size() const
{
  return dim(0) * dim(1) * dim(2);
}

size_t Array::elementSize() const
{
  return anari::sizeOf(m_elementType);
}

anari::DataType Array::elementType() const
{
  return m_elementType;
}

size_t Array::dim(size_t d) const
{
  if (d == 0)
    return m_dim0;
  else if (d == 1)
    return m_dim1;
  else if (d == 2)
    return m_dim2;

  return 0;
}

bool Array::isEmpty() const
{
  return size() == 0;
}

Array::MemoryKind Array::kind() const
{
  return m_kind;
}

bool Array::isHost() const
{
  return kind() == MemoryKind::HOST;
}

bool Array::isCUDA() const
{
  return kind() == MemoryKind::CUDA;
}

bool Array::isProxy() const
{
  return kind() == MemoryKind::PROXY;
}

bool Array::isMapped() const
{
  return m_mapped;
}

bool Array::isImage() const
{
  const auto t = elementType();
  const bool fixed = (t >= ANARI_FIXED8 && t <= ANARI_UFIXED8_VEC4)
      || (t >= ANARI_UFIXED8_R_SRGB && t <= ANARI_UFIXED8_RGBA_SRGB);
  return isHost() && type() == ANARI_ARRAY2D && dim(0) && dim(1) && fixed
      && data();
}

bool Array::supportsHostWriteTracking() const
{
  // The SDK's scalar/vector interval also includes FIXED64, UFIXED64 and
  // FLOAT16. Tracking is write evidence, not an exchange eligibility test:
  // MeshCapture validates its narrower profile; Image callers require isImage.
  return isImage()
      || (isHost() && type() == ANARI_ARRAY2D && data()
          && elementType() == ANARI_FLOAT32_VEC3)
      || (isHost() && type() == ANARI_ARRAY1D && data()
          && elementType() >= ANARI_INT8
          && elementType() <= ANARI_FLOAT64_VEC4);
}

std::weak_ptr<ArrayLifetime> Array::lifetime() const
{
  return m_lifetime;
}

std::optional<std::uint64_t> Array::dataWriteSequence() const
{
  return m_writeSequence ? std::optional<std::uint64_t>(m_completedWrite)
                         : std::optional<std::uint64_t>{};
}

bool Array::trackImageWrites(
    const std::shared_ptr<ArrayWriteSequence> &sequence)
{
  // Enroll HDRI radiance and host numeric mesh storage as well as the
  // historical Image whitelist. Tracking is not exchange eligibility.
  if (!supportsHostWriteTracking())
    return true;
  if (m_writeSequence == sequence)
    return true;
  if (!sequence || m_writeSequence || m_mapped || !sequence->isOwningThread())
    return false;
  m_writeSequence = sequence;
  return true;
}

void Array::convertProxyToHost()
{
  if (kind() != MemoryKind::PROXY) {
    logWarning(
        "Array::convertProxyToHost() - array is not PROXY, no action taken");
    return;
  }

  if (scene() && scene()->imageWriteSequence()
      && !scene()->imageWriteSequence()->isOwningThread())
    throw std::runtime_error("Array::convertProxyToHost() - wrong thread");
  auto descriptor = std::make_unique<ArrayUsageDescriptor>();
  auto *data = std::malloc(size() * elementSize());
  if (!data && size() && elementSize())
    throw std::bad_alloc();
  m_kind = MemoryKind::HOST;
  m_data = data;
  m_dataUsageDescriptor = descriptor.release();
  if (scene() && scene()->imageWriteSequence())
    trackImageWrites(scene()->imageWriteSequence());
}

void *Array::map()
{
  if (kind() == MemoryKind::PROXY) {
    logError("Array::map() - cannot map PROXY arrays");
    return nullptr;
  }

  if (m_mapped)
    throw std::runtime_error("Array::map() - nested mapping is unsupported");
  if (!m_data)
    return nullptr;
  if (m_writeSequence) {
    const auto reserved = m_writeSequence->reserve();
    if (!reserved)
      throw std::runtime_error("Array::map() - Array write admission refused");
    m_reservedWrite = *reserved;
  }
  if (auto *ud = updateDelegate(); ud != nullptr)
    ud->signalArrayMapped(this);
  m_mapped = true;
  return m_data;
}

const void *Array::data() const
{
  return m_data;
}

const void *Array::elementAt(size_t i) const
{
  if (kind() == MemoryKind::PROXY) {
    logError("Array::elementAt() - cannot access PROXY arrays locally");
    return nullptr;
  } else if (i >= size()) {
    logWarning("Array::elementAt() - index out of bounds");
    return nullptr;
  }
  auto *bytes = static_cast<const uint8_t *>(m_data);
  return bytes + i * elementSize();
}

void Array::unmap()
{
  if (kind() == MemoryKind::PROXY) {
    logError("Array::unmap() - cannot unmap PROXY arrays");
    return;
  }

  if (!m_mapped)
    throw std::runtime_error("Array::unmap() - array is not mapped");
  if (m_writeSequence && !m_writeSequence->isOwningThread())
    throw std::runtime_error("Array::unmap() - wrong owning thread");
  m_mapped = false;
  if (m_writeSequence) {
    m_completedWrite = m_reservedWrite;
    m_reservedWrite = 0;
  }
  if (auto *ud = updateDelegate(); ud != nullptr)
    ud->signalArrayUnmapped(this);
}

void Array::setData(const void *data, size_t byteOffset)
{
  if (kind() == MemoryKind::PROXY) {
    logError("Array::setData() - cannot set data on PROXY arrays");
    return;
  }

  if (!data && size())
    throw std::runtime_error("Array::setData() - null source");
  auto *bytes = static_cast<const uint8_t *>(data);
  auto *destination = map();
  if (!destination)
    return;
  std::memcpy(destination, bytes + byteOffset, size() * elementSize());
  unmap();
}

size_t Array::setData(std::FILE *stream)
{
  if (kind() == MemoryKind::PROXY) {
    logError("Array::setData() - cannot set data on PROXY arrays");
    return 0;
  } else if (!stream) {
    return 0;
  }

  auto *destination = map();
  if (!destination)
    return 0;
  auto r = std::fread(destination, elementSize(), size(), stream);
  unmap();
  return r;
}

ObjectPoolRef<Array> Array::self() const
{
  return scene() ? scene()->getObject<Array>(index()) : ObjectPoolRef<Array>{};
}

anari::Object Array::makeANARIObject(anari::Device d) const
{
  if (elementType() == ANARI_UNKNOWN || isEmpty()) {
    logError(
        "Array::makeANARIObject() - cannot create ANARI object for empty or"
        " unknown element type array");
    return nullptr;
  } else if (kind() == MemoryKind::PROXY) {
    logError(
        "Array::makeANARIObject() - cannot create ANARI object"
        " for PROXY array");
    return nullptr;
  }

  anari::Object retval = nullptr;

  const void *ptr = anari::isObject(elementType()) ? nullptr : m_data;
  switch (type()) {
  case ANARI_ARRAY1D:
    retval = anari::newArray1D(
        d, ptr, vsrDeleter, m_dataUsageDescriptor, elementType(), dim(0));
    break;
  case ANARI_ARRAY2D:
    retval = anari::newArray2D(d,
        ptr,
        vsrDeleter,
        m_dataUsageDescriptor,
        elementType(),
        dim(0),
        dim(1));
    break;
  case ANARI_ARRAY3D:
    retval = anari::newArray3D(d,
        ptr,
        vsrDeleter,
        m_dataUsageDescriptor,
        elementType(),
        dim(0),
        dim(1),
        dim(2));
    break;
  default:
    break;
  }

  auto *descriptor = static_cast<ArrayUsageDescriptor *>(m_dataUsageDescriptor);
  if (descriptor)
    descriptor->numReferences++;

  assert(retval != nullptr);
  return retval;
}

Array::Array(Array &&o) : Object(std::move(static_cast<Object &&>(o)))
{
  m_data = o.m_data;
  m_dataUsageDescriptor = o.m_dataUsageDescriptor;
  m_kind = o.m_kind;
  m_elementType = o.m_elementType;
  m_dim0 = o.m_dim0;
  m_dim1 = o.m_dim1;
  m_dim2 = o.m_dim2;
  m_mapped = o.m_mapped;
  m_lifetime = std::move(o.m_lifetime);
  if (m_lifetime)
    m_lifetime->array = this;
  m_writeSequence = std::move(o.m_writeSequence);
  m_completedWrite = o.m_completedWrite;
  m_reservedWrite = o.m_reservedWrite;
  o.m_mapped = false;
  o.m_reservedWrite = 0;
  o.m_data = nullptr;
  o.m_dataUsageDescriptor = nullptr;
}

Array &Array::operator=(Array &&o)
{
  if (this != &o) {
    if (m_writeSequence && o.type() != ANARI_UNKNOWN)
      throw std::runtime_error("Array - cannot replace a live tracked Image");
    if (m_lifetime)
      m_lifetime->array = nullptr;
    vsrDeleter(m_dataUsageDescriptor, m_data);
    *static_cast<Object *>(this) = std::move(*static_cast<Object *>(&o));
    m_data = o.m_data;
    m_dataUsageDescriptor = o.m_dataUsageDescriptor;
    m_kind = o.m_kind;
    m_elementType = o.m_elementType;
    m_dim0 = o.m_dim0;
    m_dim1 = o.m_dim1;
    m_dim2 = o.m_dim2;
    m_mapped = o.m_mapped;
    m_lifetime = std::move(o.m_lifetime);
    if (m_lifetime)
      m_lifetime->array = this;
    m_writeSequence = std::move(o.m_writeSequence);
    m_completedWrite = o.m_completedWrite;
    m_reservedWrite = o.m_reservedWrite;
    o.m_mapped = false;
    o.m_reservedWrite = 0;
    o.m_data = nullptr;
    o.m_dataUsageDescriptor = nullptr;
  }
  return *this;
}

Array::Array(anari::DataType arrayType,
    anari::DataType type,
    size_t items0,
    size_t items1,
    size_t items2,
    MemoryKind kind)
    : Object(arrayType),
      m_kind(kind),
      m_elementType(type),
      m_dim0(items0),
      m_dim1(items1),
      m_dim2(items2)
{
  m_lifetime = std::make_shared<ArrayLifetime>();
  m_lifetime->array = this;
  if (anari::isObject(type) && kind == MemoryKind::CUDA)
    throw std::runtime_error("cannot create CUDA arrays of objects!");

  constexpr auto MAX_SIZE = std::numeric_limits<size_t>::max();
  if ((items1 && items0 > MAX_SIZE / items1)
      || (items2 && items0 * items1 > MAX_SIZE / items2)
      || (elementSize() && size() > MAX_SIZE / elementSize()))
    throw std::runtime_error("Array - storage size overflow");

  if (isEmpty()) {
    logWarning("%s of %s elements created with 0 size",
        anari::toString(this->type()),
        anari::toString(this->elementType()));
    return;
  }

  auto descriptor = kind == MemoryKind::PROXY
      ? std::unique_ptr<ArrayUsageDescriptor>{}
      : std::make_unique<ArrayUsageDescriptor>();
  if (kind == MemoryKind::PROXY) {
    m_data = nullptr;
  } else if (kind == MemoryKind::CUDA) {
#if VSR_USE_CUDA
    cudaMalloc(&m_data, size() * elementSize());
#else
    throw std::runtime_error("CUDA support not enabled!");
#endif
  } else { // MemoryKind::HOST
    m_data = std::malloc(size() * elementSize());
    if (!m_data && elementSize())
      throw std::bad_alloc();
  }

  m_dataUsageDescriptor = descriptor.release();
}

} // namespace vsr::scene

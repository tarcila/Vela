// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// anari
#include <anari/backend/DeviceImpl.h>
#include <anari/backend/LibraryImpl.h>
// std
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {
/* A loadable ANARI boundary fixture. Device parameters test.metadata select
 * full/missing/incomplete/ambiguous/unsupported metadata. test.map selects
 * null/wrong-type/wrong-size maps, and test.type overrides custom pixel types.
 * Environment VSR_CHANNEL_TEST_METADATA supplies the subprocess equivalent.
 * Other objects are deliberately inert; frames own deterministic sample data.
 */
struct TestObject
{
  unsigned refs{1};
  std::string subtype;
  TestObject *renderer{nullptr};
  uint32_t width{2};
  uint32_t height{2};
  uint32_t renders{0};
  std::vector<float> pixels;
  std::vector<uint32_t> integerPixels;
  std::vector<uint8_t> bytePixels;
  std::map<std::string, ANARIDataType> requested;
  std::vector<std::string> mapped;
};

struct TestDevice : anari::DeviceImpl
{
  TestDevice(ANARILibrary library);
  ANARIArray1D newArray1D(const void *,
      ANARIMemoryDeleter,
      const void *,
      ANARIDataType,
      uint64_t) override;
  ANARIArray2D newArray2D(const void *,
      ANARIMemoryDeleter,
      const void *,
      ANARIDataType,
      uint64_t,
      uint64_t) override;
  ANARIArray3D newArray3D(const void *,
      ANARIMemoryDeleter,
      const void *,
      ANARIDataType,
      uint64_t,
      uint64_t,
      uint64_t) override;
  ANARIGeometry newGeometry(const char *) override;
  ANARIMaterial newMaterial(const char *) override;
  ANARISampler newSampler(const char *) override;
  ANARISurface newSurface() override;
  ANARISpatialField newSpatialField(const char *) override;
  ANARIVolume newVolume(const char *) override;
  ANARILight newLight(const char *) override;
  ANARIGroup newGroup() override;
  ANARIInstance newInstance(const char *) override;
  ANARIWorld newWorld() override;
  ANARICamera newCamera(const char *) override;
  ANARIRenderer newRenderer(const char *) override;
  ANARIFrame newFrame() override;
  void setParameter(
      ANARIObject, const char *, ANARIDataType, const void *) override;
  void unsetParameter(ANARIObject, const char *) override;
  void unsetAllParameters(ANARIObject) override;
  void *mapParameterArray1D(
      ANARIObject, const char *, ANARIDataType, uint64_t, uint64_t *) override;
  void *mapParameterArray2D(ANARIObject,
      const char *,
      ANARIDataType,
      uint64_t,
      uint64_t,
      uint64_t *) override;
  void *mapParameterArray3D(ANARIObject,
      const char *,
      ANARIDataType,
      uint64_t,
      uint64_t,
      uint64_t,
      uint64_t *) override;
  void unmapParameterArray(ANARIObject, const char *) override;
  void commitParameters(ANARIObject) override;
  void release(ANARIObject) override;
  void retain(ANARIObject) override;
  void *mapArray(ANARIArray) override;
  void unmapArray(ANARIArray) override;
  int getProperty(ANARIObject,
      const char *,
      ANARIDataType,
      void *,
      uint64_t,
      ANARIWaitMask) override;
  const char **getObjectSubtypes(ANARIDataType) override;
  const void *getObjectInfo(
      ANARIDataType, const char *, const char *, ANARIDataType) override;
  const void *getParameterInfo(ANARIDataType,
      const char *,
      const char *,
      ANARIDataType,
      const char *,
      ANARIDataType) override;
  const void *frameBufferMap(ANARIFrame,
      const char *,
      uint32_t *,
      uint32_t *,
      ANARIDataType *) override;
  void frameBufferUnmap(ANARIFrame, const char *) override;
  void renderFrame(ANARIFrame) override;
  int frameReady(ANARIFrame, ANARIWaitMask) override;
  void discardFrame(ANARIFrame) override;

 private:
  template <typename T>
  T makeObject();
  std::string m_metadata{"full"};
  std::string m_map;
  std::string m_rendererMetadata;
  ANARIDataType m_type{ANARI_UNKNOWN};
  bool m_pending{false};
  std::vector<float> m_samples;
  std::vector<uint32_t> m_integerSamples;
  unsigned m_refs{1};
  unsigned m_renderCount{0};
  std::vector<std::unique_ptr<TestObject>> m_objects;
  std::vector<ANARIDataType> m_types;
};

TestDevice::TestDevice(ANARILibrary library) : anari::DeviceImpl(library)
{
  if (const char *mode = std::getenv("VSR_CHANNEL_TEST_METADATA"))
    m_metadata = mode;
  if (const char *mode = std::getenv("VSR_CHANNEL_TEST_MAP"))
    m_map = mode;
  if (const char *vector = std::getenv("VSR_CHANNEL_TEST_VECTOR")) {
    if (std::strcmp(vector, "vec3") == 0)
      m_type = ANARI_FLOAT32_VEC3;
    if (std::strcmp(vector, "vec4") == 0)
      m_type = ANARI_FLOAT32_VEC4;
  }
  if (const char *scalar = std::getenv("VSR_CHANNEL_TEST_SCALAR")) {
    if (std::strcmp(scalar, "uint32") == 0)
      m_integerSamples = {10, 20, 30, 40};
  }
}

template <typename T>
T TestDevice::makeObject()
{
  auto object = std::make_unique<TestObject>();
  auto handle = reinterpret_cast<T>(object.get());
  m_objects.push_back(std::move(object));
  return handle;
}

ANARIArray1D TestDevice::newArray1D(
    const void *, ANARIMemoryDeleter, const void *, ANARIDataType, uint64_t)
{
  return makeObject<ANARIArray1D>();
}
ANARIArray2D TestDevice::newArray2D(const void *,
    ANARIMemoryDeleter,
    const void *,
    ANARIDataType,
    uint64_t,
    uint64_t)
{
  return makeObject<ANARIArray2D>();
}
ANARIArray3D TestDevice::newArray3D(const void *,
    ANARIMemoryDeleter,
    const void *,
    ANARIDataType,
    uint64_t,
    uint64_t,
    uint64_t)
{
  return makeObject<ANARIArray3D>();
}
ANARIGeometry TestDevice::newGeometry(const char *)
{
  return makeObject<ANARIGeometry>();
}
ANARIMaterial TestDevice::newMaterial(const char *)
{
  return makeObject<ANARIMaterial>();
}
ANARISampler TestDevice::newSampler(const char *)
{
  return makeObject<ANARISampler>();
}
ANARISurface TestDevice::newSurface()
{
  return makeObject<ANARISurface>();
}
ANARISpatialField TestDevice::newSpatialField(const char *)
{
  return makeObject<ANARISpatialField>();
}
ANARIVolume TestDevice::newVolume(const char *)
{
  return makeObject<ANARIVolume>();
}
ANARILight TestDevice::newLight(const char *)
{
  return makeObject<ANARILight>();
}
ANARIGroup TestDevice::newGroup()
{
  return makeObject<ANARIGroup>();
}
ANARIInstance TestDevice::newInstance(const char *)
{
  return makeObject<ANARIInstance>();
}
ANARIWorld TestDevice::newWorld()
{
  return makeObject<ANARIWorld>();
}
ANARICamera TestDevice::newCamera(const char *)
{
  return makeObject<ANARICamera>();
}
ANARIRenderer TestDevice::newRenderer(const char *name)
{
  if (const char *required = std::getenv("VSR_CHANNEL_TEST_RENDERER")) {
    if (std::strcmp(name, required) != 0)
      return nullptr;
  }
  const std::string subtype(name);
  if (subtype != "default" && subtype != "diagnostic" && subtype != "broken"
      && subtype != "reordered" && subtype != "limited" && subtype != "opaque")
    return nullptr;
  auto renderer = makeObject<ANARIRenderer>();
  reinterpret_cast<TestObject *>(renderer)->subtype = subtype;
  return renderer;
}
ANARIFrame TestDevice::newFrame()
{
  return makeObject<ANARIFrame>();
}

void TestDevice::setParameter(
    ANARIObject object, const char *name, ANARIDataType type, const void *mem)
{
  if (handleIsDevice(object)) {
    if (type == ANARI_STRING && std::strcmp(name, "test.metadata") == 0)
      m_metadata = static_cast<const char *>(mem);
    if (type == ANARI_STRING && std::strcmp(name, "test.map") == 0)
      m_map = static_cast<const char *>(mem);
    if (type == ANARI_DATA_TYPE && std::strcmp(name, "test.type") == 0)
      m_type = *static_cast<const ANARIDataType *>(mem);
    if (type == ANARI_BOOL && std::strcmp(name, "test.pending") == 0)
      m_pending = *static_cast<const bool *>(mem);
    if (type == ANARI_FLOAT32_VEC4 && std::strcmp(name, "test.samples") == 0)
      m_samples.assign(
          static_cast<const float *>(mem), static_cast<const float *>(mem) + 4);
    if (type == ANARI_UINT32_VEC4
        && std::strcmp(name, "test.integerSamples") == 0)
      m_integerSamples.assign(static_cast<const uint32_t *>(mem),
          static_cast<const uint32_t *>(mem) + 4);
  } else if (type == ANARI_RENDERER && std::strcmp(name, "renderer") == 0) {
    auto *renderer = reinterpret_cast<TestObject *>(
        *static_cast<const ANARIRenderer *>(mem));
    reinterpret_cast<TestObject *>(object)->renderer = renderer;
    const auto subtype = renderer ? renderer->subtype : std::string{};
    m_rendererMetadata = subtype == "limited" ? "depth"
        : subtype == "reordered"              ? "reordered"
        : subtype == "opaque"                 ? "opaque"
                                              : "";
  } else if (type == ANARI_DATA_TYPE) {
    reinterpret_cast<TestObject *>(object)->requested[name] =
        *static_cast<const ANARIDataType *>(mem);
  } else if (type == ANARI_UINT32_VEC2 && std::strcmp(name, "size") == 0) {
    auto &o = *reinterpret_cast<TestObject *>(object);
    o.width = static_cast<const uint32_t *>(mem)[0];
    o.height = static_cast<const uint32_t *>(mem)[1];
  }
}
void TestDevice::unsetParameter(ANARIObject object, const char *name)
{
  if (!handleIsDevice(object))
    reinterpret_cast<TestObject *>(object)->requested.erase(name);
}
void TestDevice::unsetAllParameters(ANARIObject) {}
void *TestDevice::mapParameterArray1D(
    ANARIObject, const char *, ANARIDataType, uint64_t, uint64_t *)
{
  return nullptr;
}
void *TestDevice::mapParameterArray2D(
    ANARIObject, const char *, ANARIDataType, uint64_t, uint64_t, uint64_t *)
{
  return nullptr;
}
void *TestDevice::mapParameterArray3D(ANARIObject,
    const char *,
    ANARIDataType,
    uint64_t,
    uint64_t,
    uint64_t,
    uint64_t *)
{
  return nullptr;
}
void TestDevice::unmapParameterArray(ANARIObject, const char *) {}
void TestDevice::commitParameters(ANARIObject) {}
void TestDevice::release(ANARIObject object)
{
  if (handleIsDevice(object)) {
    if (--m_refs == 0) {
      std::unique_ptr<TestDevice> owner(this);
    }
  } else if (object) {
    auto *o = reinterpret_cast<TestObject *>(object);
    if (--o->refs == 0) {
      for (auto it = m_objects.begin(); it != m_objects.end(); ++it) {
        if (it->get() == o) {
          m_objects.erase(it);
          break;
        }
      }
    }
  }
}
void TestDevice::retain(ANARIObject object)
{
  if (handleIsDevice(object))
    ++m_refs;
  else if (object)
    ++reinterpret_cast<TestObject *>(object)->refs;
}
void *TestDevice::mapArray(ANARIArray)
{
  return nullptr;
}
void TestDevice::unmapArray(ANARIArray) {}
int TestDevice::getProperty(ANARIObject object,
    const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    ANARIWaitMask)
{
  if (handleIsDevice(object))
    return 0;
  const auto &o = *reinterpret_cast<TestObject *>(object);
  if (type == ANARI_UINT32 && size >= sizeof(uint32_t)
      && std::strcmp(name, "test.renders") == 0) {
    *static_cast<uint32_t *>(mem) = o.renders;
    return 1;
  }
  if (type != ANARI_BOOL || size < sizeof(bool))
    return 0;
  const std::string property(name);
  if (property.compare(0, 15, "test.requested.") == 0) {
    *static_cast<bool *>(mem) = o.requested.count(property.substr(15)) != 0;
    return 1;
  }
  if (property.compare(0, 12, "test.mapped.") == 0) {
    *static_cast<bool *>(mem) =
        std::find(o.mapped.begin(), o.mapped.end(), property.substr(12))
        != o.mapped.end();
    return 1;
  }
  return 0;
}
const char **TestDevice::getObjectSubtypes(ANARIDataType)
{
  static const char *names[] = {"default",
      "diagnostic",
      "broken",
      "reordered",
      "limited",
      "opaque",
      nullptr};
  return names;
}
const void *TestDevice::getObjectInfo(
    ANARIDataType type, const char *, const char *name, ANARIDataType infoType)
{
  static const char *extensions[] = {
      "ANARI_KHR_FRAME_CHANNEL_OBJECT_ID", nullptr};
  static const char *identityExtensions[] = {
      "ANARI_KHR_FRAME_CHANNEL_OBJECT_ID",
      "ANARI_KHR_FRAME_CHANNEL_PRIMITIVE_ID",
      "ANARI_KHR_FRAME_CHANNEL_INSTANCE_ID",
      nullptr};
  static const ANARIParameter identities[] = {
      {"channel.objectId", ANARI_DATA_TYPE},
      {"channel.primitiveId", ANARI_DATA_TYPE},
      {"channel.instanceId", ANARI_DATA_TYPE},
      {"channel.color", ANARI_DATA_TYPE},
      {nullptr, ANARI_UNKNOWN}};
  static const ANARIParameter full[] = {
      {"channel.motionVectors", ANARI_DATA_TYPE},
      {"Temperature_RAW", ANARI_DATA_TYPE},
      {"channel.channel.Case", ANARI_DATA_TYPE},
      {"channel.motionVectors", ANARI_DATA_TYPE},
      {"channel.unusual", ANARI_DATA_TYPE},
      {"channel.color", ANARI_DATA_TYPE},
      {"channel.normal", ANARI_DATA_TYPE},
      {"channel.albedo", ANARI_DATA_TYPE},
      {nullptr, ANARI_UNKNOWN}};
  static const ANARIParameter reordered[] = {
      {"channel.albedo", ANARI_DATA_TYPE},
      {"channel.color", ANARI_DATA_TYPE},
      {"Temperature_RAW", ANARI_DATA_TYPE},
      {"channel.motionVectors", ANARI_DATA_TYPE},
      {"channel.normal", ANARI_DATA_TYPE},
      {nullptr, ANARI_UNKNOWN}};
  static const ANARIParameter incomplete[] = {
      {"channel.depth", ANARI_DATA_TYPE},
      {"Temperature_RAW", ANARI_DATA_TYPE},
      {nullptr, ANARI_UNKNOWN}};
  static const ANARIParameter ambiguous[] = {{"channel.Case", ANARI_DATA_TYPE},
      {"Case", ANARI_DATA_TYPE},
      {nullptr, ANARI_UNKNOWN}};
  static const ANARIParameter unsupported[] = {
      {"channel.color", ANARI_DATA_TYPE},
      {"channel.depth", ANARI_DATA_TYPE},
      {"channel.objectId", ANARI_DATA_TYPE},
      {nullptr, ANARI_UNKNOWN}};
  const auto &metadata =
      m_rendererMetadata.empty() ? m_metadata : m_rendererMetadata;
  if (metadata == "reordered" && type == ANARI_FRAME
      && infoType == ANARI_PARAMETER_LIST
      && std::strcmp(name, "parameter") == 0)
    return reordered;
  if (type == ANARI_DEVICE && infoType == ANARI_STRING_LIST
      && std::strcmp(name, "extension") == 0)
    return m_metadata == "identities" ? identityExtensions : extensions;
  if (type != ANARI_FRAME || infoType != ANARI_PARAMETER_LIST
      || std::strcmp(name, "parameter") != 0 || m_metadata == "missing")
    return nullptr;
  if (m_metadata == "identities")
    return identities;
  if (metadata == "incomplete" || metadata == "depth")
    return incomplete;
  if (m_metadata == "ambiguous")
    return ambiguous;
  if (m_metadata == "unsupported")
    return unsupported;
  return full;
}
const void *TestDevice::getParameterInfo(ANARIDataType type,
    const char *,
    const char *name,
    ANARIDataType parameterType,
    const char *info,
    ANARIDataType infoType)
{
  if (type != ANARI_FRAME || parameterType != ANARI_DATA_TYPE
      || infoType != ANARI_DATA_TYPE_LIST || std::strcmp(info, "value") != 0)
    return nullptr;
  if (m_metadata == "incomplete")
    return nullptr;
  if (m_rendererMetadata == "opaque"
      && std::strcmp(name, "Temperature_RAW") == 0) {
    m_types = {ANARI_FLOAT64, ANARI_UNKNOWN};
    return m_types.data();
  }
  if (m_metadata == "identities"
      && (std::strcmp(name, "channel.objectId") == 0
          || std::strcmp(name, "channel.primitiveId") == 0
          || std::strcmp(name, "channel.instanceId") == 0))
    m_types = {ANARI_UINT32, ANARI_UNKNOWN};
  else if (m_metadata == "mixed" && std::strcmp(name, "channel.unusual") == 0)
    m_types = {ANARI_FLOAT64, ANARI_FLOAT32, ANARI_UNKNOWN};
  else if (m_metadata == "unsupported"
      || std::strcmp(name, "channel.unusual") == 0)
    m_types = {ANARI_FLOAT64, ANARI_UNKNOWN};
  else if (std::strcmp(name, "channel.normal") == 0
      || std::strcmp(name, "channel.albedo") == 0)
    m_types = {ANARI_UFIXED8_VEC3, ANARI_FLOAT32_VEC3, ANARI_UNKNOWN};
  else if (std::strcmp(name, "Temperature_RAW") == 0
      && !m_integerSamples.empty())
    m_types = {ANARI_UINT32, ANARI_UNKNOWN};
  else if (m_type != ANARI_UNKNOWN)
    m_types = {m_type, ANARI_UNKNOWN};
  else if (std::strcmp(name, "channel.motionVectors") == 0)
    m_types = {ANARI_FLOAT32_VEC2, ANARI_UNKNOWN};
  else if (std::strcmp(name, "channel.color") == 0)
    m_types = {ANARI_UFIXED8_RGBA_SRGB, ANARI_FLOAT32_VEC4, ANARI_UNKNOWN};
  else
    m_types = {ANARI_FLOAT32, ANARI_UNKNOWN};
  return m_types.data();
}
const void *TestDevice::frameBufferMap(ANARIFrame frame,
    const char *channel,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *type)
{
  if (std::getenv("VSR_CHANNEL_TEST_FORBID_RENDER"))
    std::abort();
  auto &o = *reinterpret_cast<TestObject *>(frame);
  *width = m_map == "wrong-size" ? 0 : o.width;
  *height = o.height;
  o.mapped.push_back(channel);
  auto request = o.requested.find(channel);
  *type = m_map == "wrong-type"      ? ANARI_FLOAT64
      : request == o.requested.end() ? ANARI_UNKNOWN
                                     : request->second;
  if (request == o.requested.end())
    return nullptr;
  const bool identity = m_metadata == "identities"
      && (std::strcmp(channel, "channel.objectId") == 0
          || std::strcmp(channel, "channel.primitiveId") == 0
          || std::strcmp(channel, "channel.instanceId") == 0);
  if ((o.renderer && o.renderer->subtype == "broken"
          && std::strcmp(channel, "Temperature_RAW") == 0)
      || m_map == "custom-null" && std::strcmp(channel, "Temperature_RAW") == 0
      || m_map == "null" || (m_map == "after-first" && m_renderCount > 1)
      || (m_map == "identity-null" && identity)
      || (!identity && std::strcmp(channel, "Temperature_RAW") != 0
          && std::strcmp(channel, "channel.motionVectors") != 0
          && std::strcmp(channel, "channel.color") != 0
          && std::strcmp(channel, "channel.normal") != 0
          && std::strcmp(channel, "channel.albedo") != 0
          && std::strcmp(channel, "channel.depth") != 0))
    return nullptr;
  if (identity) {
    static const uint32_t ids[] = {0u, 1u, 0x80000001u, ~0u};
    o.integerPixels.resize(size_t(o.width) * o.height);
    for (size_t i = 0; i < o.integerPixels.size(); ++i)
      o.integerPixels[i] = m_integerSamples.empty()
          ? ids[i % 4]
          : m_integerSamples[i % m_integerSamples.size()];
    return o.integerPixels.data();
  }
  if (std::strcmp(channel, "channel.color") == 0
      && (*type == ANARI_UFIXED8_RGBA_SRGB || *type == ANARI_UFIXED8_VEC4)) {
    static const uint32_t colors[] = {
        0xff030201u, 0xff060504u, 0xff090807u, 0xff0c0b0au};
    o.integerPixels.resize(size_t(o.width) * o.height);
    for (size_t i = 0; i < o.integerPixels.size(); ++i)
      o.integerPixels[i] = colors[i % 4];
    return o.integerPixels.data();
  }
  if (*type == ANARI_UFIXED8_VEC3 || *type == ANARI_UFIXED8_VEC4
      || *type == ANARI_UFIXED8_RGB_SRGB || *type == ANARI_UFIXED8_RGBA_SRGB) {
    const int components =
        (*type == ANARI_UFIXED8_VEC3 || *type == ANARI_UFIXED8_RGB_SRGB) ? 3
                                                                         : 4;
    static const uint8_t samples[] = {0, 64, 128, 192};
    o.bytePixels.resize(size_t(o.width) * o.height * components);
    for (size_t i = 0; i < o.bytePixels.size(); ++i)
      o.bytePixels[i] = samples[i % 4];
    return o.bytePixels.data();
  }
  if ((*type == ANARI_FLOAT32_VEC2 || *type == ANARI_FLOAT32_VEC3
          || *type == ANARI_FLOAT32_VEC4)
      && !m_samples.empty()) {
    const int components = *type == ANARI_FLOAT32_VEC2 ? 2
        : *type == ANARI_FLOAT32_VEC3                  ? 3
                                                       : 4;
    o.pixels.resize(size_t(o.width) * o.height * components);
    for (size_t i = 0; i < size_t(o.width) * o.height; ++i)
      for (int c = 0; c < components; ++c)
        o.pixels[i * components + c] = m_samples[i % 4] * float(c + 1);
    return o.pixels.data();
  }
  if (std::strcmp(channel, "Temperature_RAW") == 0) {
    const size_t count = size_t(o.width) * o.height;
    if (*type == ANARI_UINT32) {
      o.integerPixels.resize(count);
      for (size_t i = 0; i < count; ++i)
        o.integerPixels[i] = m_integerSamples.empty()
            ? uint32_t(i % 4)
            : m_integerSamples[i % m_integerSamples.size()];
      return o.integerPixels.data();
    }
    if (!m_samples.empty()) {
      o.pixels.resize(count);
      for (size_t i = 0; i < count; ++i)
        o.pixels[i] = m_samples[i % m_samples.size()];
      return o.pixels.data();
    }
  }
  o.pixels.resize(o.width * o.height * 4);
  for (size_t i = 0; i < o.pixels.size(); ++i)
    o.pixels[i] = float(i % 4);
  return o.pixels.data();
}
void TestDevice::frameBufferUnmap(ANARIFrame frame, const char *)
{
  auto &pixels = reinterpret_cast<TestObject *>(frame)->pixels;
  std::fill(pixels.begin(), pixels.end(), -999.f);
  auto &integers = reinterpret_cast<TestObject *>(frame)->integerPixels;
  std::fill(integers.begin(), integers.end(), ~0u);
}
void TestDevice::renderFrame(ANARIFrame frame)
{
  ++reinterpret_cast<TestObject *>(frame)->renders;
  ++m_renderCount;
  if (std::getenv("VSR_CHANNEL_TEST_FORBID_RENDER"))
    std::abort();
}
int TestDevice::frameReady(ANARIFrame, ANARIWaitMask wait)
{
  return wait == ANARI_WAIT || !m_pending;
}
void TestDevice::discardFrame(ANARIFrame) {}

struct TestLibrary : anari::LibraryImpl
{
  TestLibrary(void *, ANARIStatusCallback, const void *);
  ANARIDevice newDevice(const char *) override;
  const char **getDeviceExtensions(const char *) override;
};
TestLibrary::TestLibrary(void *handle, ANARIStatusCallback cb, const void *user)
    : anari::LibraryImpl(handle, cb, user)
{}
ANARIDevice TestLibrary::newDevice(const char *)
{
  return reinterpret_cast<ANARIDevice>(
      std::make_unique<TestDevice>(this_library()).release());
}
const char **TestLibrary::getDeviceExtensions(const char *)
{
  static const char *extensions[] = {
      "ANARI_KHR_FRAME_CHANNEL_OBJECT_ID", nullptr};
  return extensions;
}
} // namespace

extern "C" ANARI_DEFINE_LIBRARY_ENTRYPOINT(channel_test, handle, cb, user)
{
  return reinterpret_cast<ANARILibrary>(
      std::make_unique<TestLibrary>(handle, cb, user).release());
}

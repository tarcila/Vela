// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_integration
#include "vsr/integration/image/ImageExchange.hpp"
#include "vsr/integration/mcp/Json.hpp"
// posix
#if defined(__linux__)
#include <dirent.h>
#include <fcntl.h>
#include <linux/openat2.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
// std
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <future>
#include <limits>
#include <mutex>
#include <optional>
#include <vector>

namespace vsr::integration::image {
namespace {
using operations::Outcome;
using operations::Step;
using ByteBuffer = std::pmr::vector<std::uint8_t>;
constexpr std::size_t FILE_CAP = 1024;
constexpr std::size_t BYTE_CAP = 268435456;
constexpr std::size_t ENTRY_CAP = 4096;
constexpr std::size_t WORKER_STACK_BYTES = 512u << 10;
constexpr std::size_t WORKER_GUARD_BYTES = 64u << 10;

std::string quote(const std::string &s)
{
  return mcp::json::quote(s);
}

std::string handleName(const identity::ObjectHandle &handle)
{
  char text[33];
  std::snprintf(text,
      sizeof(text),
      "%016llx%016llx",
      (unsigned long long)handle.domain,
      (unsigned long long)handle.serial);
  return text;
}

Step reject(Outcome code, const char *reason)
{
  Step step;
  step.outcome = code;
  step.result = std::string("{\"reason\":") + quote(reason) + "}";
  return step;
}

struct Bounds
{
  std::size_t texels{0};
  std::size_t filtered{0};
  std::size_t encoded{0};
  std::size_t work{0};
  std::size_t peak{0};
};

std::optional<Bounds> bounds(
    std::size_t width, std::size_t height, std::size_t channels)
{
  constexpr auto MAX = std::numeric_limits<std::size_t>::max();
  if (!width || !height || !channels || channels > 4 || width > UINT32_MAX
      || height > UINT32_MAX || width > MAX / height
      || width * height > MAX / channels)
    return {};
  Bounds b;
  b.texels = width * height * channels;
  if (height > (MAX - 1048576) / 4 || b.texels > (MAX - 1048576) / 4 - height)
    return {};
  b.filtered = b.texels + height;
  b.encoded = std::min<std::size_t>(67108864, 2 * b.filtered + 65536);
  b.work = 4 * b.filtered + 1048576;
  if (b.texels > MAX - b.encoded || b.texels + b.encoded > MAX - b.work)
    return {};
  b.peak = b.texels + b.encoded + b.work;
  // Stored DEFLATE: F + five bytes per 65535-byte block, zlib header/trailer,
  // PNG signature/IHDR/one IDAT/IEND. No compressor allocations or growth.
  const auto actual = b.filtered + 5 * ((b.filtered + 65534) / 65535) + 63;
  if (actual > b.encoded)
    return {};
  return b;
}

void be32(ByteBuffer &out, std::uint32_t value)
{
  for (int i = 3; i >= 0; --i)
    out.push_back(std::uint8_t(value >> (8 * i)));
}

std::uint32_t crc(const std::uint8_t *data, std::size_t size)
{
  std::uint32_t value = UINT32_MAX;
  for (std::size_t i = 0; i < size; ++i) {
    value ^= data[i];
    for (int bit = 0; bit < 8; ++bit)
      value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1)));
  }
  return ~value;
}

bool chunkEnd(ByteBuffer &out,
    std::size_t start,
    const std::function<bool()> &abandoned = {})
{
  if (!abandoned) {
    be32(out, crc(out.data() + start, out.size() - start));
    return true;
  }
  std::uint32_t value = UINT32_MAX;
  for (std::size_t i = start; i < out.size(); ++i) {
    if ((i - start) % 4096 == 0 && abandoned())
      return false;
    value ^= out[i];
    for (int bit = 0; bit < 8; ++bit)
      value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1)));
  }
  be32(out, ~value);
  return true;
}

ByteBuffer encode(const ByteBuffer &pixels,
    std::size_t width,
    std::size_t height,
    std::size_t channels,
    const Bounds &b,
    const std::function<bool()> &abandoned)
{
  const auto blocks = (b.filtered + 65534) / 65535;
  const auto compressed = b.filtered + 5 * blocks + 6;
  ByteBuffer out(pixels.get_allocator().resource());
  out.reserve(compressed + 57);
  out.insert(out.end(), {137, 80, 78, 71, 13, 10, 26, 10});
  be32(out, 13);
  auto start = out.size();
  out.insert(out.end(), {'I', 'H', 'D', 'R'});
  be32(out, std::uint32_t(width));
  be32(out, std::uint32_t(height));
  constexpr std::uint8_t TYPES[] = {0, 0, 4, 2, 6};
  out.insert(out.end(), {8, TYPES[channels], 0, 0, 0});
  chunkEnd(out, start);
  be32(out, std::uint32_t(compressed));
  start = out.size();
  out.insert(out.end(), {'I', 'D', 'A', 'T', 0x78, 0x01});
  const auto stride = width * channels;
  std::uint32_t a = 1, d = 0;
  for (std::size_t offset = 0; offset < b.filtered;) {
    if (abandoned())
      return {};
    const auto count = std::min<std::size_t>(65535, b.filtered - offset);
    out.push_back(offset + count == b.filtered ? 1 : 0);
    out.push_back(std::uint8_t(count));
    out.push_back(std::uint8_t(count >> 8));
    out.push_back(std::uint8_t(~count));
    out.push_back(std::uint8_t((~count) >> 8));
    for (std::size_t i = 0; i < count; ++i) {
      const auto position = offset + i;
      const auto row = position / (stride + 1);
      const auto column = position % (stride + 1);
      const auto sample = column ? pixels[row * stride + column - 1] : 0;
      out.push_back(std::uint8_t(sample));
      a = (a + sample) % 65521;
      d = (d + a) % 65521;
    }
    offset += count;
  }
  be32(out, (d << 16) | a);
  if (!chunkEnd(out, start, abandoned))
    return {};
  be32(out, 0);
  start = out.size();
  out.insert(out.end(), {'I', 'E', 'N', 'D'});
  chunkEnd(out, start);
  return out;
}

#if defined(__linux__)
struct Directory
{
  int fd{-1};
  int parent{-1};
  std::string name;
  dev_t device{};
  ino_t inode{};
  Directory() = default;
  VSR_NOT_COPYABLE(Directory)
  ~Directory();
  bool current() const;
};
Directory::~Directory()
{
  if (fd >= 0)
    ::close(fd);
  if (parent >= 0)
    ::close(parent);
}
bool Directory::current() const
{
  struct stat held{}, named{};
  return ::fstat(fd, &held) == 0 && held.st_nlink > 0
      && ::fstatat(parent, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) == 0
      && S_ISDIR(named.st_mode) && held.st_dev == device && held.st_ino == inode
      && named.st_dev == device && named.st_ino == inode
      && descriptor::detail::checkAncestor(fd, ::geteuid()).ok();
}

void removeContents(int directory, const std::function<bool()> &current)
{
  descriptor::detail::Fd scanFd(
      ::openat(directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  auto *scan = ::fdopendir(scanFd.fd);
  if (!scan)
    return;
  scanFd.release();
  while (auto *entry = ::readdir(scan)) {
    if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, ".."))
      continue;
    if (!current())
      break;
    struct stat named{};
    if (::fstatat(directory, entry->d_name, &named, AT_SYMLINK_NOFOLLOW) != 0)
      continue;
    if (S_ISDIR(named.st_mode)) {
      struct open_how how{};
      how.flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW;
      how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_XDEV;
      descriptor::detail::Fd child(int(
          ::syscall(SYS_openat2, directory, entry->d_name, &how, sizeof(how))));
      if (child.fd < 0)
        continue; // Never recurse into a mount or a replacement link.
      const std::string name = entry->d_name;
      const auto childCurrent = [&] {
        struct stat held{}, latest{};
        return current() && ::fstat(child.fd, &held) == 0
            && ::fstatat(directory, name.c_str(), &latest, AT_SYMLINK_NOFOLLOW)
            == 0
            && S_ISDIR(latest.st_mode) && held.st_uid == ::geteuid()
            && held.st_dev == named.st_dev && held.st_ino == named.st_ino
            && latest.st_dev == held.st_dev && latest.st_ino == held.st_ino;
      };
      if (childCurrent())
        removeContents(child.fd, childCurrent);
      if (childCurrent())
        ::unlinkat(directory, name.c_str(), AT_REMOVEDIR);
    } else if (current())
      ::unlinkat(directory, entry->d_name, 0);
  }
  ::closedir(scan);
}
#endif

std::string bindings(
    identity::SceneIdentity &ids, const vsr::scene::Array &image)
{
  const auto &db = ids.scene()->objectDB();
  const anari::DataType types[] = {ANARI_ARRAY1D,
      ANARI_SURFACE,
      ANARI_GEOMETRY,
      ANARI_MATERIAL,
      ANARI_SAMPLER,
      ANARI_VOLUME,
      ANARI_SPATIAL_FIELD,
      ANARI_LIGHT,
      ANARI_CAMERA,
      ANARI_RENDERER};
  const std::size_t capacities[] = {db.array.capacity(),
      db.surface.capacity(),
      db.geometry.capacity(),
      db.material.capacity(),
      db.sampler.capacity(),
      db.volume.capacity(),
      db.field.capacity(),
      db.light.capacity(),
      db.camera.capacity(),
      db.renderer.capacity()};
  std::string samplers = "[", users = "[";
  std::size_t work = 0, results = 0;
  bool complete = true;
  for (std::size_t pool = 0; pool < std::size(types) && complete; ++pool) {
    for (std::size_t slot = 0; slot < capacities[pool] && complete; ++slot) {
      if (++work > 4096) {
        complete = false;
        break;
      }
      auto *object = ids.scene()->getObject(types[pool], slot);
      if (!object)
        continue;
      for (std::size_t p = 0; p < object->numParameters(); ++p) {
        if (++work > 4096) {
          complete = false;
          break;
        }
        const auto &value = object->parameterAt(p).value();
        if (!anari::isArray(value.type())
            || value.getAsObjectIndex() != image.index())
          continue;
        auto &list = object->type() == ANARI_SAMPLER ? samplers : users;
        if (list.size() > 1)
          list += ",";
        list += quote(handleName(ids.handleOf(object)));
        if (++results == 256)
          complete = false;
        break; // Each direct user appears only once.
      }
    }
  }
  return "\"affected_samplers\":" + samplers + "],\"affected_users\":" + users
      + "],\"affected_complete\":" + (complete ? "true" : "false");
}

struct Usage
{
  std::size_t files{0};
  std::size_t bytes{0};
  bool complete{false};
};
} // namespace

struct ImageExchange::State
{
  struct Source
  {
    SceneId scene;
    identity::SceneIdentity *identity;
    std::weak_ptr<const void> lifetime;
  };
  struct Job
  {
    explicit Job(std::pmr::memory_resource *allocator) : pixels(allocator) {}
    std::shared_ptr<State> state;
    std::shared_ptr<const void> physicalLease;
    ByteBuffer pixels;
    Bounds bound;
    std::size_t width{}, height{}, channels{};
    std::string receipt;
    std::string name;
    std::function<bool()> abandoned;
    std::function<void()> wake;
    std::future<Step> future;
    bool retained{false};
#if defined(__linux__)
    struct stat fileIdentity{};
    std::size_t encodedSize{0};
    pthread_t worker{};
    void *stack{nullptr};
    bool workerStarted{false};
    std::packaged_task<Step()> task;
#endif
    bool start();
    void join();
    ~Job();
  };
  std::string launchId;
  std::string path;
  std::string reason{"unsupported_platform"};
  ExchangeLimits limits;
  std::vector<Source> sources;
  std::vector<std::weak_ptr<Job>> jobs;
  mutable std::mutex mutex;
  std::mutex fileMutex;
  Usage usage;
  std::size_t reserved{0};
  std::size_t availableRetention{0};
  std::atomic<bool> stopping{false};
#if defined(__linux__)
  std::vector<std::unique_ptr<Directory>> ancestry;
  int runtimeFd{-1};
  int launchFd{-1};
  int imagesFd{-1};
#endif
  ~State();
  bool current() const;
  std::string limitsJson(
      const Bounds *bound = nullptr, bool authenticatedSession = false) const;
  Step write(Job &job);
  bool fileCurrent(const Job &job) const;
};

bool ImageExchange::State::Job::start()
{
#if defined(__linux__)
  const auto page = ::sysconf(_SC_PAGESIZE);
  if (page <= 0 || WORKER_STACK_BYTES % page || WORKER_GUARD_BYTES % page)
    return false;
  task = std::packaged_task<Step()>([this] { return state->write(*this); });
  stack = ::mmap(nullptr,
      WORKER_STACK_BYTES + WORKER_GUARD_BYTES,
      PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS,
      -1,
      0);
  if (stack == MAP_FAILED) {
    stack = nullptr;
    return false;
  }
  pthread_attr_t attr;
  const bool initialized = ::pthread_attr_init(&attr) == 0;
  if (initialized && ::mprotect(stack, WORKER_GUARD_BYTES, PROT_NONE) == 0
      && ::pthread_attr_setguardsize(&attr, 0) == 0
      && ::pthread_attr_setstack(&attr,
             static_cast<char *>(stack) + WORKER_GUARD_BYTES,
             WORKER_STACK_BYTES)
          == 0) {
    future = task.get_future();
    workerStarted = ::pthread_create(
                        &worker,
                        &attr,
                        [](void *self) -> void * {
                          static_cast<Job *>(self)->task();
                          return nullptr;
                        },
                        this)
        == 0;
  }
  if (initialized)
    ::pthread_attr_destroy(&attr);
  if (!workerStarted) {
    future = {};
    ::munmap(stack, WORKER_STACK_BYTES + WORKER_GUARD_BYTES);
    stack = nullptr;
  }
  return workerStarted;
#else
  return false;
#endif
}

void ImageExchange::State::Job::join()
{
#if defined(__linux__)
  if (workerStarted) {
    ::pthread_join(worker, nullptr);
    workerStarted = false;
  }
  if (stack) {
    ::munmap(stack, WORKER_STACK_BYTES + WORKER_GUARD_BYTES);
    stack = nullptr;
  }
#endif
}

ImageExchange::State::Job::~Job()
{
  join();
#if defined(__linux__)
  if (!retained && !name.empty()) {
    struct stat named{};
    if (::fstatat(state->imagesFd, name.c_str(), &named, AT_SYMLINK_NOFOLLOW)
            == 0
        && S_ISREG(named.st_mode) && named.st_dev == fileIdentity.st_dev
        && named.st_ino == fileIdentity.st_ino)
      ::unlinkat(state->imagesFd, name.c_str(), 0);
  }
#endif
  // Release physical buffers before either cap-accounting lease. A host
  // consumer may admit new memory as soon as the lease disappears.
  {
    ByteBuffer empty(pixels.get_allocator().resource());
    pixels.swap(empty);
  }
  // physicalLease is declared before every buffer/control member, so its
  // automatic destruction occurs only after all of them have been freed.
  std::lock_guard<std::mutex> lock(state->mutex);
  state->reserved -= bound.peak;
}

ImageExchange::State::~State()
{
#if defined(__linux__)
  if (imagesFd >= 0) {
    const auto owned = [&] {
      struct stat held{}, named{};
      return ::fstat(imagesFd, &held) == 0
          && ::fstatat(launchFd, "images", &named, AT_SYMLINK_NOFOLLOW) == 0
          && S_ISDIR(named.st_mode) && held.st_dev == named.st_dev
          && held.st_ino == named.st_ino
          && (ancestry.empty()
              || std::all_of(ancestry.begin(),
                  ancestry.end(),
                  [](const auto &d) { return d->current(); }));
    };
    if (owned())
      removeContents(imagesFd, owned);
    if (owned())
      ::unlinkat(launchFd, "images", AT_REMOVEDIR);
    ::close(imagesFd);
  }
  if (launchFd >= 0) {
    struct stat held{}, named{};
    if (::fstat(launchFd, &held) == 0
        && ::fstatat(runtimeFd, launchId.c_str(), &named, AT_SYMLINK_NOFOLLOW)
            == 0
        && S_ISDIR(named.st_mode) && held.st_dev == named.st_dev
        && held.st_ino == named.st_ino)
      ::unlinkat(runtimeFd, launchId.c_str(), AT_REMOVEDIR);
    ::close(launchFd);
  }
  if (runtimeFd >= 0)
    ::close(runtimeFd);
#endif
}

bool ImageExchange::State::current() const
{
#if defined(__linux__)
  return reason.empty()
      && std::all_of(ancestry.begin(),
          ancestry.end(),
          [](const auto &d) { return d->current(); })
      && descriptor::detail::checkPrivateDirectory(launchFd, ::geteuid()).ok()
      && ancestry.size() >= 3
      && descriptor::detail::checkPrivateDirectory(
          ancestry[ancestry.size() - 3]->fd, ::geteuid())
             .ok()
      && descriptor::detail::checkPrivateDirectory(imagesFd, ::geteuid()).ok();
#else
  return false;
#endif
}

bool ImageExchange::State::fileCurrent(const Job &job) const
{
#if defined(__linux__)
  struct stat named{};
  const auto &held = job.fileIdentity;
  return current() && !job.name.empty()
      && ::fstatat(imagesFd, job.name.c_str(), &named, AT_SYMLINK_NOFOLLOW) == 0
      && S_ISREG(named.st_mode) && named.st_uid == ::geteuid()
      && (named.st_mode & 07777) == 0600 && named.st_nlink == 1
      && named.st_dev == held.st_dev && named.st_ino == held.st_ino
      && named.st_size >= 0 && std::size_t(named.st_size) == job.encodedSize
      && named.st_mtim.tv_sec == held.st_mtim.tv_sec
      && named.st_mtim.tv_nsec == held.st_mtim.tv_nsec
      && named.st_ctim.tv_sec == held.st_ctim.tv_sec
      && named.st_ctim.tv_nsec == held.st_ctim.tv_nsec;
#else
  (void)job;
  return false;
#endif
}

std::string ImageExchange::State::limitsJson(
    const Bounds *b, bool authenticatedSession) const
{
  std::lock_guard<std::mutex> lock(mutex);
  std::string result = "{\"file_cap\":1024,\"byte_cap\":268435456,\"files_used\":"
      + std::to_string(usage.files) + ",\"bytes_used\":" + std::to_string(usage.bytes)
      + ",\"usage_complete\":" + (usage.complete ? "true" : "false")
      + ",\"quota_scan_entry_cap\":4096,\"max_ancillary_bytes\":65536,"
        "\"transient_image_byte_cap\":" + std::to_string(limits.transientBytes)
      + ",\"session_snapshot_byte_cap\":67108864,\"effective_global_retention_byte_cap\":"
      + std::to_string(limits.globalRetentionBytes)
      + ",\"available_global_retention_bytes\":"
      + std::to_string(availableRetention);
  if (b)
    result += ",\"max_encoded_file_bytes\":" + std::to_string(b->encoded)
        + ",\"max_codec_work_bytes\":" + std::to_string(b->work);
  // Before Import exists no session has a retained or pending Image snapshot.
  if (b || authenticatedSession)
    result += ",\"available_session_snapshot_bytes\":67108864";
  return result + "}";
}

Step ImageExchange::State::write(Job &job)
{
#if defined(__linux__)
  std::lock_guard<std::mutex> serial(fileMutex);
  if (!current())
    return reject(Outcome::INVALID_REQUEST, "file_rejected");
  Usage observed;
  descriptor::detail::Fd scanFd(
      ::openat(imagesFd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  DIR *raw = ::fdopendir(scanFd.fd);
  if (!raw)
    return reject(Outcome::BUSY, "resource_limit");
  scanFd.release();
  std::unique_ptr<DIR, decltype(&::closedir)> scan(raw, ::closedir);
  std::size_t entries = 0;
  for (;;) {
    if (job.abandoned())
      return reject(Outcome::CANCELLED, "resource_limit");
    errno = 0;
    auto *entry = ::readdir(scan.get());
    if (!entry) {
      observed.complete = errno == 0;
      break;
    }
    if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, ".."))
      continue;
    ++entries;
    struct stat st{};
    if (::fstatat(imagesFd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0)
      break;
    if (S_ISREG(st.st_mode)) {
      ++observed.files;
      const auto bytes = st.st_size < 0 ? BYTE_CAP : std::size_t(st.st_size);
      observed.bytes = std::min(
          BYTE_CAP + 1, observed.bytes + std::min(bytes, BYTE_CAP + 1));
    }
    if (entries == ENTRY_CAP || observed.files >= FILE_CAP
        || observed.bytes > BYTE_CAP)
      break;
  }
  {
    std::lock_guard<std::mutex> lock(mutex);
    usage = observed;
  }
  if (!observed.complete || observed.files >= FILE_CAP
      || observed.bytes > BYTE_CAP
      || job.bound.encoded > BYTE_CAP - observed.bytes)
    return reject(Outcome::BUSY, "resource_limit");
  auto png = encode(job.pixels,
      job.width,
      job.height,
      job.channels,
      job.bound,
      job.abandoned);
  if (png.empty() || job.abandoned())
    return reject(Outcome::CANCELLED, "resource_limit");
  descriptor::detail::Fd file;
  for (int attempt = 0; attempt < 16; ++attempt) {
    std::string random;
    if (!descriptor::detail::randomHex(random, 16))
      return reject(Outcome::BUSY, "resource_limit");
    const auto name = "image-" + random + ".png";
    if (!current())
      return reject(Outcome::INVALID_REQUEST, "file_rejected");
    file.fd = ::openat(imagesFd,
        name.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
        0600);
    if (file.fd >= 0) {
      if (::fstat(file.fd, &job.fileIdentity) != 0)
        return reject(Outcome::INVALID_REQUEST, "file_rejected");
      job.name = name;
      break;
    }
    if (errno != EEXIST)
      return reject(Outcome::INVALID_REQUEST, "file_rejected");
  }
  if (file.fd < 0)
    return reject(Outcome::BUSY, "resource_limit");
  if (::fchmod(file.fd, 0600) != 0)
    return reject(Outcome::INVALID_REQUEST, "file_rejected");
  for (std::size_t offset = 0; offset < png.size();) {
    if (job.abandoned())
      return reject(Outcome::CANCELLED, "file_rejected");
    if (!current())
      return reject(Outcome::INVALID_REQUEST, "file_rejected");
    const auto n = ::write(file.fd,
        png.data() + offset,
        std::min<std::size_t>(65536, png.size() - offset));
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      return reject(Outcome::INVALID_REQUEST, "file_rejected");
    offset += std::size_t(n);
  }
  job.encodedSize = png.size();
  struct stat completed{};
  if (::fstat(file.fd, &completed) != 0
      || completed.st_dev != job.fileIdentity.st_dev
      || completed.st_ino != job.fileIdentity.st_ino)
    return reject(Outcome::INVALID_REQUEST, "file_rejected");
  job.fileIdentity = completed;
  const auto fd = file.release();
  if (::close(fd) != 0 || !fileCurrent(job) || job.abandoned())
    return reject(Outcome::INVALID_REQUEST, "file_rejected");
  {
    std::lock_guard<std::mutex> lock(mutex);
    usage.files = observed.files + 1;
    usage.bytes = observed.bytes + png.size();
  }
  Step result;
  result.result = "{\"file_name\":" + quote(job.name) + "," + job.receipt
      + ",\"exchange_limits\":" + limitsJson(&job.bound) + "}";
  return result;
#else
  (void)job;
  return reject(Outcome::INVALID_REQUEST, "unsupported_platform");
#endif
}

ImageExchange::ImageExchange(Integration &integration,
    operations::OperationEngine &engine,
    std::string launchId,
    const descriptor::RuntimeDirectory &runtime,
    ExchangeLimits limits)
    : m_state(std::make_shared<State>()),
      m_integration(&integration),
      m_engine(&engine)
{
  m_state->launchId = std::move(launchId);
  m_state->limits = std::move(limits);
  m_state->limits.transientBytes = std::min({m_state->limits.transientBytes,
      m_state->limits.physicalByteCap,
      BYTE_CAP});
  m_state->availableRetention = m_state->limits.globalRetentionBytes;
  if (!m_state->limits.allocator)
    m_state->limits.allocator = std::pmr::new_delete_resource();
#if defined(__linux__)
  m_state->reason = "unsafe_directory";
  if (!runtime.valid()
      || !descriptor::detail::isLowerHex(m_state->launchId, 32))
    return;
  descriptor::RuntimeDirectory checked;
  if (!descriptor::openDirectory(runtime.path(), {}, checked).ok())
    return;
  struct stat original{}, reopened{};
  if (::fstat(runtime.fd(), &original) || ::fstat(checked.fd(), &reopened)
      || original.st_dev != reopened.st_dev
      || original.st_ino != reopened.st_ino)
    return;
  m_state->runtimeFd = ::fcntl(runtime.fd(), F_DUPFD_CLOEXEC, 0);
  if (m_state->runtimeFd < 0)
    return;
  // Normal-exit recursive cleanup must be able to refuse mount crossings,
  // not merely follow directory-relative names on an older kernel.
  struct open_how probeHow{};
  probeHow.flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW;
  probeHow.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_XDEV;
  descriptor::detail::Fd probe(int(
      ::syscall(SYS_openat2, runtime.fd(), ".", &probeHow, sizeof(probeHow))));
  if (probe.fd < 0) {
    if (errno == ENOSYS || errno == EINVAL)
      m_state->reason = "unsupported_platform";
    return;
  }
  const auto launchPath = runtime.path() + "/" + m_state->launchId;
  if (::mkdirat(runtime.fd(), m_state->launchId.c_str(), 0700) != 0)
    return; // fresh launch only; never adopt existing content
  struct stat createdLaunch{};
  if (::fstatat(runtime.fd(),
          m_state->launchId.c_str(),
          &createdLaunch,
          AT_SYMLINK_NOFOLLOW)
      != 0)
    return;
  descriptor::detail::Fd launch(::openat(runtime.fd(),
      m_state->launchId.c_str(),
      O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (launch.fd < 0
      || !descriptor::detail::checkPrivateDirectory(launch.fd, ::geteuid())
          .ok()) {
    struct stat named{};
    if (::fstatat(runtime.fd(),
            m_state->launchId.c_str(),
            &named,
            AT_SYMLINK_NOFOLLOW)
            == 0
        && S_ISDIR(named.st_mode) && named.st_dev == createdLaunch.st_dev
        && named.st_ino == createdLaunch.st_ino)
      ::unlinkat(runtime.fd(), m_state->launchId.c_str(), AT_REMOVEDIR);
    return;
  }
  struct stat openedLaunch{};
  if (::fstat(launch.fd, &openedLaunch) != 0
      || openedLaunch.st_dev != createdLaunch.st_dev
      || openedLaunch.st_ino != createdLaunch.st_ino)
    return;
  m_state->launchFd = launch.release();
  if (::mkdirat(m_state->launchFd, "images", 0700) != 0)
    return;
  struct stat createdImages{};
  if (::fstatat(
          m_state->launchFd, "images", &createdImages, AT_SYMLINK_NOFOLLOW)
      != 0)
    return;
  m_state->imagesFd = ::openat(m_state->launchFd,
      "images",
      O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (m_state->imagesFd < 0) {
    struct stat named{};
    if (::fstatat(m_state->launchFd, "images", &named, AT_SYMLINK_NOFOLLOW) == 0
        && S_ISDIR(named.st_mode) && named.st_dev == createdImages.st_dev
        && named.st_ino == createdImages.st_ino)
      ::unlinkat(m_state->launchFd, "images", AT_REMOVEDIR);
    return;
  }
  struct stat openedImages{};
  if (::fstat(m_state->imagesFd, &openedImages) != 0
      || openedImages.st_dev != createdImages.st_dev
      || openedImages.st_ino != createdImages.st_ino)
    return;
  m_state->path = launchPath + "/images";
  descriptor::detail::Fd parent(
      ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  std::size_t offset = 1;
  while (offset < m_state->path.size()) {
    auto end = m_state->path.find('/', offset);
    if (end == std::string::npos)
      end = m_state->path.size();
    auto d = std::make_unique<Directory>();
    d->name = m_state->path.substr(offset, end - offset);
    d->parent = ::fcntl(parent.fd, F_DUPFD_CLOEXEC, 0);
    d->fd = ::openat(parent.fd,
        d->name.c_str(),
        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat st{};
    if (d->fd < 0 || ::fstat(d->fd, &st) != 0)
      return;
    d->device = st.st_dev;
    d->inode = st.st_ino;
    ::close(parent.fd);
    parent.fd = ::fcntl(d->fd, F_DUPFD_CLOEXEC, 0);
    m_state->ancestry.push_back(std::move(d));
    offset = end + 1;
  }
  if (!descriptor::detail::checkPrivateDirectory(m_state->imagesFd, ::geteuid())
          .ok())
    return;
  // Path observations must name the same held descriptors used for I/O,
  // even if ancestry changed during setup.
  const int expected[] = {
      m_state->runtimeFd, m_state->launchFd, m_state->imagesFd};
  if (m_state->ancestry.size() < std::size(expected))
    return;
  for (std::size_t i = 0; i < std::size(expected); ++i) {
    struct stat held{}, named{};
    if (::fstat(expected[i], &held) != 0
        || ::fstat(
               m_state->ancestry[m_state->ancestry.size() - 3 + i]->fd, &named)
            != 0
        || held.st_dev != named.st_dev || held.st_ino != named.st_ino)
      return;
  }
  m_state->reason.clear();
  m_state->usage.complete = true;
#else
  (void)runtime;
#endif
}

ImageExchange::~ImageExchange()
{
  m_state->stopping = true;
  // Join while engine callbacks are still live, before the host tears down
  // its engine and activity pool. Jobs hold State until their cleanup ends.
  std::vector<std::weak_ptr<State::Job>> jobs;
  {
    std::lock_guard<std::mutex> lock(m_state->mutex);
    jobs = m_state->jobs;
  }
  for (const auto &weak : jobs)
    if (auto job = weak.lock())
      job->join();
}

void ImageExchange::attachScene(
    SceneId scene, identity::SceneIdentity &identity)
{
  m_state->sources.push_back({scene, &identity, identity.lifetime()});
}

std::string ImageExchange::metadata(
    SceneId scene, const std::string &sessionId) const
{
  if (m_state->limits.availableGlobalRetention) {
    const auto available = m_state->limits.availableGlobalRetention();
    std::lock_guard<std::mutex> lock(m_state->mutex);
    m_state->availableRetention = available;
  }
  const bool tracking = m_integration->imageTrackingAvailable(scene);
  const bool enabled = tracking && m_state->current();
  return std::string("{\"supported\":") + (enabled ? "true" : "false")
      + ",\"enabled\":" + (enabled ? "true" : "false")
      + ",\"export\":" + (enabled ? "true" : "false")
      + ",\"import\":false,\"data_tracking\":" + (tracking ? "true" : "false")
      + (enabled ? ",\"image_exchange_directory\":" + quote(m_state->path)
                 : ",\"reason\":"
                  + quote(!tracking ? "host_write_contract_unavailable"
                          : m_state->reason.empty() ? "file_rejected"
                                                    : m_state->reason))
      + ",\"exchange_limits\":"
      + m_state->limitsJson(nullptr, !sessionId.empty()) + "}";
}

std::string ImageExchange::unavailableReason(SceneId scene) const
{
  if (!m_integration->imageTrackingAvailable(scene))
    return "host_write_contract_unavailable";
  if (m_state->current())
    return {};
  return m_state->reason.empty() ? "file_rejected" : m_state->reason;
}

void ImageExchange::pump()
{
  std::vector<std::shared_ptr<State::Job>> ready;
  {
    std::lock_guard<std::mutex> lock(m_state->mutex);
    for (const auto &weak : m_state->jobs) {
      auto job = weak.lock();
      if (job && job->future.valid()
          && job->future.wait_for(std::chrono::seconds(0))
              == std::future_status::ready)
        ready.push_back(std::move(job));
    }
  }
  for (const auto &job : ready)
    job->wake();
}

operations::Admission ImageExchange::submitExport(
    const session::Credentials &credentials,
    const operations::OperationRef &ref,
    SceneId scene,
    identity::ObjectHandle handle)
{
  auto state = m_state;
  auto job = std::make_shared<State::Job>(state->limits.allocator);
  job->state = state;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->jobs.erase(std::remove_if(state->jobs.begin(),
                          state->jobs.end(),
                          [](const auto &weak) { return weak.expired(); }),
        state->jobs.end());
    state->jobs.push_back(job);
  }
  operations::Request request;
  request.kind = "image_export";
  request.target = session::Target::scene(scene);
  request.capability = session::Capability::READ;
  request.deadlineSeconds = 30.;
  request.canonicalArguments = std::to_string(scene) + ":" + handleName(handle);
  request.executor = [state, job, scene, handle, integration = m_integration](
                         operations::StepContext &ctx) {
    try {
      if (job->future.valid()) {
        // pump wakes only ready futures. Active cancellation retains the
        // backend slot; never join a worker under the engine's record lock.
        if (job->future.wait_for(std::chrono::seconds(0))
            != std::future_status::ready) {
          Step pending;
          pending.status = Step::Status::BACKEND_PENDING;
          return pending;
        }
        Step result;
        try {
          result = job->future.get();
        } catch (const std::bad_alloc &) {
          return reject(Outcome::BUSY, "resource_limit");
        }
        if (result.outcome == Outcome::OK && !state->fileCurrent(*job))
          return reject(Outcome::INVALID_REQUEST, "file_rejected");
        if (result.outcome == Outcome::OK && !ctx.isAbandoned())
          result.onAccepted = [job] { job->retained = true; };
        return result;
      }
      if (ctx.abandoned || ctx.isAbandoned())
        return Step{};
      identity::SceneIdentity *ids = nullptr;
      for (const auto &source : state->sources) {
        if (source.lifetime.expired())
          continue;
        if (source.scene == scene)
          ids = source.identity;
        else if (auto *foreign = source.identity->resolve(handle))
          return reject(Outcome::INVALID_REQUEST,
              dynamic_cast<vsr::scene::Array *>(foreign) ? "foreign_scene"
                                                         : "invalid_handle");
      }
      auto *object = ids ? ids->resolve(handle) : nullptr;
      auto *array = dynamic_cast<vsr::scene::Array *>(object);
      if (!array)
        return reject(Outcome::INVALID_REQUEST, "invalid_handle");
      if (!integration->imageTrackingAvailable(scene))
        return reject(
            Outcome::INVALID_REQUEST, "host_write_contract_unavailable");
      if (!array->isImage())
        return reject(Outcome::INVALID_REQUEST, "unsupported_image");
      if (array->isMapped())
        return reject(Outcome::BUSY, "image_mapped");
      if (!state->current())
        return reject(Outcome::INVALID_REQUEST, "file_rejected");
      const auto version = ids->imageDataVersion(handle);
      if (!version)
        return reject(
            Outcome::INVALID_REQUEST, "host_write_contract_unavailable");
      const auto b = bounds(array->dim(0), array->dim(1), array->elementSize());
      if (!b)
        return reject(Outcome::BUSY, "resource_limit");
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (b->peak > state->limits.transientBytes
            || state->reserved > state->limits.transientBytes - b->peak)
          return reject(Outcome::BUSY, "resource_limit");
        state->reserved += b->peak;
        job->bound = *b;
      }
      if (state->limits.reservePhysicalMemory) {
        job->physicalLease = state->limits.reservePhysicalMemory(b->peak);
        if (!job->physicalLease)
          return reject(Outcome::BUSY, "resource_limit");
      }
      job->width = array->dim(0);
      job->height = array->dim(1);
      job->channels = array->elementSize();
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->availableRetention = state->limits.availableGlobalRetention
            ? state->limits.availableGlobalRetention()
            : state->limits.globalRetentionBytes;
      }
      try {
        job->pixels.resize(b->texels);
      } catch (const std::bad_alloc &) {
        return reject(Outcome::BUSY, "resource_limit");
      }
      std::memcpy(job->pixels.data(), array->data(), b->texels);
      const auto type = array->elementType();
      const bool srgb =
          type >= ANARI_UFIXED8_R_SRGB && type <= ANARI_UFIXED8_RGBA_SRGB;
      job->receipt = "\"handle\":" + quote(handleName(handle))
          + ",\"data_version\":" + quote(*version) + ",\"dimensions\":["
          + std::to_string(job->width) + "," + std::to_string(job->height)
          + "],\"element_type\":" + quote(anari::toString(type))
          + ",\"channel_count\":" + std::to_string(job->channels)
          + ",\"color_space\":\"" + (srgb ? "srgb" : "linear")
          + "\",\"row_order\":\"top_down\"," + bindings(*ids, *array);
      job->abandoned = [state, check = ctx.isAbandoned] {
        return state->stopping.load() || check();
      };
      job->wake = ctx.wake;
      if (!job->start())
        return reject(Outcome::BUSY, "resource_limit");
      // Host pump observes completion after backend admission, eliminating
      // lost wakes and preserving asynchronous cancellation cleanup.
      Step pending;
      pending.status = Step::Status::BACKEND_PENDING;
      return pending;
    } catch (const std::bad_alloc &) {
      return reject(Outcome::BUSY, "resource_limit");
    } catch (const std::system_error &) {
      return reject(Outcome::BUSY, "resource_limit");
    }
  };
  return m_engine->submit(credentials, ref, std::move(request));
}

} // namespace vsr::integration::image

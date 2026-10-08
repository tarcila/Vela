// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_integration
#include "vsr/integration/image/ImageExchange.hpp"
#include "vsr/integration/activity/Activity.hpp"
#include "vsr/integration/edit/AgentEdit.hpp"
#include "vsr/integration/image/PngImport.hpp"
#include "vsr/integration/mcp/Json.hpp"
#include "vsr/integration/mesh/MeshCapture.hpp"
#include "vsr/integration/mesh/MeshCommit.hpp"
#include "vsr/integration/mesh/MeshReplacement.hpp"
#include "vsr/integration/restore/BatchRestore.hpp"
// vsr_scene
#include "vsr/scene/detail/PreparedPositionBinding.hpp"
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
constexpr std::size_t IMPORT_CONTROL_BYTES = 32768;
constexpr std::size_t IMPORT_STORE_BYTES = 4096;
constexpr std::size_t IMPORT_RESULT_BYTES = 65536;
constexpr std::size_t IMPORT_OWNER_BYTES =
    IMPORT_CONTROL_BYTES + 2 * IMPORT_STORE_BYTES + IMPORT_RESULT_BYTES;

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

std::optional<Bounds> bounds(std::size_t width,
    std::size_t height,
    std::size_t channels,
    bool exporting = true)
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
  if (exporting && actual > b.encoded)
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

struct SessionLedger
{
  std::mutex mutex;
  std::uint64_t bytes{0};
};
struct SessionCharge
{
  std::shared_ptr<SessionLedger> ledger;
  std::uint64_t bytes{0};
  ~SessionCharge();
};
SessionCharge::~SessionCharge()
{
  std::lock_guard<std::mutex> lock(ledger->mutex);
  ledger->bytes -= bytes;
}

bool confinedName(const std::string &name, bool mesh = false)
{
  if (name.empty() || name.size() > 128 || name[0] == '.' || name.size() < 5
      || (mesh ? (name.size() < 7 || name.compare(name.size() - 6, 6, ".vmesh"))
               : name.compare(name.size() - 4, 4, ".png")))
    return false;
  for (const unsigned char c : name)
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
      return false;
  return true;
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
    explicit Job(std::pmr::memory_resource *allocator)
        : pixels(allocator), encoded(allocator), bundle(allocator)
    {}
    std::shared_ptr<State> state;
    std::shared_ptr<const void> controlLease;
    std::shared_ptr<const void> physicalLease;
    ByteBuffer pixels;
    ByteBuffer encoded;
    mesh::Bundle bundle;
    std::shared_ptr<const void> decodeLease;
    std::string expectedDependencies;
    Bounds bound;
    bool importing{false};
    bool mesh{false};
    bool committed{false};
    bool notificationFailed{false};
    bool storePrepared{false};
    edit::BatchRef batch;
    std::shared_ptr<edit::ImageSnapshot> snapshot;
    std::weak_ptr<const void> registration;
    std::string expectedVersion;
    std::string sessionId;
    std::size_t width{}, height{}, channels{};
    anari::DataType elementType{ANARI_UNKNOWN};
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
    void releaseTransient(bool keepPhysicalLease = false);
    ~Job();
  };
  std::string launchId;
  std::string path;
  std::string reason{"unsupported_platform"};
  ExchangeLimits limits;
  std::vector<Source> sources;
  edit::EditService *edits{nullptr};
  std::weak_ptr<const void> editsLifetime;
  restore::RestoreService *restores{nullptr};
  std::weak_ptr<const void> restoresLifetime;
  std::map<std::string, std::shared_ptr<SessionLedger>> ledgers;
  std::shared_ptr<SessionLedger> retentionLedger{
      std::make_shared<SessionLedger>()};
  std::vector<std::weak_ptr<Job>> jobs;
  mutable std::mutex mutex;
  std::mutex fileMutex;
  Usage usage;
  std::size_t reserved{0};
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
      const Bounds *bound = nullptr, const std::string &sessionId = {}) const;
  operations::RestorationPolicy restorationPolicy() const;
  Step write(Job &job);
  Step read(Job &job);
  void pruneRestorations() const;
  bool fileCurrent(const Job &job) const;
};

bool ImageExchange::State::Job::start()
{
#if defined(__linux__)
  const auto page = ::sysconf(_SC_PAGESIZE);
  if (page <= 0 || WORKER_STACK_BYTES % page || WORKER_GUARD_BYTES % page)
    return false;
  task = std::packaged_task<Step()>(
      [this] { return importing ? state->read(*this) : state->write(*this); });
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
  if (!importing && !retained && !name.empty()) {
    struct stat named{};
    if (::fstatat(state->imagesFd, name.c_str(), &named, AT_SYMLINK_NOFOLLOW)
            == 0
        && S_ISREG(named.st_mode) && named.st_dev == fileIdentity.st_dev
        && named.st_ino == fileIdentity.st_ino)
      ::unlinkat(state->imagesFd, name.c_str(), 0);
  }
#endif
  if (storePrepared && !state->editsLifetime.expired())
    state->edits->restorations().cancelImage(batch);
  snapshot.reset();
  // Export's physical lease is destroyed after *all* control members, as
  // well as buffers/stack. Do not release it early in the destructor body.
  releaseTransient(true);
}

void ImageExchange::State::Job::releaseTransient(bool keepPhysicalLease)
{
  join();
  {
    ByteBuffer empty(pixels.get_allocator().resource());
    pixels.swap(empty);
    ByteBuffer emptyEncoded(encoded.get_allocator().resource());
    encoded.swap(emptyEncoded);
    mesh::Bundle emptyBundle(pixels.get_allocator().resource());
    bundle = std::move(emptyBundle);
  }
  decodeLease.reset();
#if defined(__linux__)
  task = {};
#endif
  future = {};
  if (!keepPhysicalLease)
    physicalLease.reset();
  std::lock_guard<std::mutex> lock(state->mutex);
  state->reserved -= bound.peak;
  bound.peak = 0;
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

operations::RestorationPolicy ImageExchange::State::restorationPolicy() const
{
  const auto &policy = limits.retention->limits();
  return {policy.retentionBytes, policy.hardRetentionBytes};
}

std::string ImageExchange::State::limitsJson(
    const Bounds *b, const std::string &sessionId) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto policy = activity::Limits{};
  policy.retentionBytes = limits.globalRetentionBytes;
  policy.hardRetentionBytes = limits.hardRetentionBytes;
  std::uint64_t available = policy.retentionBytes;
  bool oversized = false;
  if (limits.retention) {
    policy = limits.retention->limits();
    const auto used = limits.retention->usage().retentionBytes;
    oversized = used > policy.retentionBytes;
    available = oversized ? 0 : policy.retentionBytes - used;
  }
  std::uint64_t physical = 0;
  {
    std::lock_guard<std::mutex> retentionLock(retentionLedger->mutex);
    physical = retentionLedger->bytes;
  }
  std::string result = "{\"file_cap\":1024,\"byte_cap\":268435456,\"files_used\":"
      + std::to_string(usage.files) + ",\"bytes_used\":" + std::to_string(usage.bytes)
      + ",\"usage_complete\":" + (usage.complete ? "true" : "false")
      + ",\"quota_scan_entry_cap\":4096,\"max_ancillary_bytes\":65536,"
        "\"transient_image_byte_cap\":" + std::to_string(limits.transientBytes)
      + ",\"effective_global_retention_byte_cap\":"
      + std::to_string(policy.retentionBytes)
      + ",\"effective_single_entry_hard_cap_bytes\":"
      + std::to_string(policy.hardRetentionBytes)
      + ",\"oversized_retention\":" + (oversized ? "true" : "false")
      + ",\"approval_available\":false"
      + ",\"available_global_retention_bytes\":" + std::to_string(available)
      + ",\"retained_physical_bytes\":" + std::to_string(physical)
      + ",\"physical_byte_cap\":" + std::to_string(limits.physicalByteCap)
      + ",\"retention_order\":\"publication_fifo\",\"retention_entry_cap\":"
      + std::to_string(policy.restorableBatches)
      + ",\"activity_metadata_byte_cap\":" + std::to_string(policy.bytes)
      + ",\"activity_entry_cap\":" + std::to_string(policy.entries);
  if (b)
    result += ",\"max_encoded_file_bytes\":" + std::to_string(b->encoded)
        + ",\"max_codec_work_bytes\":" + std::to_string(b->work);
  if (!sessionId.empty()) {
    std::uint64_t used = 0;
    const auto it = ledgers.find(sessionId);
    if (it != ledgers.end()) {
      std::lock_guard<std::mutex> ledgerLock(it->second->mutex);
      used = it->second->bytes;
    }
    result += ",\"session_retained_physical_bytes\":" + std::to_string(used);
  }
  return result + "}";
}

Step ImageExchange::State::read(Job &job)
{
#if defined(__linux__)
  if (!current() || !confinedName(job.name, job.mesh))
    return reject(Outcome::INVALID_REQUEST, "file_rejected");
  if (job.mesh) {
    descriptor::detail::Fd scanFd(
        ::openat(imagesFd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    DIR *raw = ::fdopendir(scanFd.fd);
    if (!raw)
      return reject(Outcome::BUSY, "resource_limit");
    scanFd.release();
    std::unique_ptr<DIR, decltype(&::closedir)> scan(raw, ::closedir);
    std::size_t entries = 0, files = 0, bytes = 0;
    for (;;) {
      if (job.abandoned())
        return reject(Outcome::CANCELLED, "cancelled");
      errno = 0;
      auto *entry = ::readdir(scan.get());
      if (!entry) {
        if (errno)
          return reject(Outcome::BUSY, "resource_limit");
        break;
      }
      if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, ".."))
        continue;
      if (++entries > ENTRY_CAP)
        return reject(Outcome::BUSY, "resource_limit");
      struct stat observed{};
      if (::fstatat(imagesFd, entry->d_name, &observed, AT_SYMLINK_NOFOLLOW))
        return reject(Outcome::INVALID_REQUEST, "file_rejected");
      if (S_ISREG(observed.st_mode)) {
        if (++files > FILE_CAP || observed.st_size < 0
            || std::uint64_t(observed.st_size) > BYTE_CAP - bytes)
          return reject(Outcome::BUSY, "resource_limit");
        bytes += std::size_t(observed.st_size);
      }
    }
  }
  descriptor::detail::Fd file(::openat(imagesFd,
      job.name.c_str(),
      O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  struct stat before{}, after{};
  if (file.fd < 0 || ::fstat(file.fd, &before) != 0 || !S_ISREG(before.st_mode)
      || before.st_uid != ::geteuid() || before.st_nlink != 1
      || before.st_size < 0 || (job.mesh && (before.st_mode & 07777) != 0600))
    return reject(Outcome::INVALID_REQUEST, "file_rejected");
  if (std::uint64_t(before.st_size) > job.bound.encoded)
    return reject(
        Outcome::INVALID_REQUEST, job.mesh ? "mesh_file_limit" : "invalid_png");
  if (job.mesh) {
    const auto bytes = std::size_t(before.st_size);
    job.decodeLease = limits.reservePhysicalMemory(bytes);
    if (!job.decodeLease)
      return reject(Outcome::BUSY, "resource_limit");
    std::lock_guard<std::mutex> lock(mutex);
    if (bytes > limits.transientBytes
        || reserved > limits.transientBytes - bytes)
      return reject(Outcome::BUSY, "resource_limit");
    reserved += bytes;
    job.bound.peak += bytes;
  }
  job.encoded.resize(std::size_t(before.st_size));
  for (std::size_t offset = 0; offset < job.encoded.size();) {
    if (job.abandoned())
      return reject(Outcome::CANCELLED, "resource_limit");
    const auto count = ::read(file.fd,
        job.encoded.data() + offset,
        std::min<std::size_t>(65536, job.encoded.size() - offset));
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      return reject(Outcome::INVALID_REQUEST, "file_rejected");
    offset += std::size_t(count);
  }
  std::uint8_t extra{};
  if (::read(file.fd, &extra, 1) != 0 || ::fstat(file.fd, &after) != 0
      || before.st_dev != after.st_dev || before.st_ino != after.st_ino
      || before.st_size != after.st_size || before.st_uid != after.st_uid
      || before.st_nlink != after.st_nlink || before.st_mode != after.st_mode
      || before.st_mtim.tv_sec != after.st_mtim.tv_sec
      || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec
      || before.st_ctim.tv_sec != after.st_ctim.tv_sec
      || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec || !current())
    return reject(Outcome::INVALID_REQUEST, "file_rejected");
  if (job.mesh) {
    job.fileIdentity = after;
    job.encodedSize = std::size_t(after.st_size);
    if (!fileCurrent(job))
      return reject(Outcome::INVALID_REQUEST, "file_rejected");
    if (const auto *reason = mesh::decodeBundle(
            std::move(job.encoded), job.bundle, job.abandoned))
      return reject(std::strcmp(reason, "cancelled") == 0
              ? Outcome::CANCELLED
              : Outcome::INVALID_REQUEST,
          reason);
    return Step{};
  }
  const auto decoded = decodePng(job.encoded,
      job.width,
      job.height,
      job.channels,
      job.pixels,
      job.abandoned);
  if (decoded == DecodeResult::INVALID_PNG)
    return reject(Outcome::INVALID_REQUEST, "invalid_png");
  if (decoded == DecodeResult::RESOURCE_LIMIT)
    return reject(Outcome::BUSY, "resource_limit");
  if (decoded == DecodeResult::CANCELLED)
    return reject(Outcome::CANCELLED, "resource_limit");
  return Step{};
#else
  (void)job;
  return reject(Outcome::INVALID_REQUEST, "unsupported_platform");
#endif
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
  auto png = job.mesh ? std::move(job.pixels)
                      : encode(job.pixels,
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
    const auto name = (job.mesh ? "mesh-" : "image-") + random
        + (job.mesh ? ".vmesh" : ".png");
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
      + ",\"exchange_limits\":" + limitsJson(&job.bound, job.sessionId) + "}";
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

void ImageExchange::attachRestoration(
    edit::EditService &edits, restore::RestoreService &restores)
{
  if (!restores.owns(*m_engine, edits))
    throw std::runtime_error(
        "Image restoration requires the normal restore service");
  if (edits.launchId() != m_state->launchId)
    throw std::runtime_error("Image restoration requires the same launch");
  m_state->edits = &edits;
  m_state->editsLifetime = edits.lifetime();
  m_state->restores = &restores;
  m_state->restoresLifetime = restores.lifetime();
}

bool ImageExchange::importAvailable() const
{
  return m_state->edits && !m_state->editsLifetime.expired()
      && m_state->restores && !m_state->restoresLifetime.expired()
      && m_state->restores->resultOwnershipAvailable()
      && m_state->limits.reservePhysicalMemory && m_state->limits.retention
      && m_state->limits.restorationRetained;
}

void ImageExchange::State::pruneRestorations() const
{
  if (edits && !editsLifetime.expired() && limits.restorationRetained)
    edits->restorations().expireUnless([&](const edit::BatchRef &ref) {
      return limits.restorationRetained(ref.launchId, ref.batchId);
    });
}

void ImageExchange::attachScene(
    SceneId scene, identity::SceneIdentity &identity)
{
  m_state->sources.push_back({scene, &identity, identity.lifetime()});
}

std::string ImageExchange::metadata(
    SceneId scene, const std::string &sessionId) const
{
  m_state->pruneRestorations();
  const bool tracking = m_integration->imageTrackingAvailable(scene);
  const bool enabled = tracking && m_state->current();
  return std::string("{\"supported\":") + (enabled ? "true" : "false")
      + ",\"enabled\":" + (enabled ? "true" : "false")
      + ",\"export\":" + (enabled ? "true" : "false")
      + ",\"import\":" + (enabled && importAvailable() ? "true" : "false")
      + ",\"data_tracking\":" + (tracking ? "true" : "false")
      + ",\"mesh_export\":"
      + (enabled && m_state->limits.reservePhysicalMemory ? "true" : "false")
      + ",\"mesh_import\":" + (enabled && importAvailable() ? "true" : "false")
      + ",\"mesh_revision\":1,\"mesh_payload_byte_cap\":67108864,"
        "\"mesh_header_byte_cap\":65536,\"mesh_field_cap\":64"
      + (enabled ? ",\"mesh_exchange_directory\":" + quote(m_state->path) : "")
      + (enabled ? ",\"image_exchange_directory\":" + quote(m_state->path)
                 : ",\"reason\":"
                  + quote(!tracking ? "host_write_contract_unavailable"
                          : m_state->reason.empty() ? "file_rejected"
                                                    : m_state->reason))
      + ",\"exchange_limits\":" + m_state->limitsJson(nullptr, sessionId) + "}";
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
  m_state->pruneRestorations();
  for (const auto &source : m_state->sources)
    if (!source.lifetime.expired())
      vsr::scene::detail::PreparedPositionBinding::drain(
          *source.identity->scene());
  {
    std::lock_guard<std::mutex> lock(m_state->mutex);
    for (auto it = m_state->ledgers.begin(); it != m_state->ledgers.end();) {
      auto ledger = it->second;
      std::lock_guard<std::mutex> ledgerLock(ledger->mutex);
      if (!ledger->bytes && ledger.use_count() == 2)
        it = m_state->ledgers.erase(it);
      else
        ++it;
    }
  }
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
  return submitCapture(credentials, ref, scene, handle, false);
}

operations::Admission ImageExchange::submitMeshExport(
    const session::Credentials &credentials,
    const operations::OperationRef &ref,
    SceneId scene,
    identity::ObjectHandle handle)
{
  return submitCapture(credentials, ref, scene, handle, true);
}

operations::Admission ImageExchange::submitCapture(
    const session::Credentials &credentials,
    const operations::OperationRef &ref,
    SceneId scene,
    identity::ObjectHandle handle,
    bool mesh)
{
  auto state = m_state;
  auto job = std::make_shared<State::Job>(state->limits.allocator);
  job->state = state;
  job->sessionId = credentials.sessionId;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->jobs.erase(std::remove_if(state->jobs.begin(),
                          state->jobs.end(),
                          [](const auto &weak) { return weak.expired(); }),
        state->jobs.end());
    state->jobs.push_back(job);
  }
  operations::Request request;
  job->mesh = mesh;
  request.kind = mesh ? "mesh_export" : "image_export";
  request.target = session::Target::scene(scene);
  request.capability = session::Capability::READ;
  request.deadlineSeconds = 30.;
  request.canonicalArguments = std::to_string(scene) + ":" + handleName(handle);
  request.executor = [state,
                         job,
                         scene,
                         handle,
                         mesh,
                         integration = m_integration](
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
        if (mesh && result.outcome == Outcome::OK) {
          bool exposed = false;
          for (const auto &source : state->sources)
            if (source.scene == scene && !source.lifetime.expired())
              exposed = source.identity->resolve(handle) != nullptr;
          if (!exposed || !integration->imageTrackingAvailable(scene))
            return reject(Outcome::INVALID_REQUEST, "invalid_handle");
          result.resourceLease = state->limits.reservePhysicalMemory(65536);
          if (!result.resourceLease)
            return reject(Outcome::BUSY, "resource_limit");
          result.reserveStatus = [reserve =
                                         state->limits.reservePhysicalMemory] {
            return reserve(65536);
          };
          if (!ctx.prepareActivityResult(
                  result.result.size() + 512, result.reserveStatus))
            return reject(Outcome::BUSY, "resource_limit");
        }
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
              (dynamic_cast<vsr::scene::Array *>(foreign)
                  || (mesh && dynamic_cast<vsr::scene::Geometry *>(foreign)))
                  ? "foreign_scene"
                  : "invalid_handle");
      }
      auto *object = ids ? ids->resolve(handle) : nullptr;
      auto *array = dynamic_cast<vsr::scene::Array *>(object);
      auto *geometry = dynamic_cast<vsr::scene::Geometry *>(object);
      if (mesh ? !geometry : !array)
        return reject(Outcome::INVALID_REQUEST, "invalid_handle");
      if (!integration->imageTrackingAvailable(scene))
        return reject(
            Outcome::INVALID_REQUEST, "host_write_contract_unavailable");
      if (mesh) {
        if (!state->current())
          return reject(Outcome::INVALID_REQUEST, "file_rejected");
        if (!state->limits.reservePhysicalMemory)
          return reject(
              Outcome::INVALID_REQUEST, "physical_ownership_unavailable");
        constexpr auto CONTROL_BYTES = 8 * mesh::HEADER_CAP + WORKER_STACK_BYTES
            + WORKER_GUARD_BYTES + 65536;
        job->controlLease = state->limits.reservePhysicalMemory(CONTROL_BYTES);
        if (!job->controlLease)
          return reject(Outcome::BUSY, "resource_limit");
        mesh::CapturePlan plan;
        if (const auto *reason = mesh::plan(*geometry, plan))
          return reject(std::strcmp(reason, "array_mapped") == 0
                  ? Outcome::BUSY
                  : Outcome::INVALID_REQUEST,
              reason);
        Bounds bound;
        bound.encoded = plan.payloadBytes + mesh::HEADER_CAP + 12;
        bound.peak = bound.encoded + CONTROL_BYTES;
        {
          std::lock_guard<std::mutex> lock(state->mutex);
          if (bound.peak > state->limits.transientBytes
              || state->reserved > state->limits.transientBytes - bound.peak)
            return reject(Outcome::BUSY, "resource_limit");
          state->reserved += bound.peak;
          job->bound = bound;
        }
        job->physicalLease = state->limits.reservePhysicalMemory(bound.encoded);
        if (!job->physicalLease)
          return reject(Outcome::BUSY, "resource_limit");
        if (const auto *reason =
                mesh::capture(*geometry, *ids, plan, job->pixels, job->receipt))
          return reject(Outcome::INVALID_REQUEST, reason);
        job->receipt += ",\"handle\":" + quote(handleName(handle));
        job->abandoned = [state, check = ctx.isAbandoned] {
          return state->stopping.load() || check();
        };
        job->wake = ctx.wake;
        if (!job->start())
          return reject(Outcome::BUSY, "resource_limit");
        Step pending;
        pending.status = Step::Status::BACKEND_PENDING;
        return pending;
      }
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

operations::Admission ImageExchange::submitMeshImport(
    const session::Credentials &credentials,
    const operations::OperationRef &ref,
    SceneId scene,
    identity::ObjectHandle handle,
    std::string targetVersion,
    std::string dependencies,
    std::string fileName)
{
  operations::Admission invalid;
  invalid.code = Outcome::INVALID_REQUEST;
  mcp::json::Value evidence;
  if (!importAvailable() || targetVersion.empty() || targetVersion.size() > 256
      || dependencies.size() > mesh::HEADER_CAP || !confinedName(fileName, true)
      || !mesh::detail::boundedMetadata(dependencies))
    return invalid;
  auto state = m_state;
  auto control = state->limits.reservePhysicalMemory(
      IMPORT_CONTROL_BYTES + 4 * mesh::HEADER_CAP);
  if (!control) {
    invalid.code = Outcome::BUSY;
    return invalid;
  }
  if (!mcp::json::parse(dependencies, evidence)
      || evidence.type != mcp::json::Value::Type::ARRAY
      || evidence.a.size() > mesh::FIELD_CAP)
    return invalid;
  for (const auto &entry : evidence.a) {
    if (!entry.isObject() || entry.o.size() != 3)
      return invalid;
    for (const auto *key : {"payload", "handle", "data_version"}) {
      const auto *value = entry.get(key);
      if (!value || !value->isString() || value->s.empty()
          || value->s.size() > 256)
        return invalid;
    }
  }
  auto job = std::make_shared<State::Job>(state->limits.allocator);
  job->state = state;
  job->controlLease = std::move(control);
  job->importing = true;
  job->mesh = true;
  job->sessionId = credentials.sessionId;
  job->name = std::move(fileName);
  job->expectedVersion = std::move(targetVersion);
  job->expectedDependencies = std::move(dependencies);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->jobs.erase(std::remove_if(state->jobs.begin(),
                          state->jobs.end(),
                          [](const auto &weak) { return weak.expired(); }),
        state->jobs.end());
    state->jobs.push_back(job);
  }
  operations::Request request;
  request.kind = "mesh_import";
  request.resourceLease = job->controlLease;
  request.reserveStatus = [reserve = state->limits.reservePhysicalMemory] {
    return reserve(IMPORT_RESULT_BYTES);
  };
  request.target = session::Target::scene(scene);
  request.capability = session::Capability::EDIT;
  request.boundSceneRead = true;
  request.deadlineSeconds = 30.;
  request.canonicalArguments = std::to_string(scene) + ":" + handleName(handle)
      + ":" + job->expectedVersion + ":" + job->expectedDependencies + ":"
      + job->name;
  request.executor = [state, job, scene, handle, integration = m_integration](
                         operations::StepContext &ctx) {
    if (job->committed) {
      Step result;
      result.committed = true;
      try {
        result.outcome = state->edits->synchronizeCommitted(scene);
      } catch (...) {
        result.outcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
      }
      if (job->notificationFailed)
        result.outcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
      result.result = std::move(job->receipt);
      result.resourceLease = job->controlLease;
      result.reserveStatus = [reserve = state->limits.reservePhysicalMemory] {
        return reserve(IMPORT_RESULT_BYTES);
      };
      return result;
    }
    const auto releasePins = [](vsr::scene::Scene *ownedScene) {
      vsr::scene::detail::PreparedPositionBinding::drain(*ownedScene);
    };
    std::unique_ptr<vsr::scene::Scene, decltype(releasePins)> pinDrain(
        &ctx.scene, releasePins);
    const auto fail = [&](Outcome outcome, const char *reason) {
      if (job->storePrepared) {
        if (!state->editsLifetime.expired())
          state->edits->restorations().cancelImage(job->batch);
        job->storePrepared = false;
      }
      job->snapshot.reset();
      if (ctx.cancelRestoration)
        ctx.cancelRestoration();
      return reject(outcome, reason);
    };
    try {
      if (job->future.valid()
          && job->future.wait_for(std::chrono::seconds(0))
              != std::future_status::ready) {
        Step pending;
        pending.status = Step::Status::BACKEND_PENDING;
        return pending;
      }
      if (ctx.abandoned || ctx.isAbandoned())
        return fail(Outcome::CANCELLED, "cancelled");
      if (state->editsLifetime.expired() || state->restoresLifetime.expired())
        return fail(Outcome::BUSY, "resource_limit");
      identity::SceneIdentity *ids = nullptr;
      std::weak_ptr<const void> registration;
      for (const auto &source : state->sources) {
        if (source.lifetime.expired())
          continue;
        if (source.scene == scene) {
          ids = source.identity;
          registration = source.lifetime;
        } else if (auto *foreign = source.identity->resolve(handle))
          return fail(Outcome::INVALID_REQUEST,
              dynamic_cast<vsr::scene::Geometry *>(foreign) ? "foreign_scene"
                                                            : "invalid_handle");
      }
      auto *geometry = ids
          ? dynamic_cast<vsr::scene::Geometry *>(ids->resolve(handle))
          : nullptr;
      if (!geometry || ids->scene() != &ctx.scene)
        return fail(Outcome::INVALID_REQUEST, "invalid_handle");
      if (!integration->imageTrackingAvailable(scene))
        return fail(
            Outcome::INVALID_REQUEST, "host_write_contract_unavailable");
      mcp::json::Value dependencies;
      if (!mcp::json::parse(job->expectedDependencies, dependencies))
        return fail(Outcome::INVALID_REQUEST, "invalid_evidence");
      mesh::CapturePlan plan;
      const auto *reason = mesh::plan(*geometry, plan, false);
      if (reason)
        return fail(std::strcmp(reason, "array_mapped") == 0
                ? Outcome::BUSY
                : Outcome::INVALID_REQUEST,
            reason);
      if ((reason = mesh::current(
               *geometry, *ids, plan, job->expectedVersion, dependencies)))
        return fail(Outcome::CONFLICT, reason);
      if (!state->current())
        return fail(Outcome::INVALID_REQUEST, "file_rejected");
      if (!job->future.valid()) {
        constexpr auto WORK =
            32 * mesh::HEADER_CAP + WORKER_STACK_BYTES + WORKER_GUARD_BYTES;
        job->physicalLease = state->limits.reservePhysicalMemory(WORK);
        if (!job->physicalLease)
          return fail(Outcome::BUSY, "resource_limit");
        {
          std::lock_guard<std::mutex> lock(state->mutex);
          if (WORK > state->limits.transientBytes
              || state->reserved > state->limits.transientBytes - WORK)
            return fail(Outcome::BUSY, "resource_limit");
          state->reserved += WORK;
          job->bound.peak = WORK;
          job->bound.encoded = mesh::PAYLOAD_CAP + mesh::HEADER_CAP + 12;
        }
        job->registration = registration;
        job->abandoned = [state, check = ctx.isAbandoned] {
          return state->stopping.load() || check();
        };
        job->wake = ctx.wake;
        if (!job->start())
          return fail(Outcome::BUSY, "resource_limit");
        Step pending;
        pending.status = Step::Status::BACKEND_PENDING;
        return pending;
      }
      auto decoded = job->future.get();
      job->join();
      if (decoded.outcome != Outcome::OK)
        return decoded;
      if (job->registration.expired() || !state->fileCurrent(*job))
        return fail(Outcome::CONFLICT, "file_rejected");
      mesh::MeshReplacement replacement;
      if ((reason = mesh::prepareMesh(*geometry, job->bundle, replacement)))
        return fail(Outcome::INVALID_REQUEST, reason);
      if (!replacement.changed) {
        Step unchanged;
        unchanged.result = "{\"committed\":false,\"unchanged\":true,\"handle\":"
            + quote(handleName(handle))
            + ",\"target_version\":" + quote(job->expectedVersion)
            + ",\"dependencies\":" + job->expectedDependencies + "}";
        return unchanged;
      }
      const auto retainedBytes = replacement.dependencies.payloadBytes
          + replacement.newBytes
          + (replacement.dependencies.arrayCount + replacement.newCount)
              * (sizeof(vsr::scene::Array) + 4096)
          + sizeof(edit::MeshSnapshot) + 4 * mesh::HEADER_CAP
          + IMPORT_OWNER_BYTES;
      const auto policy = state->restorationPolicy();
      if (retainedBytes > policy.hardBytes) {
        Step rejected;
        rejected.outcome = Outcome::BUSY;
        rejected.result = policy.refusal(retainedBytes);
        return rejected;
      }
      vsr::scene::detail::PreparedPositionBinding::drain(ctx.scene);
      std::shared_ptr<SessionLedger> ledger;
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        auto &entry = state->ledgers[job->sessionId];
        if (!entry)
          entry = std::make_shared<SessionLedger>();
        ledger = entry;
      }
      const auto charge =
          [&](const std::shared_ptr<SessionLedger> &account,
              std::uint64_t limit) -> std::shared_ptr<SessionCharge> {
        auto result = std::make_shared<SessionCharge>();
        result->ledger = account;
        std::lock_guard<std::mutex> lock(account->mutex);
        if (retainedBytes > limit || account->bytes > limit - retainedBytes)
          return {};
        account->bytes += retainedBytes;
        result->bytes = retainedBytes;
        return result;
      };
      auto sessionCharge = charge(ledger, UINT64_MAX);
      auto retentionCharge = charge(state->retentionLedger, UINT64_MAX);
      if (!sessionCharge || !retentionCharge) {
        // No retained storage has been allocated. Remove partial reservations
        // before reporting the originating session's actual available ceiling.
        sessionCharge.reset();
        retentionCharge.reset();
        return fail(Outcome::BUSY, "resource_limit");
      }
      auto snapshot = std::make_shared<edit::MeshSnapshot>();
      job->snapshot = snapshot;
      snapshot->controlOwnership = operations::ResultOwnership(
          [reserve = state->limits.reservePhysicalMemory] {
            return reserve(IMPORT_STORE_BYTES);
          });
      snapshot->scopeLease =
          state->limits.reservePhysicalMemory(IMPORT_STORE_BYTES);
      snapshot->physicalLease =
          state->limits.reservePhysicalMemory(retainedBytes);
      if (!snapshot->scopeLease || !snapshot->physicalLease)
        return fail(Outcome::BUSY, "resource_limit");
      snapshot->reserveResult = [reserve =
                                        state->limits.reservePhysicalMemory] {
        return reserve(IMPORT_RESULT_BYTES);
      };
      snapshot->sessionLease = sessionCharge;
      snapshot->registration = registration;
      snapshot->scene = &ctx.scene;
      snapshot->target = handle;
      snapshot->retainedBytes = retainedBytes;
      snapshot->pinOwner = std::make_shared<int>(0);
      geometry = dynamic_cast<vsr::scene::Geometry *>(ids->resolve(handle));
      if ((reason = mesh::currentPlan(
               geometry, *ids, plan, job->expectedVersion, dependencies)))
        return fail(reason && std::strcmp(reason, "array_mapped") == 0
                ? Outcome::BUSY
                : Outcome::CONFLICT,
            reason ? reason : "stale_target_version");
      snapshot->dependencies.reserve(plan.arrayCount + replacement.newCount);
      std::vector<std::weak_ptr<vsr::scene::ArrayLifetime>> pins;
      pins.reserve(plan.arrayCount + replacement.newCount);
      for (std::size_t i = 0; i < plan.arrayCount; ++i) {
        pins.push_back(plan.arrays[i]->lifetime());
        snapshot->dependencies.push_back(
            {pins.back(), *plan.arrays[i]->dataWriteSequence()});
      }
      if (!mesh::boundedParameters(*geometry))
        return fail(Outcome::INVALID_REQUEST, "mesh_parameter_limit");
      snapshot->bindings.reserve(mesh::FIELD_CAP);
      for (std::size_t i = 0; i < plan.fieldCount; ++i) {
        const auto *field = plan.fields[i];
        snapshot->bindings.push_back({field->name(),
            *field,
            plan.arrays[plan.aliases[i]]->lifetime(),
            {},
            false,
            true});
      }
      for (const auto &field : replacement.fields)
        if (!geometry->parameter(field.name.c_str()))
          snapshot->bindings.push_back(
              {vsr::core::Token(field.name.c_str()), {}, {}, {}, false, true});
      const auto batchId = state->edits->newBatchId();
      if (!batchId)
        return fail(Outcome::BUSY, "resource_limit");
      job->batch = {state->launchId, *batchId};
      if (!ctx.reserveRestoration
          || !ctx.reserveRestoration(
              retainedBytes, state->launchId, *batchId)) {
        Step rejected;
        rejected.outcome = Outcome::BUSY;
        rejected.result = policy.refusal(retainedBytes);
        return rejected;
      }
      edit::RestorationRecord record;
      record.ref = job->batch;
      record.scene = scene;
      record.image = snapshot;
      record.mesh = snapshot;
      if (!state->edits->restorations().prepareImage(std::move(record)))
        return fail(Outcome::BUSY, "resource_limit");
      job->storePrepared = true;
      state->edits->syncStates().prepareScene(scene);
      using StagedArray = vsr::scene::detail::PreparedPositionBinding;
      std::vector<std::unique_ptr<StagedArray>> stagedArrays;
      stagedArrays.reserve(replacement.newCount);
      std::array<vsr::scene::Array *, mesh::FIELD_CAP> payloadArrays{};
      for (std::size_t p = 0; p < job->bundle.arrays.size(); ++p) {
        if (!replacement.needed[p])
          continue;
        if (replacement.reuse[p]) {
          payloadArrays[p] =
              const_cast<vsr::scene::Array *>(replacement.reuse[p]);
          continue;
        }
        const auto &payload = job->bundle.arrays[p];
        stagedArrays.push_back(std::make_unique<StagedArray>(ctx.scene,
            payload.elementType,
            payload.count,
            job->bundle.data(payload)));
        auto *array = stagedArrays.back()->array();
        if (!array)
          return fail(Outcome::CONFLICT, "stale_dependencies");
        payloadArrays[p] = array;
        pins.push_back(array->lifetime());
        snapshot->dependencies.push_back(
            {pins.back(), *array->dataWriteSequence()});
      }
      struct RetentionOwners
      {
        std::shared_ptr<const void> physical, session, global;
      };
      auto owners = std::make_shared<RetentionOwners>(RetentionOwners{
          snapshot->physicalLease, sessionCharge, retentionCharge});
      vsr::scene::detail::PreparedPositionBinding::retain(
          ctx.scene, snapshot->pinOwner, owners, std::move(pins));
      job->receipt.reserve(mesh::HEADER_CAP);
      if (!ctx.prepareActivityResult
          || !ctx.prepareActivityResult(16384, snapshot->reserveResult))
        return fail(Outcome::BUSY, "resource_limit");
      if (!ctx.beforeWrite)
        return fail(Outcome::FORBIDDEN, "authority_unavailable");
      const auto authority = ctx.beforeWrite();
      if (authority != Outcome::OK)
        return fail(authority, "authority_withdrawn");
      // No Scene notifications before publication. Detached preparation and
      // reserved identity remain unresolvable through either final guard.
      const auto discardIdentities = [&stagedArrays](
                                         identity::SceneIdentity *ids) {
        for (const auto &array : stagedArrays)
          if (array->array())
            mesh::MeshCommitAccess::discard(*ids, *array->array());
      };
      // Borrowed identity owner: the deleter only unwinds reserved entries,
      // never deletes the host identity service.
      std::unique_ptr<identity::SceneIdentity, decltype(discardIdentities)>
          identityStage(ids, discardIdentities);
      std::vector<identity::ObjectHandle> newHandles;
      newHandles.reserve(stagedArrays.size());
      for (std::size_t i = 0; i < stagedArrays.size(); ++i) {
        stagedArrays[i]->prepareSlot(i, stagedArrays.size());
        newHandles.push_back(
            mesh::MeshCommitAccess::prepare(*ids, *stagedArrays[i]->array()));
      }
      // Pool reservation can relocate Arrays; final evidence is reacquired.
      geometry = dynamic_cast<vsr::scene::Geometry *>(ids->resolve(handle));
      mesh::CapturePlan finalPlan;
      if ((reason = mesh::currentPlan(
               geometry, *ids, finalPlan, job->expectedVersion, dependencies)))
        return fail(reason && std::strcmp(reason, "array_mapped") == 0
                ? Outcome::BUSY
                : Outcome::CONFLICT,
            reason ? reason : "stale_target_version");
      // Rebuild after every callback/pool relocation; no plan pointers from
      // before host reservations may reach publication.
      if ((reason = mesh::prepareMesh(*geometry, job->bundle, replacement)))
        return fail(Outcome::INVALID_REQUEST, reason);
      vsr::scene::detail::PreparedMeshBinding transaction(*geometry);
      std::vector<vsr::scene::Array *> oldArrays, addedArrays;
      oldArrays.reserve(finalPlan.arrayCount);
      addedArrays.reserve(stagedArrays.size());
      for (std::size_t i = 0; i < finalPlan.arrayCount; ++i)
        oldArrays.push_back(
            const_cast<vsr::scene::Array *>(finalPlan.arrays[i]));
      for (const auto &array : stagedArrays)
        addedArrays.push_back(array->array());
      for (std::size_t p = 0; p < job->bundle.arrays.size(); ++p)
        if (replacement.reuse[p])
          payloadArrays[p] =
              const_cast<vsr::scene::Array *>(replacement.reuse[p]);
      for (auto &binding : snapshot->bindings) {
        // Host preparation may edit unversioned UI metadata without changing
        // binding evidence. Capture the actual prepublication removed state.
        if (binding.prior)
          binding.prior = *geometry->parameter(binding.name);
        const auto found = std::find_if(replacement.fields.begin(),
            replacement.fields.end(),
            [&](const auto &field) {
              return binding.name.str() == field.name;
            });
        auto *array = found == replacement.fields.end() ? nullptr
            : found->retained ? const_cast<vsr::scene::Array *>(found->retained)
                              : payloadArrays[found->payload];
        binding.present = array;
        binding.enabled =
            found == replacement.fields.end() ? true : found->enabled;
        if (array)
          binding.after = array->lifetime();
        transaction.bind(binding.name, array, binding.enabled);
      }
      transaction.prepareNotifications();
      std::string resulting = "[";
      std::vector<const vsr::scene::Array *> postArrays;
      postArrays.reserve(replacement.fields.size());
      for (const auto &field : replacement.fields) {
        const auto *array =
            field.retained ? field.retained : payloadArrays[field.payload];
        if (std::find(postArrays.begin(), postArrays.end(), array)
            == postArrays.end())
          postArrays.push_back(array);
      }
      for (std::size_t i = 0; i < postArrays.size(); ++i) {
        const auto *array = postArrays[i];
        auto h = ids->handleOf(array);
        for (std::size_t n = 0; n < stagedArrays.size(); ++n)
          if (array == stagedArrays[n]->array())
            h = newHandles[n];
        const auto dataVersion = "array:" + std::to_string(h.launch.activation)
            + "." + std::to_string(h.launch.nonce) + "."
            + std::to_string(h.domain) + "." + std::to_string(h.serial) + "."
            + std::to_string(*array->dataWriteSequence());
        if (i)
          resulting += ',';
        resulting += "{\"payload\":\"a" + std::to_string(i)
            + "\",\"handle\":" + quote(handleName(h))
            + ",\"data_version\":" + quote(dataVersion) + "}";
      }
      job->receipt = "{\"handle\":" + quote(handleName(handle))
          + ",\"batch_ref\":" + edit::detail::batchRefJson(job->batch)
          + ",\"target_version\":"
          + quote(mesh::targetVersion(*geometry, handle, true))
          + ",\"dependencies\":" + resulting + "],\"committed\":true"
          + ",\"restoration_policy\":" + policy.receipt(retainedBytes);
      if (job->receipt.size() + 256 > 16384)
        return fail(Outcome::BUSY, "resource_limit");
      job->receipt.reserve(job->receipt.size() + 256);
      geometry = dynamic_cast<vsr::scene::Geometry *>(ids->resolve(handle));
      mesh::CapturePlan safePlan;
      if ((reason = mesh::currentPlan(
               geometry, *ids, safePlan, job->expectedVersion, dependencies))
          || !state->fileCurrent(*job) || registration.expired())
        return fail(reason && std::strcmp(reason, "array_mapped") == 0
                ? Outcome::BUSY
                : Outcome::CONFLICT,
            reason ? reason : "stale_target_version");
      const auto expectedCore = geometry->lastParameterChange();
      std::vector<std::size_t> expectedIndices;
      expectedIndices.reserve(snapshot->dependencies.size());
      for (const auto &dependency : snapshot->dependencies)
        expectedIndices.push_back(dependency.lifetime.lock()->array->index());
      const auto finalAuthority = ctx.beforeWrite();
      if (finalAuthority != Outcome::OK)
        return fail(finalAuthority, "authority_withdrawn");
      geometry = dynamic_cast<vsr::scene::Geometry *>(ids->resolve(handle));
      if (!geometry || geometry->lastParameterChange() != expectedCore
          || !transaction.isCurrent(*geometry)
          || (!stagedArrays.empty() && !stagedArrays.front()->slotAvailable())
          || registration.expired() || !state->fileCurrent(*job)
          || !integration->imageTrackingAvailable(scene)
          || (reason = mesh::dependenciesCurrent(*snapshot)))
        return fail(reason && std::strcmp(reason, "array_mapped") == 0
                ? Outcome::BUSY
                : Outcome::CONFLICT,
            reason ? reason : "stale_target_version");
      for (std::size_t i = 0; i < snapshot->dependencies.size(); ++i)
        if (snapshot->dependencies[i].lifetime.lock()->array->index()
            != expectedIndices[i])
          return fail(Outcome::CONFLICT, "stale_dependencies");
      // Pool backing can relocate at the final host guard, without changing
      // logical indices. Notification pointers are reacquired without
      // allocation.
      for (std::size_t i = 0; i < oldArrays.size(); ++i)
        oldArrays[i] = snapshot->dependencies[i].lifetime.lock()->array;
      for (std::size_t i = 0; i < stagedArrays.size(); ++i) {
        stagedArrays[i]->publish();
        addedArrays[i] = stagedArrays[i]->array();
      }
      transaction.commit(*geometry);
      // Publication transfers all identity ownership out of the abort guard.
      (void)identityStage.release();
      job->committed = true;
      snapshot->targetVersion = geometry->lastParameterChange();
      mesh::MeshCommitAccess::committed(*ids, *geometry);
      state->edits->restorations().publishImage(job->batch);
      job->storePrepared = false;
      if (ctx.publishRestoration)
        ctx.publishRestoration();
      state->pruneRestorations();
      job->notificationFailed = mesh::notify(ctx.scene,
          *geometry,
          transaction,
          snapshot->bindings,
          addedArrays,
          oldArrays);
      const auto revision = ids->sceneRevision();
      char suffix[256];
      std::snprintf(suffix,
          sizeof(suffix),
          ",\"scene_revision\":\"%llu.%llu.%llu.%llu\"}",
          (unsigned long long)revision.launch.activation,
          (unsigned long long)revision.launch.nonce,
          (unsigned long long)revision.domain,
          (unsigned long long)revision.value);
      job->receipt.append(suffix);
      job->snapshot.reset();
      job->releaseTransient();
      try {
        state->edits->syncStates().beginHook(scene);
      } catch (...) {
        job->notificationFailed = true;
      }
      Step committed;
      committed.committed = true;
      committed.status = Step::Status::SYNCHRONIZING;
      return committed;
    } catch (...) {
      if (job->committed) {
        job->notificationFailed = true;
        Step committed;
        committed.committed = true;
        committed.status = Step::Status::SYNCHRONIZING;
        return committed;
      }
      return fail(Outcome::BUSY, "resource_limit");
    }
  };
  return m_engine->submit(credentials, ref, std::move(request));
}

operations::Admission ImageExchange::submitImport(
    const session::Credentials &credentials,
    const operations::OperationRef &ref,
    SceneId scene,
    identity::ObjectHandle handle,
    std::string dataVersion,
    std::string fileName)
{
  if (!importAvailable() || dataVersion.empty() || dataVersion.size() > 256
      || !confinedName(fileName)) {
    operations::Admission admission;
    admission.code = Outcome::INVALID_REQUEST;
    return admission;
  }
  auto state = m_state;
  // Cover controls before constructing the Job or its owning strings.
  auto controlLease = state->limits.reservePhysicalMemory(IMPORT_CONTROL_BYTES);
  if (!controlLease) {
    operations::Admission admission;
    admission.code = Outcome::BUSY;
    return admission;
  }
  auto job = std::make_shared<State::Job>(state->limits.allocator);
  job->state = state;
  job->controlLease = std::move(controlLease);
  job->importing = true;
  job->sessionId = credentials.sessionId;
  job->name = std::move(fileName);
  job->expectedVersion = std::move(dataVersion);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->jobs.erase(std::remove_if(state->jobs.begin(),
                          state->jobs.end(),
                          [](const auto &weak) { return weak.expired(); }),
        state->jobs.end());
    state->jobs.push_back(job);
  }
  operations::Request request;
  request.kind = "image_import";
  request.resourceLease = job->controlLease;
  request.reserveStatus = [reserve = state->limits.reservePhysicalMemory] {
    return reserve(IMPORT_RESULT_BYTES);
  };
  request.target = session::Target::scene(scene);
  request.capability = session::Capability::EDIT;
  request.boundSceneRead = true;
  request.deadlineSeconds = 30.;
  request.canonicalArguments = std::to_string(scene) + ":" + handleName(handle)
      + ":" + job->expectedVersion + ":" + job->name;
  request.executor = [state, job, scene, handle, integration = m_integration](
                         operations::StepContext &ctx) {
    if (job->committed) {
      Step result;
      result.committed = true;
      try {
        result.outcome = state->edits->synchronizeCommitted(scene);
      } catch (...) {
        result.outcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
      }
      if (job->notificationFailed)
        result.outcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
      result.result = std::move(job->receipt);
      result.resourceLease = job->controlLease;
      result.reserveStatus = [reserve = state->limits.reservePhysicalMemory] {
        return reserve(IMPORT_RESULT_BYTES);
      };
      return result;
    }
    const auto fail = [&](Outcome outcome, const char *reason) {
      if (ctx.cancelRestoration)
        ctx.cancelRestoration();
      return reject(outcome, reason);
    };
    try {
      // Even rejected live state must wait for bounded worker cleanup before
      // dropping its owner under the engine lock (worker checks that lock).
      if (job->future.valid()
          && job->future.wait_for(std::chrono::seconds(0))
              != std::future_status::ready) {
        Step pending;
        pending.status = Step::Status::BACKEND_PENDING;
        return pending;
      }
      if (ctx.abandoned || ctx.isAbandoned())
        return Step{};
      if (state->editsLifetime.expired() || state->restoresLifetime.expired())
        return fail(Outcome::BUSY, "resource_limit");
      identity::SceneIdentity *ids = nullptr;
      std::weak_ptr<const void> registration;
      for (const auto &source : state->sources) {
        if (source.lifetime.expired())
          continue;
        if (source.scene == scene) {
          ids = source.identity;
          registration = source.lifetime;
        } else if (auto *foreign = source.identity->resolve(handle))
          return fail(Outcome::INVALID_REQUEST,
              dynamic_cast<vsr::scene::Array *>(foreign) ? "foreign_scene"
                                                         : "invalid_handle");
      }
      auto *array = ids
          ? dynamic_cast<vsr::scene::Array *>(ids->resolve(handle))
          : nullptr;
      if (!array || ids->scene() != &ctx.scene)
        return fail(Outcome::INVALID_REQUEST, "invalid_handle");
      if (!integration->imageTrackingAvailable(scene))
        return fail(
            Outcome::INVALID_REQUEST, "host_write_contract_unavailable");
      if (!array->isImage())
        return fail(Outcome::INVALID_REQUEST, "unsupported_image");
      if (array->isMapped())
        return fail(Outcome::BUSY, "image_mapped");
      const auto version = ids->imageDataVersion(handle);
      if (!version || *version != job->expectedVersion)
        return fail(Outcome::CONFLICT, "stale_data_version");
      if (!state->current())
        return fail(Outcome::INVALID_REQUEST, "file_rejected");
      if (job->future.valid()) {
        if (job->future.wait_for(std::chrono::seconds(0))
            != std::future_status::ready) {
          Step pending;
          pending.status = Step::Status::BACKEND_PENDING;
          return pending;
        }
        auto decoded = job->future.get();
        job->join();
        if (decoded.outcome != Outcome::OK) {
          if (ctx.cancelRestoration)
            ctx.cancelRestoration();
          return decoded;
        }
        if (job->registration.expired() || job->snapshot->scene != &ctx.scene
            || array->dim(0) != job->width || array->dim(1) != job->height
            || array->elementSize() != job->channels
            || array->elementType() != job->elementType)
          return fail(Outcome::CONFLICT, "stale_data_version");
        if (array->data()
            && std::memcmp(array->data(), job->pixels.data(), job->bound.texels)
                == 0) {
          if (ctx.cancelRestoration)
            ctx.cancelRestoration();
          state->edits->restorations().cancelImage(job->batch);
          job->storePrepared = false;
          job->snapshot.reset();
          job->releaseTransient();
          Step unchanged;
          unchanged.result =
              "{\"committed\":false,\"unchanged\":true,\"handle\":"
              + quote(handleName(handle))
              + ",\"data_version\":" + quote(job->expectedVersion) + "}";
          return unchanged;
        }
        // All fallible control/store/receipt preparation precedes map(), the
        // sole ordinary write-sequence reservation boundary for this write.
        job->receipt = "{\"handle\":" + quote(handleName(handle))
            + ",\"batch_ref\":" + edit::detail::batchRefJson(job->batch) + ","
            + bindings(*ids, *array) + ",\"restoration_policy\":"
            + state->restorationPolicy().receipt(job->bound.texels
                + sizeof(edit::ImageSnapshot) + IMPORT_OWNER_BYTES);
        job->receipt.reserve(job->receipt.size() + 512);
        if (!ctx.prepareActivityResult
            || !ctx.prepareActivityResult(
                job->receipt.size() + 512, job->snapshot->reserveResult))
          return fail(Outcome::BUSY, "resource_limit");
        state->edits->syncStates().prepareScene(scene);
        if (ctx.beforeWrite) {
          const auto authority = ctx.beforeWrite();
          if (authority != Outcome::OK) {
            if (ctx.cancelRestoration)
              ctx.cancelRestoration();
            Step rejected;
            rejected.outcome = authority;
            return rejected;
          }
        } else if (ctx.isAbandoned())
          return Step{};
        void *destination = nullptr;
        try {
          destination = array->map();
        } catch (...) {
          // Reservation remains spent if pre-map notification fails; no
          // pointer was exposed and no completed token may be published.
          return fail(Outcome::BUSY, "resource_limit");
        }
        if (!destination)
          return fail(Outcome::BUSY, "resource_limit");
        std::memcpy(
            job->snapshot->bytes.data(), destination, job->bound.texels);
        std::memcpy(destination, job->pixels.data(), job->bound.texels);
        job->committed = true;
        try {
          array->unmap();
        } catch (...) {
          // Array records completedWrite before delegate notification. A
          // downstream failure cannot turn a completed write into rollback.
          job->notificationFailed = true;
        }
        job->snapshot->completedWrite = *array->dataWriteSequence();
        state->edits->restorations().publishImage(job->batch);
        job->storePrepared = false;
        if (ctx.publishRestoration)
          ctx.publishRestoration();
        state->pruneRestorations();
        job->snapshot.reset();
        job->releaseTransient();
        const auto revision = ids->sceneRevision();
        char detail[384];
        std::snprintf(detail,
            sizeof(detail),
            ",\"data_version\":\"image:%llu.%llu.%llu.%llu.%llu\","
            "\"scene_revision\":\"%llu.%llu.%llu.%llu\"}",
            (unsigned long long)handle.launch.activation,
            (unsigned long long)handle.launch.nonce,
            (unsigned long long)handle.domain,
            (unsigned long long)handle.serial,
            (unsigned long long)*array->dataWriteSequence(),
            (unsigned long long)revision.launch.activation,
            (unsigned long long)revision.launch.nonce,
            (unsigned long long)revision.domain,
            (unsigned long long)revision.value);
        job->receipt.append(detail); // capacity reserved before map()
        try {
          state->edits->syncStates().beginHook(scene);
        } catch (...) {
          job->notificationFailed = true;
        }
        Step committed;
        committed.committed = true;
        committed.status = Step::Status::SYNCHRONIZING;
        return committed;
      }
      auto b =
          bounds(array->dim(0), array->dim(1), array->elementSize(), false);
      if (!b || b->texels > std::numeric_limits<std::size_t>::max() - b->peak)
        return fail(Outcome::BUSY, "resource_limit");
      b->peak += b->texels; // E + D + W + S, not two physical snapshots.
      const auto retainedBytes =
          b->texels + sizeof(edit::ImageSnapshot) + IMPORT_OWNER_BYTES;
      const auto policy = state->restorationPolicy();
      if (retainedBytes > policy.hardBytes) {
        Step rejected;
        rejected.outcome = Outcome::BUSY;
        rejected.result = policy.refusal(retainedBytes);
        return rejected;
      }
      std::shared_ptr<SessionLedger> ledger;
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        auto &entry = state->ledgers[job->sessionId];
        if (!entry)
          entry = std::make_shared<SessionLedger>();
        ledger = entry;
      }
      auto charge = std::make_shared<SessionCharge>();
      charge->ledger = ledger;
      bool sessionReserved = false;
      {
        std::lock_guard<std::mutex> lock(ledger->mutex);
        if (ledger->bytes <= UINT64_MAX - b->texels) {
          ledger->bytes += b->texels;
          charge->bytes = b->texels;
          sessionReserved = true;
        }
      }
      if (!sessionReserved)
        return fail(Outcome::BUSY, "resource_limit");
      job->snapshot =
          std::make_shared<edit::ImageSnapshot>(state->limits.allocator);
      job->snapshot->controlOwnership = operations::ResultOwnership(
          [reserve = state->limits.reservePhysicalMemory] {
            return reserve(IMPORT_STORE_BYTES);
          });
      job->snapshot->scopeLease =
          state->limits.reservePhysicalMemory(IMPORT_STORE_BYTES);
      if (!job->snapshot->scopeLease)
        return fail(Outcome::BUSY, "resource_limit");
      job->snapshot->reserveResult =
          [reserve = state->limits.reservePhysicalMemory] {
            return reserve(IMPORT_RESULT_BYTES);
          };
      job->snapshot->sessionLease = std::move(charge);
      job->snapshot->handle = handle;
      job->snapshot->scene = &ctx.scene;
      job->snapshot->registration = registration;
      job->registration = registration;
      const auto batchId = state->edits->newBatchId();
      if (!batchId)
        return fail(Outcome::BUSY, "resource_limit");
      job->batch = {state->launchId, *batchId};
      if (!ctx.reserveRestoration
          || !ctx.reserveRestoration(
              retainedBytes, state->launchId, *batchId)) {
        Step rejected;
        rejected.outcome = Outcome::BUSY;
        rejected.result = policy.refusal(retainedBytes);
        return rejected;
      }
      auto globalCharge = std::make_shared<SessionCharge>();
      globalCharge->ledger = state->retentionLedger;
      {
        std::lock_guard<std::mutex> lock(state->retentionLedger->mutex);
        if (state->retentionLedger->bytes > UINT64_MAX - b->texels)
          return fail(Outcome::BUSY, "resource_limit");
        state->retentionLedger->bytes += b->texels;
        globalCharge->bytes = b->texels;
      }
      struct SnapshotCharges
      {
        std::shared_ptr<const void> session, global;
      };
      job->snapshot->sessionLease =
          std::make_shared<SnapshotCharges>(SnapshotCharges{
              job->snapshot->sessionLease, std::move(globalCharge)});
      edit::RestorationRecord record;
      record.ref = job->batch;
      record.scene = scene;
      record.image = job->snapshot;
      if (!state->edits->restorations().prepareImage(std::move(record)))
        return fail(Outcome::BUSY, "resource_limit");
      job->storePrepared = true;
      if (!ctx.prepareActivityResult
          || !ctx.prepareActivityResult(64, job->snapshot->reserveResult))
        return fail(Outcome::BUSY, "resource_limit");
      bool transientReserved = false;
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (b->peak <= state->limits.transientBytes
            && state->reserved <= state->limits.transientBytes - b->peak) {
          state->reserved += b->peak;
          job->bound = *b;
          transientReserved = true;
        }
      }
      if (!transientReserved)
        return fail(Outcome::BUSY, "resource_limit");
      if (state->limits.reservePhysicalMemory) {
        job->snapshot->physicalLease =
            state->limits.reservePhysicalMemory(b->texels);
        if (!job->snapshot->physicalLease)
          return fail(Outcome::BUSY, "resource_limit");
        // peak = E + D + W + S; S has one lease above. Fixed owners borrow
        // 106496 bytes from W, leaving scratch = peak - S - owners. The
        // remaining W exceeds F+1 filtered bytes + 128 KiB inflate arena +
        // 512 KiB stack + 64 KiB guard by 221183 + 3*F bytes. This arithmetic
        // does not establish a bound on platform TLS allocation.
        job->physicalLease = state->limits.reservePhysicalMemory(
            b->peak - b->texels - IMPORT_OWNER_BYTES);
        if (!job->physicalLease)
          return fail(Outcome::BUSY, "resource_limit");
      }
      job->snapshot->bytes.resize(b->texels);
      job->width = array->dim(0);
      job->height = array->dim(1);
      job->channels = array->elementSize();
      job->elementType = array->elementType();
      job->abandoned = [state, check = ctx.isAbandoned] {
        return state->stopping.load() || check();
      };
      job->wake = ctx.wake;
      if (!job->start())
        return fail(Outcome::BUSY, "resource_limit");
      Step pending;
      pending.status = Step::Status::BACKEND_PENDING;
      return pending;
    } catch (const std::bad_alloc &) {
      if (job->committed) {
        Step committed;
        committed.committed = true;
        committed.outcome = Outcome::APPLIED_WITH_SYNC_FAILURE;
        return committed;
      }
      return fail(Outcome::BUSY, "resource_limit");
    } catch (const std::system_error &) {
      return fail(Outcome::BUSY, "resource_limit");
    }
  };
  return m_engine->submit(credentials, ref, std::move(request));
}

} // namespace vsr::integration::image

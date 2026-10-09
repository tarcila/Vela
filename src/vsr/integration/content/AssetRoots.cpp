// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_integration
#include "vsr/integration/content/AssetRoots.hpp"
// std
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
// linux
#ifdef __linux__
#include <linux/openat2.h>
#include <sys/syscall.h>
#endif
// posix
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace vsr::integration::content {

struct AssetRoots::Root
{
  ~Root();
  std::atomic<bool> approved{true};
  int fd{-1};
};

struct AssetRoots::Snapshot::Currency
{
#ifndef _WIN32
  struct stat stat{};
#endif
};

namespace {
#ifndef _WIN32
class File
{
 public:
  explicit File(int fd = -1) : m_fd(fd) {}
  ~File();
  VSR_NOT_COPYABLE(File)
  VSR_NOT_MOVEABLE(File)
  int fd() const;
  int release();

 private:
  int m_fd;
};

File::~File()
{
  if (m_fd >= 0)
    close(m_fd);
}

int File::fd() const
{
  return m_fd;
}

int File::release()
{
  const auto fd = m_fd;
  m_fd = -1;
  return fd;
}

bool validPath(const std::string &path)
{
  if (path.empty() || path.size() > AssetRoots::MAX_PATH_BYTES
      || path.front() == '/' || path.find('\0') != std::string::npos)
    return false;
  size_t start = 0;
  while (start < path.size()) {
    const auto end = path.find('/', start);
    const auto count = (end == std::string::npos ? path.size() : end) - start;
    if (!count || (count == 1 && path[start] == '.')
        || (count == 2 && path.compare(start, count, "..") == 0))
      return false;
    if (end == std::string::npos)
      return true;
    start = end + 1;
  }
  return false;
}

int openBeneath(int root, const std::string &path, bool directory = false)
{
  if (!validPath(path))
    return -1;
#ifdef __linux__
  struct open_how how{};
  how.flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK
      | (directory ? O_DIRECTORY : 0);
  how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS;
  // One kernel resolution also rejects intermediate directory-rename races.
  // No weaker fallback on ENOSYS/EAGAIN or unsupported platforms.
  return int(syscall(SYS_openat2, root, path.c_str(), &how, sizeof(how)));
#else
  return -1;
#endif
}

bool sameCurrency(const struct stat &a, const struct stat &b)
{
#ifdef __APPLE__
  const auto &am = a.st_mtimespec, &bm = b.st_mtimespec;
  const auto &ac = a.st_ctimespec, &bc = b.st_ctimespec;
#else
  const auto &am = a.st_mtim, &bm = b.st_mtim;
  const auto &ac = a.st_ctim, &bc = b.st_ctim;
#endif
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_size == b.st_size
      && am.tv_sec == bm.tv_sec && am.tv_nsec == bm.tv_nsec
      && ac.tv_sec == bc.tv_sec && ac.tv_nsec == bc.tv_nsec;
}
#endif
} // namespace

AssetRoots::Root::~Root()
{
#ifndef _WIN32
  if (fd >= 0)
    close(fd);
#endif
}

AssetRoots::Snapshot::Snapshot(
    std::shared_ptr<CapacityArena> arena, CapacityArena::Charge control)
    : m_control(std::move(control)),
      m_bytes(CapacityAllocator<std::uint8_t>(std::move(arena)))
{}

AssetRoots::Snapshot::~Snapshot()
{
#ifndef _WIN32
  if (m_file >= 0)
    close(m_file);
#endif
}

const AssetRoots::Bytes &AssetRoots::Snapshot::bytes() const
{
  return m_bytes;
}

bool AssetRoots::Snapshot::metadataCurrent() const
{
#ifndef _WIN32
  if (!m_root || !m_root->approved.load() || !m_currency || m_file < 0)
    return false;
  struct stat held{}, named{};
  File source(openBeneath(m_root->fd, m_relative));
  return source.fd() >= 0 && fstat(m_file, &held) == 0
      && fstat(source.fd(), &named) == 0 && sameCurrency(m_currency->stat, held)
      && sameCurrency(m_currency->stat, named) && m_root->approved.load();
#else
  return false;
#endif
}

bool AssetRoots::Snapshot::isCurrent(
    const std::function<bool()> &cancelled) const
{
#ifndef _WIN32
  if (!m_root || !m_root->approved.load() || !m_currency || m_file < 0)
    return false;
  struct stat held{}, named{};
  File source(openBeneath(m_root->fd, m_relative));
  if (source.fd() < 0 || fstat(m_file, &held) != 0
      || fstat(source.fd(), &named) != 0
      || !sameCurrency(m_currency->stat, held)
      || !sameCurrency(m_currency->stat, named))
    return false;
  // Check the actual consumed bytes as well as metadata: timestamp granularity
  // is not a content identity. This bounded read belongs on the preparation
  // worker, not in an unbounded viewer callback.
  std::array<std::uint8_t, 64u << 10> buffer;
  // The original descriptor is the consumed source. A newly confined named
  // descriptor is independently checked: neither metadata nor checking only
  // the path is a substitute for currency of the actual consumed file.
  for (const auto fd : {m_file, source.fd()}) {
    size_t offset = 0;
    while (offset < m_bytes.size()) {
      if (cancelled && cancelled())
        return false;
      const auto count = std::min(buffer.size(), m_bytes.size() - offset);
      const auto read = pread(fd, buffer.data(), count, off_t(offset));
      if (read < 0 && errno == EINTR)
        continue;
      if (read <= 0
          || std::memcmp(buffer.data(), m_bytes.data() + offset, size_t(read)))
        return false;
      offset += size_t(read);
    }
  }
  return m_root->approved.load() && fstat(m_file, &held) == 0
      && fstat(source.fd(), &named) == 0 && sameCurrency(m_currency->stat, held)
      && sameCurrency(m_currency->stat, named);
#else
  return false;
#endif
}

AssetRoots::~AssetRoots()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  for (const auto &root : m_roots)
    root->approved.store(false);
}

void AssetRoots::setCapacityArena(std::shared_ptr<CapacityArena> arena)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_roots.empty())
    throw std::runtime_error("Root allocation authority requires idle roots");
  m_roots = Roots(CapacityAllocator<std::shared_ptr<Root>>(arena));
  m_arena = std::move(arena);
}

std::optional<size_t> AssetRoots::approve(const std::string &directory)
{
  std::lock_guard<std::mutex> lock(m_mutex);
#ifndef _WIN32
  if (m_roots.size() >= MAX_ROOTS || directory.empty()
      || directory.front() != '/' || directory.size() > MAX_PATH_BYTES)
    return {};
  File slash(open("/", O_RDONLY | O_CLOEXEC | O_DIRECTORY));
  File root(directory == "/"
          ? fcntl(slash.fd(), F_DUPFD_CLOEXEC, 0)
          : openBeneath(slash.fd(), directory.substr(1), true));
  if (root.fd() < 0)
    return {};
  struct stat stat{};
  if (fstat(root.fd(), &stat) != 0 || !S_ISDIR(stat.st_mode))
    return {};
  for (const auto &existing : m_roots) {
    struct stat other{};
    if (fstat(existing->fd, &other) == 0 && stat.st_dev == other.st_dev
        && stat.st_ino == other.st_ino)
      return {};
  }
  auto approval = std::allocate_shared<Root>(CapacityAllocator<Root>(m_arena));
  approval->fd = root.release();
  m_roots.push_back(std::move(approval));
  return m_roots.size() - 1;
#else
  return {};
#endif
}

bool AssetRoots::revoke(size_t root)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (root >= m_roots.size())
    return false;
  m_roots[root]->approved.store(false);
  return true;
}

size_t AssetRoots::size() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_roots.size();
}

std::shared_ptr<AssetRoots::Snapshot> AssetRoots::read(size_t root,
    const std::string &relative,
    size_t byteLimit,
    const std::function<bool()> &cancelled,
    std::shared_ptr<CapacityArena> arena) const
{
#ifndef _WIN32
  std::shared_ptr<Root> approved;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (root >= m_roots.size())
      return {};
    approved = m_roots[root];
  }
  if ((cancelled && cancelled()) || !approved->approved.load() || !byteLimit
      || byteLimit > MAX_INPUT_BYTES)
    return {};
  File file(openBeneath(approved->fd, relative));
  struct stat before{};
  if (file.fd() < 0 || fstat(file.fd(), &before) != 0
      || !S_ISREG(before.st_mode) || before.st_size <= 0
      || std::uint64_t(before.st_size) > byteLimit)
    return {};
  // Construction is private; ownership of a successful file is transferred
  // before allocating buffers so all exception paths close it.
#ifndef __GLIBCXX__
  if (arena)
    throw std::bad_alloc();
#endif
  // Empty libstdc++ string assignment allocates max(size, 2*SSO-capacity).
  // Reserve Currency and that path backing BEFORE their allocation.
  // The first member releases after all path/currency/byte owners, on every
  // return, exception and off-thread destruction path.
  auto control = arena
      ? arena->reserve(sizeof(Snapshot::Currency)
            + std::max(relative.size(), 2 * std::string{}.capacity()) + 1)
      : CapacityArena::Charge{};
  // The allocator covers the actual coallocated Snapshot/control block until
  // deallocation (including weak owners), not merely through its destructor.
  auto snapshot = std::allocate_shared<Snapshot>(
      CapacityAllocator<Snapshot>(arena), arena, std::move(control));
  if (arena)
    arena->allocated(snapshot->m_control);
  snapshot->m_file = file.release();
  snapshot->m_root = std::move(approved);
  snapshot->m_relative = relative;
  snapshot->m_currency = std::make_unique<Snapshot::Currency>();
  snapshot->m_currency->stat = before;
  snapshot->m_bytes.resize(size_t(before.st_size));
  size_t offset = 0;
  while (offset < snapshot->m_bytes.size()) {
    if (cancelled && cancelled())
      return {};
    const auto count =
        std::min(size_t(1u << 20), snapshot->m_bytes.size() - offset);
    const auto read = pread(snapshot->m_file,
        snapshot->m_bytes.data() + offset,
        count,
        off_t(offset));
    if (read < 0 && errno == EINTR)
      continue;
    if (read <= 0)
      return {};
    offset += size_t(read);
  }
  if (!snapshot->isCurrent(cancelled))
    return {};
  return snapshot;
#else
  return {};
#endif
}

} // namespace vsr::integration::content

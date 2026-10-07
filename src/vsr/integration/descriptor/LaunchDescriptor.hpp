// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Header-only: the vsr_integration library globs only top-level sources, and
// this ticket may not edit that shared CMake file (see ticket 19).

// vsr_integration
#include "vsr/integration/mcp/Json.hpp"
// posix
#if defined(__linux__)
#include <fcntl.h>
#include <stdio.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>
#endif
// std
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vsr::integration::descriptor {

/*
 * Private launch descriptor: strict v1 schema, publication into a validated
 * owner-private runtime directory, opened-file validation for readers, and
 * identity-checked cleanup on orderly shutdown.
 *
 * Schema (one UTF-8 JSON object, at most 4 KiB, required fields once):
 *   schema_version     integer 1
 *   launch_id          32 lowercase hex
 *   scheme             "http"
 *   host               "127.0.0.1" | "::1"
 *   port               integer in [1, 65535]
 *   path               "/mcp"
 *   launch_credential  canonical unpadded base64url of 32 bytes (43 chars)
 *   image_exchange     optional strict launch-wide metadata object; never
 *                      contains authenticated session availability
 *
 * Directory policy (Linux): every component is opened directory-relative with
 * O_NOFOLLOW from "/". Ancestors must be owned by root or the effective user
 * and, when writable by group/other (mode or ACL), must be sticky. The final
 * directory must be owned by the effective user, mode exactly rwx for the
 * owner with no group/other bits, and carry no named-user/group ACL entries.
 * Unsafe directories are rejected, never repaired. Only the final component
 * may be created, with mode 0700 from creation.
 *
 * Publication writes a no-follow exclusive 0600 temporary file, then renames
 * it with no-replace semantics to vsr-mcp-<launch_id>.json (hard-link fallback
 * where renameat2 is unsupported). A pre-existing name fails closed.
 *
 * Only Linux is validated; elsewhere every operation reports
 * Code::unsupported_platform. Status reasons never contain descriptor values;
 * in particular the launch credential never appears in any output.
 */

inline constexpr std::size_t maxDescriptorBytes = 4096;

enum class Code
{
  ok,
  unsupported_platform,
  invalid_descriptor,
  unsafe_directory,
  unsafe_file,
  destination_exists,
  not_found,
  io_error,
};

inline const char *codeName(Code c)
{
  switch (c) {
  case Code::ok:
    return "ok";
  case Code::unsupported_platform:
    return "unsupported_platform";
  case Code::invalid_descriptor:
    return "invalid_descriptor";
  case Code::unsafe_directory:
    return "unsafe_directory";
  case Code::unsafe_file:
    return "unsafe_file";
  case Code::destination_exists:
    return "destination_exists";
  case Code::not_found:
    return "not_found";
  case Code::io_error:
    return "io_error";
  }
  return "unknown";
}

struct Status
{
  Code code{Code::ok};
  std::string reason; // never contains descriptor values or credentials

  bool ok() const
  {
    return code == Code::ok;
  }
};

// The variable fields; schema_version, scheme and path are fixed by v1.
struct Descriptor
{
  std::string launchId;
  std::string host;
  std::uint32_t port{0};
  std::string launchCredential;
  std::string imageExchange; // validated JSON, empty for older hosts
};

inline bool platformSupported()
{
#if defined(__linux__)
  return true;
#else
  return false;
#endif
}

inline std::string fileNameFor(std::string_view launchId)
{
  return "vsr-mcp-" + std::string(launchId) + ".json";
}

namespace detail {

inline Status fail(Code c, std::string reason)
{
  return Status{c, std::move(reason)};
}

inline bool isLowerHex(std::string_view s, std::size_t n)
{
  if (s.size() != n)
    return false;
  for (char c : s)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      return false;
  return true;
}

inline int base64UrlValue(char c)
{
  if (c >= 'A' && c <= 'Z')
    return c - 'A';
  if (c >= 'a' && c <= 'z')
    return c - 'a' + 26;
  if (c >= '0' && c <= '9')
    return c - '0' + 52;
  if (c == '-')
    return 62;
  if (c == '_')
    return 63;
  return -1;
}

// 32 bytes -> 43 chars; the final char carries 2 unused bits that must be 0.
inline bool isCanonicalCredential(std::string_view s)
{
  if (s.size() != 43)
    return false;
  for (char c : s)
    if (base64UrlValue(c) < 0)
      return false;
  return (base64UrlValue(s.back()) & 0x3) == 0;
}

// Strict RFC 3629 UTF-8 validation (no overlongs, surrogates or > U+10FFFF).
inline bool isValidUtf8(std::string_view s)
{
  std::size_t i = 0;
  while (i < s.size()) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      i++;
      continue;
    }
    std::size_t n = 0;
    std::uint32_t cp = 0;
    if ((c & 0xe0) == 0xc0) {
      n = 1;
      cp = c & 0x1f;
    } else if ((c & 0xf0) == 0xe0) {
      n = 2;
      cp = c & 0x0f;
    } else if ((c & 0xf8) == 0xf0) {
      n = 3;
      cp = c & 0x07;
    } else
      return false;
    if (i + n >= s.size())
      return false;
    for (std::size_t k = 1; k <= n; k++) {
      const auto cc = static_cast<unsigned char>(s[i + k]);
      if ((cc & 0xc0) != 0x80)
        return false;
      cp = (cp << 6) | (cc & 0x3f);
    }
    if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800)
        || (n == 3 && cp < 0x10000) || cp > 0x10ffff
        || (cp >= 0xd800 && cp <= 0xdfff))
      return false;
    i += n + 1;
  }
  return true;
}

inline void appendUtf8(std::string &out, std::uint32_t cp)
{
  if (cp < 0x80)
    out.push_back(char(cp));
  else if (cp < 0x800) {
    out.push_back(char(0xc0 | (cp >> 6)));
    out.push_back(char(0x80 | (cp & 0x3f)));
  } else if (cp < 0x10000) {
    out.push_back(char(0xe0 | (cp >> 12)));
    out.push_back(char(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(char(0x80 | (cp & 0x3f)));
  } else {
    out.push_back(char(0xf0 | (cp >> 18)));
    out.push_back(char(0x80 | ((cp >> 12) & 0x3f)));
    out.push_back(char(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(char(0x80 | (cp & 0x3f)));
  }
}

inline bool validImageExchange(std::string_view text, std::string_view launchId)
{
  if (text.empty())
    return true;
  using mcp::json::Value;
  Value value;
  if (!mcp::json::parse(text, value) || !value.isObject())
    return false;
  const auto flag = [&](const char *name) {
    const auto *v = value.get(name);
    return v && v->type == Value::Type::BOOL;
  };
  for (const auto *name :
      {"supported", "enabled", "export", "import", "data_tracking"})
    if (!flag(name))
      return false;
  const bool enabled = value.get("enabled")->b;
  if (value.get("supported")->b != enabled || value.get("export")->b != enabled
      || value.get("import")->b || (enabled && !value.get("data_tracking")->b))
    return false;
  if (value.o.size() != 7)
    return false;
  if (enabled) {
    const auto *directory = value.get("image_exchange_directory");
    if (!directory || !directory->isString() || directory->s.empty()
        || directory->s.front() != '/'
        || directory->s.find('\0') != std::string::npos
        || directory->s.find("/../") != std::string::npos
        || directory->s.find("/./") != std::string::npos
        || directory->s.find("//") != std::string::npos)
      return false;
    const auto suffix = "/" + std::string(launchId) + "/images";
    if (directory->s.size() <= suffix.size()
        || directory->s.compare(
            directory->s.size() - suffix.size(), suffix.size(), suffix))
      return false;
  } else {
    const auto *reason = value.get("reason");
    if (!reason || !reason->isString()
        || (reason->s != "unsafe_directory" && reason->s != "file_rejected"
            && reason->s != "unsupported_platform"
            && reason->s != "host_write_contract_unavailable"))
      return false;
  }
  const auto *limits = value.get("exchange_limits");
  if (!limits || !limits->isObject() || limits->o.size() != 11)
    return false;
  const auto integer = [&](const char *name, double cap, bool exact = false) {
    const auto *v = limits->get(name);
    return v && v->isNumber() && v->n >= 0 && v->n <= cap
        && std::floor(v->n) == v->n && (!exact || v->n == cap);
  };
  const auto *complete = limits->get("usage_complete");
  if (!complete || complete->type != Value::Type::BOOL)
    return false;
  return integer("file_cap", 1024, true) && integer("byte_cap", 268435456, true)
      && integer("files_used", 9007199254740991.)
      && integer("bytes_used", 9007199254740991.)
      && integer("quota_scan_entry_cap", 4096, true)
      && integer("max_ancillary_bytes", 65536, true)
      && integer("transient_image_byte_cap", 268435456)
      && integer("session_snapshot_byte_cap", 67108864, true)
      && integer("effective_global_retention_byte_cap", 9007199254740991.)
      && integer("available_global_retention_bytes",
          limits->get("effective_global_retention_byte_cap")
              ? limits->get("effective_global_retention_byte_cap")->n
              : 0);
}

class StrictParser
{
 public:
  explicit StrictParser(std::string_view in) : m_in(in) {}

  Status parse(Descriptor &out)
  {
    enum Field
    {
      fVersion,
      fLaunchId,
      fScheme,
      fHost,
      fPort,
      fPath,
      fCredential,
      fExchange,
      fCount
    };
    static const char *names[fCount] = {"schema_version",
        "launch_id",
        "scheme",
        "host",
        "port",
        "path",
        "launch_credential",
        "image_exchange"};
    bool seen[fCount] = {};

    ws();
    if (!eat('{'))
      return bad("descriptor is not a JSON object");
    ws();
    bool first = true;
    while (true) {
      if (first && peek() == '}') {
        m_pos++;
        break;
      }
      if (!first) {
        if (eat('}'))
          break;
        if (!eat(','))
          return bad("malformed JSON object");
        ws();
      }
      first = false;
      std::string key;
      if (!string(key))
        return bad("malformed field name");
      ws();
      if (!eat(':'))
        return bad("malformed JSON object");
      ws();
      int f = -1;
      for (int i = 0; i < fCount; i++)
        if (key == names[i])
          f = i;
      if (f < 0)
        return bad("unknown field");
      if (seen[f])
        return bad(std::string("duplicate field ") + names[f]);
      seen[f] = true;

      if (f == fExchange) {
        const auto begin = m_pos;
        if (!eat('{'))
          return bad("image_exchange must be an object");
        int depth = 1;
        while (depth && m_pos < m_in.size()) {
          if (peek() == '"') {
            std::string ignored;
            if (!string(ignored))
              return bad("malformed image_exchange string");
          } else {
            const auto c = m_in[m_pos++];
            if (c == '{')
              ++depth;
            if (c == '}')
              --depth;
          }
        }
        if (depth)
          return bad("malformed image_exchange object");
        out.imageExchange = std::string(m_in.substr(begin, m_pos - begin));
      } else if (f == fVersion || f == fPort) {
        std::uint64_t v = 0;
        if (!integer(v))
          return bad(std::string("field ") + names[f] + " must be an integer");
        if (f == fVersion && v != 1)
          return bad("unsupported schema_version");
        if (f == fPort) {
          if (v < 1 || v > 65535)
            return bad("port out of range");
          out.port = std::uint32_t(v);
        }
      } else {
        std::string v;
        if (!string(v))
          return bad(std::string("field ") + names[f] + " must be a string");
        switch (f) {
        case fLaunchId:
          if (!isLowerHex(v, 32))
            return bad("launch_id must be 32 lowercase hex characters");
          out.launchId = std::move(v);
          break;
        case fScheme:
          if (v != "http")
            return bad("scheme must be http");
          break;
        case fHost:
          if (v != "127.0.0.1" && v != "::1")
            return bad("host must be 127.0.0.1 or ::1");
          out.host = std::move(v);
          break;
        case fPath:
          if (v != "/mcp")
            return bad("path must be /mcp");
          break;
        case fCredential:
          if (!isCanonicalCredential(v))
            return bad(
                "launch_credential is not canonical base64url of 32 "
                "bytes");
          out.launchCredential = std::move(v);
          break;
        default:
          break;
        }
      }
      ws();
    }
    ws();
    if (m_pos != m_in.size())
      return bad("trailing content after descriptor object");
    for (int i = 0; i < fExchange; i++)
      if (!seen[i])
        return bad(std::string("missing field ") + names[i]);
    if (!validImageExchange(out.imageExchange, out.launchId))
      return bad("invalid image_exchange metadata");
    return {};
  }

 private:
  static Status bad(std::string why)
  {
    return fail(Code::invalid_descriptor, std::move(why));
  }

  char peek() const
  {
    return m_pos < m_in.size() ? m_in[m_pos] : '\0';
  }

  bool eat(char c)
  {
    if (m_pos < m_in.size() && m_in[m_pos] == c) {
      m_pos++;
      return true;
    }
    return false;
  }

  void ws()
  {
    while (m_pos < m_in.size()
        && (m_in[m_pos] == ' ' || m_in[m_pos] == '\t' || m_in[m_pos] == '\n'
            || m_in[m_pos] == '\r'))
      m_pos++;
  }

  bool hex4(std::uint32_t &v)
  {
    if (m_pos + 4 > m_in.size())
      return false;
    v = 0;
    for (int i = 0; i < 4; i++) {
      const char c = m_in[m_pos++];
      v <<= 4;
      if (c >= '0' && c <= '9')
        v |= std::uint32_t(c - '0');
      else if (c >= 'a' && c <= 'f')
        v |= std::uint32_t(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F')
        v |= std::uint32_t(c - 'A' + 10);
      else
        return false;
    }
    return true;
  }

  // JSON string; input is already validated UTF-8.
  bool string(std::string &out)
  {
    if (!eat('"'))
      return false;
    while (m_pos < m_in.size()) {
      const auto c = static_cast<unsigned char>(m_in[m_pos++]);
      if (c == '"')
        return true;
      if (c < 0x20)
        return false;
      if (c != '\\') {
        out.push_back(char(c));
        continue;
      }
      if (m_pos >= m_in.size())
        return false;
      const char e = m_in[m_pos++];
      switch (e) {
      case '"':
      case '\\':
      case '/':
        out.push_back(e);
        break;
      case 'b':
        out.push_back('\b');
        break;
      case 'f':
        out.push_back('\f');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'u': {
        std::uint32_t cp = 0;
        if (!hex4(cp))
          return false;
        if (cp >= 0xdc00 && cp <= 0xdfff)
          return false; // lone low surrogate
        if (cp >= 0xd800 && cp <= 0xdbff) {
          std::uint32_t lo = 0;
          if (!eat('\\') || !eat('u') || !hex4(lo) || lo < 0xdc00
              || lo > 0xdfff)
            return false;
          cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
        }
        appendUtf8(out, cp);
        break;
      }
      default:
        return false;
      }
    }
    return false;
  }

  // Strict JSON integer: -?(0|[1-9][0-9]*) with no fraction or exponent.
  bool integer(std::uint64_t &v)
  {
    if (peek() == '-')
      return false; // negative values are never valid here
    if (!(peek() >= '0' && peek() <= '9'))
      return false;
    if (peek() == '0') {
      m_pos++;
      v = 0;
    } else {
      v = 0;
      while (peek() >= '0' && peek() <= '9') {
        if (v > 1000000)
          return false; // far beyond any valid value; avoids overflow
        v = v * 10 + std::uint64_t(peek() - '0');
        m_pos++;
      }
    }
    const char n = peek();
    return !(n == '.' || n == 'e' || n == 'E' || (n >= '0' && n <= '9'));
  }

  std::string_view m_in;
  std::size_t m_pos{0};
};

} // namespace detail

inline Status validate(const Descriptor &d)
{
  if (!detail::isLowerHex(d.launchId, 32))
    return detail::fail(Code::invalid_descriptor, "invalid launch_id");
  if (d.host != "127.0.0.1" && d.host != "::1")
    return detail::fail(Code::invalid_descriptor, "invalid host");
  if (d.port < 1 || d.port > 65535)
    return detail::fail(Code::invalid_descriptor, "invalid port");
  if (!detail::isCanonicalCredential(d.launchCredential))
    return detail::fail(Code::invalid_descriptor, "invalid launch_credential");
  if (!detail::validImageExchange(d.imageExchange, d.launchId))
    return detail::fail(Code::invalid_descriptor, "invalid image_exchange");
  return {};
}

// Canonical serialization; empty when the descriptor is invalid.
inline std::string serialize(const Descriptor &d)
{
  if (!validate(d).ok())
    return {};
  return "{\"schema_version\":1,\"launch_id\":\"" + d.launchId
      + "\",\"scheme\":\"http\",\"host\":\"" + d.host + "\",\"port\":"
      + std::to_string(d.port) + ",\"path\":\"/mcp\",\"launch_credential\":\""
      + d.launchCredential + "\""
      + (d.imageExchange.empty() ? ""
                                 : ",\"image_exchange\":" + d.imageExchange)
      + "}\n";
}

inline Status parse(std::string_view json, Descriptor &out)
{
  if (json.size() > maxDescriptorBytes)
    return detail::fail(Code::invalid_descriptor, "descriptor exceeds 4 KiB");
  if (json.size() >= 3 && static_cast<unsigned char>(json[0]) == 0xef
      && static_cast<unsigned char>(json[1]) == 0xbb
      && static_cast<unsigned char>(json[2]) == 0xbf)
    return detail::fail(Code::invalid_descriptor, "byte order mark present");
  if (!detail::isValidUtf8(json))
    return detail::fail(Code::invalid_descriptor, "invalid UTF-8");
  Descriptor d;
  auto s = detail::StrictParser(json).parse(d);
  if (s.ok())
    out = std::move(d);
  return s;
}

// ---------------------------------------------------------------------------
// Filesystem

struct DirectoryOptions
{
  // Create the final path component (mode 0700 from creation) when missing.
  bool create{false};
#if defined(__linux__)
  // Account that must own the directory; defaults to the effective user.
  uid_t owner{geteuid()};
#endif
};

class RuntimeDirectory;
inline Status openDirectory(const std::string &path,
    const DirectoryOptions &options,
    RuntimeDirectory &out);

class RuntimeDirectory
{
 public:
  RuntimeDirectory() = default;
  RuntimeDirectory(const RuntimeDirectory &) = delete;
  RuntimeDirectory &operator=(const RuntimeDirectory &) = delete;
  RuntimeDirectory(RuntimeDirectory &&o) noexcept
  {
    *this = std::move(o);
  }
  RuntimeDirectory &operator=(RuntimeDirectory &&o) noexcept
  {
    if (this != &o) {
      reset();
      m_fd = std::exchange(o.m_fd, -1);
      m_path = std::move(o.m_path);
#if defined(__linux__)
      m_owner = o.m_owner;
#endif
    }
    return *this;
  }
  ~RuntimeDirectory()
  {
    reset();
  }

  bool valid() const
  {
    return m_fd >= 0;
  }
  const std::string &path() const
  {
    return m_path;
  }
  int fd() const
  {
    return m_fd;
  }
#if defined(__linux__)
  uid_t owner() const
  {
    return m_owner;
  }
#endif

 private:
  void reset()
  {
#if defined(__linux__)
    if (m_fd >= 0)
      ::close(m_fd);
#endif
    m_fd = -1;
  }

  friend Status openDirectory(
      const std::string &, const DirectoryOptions &, RuntimeDirectory &);

  int m_fd{-1};
  std::string m_path;
#if defined(__linux__)
  uid_t m_owner{0};
#endif
};

#if defined(__linux__)
namespace detail {

struct Fd
{
  int fd{-1};
  explicit Fd(int f = -1) : fd(f) {}
  Fd(const Fd &) = delete;
  Fd &operator=(const Fd &) = delete;
  ~Fd()
  {
    if (fd >= 0)
      ::close(fd);
  }
  int release()
  {
    return std::exchange(fd, -1);
  }
};

struct AclSummary
{
  bool readable{true}; // false when present but unparseable
  bool namedEntries{false}; // any ACL_USER / ACL_GROUP entry
  bool namedWrite{false}; // any named entry granting write
};

// Linux POSIX ACL xattr: 4-byte version header then 8-byte entries
// {u16 tag, u16 perm, u32 id}. Tags: USER=2, GROUP=8.
inline AclSummary aclOf(int fd)
{
  AclSummary r;
  unsigned char buf[1024];
  const auto n = ::fgetxattr(fd, "system.posix_acl_access", buf, sizeof(buf));
  if (n < 0) {
    if (errno == ENODATA || errno == ENOTSUP || errno == EOPNOTSUPP)
      return r;
    r.readable = false;
    return r;
  }
  if (n < 4 || (n - 4) % 8 != 0) {
    r.readable = false;
    return r;
  }
  for (ssize_t off = 4; off + 8 <= n; off += 8) {
    const std::uint16_t tag = std::uint16_t(buf[off] | (buf[off + 1] << 8));
    const std::uint16_t perm =
        std::uint16_t(buf[off + 2] | (buf[off + 3] << 8));
    if (tag == 0x02 || tag == 0x08) {
      r.namedEntries = true;
      if (perm & 0x2)
        r.namedWrite = true;
    }
  }
  return r;
}

inline Status checkAncestor(int fd, uid_t owner)
{
  struct stat st;
  if (::fstat(fd, &st) != 0)
    return fail(Code::io_error, "cannot stat ancestor directory");
  if (!S_ISDIR(st.st_mode))
    return fail(Code::unsafe_directory, "ancestor is not a directory");
  if (st.st_uid != 0 && st.st_uid != owner)
    return fail(Code::unsafe_directory,
        "ancestor directory is owned by another account");
  const auto acl = aclOf(fd);
  if (!acl.readable)
    return fail(Code::unsafe_directory, "cannot validate ancestor ACL");
  const bool othersWrite =
      (st.st_mode & (S_IWGRP | S_IWOTH)) != 0 || acl.namedWrite;
  if (othersWrite && !(st.st_mode & S_ISVTX))
    return fail(Code::unsafe_directory,
        "ancestor directory is replaceable by another account");
  return {};
}

inline Status checkPrivateDirectory(int fd, uid_t owner)
{
  struct stat st;
  if (::fstat(fd, &st) != 0)
    return fail(Code::io_error, "cannot stat runtime directory");
  if (!S_ISDIR(st.st_mode))
    return fail(Code::unsafe_directory, "runtime path is not a directory");
  if (st.st_uid != owner)
    return fail(Code::unsafe_directory,
        "runtime directory is not owned by the effective user");
  if ((st.st_mode & 07777) != 0700)
    return fail(Code::unsafe_directory, "runtime directory mode is not 0700");
  const auto acl = aclOf(fd);
  if (!acl.readable || acl.namedEntries)
    return fail(
        Code::unsafe_directory, "runtime directory grants access via ACL");
  return {};
}

inline Status checkPrivateFile(int fd, uid_t owner, struct stat &st)
{
  if (::fstat(fd, &st) != 0)
    return fail(Code::io_error, "cannot stat descriptor file");
  if (!S_ISREG(st.st_mode))
    return fail(Code::unsafe_file, "descriptor is not a regular file");
  if (st.st_uid != owner)
    return fail(Code::unsafe_file,
        "descriptor file is not owned by the effective user");
  if ((st.st_mode & 07777) != 0600)
    return fail(Code::unsafe_file, "descriptor file mode is not 0600");
  const auto acl = aclOf(fd);
  if (!acl.readable || acl.namedEntries)
    return fail(Code::unsafe_file, "descriptor file grants access via ACL");
  if (st.st_size < 0 || std::size_t(st.st_size) > maxDescriptorBytes)
    return fail(Code::unsafe_file, "descriptor file exceeds 4 KiB");
  return {};
}

inline bool validName(std::string_view n)
{
  return !n.empty() && n != "." && n != ".."
      && n.find('/') == std::string_view::npos
      && n.find('\0') == std::string_view::npos;
}

inline bool randomHex(std::string &out, std::size_t bytes)
{
  std::vector<unsigned char> buf(bytes);
  std::size_t filled = 0;
  while (filled < bytes) {
    const auto n = ::getrandom(buf.data() + filled, bytes - filled, 0);
    if (n <= 0)
      return false;
    filled += std::size_t(n);
  }
  static const char *digits = "0123456789abcdef";
  out.clear();
  for (auto b : buf) {
    out.push_back(digits[b >> 4]);
    out.push_back(digits[b & 0xf]);
  }
  return true;
}

inline Status openFailure(int err, const char *what)
{
  if (err == ENOENT)
    return fail(Code::not_found, std::string(what) + " does not exist");
  if (err == ELOOP || err == ENOTDIR)
    return fail(Code::unsafe_directory,
        std::string(what) + " is a symlink or not a directory");
  if (err == EACCES)
    return fail(
        Code::unsafe_directory, std::string(what) + " lacks owner access");
  return fail(Code::io_error, std::string("cannot open ") + what);
}

} // namespace detail
#endif

// Open and validate `path` (absolute, no "." / ".." components) one component
// at a time without following links. Never repairs an unsafe directory.
inline Status openDirectory(const std::string &path,
    const DirectoryOptions &options,
    RuntimeDirectory &out)
{
#if defined(__linux__)
  if (path.empty() || path[0] != '/')
    return detail::fail(
        Code::unsafe_directory, "runtime directory path is not absolute");
  std::vector<std::string> parts;
  std::size_t i = 0;
  while (i < path.size()) {
    const auto j = path.find('/', i);
    const auto part =
        path.substr(i, j == std::string::npos ? std::string::npos : j - i);
    if (part == "." || part == "..")
      return detail::fail(
          Code::unsafe_directory, "runtime directory path has . or ..");
    if (!part.empty())
      parts.push_back(part);
    if (j == std::string::npos)
      break;
    i = j + 1;
  }
  if (parts.empty())
    return detail::fail(
        Code::unsafe_directory, "runtime directory cannot be /");

  detail::Fd cur(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  if (cur.fd < 0)
    return detail::fail(Code::io_error, "cannot open /");
  if (auto s = detail::checkAncestor(cur.fd, options.owner); !s.ok())
    return s;

  for (std::size_t k = 0; k < parts.size(); k++) {
    const bool last = k + 1 == parts.size();
    if (last && options.create) {
      if (::mkdirat(cur.fd, parts[k].c_str(), 0700) != 0 && errno != EEXIST)
        return detail::fail(Code::io_error, "cannot create runtime directory");
    }
    detail::Fd next(::openat(cur.fd,
        parts[k].c_str(),
        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (next.fd < 0)
      return detail::openFailure(
          errno, last ? "runtime directory" : "ancestor directory");
    auto s = last ? detail::checkPrivateDirectory(next.fd, options.owner)
                  : detail::checkAncestor(next.fd, options.owner);
    if (!s.ok())
      return s;
    ::close(cur.fd);
    cur.fd = next.release();
  }

  RuntimeDirectory d;
  d.m_fd = cur.release();
  d.m_owner = options.owner;
  d.m_path = "/";
  for (std::size_t k = 0; k < parts.size(); k++)
    d.m_path += (k ? "/" : "") + parts[k];
  out = std::move(d);
  return {};
#else
  (void)path;
  (void)options;
  (void)out;
  return detail::fail(Code::unsupported_platform,
      "private descriptor publication is not validated on this platform");
#endif
}

// Per-user OS runtime location: $XDG_RUNTIME_DIR (else /run/user/<euid>)
// plus a "vsr" subdirectory created 0700. The environment label only selects
// the path; the opened directories are validated exactly like an explicit one.
inline Status openOsRuntimeDirectory(RuntimeDirectory &out)
{
#if defined(__linux__)
  std::string base;
  if (const char *x = std::getenv("XDG_RUNTIME_DIR"); x && x[0] == '/')
    base = x;
  else
    base = "/run/user/" + std::to_string(geteuid());
  RuntimeDirectory parent;
  if (auto s = openDirectory(base, {}, parent); !s.ok())
    return s;
  DirectoryOptions opt;
  opt.create = true;
  return openDirectory(parent.path() + "/vsr", opt, out);
#else
  (void)out;
  return detail::fail(Code::unsupported_platform,
      "private descriptor publication is not validated on this platform");
#endif
}

// A descriptor this instance published. Orderly destruction removes it only
// if the directory entry still has the recorded identity.
class PublishedDescriptor
{
 public:
  PublishedDescriptor(const PublishedDescriptor &) = delete;
  PublishedDescriptor &operator=(const PublishedDescriptor &) = delete;
  ~PublishedDescriptor()
  {
    remove();
  }

  const std::string &path() const
  {
    return m_path;
  }
  const std::string &fileName() const
  {
    return m_name;
  }

  // Startup output: the path only, never the credential.
  void announce(std::ostream &os) const
  {
    os << "VSR integration descriptor: " << m_path << '\n';
  }

  // Remove this instance's file. Missing or mismatched entries stay
  // untouched and report not_found / unsafe_file. Idempotent.
  Status remove()
  {
#if defined(__linux__)
    if (m_dirFd < 0)
      return {};
    detail::Fd dir(std::exchange(m_dirFd, -1));
    struct stat st;
    if (::fstatat(dir.fd, m_name.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0)
      return detail::fail(Code::not_found, "descriptor already removed");
    if (!S_ISREG(st.st_mode) || st.st_dev != m_dev || st.st_ino != m_ino)
      return detail::fail(
          Code::unsafe_file, "descriptor entry is not this instance's file");
    if (::unlinkat(dir.fd, m_name.c_str(), 0) != 0)
      return detail::fail(Code::io_error, "cannot remove descriptor");
    return {};
#else
    return {};
#endif
  }

 private:
  PublishedDescriptor() = default;
  friend Status publish(const RuntimeDirectory &,
      const Descriptor &,
      std::unique_ptr<PublishedDescriptor> &);

  int m_dirFd{-1};
  std::string m_name;
  std::string m_path;
#if defined(__linux__)
  dev_t m_dev{0};
  ino_t m_ino{0};
#endif
};

inline Status publish(const RuntimeDirectory &dir,
    const Descriptor &d,
    std::unique_ptr<PublishedDescriptor> &out)
{
#if defined(__linux__)
  if (auto s = validate(d); !s.ok())
    return s;
  if (!dir.valid())
    return detail::fail(Code::unsafe_directory, "runtime directory not open");
  // Re-validate the held directory right before publishing.
  if (auto s = detail::checkPrivateDirectory(dir.fd(), dir.owner()); !s.ok())
    return s;

  const auto content = serialize(d);
  if (content.size() > maxDescriptorBytes)
    return detail::fail(Code::invalid_descriptor, "descriptor exceeds 4 KiB");
  const auto name = fileNameFor(d.launchId);

  std::string rnd;
  if (!detail::randomHex(rnd, 16))
    return detail::fail(Code::io_error, "no secure randomness");
  const auto tmp = ".vsr-mcp-" + d.launchId + "." + rnd + ".tmp";

  detail::Fd dirFd(::fcntl(dir.fd(), F_DUPFD_CLOEXEC, 0));
  if (dirFd.fd < 0)
    return detail::fail(Code::io_error, "cannot duplicate directory handle");

  detail::Fd f(::openat(dirFd.fd,
      tmp.c_str(),
      O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
      0600));
  if (f.fd < 0)
    return detail::fail(Code::io_error, "cannot create temporary descriptor");
  auto discardTmp = [&](Status s) {
    ::unlinkat(dirFd.fd, tmp.c_str(), 0);
    return s;
  };
  struct stat st;
  if (auto s = detail::checkPrivateFile(f.fd, dir.owner(), st); !s.ok())
    return discardTmp(s);

  std::size_t written = 0;
  while (written < content.size()) {
    const auto n =
        ::write(f.fd, content.data() + written, content.size() - written);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      return discardTmp(
          detail::fail(Code::io_error, "cannot write descriptor"));
    written += std::size_t(n);
  }
  if (::fsync(f.fd) != 0)
    return discardTmp(detail::fail(Code::io_error, "cannot sync descriptor"));

  int rc = -1;
#if defined(RENAME_NOREPLACE)
  rc = ::renameat2(
      dirFd.fd, tmp.c_str(), dirFd.fd, name.c_str(), RENAME_NOREPLACE);
  if (rc != 0 && errno == EEXIST)
    return discardTmp(detail::fail(
        Code::destination_exists, "descriptor name already exists"));
  if (rc != 0 && errno != EINVAL && errno != ENOSYS)
    return discardTmp(
        detail::fail(Code::io_error, "cannot publish descriptor"));
#endif
  if (rc != 0) {
    // Hard links never replace an existing entry.
    if (::linkat(dirFd.fd, tmp.c_str(), dirFd.fd, name.c_str(), 0) != 0) {
      const bool exists = errno == EEXIST;
      return discardTmp(exists
              ? detail::fail(
                    Code::destination_exists, "descriptor name already exists")
              : detail::fail(Code::io_error, "cannot publish descriptor"));
    }
    ::unlinkat(dirFd.fd, tmp.c_str(), 0);
  }

  std::unique_ptr<PublishedDescriptor> p(new PublishedDescriptor);
  p->m_dirFd = dirFd.release();
  p->m_name = name;
  p->m_path = dir.path() + "/" + name;
  p->m_dev = st.st_dev;
  p->m_ino = st.st_ino;
  out = std::move(p);
  return {};
#else
  (void)dir;
  (void)d;
  (void)out;
  return detail::fail(Code::unsupported_platform,
      "private descriptor publication is not validated on this platform");
#endif
}

// Reader: open the named entry without following links, then validate the
// opened file's type, owner, mode, ACL, size and strict schema.
inline Status read(
    const RuntimeDirectory &dir, std::string_view name, Descriptor &out)
{
#if defined(__linux__)
  if (!dir.valid())
    return detail::fail(Code::unsafe_directory, "runtime directory not open");
  if (!detail::validName(name))
    return detail::fail(Code::unsafe_file, "invalid descriptor name");
  const std::string n(name);
  detail::Fd f(::openat(
      dir.fd(), n.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  if (f.fd < 0) {
    if (errno == ENOENT)
      return detail::fail(Code::not_found, "descriptor does not exist");
    if (errno == ELOOP)
      return detail::fail(Code::unsafe_file, "descriptor is a symlink");
    return detail::fail(Code::io_error, "cannot open descriptor");
  }
  struct stat st;
  if (auto s = detail::checkPrivateFile(f.fd, dir.owner(), st); !s.ok())
    return s;
  std::string buf(maxDescriptorBytes + 1, '\0');
  std::size_t got = 0;
  while (got < buf.size()) {
    const auto r = ::read(f.fd, buf.data() + got, buf.size() - got);
    if (r < 0 && errno == EINTR)
      continue;
    if (r < 0)
      return detail::fail(Code::io_error, "cannot read descriptor");
    if (r == 0)
      break;
    got += std::size_t(r);
  }
  buf.resize(got);
  return parse(buf, out);
#else
  (void)dir;
  (void)name;
  (void)out;
  return detail::fail(Code::unsupported_platform,
      "private descriptor publication is not validated on this platform");
#endif
}

// Reader by full path: validates the containing directory as private first.
inline Status read(const std::string &filePath,
    Descriptor &out,
    const DirectoryOptions &options = {})
{
  const auto slash = filePath.rfind('/');
  if (slash == std::string::npos || slash == 0)
    return detail::fail(Code::unsafe_file, "descriptor path is not absolute");
  DirectoryOptions opt = options;
  opt.create = false;
  RuntimeDirectory dir;
  if (auto s = openDirectory(filePath.substr(0, slash), opt, dir); !s.ok())
    return s;
  return read(dir, std::string_view(filePath).substr(slash + 1), out);
}

} // namespace vsr::integration::descriptor

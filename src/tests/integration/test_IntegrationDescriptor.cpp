// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// The POSIX-ACL case needs a filesystem for the test temp directory that
// accepts system.posix_acl_access. When it does not, the case FAILS by
// default. Set VSR_TEST_ALLOW_NO_ACL=1 to downgrade that failure to a WARN
// and skip the case (e.g. on tmpfs without ACL support in CI sandboxes).

// catch
#include "catch.hpp"
// vsr
#include "vsr/integration/descriptor/LaunchDescriptor.hpp"
// std
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
// posix
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace desc = vsr::integration::descriptor;
using desc::Code;

namespace {

const char *kId = "0123456789abcdef0123456789abcdef";
const char *kId2 = "fedcba9876543210fedcba9876543210";
// 43 chars; last char 'A' leaves the 2 padding bits zero.
const char *kCred = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMN-_A";

desc::Descriptor sample(const char *id = kId)
{
  desc::Descriptor d;
  d.launchId = id;
  d.host = "127.0.0.1";
  d.port = 8765;
  d.launchCredential = kCred;
  return d;
}

std::string json(const std::string &body)
{
  return "{" + body + "}";
}

const std::string kFields = std::string("\"schema_version\":1,\"launch_id\":\"")
    + kId + "\",\"scheme\":\"http\",\"host\":\"127.0.0.1\",\"port\":8765,"
    + "\"path\":\"/mcp\",\"launch_credential\":\"" + kCred + "\"";

Code parseCode(const std::string &s)
{
  desc::Descriptor d;
  return desc::parse(s, d).code;
}

std::string replaceOnce(
    std::string s, const std::string &from, const std::string &to)
{
  const auto p = s.find(from);
  REQUIRE(p != std::string::npos);
  s.replace(p, from.size(), to);
  return s;
}

struct TempDir
{
  std::string path;
  TempDir()
  {
    char tmpl[] = "/tmp/vsr-desc-test-XXXXXX";
    REQUIRE(::mkdtemp(tmpl) != nullptr);
    path = tmpl; // mkdtemp creates 0700
  }
  ~TempDir()
  {
    std::string cmd =
        "chmod -R u+rwx '" + path + "' 2>/dev/null; rm -rf '" + path + "'";
    (void)std::system(cmd.c_str());
  }
  std::string sub(const std::string &name, mode_t mode) const
  {
    const auto p = path + "/" + name;
    REQUIRE(::mkdir(p.c_str(), 0700) == 0);
    REQUIRE(::chmod(p.c_str(), mode) == 0);
    return p;
  }
};

mode_t modeOf(const std::string &p)
{
  struct stat st;
  REQUIRE(::lstat(p.c_str(), &st) == 0);
  return st.st_mode & 07777;
}

bool exists(const std::string &p)
{
  struct stat st;
  return ::lstat(p.c_str(), &st) == 0;
}

std::vector<std::string> entries(const std::string &dir)
{
  std::vector<std::string> out;
  DIR *d = ::opendir(dir.c_str());
  REQUIRE(d);
  while (auto *e = ::readdir(d)) {
    std::string n = e->d_name;
    if (n != "." && n != "..")
      out.push_back(n);
  }
  ::closedir(d);
  return out;
}

void writeFile(const std::string &p, const std::string &content, mode_t mode)
{
  int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  REQUIRE(fd >= 0);
  REQUIRE(
      ::write(fd, content.data(), content.size()) == ssize_t(content.size()));
  ::close(fd);
  REQUIRE(::chmod(p.c_str(), mode) == 0);
}

std::string slurp(const std::string &p)
{
  std::string out;
  int fd = ::open(p.c_str(), O_RDONLY);
  REQUIRE(fd >= 0);
  char buf[8192];
  ssize_t n;
  while ((n = ::read(fd, buf, sizeof(buf))) > 0)
    out.append(buf, size_t(n));
  ::close(fd);
  return out;
}

} // namespace

SCENARIO("Descriptor schema is strict v1", "[integration]")
{
  GIVEN("the canonical serialization")
  {
    const auto s = desc::serialize(sample());
    REQUIRE(!s.empty());
    REQUIRE(s.size() <= desc::maxDescriptorBytes);
    desc::Descriptor d;
    REQUIRE(desc::parse(s, d).ok());
    REQUIRE(d.launchId == kId);
    REQUIRE(d.host == "127.0.0.1");
    REQUIRE(d.port == 8765);
    REQUIRE(d.launchCredential == kCred);
    REQUIRE(s.find("session") == std::string::npos);
  }

  THEN("whitespace, field order and IPv6 loopback are accepted")
  {
    REQUIRE(parseCode(" \n{ " + kFields + " }\n") == Code::ok);
    auto v6 = replaceOnce(kFields, "\"127.0.0.1\"", "\"::1\"");
    REQUIRE(parseCode(json(v6)) == Code::ok);
    auto reordered = replaceOnce(kFields, "\"schema_version\":1,", "")
        + ",\"schema_version\":1";
    REQUIRE(parseCode(json(reordered)) == Code::ok);
    // An escape that decodes to the required value is still that value.
    REQUIRE(parseCode(json(replaceOnce(kFields, "\"/mcp\"", "\"\\/mcp\"")))
        == Code::ok);
  }

  THEN("duplicate, unknown and missing fields are rejected")
  {
    REQUIRE(parseCode(json(kFields + ",\"port\":8765"))
        == Code::invalid_descriptor);
    REQUIRE(
        parseCode(json(kFields + ",\"extra\":1")) == Code::invalid_descriptor);
    REQUIRE(parseCode(json(kFields + ",\"session_id\":\"x\""))
        == Code::invalid_descriptor);
    REQUIRE(parseCode(json(replaceOnce(kFields, ",\"path\":\"/mcp\"", "")))
        == Code::invalid_descriptor);
    REQUIRE(parseCode("{}") == Code::invalid_descriptor);
  }

  THEN("wrong types and values are rejected, never coerced")
  {
    const std::vector<std::pair<std::string, std::string>> bad = {
        {"\"schema_version\":1", "\"schema_version\":\"1\""},
        {"\"schema_version\":1", "\"schema_version\":2"},
        {"\"schema_version\":1", "\"schema_version\":1.0"},
        {"\"schema_version\":1", "\"schema_version\":1e0"},
        {"\"schema_version\":1", "\"schema_version\":01"},
        {"\"schema_version\":1", "\"schema_version\":true"},
        {"\"port\":8765", "\"port\":\"8765\""},
        {"\"port\":8765", "\"port\":0"},
        {"\"port\":8765", "\"port\":65536"},
        {"\"port\":8765", "\"port\":-1"},
        {"\"port\":8765", "\"port\":8765.0"},
        {"\"port\":8765", "\"port\":99999999999999999999999"},
        {"\"port\":8765", "\"port\":null"},
        {"\"scheme\":\"http\"", "\"scheme\":\"https\""},
        {"\"scheme\":\"http\"", "\"scheme\":\"HTTP\""},
        {"\"path\":\"/mcp\"", "\"path\":\"/mcp/\""},
        {"\"host\":\"127.0.0.1\"", "\"host\":\"localhost\""},
        {"\"host\":\"127.0.0.1\"", "\"host\":\"::ffff:127.0.0.1\""},
        {"\"host\":\"127.0.0.1\"", "\"host\":\"0:0:0:0:0:0:0:1\""},
        {"\"host\":\"127.0.0.1\"", "\"host\":\"[::1]\""},
        {"\"host\":\"127.0.0.1\"", "\"host\":\"0.0.0.0\""},
        {"\"host\":\"127.0.0.1\"", "\"host\":127"},
        {std::string("\"launch_id\":\"") + kId + "\"",
            "\"launch_id\":\"0123456789ABCDEF0123456789abcdef\""},
        {std::string("\"launch_id\":\"") + kId + "\"",
            "\"launch_id\":\"0123456789abcdef\""},
        {kCred, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMN-_B"}, // non-canonical
        {kCred, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMN-_A="}, // padded
        {kCred, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMN+/A"}, // std alphabet
        {kCred, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMN-_"}, // short
    };
    for (const auto &[from, to] : bad) {
      INFO(to);
      REQUIRE(parseCode(json(replaceOnce(kFields, from, to)))
          == Code::invalid_descriptor);
    }
  }

  THEN("malformed JSON and invalid encodings are rejected")
  {
    REQUIRE(parseCode("[" + kFields + "]") == Code::invalid_descriptor);
    REQUIRE(parseCode(json(kFields + ",")) == Code::invalid_descriptor);
    REQUIRE(parseCode(json(kFields) + "{}") == Code::invalid_descriptor);
    REQUIRE(parseCode(json(kFields) + std::string(1, '\0'))
        == Code::invalid_descriptor);
    REQUIRE(
        parseCode("\xEF\xBB\xBF" + json(kFields)) == Code::invalid_descriptor);
    // Invalid UTF-8 inside an otherwise ignorable position.
    REQUIRE(parseCode(json(kFields) + " \xC0\xAF") == Code::invalid_descriptor);
    REQUIRE(
        parseCode(json(kFields + ",\"\xFF\":1")) == Code::invalid_descriptor);
    // Overlong, surrogate and lone-surrogate escapes.
    REQUIRE(parseCode(json(replaceOnce(kFields, "\"/mcp\"", "\"/mc\xC1\xB0\"")))
        == Code::invalid_descriptor);
    REQUIRE(
        parseCode(json(replaceOnce(kFields, "\"/mcp\"", "\"\xED\xA0\x80\"")))
        == Code::invalid_descriptor);
    REQUIRE(parseCode(json(replaceOnce(kFields, "\"/mcp\"", "\"\\ud800\"")))
        == Code::invalid_descriptor);
    REQUIRE(parseCode(json(replaceOnce(kFields, "\"/mcp\"", "\"/m\\qp\"")))
        == Code::invalid_descriptor);
    // Raw control character inside a string.
    REQUIRE(parseCode(json(replaceOnce(kFields, "\"/mcp\"", "\"/m\tcp\"")))
        == Code::invalid_descriptor);
  }

  THEN("input above 4 KiB is rejected")
  {
    std::string big = json(kFields);
    big.insert(1, desc::maxDescriptorBytes, ' ');
    REQUIRE(parseCode(big) == Code::invalid_descriptor);
  }

  THEN("invalid descriptors are never serialized")
  {
    auto d = sample();
    d.host = "localhost";
    REQUIRE(desc::serialize(d).empty());
    d = sample();
    d.port = 0;
    REQUIRE(desc::serialize(d).empty());
  }
}

SCENARIO("Runtime directories are validated, never repaired", "[integration]")
{
  REQUIRE(desc::platformSupported());
  TempDir tmp;

  THEN("a private 0700 directory owned by the effective user is accepted")
  {
    desc::RuntimeDirectory dir;
    REQUIRE(desc::openDirectory(tmp.path, {}, dir).ok());
    REQUIRE(dir.valid());
    REQUIRE(dir.path() == tmp.path);
  }

  THEN(
      "group/other access and insufficient owner access are rejected "
      "without chmod")
  {
    for (mode_t m : {0750, 0705, 0770, 0777, 0500, 0300, 01700, 02700}) {
      INFO(m);
      const auto p = tmp.sub("d" + std::to_string(m), m);
      desc::RuntimeDirectory dir;
      REQUIRE(desc::openDirectory(p, {}, dir).code == Code::unsafe_directory);
      REQUIRE(modeOf(p) == m);
    }
  }

  THEN("a directory owned by another account is rejected")
  {
    desc::DirectoryOptions opt;
    opt.owner = ::geteuid() + 1;
    desc::RuntimeDirectory dir;
    REQUIRE(
        desc::openDirectory(tmp.path, opt, dir).code == Code::unsafe_directory);
  }

  THEN("a symlinked directory or ancestor is rejected")
  {
    const auto real = tmp.sub("real", 0700);
    const auto link = tmp.path + "/link";
    REQUIRE(::symlink(real.c_str(), link.c_str()) == 0);
    desc::RuntimeDirectory dir;
    REQUIRE(desc::openDirectory(link, {}, dir).code == Code::unsafe_directory);
    tmp.sub("real/inner", 0700);
    REQUIRE(desc::openDirectory(link + "/inner", {}, dir).code
        == Code::unsafe_directory);
    REQUIRE(desc::openDirectory(real + "/inner", {}, dir).ok());
  }

  THEN("a non-directory is rejected")
  {
    const auto f = tmp.path + "/file";
    writeFile(f, "x", 0700);
    desc::RuntimeDirectory dir;
    REQUIRE(desc::openDirectory(f, {}, dir).code == Code::unsafe_directory);
  }

  THEN("an ancestor another account could replace is rejected")
  {
    const auto shared = tmp.sub("shared", 0777);
    const auto inner = shared + "/inner";
    REQUIRE(::mkdir(inner.c_str(), 0700) == 0);
    desc::RuntimeDirectory dir;
    REQUIRE(desc::openDirectory(inner, {}, dir).code == Code::unsafe_directory);
    AND_THEN("a sticky world-writable ancestor, like /tmp, is acceptable")
    {
      REQUIRE(::chmod(shared.c_str(), 01777) == 0);
      REQUIRE(desc::openDirectory(inner, {}, dir).ok());
    }
  }

  THEN("relative paths and dot components are rejected")
  {
    desc::RuntimeDirectory dir;
    REQUIRE(desc::openDirectory("relative/dir", {}, dir).code
        == Code::unsafe_directory);
    REQUIRE(desc::openDirectory(tmp.path + "/../x", {}, dir).code
        == Code::unsafe_directory);
    REQUIRE(desc::openDirectory("/", {}, dir).code == Code::unsafe_directory);
  }

  THEN("a named-user ACL entry on the directory is rejected")
  {
    const auto p = tmp.sub("acl", 0700);
    // system.posix_acl_access v2: USER_OBJ rwx, USER(uid) r, GROUP_OBJ ---,
    // MASK r, OTHER ---.
    auto le16 = [](std::vector<unsigned char> &b, unsigned v) {
      b.push_back(v & 0xff);
      b.push_back((v >> 8) & 0xff);
    };
    auto le32 = [](std::vector<unsigned char> &b, unsigned v) {
      for (int i = 0; i < 4; i++)
        b.push_back((v >> (8 * i)) & 0xff);
    };
    std::vector<unsigned char> acl;
    le32(acl, 2);
    const unsigned undef = 0xffffffffu;
    le16(acl, 0x01), le16(acl, 7), le32(acl, undef);
    le16(acl, 0x02), le16(acl, 4), le32(acl, ::geteuid() + 1);
    le16(acl, 0x04), le16(acl, 0), le32(acl, undef);
    le16(acl, 0x10), le16(acl, 4), le32(acl, undef);
    le16(acl, 0x20), le16(acl, 0), le32(acl, undef);
    if (::setxattr(
            p.c_str(), "system.posix_acl_access", acl.data(), acl.size(), 0)
        != 0) {
      const int err = errno;
      const char *allow = std::getenv("VSR_TEST_ALLOW_NO_ACL");
      if (allow && std::string(allow) == "1") {
        WARN("filesystem does not support POSIX ACLs (" << std::strerror(
                 err) << "); ACL case skipped because VSR_TEST_ALLOW_NO_ACL=1");
      } else {
        FAIL("setxattr(system.posix_acl_access) failed on "
            << p << ": " << std::strerror(err)
            << "; set VSR_TEST_ALLOW_NO_ACL=1 to skip this case");
      }
    } else {
      desc::RuntimeDirectory dir;
      REQUIRE(desc::openDirectory(p, {}, dir).code == Code::unsafe_directory);
    }
  }

  THEN("a missing directory is created private from creation when asked")
  {
    const auto p = tmp.path + "/fresh";
    desc::RuntimeDirectory dir;
    REQUIRE(desc::openDirectory(p, {}, dir).code == Code::not_found);
    REQUIRE_FALSE(exists(p));
    const auto old = ::umask(0);
    desc::DirectoryOptions opt;
    opt.create = true;
    const auto s = desc::openDirectory(p, opt, dir);
    ::umask(old);
    REQUIRE(s.ok());
    REQUIRE(modeOf(p) == 0700);
    AND_THEN("create never repairs an existing unsafe directory")
    {
      const auto bad = tmp.sub("bad", 0755);
      REQUIRE(
          desc::openDirectory(bad, opt, dir).code == Code::unsafe_directory);
      REQUIRE(modeOf(bad) == 0755);
    }
  }

  THEN("the OS runtime location is validated like an explicit directory")
  {
    const char *prev = std::getenv("XDG_RUNTIME_DIR");
    const std::string saved = prev ? prev : "";
    const auto unsafe = tmp.sub("xdg-unsafe", 0755);
    ::setenv("XDG_RUNTIME_DIR", unsafe.c_str(), 1);
    desc::RuntimeDirectory dir;
    const auto rejected = desc::openOsRuntimeDirectory(dir).code;
    const bool noChild = !exists(unsafe + "/vsr");

    const auto safe = tmp.sub("xdg-safe", 0700);
    ::setenv("XDG_RUNTIME_DIR", safe.c_str(), 1);
    const auto accepted = desc::openOsRuntimeDirectory(dir);
    if (prev)
      ::setenv("XDG_RUNTIME_DIR", saved.c_str(), 1);
    else
      ::unsetenv("XDG_RUNTIME_DIR");

    REQUIRE(rejected == Code::unsafe_directory);
    REQUIRE(noChild);
    REQUIRE(modeOf(unsafe) == 0755);
    REQUIRE(accepted.ok());
    REQUIRE(dir.path() == safe + "/vsr");
    REQUIRE(modeOf(safe + "/vsr") == 0700);
  }
}

SCENARIO(
    "Descriptors are published privately, atomically and without "
    "replacement",
    "[integration]")
{
  TempDir tmp;
  desc::RuntimeDirectory dir;
  REQUIRE(desc::openDirectory(tmp.path, {}, dir).ok());
  const auto name = desc::fileNameFor(kId);
  const auto path = tmp.path + "/" + name;

  WHEN("a descriptor is published")
  {
    std::unique_ptr<desc::PublishedDescriptor> pub;
    REQUIRE(desc::publish(dir, sample(), pub).ok());
    REQUIRE(pub);

    THEN("it is a complete 0600 file under the fresh name, with no temp left")
    {
      REQUIRE(pub->path() == path);
      REQUIRE(modeOf(path) == 0600);
      REQUIRE(entries(tmp.path) == std::vector<std::string>{name});
      REQUIRE(slurp(path) == desc::serialize(sample()));
    }

    THEN("a reader validates and parses the opened file")
    {
      desc::Descriptor d;
      REQUIRE(desc::read(dir, name, d).ok());
      REQUIRE(d.launchCredential == kCred);
      desc::Descriptor d2;
      REQUIRE(desc::read(path, d2).ok());
      REQUIRE(d2.launchId == kId);
    }

    THEN("announcing prints the path but never the credential")
    {
      std::ostringstream os;
      pub->announce(os);
      REQUIRE(os.str().find(path) != std::string::npos);
      REQUIRE(os.str().find(kCred) == std::string::npos);
    }

    THEN("orderly shutdown removes it")
    {
      REQUIRE(pub->remove().ok());
      REQUIRE_FALSE(exists(path));
      REQUIRE(pub->remove().ok()); // idempotent
    }

    THEN("destruction removes it")
    {
      pub.reset();
      REQUIRE_FALSE(exists(path));
    }

    THEN("a replaced entry is left untouched at shutdown")
    {
      REQUIRE(::unlink(path.c_str()) == 0);
      writeFile(path, "someone else", 0600);
      REQUIRE(pub->remove().code == Code::unsafe_file);
      REQUIRE(slurp(path) == "someone else");
    }

    THEN("a symlink substituted for the entry is left untouched")
    {
      REQUIRE(::unlink(path.c_str()) == 0);
      const auto target = tmp.sub("target", 0700);
      REQUIRE(::symlink(target.c_str(), path.c_str()) == 0);
      REQUIRE(pub->remove().code == Code::unsafe_file);
      REQUIRE(exists(path));
      REQUIRE(exists(target));
    }

    THEN("a missing entry is reported, not an error elsewhere")
    {
      REQUIRE(::unlink(path.c_str()) == 0);
      REQUIRE(pub->remove().code == Code::not_found);
    }

    THEN("publishing the same launch again fails closed without overwrite")
    {
      std::unique_ptr<desc::PublishedDescriptor> again;
      auto other = sample();
      other.port = 9999;
      REQUIRE(
          desc::publish(dir, other, again).code == Code::destination_exists);
      REQUIRE_FALSE(again);
      REQUIRE(slurp(path) == desc::serialize(sample()));
      REQUIRE(entries(tmp.path) == std::vector<std::string>{name});
    }
  }

  THEN("a pre-existing destination of any kind fails closed, unmodified")
  {
    const auto target = tmp.sub("target", 0700);
    SECTION("regular file")
    {
      writeFile(path, "old", 0644);
    }
    SECTION("symlink")
    {
      REQUIRE(::symlink(target.c_str(), path.c_str()) == 0);
    }
    SECTION("dangling symlink")
    {
      REQUIRE(::symlink((tmp.path + "/nowhere").c_str(), path.c_str()) == 0);
    }
    SECTION("directory")
    {
      REQUIRE(::mkdir(path.c_str(), 0700) == 0);
    }
    SECTION("fifo")
    {
      REQUIRE(::mkfifo(path.c_str(), 0600) == 0);
    }
    struct stat before;
    REQUIRE(::lstat(path.c_str(), &before) == 0);
    std::unique_ptr<desc::PublishedDescriptor> pub;
    REQUIRE(desc::publish(dir, sample(), pub).code == Code::destination_exists);
    REQUIRE_FALSE(pub);
    struct stat after;
    REQUIRE(::lstat(path.c_str(), &after) == 0);
    REQUIRE(before.st_ino == after.st_ino);
    REQUIRE(before.st_mode == after.st_mode);
    REQUIRE(before.st_size == after.st_size);
    REQUIRE_FALSE(exists(tmp.path + "/nowhere"));
    // No other name was used and no temporary remains.
    REQUIRE(entries(tmp.path).size() == 2);
  }

  THEN("publishing into a directory that became unsafe is rejected")
  {
    REQUIRE(::chmod(tmp.path.c_str(), 0750) == 0);
    std::unique_ptr<desc::PublishedDescriptor> pub;
    REQUIRE(desc::publish(dir, sample(), pub).code == Code::unsafe_directory);
    REQUIRE_FALSE(exists(path));
    REQUIRE(modeOf(tmp.path) == 0750);
    REQUIRE(::chmod(tmp.path.c_str(), 0700) == 0);
  }

  THEN("an invalid descriptor is never published")
  {
    auto d = sample();
    d.launchCredential = "short";
    std::unique_ptr<desc::PublishedDescriptor> pub;
    REQUIRE(desc::publish(dir, d, pub).code == Code::invalid_descriptor);
    REQUIRE(entries(tmp.path).empty());
  }

  THEN("concurrent launches publish and clean up independently")
  {
    std::unique_ptr<desc::PublishedDescriptor> a, b;
    REQUIRE(desc::publish(dir, sample(kId), a).ok());
    REQUIRE(desc::publish(dir, sample(kId2), b).ok());
    REQUIRE(entries(tmp.path).size() == 2);
    REQUIRE(a->remove().ok());
    REQUIRE_FALSE(exists(a->path()));
    desc::Descriptor d;
    REQUIRE(desc::read(dir, desc::fileNameFor(kId2), d).ok());
    REQUIRE(d.launchId == kId2);
  }

  THEN("the published credential never appears in status text")
  {
    std::unique_ptr<desc::PublishedDescriptor> a, b;
    REQUIRE(desc::publish(dir, sample(), a).ok());
    const auto s = desc::publish(dir, sample(), b);
    REQUIRE_FALSE(s.ok());
    REQUIRE(s.reason.find(kCred) == std::string::npos);
    auto bad = sample();
    bad.launchCredential = std::string(kCred) + "x";
    const auto s2 = desc::publish(dir, bad, b);
    REQUIRE(s2.reason.find(kCred) == std::string::npos);
  }
}

SCENARIO("Readers validate the opened file", "[integration]")
{
  TempDir tmp;
  desc::RuntimeDirectory dir;
  REQUIRE(desc::openDirectory(tmp.path, {}, dir).ok());
  const auto name = desc::fileNameFor(kId);
  const auto path = tmp.path + "/" + name;
  const auto good = desc::serialize(sample());
  desc::Descriptor d;

  THEN("a well-formed private file is accepted")
  {
    writeFile(path, good, 0600);
    REQUIRE(desc::read(dir, name, d).ok());
  }
  THEN("group/other readable files are rejected")
  {
    writeFile(path, good, 0644);
    REQUIRE(desc::read(dir, name, d).code == Code::unsafe_file);
    REQUIRE(modeOf(path) == 0644);
  }
  THEN("symlinks are rejected, even to a valid file")
  {
    const auto real = tmp.path + "/real.json";
    writeFile(real, good, 0600);
    REQUIRE(::symlink(real.c_str(), path.c_str()) == 0);
    REQUIRE(desc::read(dir, name, d).code == Code::unsafe_file);
    REQUIRE(desc::read(path, d).code == Code::unsafe_file);
  }
  THEN("non-regular entries are rejected")
  {
    REQUIRE(::mkfifo(path.c_str(), 0600) == 0);
    REQUIRE(desc::read(dir, name, d).code == Code::unsafe_file);
  }
  THEN("oversized files are rejected")
  {
    writeFile(path, good + std::string(desc::maxDescriptorBytes, ' '), 0600);
    REQUIRE(desc::read(dir, name, d).code == Code::unsafe_file);
  }
  THEN("schema violations are rejected")
  {
    writeFile(path, good.substr(0, good.size() / 2), 0600);
    REQUIRE(desc::read(dir, name, d).code == Code::invalid_descriptor);
  }
  THEN("absence is reported as not found")
  {
    REQUIRE(desc::read(dir, name, d).code == Code::not_found);
  }
  THEN("path traversal names are rejected")
  {
    REQUIRE(desc::read(dir, "../x.json", d).code == Code::unsafe_file);
    REQUIRE(desc::read(dir, "..", d).code == Code::unsafe_file);
  }
  THEN("a path reader rejects an unsafe containing directory")
  {
    const auto open = tmp.sub("open", 0755);
    writeFile(open + "/" + name, good, 0600);
    REQUIRE(desc::read(open + "/" + name, d).code == Code::unsafe_directory);
  }
  THEN("a file owned by another account is rejected")
  {
    writeFile(path, good, 0600);
    desc::DirectoryOptions opt;
    opt.owner = ::geteuid() + 1;
    // Directory ownership check fires first for the path reader.
    REQUIRE_FALSE(desc::read(path, d, opt).ok());
  }
}

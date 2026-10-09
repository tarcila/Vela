// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/core/Token.hpp"
#include "vsr/core/StorageProfile.hpp"
// std
#include <memory>
#include <mutex>
#include <unordered_set>

namespace vsr::core {

static std::mutex g_tokenRegistryMutex;
static std::unique_ptr<std::unordered_set<std::string>> g_tokenRegistry;

Token::Token(const char *s) : Token(std::string(s)) {}

Token::Token(const std::string &s)
{
  if (s.empty())
    return;
  std::lock_guard<std::mutex> lock(g_tokenRegistryMutex);
  if (!g_tokenRegistry)
    g_tokenRegistry = std::make_unique<std::unordered_set<std::string>>();
  auto result = g_tokenRegistry->insert(s);
  m_value = result.first->c_str();
}

std::optional<Token> Token::internBounded(const std::string &value,
    const std::function<bool(size_t)> &reserve,
    size_t bucketLimit)
{
#ifdef __GLIBCXX__
  if (value.empty())
    return Token{};
  if (!reserve || value.size() > 256 || !bucketLimit || bucketLimit > 65536)
    return {};
  std::lock_guard<std::mutex> lock(g_tokenRegistryMutex);
  Token token;
  // Heterogeneous unordered lookup is unavailable in C++17. A bounded scan
  // avoids constructing an allocation-bearing lookup string under admission.
  if (g_tokenRegistry) {
    if (g_tokenRegistry->size() > bucketLimit
        || g_tokenRegistry->bucket_count() > bucketLimit)
      return {};
    for (const auto &entry : *g_tokenRegistry)
      if (entry == value) {
        token.m_value = entry.c_str();
        return token;
      }
  }
  const auto count = g_tokenRegistry ? g_tokenRegistry->size() : 0;
  const auto buckets = g_tokenRegistry ? g_tokenRegistry->bucket_count() : 1;
  std::__detail::_Prime_rehash_policy policy(1.f);
  // libstdc++ rehash uses at least double the old bucket count; rounding to
  // the next prime and the minimum first-insert bucket request are included.
  const auto next = policy._M_next_bkt(
      std::max(size_t(13), std::max(2 * buckets, count + 1)));
  if (next > bucketLimit)
    return {};
  StorageProfile profile(4u << 20);
  if (!profile.add(buckets + next, sizeof(void *))
      || !profile.add(sizeof(std::string) + 4 * sizeof(void *))
      || !profile.add(value.size() + 1)
      || (!g_tokenRegistry
          && !profile.add(sizeof(std::unordered_set<std::string>)))
      || !reserve(profile.bytes()))
    return {};
  if (!g_tokenRegistry)
    g_tokenRegistry = std::make_unique<std::unordered_set<std::string>>();
  token.m_value = g_tokenRegistry->insert(value).first->c_str();
  return token;
#else
  return {};
#endif
}

const char *Token::c_str() const
{
  return value();
}

const char *Token::value() const
{
  return m_value;
}

std::string Token::str() const
{
  return empty() ? std::string() : std::string(c_str());
}

bool Token::empty() const
{
  return value() == nullptr;
}

Token::operator bool() const
{
  return !empty();
}

bool operator==(const Token &t1, const Token &t2)
{
  return t1.value() == t2.value();
}

bool operator!=(const Token &t1, const Token &t2)
{
  return !(t1.value() == t2.value());
}

} // namespace vsr::core

// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace vsr::integration::mcp::json {

/*
 * Minimal strict JSON value, parser and writer for the embedded MCP
 * transport (JSON-RPC 2.0 envelopes). Bounded depth; rejects duplicate keys,
 * trailing data and invalid escapes rather than coercing.
 */

struct Value
{
  enum class Type
  {
    NUL,
    BOOL,
    NUMBER,
    STRING,
    ARRAY,
    OBJECT
  };

  Type type{Type::NUL};
  bool b{false};
  double n{0.0};
  std::string s;
  std::vector<Value> a;
  std::map<std::string, Value> o;

  bool isObject() const;
  bool isString() const;
  bool isNumber() const;
  const Value *get(const std::string &key) const;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline bool Value::isObject() const
{
  return type == Type::OBJECT;
}

inline bool Value::isString() const
{
  return type == Type::STRING;
}

inline bool Value::isNumber() const
{
  return type == Type::NUMBER;
}

inline const Value *Value::get(const std::string &key) const
{
  if (type != Type::OBJECT)
    return nullptr;
  auto it = o.find(key);
  return it == o.end() ? nullptr : &it->second;
}

namespace detail {

class Parser
{
 public:
  explicit Parser(std::string_view in) : m_in(in) {}

  bool parse(Value &out);

 private:
  static constexpr int maxDepth = 32;

  void ws();
  bool lit(std::string_view word);
  static void utf8(std::string &out, std::uint32_t cp);
  bool hex4(std::uint32_t &v);
  bool string(std::string &out);
  bool number(double &out);
  bool value(Value &v, int depth);

  std::string_view m_in;
  std::size_t m_pos{0};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline bool Parser::parse(Value &out)
{
  ws();
  if (!value(out, 0))
    return false;
  ws();
  return m_pos == m_in.size();
}

inline void Parser::ws()
{
  while (m_pos < m_in.size()
      && (m_in[m_pos] == ' ' || m_in[m_pos] == '\t' || m_in[m_pos] == '\n'
          || m_in[m_pos] == '\r'))
    m_pos++;
}

inline bool Parser::lit(std::string_view word)
{
  if (m_in.substr(m_pos, word.size()) != word)
    return false;
  m_pos += word.size();
  return true;
}

inline void Parser::utf8(std::string &out, std::uint32_t cp)
{
  if (cp < 0x80)
    out += char(cp);
  else if (cp < 0x800) {
    out += char(0xC0 | (cp >> 6));
    out += char(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += char(0xE0 | (cp >> 12));
    out += char(0x80 | ((cp >> 6) & 0x3F));
    out += char(0x80 | (cp & 0x3F));
  } else {
    out += char(0xF0 | (cp >> 18));
    out += char(0x80 | ((cp >> 12) & 0x3F));
    out += char(0x80 | ((cp >> 6) & 0x3F));
    out += char(0x80 | (cp & 0x3F));
  }
}

inline bool Parser::hex4(std::uint32_t &v)
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

inline bool Parser::string(std::string &out)
{
  if (m_pos >= m_in.size() || m_in[m_pos] != '"')
    return false;
  m_pos++;
  while (m_pos < m_in.size()) {
    const char c = m_in[m_pos++];
    if (c == '"')
      return true;
    if (static_cast<unsigned char>(c) < 0x20)
      return false;
    if (c != '\\') {
      out += c;
      continue;
    }
    if (m_pos >= m_in.size())
      return false;
    const char e = m_in[m_pos++];
    switch (e) {
    case '"':
    case '\\':
    case '/':
      out += e;
      break;
    case 'b':
      out += '\b';
      break;
    case 'f':
      out += '\f';
      break;
    case 'n':
      out += '\n';
      break;
    case 'r':
      out += '\r';
      break;
    case 't':
      out += '\t';
      break;
    case 'u': {
      std::uint32_t cp;
      if (!hex4(cp))
        return false;
      if (cp >= 0xD800 && cp <= 0xDBFF) {
        std::uint32_t lo;
        if (!lit("\\u") || !hex4(lo) || lo < 0xDC00 || lo > 0xDFFF)
          return false;
        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
      } else if (cp >= 0xDC00 && cp <= 0xDFFF)
        return false;
      utf8(out, cp);
      break;
    }
    default:
      return false;
    }
  }
  return false;
}

inline bool Parser::number(double &out)
{
  const std::size_t start = m_pos;
  auto digit = [&] {
    return m_pos < m_in.size() && m_in[m_pos] >= '0' && m_in[m_pos] <= '9';
  };
  if (m_pos < m_in.size() && m_in[m_pos] == '-')
    m_pos++;
  if (!digit())
    return false;
  if (m_in[m_pos] == '0')
    m_pos++;
  else
    while (digit())
      m_pos++;
  if (m_pos < m_in.size() && m_in[m_pos] == '.') {
    m_pos++;
    if (!digit())
      return false;
    while (digit())
      m_pos++;
  }
  if (m_pos < m_in.size() && (m_in[m_pos] == 'e' || m_in[m_pos] == 'E')) {
    m_pos++;
    if (m_pos < m_in.size() && (m_in[m_pos] == '+' || m_in[m_pos] == '-'))
      m_pos++;
    if (!digit())
      return false;
    while (digit())
      m_pos++;
  }
  // Locale-independent and non-throwing; out-of-range or non-finite
  // values fail the parse.
  const char *first = m_in.data() + start;
  const char *last = m_in.data() + m_pos;
  double v = 0.0;
  const auto r = std::from_chars(first, last, v);
  if (r.ec != std::errc() || r.ptr != last || !std::isfinite(v))
    return false;
  out = v;
  return true;
}

inline bool Parser::value(Value &v, int depth)
{
  if (depth > maxDepth || m_pos >= m_in.size())
    return false;
  const char c = m_in[m_pos];
  if (c == '{') {
    v.type = Value::Type::OBJECT;
    m_pos++;
    ws();
    if (m_pos < m_in.size() && m_in[m_pos] == '}') {
      m_pos++;
      return true;
    }
    for (;;) {
      ws();
      std::string key;
      if (!string(key))
        return false;
      ws();
      if (!lit(":"))
        return false;
      ws();
      Value child;
      if (!value(child, depth + 1))
        return false;
      if (!v.o.emplace(std::move(key), std::move(child)).second)
        return false; // duplicate key
      ws();
      if (lit(","))
        continue;
      return lit("}");
    }
  }
  if (c == '[') {
    v.type = Value::Type::ARRAY;
    m_pos++;
    ws();
    if (lit("]"))
      return true;
    for (;;) {
      ws();
      Value child;
      if (!value(child, depth + 1))
        return false;
      v.a.push_back(std::move(child));
      ws();
      if (lit(","))
        continue;
      return lit("]");
    }
  }
  if (c == '"') {
    v.type = Value::Type::STRING;
    return string(v.s);
  }
  if (lit("true")) {
    v.type = Value::Type::BOOL;
    v.b = true;
    return true;
  }
  if (lit("false")) {
    v.type = Value::Type::BOOL;
    return true;
  }
  if (lit("null"))
    return true;
  v.type = Value::Type::NUMBER;
  return number(v.n);
}

} // namespace detail

inline bool parse(std::string_view in, Value &out)
{
  out = Value{};
  return detail::Parser(in).parse(out);
}

// Quoted, escaped JSON string literal.
inline std::string quote(std::string_view s)
{
  static const char *hex = "0123456789abcdef";
  std::string out = "\"";
  for (char c : s) {
    const auto u = static_cast<unsigned char>(c);
    if (c == '"')
      out += "\\\"";
    else if (c == '\\')
      out += "\\\\";
    else if (c == '\n')
      out += "\\n";
    else if (c == '\r')
      out += "\\r";
    else if (c == '\t')
      out += "\\t";
    else if (u < 0x20) {
      out += "\\u00";
      out += hex[u >> 4];
      out += hex[u & 0xF];
    } else
      out += c;
  }
  out += '"';
  return out;
}

// Serializes a parsed value back to compact JSON (used to echo JSON-RPC ids).
inline std::string write(const Value &v)
{
  switch (v.type) {
  case Value::Type::NUL:
    return "null";
  case Value::Type::BOOL:
    return v.b ? "true" : "false";
  case Value::Type::NUMBER: {
    char buf[32];
    if (v.n == static_cast<double>(static_cast<long long>(v.n))
        && v.n > -9007199254740992.0 && v.n < 9007199254740992.0)
      std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v.n));
    else
      std::snprintf(buf, sizeof(buf), "%.17g", v.n);
    return buf;
  }
  case Value::Type::STRING:
    return quote(v.s);
  case Value::Type::ARRAY: {
    std::string out = "[";
    for (std::size_t i = 0; i < v.a.size(); i++) {
      if (i)
        out += ',';
      out += write(v.a[i]);
    }
    return out + "]";
  }
  case Value::Type::OBJECT: {
    std::string out = "{";
    bool first = true;
    for (const auto &[k, c] : v.o) {
      if (!first)
        out += ',';
      first = false;
      out += quote(k) + ":" + write(c);
    }
    return out + "}";
  }
  }
  return "null";
}

} // namespace vsr::integration::mcp::json

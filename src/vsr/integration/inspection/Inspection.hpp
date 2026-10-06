// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Header-only: the vsr_integration library globs only top-level sources (see
// ticket 19).

// vsr_integration
#include "vsr/integration/identity/Identity.hpp"
#include "vsr/integration/inspection/Bounds.hpp"
#include "vsr/integration/inspection/RequestDiagnostics.hpp"
#include "vsr/integration/inspection/ViewInfo.hpp"
#include "vsr/integration/operations/Operations.hpp"
#include "vsr/integration/values/EditableValues.hpp"
// std
#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

namespace vsr::integration::inspection {

/*
 * Bounded, paged target discovery and object/parameter inspection (spec §7).
 *
 * Each page is one admitted scene operation built by makeDiscovery() /
 * makeInspection() and submitted through OperationEngine. The executor walks
 * candidates in slices of at most `sliceVisits`, yielding between slices so
 * the page keeps its own ID/slot. Nothing is materialized beyond the page
 * being built; cursors carry only a position and are MAC-authenticated and
 * bound to session, query, target and revision.
 *
 * Result: a JSON object with "revision", "visited", "returned", "items",
 * "complete" and, for continuations, "cursor" and "stop" (item|byte|work).
 * Inspection pages also carry "scene_revision" and discovery view items
 * "view_revision" (addendum 04 §8), in the render module's token form
 * "<activation>.<nonce>.<domain>.<value>", so a first render request can be
 * formed from wire data alone.
 *
 * Inspection items carry a bounded legacy "values" mirror for supported
 * float32 parameters. Parameter entries advertise generic stored-type
 * editability, matching-typed bounds and string choices.
 *
 * Each parameter also carries a declared-ANARI-type "value" with an explicit
 * state: available (data), valid (object handle), null, truncated (preview),
 * or unavailable (reason). Integer components outside JSON's safe interval use
 * decimal-string tags; non-finite float components use non_finite tags. Array
 * links/items carry metadata summaries only, never bulk contents. These read
 * representations alone do not imply editability: references, matrices and
 * opaque storage remain read-only.
 *
 * Discovery is session-wide: it needs no capability on any one target, lists
 * only readable targets, and its record covers exactly the listed targets.
 */

using operations::Outcome;
using operations::Request;
using operations::Step;
using operations::StepContext;
using session::Target;

struct PageLimits
{
  std::int64_t defaultItems{64};
  std::int64_t maxItems{256};
  std::size_t pageBytes{1u << 20};
  std::size_t itemBytes{64u << 10};
  std::size_t optionalBytes{16u << 10};
  std::size_t displayString{4096};
  std::size_t visits{4096};
  std::size_t sliceVisits{256};
  std::size_t scratchBytes{8u << 20};
  double deadlineSeconds{5.0};
  // Stored-name byte comparisons (including KMP fallback comparisons).
  std::size_t nameWork{65536};
  std::size_t sliceNameWork{4096};
};

// Global scratch accounting shared by all admitted discovery/inspection work.
class ScratchPool
{
 public:
  explicit ScratchPool(std::size_t capacity) : m_capacity(capacity) {}
  bool reserve(std::size_t n,
      std::size_t ceiling = std::numeric_limits<std::size_t>::max());
  void release(std::size_t n)
  {
    m_used -= n;
  }
  std::size_t used() const
  {
    return m_used.load();
  }

 private:
  std::size_t m_capacity;
  std::atomic<std::size_t> m_used{0};
};

inline bool ScratchPool::reserve(std::size_t n, std::size_t ceiling)
{
  const auto capacity = std::min(m_capacity, ceiling);
  auto cur = m_used.load();
  do {
    if (cur > capacity || n > capacity - cur)
      return false;
  } while (!m_used.compare_exchange_weak(cur, cur + n));
  return true;
}

namespace detail {

inline std::uint64_t rotl(std::uint64_t x, int b)
{
  return (x << b) | (x >> (64 - b));
}

// SipHash-2-4 keyed MAC.
inline std::uint64_t siphash(const std::uint64_t key[2], const std::string &msg)
{
  std::uint64_t v0 = 0x736f6d6570736575ull ^ key[0];
  std::uint64_t v1 = 0x646f72616e646f6dull ^ key[1];
  std::uint64_t v2 = 0x6c7967656e657261ull ^ key[0];
  std::uint64_t v3 = 0x7465646279746573ull ^ key[1];
  auto round = [&] {
    v0 += v1;
    v1 = rotl(v1, 13);
    v1 ^= v0;
    v0 = rotl(v0, 32);
    v2 += v3;
    v3 = rotl(v3, 16);
    v3 ^= v2;
    v0 += v3;
    v3 = rotl(v3, 21);
    v3 ^= v0;
    v2 += v1;
    v1 = rotl(v1, 17);
    v1 ^= v2;
    v2 = rotl(v2, 32);
  };
  const std::size_t n = msg.size();
  std::size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    std::uint64_t m;
    std::memcpy(&m, msg.data() + i, 8);
    v3 ^= m;
    round();
    round();
    v0 ^= m;
  }
  std::uint64_t b = std::uint64_t(n) << 56;
  for (std::size_t j = 0; i + j < n; j++)
    b |= std::uint64_t(std::uint8_t(msg[i + j])) << (8 * j);
  v3 ^= b;
  round();
  round();
  v0 ^= b;
  v2 ^= 0xff;
  round();
  round();
  round();
  round();
  return v0 ^ v1 ^ v2 ^ v3;
}

// "<activation>.<nonce>.<domain>.<value>", as vsr_render parses it.
template <typename R>
inline std::string revisionToken(const R &r)
{
  return std::to_string(r.launch.activation) + "."
      + std::to_string(r.launch.nonce) + "." + std::to_string(r.domain) + "."
      + std::to_string(r.value);
}

inline std::string hex64(std::uint64_t v)
{
  static const char *d = "0123456789abcdef";
  std::string s(16, '0');
  for (int i = 15; i >= 0; i--, v >>= 4)
    s[i] = d[v & 15];
  return s;
}

inline std::string jsonString(const std::string &s)
{
  std::string o = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      o += '\\';
      o += char(c);
    } else if (c < 0x20) {
      o += "\\u00";
      o += "0123456789abcdef"[c >> 4];
      o += "0123456789abcdef"[c & 15];
    } else
      o += char(c);
  }
  return o + "\"";
}

// Truncate at a UTF-8 boundary without scanning past the limit.
inline std::string truncateUtf8(std::string_view s, std::size_t limit)
{
  std::size_t n = limit;
  while (n > 0 && (std::uint8_t(s[n]) & 0xC0) == 0x80)
    n--;
  return std::string(s.substr(0, n));
}

// Numeric reads use memcpy: Any's byte storage need not be aligned for T.
template <typename T>
inline std::string numericComponent(const core::Any &value, std::size_t index)
{
  T component;
  std::memcpy(&component,
      static_cast<const unsigned char *>(value.data()) + index * sizeof(T),
      sizeof(T));
  if constexpr (std::is_floating_point_v<T>) {
    if (!std::isfinite(component))
      return "{\"type\":" + jsonString(anari::toString(value.type()))
          + ",\"encoding\":\"non_finite\",\"data\":"
          + jsonString(std::isnan(component) ? "nan"
                  : component > 0            ? "positive_infinity"
                                             : "negative_infinity")
          + "}";
    char buffer[64];
    const auto result = std::to_chars(buffer,
        buffer + sizeof(buffer),
        double(component),
        std::chars_format::general,
        std::numeric_limits<double>::max_digits10);
    return std::string(buffer, result.ptr);
  } else {
    constexpr std::uint64_t SAFE_INTEGER = 9007199254740991ull;
    const auto decimal = std::to_string(component);
    bool safe;
    if constexpr (std::is_signed_v<T>)
      safe = component >= -std::int64_t(SAFE_INTEGER)
          && component <= std::int64_t(SAFE_INTEGER);
    else
      safe = component <= SAFE_INTEGER;
    return safe ? decimal
                : "{\"type\":" + jsonString(anari::toString(value.type()))
            + ",\"encoding\":\"decimal\",\"data\":" + jsonString(decimal) + "}";
  }
}

template <typename T>
inline std::string numericData(
    const core::Any &value, std::size_t rows = 0, std::size_t columns = 0)
{
  const auto count = anari::componentsOf(value.type());
  if (count == 1)
    return numericComponent<T>(value, 0);
  std::string result = "[";
  if (rows) {
    for (std::size_t row = 0; row < rows; ++row) {
      result += row ? ",[" : "[";
      for (std::size_t column = 0; column < columns; ++column)
        result += (column ? "," : "")
            + numericComponent<T>(value, column * rows + row);
      result += "]";
    }
  } else {
    for (std::size_t i = 0; i < count; ++i)
      result += (i ? "," : "") + numericComponent<T>(value, i);
  }
  return result + "]";
}

inline std::string summaryInteger(std::size_t value)
{
  const std::uint64_t count = value;
  const core::Any integer(ANARI_UINT64, &count);
  return numericComponent<std::uint64_t>(integer, 0);
}

inline std::string arraySummary(const scene::Array &array)
{
  const auto rank = array.type() == ANARI_ARRAY1D ? 1u
      : array.type() == ANARI_ARRAY2D             ? 2u
                                                  : 3u;
  std::string dimensions = "[";
  std::size_t count = 1;
  bool overflow = false;
  for (std::size_t i = 0; i < rank; ++i) {
    const auto dimension = array.dim(i);
    dimensions += (i ? "," : "") + summaryInteger(dimension);
    if (dimension
        && count > std::numeric_limits<std::size_t>::max() / dimension)
      overflow = true;
    else if (!overflow)
      count *= dimension;
  }
  return "{\"element_type\":" + jsonString(anari::toString(array.elementType()))
      + ",\"dimensionality\":" + std::to_string(rank)
      + ",\"dimensions\":" + dimensions + "],\"element_count\":"
      + (overflow ? "{\"state\":\"unavailable\",\"reason\":\"count_overflow\"}"
                  : summaryInteger(count))
      + "}";
}

inline std::string parameterValue(const core::Any &value,
    scene::Scene &scene,
    identity::SceneIdentity &ids,
    const PageLimits &limits)
{
  const auto type = value.type();
  const auto prefix = "{\"type\":" + jsonString(anari::toString(type));
  if (value.holdsObject()) {
    if (value.getAsObjectIndex() == core::Any::INVALID_INDEX)
      return prefix + ",\"state\":\"null\"}";
    const auto *object = scene.getObject(value);
    const auto handle = ids.handleOf(object);
    if (!handle.valid())
      return prefix
          + ",\"state\":\"unavailable\",\"reason\":\"dangling_reference\"}";
    std::string result = prefix + ",\"state\":\"valid\",\"handle\":\""
        + hex64(handle.domain) + hex64(handle.serial) + "\"";
    if (const auto *array = dynamic_cast<const scene::Array *>(object))
      result += ",\"summary\":" + arraySummary(*array);
    return result + "}";
  }
  std::string data;
  if (type == ANARI_BOOL)
    data = value.get<bool>() ? "true" : "false";
  else if (type == ANARI_STRING) {
    const auto text = value.getStringView();
    const auto stringLimit =
        std::min(limits.displayString, limits.optionalBytes);
    if (text.size() > stringLimit)
      return prefix + ",\"state\":\"truncated\",\"reason\":"
          + jsonString(limits.displayString <= limits.optionalBytes
                  ? "display_string_limit"
                  : "optional_byte_limit")
          + ",\"data\":" + jsonString(truncateUtf8(text, stringLimit)) + "}";
    data = jsonString(std::string(text));
  }
#define VSR_INSPECT_NUMERIC(BASE, CPP)                                         \
  else if (type == BASE || type == BASE##_VEC2 || type == BASE##_VEC3          \
      || type == BASE##_VEC4) data = numericData<CPP>(value)
  VSR_INSPECT_NUMERIC(ANARI_INT8, std::int8_t);
  VSR_INSPECT_NUMERIC(ANARI_UINT8, std::uint8_t);
  VSR_INSPECT_NUMERIC(ANARI_INT16, std::int16_t);
  VSR_INSPECT_NUMERIC(ANARI_UINT16, std::uint16_t);
  VSR_INSPECT_NUMERIC(ANARI_INT32, std::int32_t);
  VSR_INSPECT_NUMERIC(ANARI_UINT32, std::uint32_t);
  VSR_INSPECT_NUMERIC(ANARI_INT64, std::int64_t);
  VSR_INSPECT_NUMERIC(ANARI_UINT64, std::uint64_t);
  VSR_INSPECT_NUMERIC(ANARI_FLOAT32, float);
  VSR_INSPECT_NUMERIC(ANARI_FLOAT64, double);
#undef VSR_INSPECT_NUMERIC
  else if (type == ANARI_FLOAT32_MAT2) data = numericData<float>(value, 2, 2);
  else if (type == ANARI_FLOAT32_MAT3) data = numericData<float>(value, 3, 3);
  else if (type == ANARI_FLOAT32_MAT4) data = numericData<float>(value, 4, 4);
  else if (type == ANARI_FLOAT32_MAT2x3) data = numericData<float>(value, 2, 3);
  else if (type == ANARI_FLOAT32_MAT3x4) data = numericData<float>(value, 3, 4);
  return data.empty()
      ? prefix + ",\"state\":\"unavailable\",\"reason\":\"unsupported_type\"}"
      : prefix + ",\"state\":\"available\",\"data\":" + data + "}";
}

} // namespace detail

enum class Query
{
  DISCOVER,
  INSPECT
};

struct ObjectFilters
{
  std::optional<std::string> name;
  std::optional<std::string> nameContains;
  std::optional<std::string> handle;
  std::optional<std::string> objectType;
  std::optional<std::string> objectSubtype;
  std::optional<std::string> nodeKind;
  std::optional<std::string> layerHandle;
  std::optional<std::string> parentHandle;
};

struct PageArgs
{
  std::optional<std::int64_t> pageSize; // omitted -> default
  std::string cursor; // empty -> first page
  ObjectFilters filters;
  std::string kind{"objects"};
};

struct Built
{
  Outcome code{Outcome::OK}; // INVALID_REQUEST for bad page_size
  Request request;
  RequestDiagnostics diagnostics;
};

class Inspector
{
 public:
  // `identities` maps a registered SceneId to its SceneIdentity (may return
  // null for scenes without one, which then report unsupported).
  using IdentityLookup = std::function<identity::SceneIdentity *(SceneId)>;
  // Maps a registered ViewId to its revision tracker (null: no token).
  using ViewLookup = std::function<identity::ViewRevisionTracker *(ViewId)>;

  Inspector(const Integration &integration,
      session::SessionAuthority &authority,
      IdentityLookup identities,
      PageLimits limits = {})
      : m_integration(integration),
        m_authority(authority),
        m_identities(std::move(identities)),
        m_limits(limits),
        m_scratch(std::make_shared<ScratchPool>(limits.scratchBytes))
  {
    std::random_device rd;
    for (auto &k : m_key)
      k = (std::uint64_t(rd()) << 32) ^ rd();
  }

  // Host: source of discovery's per-view "view_revision". Without it, view
  // items carry no token. Set before the first discovery is serviced.
  void setViewRevisions(ViewLookup views)
  {
    m_views = std::move(views);
  }

  const PageLimits &limits() const
  {
    return m_limits;
  }
  // Syntax only; safe on the carrier thread and shared with native validation.
  RequestDiagnostics requestDiagnostics(Query q, const PageArgs &args) const;

  const ScratchPool &scratch() const
  {
    return *m_scratch;
  }

  // Discovery lists registered scenes then views readable by the session.
  // Session-wide: admitted without a per-target capability.
  Built makeDiscovery(const std::string &sessionId, const PageArgs &args)
  {
    return make(Query::DISCOVER, sessionId, Target::scene(0), args);
  }
  // Inspection lists objects and parameters of one registered scene.
  Built makeInspection(
      const std::string &sessionId, SceneId scene, const PageArgs &args)
  {
    return make(Query::INSPECT, sessionId, Target::scene(scene), args);
  }

  // Value-only read of the registered host binding, admitted like inspection.
  Built makeViewInfo(const std::string &sessionId, ViewId view);

  // Default-visible scene bounds only. Unsupported choices reject before
  // admission; missing host data is not evidence of empty geometry.
  Built makeBounds(const std::string &sessionId,
      SceneId scene,
      const bounds::Args &args = {});

 private:
  // Explicit values preserve the authenticated cursor's uint64 carrier.
  enum class TransformIssue : std::uint64_t
  {
    NONE = 0,
    NON_FINITE = 1,
    NON_AFFINE = 2,
    ARRAY_ANCESTRY = 3,
    WORLD_OVERFLOW = 4
  };

  struct Position
  {
    std::uint64_t pool{0};
    std::uint64_t index{0};
    // Nodes: index is slot+1 (zero starts at root); aux is the current
    // ancestor/climb slot+1. Every ancestry and climb step is budgeted.
    std::uint64_t aux{0};
    std::uint64_t phase{0};
    bool effective{true};
    std::uint64_t structuralVersion{0};
    std::uint64_t transformVersion{0};
    // Fixed-size, authenticated continuation state; no parent stack.
    core::math::mat4 world{core::math::IDENTITY_MAT4};
    TransformIssue worldIssue{TransformIssue::NONE};
    // 0: unvisited, 1: exact, 2: substring, 3: matched.
    std::uint64_t namePhase{0};
    std::uint64_t nameOffset{0};
    std::uint64_t nameMatched{0};
  };

  // Owns its scratch reservation: released on completion, and otherwise when
  // the engine drops the executor on any other terminal path (cancelled,
  // timed out, session ended) or the request is never admitted.
  struct Job
  {
    Job() = default;
    Job(const Job &) = delete;
    Job &operator=(const Job &) = delete;
    ~Job()
    {
      releaseScratch();
    }
    void releaseScratch()
    {
      if (pool && reserved)
        pool->release(reserved);
      reserved = 0;
    }

    std::shared_ptr<ScratchPool> pool;
    Query query;
    std::string kind;
    ObjectFilters filters;
    std::string policy;
    std::string identityBinding;
    scene::Object *exactObject{nullptr}; // resolved only at the safe point
    std::string sessionId;
    Target target;
    std::size_t maxItems{0};
    Position pos;
    bool started{false};
    std::uint64_t revision{0};
    std::size_t visited{0};
    std::size_t nameWork{0};
    std::size_t sliceNameWork{0};
    std::vector<std::size_t> namePrefix;
    std::size_t returned{0};
    std::string items;
    std::size_t reserved{0};
    const char *stop{nullptr};
    std::vector<Target> listed; // discovery: targets on this page
  };

  std::string mac(const std::string &payload) const
  {
    return detail::hex64(detail::siphash(m_key, payload));
  }

  std::string binding(const Job &j, std::uint64_t revision) const
  {
    return std::to_string(int(j.query)) + ":" + j.sessionId + ":"
        + std::to_string(int(j.target.kind)) + ":" + std::to_string(j.target.id)
        + ":" + std::to_string(revision) + ":" + j.policy + ":"
        + j.identityBinding;
  }

  std::string encodeCursor(const Job &j) const
  {
    const std::string payload = detail::hex64(j.pos.pool)
        + detail::hex64(j.pos.index) + detail::hex64(j.revision)
        + mac(binding(j, j.revision))
        + (j.kind == "nodes"
                ? detail::hex64(j.pos.aux) + detail::hex64(j.pos.phase)
                    + detail::hex64(j.pos.effective)
                    + detail::hex64(j.pos.structuralVersion)
                    + detail::hex64(j.pos.transformVersion)
                : "");
    std::string carrier = payload;
    if (j.kind == "nodes") {
      carrier += detail::hex64(std::uint64_t(j.pos.worldIssue));
      for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
          std::uint32_t bits;
          std::memcpy(&bits, &j.pos.world[column][row], sizeof(bits));
          carrier += detail::hex64(bits).substr(8);
        }
      }
    }
    carrier += detail::hex64(j.pos.namePhase) + detail::hex64(j.pos.nameOffset)
        + detail::hex64(j.pos.nameMatched);
    return carrier + mac(carrier);
  }

  Outcome decodeCursor(const std::string &c, Job &j) const
  {
    // Authenticate either supported carrier before checking its query binding:
    // a genuine cursor from another kind is stale, not malformed.
    if (c.size() != 128 && c.size() != 352)
      return Outcome::INVALID_REQUEST;
    const auto payloadSize = c.size() - 16;
    for (char ch : c)
      if (!std::isxdigit(std::uint8_t(ch)) || std::isupper(std::uint8_t(ch)))
        return Outcome::INVALID_REQUEST;
    const std::string payload = c.substr(0, payloadSize);
    if (mac(payload) != c.substr(payloadSize))
      return Outcome::INVALID_REQUEST;
    const std::uint64_t rev = std::stoull(c.substr(32, 16), nullptr, 16);
    if (mac(binding(j, rev)) != c.substr(48, 16))
      return Outcome::CURSOR_STALE;
    if (payloadSize != (j.kind == "nodes" ? 336u : 112u))
      return Outcome::INVALID_REQUEST;
    j.pos.pool = std::stoull(c.substr(0, 16), nullptr, 16);
    j.pos.index = std::stoull(c.substr(16, 16), nullptr, 16);
    j.revision = rev;
    if (j.kind == "nodes") {
      j.pos.aux = std::stoull(c.substr(64, 16), nullptr, 16);
      j.pos.phase = std::stoull(c.substr(80, 16), nullptr, 16);
      j.pos.effective = std::stoull(c.substr(96, 16), nullptr, 16) != 0;
      j.pos.structuralVersion = std::stoull(c.substr(112, 16), nullptr, 16);
      j.pos.transformVersion = std::stoull(c.substr(128, 16), nullptr, 16);
      j.pos.worldIssue =
          TransformIssue(std::stoull(c.substr(144, 16), nullptr, 16));
      for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
          const std::uint32_t bits = std::stoul(
              c.substr(160 + (column * 4 + row) * 8, 8), nullptr, 16);
          std::memcpy(&j.pos.world[column][row], &bits, sizeof(bits));
        }
      }
    }
    const auto nameStart = payloadSize - 48;
    j.pos.namePhase = std::stoull(c.substr(nameStart, 16), nullptr, 16);
    j.pos.nameOffset = std::stoull(c.substr(nameStart + 16, 16), nullptr, 16);
    j.pos.nameMatched = std::stoull(c.substr(nameStart + 32, 16), nullptr, 16);
    j.started = true;
    return Outcome::OK;
  }

  static std::string filterPolicy(const PageArgs &args, std::int64_t size)
  {
    auto value = [](const std::optional<std::string> &s) {
      return s ? detail::jsonString(*s) : std::string("null");
    };
    const auto &f = args.filters;
    return "{\"kind\":" + detail::jsonString(args.kind) + ",\"page_size\":"
        + std::to_string(size) + ",\"name\":" + value(f.name)
        + ",\"name_contains\":" + value(f.nameContains) + ",\"handle\":"
        + value(f.handle) + ",\"object_type\":" + value(f.objectType)
        + ",\"object_subtype\":" + value(f.objectSubtype) + ",\"node_kind\":"
        + value(f.nodeKind) + ",\"layer_handle\":" + value(f.layerHandle)
        + ",\"parent_handle\":" + value(f.parentHandle) + "}";
  }

  bool validFilters(Query q,
      const PageArgs &args,
      RequestDiagnostics *diagnostics = nullptr) const
  {
    RequestDiagnostics local;
    auto &errors = diagnostics ? *diagnostics : local;
    using F = DiagnosticField;
    using R = DiagnosticReason;
    if ((args.kind != "objects" && args.kind != "layers"
            && args.kind != "nodes")
        || (q == Query::DISCOVER && args.kind != "objects"))
      errors.add(F::KIND, R::UNSUPPORTED);
    const auto &f = args.filters;
    const std::pair<F, const std::optional<std::string> *> fields[] = {
        {F::NAME, &f.name},
        {F::NAME_CONTAINS, &f.nameContains},
        {F::HANDLE, &f.handle},
        {F::OBJECT_TYPE, &f.objectType},
        {F::OBJECT_SUBTYPE, &f.objectSubtype},
        {F::NODE_KIND, &f.nodeKind},
        {F::LAYER_HANDLE, &f.layerHandle},
        {F::PARENT_HANDLE, &f.parentHandle}};
    for (const auto &field : fields) {
      if (!*field.second)
        continue;
      const bool objectOnly =
          field.first == F::OBJECT_TYPE || field.first == F::OBJECT_SUBTYPE;
      const bool nodeOnly = field.first == F::NODE_KIND
          || field.first == F::LAYER_HANDLE || field.first == F::PARENT_HANDLE;
      if (q == Query::DISCOVER || (objectOnly && args.kind != "objects")
          || (nodeOnly && args.kind != "nodes"))
        errors.add(field.first, R::INAPPLICABLE);
      if ((*field.second)->size() > m_limits.displayString)
        errors.add(field.first, R::LENGTH);
    }
    if (f.nodeKind && *f.nodeKind != "empty" && *f.nodeKind != "transform"
        && *f.nodeKind != "object" && *f.nodeKind != "transform_array")
      errors.add(F::NODE_KIND, R::UNSUPPORTED);
    for (const auto &field : fields) {
      if (!*field.second
          || (field.first != F::HANDLE && field.first != F::LAYER_HANDLE
              && field.first != F::PARENT_HANDLE))
        continue;
      const auto &handle = **field.second;
      bool syntax = handle.size() == 32;
      for (char c : handle)
        syntax &= (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
      if (!syntax) {
        errors.add(field.first, R::HANDLE_SYNTAX);
        continue;
      }
      const auto expected = field.first == F::LAYER_HANDLE
              || (field.first == F::HANDLE && args.kind == "layers")
          ? identity::HandleKind::LAYER
          : field.first == F::PARENT_HANDLE || args.kind == "nodes"
          ? identity::HandleKind::NODE
          : identity::HandleKind::OBJECT;
      const auto domain = std::stoull(handle.substr(0, 16), nullptr, 16);
      if (identity::IdentityAuthority::handleKind(domain) != expected)
        errors.add(field.first, R::HANDLE_KIND);
    }
    if (f.objectType) {
      bool supported = false;
      for (auto type : kPools)
        supported |= *f.objectType == anari::toString(type);
      for (auto type : {ANARI_ARRAY1D, ANARI_ARRAY2D, ANARI_ARRAY3D})
        supported |= *f.objectType == anari::toString(type);
      if (!supported)
        errors.add(F::OBJECT_TYPE, R::UNSUPPORTED);
    }
    return errors.count == 0;
  }

  // Discovery revision covers membership and each target's returned
  // capabilities; bounded by the (small) host registration.
  std::optional<std::uint64_t> revisionOf(const Job &j) const
  {
    if (j.query == Query::INSPECT) {
      auto *ids = m_identities(j.target.id);
      if (!ids)
        return std::nullopt;
      return ids->inspectionRevision().value;
    }
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&](std::uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    for (std::uint32_t k = 0; k < 2; k++)
      for (std::uint32_t i = 0;; i++) {
        const bool exists = k == 0 ? m_integration.isRegisteredScene(i)
                                   : m_integration.isRegisteredView(i);
        if (!exists)
          break;
        const auto c = m_authority.effectiveCapabilities(
            k == 0 ? Target::scene(i) : Target::view(i));
        mix(k << 8 | i);
        mix(c.read | c.render << 1 | c.edit << 2 | c.resynchronize << 3);
      }
    return h;
  }

  Built make(Query q,
      const std::string &sessionId,
      Target target,
      const PageArgs &args)
  {
    Built b;
    const std::int64_t size = args.pageSize.value_or(m_limits.defaultItems);
    b.diagnostics = requestDiagnostics(q, args);
    if (b.diagnostics.count || !m_limits.nameWork || !m_limits.sliceNameWork) {
      b.code = Outcome::INVALID_REQUEST;
      return b;
    }
    auto job = std::make_shared<Job>();
    job->pool = m_scratch;
    job->query = q;
    job->kind = args.kind;
    job->filters = args.filters;
    job->sessionId = sessionId;
    job->target = target;
    job->maxItems = std::size_t(size);
    job->policy = filterPolicy(args, size);
    if (q == Query::INSPECT && args.kind != "objects")
      if (auto *ids = m_identities(target.id))
        job->identityBinding = layerIdentityBinding(*ids);
    // Charge retained query/filter state to the same global scratch budget as
    // returned items, including no-match scans. Job owns release on every path.
    std::size_t queryBytes = job->policy.size() + job->sessionId.size()
        + job->identityBinding.size() + sizeof(Position);
    for (const auto *s : {&job->filters.name,
             &job->filters.nameContains,
             &job->filters.handle,
             &job->filters.objectType,
             &job->filters.objectSubtype,
             &job->filters.nodeKind,
             &job->filters.layerHandle,
             &job->filters.parentHandle})
      if (*s)
        queryBytes += (*s)->size();
    const auto patternSize =
        job->filters.nameContains ? job->filters.nameContains->size() : 0;
    queryBytes += patternSize * sizeof(std::size_t);
    if (!m_scratch->reserve(queryBytes)) {
      b.code = Outcome::BUSY;
      return b;
    }
    job->reserved = queryBytes;
    // Admission work is O(input bytes), bounded by displayString; no stored
    // name is touched here. The linear KMP failure table is charged scratch.
    job->namePrefix.resize(patternSize);
    if (patternSize) {
      const auto &pattern = *job->filters.nameContains;
      for (std::size_t i = 1, matched = 0; i < patternSize; ++i) {
        while (matched && pattern[i] != pattern[matched])
          matched = job->namePrefix[matched - 1];
        if (pattern[i] == pattern[matched])
          ++matched;
        job->namePrefix[i] = matched;
      }
    }
    if (!args.cursor.empty()) {
      b.code = decodeCursor(args.cursor, *job);
      if (b.code != Outcome::OK) {
        if (b.code == Outcome::INVALID_REQUEST)
          b.diagnostics.add(DiagnosticField::CURSOR, DiagnosticReason::CURSOR);
        return b;
      }
    }
    b.request.kind = q == Query::DISCOVER ? "discover" : "inspect";
    b.request.target = target;
    b.request.capability = session::Capability::READ;
    b.request.sessionWide = q == Query::DISCOVER;
    b.request.canonicalArguments =
        job->policy + ":" + detail::jsonString(args.cursor);
    b.request.deadlineSeconds = m_limits.deadlineSeconds;
    b.request.executor = [this, job](StepContext &ctx) {
      Step s = run(*job, ctx);
      if (s.status == Step::Status::COMPLETE) {
        if (s.outcome == Outcome::OK)
          s.covered = job->listed;
        job->releaseScratch();
      }
      return s;
    };
    return b;
  }

  static Step done(Outcome o, std::string result = {})
  {
    Step s;
    s.outcome = o;
    s.result = std::move(result);
    return s;
  }

  // Appends an item; false when the page must stop (sets job.stop) or the
  // item can never fit (sets tooLarge).
  bool append(Job &j, const std::string &item, bool &tooLarge)
  {
    if (item.size() > m_limits.itemBytes) {
      tooLarge = true;
      return false;
    }
    if (j.returned >= j.maxItems) {
      j.stop = "item";
      return false;
    }
    const std::size_t add = item.size() + 1;
    if (j.items.size() + add + 512 > m_limits.pageBytes
        || !m_scratch->reserve(add)) {
      j.stop = "byte";
      return false;
    }
    j.reserved += add;
    if (!j.items.empty())
      j.items += ',';
    j.items += item;
    j.returned++;
    return true;
  }

  std::string capsJson(const session::EffectiveCapabilities &c) const
  {
    auto b = [](bool v) { return v ? "true" : "false"; };
    return std::string("{\"read\":") + b(c.read) + ",\"render\":" + b(c.render)
        + ",\"edit\":" + b(c.edit) + ",\"resynchronize\":" + b(c.resynchronize)
        + "}";
  }

  std::string display(const std::string &s) const
  {
    if (s.size() <= m_limits.displayString)
      return "{\"text\":" + detail::jsonString(s) + ",\"truncated\":false}";
    return "{\"text\":"
        + detail::jsonString(detail::truncateUtf8(s, m_limits.displayString))
        + ",\"truncated\":true,\"original_length\":" + std::to_string(s.size())
        + "}";
  }

  // Candidate at `pos`; advances pos. Returns false when exhausted.
  bool nextDiscovery(Job &j, bool &tooLarge, bool &exhausted)
  {
    const bool views = j.pos.pool == 1;
    const std::uint32_t i = std::uint32_t(j.pos.index);
    const bool exists = views ? m_integration.isRegisteredView(i)
                              : m_integration.isRegisteredScene(i);
    if (!exists) {
      if (views) {
        if (i < m_integration.viewSlotCount()) {
          j.pos.index++;
          return true;
        }
        exhausted = true;
        return false;
      }
      j.pos = {1, 0};
      return true;
    }
    const Target t = views ? Target::view(i) : Target::scene(i);
    const auto caps = m_authority.effectiveCapabilities(t);
    j.visited++;
    if (!caps.read) {
      j.pos.index++;
      return true;
    }
    std::string item = std::string("{\"kind\":\"") + (views ? "view" : "scene")
        + "\",\"id\":" + std::to_string(i)
        + ",\"capabilities\":" + capsJson(caps);
    if (!views)
      item += std::string(",\"node_edits_exposed\":")
          + (m_integration.nodeEditsExposed(i) ? "true" : "false");
    if (!views)
      item +=
          ",\"inspection\":{\"kinds\":[\"objects\",\"layers\",\"nodes\"],\"default_page_size\":"
          + std::to_string(m_limits.defaultItems)
          + ",\"max_page_size\":" + std::to_string(m_limits.maxItems) + "}";
    if (views) {
      if (auto description = m_integration.viewDescription(i))
        item += "," + viewinfo::identityJson(*description, detail::jsonString);
      const auto policy = m_integration.capturePolicy(i);
      item += ",\"binding_observation\":\"vsr_view_info\",\"capture_policy\":"
          + (policy ? capturePolicyJson(policy->policy,
                          policy->presentedFramesExposed,
                          policy->frameHostBudgetBytes)
                    : viewinfo::policyJson());
      auto *tracker = m_views ? m_views(i) : nullptr;
      const auto scene = m_integration.sceneOfView(i);
      auto *ids = scene ? m_identities(*scene) : nullptr;
      if (policy && tracker) {
        tracker->observeCapturePolicy(policy->revision);
        item += ",\"capture_policy_revision\":"
            + detail::jsonString(
                detail::revisionToken(tracker->capturePolicyRevision()));
      }
      const auto before =
          ids ? ids->inspectionRevision() : identity::InspectionRevision{};
      const auto viewBefore =
          tracker ? tracker->revision() : identity::ViewRevision{};
      auto observed = m_integration.observeViewBinding(i);
      const char *reason =
          observed ? viewinfo::validate(*observed) : "missing_binding";
      if (!ids || !tracker)
        reason = "missing_revision_tracking";
      else if (ids->inspectionRevision() != before
          || tracker->revision() != viewBefore)
        reason = "observation_changed";
      if (observed && ids) {
        for (const auto &reference :
            {std::make_pair(observed->cameraHandle, ANARI_CAMERA),
                std::make_pair(observed->rendererHandle, ANARI_RENDERER)}) {
          auto *object = ids->resolve(reference.first);
          if (reference.first.valid()
              && (!object || object->type() != reference.second))
            reason = "invalid_bound_reference";
        }
      }
      const auto payload = reason
          ? std::string("\"state\":\"unavailable\",\"reason\":")
              + detail::jsonString(reason)
          : viewinfo::bindingJson(*observed, detail::jsonString);
      if (tracker) {
        tracker->observeConfiguration(payload);
        if (!reason) {
          tracker->setCamera(observed->cameraHandle);
          tracker->setRenderer(observed->rendererHandle);
          tracker->setDimensions(observed->width, observed->height);
        }
      }
      if (tracker)
        item += ",\"view_revision\":"
            + detail::jsonString(detail::revisionToken(tracker->revision()));
      item += ",\"effective_binding\":{" + payload;
      if (ids)
        item += ",\"scene_revision\":"
            + detail::jsonString(detail::revisionToken(ids->sceneRevision()));
      if (tracker)
        item += ",\"view_revision\":"
            + detail::jsonString(detail::revisionToken(tracker->revision()));
      item += "}";
    }
    item += "}";
    if (!append(j, item, tooLarge))
      return false;
    j.listed.push_back(t);
    j.pos.index++;
    return true;
  }

  static constexpr anari::DataType kPools[] = {ANARI_ARRAY,
      ANARI_SURFACE,
      ANARI_GEOMETRY,
      ANARI_MATERIAL,
      ANARI_SAMPLER,
      ANARI_VOLUME,
      ANARI_SPATIAL_FIELD,
      ANARI_LIGHT,
      ANARI_CAMERA,
      ANARI_RENDERER};

  static std::size_t poolCapacity(scene::Scene &s, std::size_t p)
  {
    auto &db = s.objectDB();
    switch (p) {
    case 0:
      return db.array.capacity();
    case 1:
      return db.surface.capacity();
    case 2:
      return db.geometry.capacity();
    case 3:
      return db.material.capacity();
    case 4:
      return db.sampler.capacity();
    case 5:
      return db.volume.capacity();
    case 6:
      return db.field.capacity();
    case 7:
      return db.light.capacity();
    case 8:
      return db.camera.capacity();
    default:
      return db.renderer.capacity();
    }
  }

  // Bounded compatibility mirror; the parameter entries are authoritative.
  static std::string editableValues(
      const scene::Object &o, std::size_t budget, std::size_t represented)
  {
    std::string out;
    for (std::size_t i = 0; i < represented; ++i) {
      const auto &p = o.parameterAt(i);
      const auto type = p.value().type();
      if (type != ANARI_FLOAT32 && type != ANARI_FLOAT32_VEC2
          && type != ANARI_FLOAT32_VEC3 && type != ANARI_FLOAT32_VEC4)
        continue;
      const auto entry = (out.empty() ? "" : ",")
          + detail::jsonString(p.name().str()) + ":"
          + detail::numericData<float>(p.value());
      if (entry.size() > budget - out.size())
        break;
      out += entry;
    }
    return out;
  }

  bool nextLayer(Job &j,
      scene::Scene &scene,
      identity::SceneIdentity &ids,
      bool &tooLarge,
      bool &exhausted)
  {
    if (j.pos.index >= scene.numberOfLayers()) {
      exhausted = true;
      return false;
    }
    const auto &state = scene.layers().at_index(j.pos.index).second;
    const auto *layer = state.ptr.get();
    if (!j.pos.namePhase)
      j.visited++;
    const auto h = ids.handleOf(layer);
    const auto handle = detail::hex64(h.domain) + detail::hex64(h.serial);
    const auto &f = j.filters;
    const auto matches = (f.handle && handle != *f.handle)
        ? std::optional<bool>(false)
        : matchName(j, layer->name());
    if (!matches)
      return true;
    if (!*matches) {
      resetName(j.pos);
      j.pos.index++;
      return true;
    }
    const auto root = ids.rootHandle(layer);
    const std::string item = "{\"handle\":\"" + handle
        + "\",\"name\":" + display(layer->name()) + ",\"active\":"
        + (state.active ? "true" : "false") + ",\"root_handle\":\""
        + detail::hex64(root.domain) + detail::hex64(root.serial) + "\"}";
    if (!append(j, item, tooLarge))
      return false;
    resetName(j.pos);
    j.pos.index++;
    return true;
  }

  template <typename Handle>
  static std::string handleText(const Handle &h)
  {
    return detail::hex64(h.domain) + detail::hex64(h.serial);
  }

  static TransformIssue matrixIssue(const core::math::mat4 &matrix);
  static TransformIssue transformIssue(const scene::LayerNodeData &data);
  static const char *issueReason(TransformIssue issue);

  static std::string unavailable(const char *reason)
  {
    return "{\"state\":\"unavailable\",\"reason\":" + detail::jsonString(reason)
        + "}";
  }

  static std::string affordance(const char *reason)
  {
    return reason
        ? "{\"available\":false,\"reason\":" + detail::jsonString(reason) + "}"
        : "{\"available\":true}";
  }

  static std::string transformObservations(const scene::LayerNodeData &data)
  {
    const auto issue = transformIssue(data);
    const char *reason = !data.isTransform() ? "not_scalar_transform"
        : issue != TransformIssue::NONE      ? issueReason(issue)
                                             : nullptr;
    std::string result;
    if (data.isTransform()) {
      result += ",\"local_position\":";
      const core::Any position(data.getTransform()[3].xyz());
      result += reason ? unavailable(reason)
                       : "{\"state\":\"available\",\"data\":"
              + detail::numericData<float>(position) + "}";
    }
    const char *srtReason = reason;
    const auto srt = data.getTransformSRT();
    if (!srtReason) {
      for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row)
          if (!std::isfinite(srt[column][row]))
            srtReason = "non_finite_srt_cache";
    }
    if (!srtReason) {
      // Exactly the node editor's degree-based Y then X then Z convention.
      auto rotation = core::math::IDENTITY_MAT4;
      const core::math::float3 axes[] = {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}};
      for (int i = 0; i < 3; ++i)
        rotation = core::math::mul(rotation,
            core::math::rotation_matrix(core::math::rotation_quat(
                axes[i], core::math::radians(srt[1][i]))));
      const auto rebuilt =
          core::math::mul(rotation, core::math::scaling_matrix(srt[0]));
      const auto original = data.getTransform();
      for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
          const auto value = original[column][row];
          if (!std::isfinite(rebuilt[column][row])
              || std::abs(double(value) - double(rebuilt[column][row]))
                  > 1e-5 * std::max(1., std::abs(double(value))))
            srtReason = "unfaithful_srt_cache";
        }
      }
    }
    if (data.isTransform()) {
      result += ",\"rotation\":";
      result += srtReason ? unavailable(srtReason)
                          : "{\"state\":\"available\",\"data\":"
              + detail::numericData<float>(core::Any(srt[1])) + "}";
      result += ",\"scale\":";
      result += srtReason ? unavailable(srtReason)
                          : "{\"state\":\"available\",\"data\":"
              + detail::numericData<float>(core::Any(srt[0])) + "}";
    }
    return result + ",\"edit_affordances\":{\"enabled\":" + affordance(nullptr)
        + ",\"matrix\":"
        + affordance(data.isTransform() ? nullptr : "not_scalar_transform")
        + ",\"position\":" + affordance(reason) + ",\"rotation\":"
        + affordance(srtReason) + ",\"scale\":" + affordance(srtReason) + "}";
  }

  bool nextNode(Job &j,
      scene::Scene &scene,
      identity::SceneIdentity &ids,
      bool &tooLarge,
      bool &exhausted)
  {
    if (j.pos.pool >= scene.numberOfLayers()) {
      exhausted = true;
      return false;
    }
    const auto &state = scene.layers().at_index(j.pos.pool).second;
    auto *layer = state.ptr.get();
    auto node = j.pos.index ? layer->at(j.pos.index - 1) : layer->root();
    if (j.pos.phase || !j.pos.namePhase)
      j.visited++;
    auto advance = [&] {
      resetName(j.pos);
      if (!node->isLeaf()) {
        j.pos.index = node->next().index() + 1;
        j.pos.phase = 0;
      } else {
        j.pos.phase = 2;
        j.pos.aux = node.index() + 1;
      }
    };
    // Climb one edge at a time; even a deep final branch yields boundedly.
    if (j.pos.phase == 2) {
      auto current = layer->at(j.pos.aux - 1);
      if (current->isRoot()) {
        j.pos = {j.pos.pool + 1, 0};
      } else if (current->sibling() != current->parent()) {
        j.pos.index = current->sibling().index() + 1;
        j.pos.phase = 0;
      } else {
        j.pos.aux = current->parent().index() + 1;
      }
      return true;
    }
    const auto &data = node->value();
    const std::string kind = data.isTransform() ? "transform"
        : anari::isArray(data.type())           ? "transform_array"
        : data.isObject()                       ? "object"
                                                : "empty";
    const auto handle = handleText(ids.handleOf(node));
    const auto layerHandle = handleText(ids.handleOf(layer));
    const auto parent = node->parent();
    const auto parentHandle =
        node->isRoot() ? std::string{} : handleText(ids.handleOf(parent));
    if (j.pos.phase == 0) {
      const auto &f = j.filters;
      if ((f.handle && handle != *f.handle)
          || (f.nodeKind && kind != *f.nodeKind)
          || (f.layerHandle && layerHandle != *f.layerHandle)
          || (f.parentHandle && parentHandle != *f.parentHandle)) {
        advance();
        return true;
      }
      const auto matches = matchName(j, data.name());
      if (!matches)
        return true;
      if (!*matches) {
        advance();
        return true;
      }
      j.pos.index = node.index() + 1;
      j.pos.effective = state.active && data.isEnabled();
      j.pos.world = data.getTransform();
      j.pos.worldIssue = transformIssue(data);
      const auto dependencies = ids.beginNodeDependencies(ids.handleOf(node));
      j.pos.structuralVersion = dependencies->structuralVersion;
      j.pos.transformVersion = 0;
      j.pos.aux = dependencies->ancestorSlot;
      j.pos.phase = j.pos.aux ? 1 : 3;
      return true;
    }
    if (j.pos.phase == 1) {
      auto ancestor = layer->at(j.pos.aux - 1);
      j.pos.effective = j.pos.effective && ancestor->value().isEnabled();
      const auto issue = transformIssue(ancestor->value());
      if (issue != TransformIssue::NONE)
        j.pos.worldIssue = issue;
      if (j.pos.worldIssue == TransformIssue::NONE
          && ancestor->value().isTransform()) {
        j.pos.world =
            core::math::mul(ancestor->value().getTransform(), j.pos.world);
        if (matrixIssue(j.pos.world) != TransformIssue::NONE)
          j.pos.worldIssue = TransformIssue::WORLD_OVERFLOW;
      }
      identity::NodeDependencyWalk walk{ids.handleOf(node),
          ids.handleOf(layer),
          j.pos.aux,
          j.pos.structuralVersion,
          j.pos.transformVersion};
      if (!ids.advanceNodeDependencies(walk)) {
        exhausted = true;
        return false;
      }
      j.pos.aux = walk.ancestorSlot;
      j.pos.structuralVersion = walk.structuralVersion;
      j.pos.transformVersion = walk.transformVersion;
      if (!j.pos.aux)
        j.pos.phase = 3;
      return true;
    }
    std::string item = "{\"handle\":\"" + handle + "\",\"layer_handle\":\""
        + layerHandle + "\",\"parent_handle\":"
        + (node->isRoot() ? "null" : detail::jsonString(parentHandle))
        + ",\"root\":" + (node->isRoot() ? "true" : "false") + ",\"name\":"
        + display(data.name()) + ",\"node_kind\":" + detail::jsonString(kind)
        + ",\"enabled\":" + (data.isEnabled() ? "true" : "false")
        + ",\"effective_enabled\":" + (j.pos.effective ? "true" : "false")
        + ",\"node_edit_version\":"
        + detail::jsonString("ne:" + handle + ":"
            + std::to_string(ids.nodeEditVersion(ids.handleOf(node))->version))
        + ",\"structural_version\":"
        + detail::jsonString("ns:" + handle + ":" + layerHandle + ":"
            + std::to_string(j.pos.structuralVersion))
        + ",\"ancestry_version\":"
        + detail::jsonString("na:" + handle + ":" + layerHandle + ":"
            + std::to_string(j.pos.structuralVersion) + ":"
            + std::to_string(j.pos.transformVersion));
    if (data.isTransform()) {
      item += ",\"local_matrix\":";
      // parameterValue already encodes matrices as complete row arrays and
      // tags non-finite components, without consulting the SRT cache.
      item += detail::parameterValue(data.getValueRaw(), scene, ids, m_limits);
    } else if (data.isObject()) {
      item += kind == "transform_array" ? ",\"array\":" : ",\"object\":";
      item += detail::parameterValue(data.getValueRaw(), scene, ids, m_limits);
    }
    item += ",\"world_matrix\":";
    item += j.pos.worldIssue != TransformIssue::NONE
        ? unavailable(issueReason(j.pos.worldIssue))
        : detail::parameterValue(core::Any(j.pos.world), scene, ids, m_limits);
    item += transformObservations(data);
    if (!append(j, item + "}", tooLarge))
      return false;
    advance();
    return true;
  }

  bool nextInspection(Job &j,
      scene::Scene &scene,
      identity::SceneIdentity &ids,
      bool &tooLarge,
      bool &exhausted)
  {
    if (j.pos.pool >= std::size(kPools)) {
      exhausted = true;
      return false;
    }
    if (!j.exactObject && j.pos.index >= poolCapacity(scene, j.pos.pool)) {
      j.pos = {j.pos.pool + 1, 0};
      return true;
    }
    if (!j.pos.namePhase)
      j.visited++;
    auto *o = j.exactObject ? j.exactObject
                            : scene.getObject(kPools[j.pos.pool], j.pos.index);
    auto advance = [&] {
      resetName(j.pos);
      if (j.exactObject)
        j.pos.pool = std::size(kPools);
      else
        j.pos.index++;
    };
    if (!o) {
      j.pos.index++;
      return true;
    }
    const auto h = ids.handleOf(o);
    const auto handle = detail::hex64(h.domain) + detail::hex64(h.serial);
    const auto &f = j.filters;
    if ((f.handle && handle != *f.handle)
        || (f.objectType && *f.objectType != anari::toString(o->type()))
        || (f.objectSubtype && *f.objectSubtype != o->subtype().str())) {
      advance();
      return true;
    }
    const auto matches = matchName(j, o->name());
    if (!matches)
      return true;
    if (!*matches) {
      advance();
      return true;
    }
    const auto v = ids.parameterVersion(h);
    std::string item = "{\"handle\":\"" + detail::hex64(h.domain)
        + detail::hex64(h.serial)
        + "\",\"type\":" + detail::jsonString(anari::toString(o->type()))
        + ",\"subtype\":" + detail::jsonString(o->subtype().str())
        + ",\"version\":" + std::to_string(v ? v->version : 0) + ",\"name\":";
    std::string name = display(o->name());
    std::string params = ",\"parameters\":[";
    bool paramsTruncated = false;
    std::size_t represented = 0;
    for (std::size_t p = 0; p < o->numParameters(); p++) {
      const auto &par = o->parameterAt(p);
      std::string e = std::string(p ? "," : "")
          + "{\"name\":" + detail::jsonString(par.name().str()) + ",\"type\":"
          + detail::jsonString(anari::toString(par.value().type()))
          + ",\"value\":"
          + detail::parameterValue(par.value(), scene, ids, m_limits)
          + ",\"editable\":" + (isEditableParameter(par) ? "true" : "false");
      if (par.hasMin())
        e += ",\"min\":"
            + detail::parameterValue(par.min(), scene, ids, m_limits);
      if (par.hasMax())
        e += ",\"max\":"
            + detail::parameterValue(par.max(), scene, ids, m_limits);
      if (!par.stringValues().empty()) {
        e += ",\"string_values\":[";
        for (const auto &choice : par.stringValues()) {
          if (e.size() + choice.size() > m_limits.optionalBytes) {
            // Omit the entire entry rather than advertise a partial enum.
            e.resize(m_limits.optionalBytes + 1);
            break;
          }
          e += (e.back() == '[' ? "" : ",") + detail::jsonString(choice);
        }
        e += "]";
      }
      e += "}";
      if (params.size() + e.size() > m_limits.optionalBytes) {
        paramsTruncated = true;
        break;
      }
      params += e;
      represented++;
    }
    params += "],\"parameters_truncated\":";
    params += paramsTruncated ? "true" : "false";
    if (paramsTruncated)
      params +=
          ",\"parameters_reason\":\"optional_byte_limit\",\"parameters_omitted\":"
          + std::to_string(o->numParameters() - represented);
    const auto vals = editableValues(*o,
        params.size() + 16 < m_limits.optionalBytes
            ? m_limits.optionalBytes - params.size() - 16
            : 0,
        represented);
    if (!vals.empty())
      params += ",\"values\":{" + vals + "}";
    item += name + params;
    if (const auto *array = dynamic_cast<const scene::Array *>(o))
      item += ",\"summary\":" + detail::arraySummary(*array);
    item += "}";
    if (!append(j, item, tooLarge))
      return false;
    advance();
    return true;
  }

  // A pending result never advances the candidate. Each byte comparison,
  // including fallback with an unchanged input offset, costs one work unit.
  std::optional<bool> matchName(Job &j, const std::string &name)
  {
    auto &p = j.pos;
    const auto &f = j.filters;
    if (!p.namePhase)
      p.namePhase = 1;
    auto charge = [&] {
      if (j.nameWork >= m_limits.nameWork
          || j.sliceNameWork >= m_limits.sliceNameWork)
        return false;
      ++j.nameWork;
      ++j.sliceNameWork;
      return true;
    };
    if (p.namePhase == 1) {
      if (f.name) {
        if (name.size() != f.name->size())
          return false;
        while (p.nameOffset < name.size()) {
          if (!charge())
            return std::nullopt;
          if (name[p.nameOffset] != (*f.name)[p.nameOffset])
            return false;
          ++p.nameOffset;
        }
      }
      p.namePhase = 2;
      p.nameOffset = 0;
    }
    if (p.namePhase == 2 && f.nameContains && !f.nameContains->empty()) {
      const auto &pattern = *f.nameContains;
      while (p.nameOffset < name.size()) {
        if (!charge())
          return std::nullopt;
        if (name[p.nameOffset] == pattern[p.nameMatched]) {
          ++p.nameOffset;
          if (++p.nameMatched == pattern.size()) {
            p.namePhase = 3;
            return true;
          }
        } else if (p.nameMatched) {
          p.nameMatched = j.namePrefix[p.nameMatched - 1];
        } else {
          ++p.nameOffset;
        }
      }
      return false;
    }
    p.namePhase = 3;
    return true;
  }

  static void resetName(Position &p)
  {
    p.namePhase = p.nameOffset = p.nameMatched = 0;
  }

  static std::string layerIdentityBinding(const identity::SceneIdentity &ids)
  {
    const auto r = ids.inspectionRevision();
    return detail::hex64(r.launch.activation) + detail::hex64(r.launch.nonce)
        + detail::hex64(r.domain);
  }

  Step run(Job &j, StepContext &ctx)
  {
    if (ctx.abandoned)
      return done(Outcome::CANCELLED);
    identity::SceneIdentity *ids = nullptr;
    if (j.query == Query::INSPECT) {
      ids = m_identities(j.target.id);
      if (!ids || ids->scene() != &ctx.scene)
        return done(Outcome::INVALID_REQUEST, "{\"error\":\"unsupported\"}");
      if (j.kind != "objects"
          && j.identityBinding != layerIdentityBinding(*ids))
        return done(Outcome::CURSOR_STALE);
      // Name/metadata changes arrive incrementally via the scene's update
      // delegate; no per-slice O(objects) refresh.
    }
    const auto rev = revisionOf(j);
    if (!rev)
      return done(Outcome::INVALID_REQUEST, "{\"error\":\"unsupported\"}");
    if (!j.started) {
      j.started = true;
      j.revision = *rev;
    } else if (*rev != j.revision) {
      return done(Outcome::CURSOR_STALE);
    }

    if (ids && j.filters.handle) {
      const auto &s = *j.filters.handle;
      const auto launch = ids->inspectionRevision().launch;
      const auto domain = std::stoull(s.substr(0, 16), nullptr, 16);
      const auto serial = std::stoull(s.substr(16, 16), nullptr, 16);
      if (j.kind == "layers") {
        if (!ids->resolve(identity::LayerHandle{launch, domain, serial}))
          return done(Outcome::CONFLICT);
      } else if (j.kind == "nodes") {
        if (!ids->resolve(identity::NodeHandle{launch, domain, serial}))
          return done(Outcome::CONFLICT);
      } else {
        j.exactObject =
            ids->resolve(identity::ObjectHandle{launch, domain, serial});
        if (!j.exactObject)
          return done(Outcome::CONFLICT);
      }
    }

    if (ids) {
      const auto launch = ids->inspectionRevision().launch;
      for (const auto &s : {j.filters.layerHandle, j.filters.parentHandle}) {
        if (!s)
          continue;
        const auto domain = std::stoull(s->substr(0, 16), nullptr, 16);
        const auto serial = std::stoull(s->substr(16, 16), nullptr, 16);
        if (identity::IdentityAuthority::handleKind(domain)
            == identity::HandleKind::LAYER) {
          if (!ids->resolve(identity::LayerHandle{launch, domain, serial}))
            return done(Outcome::CONFLICT);
        } else if (!ids->resolve(identity::NodeHandle{launch, domain, serial}))
          return done(Outcome::CONFLICT);
      }
    }
    std::size_t slice = 0;
    j.sliceNameWork = 0;
    bool tooLarge = false, exhausted = false;
    while (slice < m_limits.sliceVisits
        && j.sliceNameWork < m_limits.sliceNameWork) {
      if (j.visited >= m_limits.visits || j.nameWork >= m_limits.nameWork) {
        j.stop = "work";
        break;
      }
      const std::size_t before = j.visited;
      const bool more = j.query == Query::DISCOVER
          ? nextDiscovery(j, tooLarge, exhausted)
          : j.kind == "layers"
          ? nextLayer(j, ctx.scene, *ids, tooLarge, exhausted)
          : j.kind == "nodes"
          ? nextNode(j, ctx.scene, *ids, tooLarge, exhausted)
          : nextInspection(j, ctx.scene, *ids, tooLarge, exhausted);
      if (tooLarge)
        return done(Outcome::ITEM_TOO_LARGE);
      if (!more)
        break;
      slice += j.visited - before;
      // Pool transitions count as a bounded step too.
      if (j.visited == before)
        slice++;
    }
    if (!exhausted && !j.stop && j.nameWork >= m_limits.nameWork)
      j.stop = "work";
    if (!exhausted && !j.stop)
      return Step{Step::Status::YIELD};

    std::string r = "{\"revision\":\"" + detail::hex64(j.revision)
        + "\",\"visited\":" + std::to_string(j.visited)
        + ",\"name_work\":" + std::to_string(j.nameWork)
        + ",\"returned\":" + std::to_string(j.returned) + ",\"items\":["
        + j.items + "],\"complete\":" + (exhausted ? "true" : "false");
    if (ids)
      r += ",\"scene_revision\":\""
          + detail::revisionToken(ids->sceneRevision()) + "\"";
    if (!exhausted)
      r += ",\"stop\":\"" + std::string(j.stop) + "\",\"cursor\":\""
          + encodeCursor(j) + "\"";
    return done(Outcome::OK, r + "}");
  }

  const Integration &m_integration;
  session::SessionAuthority &m_authority;
  IdentityLookup m_identities;
  ViewLookup m_views;
  PageLimits m_limits;
  std::shared_ptr<ScratchPool> m_scratch;
  std::uint64_t m_key[2]{};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline RequestDiagnostics Inspector::requestDiagnostics(
    Query q, const PageArgs &args) const
{
  RequestDiagnostics errors;
  validFilters(q, args, &errors);
  const auto size = args.pageSize.value_or(m_limits.defaultItems);
  if (size < 1 || size > m_limits.maxItems)
    errors.add(DiagnosticField::PAGE_SIZE, DiagnosticReason::RANGE);
  return errors;
}

inline Built Inspector::makeBounds(
    const std::string &sessionId, SceneId scene, const bounds::Args &args)
{
  constexpr bounds::Limits limits;
  static_assert(limits.valid(), "invalid fixed bounds limits");
  if (args.nodeHandle || args.includeDisabled.value_or(false)
      || !m_integration.isRegisteredScene(scene))
    return {Outcome::INVALID_REQUEST, {}};
  const auto target = Target::scene(scene);
  if (m_authority.authorizeExecution(
          sessionId, target, session::Capability::READ)
      != session::Code::OK)
    return {Outcome::FORBIDDEN, {}};
  // RAII lifetime follows the admitted executor, including unadmitted requests
  // and all engine terminal paths. Use the inspection pool, never another pool.
  const auto bytes = sizeof(Job) + bounds::TEMPORARY_BYTES;
  Job reservation;
  reservation.pool = m_scratch;
  if (!m_scratch->reserve(bytes, limits.scratchBytes))
    return {Outcome::BUSY, {}};
  reservation.reserved = bytes;
  auto job = std::make_shared<Job>();
  job->pool = m_scratch;
  job->reserved = bytes;
  reservation.reserved = 0;
  Request request;
  request.kind = "bounds";
  request.target = target;
  request.canonicalArguments = "bounds:" + std::to_string(scene)
      + (args.includeDisabled ? ":explicit_false" : ":omitted")
      + ":effective_false";
  request.deadlineSeconds = limits.deadlineSeconds;
  request.executor = [this, scene, sessionId, job](StepContext &ctx) {
    if (ctx.abandoned) {
      job->releaseScratch();
      return Step{Step::Status::COMPLETE, Outcome::CANCELLED};
    }
    auto *ids = m_identities(scene);
    if (!ids) {
      job->releaseScratch();
      return Step{Step::Status::COMPLETE, Outcome::INVALID_REQUEST};
    }
    auto readAllowed = [&] {
      return m_authority.authorizeExecution(
                 sessionId, Target::scene(scene), session::Capability::READ)
          == session::Code::OK;
    };
    // Identity lookup is a host callback too: recheck before the driver seam.
    if (!readAllowed()) {
      job->releaseScratch();
      return Step{Step::Status::COMPLETE, Outcome::FORBIDDEN};
    }
    if (ctx.isAbandoned && ctx.isAbandoned()) {
      job->releaseScratch();
      return Step{Step::Status::COMPLETE, Outcome::TIMEOUT};
    }
    const auto provider = m_integration.worldBoundsProvider(scene);
    rendering::WorldBoundsObservation observation;
    const bool bound = bool(provider.observe);
    if (bound && provider.isCurrent)
      observation = provider.observe();
    else if (bound)
      observation.reason = rendering::WorldBoundsReason::UNQUALIFIED_SOURCE;
    // ANARI_WAIT cannot be interrupted. A qualified host owns the finite-call
    // guarantee; checking the clock here only suppresses late publication.
    if (!readAllowed()) {
      job->releaseScratch();
      return Step{Step::Status::COMPLETE, Outcome::FORBIDDEN};
    }
    if (ctx.isAbandoned && ctx.isAbandoned()) {
      job->releaseScratch();
      return Step{Step::Status::COMPLETE, Outcome::TIMEOUT};
    }
    const char *invalid = bounds::validate(observation);
    if (!invalid && !observation.source.identity.empty()
        && !provider.isCurrent(observation.source)) {
      observation.state = rendering::WorldBoundsState::UNAVAILABLE;
      observation.reason = rendering::WorldBoundsReason::RETIRED_SOURCE;
      observation.source = {};
    }
    std::string payload = "\"state\":"
        + detail::jsonString(
            invalid ? "unavailable" : bounds::stateName(observation.state));
    const auto *reason =
        invalid ? invalid : bounds::reasonName(observation.reason);
    if (reason)
      payload += ",\"reason\":" + detail::jsonString(reason);
    if (bound) {
      payload +=
          ",\"coverage\":\"viewport_world\","
          "\"covered_scene_revision\":null,"
          "\"revision_basis\":\"observation_only\"";
      if (!invalid && !observation.source.identity.empty()) {
        const auto &s = observation.source;
        payload +=
            ",\"source\":{\"kind\":\"interactive_viewport\","
            "\"selection\":\"primary_live_interactive\",\"identity\":"
            + detail::jsonString(s.identity) + ",\"generation\":"
            + detail::jsonString(std::to_string(s.generation))
            + ",\"device_identifier\":" + detail::jsonString(s.deviceIdentifier)
            + ",\"device_subtype\":" + detail::jsonString(s.deviceSubtype)
            + "}";
      }
      if (!invalid
          && observation.state == rendering::WorldBoundsState::AVAILABLE) {
        auto vectorJson = [](const auto &v) {
          return "[" + viewinfo::number(v[0]) + "," + viewinfo::number(v[1])
              + "," + viewinfo::number(v[2]) + "]";
        };
        payload += ",\"min\":" + vectorJson(observation.min)
            + ",\"max\":" + vectorJson(observation.max);
      }
    }
    const auto result = "{" + payload + ",\"scene_id\":" + std::to_string(scene)
        + ",\"target\":{\"kind\":\"scene\",\"scene_id\":"
        + std::to_string(scene) + "},\"scene_revision\":"
        + detail::jsonString(detail::revisionToken(ids->sceneRevision()))
        + ",\"inspection_revision\":"
        + detail::jsonString(detail::revisionToken(ids->inspectionRevision()))
        + (bound
                ? ",\"selection\":{\"include_disabled\":false,"
                  "\"basis\":\"existing_world_filtering_and_external_instances\"}}"
                : ",\"selection\":{\"include_disabled\":false,"
                  "\"active_layers_only\":true,\"effectively_enabled_only\":true}}");
    job->releaseScratch();
    const auto read = m_authority.authorizeExecution(
        sessionId, Target::scene(scene), session::Capability::READ);
    if (read != session::Code::OK)
      return Step{Step::Status::COMPLETE, operations::fromSessionCode(read)};
    if (result.size() > bounds::MAX_RESULT_BYTES
        || result.size() > m_limits.itemBytes)
      return Step{Step::Status::COMPLETE, Outcome::ITEM_TOO_LARGE};
    return Step{Step::Status::COMPLETE, Outcome::OK, result};
  };
  return {Outcome::OK, std::move(request)};
}

inline Built Inspector::makeViewInfo(const std::string &sessionId, ViewId view)
{
  const auto sceneId = m_integration.sceneOfView(view);
  if (!sceneId)
    return {Outcome::INVALID_REQUEST, {}};
  if (m_authority.authorizeExecution(
          sessionId, Target::scene(*sceneId), session::Capability::READ)
      != session::Code::OK)
    return {Outcome::FORBIDDEN, {}};
  Request request;
  request.kind = "view_info";
  request.target = Target::view(view);
  request.canonicalArguments = "view_info:" + std::to_string(view);
  request.deadlineSeconds = m_limits.deadlineSeconds;
  request.executor = [this, view, sessionId](StepContext &) {
    const auto scene = m_integration.sceneOfView(view);
    if (!scene
        || m_authority.authorizeExecution(
               sessionId, Target::scene(*scene), session::Capability::READ)
            != session::Code::OK)
      return Step{Step::Status::COMPLETE, Outcome::FORBIDDEN};
    auto *ids = scene ? m_identities(*scene) : nullptr;
    auto *tracker = m_views ? m_views(view) : nullptr;
    const auto description = m_integration.viewDescription(view);
    if (!description)
      return Step{Step::Status::COMPLETE, Outcome::SUPERSEDED};
    const auto identityJson =
        viewinfo::identityJson(*description, detail::jsonString);
    const auto policy = m_integration.capturePolicy(view);
    const auto policyJson = policy ? capturePolicyJson(policy->policy,
                                         policy->presentedFramesExposed,
                                         policy->frameHostBudgetBytes)
                                   : viewinfo::policyJson();
    if (tracker && policy)
      tracker->observeCapturePolicy(policy->revision);
    if (!ids || !tracker) {
      const std::string result = "{\"view_id\":" + std::to_string(view)
          + ",\"scene_id\":" + std::to_string(*scene)
          + ",\"scene_revision\":"
          + (ids ? detail::jsonString(detail::revisionToken(ids->sceneRevision())) : "null")
          + ",\"view_revision\":null,\"capture_policy_revision\":null,"
            "\"state\":\"unavailable\",\"reason\":\"missing_revision_tracking\","
            "\"capture_policy\":" + policyJson + "," + identityJson + "}";
      return Step{Step::Status::COMPLETE,
          result.size() > m_limits.itemBytes ? Outcome::ITEM_TOO_LARGE
                                             : Outcome::OK,
          result.size() > m_limits.itemBytes ? std::string{} : result};
    }
    const auto before = ids->inspectionRevision();
    const auto viewBefore = tracker->revision();
    auto binding = m_integration.observeViewBinding(view);
    if (m_authority.authorizeExecution(
            sessionId, Target::scene(*scene), session::Capability::READ)
            != session::Code::OK
        || m_authority.authorizeExecution(
               sessionId, Target::view(view), session::Capability::READ)
            != session::Code::OK)
      return Step{Step::Status::COMPLETE, Outcome::FORBIDDEN};
    if (!m_integration.isRegisteredView(view)
        || ids->inspectionRevision() != before
        || tracker->revision() != viewBefore
        || (policy
            && (!m_integration.capturePolicy(view)
                || m_integration.capturePolicy(view)->revision
                    != policy->revision)))
      return Step{Step::Status::COMPLETE, Outcome::SUPERSEDED};
    const char *reason =
        binding ? viewinfo::validate(*binding) : "missing_binding";
    auto validReference = [&](const identity::ObjectHandle &h,
                              anari::DataType type) {
      auto *object = ids->resolve(h);
      return !h.valid() || (object && object->type() == type);
    };
    if (binding
        && (!validReference(binding->cameraHandle, ANARI_CAMERA)
            || !validReference(binding->rendererHandle, ANARI_RENDERER)))
      reason = "invalid_bound_reference";
    std::string payload;
    if (reason)
      payload =
          "\"state\":\"unavailable\",\"reason\":" + detail::jsonString(reason);
    else
      payload = viewinfo::bindingJson(*binding, detail::jsonString);
    if (payload.size() > viewinfo::MAX_BINDING_BYTES
        || payload.size() > m_limits.itemBytes)
      return Step{Step::Status::COMPLETE, Outcome::ITEM_TOO_LARGE};
    tracker->observeConfiguration(payload);
    if (!reason) {
      tracker->setCamera(binding->cameraHandle);
      tracker->setRenderer(binding->rendererHandle);
      tracker->setDimensions(binding->width, binding->height);
    }
    const std::string result = "{\"view_id\":" + std::to_string(view)
        + ",\"scene_id\":" + std::to_string(*scene) + ",\"scene_revision\":"
        + detail::jsonString(detail::revisionToken(ids->sceneRevision()))
        + ",\"view_revision\":"
        + detail::jsonString(detail::revisionToken(tracker->revision()))
        + ",\"capture_policy_revision\":"
        + detail::jsonString(
            detail::revisionToken(tracker->capturePolicyRevision()))
        + ",\"capture_policy\":" + policyJson + "," + identityJson + ","
        + payload + "}";
    if (result.size() > m_limits.itemBytes)
      return Step{Step::Status::COMPLETE, Outcome::ITEM_TOO_LARGE};
    return Step{Step::Status::COMPLETE, Outcome::OK, result};
  };
  return {Outcome::OK, std::move(request)};
}

inline Inspector::TransformIssue Inspector::matrixIssue(
    const core::math::mat4 &matrix)
{
  for (int column = 0; column < 4; ++column)
    for (int row = 0; row < 4; ++row)
      if (!std::isfinite(matrix[column][row]))
        return TransformIssue::NON_FINITE;
  return matrix[0][3] != 0 || matrix[1][3] != 0 || matrix[2][3] != 0
          || matrix[3][3] != 1
      ? TransformIssue::NON_AFFINE
      : TransformIssue::NONE;
}

inline Inspector::TransformIssue Inspector::transformIssue(
    const scene::LayerNodeData &data)
{
  return data.isTransform()         ? matrixIssue(data.getTransform())
      : anari::isArray(data.type()) ? TransformIssue::ARRAY_ANCESTRY
                                    : TransformIssue::NONE;
}

inline const char *Inspector::issueReason(TransformIssue issue)
{
  return issue == TransformIssue::NON_FINITE    ? "non_finite_transform"
      : issue == TransformIssue::NON_AFFINE     ? "non_affine_transform"
      : issue == TransformIssue::ARRAY_ANCESTRY ? "transform_array_ancestry"
                                                : "world_transform_overflow";
}

} // namespace vsr::integration::inspection

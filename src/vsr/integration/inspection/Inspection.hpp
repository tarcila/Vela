// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Header-only: the vsr_integration library globs only top-level sources (see
// ticket 19).

// vsr_integration
#include "vsr/integration/identity/Identity.hpp"
#include "vsr/integration/operations/Operations.hpp"
#include "vsr/integration/values/EditableValues.hpp"
// std
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <random>
#include <string>
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
 * Inspection items carry "values" for the §8 editable parameters of matte
 * materials and directional lights (addendum 04): numbers are the binary64 of
 * the stored float32; direction is [azimuth, elevation] in degrees.
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
};

// Global scratch accounting shared by all admitted discovery/inspection work.
class ScratchPool
{
 public:
  explicit ScratchPool(std::size_t capacity) : m_capacity(capacity) {}
  bool reserve(std::size_t n)
  {
    auto cur = m_used.load();
    do {
      if (cur + n > m_capacity)
        return false;
    } while (!m_used.compare_exchange_weak(cur, cur + n));
    return true;
  }
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

namespace detail {

inline std::uint64_t rotl(std::uint64_t x, int b)
{
  return (x << b) | (x >> (64 - b));
}

// SipHash-2-4 keyed MAC.
inline std::uint64_t siphash(
    const std::uint64_t key[2], const std::string &msg)
{
  std::uint64_t v0 = 0x736f6d6570736575ull ^ key[0];
  std::uint64_t v1 = 0x646f72616e646f6dull ^ key[1];
  std::uint64_t v2 = 0x6c7967656e657261ull ^ key[0];
  std::uint64_t v3 = 0x7465646279746573ull ^ key[1];
  auto round = [&] {
    v0 += v1; v1 = rotl(v1, 13); v1 ^= v0; v0 = rotl(v0, 32);
    v2 += v3; v3 = rotl(v3, 16); v3 ^= v2;
    v0 += v3; v3 = rotl(v3, 21); v3 ^= v0;
    v2 += v1; v1 = rotl(v1, 17); v1 ^= v2; v2 = rotl(v2, 32);
  };
  const std::size_t n = msg.size();
  std::size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    std::uint64_t m;
    std::memcpy(&m, msg.data() + i, 8);
    v3 ^= m;
    round(); round();
    v0 ^= m;
  }
  std::uint64_t b = std::uint64_t(n) << 56;
  for (std::size_t j = 0; i + j < n; j++)
    b |= std::uint64_t(std::uint8_t(msg[i + j])) << (8 * j);
  v3 ^= b;
  round(); round();
  v0 ^= b;
  v2 ^= 0xff;
  round(); round(); round(); round();
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
inline std::string truncateUtf8(const std::string &s, std::size_t limit)
{
  std::size_t n = limit;
  while (n > 0 && (std::uint8_t(s[n]) & 0xC0) == 0x80)
    n--;
  return s.substr(0, n);
}

} // namespace detail

enum class Query
{
  DISCOVER,
  INSPECT
};

struct PageArgs
{
  std::optional<std::int64_t> pageSize; // omitted -> default
  std::string cursor; // empty -> first page
};

struct Built
{
  Outcome code{Outcome::OK}; // INVALID_REQUEST for bad page_size
  Request request;
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

 private:
  struct Position
  {
    std::uint64_t pool{0};
    std::uint64_t index{0};
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
    std::string sessionId;
    Target target;
    std::size_t maxItems{0};
    Position pos;
    bool started{false};
    std::uint64_t revision{0};
    std::size_t visited{0};
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
        + std::to_string(int(j.target.kind)) + ":"
        + std::to_string(j.target.id) + ":" + std::to_string(revision);
  }

  std::string encodeCursor(const Job &j) const
  {
    const std::string payload = detail::hex64(j.pos.pool)
        + detail::hex64(j.pos.index) + detail::hex64(j.revision);
    return payload + mac(binding(j, j.revision) + ":" + payload);
  }

  // Returns false for tampered/foreign cursors.
  bool decodeCursor(const std::string &c, Job &j) const
  {
    if (c.size() != 64)
      return false;
    for (char ch : c)
      if (!std::isxdigit(std::uint8_t(ch)) || std::isupper(std::uint8_t(ch)))
        return false;
    const std::string payload = c.substr(0, 48);
    const std::uint64_t rev = std::stoull(c.substr(32, 16), nullptr, 16);
    if (mac(binding(j, rev) + ":" + payload) != c.substr(48))
      return false;
    j.pos.pool = std::stoull(c.substr(0, 16), nullptr, 16);
    j.pos.index = std::stoull(c.substr(16, 16), nullptr, 16);
    j.revision = rev;
    j.started = true;
    return true;
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
    if (size < 1 || size > m_limits.maxItems) {
      b.code = Outcome::INVALID_REQUEST;
      return b;
    }
    auto job = std::make_shared<Job>();
    job->pool = m_scratch;
    job->query = q;
    job->sessionId = sessionId;
    job->target = target;
    job->maxItems = std::size_t(size);
    if (!args.cursor.empty() && !decodeCursor(args.cursor, *job)) {
      b.code = Outcome::INVALID_REQUEST;
      return b;
    }
    b.request.kind = q == Query::DISCOVER ? "discover" : "inspect";
    b.request.target = target;
    b.request.capability = session::Capability::READ;
    b.request.sessionWide = q == Query::DISCOVER;
    b.request.canonicalArguments = "{\"page_size\":" + std::to_string(size)
        + ",\"cursor\":" + detail::jsonString(args.cursor) + "}";
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
    std::string item = std::string("{\"kind\":\"")
        + (views ? "view" : "scene") + "\",\"id\":" + std::to_string(i)
        + ",\"capabilities\":" + capsJson(caps);
    if (views && m_views)
      if (auto *tracker = m_views(i))
        item += ",\"view_revision\":\""
            + detail::revisionToken(tracker->revision()) + "\"";
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
    case 0: return db.array.capacity();
    case 1: return db.surface.capacity();
    case 2: return db.geometry.capacity();
    case 3: return db.material.capacity();
    case 4: return db.sampler.capacity();
    case 5: return db.volume.capacity();
    case 6: return db.field.capacity();
    case 7: return db.light.capacity();
    case 8: return db.camera.capacity();
    default: return db.renderer.capacity();
    }
  }

  static std::string number(float v)
  {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.17g", double(v));
    return buf;
  }

  // Current values of the §8 editable parameters, or empty when none apply.
  static std::string editableValues(const scene::Object &o)
  {
    std::string out;
    for (const auto &f : integration::editSurface()) {
      if (o.type() != f.objectType || o.subtype() != *f.subtype)
        continue;
      const auto *p = o.parameter(Token(f.parameter));
      if (!p || p->value().type() != f.storedType)
        continue;
      const auto *v = static_cast<const float *>(p->value().data());
      std::string e = "[";
      for (std::size_t c = 0; c < f.components; c++)
        e += (c ? "," : "") + number(v[c]);
      e += "]";
      if (f.components == 1)
        e = number(v[0]);
      out += (out.empty() ? "" : ",") + detail::jsonString(f.parameter) + ":"
          + e;
    }
    return out;
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
    if (j.pos.index >= poolCapacity(scene, j.pos.pool)) {
      j.pos = {j.pos.pool + 1, 0};
      return true;
    }
    j.visited++;
    auto *o = scene.getObject(kPools[j.pos.pool], j.pos.index);
    if (!o) {
      j.pos.index++;
      return true;
    }
    const auto h = ids.handleOf(o);
    const auto v = ids.parameterVersion(h);
    std::string item = "{\"handle\":\"" + detail::hex64(h.domain)
        + detail::hex64(h.serial) + "\",\"type\":"
        + detail::jsonString(anari::toString(o->type())) + ",\"subtype\":"
        + detail::jsonString(o->subtype().str()) + ",\"version\":"
        + std::to_string(v ? v->version : 0) + ",\"name\":";
    std::string name = display(o->name());
    std::string params = ",\"parameters\":[";
    bool paramsTruncated = false;
    for (std::size_t p = 0; p < o->numParameters(); p++) {
      const auto &par = o->parameterAt(p);
      std::string e = std::string(p ? "," : "") + "{\"name\":"
          + detail::jsonString(par.name().str()) + ",\"type\":"
          + detail::jsonString(anari::toString(par.value().type())) + "}";
      if (params.size() + e.size() > m_limits.optionalBytes) {
        paramsTruncated = true;
        break;
      }
      params += e;
    }
    params += "],\"parameters_truncated\":";
    params += paramsTruncated ? "true" : "false";
    const auto vals = editableValues(*o);
    if (!vals.empty())
      params += ",\"values\":{" + vals + "}";
    item += name + params + "}";
    if (!append(j, item, tooLarge))
      return false;
    j.pos.index++;
    return true;
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

    std::size_t slice = 0;
    bool tooLarge = false, exhausted = false;
    while (slice < m_limits.sliceVisits) {
      if (j.visited >= m_limits.visits) {
        j.stop = "work";
        break;
      }
      const std::size_t before = j.visited;
      const bool more = j.query == Query::DISCOVER
          ? nextDiscovery(j, tooLarge, exhausted)
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
    if (!exhausted && !j.stop)
      return Step{Step::Status::YIELD};

    std::string r = "{\"revision\":\"" + detail::hex64(j.revision)
        + "\",\"visited\":" + std::to_string(j.visited)
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

} // namespace vsr::integration::inspection

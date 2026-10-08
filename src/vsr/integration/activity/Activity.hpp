// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Application-owned activity/history model (spec §4, §5, §9, §13).
//
// Headless and ImGui-free. Records survive client disconnect (the log is
// owned by the application, not a session) but not restart (nothing is
// persisted). Retention is bounded by bytes, entry count, restorable-batch
// count and a separate retained-image byte budget.

// vsr_integration
#include "vsr/integration/edit/AgentEdit.hpp"
#include "vsr/integration/operations/Operations.hpp"
#include "vsr/integration/session/SessionAuthority.hpp"
// std
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace vsr::integration::activity {

using edit::BatchRef;
using operations::LocalRef;
using operations::OperationRef;
using operations::Outcome;
using operations::Progress;
using session::Capability;
using session::Credentials;
using session::SessionAuthority;
using session::Target;

struct Limits
{
  std::size_t bytes{16u << 20}; // activity + restoration payloads
  std::size_t entries{1024};
  std::size_t restorableBatches{32};
  std::size_t imageBytes{32u << 20}; // retained images, counted only here

  bool valid() const;
};

// Fixed per-entry accounting overhead, in bytes, on top of payloads.
constexpr std::size_t ENTRY_OVERHEAD = 256;

enum class OperationKind
{
  DISCOVERY,
  INSPECTION,
  RENDER,
  SET_VIEW_CAMERA,
  IMAGE_EXPORT,
  MESH_EXPORT,
  IMAGE_IMPORT,
  EDIT,
  RESTORE,
  RESYNCHRONIZE,
  STATUS_LOOKUP
};

inline const char *operationKindName(OperationKind k)
{
  switch (k) {
  case OperationKind::DISCOVERY:
    return "discovery";
  case OperationKind::INSPECTION:
    return "inspection";
  case OperationKind::RENDER:
    return "render";
  case OperationKind::SET_VIEW_CAMERA:
    return "set_view_camera";
  case OperationKind::IMAGE_EXPORT:
    return "image_export";
  case OperationKind::MESH_EXPORT:
    return "mesh_export";
  case OperationKind::IMAGE_IMPORT:
    return "image_import";
  case OperationKind::EDIT:
    return "edit";
  case OperationKind::RESTORE:
    return "restore";
  case OperationKind::RESYNCHRONIZE:
    return "resynchronize";
  case OperationKind::STATUS_LOOKUP:
    return "status_lookup";
  }
  return "unknown";
}

enum class Eligibility
{
  NONE, // not a restorable batch
  ELIGIBLE,
  RESTORED,
  EXPIRED, // evicted from the restorable set; metadata retained
  CONFLICTING
};

// Where the record's facts come from. Client summaries never become
// VSR-observed facts.
struct ClientSummary
{
  std::string text; // attributed description, not proof or instruction
};

struct Entry
{
  operations::ResultOwnership ownership;
  std::size_t reservedResultBytes{0};
  // Correlation: external work has `ref`; host-local work has `localRef`.
  std::optional<OperationRef> ref;
  std::optional<LocalRef> localRef;

  // VSR-observed facts //
  OperationKind kind{OperationKind::INSPECTION};
  // Covered targets, fixed per kind; see coveredTargets().
  std::vector<Target> targets;
  std::optional<Progress> progress;
  std::optional<Outcome> terminal;
  bool committed{false};
  double admittedAt{0.0};
  double finishedAt{0.0};
  double cleanupElapsedSeconds{0.0};
  std::size_t heldSlots{0};
  std::optional<BatchRef> batchRef;
  std::string result; // scene-bearing payload
  std::size_t restorationBytes{0}; // payload retained for restoration
  Eligibility eligibility{Eligibility::NONE};
  std::vector<std::uint8_t> image; // retained image (own budget)
  bool imageEvicted{false};

  // Client-supplied, attributed separately //
  std::optional<ClientSummary> clientSummary;

  // Active work is never evicted.
  bool active{false};
};

// Allowlisted owner receipt: no scene values, names, handles, tokens,
// Batch References, images, summaries or diagnostics.
struct Receipt
{
  OperationRef ref;
  OperationKind kind{OperationKind::INSPECTION};
  std::optional<Progress> progress;
  std::optional<Outcome> terminal;
  bool committed{false};
  double admittedAt{0.0};
  double finishedAt{0.0};
  double cleanupElapsedSeconds{0.0};
  std::size_t heldSlots{0};
};

struct Lookup
{
  Outcome code{Outcome::OK};
  std::optional<Receipt> receipt;
  // Present only with current read on every covered target.
  std::optional<Entry> record;
};

struct Usage
{
  std::size_t bytes{0};
  std::size_t entries{0};
  std::size_t restorableBatches{0};
  std::size_t imageBytes{0};
};

// Covered targets per operation kind (spec §9). `primary` is the
// registered scene (or view for render); discovery pages list their items.
inline std::vector<Target> coveredTargets(OperationKind kind,
    Target primary,
    const Integration &integration,
    const std::vector<Target> &listed = {})
{
  switch (kind) {
  case OperationKind::DISCOVERY:
  case OperationKind::STATUS_LOOKUP:
    return listed;
  case OperationKind::SET_VIEW_CAMERA:
  case OperationKind::RENDER: {
    std::vector<Target> t{primary};
    if (auto s = integration.sceneOfView(primary.id))
      t.push_back(Target::scene(*s));
    return t;
  }
  default:
    return {primary};
  }
}

class ActivityLog
{
 public:
  using Id = std::uint64_t;

  explicit ActivityLog(Limits limits = {}) : m_limits(limits) {}

  const Limits &limits() const;

  // Appends a record, evicting the oldest safe records to fit. Returns
  // nullopt (busy) when the record cannot fit without evicting active work;
  // callers must then reject before commit.
  std::optional<Id> append(Entry entry);

  // Replaces an entry's state (e.g. on completion). False when evicted or
  // when the grown record cannot fit.
  bool update(Id id, Entry entry);
  bool prepareResult(
      Id id, std::size_t bytes, operations::ResultOwnership::Reserve reserve);
  bool updatePrepared(Id id, const operations::ActivityEvent &event);

  std::optional<Entry> get(Id id) const;

  // A retained entry with its log id.
  struct Record
  {
    Id id{0};
    Entry entry;
  };

  // Consistent, ordered (oldest first) snapshot of every retained entry,
  // taken under the log's lock. Ids are strictly increasing.
  std::vector<Record> snapshot() const;

  // Restoration eligibility of a Batch Reference while its metadata is
  // retained; nullopt once the record is gone (outcome_unavailable).
  std::optional<Eligibility> eligibility(const BatchRef &ref) const;

  // Restoration storage for an admitted edit (spec §5): charges `bytes` of
  // payload to the shared byte budget and holds one restorable-batch slot,
  // visibly expiring the oldest eligible batches and evicting older safe
  // records as needed. False (reject busy before commit) when it cannot fit
  // without evicting active work; nothing is evicted then.
  bool reserveRestoration(Id id, std::size_t bytes, BatchRef ref);

  // Releases an unpublished reservation and its payload bytes.
  void cancelRestoration(Id id);

  // Commit: the reserved batch becomes an eligible restorable batch.
  void publishRestoration(Id id);

  // Consumes eligibility at restoration commit and releases its payload.
  bool markRestored(const BatchRef &ref);
  bool markConflicting(const BatchRef &ref);
  // No temporary owning key at the post-write publication boundary.
  bool markRestored(const std::string &launchId, const std::string &batchId);
  bool markConflicting(const std::string &launchId, const std::string &batchId);

  // Authorized lookup by complete Operation Reference (spec §4, §9).
  Lookup lookup(SessionAuthority &authority,
      const Credentials &credentials,
      const OperationRef &ref) const;

  Usage usage() const;

  static std::size_t cost(const Entry &e);

  static Receipt receiptOf(const Entry &e);

 private:
  struct Slot
  {
    Id id{0};
    bool pinned{false};
    bool restorationReserved{false}; // reserved, not yet published
    Entry entry;
  };
  using Slots = std::list<Slot>;

  Slots::iterator findId(Id id);
  Slots::const_iterator findId(Id id) const;

  bool safe(const Slot &s) const;

  bool setEligibility(
      const std::string &launchId, const std::string &batchId, Eligibility to);

  void loseEligibility(Slot &s, Eligibility to);

  // Oldest safe restorable batch visibly expires; its metadata stays.
  // Unpublished reservations count against the limit and are never expired.
  // Makes room for one more.
  bool makeRestorableRoom();

  bool fits(std::size_t bytes, std::size_t entries) const;

  // True when evicting every safe record would make room.
  bool feasible(std::size_t bytes, std::size_t entries) const;

  bool makeRoom(std::size_t bytes, std::size_t entries);

  // Oldest safe retained images are dropped; their records stay.
  bool makeImageRoom(std::size_t bytes);

  Limits m_limits;
  mutable std::mutex m_mutex;
  Slots m_slots; // oldest first
  Usage m_usage;
  std::size_t m_reservedRestorable{0};
  Id m_serial{0};
};

// Maps an operations::Request kind string onto an OperationKind; unknown
// kinds (e.g. test or host-defined work) record as inspection.
inline OperationKind operationKindOf(const std::string &kind)
{
  if (kind == "translate_commit")
    return OperationKind::EDIT;
  for (auto k : {OperationKind::DISCOVERY,
           OperationKind::INSPECTION,
           OperationKind::RENDER,
           OperationKind::SET_VIEW_CAMERA,
           OperationKind::IMAGE_EXPORT,
           OperationKind::MESH_EXPORT,
           OperationKind::IMAGE_IMPORT,
           OperationKind::EDIT,
           OperationKind::RESTORE,
           OperationKind::RESYNCHRONIZE,
           OperationKind::STATUS_LOOKUP})
    if (kind == operationKindName(k))
      return k;
  return OperationKind::INSPECTION;
}

// Feeds OperationEngine transitions into an ActivityLog. Install with
// OperationEngine::setActivitySink(&sink); the sink must outlive that
// installation. Only VSR-observed fields are written: a client summary
// already on the entry is preserved across updates.
class EngineActivitySink : public operations::ActivitySink
{
 public:
  EngineActivitySink(ActivityLog &log, const Integration &integration)
      : m_log(log), m_integration(integration)
  {}

  std::optional<Id> admitted(const operations::ActivityEvent &ev) override;

  void updated(Id id, const operations::ActivityEvent &ev) override;
  bool prepareResult(Id id,
      std::size_t bytes,
      operations::ResultOwnership::Reserve reserve) override;

  bool reserveRestoration(Id id,
      std::size_t bytes,
      const std::string &launchId,
      const std::string &batchId) override;
  void cancelRestoration(Id id) override;
  void publishRestoration(Id id) override;
  bool restorationRetained(
      const std::string &launchId, const std::string &batchId) override;
  bool restorationKnown(
      const std::string &launchId, const std::string &batchId) override;
  void restorationRestored(
      const std::string &launchId, const std::string &batchId) override;
  void restorationConflicting(
      const std::string &launchId, const std::string &batchId) override;

 private:
  static void apply(Entry &e, const operations::ActivityEvent &ev);

  ActivityLog &m_log;
  const Integration &m_integration;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline bool Limits::valid() const
{
  return bytes > 0 && entries > 0 && restorableBatches > 0;
}

inline const Limits &ActivityLog::limits() const
{
  return m_limits;
}

inline std::optional<ActivityLog::Id> ActivityLog::append(Entry entry)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (entry.eligibility == Eligibility::ELIGIBLE && !makeRestorableRoom())
    return std::nullopt;
  const std::size_t need = cost(entry);
  if (!makeRoom(need, 1))
    return std::nullopt;
  if (entry.image.size() > m_limits.imageBytes
      || !makeImageRoom(entry.image.size()))
    entry.image.clear(), entry.imageEvicted = true;
  Slot s;
  s.id = ++m_serial;
  s.entry = std::move(entry);
  m_usage.bytes += need;
  m_usage.entries++;
  m_usage.imageBytes += s.entry.image.size();
  if (s.entry.eligibility == Eligibility::ELIGIBLE)
    m_usage.restorableBatches++;
  m_slots.push_back(std::move(s));
  return m_serial;
}

inline bool ActivityLog::update(Id id, Entry entry)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = findId(id);
  if (it == m_slots.end())
    return false;
  if (it->restorationReserved) {
    // The reservation is owned by reserve/cancel/publishRestoration.
    entry.batchRef = it->entry.batchRef;
    entry.restorationBytes = it->entry.restorationBytes;
    entry.eligibility = Eligibility::NONE;
  }
  const std::size_t oldCost = cost(it->entry);
  const std::size_t newCost = cost(entry);
  it->pinned = true; // don't evict the record being updated
  bool ok = newCost <= oldCost || makeRoom(newCost - oldCost, 0);
  it->pinned = false;
  if (!ok)
    return false;
  m_usage.bytes = m_usage.bytes - oldCost + newCost;
  m_usage.imageBytes -= it->entry.image.size();
  if (it->entry.eligibility == Eligibility::ELIGIBLE)
    m_usage.restorableBatches--;
  if (entry.eligibility == Eligibility::ELIGIBLE)
    m_usage.restorableBatches++;
  it->entry = std::move(entry);
  m_usage.imageBytes += it->entry.image.size();
  return true;
}

inline bool ActivityLog::prepareResult(
    Id id, std::size_t bytes, operations::ResultOwnership::Reserve reserve)
{
  if (bytes > 16384)
    return false;
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = findId(id);
  if (it == m_slots.end())
    return false;
  const auto oldCost = cost(it->entry);
  const auto prior =
      std::max(it->entry.result.size(), it->entry.reservedResultBytes);
  const auto growth = bytes > prior ? bytes - prior : 0;
  it->pinned = true;
  const bool ok = makeRoom(growth, 0);
  it->pinned = false;
  if (!ok)
    return false;
  if (!it->entry.ownership.lease) {
    it->entry.ownership = operations::ResultOwnership(std::move(reserve));
    it->entry.result.reserve(16384);
  }
  it->entry.reservedResultBytes = bytes;
  m_usage.bytes = m_usage.bytes - oldCost + cost(it->entry);
  return true;
}

inline bool ActivityLog::updatePrepared(
    Id id, const operations::ActivityEvent &ev)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = findId(id);
  if (it == m_slots.end() || !it->entry.ownership.lease)
    return false;
  auto &e = it->entry;
  const auto oldCost = cost(e);
  const auto &s = ev.status;
  e.progress = s.progress;
  if (s.terminal && !e.terminal)
    e.finishedAt = ev.now;
  e.terminal = s.terminal;
  e.committed = s.committed;
  e.cleanupElapsedSeconds = s.cleanupElapsedSeconds;
  e.heldSlots = s.heldSlots;
  if (s.terminal) {
    // Capacity and logical growth were reserved before mutation.
    const auto &payload = ev.resultPayload ? *ev.resultPayload : s.result;
    if (payload.size() <= e.result.capacity())
      e.result.assign(payload);
    e.reservedResultBytes = 0;
  }
  e.active = !s.terminal || s.heldSlots > 0;
  m_usage.bytes = m_usage.bytes - oldCost + cost(e);
  return true;
}

inline std::optional<Entry> ActivityLog::get(Id id) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = findId(id);
  if (it == m_slots.end())
    return std::nullopt;
  return it->entry;
}

inline std::vector<ActivityLog::Record> ActivityLog::snapshot() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<Record> out;
  out.reserve(m_slots.size());
  for (auto &s : m_slots)
    out.push_back({s.id, s.entry});
  return out;
}

inline std::optional<Eligibility> ActivityLog::eligibility(
    const BatchRef &ref) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  for (auto &s : m_slots)
    if (s.entry.batchRef && *s.entry.batchRef == ref)
      return s.entry.eligibility;
  return std::nullopt;
}

inline bool ActivityLog::reserveRestoration(
    Id id, std::size_t bytes, BatchRef ref)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = findId(id);
  if (it == m_slots.end() || it->restorationReserved
      || it->entry.eligibility != Eligibility::NONE)
    return false;
  Entry grown = it->entry;
  grown.batchRef = std::move(ref);
  grown.restorationBytes = bytes;
  const std::size_t oldCost = cost(it->entry);
  const std::size_t newCost = cost(grown);
  it->pinned = true;
  // Check the byte budget before expiring anything.
  bool ok = newCost <= oldCost || feasible(newCost - oldCost, 0);
  if (ok)
    ok = makeRestorableRoom();
  if (ok)
    ok = newCost <= oldCost || makeRoom(newCost - oldCost, 0);
  it->pinned = false;
  if (!ok)
    return false;
  m_usage.bytes = m_usage.bytes - oldCost + newCost;
  it->entry = std::move(grown);
  it->restorationReserved = true;
  m_reservedRestorable++;
  return true;
}

inline void ActivityLog::cancelRestoration(Id id)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = findId(id);
  if (it == m_slots.end() || !it->restorationReserved)
    return;
  const std::size_t oldCost = cost(it->entry);
  it->entry.batchRef.reset();
  it->entry.restorationBytes = 0;
  m_usage.bytes = m_usage.bytes - oldCost + cost(it->entry);
  it->restorationReserved = false;
  m_reservedRestorable--;
}

inline void ActivityLog::publishRestoration(Id id)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = findId(id);
  if (it == m_slots.end() || !it->restorationReserved)
    return;
  it->restorationReserved = false;
  m_reservedRestorable--;
  it->entry.eligibility = Eligibility::ELIGIBLE;
  m_usage.restorableBatches++;
}

inline bool ActivityLog::markRestored(const BatchRef &ref)
{
  return markRestored(ref.launchId, ref.batchId);
}

inline bool ActivityLog::markConflicting(const BatchRef &ref)
{
  return markConflicting(ref.launchId, ref.batchId);
}

inline bool ActivityLog::markRestored(
    const std::string &launchId, const std::string &batchId)
{
  return setEligibility(launchId, batchId, Eligibility::RESTORED);
}

inline bool ActivityLog::markConflicting(
    const std::string &launchId, const std::string &batchId)
{
  return setEligibility(launchId, batchId, Eligibility::CONFLICTING);
}

inline Lookup ActivityLog::lookup(SessionAuthority &authority,
    const Credentials &credentials,
    const OperationRef &ref) const
{
  Lookup out;
  out.code = operations::fromSessionCode(authority.authenticate(credentials));
  if (out.code != Outcome::OK)
    return out;
  std::optional<Entry> e;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &s : m_slots)
      if (s.entry.ref && s.entry.ref->sessionId == ref.sessionId
          && s.entry.ref->requestId == ref.requestId)
        e = s.entry;
  }
  if (!e) {
    out.code = Outcome::OUTCOME_UNAVAILABLE;
    return out;
  }
  // Discovery covers exactly the targets its page lists; like the engine,
  // an empty page is readable by any live session (spec §9).
  bool readAll = !e->targets.empty() || e->kind == OperationKind::DISCOVERY;
  for (auto &t : e->targets)
    if (authority.authorizeExecution(credentials.sessionId, t, Capability::READ)
        != session::Code::OK)
      readAll = false;
  const bool own = ref.sessionId == credentials.sessionId;
  if (!readAll && !own) {
    out.code = Outcome::FORBIDDEN; // whole record or nothing
    return out;
  }
  if (own)
    out.receipt = receiptOf(*e);
  if (readAll)
    out.record = std::move(e);
  return out;
}

inline Usage ActivityLog::usage() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_usage;
}

inline std::size_t ActivityLog::cost(const Entry &e)
{
  std::size_t c = ENTRY_OVERHEAD
      + std::max(e.result.size(), e.reservedResultBytes) + e.restorationBytes
      + e.targets.size() * sizeof(Target);
  if (e.ref)
    c += e.ref->sessionId.size();
  if (e.batchRef)
    c += e.batchRef->launchId.size() + e.batchRef->batchId.size();
  if (e.clientSummary)
    c += e.clientSummary->text.size();
  return c;
}

inline Receipt ActivityLog::receiptOf(const Entry &e)
{
  Receipt r;
  if (e.ref)
    r.ref = *e.ref;
  r.kind = e.kind;
  r.progress = e.progress;
  r.terminal = e.terminal;
  r.committed = e.committed;
  r.admittedAt = e.admittedAt;
  r.finishedAt = e.finishedAt;
  r.cleanupElapsedSeconds = e.cleanupElapsedSeconds;
  r.heldSlots = e.heldSlots;
  return r;
}

inline ActivityLog::Slots::iterator ActivityLog::findId(Id id)
{
  for (auto it = m_slots.begin(); it != m_slots.end(); ++it)
    if (it->id == id)
      return it;
  return m_slots.end();
}

inline ActivityLog::Slots::const_iterator ActivityLog::findId(Id id) const
{
  for (auto it = m_slots.begin(); it != m_slots.end(); ++it)
    if (it->id == id)
      return it;
  return m_slots.end();
}

inline bool ActivityLog::safe(const Slot &s) const
{
  return !s.entry.active && !s.pinned;
}

inline bool ActivityLog::setEligibility(
    const std::string &launchId, const std::string &batchId, Eligibility to)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  for (auto &s : m_slots) {
    if (!s.entry.batchRef || s.entry.batchRef->launchId != launchId
        || s.entry.batchRef->batchId != batchId)
      continue;
    if (s.entry.eligibility != Eligibility::ELIGIBLE)
      return false;
    loseEligibility(s, to);
    return true;
  }
  return false;
}

inline void ActivityLog::loseEligibility(Slot &s, Eligibility to)
{
  s.entry.eligibility = to;
  m_usage.restorableBatches--;
  m_usage.bytes -= s.entry.restorationBytes;
  s.entry.restorationBytes = 0; // payload released
}

inline bool ActivityLog::makeRestorableRoom()
{
  if (m_reservedRestorable >= m_limits.restorableBatches)
    return false;
  while (m_usage.restorableBatches + m_reservedRestorable
      >= m_limits.restorableBatches) {
    Slot *victim = nullptr;
    for (auto &s : m_slots)
      if (safe(s) && s.entry.eligibility == Eligibility::ELIGIBLE) {
        victim = &s;
        break;
      }
    if (!victim)
      return false;
    loseEligibility(*victim, Eligibility::EXPIRED);
  }
  return true;
}

inline bool ActivityLog::fits(std::size_t bytes, std::size_t entries) const
{
  return m_usage.bytes + bytes <= m_limits.bytes
      && m_usage.entries + entries <= m_limits.entries;
}

inline bool ActivityLog::feasible(std::size_t bytes, std::size_t entries) const
{
  if (bytes > m_limits.bytes)
    return false;
  std::size_t freeBytes = 0, freeEntries = 0;
  for (auto &s : m_slots)
    if (safe(s))
      freeBytes += cost(s.entry), freeEntries++;
  return m_usage.bytes - freeBytes + bytes <= m_limits.bytes
      && m_usage.entries - freeEntries + entries <= m_limits.entries;
}

inline bool ActivityLog::makeRoom(std::size_t bytes, std::size_t entries)
{
  // Check feasibility first so a failing append evicts nothing.
  if (!feasible(bytes, entries))
    return false;
  auto it = m_slots.begin();
  while (!fits(bytes, entries)) {
    while (!safe(*it))
      ++it;
    m_usage.bytes -= cost(it->entry);
    m_usage.entries--;
    m_usage.imageBytes -= it->entry.image.size();
    if (it->entry.eligibility == Eligibility::ELIGIBLE)
      m_usage.restorableBatches--;
    if (it->restorationReserved)
      m_reservedRestorable--;
    it = m_slots.erase(it);
  }
  return true;
}

inline bool ActivityLog::makeImageRoom(std::size_t bytes)
{
  for (auto &s : m_slots) {
    if (m_usage.imageBytes + bytes <= m_limits.imageBytes)
      break;
    if (!safe(s) || s.entry.image.empty())
      continue;
    m_usage.imageBytes -= s.entry.image.size();
    s.entry.image.clear();
    s.entry.image.shrink_to_fit();
    s.entry.imageEvicted = true;
  }
  return m_usage.imageBytes + bytes <= m_limits.imageBytes;
}

inline std::optional<EngineActivitySink::Id> EngineActivitySink::admitted(
    const operations::ActivityEvent &ev)
{
  Entry e;
  e.ref = ev.ref;
  e.localRef = ev.localRef;
  e.kind = operationKindOf(ev.kind);
  e.targets = coveredTargets(e.kind, ev.target, m_integration);
  e.admittedAt = ev.now;
  apply(e, ev);
  return m_log.append(std::move(e));
}

inline void EngineActivitySink::updated(
    Id id, const operations::ActivityEvent &ev)
{
  if (m_log.updatePrepared(id, ev))
    return;
  auto e = m_log.get(id);
  if (!e)
    return; // cannot happen while active; evicted once terminal
  apply(*e, ev);
  // Session-wide work learns its covered targets only on completion.
  if (e->kind == OperationKind::DISCOVERY && ev.status.terminal)
    e->targets = coveredTargets(e->kind, ev.target, m_integration, ev.covered);
  Entry trimmed = *e;
  if (m_log.update(id, std::move(*e)))
    return;
  // The grown result does not fit: keep the state, drop the payload, so
  // the record still becomes evictable.
  trimmed.result.clear();
  m_log.update(id, std::move(trimmed));
}

inline bool EngineActivitySink::prepareResult(
    Id id, std::size_t bytes, operations::ResultOwnership::Reserve reserve)
{
  return m_log.prepareResult(id, bytes, std::move(reserve));
}

inline bool EngineActivitySink::reserveRestoration(Id id,
    std::size_t bytes,
    const std::string &launchId,
    const std::string &batchId)
{
  return m_log.reserveRestoration(id, bytes, BatchRef{launchId, batchId});
}

inline void EngineActivitySink::cancelRestoration(Id id)
{
  m_log.cancelRestoration(id);
}

inline void EngineActivitySink::publishRestoration(Id id)
{
  m_log.publishRestoration(id);
}

inline bool EngineActivitySink::restorationRetained(
    const std::string &launchId, const std::string &batchId)
{
  return m_log.eligibility(BatchRef{launchId, batchId})
      == Eligibility::ELIGIBLE;
}

inline bool EngineActivitySink::restorationKnown(
    const std::string &launchId, const std::string &batchId)
{
  return m_log.eligibility(BatchRef{launchId, batchId}).has_value();
}

inline void EngineActivitySink::restorationRestored(
    const std::string &launchId, const std::string &batchId)
{
  m_log.markRestored(launchId, batchId);
}

inline void EngineActivitySink::restorationConflicting(
    const std::string &launchId, const std::string &batchId)
{
  m_log.markConflicting(launchId, batchId);
}

inline void EngineActivitySink::apply(
    Entry &e, const operations::ActivityEvent &ev)
{
  const auto &s = ev.status;
  e.progress = s.progress;
  if (s.terminal && !e.terminal)
    e.finishedAt = ev.now;
  e.terminal = s.terminal;
  e.committed = s.committed;
  e.cleanupElapsedSeconds = s.cleanupElapsedSeconds;
  e.heldSlots = s.heldSlots;
  if (s.terminal)
    e.result = ev.resultPayload ? *ev.resultPayload : s.result;
  // Evictable once terminal and no backend resources remain held.
  e.active = !s.terminal || s.heldSlots > 0;
}

} // namespace vsr::integration::activity

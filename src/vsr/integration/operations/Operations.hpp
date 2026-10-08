// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Header-only: the vsr_integration library globs only top-level sources (see
// ticket 19).

// vsr_integration
#include "vsr/integration/Integration.hpp"
#include "vsr/integration/operations/ResultOwnership.hpp"
#include "vsr/integration/session/SessionAuthority.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// std
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vsr::scene {
struct Scene;
} // namespace vsr::scene

namespace vsr::integration::operations {

/*
 * Operation identity, admission and execution of integration scene work
 * (spec §5, §9 and the cancellation rules of §4).
 *
 * All integration-submitted scene operations, external or local, share one
 * bounded admission pool. A slot is held while an operation is queued,
 * running, yielded or synchronizing, and while logically cancelled/timed-out
 * work still retains backend resources (awaiting_cleanup). Admitted steps run
 * only at the host's safe points, through Integration::submit/service.
 *
 * Control calls (renew, cancel, detach, status lookup) never take a scene
 * slot; external ones pass a separate bounded ControlGate instead.
 *
 * The engine must outlive every Integration::service() call that may still
 * run one of its steps.
 *
 * Lock order (outermost first):
 *
 *   OperationEngine::m_mutex -> ActivitySink (e.g. ActivityLog's mutex)
 *                            -> Integration's mutex (leaf)
 *
 * The engine may call the ActivitySink, the clock and Integration queries
 * (isRegisteredScene, sceneOfView) while holding m_mutex. This cannot invert
 * against Integration::service(): the Integration mutex is a leaf that is
 * never held while submitted work (and so runStep) runs, and Integration
 * never calls out while holding it. Conversely the engine never holds
 * m_mutex while calling Integration::submit/submitSessionWork, the
 * SessionAuthority, or an executor, since those may re-enter the engine
 * (onSessionEnded, wake, restoration callbacks).
 */

using session::Capability;
using session::Credentials;
using session::MonotonicClock;
using session::SessionAuthority;
using session::Target;

// Frozen semantic outcome codes (spec §9), plus INVALID_REQUEST for envelope
// errors the adapter maps to its carrier.
enum class Outcome
{
  OK,
  OCCUPIED,
  BUSY,
  CURSOR_STALE,
  SUPERSEDED,
  OUTCOME_UNAVAILABLE,
  ALREADY_RESTORED,
  ITEM_TOO_LARGE,
  CONFLICT,
  REQUEST_ID_CONFLICT,
  CANCELLED,
  TIMEOUT,
  SESSION_EXPIRED,
  UNKNOWN,
  SYNC_REQUIRED,
  APPLIED_WITH_SYNC_FAILURE,
  UNAUTHENTICATED,
  INVALID_SESSION_PROOF,
  FORBIDDEN,
  CAMERA_CONTROL_DISABLED,
  INVALID_REQUEST
};

const char *outcomeName(Outcome outcome);
Outcome fromSessionCode(session::Code code);

// Frozen non-terminal progress state names.
enum class Progress
{
  QUEUED,
  RUNNING,
  YIELDED,
  SYNCHRONIZING,
  AWAITING_CLEANUP
};

const char *progressName(Progress progress);

constexpr std::uint64_t MAX_REQUEST_ID = 9007199254740991ull; // 2^53 - 1

bool validRequestId(std::uint64_t id);
// A JSON number is a request ID only if integer-valued and in range.
std::optional<std::uint64_t> requestIdFromNumber(double value);

// Wire `operation_ref`: {session_id, request_id}.
struct OperationRef
{
  std::string sessionId;
  std::uint64_t requestId{0};
};

// Host-local correlation for in-process callers; never an OperationRef.
struct LocalRef
{
  std::uint64_t id{0};
};

struct Limits
{
  std::size_t sceneSlots{32};
  std::size_t controlSlots{8};
  std::size_t statusRecords{1024};

  bool valid() const;
};

// Executor contract ////////////////////////////////////////////////////////

struct RestorationPolicy
{
  std::uint64_t softBytes{0};
  std::uint64_t hardBytes{0};

  std::string refusal(std::size_t requiredBytes) const;
  std::string receipt(std::size_t requiredBytes) const;
};

inline std::string RestorationPolicy::refusal(std::size_t requiredBytes) const
{
  const bool approval = requiredBytes > hardBytes;
  return std::string("{\"reason\":\"")
      + (approval ? "restoration_approval_required" : "restore_budget_exhausted")
      + "\",\"required_capacity_bytes\":" + std::to_string(requiredBytes)
      + ",\"configured_capacity_bytes\":" + std::to_string(softBytes)
      + ",\"effective_soft_limit_bytes\":" + std::to_string(softBytes)
      + ",\"effective_hard_limit_bytes\":" + std::to_string(hardBytes)
      + ",\"approval_required\":" + (approval ? "true" : "false")
      + ",\"approval_available\":false,\"committed\":false,"
        "\"history_unchanged\":true}";
}

inline std::string RestorationPolicy::receipt(std::size_t requiredBytes) const
{
  return "{\"mode\":\"publication_fifo\",\"effective_global_limit_bytes\":"
      + std::to_string(softBytes)
      + ",\"effective_single_entry_hard_cap_bytes\":"
      + std::to_string(hardBytes)
      + ",\"required_capacity_bytes\":" + std::to_string(requiredBytes)
      + ",\"oversized\":" + (requiredBytes > softBytes ? "true" : "false")
      + ",\"eviction_possible\":true}";
}

struct StepContext
{
  scene::Scene &scene;
  // True once the operation is logically cancelled/timed out; any result is
  // discarded and the step should only release resources.
  bool abandoned{false};
  // Thread-safe; reschedules this operation after BACKEND_PENDING.
  std::function<void()> wake;
  // Bounded backend preparation can stay asleep until a host event. This
  // query lets its owner detect cancellation/deadline without polling status
  // (or retaining client credentials), then wake once to release resources.
  std::function<bool()> isAbandoned;
  // Owning-thread final guard, after fallible preparation and immediately
  // before mutation. Rechecks live authority, cancellation and deadline;
  // it does not reserve resources or consume a write sequence.
  std::function<Outcome()> beforeWrite;
  // Generic edits prepare a truthful fallback before writes, then mark the
  // irreversible boundary without allocating or notifying observers.
  std::function<void(const std::string &, std::size_t)> prepareCommit;
  std::function<void()> markCommitted;
  // Prepare bounded retained Activity result storage before any write.
  std::function<bool(std::size_t, ResultOwnership::Reserve)>
      prepareActivityResult;

  // Restoration storage in the application-owned activity budget (spec §5).
  // Each forwards to the installed ActivitySink for this operation's record
  // and trivially succeeds when none is installed. Call from the executor.
  //
  // Reserve `bytes` of restoration payload plus one restorable-batch slot
  // for Batch Reference {launchId, batchId}; false means reject busy.
  std::function<bool(std::size_t bytes,
      const std::string &launchId,
      const std::string &batchId)>
      reserveRestoration;
  // Releases an unpublished reservation.
  std::function<void()> cancelRestoration;
  // On commit: the record becomes an eligible restorable batch.
  std::function<void()> publishRestoration;
  bool sharedRetention{false}; // an authoritative Activity sink owns policy
  std::optional<RestorationPolicy> restorationPolicy;
  // False once the sink no longer retains the batch as eligible.
  std::function<bool(const std::string &launchId, const std::string &batchId)>
      restorationRetained;
  // Any retained batch metadata, including consumed/conflicting records.
  std::function<bool(const std::string &, const std::string &)>
      restorationKnown;
  // Restoration of batch {launchId, batchId} committed (consumes eligibility)
  // or found it conflicting; both release its retained payload.
  std::function<void(const std::string &launchId, const std::string &batchId)>
      markRestored;
  std::function<void(const std::string &launchId, const std::string &batchId)>
      markConflicting;
};

struct Step
{
  enum class Status
  {
    YIELD, // cooperative slice boundary; rescheduled at a later safe point
    SYNCHRONIZING, // post-commit synchronization in progress; rescheduled
    BACKEND_PENDING, // backend holds resources; call wake() when it changes
    COMPLETE // terminal; all resources released
  };

  Status status{Status::COMPLETE};
  Outcome outcome{Outcome::OK};
  std::string result;
  bool committed{false};
  bool backendFault{false}; // an actual backend-reported fault
  // Session-wide work only: on COMPLETE, the targets the result lists; they
  // become the record's covered targets (spec §9).
  std::vector<Target> covered;
  // Optional reservation for retained result ownership; follows the record
  // through replay/expiry/eviction. No change to admission or result schemas.
  std::shared_ptr<const void> resourceLease;
  // Optional bounded-result copy reservation (no global result-store change).
  // Called before copying visible payloads to a public Status, including
  // replay.
  std::function<std::shared_ptr<const void>()> reserveStatus;
  // No-fail ownership handoff only after COMPLETE is accepted, never after
  // cancellation/deadline. Runs under the record lock: no engine reentrancy,
  // I/O or allocation. The executor must not publish ownership speculatively.
  std::function<void()> onAccepted;
};

using Executor = std::function<Step(StepContext &)>;

struct Request
{
  std::string kind;
  Target target;
  Capability capability{Capability::READ};
  // Session-wide work (discovery) names no single target: admission and
  // execution check only the session, `target` is ignored, the executor
  // filters by capability itself and reports the covered targets in its
  // final Step. Runs on the servicing thread with the first registered scene.
  bool sessionWide{false};
  bool boundSceneRead{false};
  // Canonical semantic form of the parsed arguments (defaults applied,
  // numbers normalized); identical retries compare equal.
  std::string canonicalArguments;
  // Logical deadline from admission; <= 0 means none.
  double deadlineSeconds{0.0};
  Executor executor;
  // Prepared notification for a committed operation whose synchronization
  // step cannot be queued. Invoked outside the engine lock.
  std::function<void()> onSynchronizationFailure;
  // Optional ownership prepared by a bounded service before admission.
  std::shared_ptr<const void> resourceLease;
  ResultOwnership::Reserve reserveStatus;
  // Fresh admission only, after exact replay. Must not reenter the engine.
  std::function<Outcome()> admissionGuard;
};

struct Status
{
  ResultOwnership ownership;
  Status() = default;
  Status(const Status &other);
  Status &operator=(const Status &other);
  Status(Status &&) noexcept = default;
  Status &operator=(Status &&) noexcept = default;

  Outcome code{Outcome::OK}; // lookup result; OK when the record was found
  std::string kind;
  std::optional<Progress> progress; // physical state while slot is held
  std::optional<Outcome> terminal; // logical outcome once terminal
  bool committed{false};
  bool resultVisible{false}; // needs current read permission
  std::string result;
  double cleanupElapsedSeconds{0.0};
  std::size_t heldSlots{0};
  bool backendFault{false};
  std::shared_ptr<const void> resourceLease; // follows this public result copy
};

inline Status::Status(const Status &other)
    : ownership(other.ownership),
      code(other.code),
      kind(other.kind),
      progress(other.progress),
      terminal(other.terminal),
      committed(other.committed),
      resultVisible(other.resultVisible),
      result(other.result),
      cleanupElapsedSeconds(other.cleanupElapsedSeconds),
      heldSlots(other.heldSlots),
      backendFault(other.backendFault),
      resourceLease(ownership.lease ? ownership.lease : other.resourceLease)
{}

inline Status &Status::operator=(const Status &other)
{
  if (this != &other) {
    Status copy(other);
    *this = std::move(copy);
  }
  return *this;
}

struct Admission
{
  Outcome code{Outcome::OK};
  bool replayed{false};
  Status status;
};

struct LocalAdmission
{
  Outcome code{Outcome::OK};
  LocalRef ref;
};

// Activity sink ///////////////////////////////////////////////////////////

// One engine transition, as observed by VSR. Owned Image results borrow
// resultPayload during the synchronous sink call; other records include
// their result in status. Consumers apply their own read checks on lookup.
struct ActivityEvent
{
  std::optional<OperationRef> ref; // external work
  std::optional<LocalRef> localRef; // host-local work
  std::string kind;
  Target target;
  // Session-wide work: the targets its completed result lists (else empty).
  std::vector<Target> covered;
  Status status;
  // Borrowed only during the synchronous sink call under the record lock.
  const std::string *resultPayload{nullptr};
  double now{0.0};
};

// Application-owned activity record store fed by the engine. Declared here
// because activity/Activity.hpp depends on this header; the adapter onto
// activity::ActivityLog lives there. Called with the engine lock held: an
// implementation must not call back into the engine, and may only take
// locks ordered after it (its own, then Integration queries; see the lock
// order above).
class ActivitySink
{
 public:
  using Id = std::uint64_t;
  virtual ~ActivitySink() = default;
  // Reserves a record at admission; nullopt rejects the admission busy.
  virtual std::optional<Id> admitted(const ActivityEvent &event) = 0;
  // Progress, terminal and cleanup transitions of an admitted record.
  virtual void updated(Id id, const ActivityEvent &event) = 0;
  virtual bool prepareResult(Id, std::size_t, ResultOwnership::Reserve)
  {
    return false;
  }

  // Restoration payload accounting for an admitted record (StepContext).
  virtual std::optional<RestorationPolicy> restorationPolicy() const;
  virtual bool reserveRestoration(Id,
      std::size_t /*bytes*/,
      const std::string & /*launchId*/,
      const std::string & /*batchId*/)
  {
    return true;
  }
  virtual void cancelRestoration(Id) {}
  virtual void publishRestoration(Id) {}
  virtual bool restorationRetained(
      const std::string & /*launchId*/, const std::string & /*batchId*/)
  {
    return true;
  }
  virtual bool restorationKnown(const std::string &, const std::string &)
  {
    return true;
  }
  virtual void restorationRestored(
      const std::string & /*launchId*/, const std::string & /*batchId*/)
  {}
  virtual void restorationConflicting(
      const std::string & /*launchId*/, const std::string & /*batchId*/)
  {}
};

inline std::optional<RestorationPolicy> ActivitySink::restorationPolicy() const
{
  return std::nullopt;
}

// Control gate //////////////////////////////////////////////////////////////

enum class ControlKind
{
  RENEW,
  CANCEL,
  DETACH,
  LOOKUP
};

// Bounds concurrent external control/metadata calls. Lookups may never take
// the last slot, so polling cannot starve renew/cancel/detach; an identical
// lifecycle call already in flight coalesces without a slot.
class ControlGate
{
 public:
  class Permit
  {
   public:
    Permit() = default;
    Permit(Permit &&o) noexcept;
    Permit &operator=(Permit &&o) noexcept;
    Permit(const Permit &) = delete;
    Permit &operator=(const Permit &) = delete;
    ~Permit();

    bool granted() const;
    bool coalesced() const;

   private:
    friend class ControlGate;
    ControlGate *m_gate{nullptr};
    std::size_t m_entry{0};
    bool m_coalesced{false};
  };

  explicit ControlGate(std::size_t capacity);

  Permit acquire(ControlKind kind, const std::string &key);
  std::size_t capacity() const;
  std::size_t inFlight() const;

 private:
  struct Entry
  {
    ControlKind kind{ControlKind::LOOKUP};
    std::string key;
    std::size_t holders{0};
  };

  void release(std::size_t entry);

  mutable std::mutex m_mutex;
  std::size_t m_capacity;
  std::vector<Entry> m_entries; // one per used slot
};

// Engine ////////////////////////////////////////////////////////////////////

class OperationEngine
{
 public:
  // Returns null for invalid limits. Installs the authority's session-ended
  // callback so ending a session cancels its outstanding work.
  static std::unique_ptr<OperationEngine> create(Integration &integration,
      SessionAuthority &authority,
      Limits limits = {},
      MonotonicClock clock = session::steadyClock());

  ~OperationEngine();
  OperationEngine(const OperationEngine &) = delete;
  OperationEngine &operator=(const OperationEngine &) = delete;

  const Limits &limits() const;

  // Installs (or clears, with null) the activity record store. The sink must
  // outlive the engine or be cleared first. Only work admitted afterwards is
  // recorded.
  void setActivitySink(ActivitySink *sink);

  // External scene work //

  Admission submit(
      const Credentials &credentials, const OperationRef &ref, Request request);

  // External control path (bounded by the ControlGate) //

  Outcome renew(const Credentials &credentials);
  Outcome detach(const Credentials &credentials);
  // Cancels the caller's own session work. Unknown references above
  // high-water return UNKNOWN, never a confirmed cancellation.
  Status cancel(const Credentials &credentials, const OperationRef &ref);
  // Own-session minimal receipt, or any session's record with current read
  // on every covered target (spec §9).
  Status status(const Credentials &credentials, const OperationRef &ref);
  ControlGate &controlGate();

  // Local host callers: same pool, no session, host authority //

  LocalAdmission submitLocal(Request request);
  Status cancelLocal(LocalRef ref);
  Status statusLocal(LocalRef ref);

  // Metrics //

  std::size_t slotsInUse() const;
  std::size_t awaitingCleanup() const;
  std::uint64_t highWater(const std::string &sessionId) const;

 private:
  enum class Phase
  {
    QUEUED,
    RUNNING,
    YIELDED,
    SYNCHRONIZING,
    BACKEND,
    TERMINAL
  };

  struct Record
  {
    // Outlive every owning string/function in this retained record.
    std::shared_ptr<const void> resourceLease;
    std::uint64_t serial{0}; // admission order, for eviction
    std::string sessionId; // empty for local callers
    std::uint64_t requestId{0}; // or local counter
    std::string kind;
    Target target;
    Capability capability{Capability::READ};
    bool sessionWide{false};
    bool boundSceneRead{false};
    std::vector<Target> covered; // session-wide only, set on completion
    std::string canonicalArguments;
    double deadlineSeconds{0.0};
    double deadline{0.0};
    Executor executor;
    std::function<void()> onSynchronizationFailure;

    Phase phase{Phase::QUEUED};
    bool executing{false};
    bool dispatched{false};
    std::optional<Outcome> pendingCancel;
    bool holdsSlot{false};
    bool awaitingCleanup{false};
    double cleanupSince{0.0};
    std::optional<Outcome> terminal;
    bool committed{false};
    bool backendFault{false};
    std::string result;
    std::string commitFallback; // unpublished until the first Scene write
    std::optional<ActivitySink::Id> activityId;
    std::function<std::shared_ptr<const void>()> reserveStatus;
  };

  using Key = std::pair<std::string, std::uint64_t>;

  struct SessionState
  {
    std::uint64_t highWater{0};
  };

  OperationEngine(Integration &integration,
      SessionAuthority &authority,
      Limits limits,
      MonotonicClock clock);

  // Caller holds m_mutex unless stated otherwise.
  Record *find(const Key &key);
  bool sameRequest(const Record &r, const Request &q) const;
  bool reserveRecord();
  Key admit(std::string sessionId, std::uint64_t id, Request request);
  void finish(Record &r, Outcome outcome, double now);
  void releaseSlot(Record &r);
  void expireDue(double now);
  void cancelRecord(Record &r, Outcome outcome, double now);
  Status describe(const Record &r, bool resultVisible, double now) const;
  std::optional<SceneId> sceneFor(Target target) const;
  // Carrier scene for a request: its target's scene, or for session-wide
  // work the first registered scene, or INVALID_SCENE_ID (run as scene-less
  // session work) when no scene is registered.
  std::optional<SceneId> sceneFor(bool sessionWide, Target target) const;
  // m_mutex NOT held. False when the step could not be queued; queued-phase
  // work has then been finished BUSY and its slot released.
  bool dispatch(
      const Key &key, std::optional<SceneId> cleanupScene = std::nullopt);
  // Stand-in scene for session-wide steps when no scene is registered.
  // Servicing thread only.
  scene::Scene &placeholderScene();
  void runStep(const Key &key, scene::Scene &scene);
  void onSessionEnded(const std::string &sessionId);
  Status lookup(const Key &key, bool cancel);
  ActivityEvent eventOf(const Key &key, const Record &r, double now) const;
  void notify(const Record &r, double now);
  std::optional<ActivitySink::Id> reserveActivity(
      const Key &key, const Request &request, double now);

  Integration &m_integration;
  SessionAuthority &m_authority;
  Limits m_limits;
  MonotonicClock m_clock;
  ControlGate m_control;
  std::shared_ptr<bool> m_alive;

  mutable std::mutex m_mutex;
  std::map<Key, Record> m_records;
  std::map<std::string, SessionState> m_sessions;
  std::size_t m_slotsInUse{0};
  std::uint64_t m_serial{0};
  std::uint64_t m_localCounter{0};
  ActivitySink *m_sink{nullptr};
  std::unique_ptr<scene::Scene> m_placeholderScene;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline const char *outcomeName(Outcome outcome)
{
  switch (outcome) {
  case Outcome::OK:
    return "ok";
  case Outcome::OCCUPIED:
    return "occupied";
  case Outcome::BUSY:
    return "busy";
  case Outcome::CURSOR_STALE:
    return "cursor_stale";
  case Outcome::SUPERSEDED:
    return "superseded";
  case Outcome::OUTCOME_UNAVAILABLE:
    return "outcome_unavailable";
  case Outcome::ALREADY_RESTORED:
    return "already_restored";
  case Outcome::ITEM_TOO_LARGE:
    return "item_too_large";
  case Outcome::CONFLICT:
    return "conflict";
  case Outcome::REQUEST_ID_CONFLICT:
    return "request_id_conflict";
  case Outcome::CANCELLED:
    return "cancelled";
  case Outcome::TIMEOUT:
    return "timeout";
  case Outcome::SESSION_EXPIRED:
    return "session_expired";
  case Outcome::UNKNOWN:
    return "unknown";
  case Outcome::SYNC_REQUIRED:
    return "sync_required";
  case Outcome::APPLIED_WITH_SYNC_FAILURE:
    return "applied_with_sync_failure";
  case Outcome::UNAUTHENTICATED:
    return "unauthenticated";
  case Outcome::INVALID_SESSION_PROOF:
    return "invalid_session_proof";
  case Outcome::FORBIDDEN:
    return "forbidden";
  case Outcome::CAMERA_CONTROL_DISABLED:
    return "camera_control_disabled";
  case Outcome::INVALID_REQUEST:
    return "invalid_request";
  }
  return "unknown";
}

inline Outcome fromSessionCode(session::Code code)
{
  switch (code) {
  case session::Code::OK:
    return Outcome::OK;
  case session::Code::OCCUPIED:
    return Outcome::OCCUPIED;
  case session::Code::UNAUTHENTICATED:
    return Outcome::UNAUTHENTICATED;
  case session::Code::INVALID_SESSION_PROOF:
    return Outcome::INVALID_SESSION_PROOF;
  case session::Code::FORBIDDEN:
    return Outcome::FORBIDDEN;
  case session::Code::SESSION_EXPIRED:
    return Outcome::SESSION_EXPIRED;
  }
  return Outcome::UNKNOWN;
}

inline const char *progressName(Progress progress)
{
  switch (progress) {
  case Progress::QUEUED:
    return "queued";
  case Progress::RUNNING:
    return "running";
  case Progress::YIELDED:
    return "yielded";
  case Progress::SYNCHRONIZING:
    return "synchronizing";
  case Progress::AWAITING_CLEANUP:
    return "awaiting_cleanup";
  }
  return "unknown";
}

inline bool validRequestId(std::uint64_t id)
{
  return id >= 1 && id <= MAX_REQUEST_ID;
}

inline std::optional<std::uint64_t> requestIdFromNumber(double value)
{
  if (!std::isfinite(value) || value < 1.0 || value > double(MAX_REQUEST_ID)
      || std::floor(value) != value)
    return std::nullopt;
  return std::uint64_t(value);
}

inline bool Limits::valid() const
{
  return sceneSlots > 0 && controlSlots > 0 && statusRecords > 0;
}

// ControlGate //

inline ControlGate::Permit::Permit(Permit &&o) noexcept
    : m_gate(o.m_gate), m_entry(o.m_entry), m_coalesced(o.m_coalesced)
{
  o.m_gate = nullptr;
}

inline ControlGate::Permit &ControlGate::Permit::operator=(Permit &&o) noexcept
{
  if (this != &o) {
    if (m_gate)
      m_gate->release(m_entry);
    m_gate = o.m_gate;
    m_entry = o.m_entry;
    m_coalesced = o.m_coalesced;
    o.m_gate = nullptr;
  }
  return *this;
}

inline ControlGate::Permit::~Permit()
{
  if (m_gate)
    m_gate->release(m_entry);
}

inline bool ControlGate::Permit::granted() const
{
  return m_gate != nullptr;
}

inline bool ControlGate::Permit::coalesced() const
{
  return m_coalesced;
}

inline ControlGate::ControlGate(std::size_t capacity) : m_capacity(capacity) {}

inline ControlGate::Permit ControlGate::acquire(
    ControlKind kind, const std::string &key)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  Permit p;
  std::size_t used = 0;
  std::size_t lookups = 0;
  for (std::size_t i = 0; i < m_entries.size(); ++i) {
    auto &e = m_entries[i];
    if (e.holders == 0)
      continue;
    ++used;
    if (e.kind == ControlKind::LOOKUP)
      ++lookups;
    if (kind != ControlKind::LOOKUP && e.kind == kind && e.key == key) {
      ++e.holders;
      p.m_gate = this;
      p.m_entry = i;
      p.m_coalesced = true;
      return p;
    }
  }

  if (used >= m_capacity)
    return p;
  // Keep one slot for lifecycle calls whenever there is more than one.
  if (kind == ControlKind::LOOKUP && m_capacity > 1
      && lookups + 1 >= m_capacity)
    return p;

  std::size_t slot = m_entries.size();
  for (std::size_t i = 0; i < m_entries.size(); ++i) {
    if (m_entries[i].holders == 0) {
      slot = i;
      break;
    }
  }
  if (slot == m_entries.size())
    m_entries.emplace_back();
  m_entries[slot] = {kind, key, 1};
  p.m_gate = this;
  p.m_entry = slot;
  return p;
}

inline std::size_t ControlGate::capacity() const
{
  return m_capacity;
}

inline std::size_t ControlGate::inFlight() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  std::size_t n = 0;
  for (const auto &e : m_entries)
    n += e.holders > 0 ? 1 : 0;
  return n;
}

inline void ControlGate::release(std::size_t entry)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (entry < m_entries.size() && m_entries[entry].holders > 0)
    --m_entries[entry].holders;
}

// OperationEngine //

inline std::unique_ptr<OperationEngine> OperationEngine::create(
    Integration &integration,
    SessionAuthority &authority,
    Limits limits,
    MonotonicClock clock)
{
  if (!limits.valid() || !integration.active() || !clock)
    return nullptr;
  std::unique_ptr<OperationEngine> engine(
      new OperationEngine(integration, authority, limits, std::move(clock)));
  auto *raw = engine.get();
  std::weak_ptr<bool> alive = engine->m_alive;
  authority.setSessionEndedCallback(
      [raw, alive](const std::string &id, session::EndReason) {
        if (alive.lock())
          raw->onSessionEnded(id);
      });
  return engine;
}

inline OperationEngine::OperationEngine(Integration &integration,
    SessionAuthority &authority,
    Limits limits,
    MonotonicClock clock)
    : m_integration(integration),
      m_authority(authority),
      m_limits(limits),
      m_clock(std::move(clock)),
      m_control(limits.controlSlots),
      m_alive(std::make_shared<bool>(true))
{}

inline OperationEngine::~OperationEngine()
{
  m_authority.setSessionEndedCallback({});
  m_alive.reset();
}

inline const Limits &OperationEngine::limits() const
{
  return m_limits;
}

inline void OperationEngine::setActivitySink(ActivitySink *sink)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_sink = sink;
}

inline ActivityEvent OperationEngine::eventOf(
    const Key &key, const Record &r, double now) const
{
  ActivityEvent e;
  if (key.first.empty())
    e.localRef = LocalRef{key.second};
  else
    e.ref = OperationRef{key.first, key.second};
  e.kind = r.kind;
  e.target = r.target;
  e.covered = r.covered;
  const bool borrowed = r.resourceLease
      && (r.kind == "image_import" || r.kind == "mesh_import"
          || r.kind == "restore");
  e.status = describe(r, !borrowed, now);
  if (borrowed)
    e.resultPayload = &r.result;
  e.now = now;
  return e;
}

inline std::optional<ActivitySink::Id> OperationEngine::reserveActivity(
    const Key &key, const Request &request, double now)
{
  // Describes the record admit() is about to create: queued, one slot.
  Record r;
  r.kind = request.kind;
  r.target = request.target;
  r.holdsSlot = true;
  return m_sink->admitted(eventOf(key, r, now));
}

inline void OperationEngine::notify(const Record &r, double now)
{
  if (m_sink && r.activityId) {
    try {
      m_sink->updated(
          *r.activityId, eventOf({r.sessionId, r.requestId}, r, now));
    } catch (const std::bad_alloc &) {
      // Recovery eligibility was already published independently. A failed
      // diagnostic/status copy must not strand committed work or erase it.
      if (!r.committed)
        throw;
    }
  }
}

inline ControlGate &OperationEngine::controlGate()
{
  return m_control;
}

inline OperationEngine::Record *OperationEngine::find(const Key &key)
{
  auto it = m_records.find(key);
  return it == m_records.end() ? nullptr : &it->second;
}

inline bool OperationEngine::sameRequest(
    const Record &r, const Request &q) const
{
  return r.kind == q.kind && r.sessionWide == q.sessionWide
      && (r.sessionWide
          || (r.target.kind == q.target.kind && r.target.id == q.target.id))
      && r.capability == q.capability && r.boundSceneRead == q.boundSceneRead
      && r.canonicalArguments == q.canonicalArguments
      && r.deadlineSeconds == q.deadlineSeconds;
}

inline bool OperationEngine::reserveRecord()
{
  if (m_records.size() < m_limits.statusRecords)
    return true;
  // Evict the oldest record that no active work needs.
  auto victim = m_records.end();
  for (auto it = m_records.begin(); it != m_records.end(); ++it) {
    const auto &r = it->second;
    if (!r.terminal || r.holdsSlot || r.executing || r.dispatched)
      continue;
    if (victim == m_records.end() || r.serial < victim->second.serial)
      victim = it;
  }
  if (victim == m_records.end())
    return false;
  m_records.erase(victim);
  return true;
}

inline OperationEngine::Key OperationEngine::admit(
    std::string sessionId, std::uint64_t id, Request request)
{
  Key key{sessionId, id};
  Record r;
  r.serial = ++m_serial;
  r.sessionId = std::move(sessionId);
  r.requestId = id;
  r.kind = std::move(request.kind);
  r.target = request.target;
  r.capability = request.capability;
  r.boundSceneRead = request.boundSceneRead;
  r.sessionWide = request.sessionWide;
  r.canonicalArguments = std::move(request.canonicalArguments);
  r.deadlineSeconds = request.deadlineSeconds;
  r.deadline =
      request.deadlineSeconds > 0.0 ? m_clock() + request.deadlineSeconds : 0.0;
  r.executor = std::move(request.executor);
  r.onSynchronizationFailure = std::move(request.onSynchronizationFailure);
  r.resourceLease = std::move(request.resourceLease);
  r.reserveStatus = std::move(request.reserveStatus);
  r.holdsSlot = true;
  r.dispatched = true;
  ++m_slotsInUse;
  m_records[key] = std::move(r);
  return key;
}

inline std::optional<SceneId> OperationEngine::sceneFor(Target target) const
{
  if (target.kind == Target::Kind::SCENE) {
    if (m_integration.isRegisteredScene(target.id))
      return target.id;
    return std::nullopt;
  }
  return m_integration.sceneOfView(target.id);
}

inline std::optional<SceneId> OperationEngine::sceneFor(
    bool sessionWide, Target target) const
{
  if (!sessionWide)
    return sceneFor(target);
  if (m_integration.isRegisteredScene(0))
    return SceneId{0};
  return INVALID_SCENE_ID;
}

inline scene::Scene &OperationEngine::placeholderScene()
{
  if (!m_placeholderScene)
    m_placeholderScene = std::make_unique<scene::Scene>();
  return *m_placeholderScene;
}

inline Admission OperationEngine::submit(
    const Credentials &credentials, const OperationRef &ref, Request request)
{
  Admission a;
  a.code = fromSessionCode(request.sessionWide
          ? m_authority.authenticate(credentials)
          : m_authority.authorize(
                credentials, request.target, request.capability));
  if (a.code != Outcome::OK)
    return a;
  if (request.boundSceneRead) {
    const auto scene = sceneFor(request.target);
    a.code = scene ? fromSessionCode(m_authority.authorize(
                         credentials, Target::scene(*scene), Capability::READ))
                   : Outcome::FORBIDDEN;
    if (a.code != Outcome::OK)
      return a;
  }
  if (ref.sessionId != credentials.sessionId || !validRequestId(ref.requestId)
      || !request.executor || !sceneFor(request.sessionWide, request.target)) {
    a.code = Outcome::INVALID_REQUEST;
    return a;
  }

  Key key;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    const double now = m_clock();
    expireDue(now);
    auto &s = m_sessions[ref.sessionId];
    if (auto *r = find({ref.sessionId, ref.requestId})) {
      if (!sameRequest(*r, request)) {
        a.code = Outcome::REQUEST_ID_CONFLICT;
        return a;
      }
      a.replayed = true;
      a.status = describe(*r, true, now);
      return a;
    }
    if (ref.requestId <= s.highWater) {
      a.code = Outcome::OUTCOME_UNAVAILABLE;
      return a;
    }
    if (request.admissionGuard) {
      a.code = request.admissionGuard();
      if (a.code != Outcome::OK)
        return a;
    }
    if (m_slotsInUse >= m_limits.sceneSlots || !reserveRecord()) {
      a.code = Outcome::BUSY;
      return a;
    }
    std::optional<ActivitySink::Id> activity;
    if (m_sink) {
      activity = reserveActivity({ref.sessionId, ref.requestId}, request, now);
      if (!activity) {
        a.code = Outcome::BUSY; // cannot record without evicting active work
        return a;
      }
    }
    key = admit(ref.sessionId, ref.requestId, std::move(request));
    find(key)->activityId = activity;
    s.highWater = ref.requestId;
    a.status = describe(*find(key), true, now);
  }
  if (!dispatch(key)) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (auto *r = find(key))
      a.status = describe(*r, true, m_clock());
  }
  return a;
}

inline LocalAdmission OperationEngine::submitLocal(Request request)
{
  LocalAdmission a;
  if (!request.executor || !sceneFor(request.sessionWide, request.target)) {
    a.code = Outcome::INVALID_REQUEST;
    return a;
  }
  if (!request.sessionWide
      && !m_authority.authorizeLocal(request.target, request.capability)) {
    a.code = Outcome::FORBIDDEN;
    return a;
  }
  Key key;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    const double now = m_clock();
    expireDue(now);
    if (m_slotsInUse >= m_limits.sceneSlots || !reserveRecord()) {
      a.code = Outcome::BUSY;
      return a;
    }
    std::optional<ActivitySink::Id> activity;
    if (m_sink) {
      activity =
          reserveActivity({std::string(), m_localCounter + 1}, request, now);
      if (!activity) {
        a.code = Outcome::BUSY;
        return a;
      }
    }
    a.ref.id = ++m_localCounter;
    key = admit({}, a.ref.id, std::move(request));
    find(key)->activityId = activity;
  }
  dispatch(key);
  return a;
}

inline void OperationEngine::releaseSlot(Record &r)
{
  if (r.holdsSlot) {
    r.holdsSlot = false;
    --m_slotsInUse;
  }
  r.awaitingCleanup = false;
  // Preparation may have reserved recovery before a backend wait. Cancellation,
  // authority withdrawal and deadlines must release it without executor
  // destruction calling back into this engine under its record lock.
  if (!r.committed && m_sink && r.activityId)
    m_sink->cancelRestoration(*r.activityId);
  r.executor = {};
}

inline void OperationEngine::finish(Record &r, Outcome outcome, double now)
{
  r.terminal = outcome;
  r.pendingCancel.reset();
  if (r.phase == Phase::BACKEND) {
    // Logically terminal; resources stay held until safe release.
    r.awaitingCleanup = true;
    r.cleanupSince = now;
  } else {
    releaseSlot(r);
  }
  r.phase = Phase::TERMINAL;
  notify(r, now);
}

inline void OperationEngine::cancelRecord(
    Record &r, Outcome outcome, double now)
{
  if (r.terminal || r.committed)
    return; // completed or committing work finishes safely
  if (r.executing) {
    r.pendingCancel = outcome;
    return;
  }
  finish(r, outcome, now);
}

inline void OperationEngine::expireDue(double now)
{
  for (auto &[key, r] : m_records) {
    if (!r.terminal && r.deadline > 0.0 && now >= r.deadline)
      cancelRecord(r, Outcome::TIMEOUT, now);
  }
}

inline Status OperationEngine::describe(
    const Record &r, bool resultVisible, double now) const
{
  Status s;
  s.kind = r.kind;
  s.terminal = r.terminal;
  s.committed = r.committed;
  s.backendFault = r.backendFault;
  s.heldSlots = r.holdsSlot ? 1 : 0;
  if (r.awaitingCleanup) {
    s.progress = Progress::AWAITING_CLEANUP;
    s.cleanupElapsedSeconds = now - r.cleanupSince;
  } else if (!r.terminal) {
    switch (r.phase) {
    case Phase::QUEUED:
      s.progress = Progress::QUEUED;
      break;
    case Phase::YIELDED:
      s.progress = Progress::YIELDED;
      break;
    case Phase::SYNCHRONIZING:
      s.progress = Progress::SYNCHRONIZING;
      break;
    default:
      s.progress = Progress::RUNNING;
      break;
    }
  }
  if (r.terminal && resultVisible) {
    if (!r.result.empty() && r.reserveStatus) {
      try {
        s.ownership = ResultOwnership(r.reserveStatus);
        s.resourceLease = s.ownership.lease;
      } catch (const std::bad_alloc &) {
        s.code = Outcome::BUSY;
        return s;
      }
      if (!s.resourceLease) {
        s.code = Outcome::BUSY;
        return s;
      }
    }
    s.resultVisible = true;
    s.result = r.result;
  }
  return s;
}

inline bool OperationEngine::dispatch(
    const Key &key, std::optional<SceneId> cleanupScene)
{
  std::optional<SceneId> scene;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto *r = find(key);
    if (!r)
      return false;
    scene = sceneFor(r->sessionWide, r->target);
    // A view may close while its backend owns preparation/encoding. A wake
    // retains the original Scene safe-point route for cleanup only, never a
    // replacement view. Normal queued work still requires a live target.
    if (!scene && (r->awaitingCleanup || r->phase == Phase::BACKEND))
      scene = cleanupScene;
    r->dispatched = true;
  }
  std::weak_ptr<bool> alive = m_alive;
  bool ok = false;
  try {
    if (scene && *scene == INVALID_SCENE_ID) {
      ok = m_integration.submitSessionWork([this, alive, key] {
        if (alive.lock())
          runStep(key, placeholderScene());
      });
    } else if (scene) {
      ok = m_integration.submit(*scene, [this, alive, key](scene::Scene &s) {
        if (alive.lock())
          runStep(key, s);
      });
    }
  } catch (const std::bad_alloc &) {
    ok = false;
  }
  std::function<void()> synchronizationFailure;
  if (!ok) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (auto *r = find(key)) {
      r->dispatched = false;
      // Nothing will run this step: finish it so it releases its slot and
      // its activity entry turns terminal (never-started work reports busy,
      // started work cancelled). Work still holding backend resources keeps
      // them until a later wake() re-dispatches it. Committed work keeps its
      // prepared fallback receipt and notifies its synchronization owner.
      if (!r->terminal && !r->executing && r->phase != Phase::BACKEND) {
        if (r->committed)
          synchronizationFailure = std::move(r->onSynchronizationFailure);
        finish(*r,
            r->committed                    ? Outcome::APPLIED_WITH_SYNC_FAILURE
                : r->phase == Phase::QUEUED ? Outcome::BUSY
                                            : Outcome::CANCELLED,
            m_clock());
      }
    }
  }
  if (synchronizationFailure)
    synchronizationFailure();
  return ok;
}

inline void OperationEngine::runStep(const Key &key, scene::Scene &scene)
{
  std::string sessionId;
  Target target;
  Capability capability;
  bool boundSceneRead = false;
  bool sessionWide = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto *r = find(key);
    if (!r)
      return;
    r->dispatched = false;
    const double now = m_clock();
    if (!r->terminal && r->deadline > 0.0 && now >= r->deadline)
      cancelRecord(*r, Outcome::TIMEOUT, now);
    if (r->terminal && !r->awaitingCleanup)
      return; // expired or cancelled queued work never starts
    sessionId = r->sessionId;
    target = r->target;
    capability = r->capability;
    boundSceneRead = r->boundSceneRead;
    sessionWide = r->sessionWide;
  }

  // Current policy applies at the safe point; checked without m_mutex since
  // the authority may call back into onSessionEnded.
  Outcome auth = Outcome::OK;
  if (sessionId.empty()) {
    if (!sessionWide && !m_authority.authorizeLocal(target, capability))
      auth = Outcome::FORBIDDEN;
  } else {
    const auto code =
        m_authority.authorizeExecution(sessionId, target, capability);
    // Session-wide work needs only a live session: FORBIDDEN proves it is.
    auth = sessionWide && code == session::Code::FORBIDDEN
        ? Outcome::OK
        : fromSessionCode(code);
    if (auth == Outcome::OK && boundSceneRead) {
      const auto scene = sceneFor(target);
      auth = scene ? fromSessionCode(m_authority.authorizeExecution(
                         sessionId, Target::scene(*scene), Capability::READ))
                   : Outcome::FORBIDDEN;
    }
  }

  Executor executor;
  bool abandoned = false;
  bool sharedRetention = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto *r = find(key);
    if (!r || (r->terminal && !r->awaitingCleanup))
      return;
    if (!r->terminal && !r->committed && auth != Outcome::OK) {
      // Authority gates mutation, not the already committed sync/cleanup step.
      // Current read still gates every protected result at status retrieval.
      // Session ended or permission withdrawn before execution.
      cancelRecord(*r,
          auth == Outcome::FORBIDDEN ? Outcome::FORBIDDEN : Outcome::CANCELLED,
          m_clock());
      if (!r->awaitingCleanup)
        return;
    }
    abandoned = r->terminal.has_value();
    executor = r->executor;
    sharedRetention = m_sink != nullptr;
    r->executing = true;
    // Committed work re-dispatched to run its normal-synchronization hook
    // keeps reporting `synchronizing` while the hook executes (addendum 03
    // §5).
    const bool hookStep = r->committed && r->phase == Phase::SYNCHRONIZING;
    if (!abandoned && !hookStep && r->phase != Phase::RUNNING) {
      r->phase = Phase::RUNNING;
      notify(*r, m_clock());
    }
  }

  std::weak_ptr<bool> alive = m_alive;
  const auto cleanupScene = sceneFor(sessionWide, target);
  StepContext ctx{scene, abandoned, [this, alive, key, cleanupScene] {
                    if (!alive.lock())
                      return;
                    bool resume = false;
                    {
                      std::lock_guard<std::mutex> lock(m_mutex);
                      auto *r = find(key);
                      resume = r && !r->executing && !r->dispatched
                          && (r->phase == Phase::BACKEND || r->awaitingCleanup);
                    }
                    if (resume)
                      dispatch(key, cleanupScene);
                  }};
  ctx.sharedRetention = sharedRetention;
  ctx.isAbandoned = [this, alive, key] {
    if (!alive.lock())
      return true;
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto *r = find(key);
    return !r || r->terminal.has_value() || r->pendingCancel.has_value()
        || (r->deadline > 0. && m_clock() >= r->deadline);
  };
  ctx.beforeWrite =
      [this, alive, key, sessionId, target, capability, boundSceneRead] {
        if (!alive.lock())
          return Outcome::CANCELLED;
        Outcome current = sessionId.empty()
            ? (m_authority.authorizeLocal(target, capability)
                      ? Outcome::OK
                      : Outcome::FORBIDDEN)
            : fromSessionCode(m_authority.authorizeExecution(
                  sessionId, target, capability));
        if (current == Outcome::OK && boundSceneRead) {
          const auto bound = sceneFor(target);
          current = bound
              ? fromSessionCode(m_authority.authorizeExecution(
                    sessionId, Target::scene(*bound), Capability::READ))
              : Outcome::FORBIDDEN;
        }
        if (current != Outcome::OK)
          return current;
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto *r = find(key);
        if (!r || r->terminal || r->pendingCancel)
          return Outcome::CANCELLED;
        return r->deadline > 0. && m_clock() >= r->deadline ? Outcome::TIMEOUT
                                                            : Outcome::OK;
      };
  ctx.prepareCommit = [this, key](
                          const std::string &receipt, std::size_t capacity) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (auto *r = find(key)) {
      r->commitFallback = receipt;
      r->commitFallback.reserve(capacity);
    }
  };
  ctx.markCommitted = [this, key] {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (auto *r = find(key)) {
      r->result = std::move(r->commitFallback);
      r->committed = true;
    }
  };
  // Serialized with notify() under the engine lock.
  auto withSink = [this, alive, key](auto &&fn) {
    if (!alive.lock())
      return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto *r = find(key);
    if (m_sink && r && r->activityId)
      fn(*m_sink, *r->activityId);
  };
  ctx.prepareActivityResult = [withSink](std::size_t bytes,
                                  ResultOwnership::Reserve reserve) {
    bool ok = false;
    withSink([&](ActivitySink &s, ActivitySink::Id id) {
      ok = s.prepareResult(id, bytes, std::move(reserve));
    });
    return ok;
  };
  withSink([&](ActivitySink &s, ActivitySink::Id) {
    ctx.restorationPolicy = s.restorationPolicy();
  });
  ctx.reserveRestoration = [withSink](std::size_t bytes,
                               const std::string &launchId,
                               const std::string &batchId) {
    bool ok = true;
    withSink([&](ActivitySink &s, ActivitySink::Id id) {
      ok = s.reserveRestoration(id, bytes, launchId, batchId);
    });
    return ok;
  };
  ctx.cancelRestoration = [withSink] {
    withSink(
        [](ActivitySink &s, ActivitySink::Id id) { s.cancelRestoration(id); });
  };
  ctx.publishRestoration = [withSink] {
    withSink(
        [](ActivitySink &s, ActivitySink::Id id) { s.publishRestoration(id); });
  };
  ctx.restorationRetained = [this, alive](const std::string &launchId,
                                const std::string &batchId) {
    if (!alive.lock())
      return true;
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_sink || m_sink->restorationRetained(launchId, batchId);
  };
  ctx.restorationKnown = [this, alive](const std::string &launchId,
                             const std::string &batchId) {
    if (!alive.lock())
      return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_sink || m_sink->restorationKnown(launchId, batchId);
  };
  ctx.markRestored = [this, alive](const std::string &launchId,
                         const std::string &batchId) {
    if (!alive.lock())
      return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_sink)
      m_sink->restorationRestored(launchId, batchId);
  };
  ctx.markConflicting = [this, alive](const std::string &launchId,
                            const std::string &batchId) {
    if (!alive.lock())
      return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_sink)
      m_sink->restorationConflicting(launchId, batchId);
  };
  Step step = executor ? executor(ctx) : Step{};

  bool redispatch = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto *r = find(key);
    if (!r)
      return;
    r->executing = false;
    const double now = m_clock();
    r->backendFault = r->backendFault || step.backendFault;
    if (step.committed)
      r->committed = true;

    if (r->terminal) {
      // Abandoned: late results never make the operation succeed.
      if (step.status == Step::Status::BACKEND_PENDING)
        r->awaitingCleanup = true;
      else
        releaseSlot(*r);
      notify(*r, now);
      return;
    }

    if (!r->committed && r->deadline > 0. && now >= r->deadline)
      r->pendingCancel = Outcome::TIMEOUT;
    if (r->pendingCancel && !r->committed) {
      const Outcome o = *r->pendingCancel;
      r->phase = step.status == Step::Status::BACKEND_PENDING ? Phase::BACKEND
                                                              : Phase::RUNNING;
      finish(*r, o, now);
      return;
    }
    r->pendingCancel.reset();

    switch (step.status) {
    case Step::Status::YIELD:
      r->phase = Phase::YIELDED;
      redispatch = true;
      notify(*r, now);
      break;
    case Step::Status::SYNCHRONIZING:
      r->phase = Phase::SYNCHRONIZING;
      redispatch = true;
      notify(*r, now);
      break;
    case Step::Status::BACKEND_PENDING:
      r->phase = Phase::BACKEND;
      notify(*r, now);
      break;
    case Step::Status::COMPLETE:
      r->result = std::move(step.result);
      if (step.resourceLease)
        r->resourceLease = std::move(step.resourceLease);
      if (step.reserveStatus)
        r->reserveStatus = std::move(step.reserveStatus);
      if (r->sessionWide)
        r->covered = std::move(step.covered);
      r->phase = Phase::RUNNING;
      if (step.onAccepted)
        step.onAccepted();
      finish(*r, step.outcome, now);
      break;
    }
  }
  if (redispatch)
    dispatch(key);
}

inline void OperationEngine::onSessionEnded(const std::string &sessionId)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  const double now = m_clock();
  for (auto &[key, r] : m_records) {
    if (r.sessionId == sessionId)
      cancelRecord(r, Outcome::CANCELLED, now);
  }
}

inline Status OperationEngine::lookup(const Key &key, bool cancel)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  const double now = m_clock();
  expireDue(now);
  Status s;
  auto *r = find(key);
  if (!r) {
    std::uint64_t highWater = 0;
    if (key.first.empty()) {
      highWater = m_localCounter;
    } else {
      auto it = m_sessions.find(key.first);
      if (it != m_sessions.end())
        highWater = it->second.highWater;
    }
    // Above high-water the request may still be in transit.
    s.code = key.second <= highWater ? Outcome::OUTCOME_UNAVAILABLE
                                     : Outcome::UNKNOWN;
    return s;
  }
  if (cancel)
    cancelRecord(*r, Outcome::CANCELLED, now);
  return describe(*r, true, now);
}

inline Status OperationEngine::cancel(
    const Credentials &credentials, const OperationRef &ref)
{
  Status s;
  auto permit = m_control.acquire(ControlKind::CANCEL, credentials.sessionId);
  if (!permit.granted()) {
    s.code = Outcome::BUSY;
    return s;
  }
  s.code = fromSessionCode(m_authority.authenticate(credentials));
  if (s.code != Outcome::OK)
    return s;
  if (ref.sessionId != credentials.sessionId) {
    s.code = Outcome::FORBIDDEN; // never another session's work
    return s;
  }
  return lookup({ref.sessionId, ref.requestId}, true);
}

inline Status OperationEngine::status(
    const Credentials &credentials, const OperationRef &ref)
{
  Status s;
  auto permit = m_control.acquire(ControlKind::LOOKUP, credentials.sessionId);
  if (!permit.granted()) {
    s.code = Outcome::BUSY;
    return s;
  }
  s.code = fromSessionCode(m_authority.authenticate(credentials));
  if (s.code != Outcome::OK)
    return s;
  const bool own = ref.sessionId == credentials.sessionId;
  s = lookup({ref.sessionId, ref.requestId}, false);
  // A foreign session has ended, so nothing of its is still in transit.
  if (!own && s.code == Outcome::UNKNOWN)
    s.code = Outcome::OUTCOME_UNAVAILABLE;
  if (s.code != Outcome::OK)
    return s; // missing/evicted: outcome_unavailable
  // Covered targets: the record's target, plus a view's scene.
  // Session-wide records cover exactly the targets their result lists.
  std::vector<Target> covered;
  bool sessionWide = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (auto *r = find({ref.sessionId, ref.requestId})) {
      sessionWide = r->sessionWide;
      if (sessionWide)
        covered = r->covered;
      else
        covered.push_back(r->target);
    }
  }
  if (!sessionWide && !covered.empty()
      && covered[0].kind == Target::Kind::VIEW) {
    if (auto sc = m_integration.sceneOfView(covered[0].id))
      covered.push_back(Target::scene(*sc));
  }
  bool readAll = sessionWide || !covered.empty();
  for (auto &t : covered)
    if (m_authority.authorizeExecution(
            credentials.sessionId, t, Capability::READ)
        != session::Code::OK)
      readAll = false;
  if (!own && !readAll) {
    // Foreign records need read on every covered target; never partial.
    Status denied;
    denied.code = Outcome::FORBIDDEN;
    return denied;
  }
  if (!readAll) {
    // Payloads need current read; the own minimal receipt does not.
    s.resultVisible = false;
    s.result.clear();
  }
  return s;
}

inline Outcome OperationEngine::renew(const Credentials &credentials)
{
  auto permit = m_control.acquire(ControlKind::RENEW, credentials.sessionId);
  if (!permit.granted())
    return Outcome::BUSY;
  return fromSessionCode(m_authority.renew(credentials));
}

inline Outcome OperationEngine::detach(const Credentials &credentials)
{
  auto permit = m_control.acquire(ControlKind::DETACH, credentials.sessionId);
  if (!permit.granted())
    return Outcome::BUSY;
  return fromSessionCode(m_authority.detach(credentials));
}

inline Status OperationEngine::cancelLocal(LocalRef ref)
{
  return lookup({std::string(), ref.id}, true);
}

inline Status OperationEngine::statusLocal(LocalRef ref)
{
  return lookup({std::string(), ref.id}, false);
}

inline std::size_t OperationEngine::slotsInUse() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_slotsInUse;
}

inline std::size_t OperationEngine::awaitingCleanup() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  std::size_t n = 0;
  for (const auto &[key, r] : m_records)
    n += r.awaitingCleanup ? 1 : 0;
  return n;
}

inline std::uint64_t OperationEngine::highWater(
    const std::string &sessionId) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_sessions.find(sessionId);
  return it == m_sessions.end() ? 0 : it->second.highWater;
}

} // namespace vsr::integration::operations

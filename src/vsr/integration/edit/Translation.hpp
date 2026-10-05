// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/FlatMap.hpp"
// vsr_integration
#include "vsr/integration/edit/AgentEdit.hpp"
// std
#include <array>
#include <deque>
#include <tuple>

namespace vsr::integration::edit {

struct TranslationRequest
{
  SceneId scene{INVALID_SCENE_ID};
  std::vector<NodeVersion> selection;
  std::array<double, 3> worldDelta{};
};

struct PreviewLimits
{
  std::size_t perSession{8};
  std::size_t globalCount{32};
  std::size_t aggregateBytes{16 * 1024 * 1024};
  std::size_t maxSelection{64};
  std::size_t ancestrySteps{4096};
  std::size_t effectEntries{64}; // selected origins only; no descendant scan
  double lifetimeSeconds{120.};
  double deadlineSeconds{5.};
};

struct PreviewRef
{
  std::string launchId;
  std::string previewId;
  SceneId scene{INVALID_SCENE_ID}; // routing only; never grants plan authority
};

struct PreparedTranslationRoot
{
  NodeVersion dependency;
  core::math::mat4 beforeMatrix;
  core::math::mat3 beforeSRT;
  core::math::mat4 matrix;
  core::math::mat3 srt;
  std::array<double, 3> currentOrigin;
  std::array<double, 3> predictedOrigin;
};

// Value-only exact plan, owned by the edit module, never SessionAuthority.
// No Scene/node pointers or matrices reconstructed from wire JSON. Commit
// validates all selection dependencies and passes these exact matrices/SRT
// through the existing allocation/receipt phase, never silently rebasing.
struct PreparedTranslation
{
  PreparedTranslation() = default;
  VSR_NOT_COPYABLE(PreparedTranslation)
  VSR_NOT_MOVEABLE(PreparedTranslation)
  PreviewRef ref;
  std::string sessionId;
  TranslationRequest input; // deterministic unique set, including inherited
  std::vector<PreparedTranslationRoot> roots;
  std::vector<NodeVersion> inherited;
  double expiresAt{0.};
  std::weak_ptr<const void> sourceLifetime;
  std::shared_ptr<const void> reservation;
};

/*
 * Owning-thread Scene preparation behind the admitted public integration seam.
 * Integration/authority/engine must outlive the service and in-flight service
 * calls. Scene must outlive its SceneIdentity, as in the existing identity
 * contract. Attached identities and the EditService may retire early: weak
 * owner blocks prevent later preparation/lookup from dereferencing them.
 * Prepared plans themselves contain values only, not Scene/node ownership.
 * The localized bad_alloc boundaries translate allocation exhaustion to the
 * established integration busy outcome, before plan publication or mutation.
 */
class TranslationService
{
 public:
  static constexpr std::size_t INFRASTRUCTURE_BYTES = 64 * 1024;
  static constexpr std::size_t PREPARATION_BYTES = 512 * 1024;
  static constexpr std::size_t RESULT_BYTES = 64 * 1024;
  TranslationService(Integration &integration,
      session::SessionAuthority &authority,
      OperationEngine &engine,
      EditService &edits,
      PreviewLimits limits = {},
      session::MonotonicClock clock = session::steadyClock());
  ~TranslationService();
  VSR_NOT_COPYABLE(TranslationService)
  VSR_NOT_MOVEABLE(TranslationService)
  bool valid() const;
  const PreviewLimits &limits() const;
  bool attachScene(SceneId scene, SceneIdentity &identity);
  Admission submit(const operations::Credentials &credentials,
      const OperationRef &ref,
      TranslationRequest request);
  LocalAdmission submitLocal(TranslationRequest request);
  Admission submitCommit(const operations::Credentials &credentials,
      const OperationRef &ref,
      PreviewRef preview);
  LocalAdmission submitCommitLocal(PreviewRef preview);
  // Owning-thread maintenance, also called at submission/execution. Hosts
  // pump at each service point to promptly reclaim expired/withdrawn plans.
  void pump();
  std::size_t retainedCount() const;
  std::size_t ownedBytes() const;
  // Internal commit seam: callback cannot retain a reference. Consumption is
  // explicit, only after successful mutation. No mutation is implemented here.
  Outcome withPrepared(const operations::Credentials &credentials,
      const PreviewRef &ref,
      const std::function<Outcome(const PreparedTranslation &)> &use);
  Outcome withPreparedLocal(const PreviewRef &ref,
      const std::function<Outcome(const PreparedTranslation &)> &use);
  void consume(const PreviewRef &ref);

 private:
  struct Budget
  {
    std::mutex mutex;
    std::size_t bytes{0};
    std::size_t limit{0};
  };
  struct Lease
  {
    std::shared_ptr<Budget> budget;
    std::size_t bytes;
    ~Lease();
  };
  struct Source
  {
    SceneIdentity *identity;
    std::weak_ptr<const void> lifetime;
  };
  static std::shared_ptr<const void> reserve(
      const std::shared_ptr<Budget> &budget, std::size_t bytes);
  bool envelope(const TranslationRequest &request) const;
  bool authorized(const std::string &owner, SceneId scene) const;
  operations::Request makeRequest(TranslationRequest request,
      std::string owner,
      std::shared_ptr<const void> reservation);
  operations::Request makeCommitRequest(PreviewRef preview,
      std::string owner,
      std::shared_ptr<const void> reservation);
  EditResult commit(const PreviewRef &preview,
      const std::string &owner,
      operations::StepContext &ctx,
      double deadlineAt);
  operations::Step prepare(TranslationRequest request,
      const std::string &owner,
      scene::Scene &scene,
      std::shared_ptr<const void> reservation,
      double deadlineAt);
  Integration *m_integration{nullptr};
  session::SessionAuthority *m_authority{nullptr};
  OperationEngine *m_engine{nullptr};
  EditService *m_edits{nullptr};
  std::weak_ptr<const void> m_editLifetime;
  PreviewLimits m_limits;
  session::MonotonicClock m_clock;
  bool m_valid{false};
  std::shared_ptr<int> m_alive{std::make_shared<int>(0)};
  std::shared_ptr<Budget> m_budget{std::make_shared<Budget>()};
  std::shared_ptr<const void> m_infrastructure;
  mutable std::mutex m_mutex;
  core::FlatMap<SceneId, Source> m_sources;
  std::deque<std::shared_ptr<const PreparedTranslation>> m_plans;
};

namespace translation_detail {
inline bool token(const std::string &value)
{
  return value.size() == 32
      && std::all_of(value.begin(), value.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}
inline std::string handle(const identity::NodeHandle &h)
{
  char buffer[33];
  std::snprintf(buffer,
      sizeof(buffer),
      "%016llx%016llx",
      (unsigned long long)h.domain,
      (unsigned long long)h.serial);
  return buffer;
}

inline std::string point(const std::array<double, 3> &p)
{
  char buffer[128];
  std::snprintf(
      buffer, sizeof(buffer), "[%.17g,%.17g,%.17g]", p[0], p[1], p[2]);
  return buffer;
}

inline std::string dependency(const NodeVersion &v)
{
  const auto h = handle(v.expected.node);
  const auto &d = v.dependencies;
  const auto layer = handle({d.layer.launch, d.layer.domain, d.layer.serial});
  const auto structure = std::to_string(d.structuralVersion);
  return "\"handle\":\"" + h + "\",\"node_edit_version\":\"ne:" + h + ":"
      + std::to_string(v.expected.version)
      + "\",\"structural_version\":\"ns:" + h + ":" + layer + ":" + structure
      + "\",\"ancestry_version\":\"na:" + h + ":" + layer + ":" + structure
      + ":" + std::to_string(d.transformVersion) + "\"";
}

inline std::string matrix(const core::math::mat4 &m)
{
  std::string result = "[";
  for (int row = 0; row < 4; ++row) {
    if (row)
      result += ',';
    result += '[';
    for (int col = 0; col < 4; ++col) {
      if (col)
        result += ',';
      char value[32];
      std::snprintf(value, sizeof(value), "%.9g", double(m[col][row]));
      result += value;
    }
    result += ']';
  }
  return result + ']';
}

inline ValueError parentAffine(
    const std::vector<scene::LayerNodeRef> &ancestors, detail::Affine &parent)
{
  parent = detail::identityAffine();
  for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
    const auto &data = (*it)->value();
    if (anari::isArray(data.type()))
      return ValueError::UNSUPPORTED_NODE_KIND;
    if (!data.isTransform())
      continue;
    auto error = detail::appendAffine(parent, data.getTransform());
    if (error != ValueError::NONE)
      return error;
  }
  return ValueError::NONE;
}

inline ValueError origin(const detail::Affine &parent,
    const core::math::mat4 &local,
    std::array<double, 3> &point)
{
  auto world = parent;
  auto error = detail::appendAffine(world, local);
  if (error != ValueError::NONE)
    return error;
  for (int row = 0; row < 3; ++row)
    point[row] = world[row][3];
  return ValueError::NONE;
}

inline void sortSelection(std::vector<NodeVersion> &selection)
{
  std::sort(
      selection.begin(), selection.end(), [](const auto &a, const auto &b) {
        const auto &x = a.expected.node;
        const auto &y = b.expected.node;
        const auto &ad = a.dependencies;
        const auto &bd = b.dependencies;
        return std::tie(x.launch.activation,
                   x.launch.nonce,
                   x.domain,
                   x.serial,
                   a.expected.version,
                   ad.layer.domain,
                   ad.layer.serial,
                   ad.structuralVersion,
                   ad.transformVersion)
            < std::tie(y.launch.activation,
                y.launch.nonce,
                y.domain,
                y.serial,
                b.expected.version,
                bd.layer.domain,
                bd.layer.serial,
                bd.structuralVersion,
                bd.transformVersion);
      });
}

inline EditBatch canonicalBatch(const TranslationRequest &request)
{
  EditBatch batch;
  batch.scene = request.scene;
  for (const auto &v : request.selection) {
    EditEntry entry;
    entry.targetKind = TargetKind::NODE;
    entry.nodeExpected = v.expected;
    entry.nodeDependencies = v.dependencies;
    entry.parameter = "position";
    entry.space = "world";
    entry.hasAncestryVersion = true;
    entry.value = JsonValue::numbers(
        {request.worldDelta[0], request.worldDelta[1], request.worldDelta[2]});
    batch.edits.push_back(std::move(entry));
  }
  return batch;
}
} // namespace translation_detail

inline TranslationService::Lease::~Lease()
{
  std::lock_guard<std::mutex> lock(budget->mutex);
  budget->bytes -= bytes;
}

inline std::shared_ptr<const void> TranslationService::reserve(
    const std::shared_ptr<Budget> &budget, std::size_t bytes)
{
  std::lock_guard<std::mutex> lock(budget->mutex);
  if (bytes > budget->limit - budget->bytes)
    return {};
  // Allocate the control block before changing the counter.
  std::shared_ptr<Lease> lease;
  try {
    lease = std::make_shared<Lease>();
  } catch (const std::bad_alloc &) {
    return {};
  }
  lease->budget = budget;
  lease->bytes = bytes;
  budget->bytes += bytes;
  return lease;
}

inline TranslationService::TranslationService(Integration &integration,
    session::SessionAuthority &authority,
    OperationEngine &engine,
    EditService &edits,
    PreviewLimits limits,
    session::MonotonicClock clock)
    : m_integration(&integration),
      m_authority(&authority),
      m_engine(&engine),
      m_edits(&edits),
      m_editLifetime(edits.lifetime()),
      m_limits(limits),
      m_clock(std::move(clock))
{
  m_valid = m_clock && edits.launchId() == authority.launchId()
      && limits.perSession > 0 && limits.perSession <= 32
      && limits.globalCount >= limits.perSession && limits.globalCount <= 64
      && limits.aggregateBytes
          >= INFRASTRUCTURE_BYTES + PREPARATION_BYTES + 2 * RESULT_BYTES
      && limits.aggregateBytes <= 64 * 1024 * 1024 && limits.maxSelection > 0
      && limits.maxSelection <= MAX_BATCH_EDITS && limits.ancestrySteps > 0
      && limits.ancestrySteps <= 4096
      && limits.effectEntries >= limits.maxSelection
      && limits.effectEntries <= 64 && std::isfinite(limits.lifetimeSeconds)
      && limits.lifetimeSeconds > 0. && limits.lifetimeSeconds <= 3600.
      && std::isfinite(limits.deadlineSeconds) && limits.deadlineSeconds > 0.
      && limits.deadlineSeconds <= 60.;
  m_budget->limit = limits.aggregateBytes;
  if (m_valid) {
    m_infrastructure = reserve(m_budget, INFRASTRUCTURE_BYTES);
    if (!m_infrastructure) {
      m_valid = false;
      return;
    }
    m_sources.reserve(limits.globalCount);
  }
}

inline TranslationService::~TranslationService()
{
  m_alive.reset();
}
inline bool TranslationService::valid() const
{
  return m_valid;
}
inline const PreviewLimits &TranslationService::limits() const
{
  return m_limits;
}

inline bool TranslationService::attachScene(
    SceneId scene, SceneIdentity &identity)
{
  if (!valid() || !m_integration->isRegisteredScene(scene))
    return false;
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_sources.contains(scene) && m_sources.size() >= m_limits.globalCount)
    return false;
  m_plans.erase(std::remove_if(m_plans.begin(),
                    m_plans.end(),
                    [&](const auto &p) { return p->input.scene == scene; }),
      m_plans.end());
  m_sources.set(scene, Source{&identity, identity.lifetime()});
  return true;
}

inline bool TranslationService::envelope(
    const TranslationRequest &request) const
{
  if (!valid() || request.scene == INVALID_SCENE_ID || request.selection.empty()
      || request.selection.size() > m_limits.maxSelection)
    return false;
  for (auto d : request.worldDelta)
    if (!std::isfinite(d))
      return false;
  for (const auto &v : request.selection)
    if (!v.expected.node.valid() || !v.expected.version
        || v.dependencies.node != v.expected.node
        || !v.dependencies.layer.valid() || !v.dependencies.structuralVersion
        || v.dependencies.ancestorSlot)
      return false;
  return true;
}

inline bool TranslationService::authorized(
    const std::string &owner, SceneId scene) const
{
  const auto target = session::Target::scene(scene);
  if (m_editLifetime.expired() || !m_integration->nodeEditsExposed(scene))
    return false;
  if (owner.empty())
    return m_authority->authorizeLocal(target, session::Capability::READ)
        && m_authority->authorizeLocal(target, session::Capability::EDIT);
  return m_authority->authorizeExecution(
             owner, target, session::Capability::READ)
      == session::Code::OK
      && m_authority->authorizeExecution(
             owner, target, session::Capability::EDIT)
      == session::Code::OK;
}

inline void TranslationService::pump()
{
  if (!valid())
    return;
  const auto now = m_clock();
  // Authorization may end a session and call the engine; never hold our lock
  // around it (and never call pump from an engine status-copy callback).
  std::vector<std::shared_ptr<const PreparedTranslation>> plans;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    plans.assign(m_plans.begin(), m_plans.end());
  }
  for (const auto &plan : plans)
    if (now >= plan->expiresAt || plan->sourceLifetime.expired()
        || !authorized(plan->sessionId, plan->input.scene))
      consume(plan->ref);
}

inline std::size_t TranslationService::retainedCount() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_plans.size();
}
inline std::size_t TranslationService::ownedBytes() const
{
  std::lock_guard<std::mutex> lock(m_budget->mutex);
  return m_budget->bytes;
}
inline void TranslationService::consume(const PreviewRef &ref)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_plans.erase(std::remove_if(m_plans.begin(),
                    m_plans.end(),
                    [&](const auto &p) {
                      return p->ref.launchId == ref.launchId
                          && p->ref.previewId == ref.previewId;
                    }),
      m_plans.end());
}

inline Outcome TranslationService::withPrepared(
    const operations::Credentials &credentials,
    const PreviewRef &ref,
    const std::function<Outcome(const PreparedTranslation &)> &use)
{
  if (!m_integration->inServiceCall())
    return Outcome::INVALID_REQUEST;
  if (m_authority->authenticate(credentials) != session::Code::OK)
    return Outcome::UNAUTHENTICATED;
  pump();
  std::shared_ptr<const PreparedTranslation> plan;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto &p : m_plans)
      if (p->ref.launchId == ref.launchId && p->ref.previewId == ref.previewId
          && p->sessionId == credentials.sessionId) {
        plan = p;
        break;
      }
  }
  if (!plan)
    return Outcome::OUTCOME_UNAVAILABLE;
  if (!authorized(plan->sessionId, plan->input.scene))
    return Outcome::FORBIDDEN;
  return use(*plan);
}

inline Outcome TranslationService::withPreparedLocal(const PreviewRef &ref,
    const std::function<Outcome(const PreparedTranslation &)> &use)
{
  if (!m_integration->inServiceCall())
    return Outcome::INVALID_REQUEST;
  pump();
  std::shared_ptr<const PreparedTranslation> plan;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto &p : m_plans)
      if (p->ref.launchId == ref.launchId && p->ref.previewId == ref.previewId
          && p->sessionId.empty()) {
        plan = p;
        break;
      }
  }
  if (!plan)
    return Outcome::OUTCOME_UNAVAILABLE;
  if (!authorized({}, plan->input.scene))
    return Outcome::FORBIDDEN;
  return use(*plan);
}

inline operations::Request TranslationService::makeRequest(
    TranslationRequest request,
    std::string owner,
    std::shared_ptr<const void> reservation)
{
  translation_detail::sortSelection(request.selection);
  operations::Request work;
  work.kind = "translate_preview";
  work.target = session::Target::scene(request.scene);
  work.capability = session::Capability::EDIT;
  work.deadlineSeconds = m_limits.deadlineSeconds;
  work.canonicalArguments = "translation;"
      + EditService::canonicalArguments(
          translation_detail::canonicalBatch(request));
  if (!reservation) {
    // Still let the engine recognize historical same-ID replay without a new
    // plan reservation. A new identity completes busy, retaining no selection
    // payload; its small canonical envelope follows the engine's own bounds.
    work.executor = [](operations::StepContext &) {
      operations::Step step;
      step.outcome = Outcome::BUSY;
      return step;
    };
    return work;
  }
  std::weak_ptr<int> alive = m_alive;
  const auto deadlineAt = m_clock() + m_limits.deadlineSeconds;
  work.executor = [this,
                      alive,
                      request = std::move(request),
                      owner = std::move(owner),
                      reservation = std::move(reservation),
                      deadlineAt](operations::StepContext &ctx) {
    operations::Step step;
    if (!alive.lock()) {
      step.outcome = Outcome::OUTCOME_UNAVAILABLE;
      return step;
    }
    if (ctx.abandoned)
      return step;
    pump();
    if (!authorized(owner, request.scene)) {
      step.outcome = Outcome::FORBIDDEN;
      return step;
    }
    try {
      return prepare(request, owner, ctx.scene, reservation, deadlineAt);
    } catch (const std::bad_alloc &) {
      step.outcome = Outcome::BUSY;
      return step;
    }
  };
  return work;
}

inline Admission TranslationService::submit(
    const operations::Credentials &credentials,
    const OperationRef &ref,
    TranslationRequest request)
{
  Admission rejected;
  if (!envelope(request)) {
    rejected.code = Outcome::INVALID_REQUEST;
    return rejected;
  }
  const auto read = m_authority->authorize(credentials,
      session::Target::scene(request.scene),
      session::Capability::READ);
  if (read != session::Code::OK) {
    rejected.code = operations::fromSessionCode(read);
    return rejected;
  }
  try {
    auto reservation = reserve(m_budget, PREPARATION_BYTES);
    return m_engine->submit(credentials,
        ref,
        makeRequest(
            std::move(request), credentials.sessionId, std::move(reservation)));
  } catch (const std::bad_alloc &) {
    rejected.code = Outcome::BUSY;
    return rejected;
  }
}

inline LocalAdmission TranslationService::submitLocal(
    TranslationRequest request)
{
  LocalAdmission rejected;
  if (!envelope(request)) {
    rejected.code = Outcome::INVALID_REQUEST;
    return rejected;
  }
  try {
    auto reservation = reserve(m_budget, PREPARATION_BYTES);
    if (!reservation) {
      rejected.code = Outcome::BUSY;
      return rejected;
    }
    return m_engine->submitLocal(
        makeRequest(std::move(request), {}, std::move(reservation)));
  } catch (const std::bad_alloc &) {
    rejected.code = Outcome::BUSY;
    return rejected;
  }
}

inline Admission TranslationService::submitCommit(
    const operations::Credentials &credentials,
    const OperationRef &ref,
    PreviewRef preview)
{
  Admission rejected;
  if (!valid() || preview.scene == INVALID_SCENE_ID
      || !translation_detail::token(preview.launchId)
      || !translation_detail::token(preview.previewId)) {
    rejected.code = Outcome::INVALID_REQUEST;
    return rejected;
  }
  const auto read = m_authority->authorize(credentials,
      session::Target::scene(preview.scene),
      session::Capability::READ);
  if (read != session::Code::OK) {
    rejected.code = operations::fromSessionCode(read);
    return rejected;
  }
  try {
    return m_engine->submit(credentials,
        ref,
        makeCommitRequest(std::move(preview),
            credentials.sessionId,
            reserve(m_budget, PREPARATION_BYTES)));
  } catch (const std::bad_alloc &) {
    rejected.code = Outcome::BUSY;
    return rejected;
  }
}

inline LocalAdmission TranslationService::submitCommitLocal(PreviewRef preview)
{
  LocalAdmission rejected;
  if (!valid() || preview.scene == INVALID_SCENE_ID
      || !translation_detail::token(preview.launchId)
      || !translation_detail::token(preview.previewId)) {
    rejected.code = Outcome::INVALID_REQUEST;
    return rejected;
  }
  try {
    return m_engine->submitLocal(makeCommitRequest(
        std::move(preview), {}, reserve(m_budget, PREPARATION_BYTES)));
  } catch (const std::bad_alloc &) {
    rejected.code = Outcome::BUSY;
    return rejected;
  }
}

inline operations::Request TranslationService::makeCommitRequest(
    PreviewRef preview,
    std::string owner,
    std::shared_ptr<const void> reservation)
{
  operations::Request work;
  work.kind = "translate_commit";
  work.target = session::Target::scene(preview.scene);
  work.capability = session::Capability::EDIT;
  work.deadlineSeconds = m_limits.deadlineSeconds;
  work.canonicalArguments = "translation-commit;" + preview.launchId + ";"
      + preview.previewId + ";" + std::to_string(preview.scene);
  std::weak_ptr<int> alive = m_alive;
  auto committed = std::make_shared<std::optional<EditResult>>();
  const auto budget = m_budget;
  const auto deadlineAt = m_clock() + m_limits.deadlineSeconds;
  work.executor = [this,
                      alive,
                      budget,
                      deadlineAt,
                      preview = std::move(preview),
                      owner = std::move(owner),
                      committed,
                      reservation = std::move(reservation)](
                      operations::StepContext &ctx) {
    operations::Step step;
    const bool usable = alive.lock() && !m_editLifetime.expired();
    if (!*committed) {
      if (!usable) {
        step.outcome = Outcome::OUTCOME_UNAVAILABLE;
        return step;
      }
      if (ctx.abandoned)
        return step;
      if (!reservation) {
        step.outcome = Outcome::BUSY;
        return step;
      }
      try {
        auto result = commit(preview, owner, ctx, deadlineAt);
        if (!result.committed) {
          step.outcome = result.outcome;
          step.result = "{\"committed\":false}";
          return step;
        }
        *committed = std::move(result);
      } catch (const std::bad_alloc &) {
        step.outcome = Outcome::BUSY;
        return step;
      }
      step.committed = true;
      step.status = operations::Step::Status::SYNCHRONIZING;
      return step;
    }
    const auto &result = **committed;
    step.committed = true;
    step.outcome = usable ? m_edits->synchronizeCommitted(preview.scene)
                          : Outcome::APPLIED_WITH_SYNC_FAILURE;
    step.result = "{\"batch_ref\":" + detail::batchRefJson(*result.batchRef)
        + ","
        + detail::commitDetailJson(
            true, result.sceneRevision, result.versions, result.nodeVersions)
        + "}";
    step.resourceLease = reservation;
    step.reserveStatus = [budget] { return reserve(budget, 2 * RESULT_BYTES); };
    return step;
  };
  return work;
}

inline EditResult TranslationService::commit(const PreviewRef &preview,
    const std::string &owner,
    operations::StepContext &ctx,
    double deadlineAt)
{
  EditResult result;
  auto fail = [&](Outcome outcome) {
    result.outcome = outcome;
    return result;
  };
  pump();
  std::shared_ptr<const PreparedTranslation> plan;
  Source source{};
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto &p : m_plans)
      if (p->ref.launchId == preview.launchId
          && p->ref.previewId == preview.previewId && p->sessionId == owner
          && p->input.scene == preview.scene) {
        plan = p;
        break;
      }
    const auto *attached = m_sources.at(preview.scene);
    if (attached)
      source = *attached;
  }
  if (!plan || plan->sourceLifetime.expired() || source.lifetime.expired())
    return fail(Outcome::OUTCOME_UNAVAILABLE);
  if (!authorized(owner, preview.scene))
    return fail(Outcome::FORBIDDEN);
  auto &ids = *source.identity;
  if (ids.scene() != &ctx.scene)
    return fail(Outcome::OUTCOME_UNAVAILABLE);
  // Validate every selected node, including inherited entries, and the exact
  // selected-ancestor relation. Never replace the approved normalization.
  std::size_t remaining = m_limits.ancestrySteps;
  PreparedBatch prepared;
  prepared.identity = &ids;
  EditBatch batch;
  batch.scene = preview.scene;
  batch.edits.reserve(plan->roots.size());
  prepared.nodes.reserve(plan->roots.size());
  for (const auto &selected : plan->input.selection) {
    identity::NodeDependencyWalk current;
    std::vector<scene::LayerNodeRef> ancestors;
    const auto status = detail::nodeDependencies(
        ids, selected.expected.node, current, remaining, &ancestors);
    if (status == Outcome::BUSY)
      return fail(status); // bounded-work exhaustion is not definite staleness
    if (status != Outcome::OK || !ids.isCurrent(selected.expected)
        || !current.matchesAncestry(selected.dependencies)) {
      consume(preview); // definite stale plans cannot revive on change-back
      return fail(status == Outcome::OK ? Outcome::CONFLICT : status);
    }
    auto node = ids.resolve(selected.expected.node);
    const bool inherited = std::any_of(
        ancestors.begin(), ancestors.end(), [&](const auto &ancestor) {
          const auto handle = ids.handleOf(ancestor);
          return std::any_of(plan->input.selection.begin(),
              plan->input.selection.end(),
              [&](const auto &v) { return v.expected.node == handle; });
        });
    const auto root = std::find_if(
        plan->roots.begin(), plan->roots.end(), [&](const auto &r) {
          return r.dependency.expected.node == selected.expected.node;
        });
    if (inherited != (root == plan->roots.end())) {
      consume(preview);
      return fail(Outcome::CONFLICT);
    }
    if (inherited)
      continue;
    if (!node || !node->value().isTransform()
        || anari::isArray(node->value().type())
        || node->value().getTransform() != root->beforeMatrix
        || node->value().getTransformSRT() != root->beforeSRT) {
      consume(preview);
      return fail(Outcome::CONFLICT);
    }
    EditEntry entry;
    entry.targetKind = TargetKind::NODE;
    entry.nodeExpected = selected.expected;
    entry.nodeDependencies = selected.dependencies;
    entry.parameter = "position";
    entry.space = "world";
    entry.hasAncestryVersion = true;
    batch.edits.push_back(std::move(entry));
    PlannedNode p;
    p.node = node;
    p.entry = &batch.edits.back();
    p.worldEntry = p.entry; // preserves conditional ancestor-history restore
    p.dependencies = current;
    p.changedTransform = true;
    p.matrix = root->matrix;
    p.srt = root->srt;
    prepared.nodes.push_back(std::move(p));
    const auto *layer = node->value().layer();
    if (std::find(prepared.layers.begin(), prepared.layers.end(), layer)
        == prepared.layers.end())
      prepared.layers.push_back(layer);
  }
  const auto now = m_clock();
  if (!std::isfinite(now) || !std::isfinite(deadlineAt) || now >= deadlineAt)
    return fail(Outcome::TIMEOUT);
  if (now >= plan->expiresAt)
    return fail(Outcome::OUTCOME_UNAVAILABLE);
  if (!authorized(owner, preview.scene))
    return fail(Outcome::FORBIDDEN);
  result = m_edits->commitPrepared(preview.scene, prepared, ctx, [&] {
    const auto now = m_clock();
    if (!std::isfinite(now) || now >= deadlineAt)
      return Outcome::TIMEOUT;
    if (now >= plan->expiresAt || plan->sourceLifetime.expired())
      return Outcome::OUTCOME_UNAVAILABLE;
    if (!authorized(owner, preview.scene))
      return Outcome::FORBIDDEN;
    // Revisit precise dependencies after all planned allocations.
    for (const auto &selected : plan->input.selection) {
      identity::NodeDependencyWalk current;
      const auto status = detail::nodeDependencies(
          ids, selected.expected.node, current, remaining);
      if (status == Outcome::BUSY)
        return status;
      if (status != Outcome::OK || !ids.isCurrent(selected.expected)
          || !current.matchesAncestry(selected.dependencies)) {
        consume(preview);
        return status == Outcome::OK ? Outcome::CONFLICT : status;
      }
    }
    return Outcome::OK;
  });
  if (result.committed)
    consume(preview);
  return result;
}

inline operations::Step TranslationService::prepare(TranslationRequest request,
    const std::string &owner,
    scene::Scene &scene,
    std::shared_ptr<const void> reservation,
    double deadlineAt)
{
  operations::Step step;
  auto fail = [&](Outcome outcome, ValueError error = ValueError::NONE) {
    step.outcome = outcome;
    if (error != ValueError::NONE)
      step.result = "{\"value_error\":\""
          + std::string(detail::valueErrorName(error)) + "\"}";
    return step;
  };
  Source source{};
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto *attached = m_sources.at(request.scene);
    if (!attached)
      return fail(Outcome::OUTCOME_UNAVAILABLE);
    source = *attached;
  }
  if (source.lifetime.expired())
    return fail(Outcome::OUTCOME_UNAVAILABLE);
  auto &ids = *source.identity;
  if (ids.scene() != &scene)
    return fail(Outcome::INVALID_REQUEST);
  if (!std::isfinite(deadlineAt) || m_clock() >= deadlineAt)
    return fail(Outcome::TIMEOUT);
  translation_detail::sortSelection(request.selection);
  std::size_t duplicates = 0;
  for (std::size_t i = 1; i < request.selection.size();) {
    const auto &a = request.selection[i - 1];
    const auto &b = request.selection[i];
    if (a.expected.node != b.expected.node) {
      ++i;
      continue;
    }
    if (a.expected != b.expected
        || !a.dependencies.matchesAncestry(b.dependencies))
      return fail(Outcome::CONFLICT);
    request.selection.erase(request.selection.begin() + i);
    ++duplicates;
  }
  struct Selected
  {
    NodeVersion version;
    scene::LayerNodeRef node;
    std::vector<scene::LayerNodeRef> ancestors;
    detail::Affine parent;
    std::array<double, 3> origin;
    bool inherited{false};
  };
  std::vector<Selected> selected;
  selected.reserve(request.selection.size());
  std::size_t remaining = m_limits.ancestrySteps;
  for (const auto &v : request.selection) {
    Selected item;
    item.version = v;
    identity::NodeDependencyWalk current;
    const auto status = detail::nodeDependencies(
        ids, v.expected.node, current, remaining, &item.ancestors);
    if (status != Outcome::OK)
      return fail(status);
    if (!ids.isCurrent(v.expected) || !current.matchesAncestry(v.dependencies))
      return fail(Outcome::CONFLICT);
    item.node = ids.resolve(v.expected.node);
    if (anari::isArray(item.node->value().type()))
      return fail(Outcome::INVALID_REQUEST, ValueError::UNSUPPORTED_NODE_KIND);
    auto error = translation_detail::parentAffine(item.ancestors, item.parent);
    if (error != ValueError::NONE)
      return fail(Outcome::INVALID_REQUEST, error);
    // Non-transform inherited nodes use identity local placement. Scalar
    // selected roots alone are eligible; validation never drops descendants.
    const auto local = item.node->value().isTransform()
        ? item.node->value().getTransform()
        : core::math::IDENTITY_MAT4;
    error = translation_detail::origin(item.parent, local, item.origin);
    if (error != ValueError::NONE)
      return fail(Outcome::INVALID_REQUEST, error);
    selected.push_back(std::move(item));
  }
  for (auto &item : selected)
    for (const auto &ancestor : item.ancestors)
      if (std::any_of(selected.begin(), selected.end(), [&](const auto &other) {
            return other.node == ancestor;
          })) {
        item.inherited = true;
        break;
      }
  EditBatch batch;
  batch.scene = request.scene;
  for (const auto &item : selected) {
    if (item.inherited)
      continue;
    EditEntry e;
    e.targetKind = TargetKind::NODE;
    e.nodeExpected = item.version.expected;
    e.nodeDependencies = item.version.dependencies;
    e.parameter = "position";
    e.space = "world";
    e.hasAncestryVersion = true;
    auto target = item.origin;
    for (int i = 0; i < 3; ++i)
      target[i] += request.worldDelta[i];
    e.value = JsonValue::numbers({target[0], target[1], target[2]});
    batch.edits.push_back(std::move(e));
  }
  PreparedBatch prepared;
  const auto result = m_edits->prepare(batch, scene, prepared, remaining);
  if (result.outcome != Outcome::OK)
    return fail(result.outcome, result.valueError);
  auto plan = std::make_shared<PreparedTranslation>();
  plan->sessionId = owner;
  plan->input = request;
  plan->sourceLifetime = source.lifetime;
  plan->expiresAt = m_clock() + m_limits.lifetimeSeconds;
  plan->reservation = std::move(reservation);
  if (!std::isfinite(plan->expiresAt))
    return fail(Outcome::INVALID_REQUEST);
  std::uint8_t bytes[16];
  if (!session::secureRandom(bytes, sizeof(bytes)))
    return fail(Outcome::BUSY);
  plan->ref = {m_authority->launchId(),
      session::toLowerHex(bytes, sizeof(bytes)),
      request.scene};
  for (const auto &p : prepared.nodes) {
    const auto item = std::find_if(selected.begin(),
        selected.end(),
        [&](const auto &s) { return s.node == p.node; });
    PreparedTranslationRoot root;
    root.dependency = item->version;
    root.beforeMatrix = p.node->value().getTransform();
    root.beforeSRT = p.node->value().getTransformSRT();
    root.matrix = p.matrix;
    root.srt = p.srt;
    root.currentOrigin = item->origin;
    const auto error = translation_detail::origin(
        item->parent, root.matrix, root.predictedOrigin);
    if (error != ValueError::NONE)
      return fail(Outcome::INVALID_REQUEST, error);
    plan->roots.push_back(root);
  }
  std::string payload = "{\"preview_ref\":{\"launch_id\":\""
      + plan->ref.launchId + "\",\"preview_id\":\"" + plan->ref.previewId
      + "\",\"scene_id\":" + std::to_string(request.scene)
      + "},\"duplicates_collapsed\":" + std::to_string(duplicates)
      + ",\"roots\":[";
  for (const auto &root : plan->roots) {
    if (payload.back() != '[')
      payload += ',';
    payload += "{" + translation_detail::dependency(root.dependency)
        + ",\"current_world_origin\":"
        + translation_detail::point(root.currentOrigin)
        + ",\"predicted_world_origin\":"
        + translation_detail::point(root.predictedOrigin)
        + ",\"prepared_local_matrix\":"
        + translation_detail::matrix(root.matrix) + "}";
  }
  payload += "],\"inherited_selected\":[";
  for (const auto &item : selected) {
    if (!item.inherited)
      continue;
    plan->inherited.push_back(item.version);
    // Use the exact rounded prepared ancestor, not the requested delta, to
    // predict descendant origins. No selected descendant is written twice.
    auto parent = detail::identityAffine();
    for (auto a = item.ancestors.rbegin(); a != item.ancestors.rend(); ++a) {
      if (!(*a)->value().isTransform())
        continue;
      const auto proposed = std::find_if(prepared.nodes.begin(),
          prepared.nodes.end(),
          [&](const auto &p) { return p.node == *a; });
      const auto error = detail::appendAffine(parent,
          proposed == prepared.nodes.end() ? (*a)->value().getTransform()
                                           : proposed->matrix);
      if (error != ValueError::NONE)
        return fail(Outcome::INVALID_REQUEST, error);
    }
    std::array<double, 3> predicted;
    const auto error = translation_detail::origin(parent,
        item.node->value().isTransform() ? item.node->value().getTransform()
                                         : core::math::IDENTITY_MAT4,
        predicted);
    if (error != ValueError::NONE)
      return fail(Outcome::INVALID_REQUEST, error);
    if (payload.back() != '[')
      payload += ',';
    payload += "{" + translation_detail::dependency(item.version)
        + ",\"current_world_origin\":" + translation_detail::point(item.origin)
        + ",\"predicted_world_origin\":" + translation_detail::point(predicted)
        + "}";
  }
  payload +=
      "],\"impact\":{\"selected_origins_complete\":true,\"unselected_descendants\":\"not_enumerated\",\"complete_scene_scan\":false},\"warnings\":[\"Unselected descendants inherit root movement; their count and identities are not enumerated. World position is the node origin, not a geometry or bounds centre.\"],\"lifetime_seconds\":"
      + std::to_string(m_limits.lifetimeSeconds) + "}";
  if (payload.size() > RESULT_BYTES)
    return fail(Outcome::ITEM_TOO_LARGE);
  if (m_clock() >= deadlineAt)
    return fail(Outcome::TIMEOUT);
  if (!authorized(owner, request.scene))
    return fail(Outcome::FORBIDDEN);
  // Prepare every allocation, including the store entry and result callback,
  // before publication. Status/replay copies reserve their own finite bytes.
  const auto budget = m_budget;
  const auto target = session::Target::scene(request.scene);
  // Historical JSON is independently owned by the engine, not the plan or
  // its source. Expiry/eviction/source/service retirement cannot revive a
  // plan, nor erase/relabel a receipt. Only current authority/exposure and
  // the retained shared byte budget govern copying that historical value.
  step.reserveStatus =
      [budget, target, authority = m_authority, integration = m_integration] {
        const auto caps = authority->effectiveCapabilities(target);
        if (!caps.read || !caps.edit
            || !integration->nodeEditsExposed(target.id))
          return std::shared_ptr<const void>{};
        return reserve(budget, 2 * RESULT_BYTES);
      };
  step.result = std::move(payload);
  step.resourceLease = plan->reservation;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Push can allocate; do it before evicting any valid older plan.
    m_plans.push_back(plan);
    auto countOwner = [&] {
      return std::count_if(m_plans.begin(), m_plans.end(), [&](const auto &p) {
        return p->sessionId == owner;
      });
    };
    while (countOwner() > m_limits.perSession) {
      auto old = std::find_if(m_plans.begin(),
          m_plans.end(),
          [&](const auto &p) { return p->sessionId == owner; });
      m_plans.erase(old);
    }
    while (m_plans.size() > m_limits.globalCount)
      m_plans.pop_front();
  }
  return step;
}

} // namespace vsr::integration::edit

# 121 — Restoration commit/conflict/expiry update ActivityLog Eligibility

**Status:** resolved
**Milestone:** 1
**Blocked by:** 120
**Files:** src/vsr/integration/restore/BatchRestore.hpp, src/vsr/integration/activity/Activity.hpp

Part 3 of split ticket 80.

`RestoreService` tracks consumption in its own `m_consumed` set and releases
the payload from `RestorationStore`. It should also call
`ActivityLog::markRestored` on commit and `markConflicting` on a conflict, and
treat a log `EXPIRED` eligibility as `outcome_unavailable` (see 117).

## Acceptance criteria

- [x] Headless test: restore commit → entry `RESTORED` with `restorationBytes`
      released from `usage().bytes`; conflict → `CONFLICTING`; eviction →
      `EXPIRED`.
- [x] Viewer-model and MCP tool tests still green.
- [x] `vsrIntegrationTests "[integration]"` green.

## Comments

### 2026-09-28 iteration — resolved
`StepContext` gained `markRestored`/`markConflicting`, routed under the engine
lock to new `ActivitySink::restorationRestored/Conflicting` hooks (default
no-op), which `EngineActivitySink` forwards to `ActivityLog::markRestored/
markConflicting`. `RestoreService` calls them on commit/conflict. A conflict now
makes the batch permanently CONFLICTING (versions only advance): payload
released from the store, retries report `conflict`, and `eligible()` is false
(new `conflicting()` query; MCP README row and tests updated). Before selecting,
a batch the log no longer marks eligible is expired from the store, so the
result is `outcome_unavailable`. Ticket 117's structured reason was not folded in.
Verified: `cmake --build _out/_cmake --config RelWithDebInfo --target vsrIntegrationTests vsrViewer && _out/_cmake/RelWithDebInfo/vsrIntegrationTests "[integration]"` → All tests passed (137 test cases), 2 runs
Holes filed: 137-conflicting-batch-permanence

# 07 — Add atomic Agent Edit Batches with Batch References and ordinary host synchronization

**Status:** resolved
**Milestone:** 1
**Blocked by:** 03, 04, 05
**Files:** src/vsr/integration/edit/, src/tests/integration/test_IntegrationEdit.cpp

Contract: `.scratch/pi-integration/spec.md` — Implementation Decisions §10 (Agent Edit Batches and downstream synchronization, except the explicit Resynchronize and view-registration paragraphs handled by ticket 08), the Batch Reference definition, and Behavioral acceptance coverage item 6 (commit/notification/ordinary-hook parts). The spec is authoritative; read those sections before starting. Business behavior is tested at the public headless integration API (spec §Testing Decisions, "Confirmed primary seam"), with deterministic clients and a controlled monotonic clock.

## Current behavior

Values validate (ticket 05) and operations admit (ticket 03), but nothing mutates a scene through the integration.

## Desired behavior

A new suite `test_IntegrationEdit.cpp`, using real scenes and an injected host hook, shows:

- A batch of up to 64 edits validates all targets, permissions, values and expected object versions at the safe point. A conflicting stale edit (same object, including another parameter or a change-back) rejects with `conflict`; changes on other objects do not conflict.
- Injected failures during preparation, allocation or restoration-storage reservation leave the scene unchanged, emit no mutation notifications and publish no batch. The admitted ID stays consumed.
- Observers see only complete commits.
- After commit, the scene is `synchronizing` and the host's normal-synchronization hook runs exactly once. Terminal success requires the hook's synchronous acknowledgement for the views registered when the hook was invoked.
- A hook failure, exception or unconfirmable readiness returns `applied_with_sync_failure`, marks the scene `failed`, keeps the edit applied and still publishes its Batch Reference.
- On an already-failed scene, an edit whose hook succeeds still returns `applied_with_sync_failure` and the scene stays `failed`.
- Cancellation or disconnect during the hook never reports a non-commit.
- Every commit gets a `batch_ref` of `{launch_id, batch_id}` (32 lowercase hex, unique within the launch, never reused) and reserves restoration data for ticket 09.

## Out of scope

- Explicit Resynchronize, the recovery callback and view-registration confirmation (ticket 08).
- Batch Restoration execution (ticket 09).
- The activity model and eviction (ticket 10). Keep restoration records in a minimal store that ticket 10 will own.

## Comments

### 2026-09-28 iteration — resolved
Added the header-only `src/vsr/integration/edit/AgentEdit.hpp`: an `EditService` on top of `OperationEngine` with two steps. The first validates, prepares and commits, flushing all signals after every value is stored. The second runs the normal-sync hook exactly once. It also adds `BatchRef` and a minimal `RestorationStore` for tickets 09 and 10, plus a sticky per-scene failed state that ticket 08 will extend. I added one small accessor outside this ticket's Files: `Integration::normalSyncHook(SceneId)` in `src/vsr/integration/Integration.hpp` and `Integration.cpp`. The new suite `src/tests/integration/test_IntegrationEdit.cpp` covers every desired-behavior bullet. There was no earlier WIP change to salvage.
Verified: `cmake --build _out/_cmake --config Release --target vsrIntegrationTests && ctest -C Release -R integration` → 100% passed (64 cases, 2243 assertions)
Holes filed: 61-edit-progress-during-hook, 62-edit-batch-policy-assumptions


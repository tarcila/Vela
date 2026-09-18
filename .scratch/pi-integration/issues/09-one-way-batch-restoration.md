# 09 — Add one-way Batch Restoration for external and local callers

**Status:** resolved
**Milestone:** 1
**Blocked by:** 07
**Files:** src/vsr/integration/restore/, src/tests/integration/test_IntegrationRestore.cpp, src/vsr/integration/edit/AgentEdit.hpp

Contract: `.scratch/pi-integration/spec.md` — Implementation Decisions §11 (one-way Batch Restoration), the local-authority rules in §4, and Behavioral acceptance coverage item 7. The spec is authoritative; read those sections before starting. Business behavior is tested at the public headless integration API (spec §Testing Decisions, "Confirmed primary seam"), with deterministic clients and a controlled monotonic clock.

## Current behavior

Committed batches keep restoration data (ticket 07), but nothing restores them.

## Desired behavior

A new suite `test_IntegrationRestore.cpp` shows:

- [x] External restoration selects a batch by `target_batch_ref` and carries its own `operation_ref`. Local Restore uses host authority and host-local correlation.
- [x] The same preparation, commit and synchronization rules as edits apply.
- [x] Any later parameter change on an affected object rejects with `conflict` and leaves eligibility unchanged. Other-object changes do not conflict.
- [x] Only the batch's parameters are restored, completely or not at all.
- [x] A successful commit consumes eligibility, even if synchronization then fails.
- [x] The same restore ID replays its outcome. A new ID against a consumed batch returns `already_restored`. An unknown, expired or wrong-launch reference returns `outcome_unavailable`.
- [x] A fresh session with current proof and read/edit permission can restore an older session's batch without the old secret or a prior inspection.
- [x] Two local callers racing for one batch cannot both consume it.
- [x] Local Restore still works after Disable agent edits and after Pi detaches, but never exceeds host mutation permission.
- [x] No redo, and no restore-of-restore.

## Out of scope

- Eviction and storage accounting (ticket 10).
- Viewer UI (ticket 17).

## Comments

### 2026-09-28 iteration — failed
Implementer resolved in isolation (commit ylxzvxzyrskm), but after landing alongside ticket 08 (wave member, same wave) the combined tree fails to compile: `src/vsr/integration/edit/AgentEdit.hpp` forwarders `markCommitted`/`synchronizeCommitted` added by this ticket index the interim `SyncStates` with `operator[]`, which ticket 08 replaced with `sync::SyncStates`. Files: was incomplete; add src/vsr/integration/edit/AgentEdit.hpp. WIP change ylxzvxzyrskm (salvage restore/, tests, ticket 73 file; re-express the forwarders against 08's `sync::SyncStates` API). Retry runs alone.

### 2026-09-28 iteration — resolved
I salvaged `restore/BatchRestore.hpp`, `test_IntegrationRestore.cpp` and ticket 73 from WIP ylxzvxzyrskm without changing their logic. The interim `markCommitted` forwarder was dropped. Restore now calls `EditService::syncStates().beginHook(scene)` (ticket 08's `sync::SyncStates`) after commit. `AgentEdit.hpp` gains one public forwarder, `synchronizeCommitted(SceneId)`, so the hook runs through the shared path.
Verified: `cmake --build _out/_cmake --config RelWithDebInfo --target vsrIntegrationTests && ctest --test-dir _out/_cmake -C RelWithDebInfo -R integration` → 100% passed (vsrIntegrationTests: 88 cases / 2779 assertions; all 8 restore scenarios pass when run individually).
Holes filed: 73-restore-policy-assumptions

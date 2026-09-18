# 73 — Batch Restoration policy assumptions (codes, consumption, sync hook)

**Status:** resolved
**Milestone:** 1
**Blocked by:**
**Files:** src/vsr/integration/restore/BatchRestore.hpp, src/vsr/integration/edit/AgentEdit.hpp

**Kind:** extends

Ticket 09 settled these points, which the spec leaves open:

1. **Preparation failure code.** An injected restore preparation failure ends the admitted operation as terminal `busy`, `committed=false`, eligibility unchanged. This mirrors ticket 62 §1. Revisit together with 62 if ticket 10 wants a distinct code.
2. **Missing target/incompatible reference.** A removed affected object, or a parameter that no longer exists, rejects with `conflict` (the single edit/restore precondition code). A batch named on a scene other than its own returns `outcome_unavailable`.
3. **Consumption record.** `RestoreService` keeps a launch-qualified set of consumed batch IDs and checks it before the store, so `already_restored` survives even after ticket 10 evicts the record's payload. The restoration payload is not yet released on commit. Ticket 10 owns releasing it and bounding the consumed set, and the `expired` reason. Until then, an evicted batch reports `outcome_unavailable`.
4. **Shared sync path.** To reuse the edit commit/hook logic without duplicating sync state, restore calls `EditService::syncStates().beginHook()` after commit (ticket 08's `sync::SyncStates`), and uses one public forwarder, `EditService::synchronizeCommitted(SceneId)`, to run the hook once. Revisit this if sync ownership moves out of `EditService`.
5. **Local canonical identity.** Local restores get a fresh `LocalRef` for each submission, so there is no local replay. Racing local callers are serialized by the engine: the first commit wins, and the second gets `already_restored`.

## Comments

### 2026-09-28 orchestrator
Resolved by addendum 03 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 139.

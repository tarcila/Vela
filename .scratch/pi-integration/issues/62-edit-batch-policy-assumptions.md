# 62 — Agent Edit Batch policy assumptions (codes, envelope, ownership)

**Status:** resolved
**Milestone:** 1
**Blocked by:**
**Files:** src/vsr/integration/edit/AgentEdit.hpp, src/vsr/integration/Integration.hpp, src/vsr/integration/Integration.cpp

**Kind:** extends

Ticket 07 settled these points, which the spec leaves open:

1. **Preparation failure code.** Injected preparation, allocation and reservation failures, a full restoration store, and batch-ID generation failure all end the admitted operation as terminal `busy` with `committed=false`. The spec freezes no code for this case. Revisit if ticket 10 wants a distinct "restoration allocation failure" code (spec coverage item 12).
2. **Value rejection code.** An invalid member value is detected at the safe point and returns `invalid_request`, with `failedEntry` and `ValueError`. A stale or removed target returns `conflict`. The ID is consumed.
3. **Envelope.** An empty batch, more than 64 entries, or a duplicate (object, parameter) pair rejects with `invalid_request` before admission, so no ID is consumed.
4. **Sync-state ownership.** `EditService` keeps a minimal per-scene state: a sticky `failed` flag and a count of pending hooks, reported as `ready`, `synchronizing` or `failed`. Ticket 08 owns `needs_sync`, Resynchronize (which clears `failed`) and the view-set rules, and may move this state into its sync module.
5. **Hook lookup.** A small read-only accessor, `Integration::normalSyncHook(SceneId)`, was added to `src/vsr/integration/Integration.hpp` and `src/vsr/integration/Integration.cpp`.
6. **Restoration store.** `RestorationStore` is a bounded map (default capacity 256) that keeps every issued batch_id in a never-reuse set. Ticket 10 owns eviction and byte accounting. Provenance fields exist but are not filled yet (ticket 10).
7. **Batch IDs.** Batch IDs are 16 bytes from `session::secureRandom`, hex-encoded, and checked against every ID issued before.

## Comments

### 2026-09-28 orchestrator
Resolved by addendum 03 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 139.

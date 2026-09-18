# 79 — Activity retention/lookup assumptions from ticket 10

**Status:** resolved
**Milestone:** 1
**Blocked by:**
**Files:** src/vsr/integration/activity/Activity.hpp, src/vsr/integration/operations/Operations.hpp, src/vsr/integration/edit/AgentEdit.hpp

**Kind:** extends

Ticket 10 settled these points, which the spec leaves open:

1. **Byte accounting unit.** Each activity entry costs a fixed 256-byte overhead plus its payload bytes (result, restoration payload, refs, client summary). Images count only against the 32 MiB image budget. Revisit if a precise allocator-level measure is wanted.
2. **Image eviction keeps the record.** When the image budget is full, the oldest safe record's image is dropped (`imageEvicted=true`) while its metadata stays; an image larger than the whole budget is not retained.
3. **Expiry of restorable batches keeps metadata.** Exceeding 32 restorable batches marks the oldest safe one `EXPIRED` and releases its restoration payload; the activity entry stays until entry/byte eviction. `RestorationStore` keeps a bounded (1,024) set of expired batch IDs so `expired()` stays answerable; restore reports `outcome_unavailable` either way.
4. **Foreign unknown IDs.** `OperationEngine::status` on another session's reference above its high-water returns `outcome_unavailable` (not `unknown`): that session has ended, so nothing of it can still be in transit.
5. **Own receipt with partial read.** For the caller's own record, missing read on any covered target yields only the minimal receipt (never a partial payload).

Revisit any of these if rejected; each is local to the named files.

### 2026-09-28 orchestrator
Resolved by addendum 03 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 139.

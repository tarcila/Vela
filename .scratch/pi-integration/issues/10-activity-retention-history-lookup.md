# 10 — Add the application-owned activity model, retention budgets and cross-session result lookup

**Status:** resolved
**Milestone:** 1
**Blocked by:** 03, 07, 09
**Files:** src/vsr/integration/activity/, src/tests/integration/test_IntegrationActivity.cpp

Contract: `.scratch/pi-integration/spec.md` — Implementation Decisions §4 (minimal owner receipts), §5 (activity/restoration storage, retained images, eviction), §9 (cross-session lookup and covered targets per operation kind), §13 (activity content), and Behavioral acceptance coverage item 12. The spec is authoritative; read those sections before starting. Business behavior is tested at the public headless integration API (spec §Testing Decisions, "Confirmed primary seam"), with deterministic clients and a controlled monotonic clock.

## Current behavior

Operations, edits and restorations produce results, but there is no bounded activity/history model, eviction or cross-session lookup.

## Desired behavior

A new suite `test_IntegrationActivity.cpp` shows:

- [x] A headless activity model, with no ImGui dependency, records ownership, targets, operations, outcomes and Batch References. It separates VSR-observed operations from client-supplied summaries.
- [x] Budgets: 16 MiB and 1,024 entries of activity plus 32 restorable batches, excluding image bytes; retained images get their own 32 MiB. Payload bytes are counted. The oldest safe records are evicted first, with visible loss of eligibility, and storage needed by active work is never evicted.
- [x] Minimal receipts for the caller's own work contain only allowlisted fields. Scene-bearing fields require read.
- [x] A new session with current proof can look up an older session's record by its complete `operation_ref`, but only with read on every covered target. Covered targets are fixed per operation kind. A multi-target record without whole-record permission is denied, never partially returned.
- [x] A missing or evicted record returns `outcome_unavailable`.
- [x] Records survive client disconnect but not restart.
- [x] Default and smaller configured limits are both tested.

## Out of scope

- The viewer window (ticket 17).
- Rendering (ticket 12), which only has to store its images through this model.

## Comments

### 2026-09-28 iteration — resolved
Added headless `activity::ActivityLog` (src/vsr/integration/activity/Activity.hpp): VSR-observed entries vs attributed client summaries, default/configurable budgets (16 MiB, 1,024 entries, 32 restorable batches, 32 MiB images), payload-byte accounting, oldest-safe eviction with visible `EXPIRED`, active entries never evicted, allowlisted owner receipts, per-kind covered targets and whole-record cross-session lookup (`outcome_unavailable` when missing/evicted).
Paths outside Files touched (minimal wiring): src/vsr/integration/operations/Operations.hpp (`status()` now allows cross-session lookup with read on every covered target, render = view+scene; replaces the ticket-37 `forbidden` stub), src/vsr/integration/edit/AgentEdit.hpp (`RestorationStore` default 32, evicts oldest instead of rejecting, `expired()`, `release()`), src/vsr/integration/restore/BatchRestore.hpp (commit releases the restored record), src/tests/integration/test_IntegrationRestore.cpp (size expectation 2→1 after release; new expired-batch `outcome_unavailable` scenario).
Engine/edit records are not yet fed into the shared ActivityLog byte budget: filed 80.
Verified: `cmake --build _out/_cmake --config Release --target vsrIntegrationTests && _out/_cmake/Release/vsrIntegrationTests "[integration]"` → All tests passed (4114 assertions in 100 test cases); `ctest -C Release -R integration` → 100% passed
Holes filed: 79-activity-retention-assumptions (follow-up: 80-wire-activity-log-into-operations)

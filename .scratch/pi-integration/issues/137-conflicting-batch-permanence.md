# 137 — Restore conflict marks the batch CONFLICTING permanently

**Status:** resolved
**Milestone:** 1
**Blocked by:**
**Files:** src/vsr/integration/restore/BatchRestore.hpp, src/vsr/integration/activity/Activity.hpp

**Kind:** extends

Spec §11 says "Preparation/conflict failure changes neither values, versions,
nor original eligibility". It also says eligibility "distinguishes restored,
expired, and conflicting reasons". Ticket 121 asked for `markConflicting` on a
restore conflict.

Assumption (ticket 121): a version conflict is permanent because parameter
versions only advance; even a change back to the original value conflicts. So
the first `conflict` marks the log entry CONFLICTING, releases its payload and
restorable slot, and `RestoreService` reports `conflict` on every retry. No
value or version changes. Preparation failure (`busy`) still keeps
eligibility.

Revisit if rejected: keep the store payload and log ELIGIBLE on conflict, and
show CONFLICTING only as a derived display state (for example, recompute
`isCurrent` when the viewer renders it).

### 2026-09-28 orchestrator
Resolved by addendum 01 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): none (implemented behavior ratified).

# 117 — Expired restore target has no structured reason on the carrier

**Status:** resolved
**Milestone:** 1
**Blocked by:**
**Files:** src/vsr/integration/restore/BatchRestore.hpp, src/vsr/integration/mcp/README.md

**Kind:** extends

Spec §11 says unknown, expired and wrong-launch targets all report `outcome_unavailable`, yet also says restoration eligibility "distinguishes conflicting, consumed/restored and expired" reasons while metadata is retained. `RestorationStore::expired()` exists, but `RestoreService::commit` returns plain `outcome_unavailable` with no step result, so the MCP `vsr_restore` status cannot tell an expired batch from an unknown one (ticket 73 §3 left the expired reason to ticket 10).

Assumption for ticket 104: map as-is, so the code is `outcome_unavailable` with no reason. Revisit by adding a structured `reason: "expired"` to the restore step result when `expired(ref)` holds. The frozen code does not change. Then document it in the README.

## Comments

### 2026-09-28 orchestrator
Resolved by addendum 04 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 141.

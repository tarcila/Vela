# 80 — Feed OperationEngine/EditService records into the ActivityLog budgets

**Status:** wontfix
**Milestone:** 1
**Blocked by:** 10
**Files:** src/vsr/integration/operations/Operations.hpp, src/vsr/integration/edit/AgentEdit.hpp, src/vsr/integration/restore/BatchRestore.hpp, src/vsr/integration/activity/Activity.hpp

Ticket 10 added `activity::ActivityLog` (16 MiB / 1,024 entries / 32 restorable batches, 32 MiB images, oldest-safe eviction, cross-session lookup) and the cross-session read check in `OperationEngine::status`. The engine still keeps its own count-capped status store (1,024 records, no byte accounting), and `RestorationStore` is count-capped at 32 without byte accounting. Neither yet appends to a shared `ActivityLog`.

## Desired behavior

- The engine/edit/restore services append and update `ActivityLog` entries (kind, covered targets, progress, outcome, Batch Reference, client summary kept separate) so one application-owned log carries the combined byte budget.
- Admission rejects busy when request/status storage cannot be reserved; an admitted edit that cannot reserve restoration storage rejects before commit (spec §5).
- Restoration commit/conflict/expiry update the entry's `Eligibility` visibly.
- Tests at the public headless API show one combined budget across operation results and restoration payloads.

## Comments

### 2026-09-28 iteration — wontfix
Split into 119, 120, 121: engine sink + busy admission, edit restoration byte
budget, restore eligibility transitions. Four headers, a new cross-header seam
(Activity.hpp includes Operations.hpp, so the engine needs an abstract sink) and
three test groups don't fit one 20-minute sitting. Parts chain 119 → 120 → 121.
Verified: n/a (no code changed)
Holes filed: none

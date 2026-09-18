# 120 — Edit restoration payloads charged to the ActivityLog budget

**Status:** resolved
**Milestone:** 1
**Blocked by:** 119
**Files:** src/vsr/integration/edit/AgentEdit.hpp, src/vsr/integration/operations/Operations.hpp, src/vsr/integration/activity/Activity.hpp

Part 2 of split ticket 80.

`RestorationStore` is count-capped (32) with no byte accounting. An admitted
edit must reserve restoration storage (bytes of its `Change` payload plus one
restorable-batch slot) in the same ActivityLog budget as operation records,
and reject `busy` before commit if it cannot (spec §5). On commit the edit's
entry gets `batchRef`, `restorationBytes`, `Eligibility::ELIGIBLE`.

Correlation hint: the executor does not know its engine entry id; either expose
it via `StepContext` (e.g. `activityId`) or have the sink attach batch data
through a Step field. Keep `RestorationStore` expiry in sync with ActivityLog
`EXPIRED` (the log's `makeRestorableRoom` is authoritative).

## Acceptance criteria

- [x] Headless test: one combined byte budget across operation results and
      restoration payloads (a large restoration payload evicts older terminal
      records / rejects busy before commit when it cannot fit).
- [x] Oldest eligible batch visibly becomes `EXPIRED` in the log past 32.
- [x] `vsrIntegrationTests "[integration]"` green.

## Comments

### 2026-09-28 iteration — resolved
`StepContext` gained reserve/cancel/publishRestoration + restorationRetained,
routed under the engine lock to new `ActivitySink` hooks (default no-op) and
to `ActivityLog::reserveRestoration/cancelRestoration/publishRestoration`.
`EditService::commit` charges `restorationBytes(record)` + one restorable slot
before mutating (busy otherwise), publishes on commit (`batchRef`,
`restorationBytes`, `ELIGIBLE`), then expires `RestorationStore` records the
log no longer holds ELIGIBLE (log is authoritative). Also fixed a latent
alias bug in store eviction. Tests added in src/tests/integration/test_IntegrationEdit.cpp.
Verified: `cmake --build _out/_cmake --config Release --target vsrIntegrationTests && _out/_cmake/Release/vsrIntegrationTests "[integration]"` → All tests passed (135 test cases), 4 runs
Holes filed: none

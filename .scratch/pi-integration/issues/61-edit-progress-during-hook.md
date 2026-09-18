# 61 — Report `synchronizing` progress while the normal-sync hook step executes

**Status:** resolved
**Milestone:** 1
**Blocked by:**
**Files:** src/vsr/integration/operations/Operations.hpp

**Kind:** extends

Ticket 07 splits an edit into a commit step and a hook step: the commit step returns `Step::Status::SYNCHRONIZING`, and the hook runs in the next safe-point step. Between the steps, status reports `synchronizing` + `committed`. While the hook step itself executes, `OperationEngine::runStep` sets the phase to `RUNNING`, so a status lookup made from inside the hook reports `running`, though still `committed` and non-terminal.

Assumption: this is acceptable for v1, because `committed` is already true and no non-commit can be reported. If the spec's "while the hook runs, committed/synchronizing is progress" requires the literal `synchronizing` name during the hook, `runStep` should keep `Phase::SYNCHRONIZING` for records that are already committed. That is a one-line change in the operations module (ticket 03's scope), and the check in `test_IntegrationEdit.cpp` ("Commit reports synchronizing…") should then assert `Progress::SYNCHRONIZING`.

## Comments

### 2026-09-28 orchestrator
Resolved by addendum 03 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 139.

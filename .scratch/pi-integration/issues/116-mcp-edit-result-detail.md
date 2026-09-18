# 116 — MCP edit terminal result carries only batch_ref

**Status:** resolved
**Milestone:** 1
**Blocked by:**
**Files:** src/vsr/integration/edit/AgentEdit.hpp, src/vsr/integration/mcp/tools/EditTools.hpp, src/vsr/integration/mcp/README.md

**Kind:** extends

`EditService` sets the engine step result only on commit, and only to `{"batch_ref":…}`. `EditResult` also holds `failedEntry`/`valueError` (value rejection), resulting `versions` and `sceneRevision`, but none reaches `operations::Status::result`, so `vsr_edit` via `vsr_operation_status` cannot report them. Ticket 104 mapped the headless behavior as-is and did not change the headless module.

Assumption: a client re-inspects to learn new versions and reads the frozen `invalid_request` outcome for a rejected value. Revisit: have `AgentEdit.hpp` put `failed_entry`, `value_error`, `versions` and `scene_revision` in the step result JSON (spec says finer structured reasons may be added), then update the README table.

## Comments

### 2026-09-28 orchestrator
Resolved by addendum 04 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 141.

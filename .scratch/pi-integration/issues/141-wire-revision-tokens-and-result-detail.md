# 141 — Put scene/view revision tokens and edit/restore result detail on the wire

**Status:** resolved
**Milestone:** 1
**Blocked by:** 140
**Files:** src/vsr/integration/inspection/, src/vsr/integration/edit/AgentEdit.hpp, src/vsr/integration/restore/BatchRestore.hpp, src/vsr/integration/mcp/tools/, src/vsr/integration/mcp/README.md, src/tests/integration/test_IntegrationMcpTools.cpp, src/tests/integration/test_IntegrationEdit.cpp, src/tests/integration/test_IntegrationRestore.cpp

Contract: `.scratch/pi-integration/spec.md` plus addendum 04 (items 6–8). The spec and the addenda are authoritative; read them first. Business behavior is tested at the public headless integration API, with deterministic clients and a controlled clock.

## Current behavior

Current behavior:
- Inspection returns only the cursor revision.
- No operation returns a view's revision.
- Edit results carry only `batch_ref`.
- An expired restore returns bare `outcome_unavailable`.
- Agents cannot form a first render request from wire data.

## Desired behavior

- Inspection page results carry `scene_revision`, and discovery view items carry `view_revision`, in the existing `"<activation>.<nonce>.<domain>.<value>"` form.
- Edit and restore results carry `scene_revision`, `committed` and the new `versions`. Edit results also carry `batch_ref`. On value rejection they carry `failed_entry` and `value_error`.
- An expired restore target returns `outcome_unavailable` with `{reason:"expired"}`. Unknown and wrong-launch targets carry no reason.
- A loopback MCP test goes discover → inspect → render using only tokens obtained over the wire, with no host-side token lookup, and gets a PNG. It uses a real helide device.
- The README tables are updated.

## Out of scope

- New tools.
- Changes to token semantics.

## Comments

### 2026-09-29 iteration — resolved
Inspection pages now emit `scene_revision`; discovery view items emit `view_revision` via a new `Inspector::setViewRevisions(ViewLookup)` host hook (no token without it). Edit/restore terminal results carry `committed`, `scene_revision`, `versions` ([{handle, version}] in inspect form); edits keep `batch_ref`; value rejections emit `failed_entry`/`value_error`. Restore sets `RestoreResult::expired` (and `result {reason:"expired"}`) only for this launch's batches the store or log expired; unknown/wrong-launch carry no result. New loopback scenario "MCP discover, inspect, render using only wire tokens" renders a PNG on real helide from wire tokens only. README tables updated; holes 116–118 notes retired.
Note: the discovery page `revision`/cursor does not cover `view_revision` (documented in README); hosts must call `setViewRevisions` when they build an Inspector.
Verified: `cmake --build _out/_cmake --config Release --target vsrIntegrationTests && ./_out/_cmake/Release/vsrIntegrationTests "[integration]"` → All tests passed (5819 assertions in 145 test cases); new scenarios pass.
Holes filed: none


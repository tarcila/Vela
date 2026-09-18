# 135 — vsrViewer's MCP server exposes no tools

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/apps/interactive/viewer/vsrViewer.cpp

**Kind:** extends

On 2026-09-28, while working ticket 18, `vsrViewer --agentIntegration` published a descriptor and answered `initialize`. Its `tools/list` returned `{"tools":[]}`. `setupIntegration()` builds an `mcp::Config` but never calls `addQueryTools`, `addLookupTools`, `addEditTools` or `addRenderTools` (tickets 103–105). It also registers no view (ticket 110). As a result, the real Pi gateway connects but has nothing to call, and no end-to-end workflow can run.

The spec does not say which application hosts gate 5. The viewer is the natural candidate because the headless tutorial host is read-only and has no view (ticket 85). This ticket was not fixed under 18 for two reasons: `vsrViewer.cpp` is being edited concurrently by ticket 122, and wiring the tools needs the view/render wiring decided in ticket 110.

Needed: register all four tool families in the viewer (or another chosen host), plus at least one renderable view backed by a real `helide` device.

### 2026-09-28 orchestrator
Resolved by addendum 05 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 143.

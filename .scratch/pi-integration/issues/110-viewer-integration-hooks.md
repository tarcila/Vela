# 110 — vsrViewer integration: trivial sync hooks, no registered views, CLI opt-in

**Status:** resolved
**Milestone:** 1
**Blocked by:** 17
**Files:** src/apps/interactive/viewer/vsrViewer.cpp

**Kind:** extends

Ticket 17 wired the integration into vsrViewer behind `--agentIntegration`
(off by default; built only with `VSR_USE_INTEGRATION`). Assumptions made:

- The scene is registered with read/edit/resynchronize; `normalSyncHook` and
  `recoveryCallback` return `true` unconditionally, relying on the viewport
  RenderIndex following scene changes via the update delegate. A real hook
  should confirm the viewports' RenderIndex/Image Pipeline caught up.
- No views are registered and no `render::RenderService` is created, so the
  viewer grants no render capability (spec §12 viewer parity for rendering is
  therefore not yet wired).
- Nothing appends to the `ActivityLog` yet (ticket 80), so the window's
  activity table stays empty in the live viewer until 80 lands; the model is
  exercised headlessly in `test_IntegrationViewerModel.cpp`.
- The descriptor path is shown only when built with `VSR_USE_INTEGRATION_MCP`
  (published via `McpServer::start` into the OS runtime directory).

Revisit if the spec wants a different opt-in, real hook confirmation, or
render parity in the viewer before ticket 18.

### 2026-09-28 orchestrator
Resolved by addendum 05 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 143.

# 16 — Add the standard Context adapter, viewer execution hook and a minimal headless host

**Status:** resolved
**Milestone:** 1
**Blocked by:** 03, 08
**Files:** src/vsr/app/Context.h, src/vsr/app/Context.cpp, src/vsr/integration/context/, src/apps/tutorial/, src/tests/integration/test_IntegrationContext.cpp

Contract: `.scratch/pi-integration/spec.md` — Implementation Decisions §1 (Context adapter, viewer execution hook, headless service loop; Context does not own the Image Pipeline or RenderIndex) and Behavioral acceptance coverage item 1 (Context versus custom host). The spec is authoritative; read those sections before starting. Business behavior is tested at the public headless integration API (spec §Testing Decisions, "Confirmed primary seam"), with deterministic clients and a controlled monotonic clock.

## Current behavior

Ticket 01 provides raw host registration only; applications using the standard Context must write their own glue.

## Desired behavior

- A convenience adapter registers a Context's scene. Views are still registered separately, and rendering support is never inferred from Context alone.
- A viewer execution hook services integration work at the viewer's safe point.
- A new minimal headless tutorial host builds the procedural fixture, activates the integration, publishes the descriptor path, and runs the service loop.
- Tests show that a Context host and a custom host behave the same, and that scene access happens only at safe points.

## Out of scope

- The viewer window UI (ticket 17).
- Context ownership of the Image Pipeline or RenderIndex.

## Comments

### 2026-09-28 iteration — resolved
Added header-only `src/vsr/integration/context/ContextAdapter.hpp`: `registerContextScene()` (scene only, no views/render inferred), `ViewerExecutionHook` (services queued work at the viewer safe point, inert while inactive) and `HeadlessServiceLoop` (blocking loop with a signal-safe stop flag). Context.h/.cpp left untouched so vsr_app stays integration-free. New `vsrTutorialIntegrationHeadless` (built only with VSR_USE_INTEGRATION) builds the procedural fixture in a Context, activates and services until SIGINT; descriptor publication waits for ticket 14 and the host says so rather than advertising a non-listening endpoint. `test_IntegrationContext.cpp` shows Context vs custom host parity and safe-point-only scene access.
Verified: `cmake --build _out/_cmake --config Release --target vsrIntegrationTests vsrTutorialIntegrationHeadless` → ok; `_out/_cmake/Release/vsrIntegrationTests "[integration]"` → All tests passed (2834 assertions in 92 test cases); new scenarios run 20× → all pass; `vsrTutorialIntegrationHeadless --exit-after-activation` → rc 0, and SIGINT after 1 s → "integration stopped", rc 0.
Holes filed: 85-headless-host-assumptions

# 85 — Headless tutorial host and Context adapter assumptions

**Status:** resolved
**Milestone:** 1
**Blocked by:** 14
**Files:** src/apps/tutorial/vsrTutorialIntegrationHeadless.cpp, src/vsr/integration/context/ContextAdapter.hpp

**Kind:** extends

Ticket 16 settled these points, which spec §1 leaves open:

1. **Adapter does not touch `vsr::app::Context`.** `registerContextScene()` is a header-only template over any type exposing `ctx.vsr.scene`, so `vsr_app` stays free of the integration (builds with `VSR_USE_INTEGRATION=OFF`) and `vsr_integration` does not link `vsr_app`. Revisit if a Context member (e.g. an owned `Integration`) is wanted.
2. **Descriptor publication deferred.** `vsrTutorialIntegrationHeadless` prints `connection descriptor: not published` because no loopback listener exists yet. Publishing a descriptor would advertise an endpoint that is not listening. When ticket 14 lands, have the host start the listener, publish the descriptor and print only its path (spec story 9).
3. **Tutorial exposes read only.** The host owns no Image Pipeline or RenderIndex, so it registers no view, grants no render, and grants no edit (edit needs a normal-synchronization hook confirming downstream state). The end-to-end workflow (ticket 18) needs edit, render and restore, so extend the host with a helide pipeline, one fixed-camera view, and sync hooks. The fixture is also approximate: `generate_icosphere(withFloor)` + `generate_default_lights`, not yet checked against the spec §"sphere above a ground plane, matte materials, one directional light, fixed camera".

### 2026-09-28 orchestrator
Resolved by addendum 02 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 145.

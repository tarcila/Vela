# 10 — Resolve barney WORLD property correctness and servicing qualification

**Status:** wontfix
**Milestone:** 1
**Blocked by:** none
**Specification:** ../addenda/02-viewport-world-bounds.md
**Kind:** contradicts
**Files:** external barney driver qualification / host source exposure decision (not an authorization to edit drivers)

## Load-bearing blocker

Ticket 09 requires real barney AND Helide world queries returning independently correct transformed boxes, without servicing stalls, using the existing Frame All ANARI_WAIT semantics. The first real-Scene/RenderIndex/provider experiment cannot establish that contract for the current barney implementation.

The fixture creates one radius-1 sphere instanced by a one-element transform array at (3,4,5), populates the RenderIndex once during fixture setup, then observes the WORLD without ever creating a camera/frame/renderer or rendering. Helide reports literal [2,3,4]–[4,5,6]. An ordinary notified transform-array edit to (50,4,5) produces [49,3,4]–[51,5,6] on Helide. Barney reports [-1,-1,-1]–[1,1,1] both before and after, including repeated queries. Its first ANARI_WAIT query takes **5237.357 ms** on the actual loaded default barney device (CUDA device 0). This exceeds the entire five-second admission-to-completion allowance, with no way for that allowance to interrupt the driver call. Later warm calls do not repair the transformed box.

Evidence: `../logs/09-green.log` (2 cases, 294 assertions, 29 failures; 28 wrong-box checks and one latency check), with own-tree enabled build/install logs `09-green-build.log`/`09-green-install.log`. Source fixture: `src/tests/integration/test_IntegrationInspection.cpp`, hidden opt-in `[.world-bounds-qualification]`. It is deliberately NOT part of ordinary passing regression evidence; run it explicitly to reproduce qualification. The initial 5237 ms is cold-query evidence, not a universal latency claim, not measurement of every naturally warm production viewport, and not proof of a specific GPU build mechanism.

## Safe partial disposition

Production inspection does NOT query barney: it returns unavailable/unqualified_source. The status-preserving raw helper and optional provider increment are WIP, not a completed two-device feature. No CPU scanner, forced render, NO_WAIT substitution, deadline raise, unbounded async worker, or driver fix is authorized by this ticket. Historical missing-binding hosts remain unchanged.

## Decision needed

Determine whether a genuinely qualified existing-world strategy exists for current barney (including normal live viewport/agent edits, correct transformed bounds, cold/readiness transition and finite owning-thread servicing), or explicitly authorize an external driver fix / narrower rollout. A natural first frame may matter for cold latency, but cannot be assumed to fix transformed bounds. Do not enable production barney based solely on warm timing, a NO_WAIT flag or post-call deadline check.

## Acceptance

- [ ] Explain and remedy/qualify transformed WORLD box correctness with independent literal primitive/instance/transform evidence.
- [ ] Qualify actual current production barney source-world calls and natural update behavior, including first readiness and owner-thread heartbeats, without inspection-induced render/reset/index population.
- [ ] Preserve finite five-second queue-inclusive operation contract; blocking ANARI_WAIT is not interruptible.
- [ ] Resume ticket 09's remaining source/lifecycle/MCP/runtime acceptance only after the load-bearing decision.

## Comments

2026-10-05: Filed by ticket 09 executor after real external-boundary failure; this is not a helper-only decomposition or missing independent-review-route hole.

## User-directed disposition

Barney investigation and repair are explicitly out of this work. Existing failure evidence remains valid and is NOT marked fixed. See `.scratch/barney-world-bounds-handoff.md` in the integration workspace and addenda/03-bounds-device-scope-and-issue-policy.md. This record no longer blocks usable bounds on Helide/VisRTX. No external driver change is authorized.

# 06 — Progress capture quality without hiding work

**What to build:** Deliver qualified progressive samples/frames with truthful counters and explicit denoise support while the host remains responsive and abandonment remains safe.

**Blocked by:** 04
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decisions 4, 6–8; Testing Decisions 4–8.

## Acceptance criteria

- [ ] Advertise supported samples/frames units and count limits. Completion uses qualified actual evidence, not pumps/render invocations; reported frames and confirmed samples stay distinct.
- [ ] Correctly reset owned accumulation and progress across setup/render/readback/encoding/drain without mutating source accumulation or using an N-frame blocking loop.
- [ ] Requested denoise is demonstrably applied on supported bindings or rejected before setup; no ignored option or falsequality claim.
- [ ] Progressive revision/readiness/authority checks, cancellation/deadline/expiry, terminal-before-cleanup and busy reservations remain correct at every phase.
- [ ] At the narrow image size qualify up to256 samples, remeasure latency/memory and expose only proven limits; full target size is jointly qualified in09/10.
- [ ] Public controlled/real CPU backend and thin MCP tests prove counters, fresh samples, supersession/cancel/replay/fault behavior and source invariants.

## Out of scope

Larger dimensions, arbitrary renderer editing, automatic convergence, new denoiser dependencies without demonstrated support and persistent camera state.

## Comments

# 04 — Deliver a qualified small CPU capture

**What to build:** Produce a real small image from an explicit world-space perspective camera through headless and MCP, using capture-owned resources and complete safety/lifecycle behavior from the first advertised capture.

**Blocked by:** 20
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decisions 1, 4–8; Testing Decisions 3–8 and 10.

## Current behavior

Legacy rendering is fixed-view/fixed-size. No capture operation or qualified transient backend exists; synchronous render-twice/wait-on-release cannot be reused unchanged.

## Acceptance criteria

- [ ] Add capture operation identity, vsr_capture and vsr_capture_image through the existing status/cancel/reference pattern, with explicit host exposure/read/render checks and additive UNAVAILABLE mapping preserving legacy codes.
- [ ] First policy is deliberately narrow: small fixed image (such as64×36), explicit direction/up/FOV camera and one confirmed sample/completed-frame quality. Advertise only actually qualified support.
- [ ] Validate finite representable camera/basis/FOV and echo effective camera/aspect. Capture owns camera/renderer/frame/pipeline; source scene/view/camera/renderer/dimensions/accumulation remain unchanged.
- [ ] Baseline capacity/resource/encoding/response/retention limits, revision/readiness checks, canonical request replay, queue deadline, permission withdrawal and no late success apply immediately; 07 expands limits rather than inventing safety later.
- [ ] Partial setup/start/readback/encoding faults, cancellation/disconnect/supersession and shutdown drain safely. Reservations/view capacity remain held while awaiting_cleanup; no hidden blocking destructor or force-free.
- [ ] Real CPU backend proves camera-dependent independently expected image regions, orientation/sRGB/alpha/ownership, baseline20ms servicing and100ms heartbeat gates and memory accounting before exposure.
- [ ] Controlled public backend tests cover pending/fault/delayed release and lifetime/capacity invariants; thin MCP proves admission/poll/image retrieval. Existing render/edit/restore remain valid.

## Out of scope

Large capture policy, progressive256 quality, alternate camera forms, persistent camera writes, server filesystem and unqualified backends.

## Comments

### Orchestrator dependency reconciliation after13

13 split into19→20. This capture path needs both the actual-binding observation
and validated policy/token contract; terminal20 now gates it. Capture safety
or exposure requirements are unchanged.

### Orchestrator dependency reconciliation

01 split into 13 (admitted headless/MCP host binding observation) and14 (real
viewer observation). This portable first CPU capture needs13's completed
contract, not a viewer implementation.08 separately waits for14 as well as04,
so actual viewport capture cannot bypass the original viewer obligations.

Do not split safety/cleanup out after shipping capture. If grain is too large, add a demoable read-only preparation/qualification precursor while leaving actual capture blocked until the complete narrow contract is safe.

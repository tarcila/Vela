# 05 — Aim with a target or matched view snapshot

**What to build:** Request a capture using eye/target or the actual registered view camera, with unambiguous units and effective-state reporting, without changing persistent camera state.

**Blocked by:** 04
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decision 4; Testing Decisions 3 and 6.

## Acceptance criteria

- [ ] Explicit camera accepts exactly one direction/target; view form snapshots only matched host state. Unsupported projection rejects rather than silently converts.
- [ ] Check finite/range, equal eye/target, zero/near-parallel basis before orthogonalization and after float32 conversion, and open FOV domain before/after rounding; no clamping/random basis.
- [ ] Normalize/orthogonalize with binary64 intermediates, convert degrees correctly, derive aspect from dimensions and echo actual camera.
- [ ] Canonical request identity/defaults include camera form/full inputs and revision dependencies; same-ID replay and changed-input conflicts remain sound.
- [ ] Headless/MCP tests use independent aiming/projection oracles and exact source/main-view invariants, including malformed/degenerate cases.

## Out of scope

Persistent view/Scene camera editing, arbitrary projection models, camera animation and capture quality expansion.

## Comments

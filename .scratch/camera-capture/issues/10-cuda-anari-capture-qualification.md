# 10 — Qualify the CUDA-backed ANARI capture binding

**What to build:** Verify real GPU-backed selected-view capture and GUI coexistence at target dimensions/quality, separately from CUDA algorithm parity evidence.

**Blocked by:** 05, 06, 07, 08
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decisions 5–8; Testing Decisions 6–8 and 10.

## Acceptance criteria

- [ ] The actual CUDA-backed ANARI binding produces independently correct camera-dependent images at 1280×720/256 supported samples with truthful provenance.
- [ ] Prove rendering/interop execution, not merely GPU availability, compilation, CUDA postprocessing, CPU helide or a silent fallback.
- [ ] Measure setup, submission, readback, cleanup, UI heartbeat/latency, peak memory and live-render coexistence; test cancellation, supersession, partial failures and drain.
- [ ] Source camera/manipulator/renderer/dimensions/accumulation remain unchanged; held capacities and owned references stay safe through scene changes/shutdown.
- [ ] If qualification fails, keep the binding unexposed with a clear technical blocker rather than weakening policy. File load-bearing holes for human decision where necessary.
- [ ] Existing GPU diagnostics and CPU/MCP regressions stay green, but are not substituted for this new backend evidence.

## Out of scope

GPU CI provisioning, qualifying every device, automatic independent-device/process fallback, persistent view writes and unproven GPU capture claims.

## Comments

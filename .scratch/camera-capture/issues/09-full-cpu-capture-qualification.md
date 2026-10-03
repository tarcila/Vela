# 09 — Qualify the full CPU capture policy

**What to build:** Prove the complete camera/quality/dimension contract on the real CPU binding, including 1280×720 at 256 samples while live viewing stays responsive.

**Blocked by:** 05, 06, 07, 08
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decisions 4–8; Testing Decisions 5–8 and End-to-end acceptance.

## Acceptance criteria

- [ ] A real CPU device proves camera-dependent pixels, target dimensions and backend-confirmed 256 samples—not poll/frame substitution—with correct provenance.
- [ ] Measure setup, submit, poll, map, copy, encode and release at target policy, with 20ms servicing and 100ms heartbeat acceptance gates on controlled otherwise-idle workloads.
- [ ] Concurrent source-view rendering, scene updates, cancellation and faults preserve main state, supersede correctly and retain resources until safe drain, including partial setup/shutdown.
- [ ] Measured peak allocations, transport and retention fit quoted policy. No false hard bound, backend fallback, silent quality reduction or forced free.
- [ ] Advertise only proven policy on this binding; unsupported configuration stays explicitly unavailable. Existing regressions and disabled-feature configurations remain valid.
- [ ] Record exact real-backend commands, results and limitations under isolated user/display state. Fake-adapter success alone is not qualification.

## Out of scope

CUDA qualification, cross-device pixel parity, OS-wide real-time guarantees, new permissions and unqualified blocking wrappers.

## Comments

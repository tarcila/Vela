# 13 — Inspect registered host bindings headlessly and through MCP

**What to build:** A complete admitted read-only view observation through the confirmed public headless seam and thin MCP adapter, using an explicitly supplied host binding rather than an arbitrary Scene camera.

**Blocked by:** none
**Status:** wontfix
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decisions 1–2; Testing Decisions 1 and 10.
**Files:** src/vsr/integration/Integration.hpp, src/vsr/integration/Integration.cpp, src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/identity/Identity.hpp, src/vsr/integration/mcp/tools/QueryTools.hpp, src/vsr/integration/inspection/ViewInfo.hpp (new if needed), src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationMcpTools.cpp, src/tests/integration/test_IntegrationIdentity.cpp, src/tests/integration/CMakeLists.txt

## Current behavior

Split from 01. ViewRegistration exposes identity and grants only. Inspector accepts a ViewRevisionTracker lookup for discovery, but has no complete binding observation. QueryTools exposes discovery and inspection, not vsr_view_info. ViewRevisionTracker already covers camera/renderer/dimensions/pipeline; reuse its contract rather than inventing an unrelated revision source.

## Acceptance criteria

- [ ] Add admitted vsr_view_info with request_id and view_id through the public headless interface and thin MCP tool. Results identify the registered scene/view and matched scene/view/capture-policy revisions.
- [ ] A host-owned safe-point binding supplies the actual effective world-space camera/projection, Device Identifier, renderer subtype, bounded renderer settings and Image Pipeline provenance/configuration revisions. Include Scene camera/renderer references only when actually bound, as opaque identities, never raw handles or addresses.
- [ ] Describe camera units explicitly (world space, perspective FOV degrees, aspect derived from dimensions). Other projections may be described read-only; missing correctness fields and optional device-private information have explicit unavailable/unknown reasons rather than fabricated defaults.
- [ ] Return validated policy metadata: supported quality units/counts, dimensions/resource/transport/deadline limits and capture exposure/reason. Defaults are unexposed; metadata registration alone does not enable capture or promise a qualified backend. Authorized discovery/tool metadata advertises the observation/policy without changing the private launch descriptor.
- [ ] Configuration/returned-state changes invalidate appropriate view observations and policy changes invalidate old policy tokens. Preserve current all-target scene/view read authority, owner receipts without payload after revocation, admission/replay/request conflict, bounded result retention, and legacy discovery/render arguments and behavior.
- [ ] Public headless tests with real Scenes and explicit host adapters prove the effective binding differs from an unrelated Scene camera, configuration and policy changes are visible, inspection runs only at the safe point with no renderer allocation/GPU work, missing binding is truthful, and oversized representations do not leak or become falsely complete.
- [ ] Thin MCP tests prove admission/poll/result shape, current read denial, replay and bounded representations. Run full existing integration regressions after building/installing the enabled CUDA/UI/network/Lua/SDL3 configuration in existing _out/_cmake; record exact commands and case counts. Compile-only evidence is not capture qualification.

## Out of scope

Viewer registration/snapshot implementation (14), capture execution or exposure qualification, bounds, camera writes, device fallback, server file access and architectural cleanup. Do not add capture tools or advertise qualified support from controllable adapter tests.

## Comments

Created by the 01 grain decision. This is a vertical read-only deliverable: an explicitly registered host can be inspected through both public headless and MCP interfaces. The subsequent viewer adapter uses this contract; no UI/Pi/Lua dependencies belong in it. The public headless seam is already user-confirmed; use TDD one behavior at a time.

### 2026-10-03 iteration — wontfix

split into 19, 20. Source inspection confirms this ticket still requires both a complete effective-binding observation and configurable validated policy observation; implementing and positively verifying both within the 20-minute sitting is too large. No source changes or TDD implementation started.
19 delivers admitted headless/MCP effective-binding inspection with fixed truthful unexposed defaults; 20 delivers validated host policy updates and observable policy-token/limit changes through the same interfaces. Both include their own behavioral tests, authority/replay/bounds checks and full enabled-build regression obligations; neither enables capture. Together they preserve all 13 acceptance obligations.
Existing 04 and 14 dependencies on 13 must be reconciled to both replacement children by the orchestrator; no sibling, spec, addendum, user configuration or artifact was changed. Prior work contained ticket splits only for this capability; no WIP retry was supplied.
Verified: `jj file track --include-ignored .scratch/camera-capture/issues/19-effective-binding-observation.md .scratch/camera-capture/issues/20-configurable-policy-observation.md` plus Python split-contract assertions and `jj diff --name-only` → three intended ticket paths only; statuses, milestone, Files subsets, vertical acceptance lists and 19→20 edge pass. Documentation-only split: no build/runtime or positive implementation test claim.
Holes filed: none

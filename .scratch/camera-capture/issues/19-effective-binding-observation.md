# 19 — Inspect the effective binding with capture closed

**What to build:** An admitted read-only observation of an explicitly registered host view through the public headless interface and thin MCP tool, with a fixed truthful unexposed capture policy.

**Blocked by:** none
**Status:** resolved
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decisions 1–2 and default policy values in 5; Testing Decisions 1 and 10.
**Files:** src/vsr/integration/Integration.hpp, src/vsr/integration/Integration.cpp, src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/identity/Identity.hpp, src/vsr/integration/mcp/tools/QueryTools.hpp, src/vsr/integration/inspection/ViewInfo.hpp (new if needed), src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationMcpTools.cpp, src/tests/integration/test_IntegrationIdentity.cpp, src/tests/integration/CMakeLists.txt

## Current behavior

Split from 13 before source changes. ViewRegistration has only name, scene and grants. Inspector has a ViewRevisionTracker lookup for discovery, but no binding observation or vsr_view_info; QueryTools only adds discovery/inspection. ViewRevisionTracker covers bound Scene camera/renderer versions, dimensions and explicit pipeline-change notifications, not a complete effective host snapshot or capture-policy token.

## Acceptance criteria

- [x] Add admitted vsr_view_info with request_id and view_id through the confirmed public headless seam and thin MCP tool. Observe only at the host safe point. Results identify the registered scene/view and matched scene/view/default capture-policy revisions.
- [x] An explicit host-owned adapter supplies the actual effective world-space camera/projection, dimensions, actual Device Identifier, renderer subtype, bounded complete effective renderer settings and Image Pipeline provenance/configuration revisions. Include opaque Scene camera/renderer references only when genuinely bound; never export raw handles, addresses or credentials, or guess from an unrelated Scene object.
- [x] Describe world-space position/orientation, perspective FOV degrees and dimension-derived aspect. Other projections may be described read-only. Missing correctness-bearing state is unavailable with a structured reason; optional device-private state is explicitly unknown with a reason. Inspection performs no renderer allocation, device query/download/wait, GPU work or persistent mutation.
- [x] Return the spec's fixed default limits (axis 2048, pixels 2073600, quality maximum 1024, deadline 120 seconds, PNG 8 MiB, response 12 MiB, per-view/global capacity and host/device budgets). Capture remains unexposed with a truthful unqualified/unavailable reason and no claimed deliverable quality units. Metadata alone grants no capture authority. No capture execution/tools are added. Configurable policy validation and quality declarations belong to 20.
- [x] Effective returned-state/configuration changes invalidate the appropriate view observations using ViewRevisionTracker's contract, including standalone host cameras, device/renderer/dimension/pipeline changes and genuine bound Scene parameter changes. The default policy token is stable while unchanged, scoped to the registered view/activation, and not interchangeable with scene/view tokens. Return no falsely matched mixed snapshot.
- [x] Authorized discovery and tool metadata advertise read-only binding observation and its fixed unexposed policy without modifying the private launch descriptor or legacy discovery/render arguments and behavior.
- [x] Preserve current all-target registered scene/view read authority, owner minimal receipts without camera/provenance payload after revocation, admission/poll/result/replay/request-ID conflict and bounded retained results. Oversized settings/provenance fail or explicitly become unavailable without leaking payload or appearing complete.
- [x] TDD at the confirmed public headless seam, with real Scenes and an explicit host adapter, proves effective binding differs from an unrelated Scene camera, changed configuration is visible, no callback runs before servicing, missing binding is truthful, revision changes and oversized representations are safe, and no inspection-driven renderer/GPU work occurs.
- [x] Thin MCP positive cases prove tool admission, polling, complete result shape, current scene/view read denial, retained identical replay and bounded representations. Build/install the existing enabled CUDA/UI/network/Lua/SDL3 configuration and run the full existing integration regressions; record exact commands and positive case counts. Compilation is not capture/backend qualification.

## Out of scope

Configurable capture-policy registration/updates and quality support declarations (20), viewer adapters (14), capture execution/exposure qualification (04), bounds, camera writes, fallback, filesystem authority and architectural cleanup. No source changes outside 13's original Files subset.

## Comments

### 2026-10-03 grain decision

First vertical replacement for 13: a host can inspect its real effective binding end to end through headless and MCP, with capture visibly closed. Implement API, behavior and tests together, one red-green public behavior at a time. 20 adds a separately observable workflow: registering/updating validated policy metadata and inspecting its changed revision and limits. Neither child may enable capture. Consumers originally blocked on 13 require both 19 and 20; sibling dependency edits are left to the orchestrator.

### 2026-10-03 iteration — resolved
Added value-only `ViewRegistration::observeBinding`, admitted `Inspector::makeViewInfo(sessionId, view)` and thin `vsr_view_info`; discovery/tool metadata advertise the fixed closed policy. Complete bounded renderer/pipeline settings, world camera and optional validated opaque bound references come only from the safe-point host adapter; missing/oversized/invalid state is explicitly unavailable.
The host supplies a distinct ViewRevisionTracker per view and reports every configuration change (including changes back between observations); query snapshots also invalidate changed returned state. Separate activation/view-scoped fixed policy domains remain stable. Callback-induced scene/view changes are rejected as superseded, and current all-target read checks suppress revoked payloads.
TDD first failed to compile for the absent public interface, then passed the effective-binding vertical slice. Final positive coverage: five new cases (three headless workflows, one public identity contract, one real loopback MCP workflow), 333 focused assertions; no renderer/device execution API or capture tool was added. Configurable policy, viewer adaptation and backend qualification remain 20/14/04; compilation is not qualification.
Verified: `cmake --build _out/_cmake -j 6` → full existing CUDA/UI/network/Lua/SDL3-enabled build passed; `cmake --install _out/_cmake --prefix "$PWD/_out"` → freshly installed before tests. `_out/_cmake/vsrIntegrationTests '[view-info]'` → five cases passed (333 assertions). `ctest --test-dir _out/_cmake -R '^vsr::integration$' --output-on-failure` → 1/1 integration suite passed. `_out/_cmake/vsrIntegrationTests` → all 268 cases passed (36374 assertions), including existing identity/edit/restore/render/synchronization regressions.
Verified: `clang-format --dry-run --Werror src/vsr/integration/Integration.hpp src/vsr/integration/Integration.cpp src/vsr/integration/identity/Identity.hpp src/vsr/integration/inspection/Inspection.hpp src/vsr/integration/inspection/ViewInfo.hpp src/vsr/integration/mcp/tools/QueryTools.hpp src/tests/integration/test_IntegrationInspection.cpp src/tests/integration/test_IntegrationMcpTools.cpp src/tests/integration/test_IntegrationIdentity.cpp` → passed. Bounded logs remain ignored under `.scratch/camera-capture-logs/19-*.log`; no image/backend qualification claim.
Holes filed: none

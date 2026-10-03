# 14 — Inspect the real viewer viewport and legacy agent binding

**What to build:** Register the selected interactive viewport for truthful read-only view inspection, alongside the explicitly distinct legacy agent binding, using 13's admitted headless/MCP contract.

**Blocked by:** 20
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decisions 1–2; Testing Decisions 1 and 10.
**Files:** src/apps/interactive/viewer/vsrViewer.cpp, src/vsr/ui/imgui/windows/Viewport.h, src/vsr/ui/imgui/windows/Viewport.cpp, src/vsr/ui/imgui/windows/BaseViewport.h, src/vsr/ui/imgui/windows/BaseViewport.cpp, src/tests/integration/test_IntegrationViewerModel.cpp, src/tests/integration/CMakeLists.txt

## Current behavior

Split from 01. vsrViewer registers only AgentView, a separately configured helide offscreen view. The interactive Viewport owns its actual Device Identifier, selected Scene camera/renderer, frame-bound previous camera/renderer, Image Pipeline and display settings. Those are not presently exposed as a complete safe-point snapshot. Selected state and state bound to the ANARI pass can temporarily differ; reporting selection alone is not evidence of an effective binding.

## Acceptance criteria

- [ ] Opted-in viewer registration exposes the selected viewport as a read-only inspectable binding through 13's vsr_view_info and authorized discovery. The legacy agent view remains separately identified, with its actual camera/device/renderer/pipeline provenance, never mislabeled as a viewport screenshot.
- [ ] Snapshot the actual effective host binding on the owning thread at a safe point, including truly bound camera/renderer references, explicit world-space projection/FOV/aspect/dimensions, actual Device Identifier, bounded complete effective renderer settings and applicable Image Pipeline settings/configuration revisions. Unknown or unavailable fields carry reasons, particularly during asynchronous device switching, teardown or binding changes.
- [ ] Device/camera/renderer/dimension/pipeline changes invalidate the appropriate observation revisions; policy changes invalidate policy tokens. Read inspection itself performs no GPU query/download/wait, renderer creation, scene mutation, camera reset, pipeline update or device selection/fallback.
- [ ] Capture remains explicitly unexposed pending later backend qualification, with a truthful reason and no supported-quality/runtime claims inferred from metadata or compilation. Preserve the legacy agent render path, dimensions, grants, images, synchronization and teardown behavior; the new read-only registration must not accidentally make every scene synchronization hook certify the viewport as render-ready.
- [ ] Automated checks observe the real viewer binding using owning-thread snapshots/receipts through its genuine public interface and the admitted headless/MCP observation path. Prove a bound versus unrelated Scene camera, changed camera/device/renderer/pipeline configuration, explicit agent/viewport difference, current read denial, bounded representations and no inspection-driven render allocation or persistent-state change.
- [ ] Build/install and run full existing regressions using existing _out/_cmake with CUDA/UI/network/Lua/SDL3 enabled. Record exact commands, test counts and any runtime limitations. Tests/logs use workspace-local .scratch/camera-capture-logs, never the user's config/artifacts; do not claim runtime viewport/capture qualification from a compile-only result.

## Out of scope

Capture execution, qualified selected-viewport capture adapter (08), bounds, persistent camera/renderer writes, device fallback changes, server filesystem, Studio protocols and architectural cleanup. No qualification or capture exposure is earned by this read-only adapter.

## Comments

### Orchestrator dependency reconciliation after13

13 split into19→20. This viewer adapter consumes the completed host observation
and validated policy/revision interface from20, not the closed split parent.
Its owning-thread real-binding and capture-closed requirements remain intact.

Created by the 01 grain decision. This is the second vertical deliverable: a real opted-in viewer can report its actual viewport and legacy agent configuration through the established observation tool. It is deliberately separate from 08, which supplies capture execution/ownership after the narrow qualified capture contract exists. Read 13's completed interface before implementing; do not introduce a private test seam or read live UI/Scene state from a transport thread.

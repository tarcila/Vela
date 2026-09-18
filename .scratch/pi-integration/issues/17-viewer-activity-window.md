# 17 — Add the vsrViewer activity/results window with local controls

**Status:** resolved
**Milestone:** 1
**Blocked by:** 02, 08, 09, 10, 16
**Files:** src/vsr/ui/imgui/windows/, src/vsr/ui/imgui/CMakeLists.txt, src/apps/interactive/viewer/vsrViewer.cpp, src/apps/interactive/viewer/CMakeLists.txt

Contract: `.scratch/pi-integration/spec.md` — Implementation Decisions §13 (activity and viewer feedback), the local-authority rules in §4, and the Viewer bullet of Testing Decisions §"Thin adapter checks and end-to-end acceptance". The spec is authoritative; read those sections before starting. Business behavior is tested at the public headless integration API (spec §Testing Decisions, "Confirmed primary seam"), with deterministic clients and a controlled monotonic clock.

## Current behavior

vsrViewer has no integration window.

## Desired behavior

- A dedicated window consumes ticket 10's activity model. It shows ownership, targets, operations, outcomes, Batch References, synchronization state, cleanup age and held capacity, and restoration eligibility. Client summaries are attributed separately.
- It provides these controls:
  - Connection information (the descriptor path, never the credential).
  - Disable agent edits.
  - Disconnect (local revocation).
  - Restore.
  - Resynchronize.
- The viewer stays interactive while work runs.
- A smoke test or a documented manual check covers edit-disable versus disconnect, restore after the client exits, and synchronization recovery.

## Out of scope

- Chat input, conversation mirroring and image comparison.
- Business rules, which stay in the headless suites.

## Comments

### 2026-09-28 iteration — resolved
Added `IntegrationActivity` window (vsr_ui_imgui, built only with VSR_USE_INTEGRATION) over an ImGui-free `IntegrationActivityModel` view-model: ownership, per-scene sync state and effective/local capabilities, activity rows (kind, owner ref, targets, progress/outcome with committed flag, Batch Reference, cleanup age/held slots, eligibility, attributed client summary), and controls: descriptor path (never credential) + copy, Disable agent edits, Disconnect (confirm, `revokeSession`), per-batch Restore (`submitLocal`), per-scene Resynchronize. vsrViewer opts in with `--agentIntegration`, sets up the services after scene load, and services work via `ViewerExecutionHook` in `uiFrameStart` so the UI stays interactive; with MCP on it publishes the descriptor. Headless smoke test `test_IntegrationViewerModel.cpp` covers edit-disable vs disconnect, local restore after the client exits, and resync recovery. No display here: window rendering is build-verified only; manual check = `vsrViewer --agentIntegration`, open "Agent Activity", toggle Disable agent edits (session stays), Disconnect (ownership clears), Restore a row, Resynchronize.
Verified: `cmake --build _out/_cmake --config RelWithDebInfo --target vsrViewer vsrIntegrationTests` (INTEGRATION=ON, MCP off and on) → OK; `_out/_cmake/RelWithDebInfo/vsrIntegrationTests "[integration]"` → All tests passed (4437 assertions in 115 test cases); `cmake -B _out/_cmake_off -DVSR_USE_INTEGRATION=OFF ... && cmake --build _out/_cmake_off --target vsrViewer` → links.
Holes filed: 109-activity-log-enumeration, 110-viewer-integration-hooks

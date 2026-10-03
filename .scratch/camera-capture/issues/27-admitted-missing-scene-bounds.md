# 27 — Query missing default-visible scene bounds

**What to build:** A complete public headless and thin MCP admitted scene-bounds query that reports a missing host value truthfully, with default-visible selection, revision provenance, bounded work and existing authority/lifecycle rules.

**Blocked by:** none
**Status:** resolved
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decision 3; Testing Decisions 2 and 10.
**Files:** src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/inspection/Bounds.hpp, src/vsr/integration/operations/Operations.hpp, src/vsr/integration/mcp/tools/QueryTools.hpp, src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationMcpTools.cpp, src/tests/integration/CMakeLists.txt

## Current behavior

Split from 25 before source work. Inspector supports admitted safe-point scene queries and makeViewInfo, with shared ScratchPool accounting. QueryTools forwards requests to OperationEngine. There is no Bounds.hpp, host bounds binding or vsr_bounds. SceneIdentity already exposes distinct launch/domain-qualified scene and inspection revisions. OperationEngine supplies admission/replay, cancellation, current read authority, queue-inclusive deadlines and bounded historical retention.

## Acceptance criteria

- [x] Add public headless bounds request construction and thin admitted MCP `vsr_bounds(request_id, scene_id, optional include_disabled)`. Omission and explicit false select active layers and effectively enabled contributors. Bind both the original optional choice and effective default to canonical identity under existing rules; explicitly test identical retries for each form and omission-versus-false conflict under the same request ID. Explicit true and any node_handle reject as unsupported before admission and ID consumption. Never silently ignore a node target.
- [x] At an owning-thread safe point return a bounded `unavailable` observation with machine-readable `missing_binding` reason, exact scene/target identity, current matched scene and inspection revisions, and explicit default-visible selection. Do not invent source provenance, min/max, a zero box, empty content, provider pending work or geometry scan counters. An empty real Scene without a binding is still missing, not evidence of a complete empty bounds value. No renderer readiness, driver bounds helper, GPU download, scene walk or renderer setup is required.
- [x] Define and validate fixed defaults before admission: 256 node/geometry steps and 4096 vertex values per slice, 4096 nodes and 1,048,576 vertex values per operation, five-second admission-to-completion deadline and at most 8MiB shared temporary bounds scratch. Value-only/missing observations do not claim scans. Bound query/result metadata and use the Inspector's shared accounting for actual temporary allocations; release any reservation on rejection, cancellation, timeout, failure or completion. Ticket 28 adds finite host-value storage, not a second scratch pool.
- [x] Preserve current scene read checks at execution, publication and retrieval. After read withdrawal, owner minimal receipts have no scene identity, revisions, selection or bounds payload. Reuse bounded admission/replay/conflicts, operation status/cancel, bounded retained results and queue-inclusive deadlines. Historical retries preserve original revisions rather than taking a new observation.
- [x] TDD public behavior with real Scenes: missing value on populated and empty scenes, exact default selection/provenance, unsupported true/node rejection without consuming IDs, replay/conflicts, cancellation before execution, queue deadline, read withdrawal before execution and after completion, bounded shared scratch exhaustion/release and historical replay after scene edits. Thin MCP proves tool exposure, schema/defaults, admitted polling, rejections, retries/conflicts and payload suppression. Use independently expected identities/revisions; no renderer or scanner qualification is inferred.
- [x] Build/install changed libraries in existing `_out/_cmake` and `_out` before tests; run positive new cases and existing integration/broad-editing regressions with CUDA/UI/network/Lua/SDL3 enabled. Record exact commands and case counts. No cold configure, CUDA disable, absent test match or success-with-skip claim.

## Out of scope

Host-owned complete value installation and available/empty/explicit-pending/provider-unavailable observations (28); selectable include-disabled/configurable policy (26); node targets (22); callbacks (23); analytic/triangle scans/unions; capture/device acquisition, edits and export. This is a truthful missing-value capability, not completion of aggregate 25 or initial portable geometry coverage.

## Comments

Created by the 25 grain decision. This is an externally observable query through both public interfaces, including authority, bounded execution and operation lifecycle—not a private schema or test-only layer. All Files are a subset of 25. Ticket 28 extends this same query to complete host values and carries every remaining original correctness guarantee. Aggregate dependents must wait for 28; do not expose a missing-only query as implemented host bounds.

### 2026-10-03 iteration — resolved
Added `Inspector::makeBounds` and thin admitted `vsr_bounds`: omission/false are distinct canonical inputs with the same default-visible selection; true, any node target and MCP limit overrides reject without consuming IDs.
The safe-point observation is only `unavailable` / `missing_binding`, with exact scene/target identity and matched scene/inspection tokens, no source, box, empty claim, pending work or scan counters. Fixed defaults are statically validated; bounded temporary metadata uses the existing shared scratch pool with exception-safe reservation ownership and all terminal/rejection releases.
Added 8 public headless/real-loopback cases covering empty and geometry-populated Scenes, safe-point provenance, replay/conflict/history eviction, cancellation/deadline, execution/publication/retrieval read checks and existing inspection scratch exhaustion. Red tests established missing API/tool, publication leakage after read withdrawal and ignored MCP overrides before green fixes; corrected an oracle that incorrectly assumed Scene had no default camera.
No host value, node bounds, provider, scan, renderer or runtime capture qualification is claimed; 28 remains the complete host-value extension. No spec/addenda/sibling/config/artifact changes; no retry WIP.
Verified: `cmake --build _out/_cmake -j 4` then `cmake --install _out/_cmake` → passed, full existing tree and fresh `_out` install; cached CUDA, interactive UI, networking, Lua, SDL3 and MCP all ON, no configure or fallback.
Verified: `_out/_cmake/vsrIntegrationTests '[bounds],[bounds-mcp]'` → 8 cases passed; `_out/_cmake/vsrIntegrationTests '[integration]'` → 276 cases passed, including broad-editing regressions (no skipped or absent matches).
Verified: `ctest --test-dir _out/_cmake --output-on-failure -R '^vsr::(integration|Scene|Geometry|Forest|RenderIndex|Algorithms)$'` → 6/6 tests passed; `clang-format --dry-run --Werror src/vsr/integration/inspection/{Bounds,Inspection}.hpp src/vsr/integration/mcp/tools/QueryTools.hpp src/tests/integration/test_Integration{Inspection,McpTools}.cpp` → passed. Logs remain ignored under `.scratch/camera-capture-logs/27-*.log`, not commit artifacts.
Holes filed: none


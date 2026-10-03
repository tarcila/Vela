# 20 — Inspect validated host policy updates with capture closed

**What to build:** Let an explicit host register and update validated capture-policy metadata and observe supported quality declarations, limits and policy revision changes through admitted headless/MCP view info and authorized discovery, without enabling capture.

**Blocked by:** 19, 24, 33
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decisions 1–2 and 5; Testing Decisions 1, 4 and 10.
**Files:** src/vsr/integration/Integration.hpp, src/vsr/integration/Integration.cpp, src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/identity/Identity.hpp, src/vsr/integration/mcp/tools/QueryTools.hpp, src/vsr/integration/inspection/ViewInfo.hpp (new if needed), src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationMcpTools.cpp, src/tests/integration/test_IntegrationIdentity.cpp, src/tests/integration/CMakeLists.txt

## Current behavior

Split from 13 before source changes. Implement after 19's completed admitted binding observation with a fixed unexposed policy. No capture execution or qualified backend is supplied by either child. Read 19's completed interface before implementation; do not create an independent revision or bypass the OperationEngine.

## Acceptance criteria

- [ ] A host can explicitly register/update validated read-only capture-policy metadata and retrieve it through 19's admitted vsr_view_info, including supported quality unit/count declarations, dimensions/pixel limits, capacity, host/device resource budgets, encoded PNG/complete transport bounds, deadline and exposure/reason. Omitted registration retains 19's unexposed defaults.
- [ ] Validate policy values and cross-field relationships with checked products/sums and base64/JSON/encoder overhead as specified. Reject invalid/overflowing/incompatible policy before installing it; a failed update leaves the previous policy and token intact. Public requests cannot override policy or supply devices. Backend-private estimates/unknowns remain distinguished from enforceable bounds.
- [ ] Quality declarations distinguish samples from completed frames, bound counts and describe support honestly. Declared metadata is not evidence of a deliverable backend contract: capture remains unexposed with an explicit qualification/backend-unavailable reason. No metadata registration/update, grants or controllable-adapter test can open capture or advertise a qualified backend. No capture tools are added.
- [ ] Accepted policy changes invalidate old capture-policy tokens; unchanged metadata does not invent freshness. Tokens remain activation/view-scoped and distinct from scene/view tokens. Effective binding/configuration changes still invalidate appropriate view observations. A returned observation carries a matched scene/view/policy snapshot; replay stays historical rather than relabeling old policy as current.
- [ ] Authorized discovery and view-info/tool metadata expose active validated policy information without changing the strict private descriptor, legacy discovery arguments, or legacy render dimensions/behavior. Bounded representation applies to quality lists and all new strings/settings; overflow is not silently truncated into complete support.
- [ ] Preserve all-target current scene/view read authority, owner receipts without camera/policy/provenance payload after read revocation, request admission/poll/result/replay/conflict and bounded retention. Policy withdrawal/update does not bypass current authority.
- [ ] Public headless positive tests with real Scenes and explicit host adapters demonstrate valid narrowed/raised compatible policy registration, changed and unchanged policy tokens, failed-update preservation, checked overflow/incompatible transport rejection, unit/count metadata, capture still closed, truthful unknowns and bounded lists. Exercise changed policy between admission and safe-point observation and historical replay.
- [ ] Thin MCP positive cases observe changed validated limits and policy tokens via admission/poll/result and authorized discovery, prove current read denial and replay, and reject or truthfully mark oversized representations. Build/install existing CUDA/UI/network/Lua/SDL3-enabled configuration and run full integration regressions with exact commands and positive case counts. No cold configure or silent CUDA disable; no backend/capture qualification claim.

## Out of scope

Effective-binding implementation already delivered by 19 except policy integration, viewer observation (14), capture execution/exposure qualification (04), resource reservation execution, bounds, persistent camera writes, fallback, server/client file saving, private descriptor and architectural cleanup. No source changes outside 13's original Files subset.

## Comments

### 2026-10-03 grain decision

Second vertical replacement for 13: a host registers/updates policy and a client sees validated active metadata and invalidated tokens through the same headless/MCP observation workflow. Implement schema, behavior and tests together, not as horizontal tickets. 19 supplies the useful prior read-only workflow with fixed unexposed defaults. Consumers originally blocked on 13 require completion of both children; sibling dependency edits are left to the orchestrator.

### 2026-10-03 iteration — ready-for-human
Read 19's completed interface and implemented a candidate public headless/thin-MCP policy workflow test-first (initial build failed for the absent API; candidate three policy cases passed). Before accepting it, inspection of the actual fixed-Huffman stb encoder disproved the candidate zlib-library bound. Filed blocking contradiction 24: the default full pixel envelope cannot have its conservative possible PNG output bounded by 8 MiB with this encoder; whether the cap is independently enforceable publication policy needs an authoritative decision.
Removed **all** candidate source/test changes rather than retain misleading validation. Existing 19 bindings/default closed policy and all grants/private descriptor/legacy rendering semantics remain unchanged. No acceptance boxes checked, metadata/backend qualification claimed, or capture opened. No retry WIP was supplied. Resume only after 24 resolves the compatibility rule; the token/update API candidate is not retained.
Verified: `c++ -std=c++17 -I src .scratch/camera-capture-logs/20-encoder-bound.cpp _out/_cmake/libvsr_integration.a _out/_cmake/libvela_ext_stb_image.a -o .scratch/camera-capture-logs/20-encoder-bound && .scratch/camera-capture-logs/20-encoder-bound` → exit 0, deterministic existing encoder produced 8,746,547 bytes at 1920×1080 versus default cap 8,388,608 (reproduction algorithm recorded in 24; no PNG file output).
Verified: `cmake --build _out/_cmake -j 6` → restored baseline full existing Release CUDA/UI/network/Lua/SDL3-enabled build passed; `cmake --install _out/_cmake --prefix "$PWD/_out"` → passed before relevant tests. `ctest --test-dir _out/_cmake -R '^vsr::integration$' --output-on-failure` → 1/1 passed. `_out/_cmake/vsrIntegrationTests '[view-info]'` → five positive cases passed (333 assertions); `_out/_cmake/vsrIntegrationTests` → all 268 cases passed (36290 assertions). Bounded ignored evidence remains under `.scratch/camera-capture-logs/20-*`; runtime backend/capture qualification was not attempted.
Holes filed: 24-default-policy-encoder-bound

### 2026-10-03 orchestrator — human decision applied
24's decision is resolved: qualify a bounded integration encoder without changing the default8MiB cap/envelope.33 is the implementation prerequisite; consume its proved authoritative bound rather than an assumed stb/zlib estimate. No candidate policy implementation was restored.32's fixed-limit consistency concern is folded into this workflow: active policy JSON, defaults and MCP descriptions must agree; avoid a separate constants cleanup detour. This repair execution targets33/34 only, not automatic resumption of20 or the entire capture graph.

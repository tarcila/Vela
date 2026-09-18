# 105 — MCP tools: render (image result) and resynchronize

**Status:** resolved
**Milestone:** 1
**Blocked by:** 08, 12, 103
**Files:** src/vsr/integration/mcp/tools/, src/tests/integration/test_IntegrationMcpTools.cpp, src/vsr/integration/mcp/README.md

Part 3 of ticket 15 (split). Contract: spec MCP bullet of Testing Decisions and §9 carrier mapping.

## Desired behavior

- Render tool returns the image as an MCP image content result (not a file path); dimensions are view-owned, with no request override accepted. `sync_required`, `busy` and dimension codes (ticket 91) mapped.
- Resynchronize tool returning a qualified `operation_ref`; sync states `ready`/`needs_sync`/`synchronizing`/`failed` reported.
- Tests need a real helide device as in `test_IntegrationRender.cpp`. Extend README mapping rows.

## Acceptance criteria

- [x] Render returns image content; request dimension overrides rejected/ignored per README (tested)
- [x] Resynchronize reachable over loopback with qualified ref

## Comments

### 2026-09-28 iteration — resolved
Added `tools/RenderTools.hpp` with three tools. `vsr_render` rejects `width`/`height` as `invalid_request` (dimensions are view-owned). `vsr_render_image` returns the status plus MCP image content, a base64 PNG. `vsr_resynchronize` returns a qualified `operation_ref`, and its result carries `{ready, state}`. The README gains a mapping section. Loopback tests use real helide and cover: PNG dimensions via IHDR, replay, superseded after a resize, sync_required, busy while held, failed and successful resync, and forbidden.
Verified: `cmake --build _out/_cmake --config RelWithDebInfo --target vsrIntegrationTests && _out/_cmake/RelWithDebInfo/vsrIntegrationTests "[integration]"` → All tests passed (5121 assertions in 130 test cases)
Holes filed: 118-wire-revision-tokens

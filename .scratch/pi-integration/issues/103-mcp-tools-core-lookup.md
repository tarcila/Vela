# 103 — MCP tools scaffold: discovery, inspection, status/result lookup, cancel, and code-mapping README

**Status:** resolved
**Milestone:** 1
**Blocked by:** 06, 10, 14
**Files:** src/vsr/integration/mcp/tools/, src/tests/integration/test_IntegrationMcpTools.cpp, src/vsr/integration/mcp/README.md

Part 1 of ticket 15 (split). Contract: `.scratch/pi-integration/spec.md` — MCP bullet of Testing Decisions and the carrier-mapping deliverable in §9; read ticket 15 for the full desired behavior.

## Desired behavior

- A `tools/` module that builds `mcp::Tool` entries (the `Config::tools` seam in `McpServer.hpp` already dispatches `tools/call`; only touch `McpServer.hpp` if a minimal hook is missing, and list it in a comment).
- Tools: discovery (exposed scenes/views/operations under current capabilities), inspection (paged, `cursor_stale`/`item_too_large` mapped), status/result lookup and cancel by qualified `operation_ref` (`session_id` + `request_id`), including cross-session payload gates and retained operation kind.
- `src/vsr/integration/mcp/README.md`: payload placement (structuredContent vs content vs error.data) and the mapping of every frozen code and progress state, commit/no-commit, and operation-kind-qualified `conflict`. Map codes from open assumption tickets 37, 55, 79, 91, 97 faithfully; file holes for gaps.
- Thin loopback tests for these tools' happy path, mapping, capability policy and cross-session gates.

## Acceptance criteria

- [x] Discovery, inspection, lookup and cancel tools reachable over real loopback
- [x] README documents placement and full code/state mapping
- [x] Loopback tests pass without parsing English

## Comments

### 2026-09-28 iteration — resolved
Added `mcp/tools/`. `ToolSupport.hpp` holds the shared argument parsing, status object and placement helpers. `QueryTools.hpp` adds `vsr_discover` and `vsr_inspect`, and `LookupTools.hpp` adds `vsr_operation_status` and `vsr_cancel`. Tickets 104 and 105 add sibling headers with `add*Tools(cfg.tools, …)`.
Minimal hook in `McpServer.hpp`: `ToolResult` gained `structuredJson` and `isError`, emitted as MCP `structuredContent` and `isError`.
`mcp/README.md` documents placement (error.data.code vs structuredContent vs content), every frozen code and progress state, commit/no-commit, and kind-qualified conflict. New `test_IntegrationMcpTools.cpp` runs four loopback scenarios.
Verified: `cmake --build _out/_cmake --config RelWithDebInfo --target vsrIntegrationTests && _out/_cmake/RelWithDebInfo/vsrIntegrationTests "[integration]"` → All tests passed (4740 assertions in 126 test cases)
Holes filed: 115-discovery-admission-target

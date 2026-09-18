# 225 — Confirm vsrMcpProxy's local error codes and default limits

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/apps/tools/McpProxy.hpp, src/apps/tools/README.md

**Kind:** extends

Ticket 215 made the proxy fail requests locally. The spec does not name codes for proxy-local transport failures, so 215 assumed these `error.data.code` values: frozen `timeout` (deadline), plus two new non-frozen names, `response_too_large` (byte cap) and `untrusted_endpoint` (the ownership refusal, internal only because the proxy exits). `unavailable` is used for unreachable, as before. JSON-RPC code -32603. Default limits: connect 5 s, response 30 s, cap 16 MiB. The spec lets the adapter define further carrier mappings (the "validated adapter … documents the HTTP/MCP carrier … mappings" clause), so this seemed in scope.

Revisit if rejected: change `detail::failureCode` and the README paragraph. If `unavailable` should cover every case, change the test expectations in `test_IntegrationMcpProxy.cpp`.

## Comments

### 2026-09-28 orchestrator
Resolved by addendum 09 (ratified as implemented). No follow-up.

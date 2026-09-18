# 144 — Add vsrMcpProxy, the trusted stdio adapter for Pi

**Status:** resolved
**Milestone:** 1
**Blocked by:** 141
**Files:** src/apps/tools/, src/tests/integration/test_IntegrationMcpProxy.cpp

Contract: `.scratch/pi-integration/spec.md` plus addendum 05 (items 1–3). The spec and the addenda are authoritative; read them first. Business behavior is tested at the public headless integration API, with deterministic clients and a controlled clock.

## Current behavior

Pi's MCP gateway cannot attach, hold the proof or renew independently (hole 136). No adapter exists.

## Desired behavior

- A new `vsrMcpProxy` executable exists in `src/apps/tools/`, built only with `VSR_USE_INTEGRATION_MCP`. It behaves exactly as addendum 05 item 2 specifies:
  - it runs as a stdio MCP server;
  - it validates the descriptor from its path argument or environment;
  - it holds the secret privately in memory;
  - it forwards JSON-RPC with the proof headers;
  - it renews on a timer independent of traffic;
  - it detaches on EOF or signal;
  - it never emits secrets.
- A new test drives the proxy over pipes against an in-process MCP server with a controlled lease. It shows:
  - `tools/list` passes through;
  - `vsr/*` methods are not exposed to the stdio client;
  - renewal keeps the session alive across more than 2 leases with no traffic;
  - EOF detaches;
  - no secret appears on stdout or stderr.
- Usage is documented in `src/apps/tools/README.md`.

## Out of scope

- Changing the server-side bootstrap methods.
- Pi extensions.

## Comments

### 2026-09-29 iteration — resolved
Added `vsrMcpProxy` (src/apps/tools/vsrMcpProxy.cpp + header-only `McpProxy.hpp`, built only with `VSR_USE_INTEGRATION_MCP`). It reads the descriptor through `descriptor::read`, attaches over `vsr/attach`, keeps the ID and secret in memory, forwards newline-delimited JSON-RPC with the proof headers, renews on a condition-variable timer, reattaches as a fresh session after a lost session, and detaches on EOF or a signal (self-pipe). The client's `vsr/*` calls are answered locally with -32601.
The test (`test_IntegrationMcpProxy.cpp`) runs `Proxy::run` on pipes against an in-process server with a real-time 0.6 s lease and 0.15 s renewal. It covers passthrough of initialize and tools/list, `vsr/*` hiding, an idle period of 2.5 leases, detach on EOF, detach on the wake fd, a fresh reattach after revocation, and absence of the credential or secret on stdout/stderr. Documented in `src/apps/tools/README.md`.
Verified: `_out/_cmake/Release/vsrIntegrationTests "[integration]"` → All tests passed (5827 assertions in 148 test cases). The proxy scenarios also passed 5 of 5 repeated runs.
Holes filed: none

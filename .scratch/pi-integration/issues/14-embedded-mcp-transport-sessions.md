# 14 — Select the MCP/HTTP/JSON stack and add the loopback transport with attach, renew and detach

**Status:** resolved
**Milestone:** 1
**Blocked by:** 02, 03, 13
**Files:** external/, src/vsr/integration/mcp/, src/tests/integration/test_IntegrationMcpTransport.cpp

Contract: `.scratch/pi-integration/spec.md` — Implementation Decisions §1 (the optional embedded server), §3 (headers, Host/Origin rules, single-family listener, direct-only transport), §14 technical validation gate 2, and Behavioral acceptance coverage item 13 (wire parts). The spec is authoritative; read those sections before starting. Business behavior is tested at the public headless integration API (spec §Testing Decisions, "Confirmed primary seam"), with deterministic clients and a controlled monotonic clock.

## Current behavior

No network endpoint exists. The existing `VSR_USE_NETWORKING` boost.asio channel is scene sync, not this integration.

## Desired behavior

- An optional build option adds the embedded MCP server. It is off by default, and unrelated builds gain no dependency. The chosen HTTP/JSON/MCP libraries are vendored under `external/` following existing `vela_*` conventions, with the choice and MCP protocol version recorded in a comment on this ticket.
- The listener binds only the descriptor's exact `127.0.0.1` or `::1` address: single family, IPv6-only set before binding, no wildcard. The descriptor is published only after the listener and auth are ready.
- Host must equal the exact authority; alternate families, aliases, mapped addresses and forwarded-host values are rejected. Origin-bearing requests are rejected. There are no redirects or proxies.
- The `Authorization`, `VSR-Session-ID` and `VSR-Session-Proof` headers are enforced. Attach delivers the session secret privately, never as model-visible tool output.
- Renew and detach go through the control path and stay serviceable while the scene pool is full.
- The frozen codes survive the carrier mapping, including `unauthenticated`, `invalid_session_proof` and `forbidden`.
- A new suite drives real loopback requests, and no secrets appear in logs.

## Out of scope

- Scene-operation tools (ticket 15).
- Any Pi-side adapter (ticket 18).
- Browser or remote access.

## Comments

### 2026-09-28 iteration — resolved
Stack choice: no vendored library. I hand-wrote a minimal HTTP/1.1 loopback server plus a strict JSON parser and writer, both header-only, in `src/vsr/integration/mcp/{McpServer,Json}.hpp`. This keeps the option dependency-free, so `external/` and THIRD-PARTY-NOTICES.md are unchanged. The server speaks MCP protocol version **2025-06-18** over streamable HTTP with JSON-RPC 2.0. Each POST to `/mcp` gets one `application/json` reply; there is no SSE GET stream.
The new CMake option `VSR_USE_INTEGRATION_MCP` is off by default and depends on VSR_USE_INTEGRATION. The listener binds a single family: `127.0.0.1` IPv4-only, or `::1` with IPV6_V6ONLY set and read back before bind, and the bound address is re-checked. The descriptor is published only after that check. The server enforces Host, forwarding headers, Origin, Authorization and the VSR-Session-* headers. Attach, renew and detach go through private `vsr/*` methods; renew and detach use the engine's control path. Frozen codes travel in `error.data.code`.
Shared CMake files edited (minimal): `CMakeLists.txt` (option) and `src/vsr/integration/CMakeLists.txt` (PUBLIC define). The test file compiles to nothing when the option is off.
Verified: `cmake --build _out/_cmake --config Release --target vsrIntegrationTests && ctest -C Release -R vsr::integration` (MCP ON) → passed. That run includes 7 MCP scenarios with 129 assertions, and the whole suite is 111 cases and 4298 assertions. Same with `-DVSR_USE_INTEGRATION_MCP=OFF` in `_out/_cmake_off` → passed.
Holes filed: 97-mcp-wire-rejection-codes, 98-mcp-private-bootstrap-methods

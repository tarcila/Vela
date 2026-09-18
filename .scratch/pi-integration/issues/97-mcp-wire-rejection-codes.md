# 97 — Confirm wire codes and HTTP statuses for Host/Origin rejection

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/mcp/McpServer.hpp

**Kind:** extends

Spec §3 requires rejecting wrong Host authorities and Origin-bearing requests, but gives no code name or HTTP status for those carrier-level rejections. Ticket 14 assumed:

- Host mismatch, duplicate or missing Host, or any forwarding header (`Forwarded`, `X-Forwarded-*`, `X-Original-Host`): HTTP 400, JSON-RPC error with `data.code = "invalid_host"`.
- Origin present: HTTP 403, `data.code = "forbidden_origin"`.
- Frozen outcomes: `unauthenticated`, `invalid_session_proof` and `session_expired` return HTTP 401, and `forbidden` returns 403. Every other semantic outcome returns HTTP 200 with a JSON-RPC error whose `data.code` carries the frozen name.

These names are outside the frozen set on purpose, so clients cannot mistake them for an authentication failure. If they are rejected, rename them in `McpServer::handle` and update test_IntegrationMcpTransport.cpp.

### 2026-09-28 orchestrator
Resolved by addendum 03 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 139.

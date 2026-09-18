# 98 — Validate private JSON-RPC bootstrap methods against the Pi gateway

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/mcp/McpServer.hpp

**Kind:** extends

Spec §3 requires attachment to deliver the session secret privately and forbids a model-visible tool for it. The spec does not say which carrier to use. Ticket 14 assumed the following:

- Attachment, renewal and detach are non-tool JSON-RPC methods: `vsr/attach`, `vsr/renew` and `vsr/detach`, sent to the same `/mcp` endpoint. The attach result is `{session_id, session_secret, lease_seconds, renewal_seconds}`. `tools/list` never lists these methods.
- MCP protocol version 2025-06-18 over streamable HTTP. Each POST carries one JSON-RPC message and receives an `application/json` reply. There is no SSE GET stream and no `Mcp-Session-Id`, because VSR session identity is carried in `VSR-Session-*` headers.

Revisit this once validation gate 1 (ticket 18 / Pi adapter) shows what the gateway can send. For example, if it cannot issue custom methods, or it needs SSE or native MCP sessions, the carrier must change.

### 2026-09-28 orchestrator
Resolved by addendum 05 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 144.

# 15 — Map discovery, inspection, edit, restore, render, resynchronize and lookup onto MCP tools

**Status:** wontfix
**Milestone:** 1
**Blocked by:** 06, 07, 08, 09, 10, 12, 14
**Files:** src/vsr/integration/mcp/tools/, src/tests/integration/test_IntegrationMcpTools.cpp

Contract: `.scratch/pi-integration/spec.md` — the MCP bullet of Testing Decisions §"Thin adapter checks and end-to-end acceptance" and the carrier-mapping deliverable in §9. The spec is authoritative; read those sections before starting. Business behavior is tested at the public headless integration API (spec §Testing Decisions, "Confirmed primary seam"), with deterministic clients and a controlled monotonic clock.

## Current behavior

The transport exists (ticket 14), but no scene operation is reachable over MCP.

## Desired behavior

- MCP tools cover discovery, inspection, edit batches, restoration by `target_batch_ref`, render (returning the image as a result, not a file path), resynchronize, cancel and status/result lookup. Each carries a qualified `operation_ref`.
- The payload placement and code mapping are documented in the module README: every frozen code and progress state, plus the commit/no-commit distinction and operation-kind-qualified conflicts. No client needs to parse English.
- Thin tests over real loopback cover each tool's happy path and mapping, capability policy, cross-session payload gates, and view-owned dimensions. They do not duplicate the headless suites.

## Out of scope

- Business rules (they live in tickets 02–12).
- Pi-specific behavior (ticket 18).

## Comments

### 2026-09-28 iteration — wontfix
Seven tool families, a full code-mapping README and loopback tests over ~10k lines of headless API exceed one 20-minute sitting. `McpServer::Config::tools` already dispatches `tools/call`, so no transport hook is needed yet.
split into 103, 104, 105

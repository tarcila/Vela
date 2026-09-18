# 222 — A connection's slot is freed only after its socket closes

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/mcp/McpServer.hpp, src/tests/integration/test_IntegrationMcpTransport.cpp

**Kind:** extends

## Observation

In `McpServer` the connection thread runs `::close(fd)` and only then sets `c.done = true`. The accept loop reaps a slot only when `done` is set. A client that sees EOF on one request and reconnects straight away can therefore reach `accept` before `done` is set. With `maxConnections == 1` that client gets a 503. In ticket 221 this made the trailing back-to-back `ping` in "MCP frees a slow-reader connection at the response deadline" fail in 4 of 30 runs (`503 == 200`, elapsed ≈ 505 ms).

## Assumption taken (221)

Ticket 221 is test-only, so it removed that trailing ping. The poll loop still proves that the slot is freed and that `ping` returns 200 within 6× the deadline. The spec does not say whether back-to-back reconnects must see a free slot, so this ticket does not treat the race as a bug.

## Revisit

If back-to-back reconnects must succeed, set `done` (or decrement a live count) before or together with `close(fd)` under `m_connMutex`, then restore the trailing `REQUIRE(f.call(rpc("ping")).status == 200)`.

### 2026-09-28 orchestrator
Resolved by addendum 11. Follow-up ticket 223.

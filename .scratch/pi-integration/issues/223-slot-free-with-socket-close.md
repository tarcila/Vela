# 223 — Mark the MCP connection slot free together with the socket close

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/mcp/McpServer.hpp, src/tests/integration/test_IntegrationMcpTransport.cpp

Contract: `.scratch/pi-integration/spec.md` plus addendum 11. Source: hole 222 (from ticket 221), found during the drain after `post-review:1:5`.

## Current behavior

In `McpServer::acceptLoop`, the connection thread closes its fd under `m_connMutex`, releases the lock, and only then sets `c.done = true`. A client that sees the close and reconnects immediately can be accepted before `done` is set. With `maxConnections == 1` it gets a 503. Ticket 221 had to remove a back-to-back `ping` check that failed 4 of 30 runs.

## Desired behavior

- `done` is set in the same critical section as the close. `done` stays safe to read from the accept loop, and becomes atomic if it isn't already.
- A regression test with `maxConnections == 1` does several strictly sequential request/response round trips, each with `Connection: close` and each reconnecting as soon as the previous response completes. Every one gets 200. It passes 30 runs in a row.
- The back-to-back `REQUIRE(f.call(rpc("ping")).status == 200)` that ticket 221 removed is restored in the 500 ms slow-reader scenario, straight after the poll loop succeeds. Both slow-reader scenarios pass 30 runs in a row.
- The full `[integration]` suite passes.

## Out of scope

- Other server behavior.
- Moving the 503 reply out from under the lock (dismissed in round 3).

## Comments

### 2026-09-29 iteration — resolved
Connection thread now closes the fd, clears `fd` and sets `done` (already `std::atomic<bool>`) in one `m_connMutex` critical section, per addendum 11. Added scenario "MCP frees the only slot before a sequential client can reconnect" (`maxConnections == 1`, 50 sequential `Connection: close` pings, each must be 200) and restored the back-to-back ping in the 500 ms slow-reader scenario. Deliverable change: ukktmrxonxvu.
Verified: `vsrIntegrationTests "Scenario: MCP frees the only slot before a sequential client can reconnect"` ×30 → 0 failures; `vsrIntegrationTests "*slow*"` ×30 → 0 failures; `vsrIntegrationTests "[integration]"` → All tests passed (7430 assertions in 174 test cases)
Holes filed: none

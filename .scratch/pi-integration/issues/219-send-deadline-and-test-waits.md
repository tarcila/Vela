# 219 — Bound the whole MCP response send with a deadline; add deadlines to the new test spins

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/mcp/McpServer.hpp, src/tests/integration/test_IntegrationMcpTransport.cpp, src/tests/integration/test_IntegrationOperations.cpp, src/tests/integration/test_Registration.cpp

Source: post-review round `post-review:1:2`, report `e48ef8cb-3f55-4dac-bd9a-844f64a1b5d9`, findings gpt/finding-1 (medium) and gpt/finding-2 (low), plus the coordinator's note. The human accepted both as fixes on 2026-09-28. Contract: `.scratch/pi-integration/spec.md` plus addenda 01–10.

## Current behavior

- `McpServer` sets `SO_SNDTIMEO` per socket, but `detail::sendAll` loops until every byte is sent. A slow reader that keeps making partial progress restarts the timeout on every call, so it can hold a connection thread far longer than `sendTimeoutMs`.
- The concurrency test in `test_IntegrationOperations.cpp` spins on `slotsInUse() > 0` with no deadline. Its submitter do/while-BUSY retry loop has no deadline either.
- `test_Registration.cpp` has `while (ran < N) std::this_thread::yield();` with no deadline.

## Desired behavior

- Writing a whole response has one total deadline, measured from the first byte sent. It is configurable, and defaults to `sendTimeoutMs` or a documented total. When the deadline passes, the server stops sending, closes the connection and frees the slot.
- A new transport test uses a client that reads a large authenticated response one byte at a time, slowly. Its connection slot is freed within the deadline, and a normal client then succeeds.
- Every spin or retry loop named above has a deadline and fails with a diagnostic on timeout. It still stops and joins any helper threads on the way out.
- The full `[integration]` suite passes.

## Out of scope

- Any other server or proxy behavior.

## Comments

### 2026-09-29 iteration — resolved
Added `Config::responseDeadlineMs` (0 → defaults to `sendTimeoutMs`). `detail::sendAll` now uses poll(POLLOUT) with non-blocking sends under one total deadline measured from the start of the send. On a miss the server logs, closes, and frees the slot. Added the transport scenario "MCP frees a slow-reader connection at the response deadline": a 32 MiB tools/list response, a client reading 1 byte per 10 ms, and a single slot. It checks that the slot is taken (503), then freed within the deadline, and that a normal ping then succeeds. With the deadline raised to 60 s the test fails, as expected. Bounded the BUSY-retry and slotsInUse drain loops in test_IntegrationOperations, and the submit/`ran < N` loops in test_Registration, with 30 s deadlines. Each one fails with an INFO diagnostic and still joins its helper threads. Deliverable: lupxkmvzrxmq.
Verified: `cmake --build _out/_cmake --config RelWithDebInfo --target vsrIntegrationTests && _out/_cmake/RelWithDebInfo/vsrIntegrationTests "[integration]"` ×4 → All tests passed (172 test cases) each run
Holes filed: none

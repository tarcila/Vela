# 215 — Harden vsrMcpProxy: bounded reads, local reattach failure, endpoint ownership, fake-clock test

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/apps/tools/McpProxy.hpp, src/apps/tools/vsrMcpProxy.cpp, src/apps/tools/README.md, src/tests/integration/test_IntegrationMcpProxy.cpp

Source: post-review round `post-review:1:1` (p3 report d75894e5: gpt/finding-1 + grok/proxy-unbounded-http-read, gpt/finding-2, coordinator note on stale-descriptor credential exposure; p4 report d232cbec: grok/mcp-proxy-wallclock-flake + claude/finding-3). The human accepted these findings as fixes on 2026-09-28; see `.scratch/pi-integration/pipeline.md`, §Partition post-review:1:1. Contract: `.scratch/pi-integration/spec.md` plus addenda 01–08.

## Current behavior

- `Proxy::post` reads until EOF, with no byte cap and no socket deadline, and uses a blocking connect.
- `forward()` still POSTs when the reattach has failed.
- After a crash, a stale descriptor may name a port that another local account has rebound. The proxy would then send the launch credential to that listener.
- The lease test uses real wall-clock sleeps.

## Desired behavior

- `post()` has a connect deadline, an overall response deadline and a response byte cap. It honours `Content-Length`. Exceeding any limit fails that request locally with a JSON-RPC error to the stdio client. Renewal and shutdown can never block longer than those deadlines.
- If a reattach fails, `forward()` returns a local JSON-RPC error with the frozen code of the failure, for example `occupied`, and does not POST.
- Before sending the launch credential on each new TCP connection, the proxy verifies on Linux that the listening peer socket is owned by the proxy's own effective UID. It looks up the connected remote endpoint in `/proc/net/tcp` or `/proc/net/tcp6`. On a mismatch, or if the check is impossible, it refuses and exits with a diagnostic that contains no credential. Document this in `src/apps/tools/README.md`.
- The proxy's timing is injectable. The lease/renewal test uses a controlled clock with no wall-clock sleeps longer than a small bounded poll. It asserts renewals across more than 2 leases deterministically.
- Tests cover the response cap, the deadline (a server that never replies), local reattach failure, and ownership-check refusal. For the last, fake the ownership source if needed.

## Out of scope

- Server-side changes (ticket 214).

## Comments

### 2026-09-29 iteration — resolved
Deliverable ukxkwmkq. `McpProxy.hpp` now does a non-blocking connect under a deadline, and each POST has an overall deadline, a response byte cap and Content-Length handling. Breaches return a local JSON-RPC error (`timeout`/`response_too_large`/`unavailable`). A failed reattach answers with the attach's frozen code (e.g. `occupied`) and does not POST. On each new connection an ownership check runs before the credential is sent: the default reads `/proc/net/tcp{,6}` and compares the owner with the effective UID. Its result is injectable. A mismatch or unknown result refuses the endpoint and `run()` exits 1 with a fixed diagnostic. `Options{limits, clock, ownership}` are injectable. The renewal timer re-reads the clock (waits ≤0.25 s). The lease test drives one fake clock for the server and the proxy through more than 2.5 leases, and asserts an exact renewal count. New tests cover the cap (declared and streamed), Content-Length on a kept-open connection, a server that never replies, local reattach failure, ownership refusal (FOREIGN/UNKNOWN, zero bytes sent) and the real /proc check accepting our own listener. The README documents all of this. `vsrMcpProxy.cpp` did not need changes.
Verified: `cmake --build _out/_cmake --config RelWithDebInfo --target vsrIntegrationTests vsrMcpProxy` → clean; `_out/_cmake/RelWithDebInfo/vsrIntegrationTests "[integration]"` → All tests passed (6009 assertions in 157 test cases); `"*vsrMcpProxy*"` ×10 → 170 assertions / 7 cases pass each run (~0.6 s)
Holes filed: 225-mcp-proxy-local-error-codes


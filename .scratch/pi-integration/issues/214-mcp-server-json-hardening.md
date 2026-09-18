# 214 — Harden the MCP JSON parser and loopback HTTP server

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/mcp/Json.hpp, src/vsr/integration/mcp/McpServer.hpp, src/tests/integration/test_IntegrationMcpTransport.cpp

Source: post-review round `post-review:1:1` (p3 report d75894e5: gpt/finding-3 + grok/json-stod-throw, gpt/finding-4, grok/mcp-cl-gt-max-as-400, coordinator notes on slow-drip headers, fd reuse and stod locale; p4 report d232cbec: gpt/finding-3 (transport harness), claude/finding-1 + grok/ipv6-loopback-warn-skip). The human accepted these findings as fixes on 2026-09-28; see `.scratch/pi-integration/pipeline.md`, §Partition post-review:1:1. Contract: `.scratch/pi-integration/spec.md` plus addenda 01–08.

## Current behavior

- `Parser::number` calls `std::stod`, which depends on the locale and throws on values such as `1e9999`. Nothing catches the exception, so it reaches `std::terminate`.
- Sockets get only `SO_RCVTIMEO`; there is no send timeout.
- A client that sends its headers slowly can hold every connection slot before authentication.
- A worker closes `c.fd` but never resets it, so `stop()` may call `shutdown()` on a reused descriptor.
- A `Content-Length` above the limit is answered with 400.
- The test harness `post()` reads until EOF, with no deadline or size bound.
- The IPv6 test warns and returns when IPv6 loopback is unavailable.

## Desired behavior

- JSON number parsing is locale-independent (for example `std::from_chars` or a C-locale strtod) and never throws. Out-of-range or non-finite numbers fail the parse with a JSON-RPC parse error. A test sends `1e9999` in an authenticated request; the server answers with an error and keeps serving.
- Every connection has a send timeout and a total deadline for reading the request (headers plus body), not just a per-`recv` timeout. A test trickles header bytes on all `maxConnections` slots without credentials: the slots are released within the deadline and a legitimate client then succeeds.
- Closed connection fds are set to -1 under the lock, and `stop()` never shuts down an fd it no longer owns.
- A `Content-Length` over `maxBodyBytes` is answered with HTTP 413 before the body is read.
- The test harness `post()` has a receive deadline and a byte cap, and fails the test on either.
- The IPv6 scenario fails, not skips, when IPv6 loopback is unavailable, unless the environment variable `VSR_TEST_ALLOW_NO_IPV6=1` is set. With the variable set it skips with a WARN. Record the variable in the test file header.

## Out of scope

- The stdio proxy (ticket 215).
- Tool semantics.

## Comments

### 2026-09-29 iteration — resolved
Json.hpp: `std::from_chars` replaces `std::stod` (no locale, no throw); an out-of-range or non-finite value fails the parse, so the server returns a -32700 parse error. McpServer.hpp: new `Config::requestDeadlineMs`/`sendTimeoutMs` (default 10 s each); `SO_SNDTIMEO` on every conn; poll-bounded recv under one total deadline covering headers and body; a closed conn sets fd=-1 under the lock and `stop()` skips it; Content-Length over max → 413 before the body is read. Tests: new harness `readReply` with a 15 s deadline and a 1 MiB cap (FAIL on either); new scenarios for 1e9999, 413 and slow-drip slot release. The IPv6 scenario now fails without IPv6 loopback unless `VSR_TEST_ALLOW_NO_IPV6=1` is set (documented in the file header). Deliverable: suurzunz.
Verified: `_out/_cmake/Release/vsrIntegrationTests "[integration]"` → All tests passed (5983 assertions in 155 test cases)
Holes filed: none

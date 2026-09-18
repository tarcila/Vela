# 220 — Bound the deadlock test's joins, make the slow-reader test independent of timing, fix the sendAll deadline comment

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/tests/integration/test_IntegrationOperations.cpp, src/tests/integration/test_IntegrationMcpTransport.cpp, src/vsr/integration/mcp/McpServer.hpp

Source: post-review round `post-review:1:3`, report `52301fcb-7fe1-48c4-9eac-550e5e4250c2`, findings gpt/finding-1 (medium), gpt/finding-2 (low) and grok/finding-2 (low). On 2026-09-28 the human accepted them as fixes (option b). The same decision confirmed the coordinator's dismissal of grok/finding-1. Contract: `.scratch/pi-integration/spec.md` plus addenda 01–10.

## Current behavior

- The concurrent render deadlock scenario in `test_IntegrationOperations.cpp` bounds its retry and drain loops, but `host.join()` and the submitter joins wait forever. If the host thread really gets stuck inside `service()`, the test hangs.
- The slow-reader scenario in `test_IntegrationMcpTransport.cpp` asserts that a `ping` returns 503 after the reader sees its first byte. If the test thread stalls for about the length of the response deadline, the server frees the slot first. The ping then correctly returns 200 and the assertion fails.
- `detail::sendAll` measures its deadline from when the call starts, but the comment on `Config::responseDeadlineMs` says the deadline is "measured from the first byte sent".

## Desired behavior

- Every thread in the deadlock scenario is joined against a deadline. A thread that doesn't finish in time fails the test with a diagnostic instead of hanging. For example, each thread signals a completion flag and the test waits on the flags with a deadline. If a thread is truly stuck, the process may abort (e.g. `FAIL` then `std::abort`) rather than hang.
- The slow-reader scenario checks that a healthy client is rejected while the slow send is still in progress without depending on scheduling. For example, it uses a long response deadline for the occupied-slot check and a separate short one for the expiry check, or it confirms the server still holds the connection before sending the ping. The slot-freed-after-deadline check stays.
- The `Config::responseDeadlineMs` comment and the `sendAll` comment agree with the implementation: the deadline is measured from the start of the write, including time spent waiting for the socket to accept data. Behavior does not change.
- The full `[integration]` suite passes several times in a row.

## Out of scope

- Any change to server or engine behavior.
- Moving the 503 reply out from under the connection lock (dismissed in round 3).

## Comments

### 2026-09-29 iteration — resolved
Deliverable nrvkppll. Deadlock scenario: host and submitter threads set completion flags (RAII for submitters); each join waits on its flag with a deadline and, if the thread is stuck, prints a diagnostic to stderr and calls `std::abort` instead of hanging (engine not queried on that path, since it could be locked). Slow-reader scenario split in two with a shared `SlowReader` helper: the occupied-slot 503 check now uses a 120 s response deadline, so it no longer depends on timing, and the expiry check keeps the 500 ms deadline and waits for 200. Fixed the `responseDeadlineMs` and `sendAll` comments: the deadline is measured from the start of the write, including socket wait. Behavior is unchanged. The transport test file was left unformatted by clang-format, as it was before this change.
Verified: `cmake --build _out/_cmake --config RelWithDebInfo --target vsrIntegrationTests` → ok; `_out/_cmake/RelWithDebInfo/vsrIntegrationTests "[integration]"` ×8 → All tests passed (173 test cases) every run
Holes filed: none

# 218 — Fix test hygiene (identity use-after-free, unbounded waits, logging callback, ACL skip, tools harness) and update Pi docs currency

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/tests/integration/test_IntegrationIdentity.cpp, src/tests/integration/test_IntegrationContext.cpp, src/tests/integration/test_IntegrationSession.cpp, src/tests/integration/test_IntegrationDescriptor.cpp, src/tests/integration/test_IntegrationMcpTools.cpp, docs/pi-attachment.md

Source: post-review round `post-review:1:1` (p4 report d232cbec: gpt/finding-1, gpt/finding-2, gpt/finding-3 (tools harness), gpt/finding-4, claude/finding-2 + grok/descriptor-acl-warn-skip; spec-compliance pass: (a) docs/pi-attachment.md open item 213 stale). The human accepted these findings as fixes on 2026-09-28; see `.scratch/pi-integration/pipeline.md`, §Partition post-review:1:1. Contract: `.scratch/pi-integration/spec.md` plus addenda 01–08.

## Current behavior

- The identity scene-replacement test reads `m.index()` after its Scene was reset.
- The context test waits on `ran` and `host.join()` without deadlines.
- The session secrets test installs a global logging callback that captures a local by reference, with no RAII reset.
- The descriptor ACL case warns and passes when `setxattr` fails.
- The MCP tools harness `post()` reads with no deadline or size cap.
- `docs/pi-attachment.md` still lists ticket 213, the agent camera framing, as open.

## Desired behavior

- The identity test captures the needed index before resetting the scene, and never touches objects of a destroyed scene.
- Every wait in the context test has a deadline and fails with a diagnostic on timeout.
- The logging callback is installed through an RAII guard that resets it on scope exit, including when an assertion fails.
- The ACL case fails when ACLs are unsupported, unless the environment variable `VSR_TEST_ALLOW_NO_ACL=1` is set; then it skips with a WARN. Document this in the file header.
- The tools harness `post()` has a receive deadline and a byte cap.
- `docs/pi-attachment.md` records the framing fix from ticket 213 (addendum 08) as done, and removes it from open items.
- The full `[integration]` suite passes.

## Out of scope

- Production code.

## Comments

### 2026-09-29 iteration — resolved
Deliverable psvprpuz. Identity: `m.index()` is captured and `m` is cleared before the scene is reset. Context: `HostThread` joins against a future with a 10 s deadline; on timeout it calls `loop.stop()` and fails with INFO, and its destructor does the same. The `ran` wait is a deadline poll. Session: `ScopedLoggingCallback` RAII calls setNoLogging() on scope exit; the logging API has no getter for the previous callback. Descriptor: when setxattr fails, the test FAILs unless `VSR_TEST_ALLOW_NO_ACL=1`, which downgrades it to WARN+skip; this is documented in the file header. McpTools `post()`: poll-based 10 s receive deadline, 64 MiB byte cap, RAII close. `docs/pi-attachment.md`: 213 is under "Resolved since the run", and Open items is now "None". The McpTools file has pre-existing clang-format drift that I left untouched.
Verified: `cmake --build _out/_cmake --config Release --target vsrIntegrationTests && _out/_cmake/Release/vsrIntegrationTests '[integration]'` → All tests passed (168 test cases); ran again with VSR_TEST_ALLOW_NO_ACL=1 → pass.
Holes filed: none

# 228 — Is SlowReader's 10 s first-byte wait enough on loaded CI hosts?

**Status:** needs-triage
**Milestone:** 1
**Blocked by:** none
**Files:** src/tests/integration/test_IntegrationMcpTransport.cpp

Source: post-review:1:5 claude/finding-2 (report ccbaa444); round 6 context. Filed when milestone 1 closed on 2026-09-28, per the human's decision recorded in `.scratch/pi-integration/pipeline.md`. Not a blocker; for triage.

`SlowReader::start` waits up to 10 s for the first byte of a 32 MiB `tools/list` response. The wait is documented, but there is no CI timing data yet. If this flakes on CI, raise the bound or make it scale with the host. Test-only.

## Comments

# 152 — Feed discovery's listed targets into ActivityLog covered targets

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/activity/Activity.hpp, src/tests/integration/test_IntegrationActivity.cpp

**Kind:** extends

Ticket 140 made discovery session-wide. The engine record now covers exactly the targets its page lists (spec §9, addendum 04 item 5), and it exposes them as `operations::ActivityEvent::covered`, filled once the record completes. `EngineActivitySink` still calls `coveredTargets(kind, ev.target, integration)` without `listed`, so an ActivityLog discovery entry has empty `targets`. `ev.target` is a placeholder for session-wide work.

Assumption: the sink should copy `ev.covered` into `Entry::targets` in `updated()` when a DISCOVERY record reaches its terminal state. This was left undone because `activity/` belongs to ticket 142, which was running at the same time. Revisit if activity lookup should instead deny discovery records outright.

### 2026-09-28 orchestrator
Accepted as extends via addendum 06; now ready-for-agent. Contract: spec + addenda 04 and 06.

### 2026-09-29 iteration — resolved
`EngineActivitySink::updated()` now sets a DISCOVERY entry's `targets` from `ev.covered` once the record is terminal (addendum 06 item 1). `ActivityLog::lookup` treats an empty discovery page as readable, the same way the engine's `status()` does for session-wide records. New scenario covers: the targets are empty while pending, then equal the listed view; a foreign session keeps access after losing read on the unlisted scene and is denied after losing read on the listed view.
Verified: `_out/_cmake/Release/vsrIntegrationTests "[integration]"` → All tests passed (5047 assertions in 130 test cases)
Holes filed: none

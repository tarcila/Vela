# 119 — OperationEngine appends/updates ActivityLog entries

**Status:** resolved
**Milestone:** 1
**Blocked by:** 10
**Files:** src/vsr/integration/operations/Operations.hpp, src/vsr/integration/activity/Activity.hpp

Part 1 of split ticket 80.

`activity/Activity.hpp` includes `operations/Operations.hpp`, so the engine
cannot name `ActivityLog` directly. Suggested seam: an abstract sink declared in
Operations.hpp (e.g. `ActivitySink { admitted(...) -> optional<id>; updated(id,
Status, now); }`), installed with `OperationEngine::setActivitySink(...)`, and an
adapter in Activity.hpp that maps request kind strings to `OperationKind`,
fills `coveredTargets()`, `ref`/`localRef`, progress, terminal outcome,
committed, timestamps, heldSlots and result.

## Desired behavior

- `submit`/`submitLocal` reserve an ActivityLog entry at admission; if the log
  returns nullopt (cannot fit without evicting active work) admission rejects
  `busy` and nothing is admitted.
- Progress/terminal/cleanup transitions update the entry (`active` cleared once
  terminal and no slot held, so it becomes evictable).
- Client summaries stay in `Entry::clientSummary`, never in VSR-observed fields.
- Engine-only behavior unchanged when no sink is installed (existing tests green).

## Acceptance criteria

- [x] Headless test: engine with a small-limit ActivityLog rejects busy when the
      log is full of active entries; terminal entries are evicted oldest-first.
- [x] Headless test: entry progress/outcome mirror `status()`.
- [x] `vsrIntegrationTests "[integration]"` green.

## Comments

### 2026-09-28 iteration — resolved
Added `operations::ActivitySink`/`ActivityEvent` and
`OperationEngine::setActivitySink`; submit/submitLocal reserve an entry before
admission (nullopt → `busy`), and running/yield/sync/backend/terminal/cleanup
transitions call `updated`. `activity::EngineActivitySink` maps kind strings,
covered targets, refs, progress, outcome, timestamps, heldSlots and result;
`active` clears once terminal with no slot held. Sink runs under the engine
lock (must not re-enter the engine). Viewer wiring filed as 122.
Verified: `cmake --build _out/_cmake --config RelWithDebInfo --target vsrIntegrationTests vsrViewer && _out/_cmake/RelWithDebInfo/vsrIntegrationTests "[integration]"` → All tests passed (5125 assertions in 132 test cases)
Holes filed: none

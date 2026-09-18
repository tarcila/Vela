# 122 — vsrViewer installs EngineActivitySink on its OperationEngine

**Status:** resolved
**Milestone:** 1
**Blocked by:** 119
**Files:** src/apps/interactive/viewer/vsrViewer.cpp

Ticket 119 added `operations::ActivitySink`, `OperationEngine::setActivitySink`
and the `activity::EngineActivitySink` adapter, but the viewer still creates
its `ActivityLog` without feeding it, so the activity window stays empty in the
live viewer (see 110).

## Desired behavior

- The viewer constructs `EngineActivitySink(*m_activity, *m_integration)`
  and installs it on `m_engine` right after creating the log.
- Teardown clears the sink (`setActivitySink(nullptr)`) before destroying the
  log/sink, or destroys the engine first.

## Acceptance criteria

- [x] Viewer builds; `vsrIntegrationTests "[integration]"` green.
- [ ] Submitted agent work shows up in the viewer activity table.

## Comments

### 2026-09-28 iteration — resolved
Viewer owns an `EngineActivitySink` (`m_activitySink`) built from `*m_activity`
and `*m_integration` right after the log, installed via `m_engine->setActivitySink`.
Teardown calls `setActivitySink(nullptr)` before other resets, then destroys the
sink before the log. Live GUI check of the activity table (criterion 2) not run
headless; the sink path is the one covered by test_IntegrationActivity.
Verified: `cmake --build _out/_cmake --config RelWithDebInfo --target vsrViewer vsrIntegrationTests` → ok; `_out/_cmake/RelWithDebInfo/vsrIntegrationTests "[integration]"` → All tests passed (5209 assertions in 132 test cases)
Holes filed: none

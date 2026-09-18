# 142 — Add ordered ActivityLog enumeration and use it in the viewer model

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/activity/Activity.hpp, src/vsr/ui/imgui/windows/IntegrationActivityModel.h, src/tests/integration/test_IntegrationActivity.cpp, src/tests/integration/test_IntegrationViewerModel.cpp

Contract: `.scratch/pi-integration/spec.md` plus addendum 05 (item 5). The spec and the addenda are authoritative; read them first. Business behavior is tested at the public headless integration API, with deterministic clients and a controlled clock.

## Current behavior

The viewer model finds records by probing monotonic IDs up to 32 ahead each frame. A burst of evictions can stall discovery of later records (hole 109).

## Desired behavior

- [x] `ActivityLog` exposes an ordered, thread-safe snapshot or enumeration of retained entries.
- [x] `IntegrationActivityModel::refreshActivity()` uses it.
- [x] A test evicts more than 32 records between refreshes, and the model still shows every retained record.

## Out of scope

- ImGui layout changes.

## Comments

### 2026-09-29 iteration — resolved
Added `ActivityLog::snapshot()` returning `{id, entry}` records oldest first under the log mutex; the viewer model's `refreshActivity()` now rebuilds rows from it and the id-probing state (`m_known`, `m_next`, PROBE) is gone.
Tests: snapshot ordering/eviction/update in test_IntegrationActivity; viewer model with an 8-entry log sees 3 records, then 97 more appended (89 evicted) and shows exactly ids 93–100 (old probe would have stalled at 4–35).
Verified: `_out/_cmake/RelWithDebInfo/vsrIntegrationTests "[integration]"` → All tests passed (4947 assertions in 124 test cases)
Holes filed: none

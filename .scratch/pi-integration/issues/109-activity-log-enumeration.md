# 109 — ActivityLog lacks an enumeration API for the viewer

**Status:** resolved
**Milestone:** 1
**Blocked by:** 17
**Files:** src/vsr/integration/activity/Activity.hpp, src/vsr/ui/imgui/windows/IntegrationActivityModel.h

**Kind:** extends

`activity::ActivityLog` exposes only `get(Id)`, `lookup(...)` and `usage()`. The
viewer model (ticket 17) needs every retained record, so it assumes ids are
the monotonic serial `append()` returns: it re-reads known ids and probes up to
32 ids forward each frame. A burst of more than 32 records evicted before the
viewer sees them would stall discovery of later ones.

Assumption chosen because ticket 17 may not edit `src/vsr/integration/`. Revisit:
add `ActivityLog::snapshot()` (or `forEach`) returning retained entries in
order, and switch `IntegrationActivityModel::refreshActivity()` to it.

### 2026-09-28 orchestrator
Resolved by addendum 05 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 142.

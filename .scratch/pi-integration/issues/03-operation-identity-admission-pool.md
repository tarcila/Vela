# 03 — Add operation identity, admission, the shared scene-work pool, control path and deadlines

**Status:** resolved
**Milestone:** 1
**Blocked by:** 02
**Files:** src/vsr/integration/operations/, src/tests/integration/test_IntegrationOperations.cpp

Contract: `.scratch/pi-integration/spec.md` — Implementation Decisions §5 (execution, admission and resource accounting, including the default resource table), §9 (operation identity, admission and outcomes, including frozen codes and progress state names), the cancellation rules at the end of §4, and Behavioral acceptance coverage items 8 and 11. The spec is authoritative; read those sections before starting. Business behavior is tested at the public headless integration API (spec §Testing Decisions, "Confirmed primary seam"), with deterministic clients and a controlled monotonic clock.

## Current behavior

Sessions exist (ticket 02), but no scene operation can be submitted, admitted, queued, cancelled or looked up.

## Desired behavior

A new suite `test_IntegrationOperations.cpp` drives a generic test operation kind, so no real scene operations are needed yet, and shows:

- `request_id` rules: an integer in [1, 2^53−1], one high-water mark per session, and `operation_ref` objects of `{session_id, request_id}`.
- Admission atomically reserves one slot of the shared 32-slot pool (configurable) plus status storage, then advances high-water. Pre-admission rejection consumes nothing. The sequence 5 busy, 6 admitted, retry 5 returns `outcome_unavailable`.
- An identical retained retry replays without consuming a slot, even when the pool is full. Conflicting content returns `request_id_conflict` and leaves the record unchanged. An evicted ID at or below high-water never executes.
- Local in-process callers share the same pool, use host-local correlation, and get no extra slots.
- A separately bounded control path handles renewal, cancel, detach and status/result lookup: 8 concurrent requests by default, prioritized and coalesced. It stays serviceable when the scene pool is full.
- Non-terminal states use the frozen names `queued`, `running`, `yielded`, `synchronizing` and `awaiting_cleanup`.
- Logical deadlines start at admission and include queue wait, on a controlled clock. Expired queued work never starts.
- A cancelled, timed-out or detached request stays terminal (`cancelled`/`timeout`) even if its late result arrives. A fake backend that never releases resources keeps the slot in `awaiting_cleanup`, exposing elapsed time and held capacity, with no forced release.
- Whole-session revocation cancels the session's outstanding work and blocks later admission of in-transit IDs.
- Default and smaller configured limits are both tested.

## Out of scope

- Real discovery, inspection, edit, restore, render or resynchronize semantics (tickets 06–09, 12).
- The activity/history model and cross-session lookup (ticket 10). Here, status is only needed for the session's own operations.
- Transport and MCP mapping.

## Comments

### 2026-09-28 iteration — resolved
Added header-only `src/vsr/integration/operations/Operations.hpp` (following the ticket-19 convention). It provides frozen outcome/progress names, request-ID validation, `OperationRef`, and the `OperationEngine`. The engine keeps a per-session high-water mark, admits atomically into a shared pool (32 slots by default), and replays identical requests or rejects conflicting ones. It also handles host-local correlation, deadlines that start at admission, `awaiting_cleanup` with no forced release, cancellation when a session ends, and permission re-checks at the safe point. The bounded `ControlGate` (8 by default) holds its last slot back from lookups and coalesces identical lifecycle calls. `test_IntegrationOperations.cpp` covers all of this through a scripted fake operation kind and a controlled clock, with both default and smaller limits. I edited no shared CMake or top-level files.
Verified: `cmake --build _out/_cmake --config Release --target vsrIntegrationTests && ./_out/_cmake/Release/vsrIntegrationTests` → All tests passed (1088 assertions in 44 test cases); `ctest -C Release -R integration` → 1/1 passed
Holes filed: 37-operation-engine-policy-assumptions

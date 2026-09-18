# 08 — Add scene synchronization states, explicit Resynchronize and confirmed view registration

**Status:** resolved
**Milestone:** 1
**Blocked by:** 07
**Files:** src/vsr/integration/sync/, src/tests/integration/test_IntegrationSync.cpp

Contract: `.scratch/pi-integration/spec.md` — Implementation Decisions §10 (the view-set, view-registration, synchronization-state, Resynchronize and non-ready paragraphs) and the remaining parts of Behavioral acceptance coverage item 6. The spec is authoritative; read those sections before starting. Business behavior is tested at the public headless integration API (spec §Testing Decisions, "Confirmed primary seam"), with deterministic clients and a controlled monotonic clock.

## Current behavior

Ticket 07 can mark a scene `failed`, but nothing recovers it, reports state transitions or guards newly registered views.

## Desired behavior

A new suite `test_IntegrationSync.cpp` shows:

- Scene states use the frozen names `ready`, `needs_sync`, `synchronizing` and `failed`.
- Resynchronize requires read and resynchronize on the scene, runs the host recovery callback at a safe point, reports transitions, and reports `ready` only after the host confirms every registered view. It never replays edits, has no automatic retry, and uses a scene-work slot.
- A failure on one of several views blocks every view of that scene, while an unrelated scene stays usable.
- View registration or unregistration attempted during a hook waits for a later safe point.
- A newly registered view becomes usable only after host confirmation. Otherwise registration fails, or the scene becomes `needs_sync`, until Resynchronize succeeds.
- Inspection and authorized edits continue while the scene is not ready. The render-refusal side (`sync_required`) is left to ticket 12, but this ticket exposes the readiness predicate it needs.

## Out of scope

- Rendering (ticket 12).
- Restoration (ticket 09).
- Per-index dependency graphs and partial-view recovery.

## Comments

### 2026-09-28 iteration — resolved
Added `src/vsr/integration/sync/SceneSync.hpp` (header-only). It provides `SyncStates` with the frozen names, transition listener and `needs_sync`, plus `SyncService`: Resynchronize (scene-slot op, RESYNCHRONIZE capability, one attempt, no edit replay), queued post-activation view (un)registration with host confirmation, and the `viewReady()` predicate for ticket 12. EditService now delegates to `sync::SyncStates`, which takes over the interim state from ticket 07/62.
Touched outside Files: `src/vsr/integration/edit/AgentEdit.hpp` (uses `sync::SyncStates`, exposes `syncStates()`), `src/vsr/integration/Integration.hpp` and `.cpp` (`recoveryCallback()`, `viewsOfScene()`, `unregisterView()`, and `registerView()` allowed after activation on the servicing thread only). restore/ untouched.
Verified: `cmake --build _out/_cmake --config Release --target vsrIntegrationTests && ctest -C Release -R vsr::integration` → 100% passed (80 test cases, 2489 assertions, incl. 11 new in test_IntegrationSync.cpp)
Holes filed: 67-sync-policy-assumptions

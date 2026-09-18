# 67 — Scene synchronization policy assumptions (resync outcome, view registration)

**Status:** resolved
**Milestone:** 1
**Blocked by:**
**Files:** src/vsr/integration/sync/SceneSync.hpp, src/vsr/integration/Integration.hpp, src/vsr/integration/Integration.cpp

**Kind:** extends

Ticket 08 settled these points, which spec §10 leaves open:

1. **Resynchronize outcome.** A recovery attempt that runs always ends with terminal outcome `ok`. The status result carries `{"ready":bool,"state":"<sync state>"}`. The frozen codes include no code for a failed recovery, so failure shows as `ready:false` / `state:"failed"`. Revisit if the adapter (ticket for MCP) wants failure as a non-ok terminal code.
2. **Post-activation view registration.** `Integration::registerView`/`unregisterView` work after activation only on the servicing thread, which means inside host scene work. `SyncService::requestRegisterView`/`requestUnregisterView` queue that work through `Integration::submit`. It uses no integration scene slot, because it is host work (spec §9: ordinary application work is not charged). View ids are never reused. Unregistration marks the view removed.
3. **Unconfirmable new view.** The host chooses per registration between `FAIL_REGISTRATION`, which removes the view again, and `NEEDS_SYNC`, which keeps the view and marks the scene `needs_sync`. The spec allows either.
4. **needs_sync vs edits.** Like `failed`, a scene in `needs_sync` makes an ordinary edit return `applied_with_sync_failure`, even when the hook confirms. Only Resynchronize clears it. A failing recovery leaves the scene `failed`, even if it started from `needs_sync`.
5. **State precedence.** Recovery in progress reports `synchronizing`. Otherwise the order is `failed`, then `needs_sync`, then pending hooks (`synchronizing`), then `ready`.

## Comments

### 2026-09-28 orchestrator
Resolved by addendum 03 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 139.

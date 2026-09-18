# 55 — Inspection item shape and cursor-validation assumptions

**Status:** resolved
**Milestone:** 1
**Blocked by:**
**Files:** src/vsr/integration/inspection/Inspection.hpp

**Kind:** extends

Ticket 06 had to pick an inspection item shape the spec leaves open. Assumed:

- Items carry handle (hex domain+serial), ANARI type, subtype, parameter-version number, name (optional display string, truncation-marked) and parameter name/type pairs (optional, capped at 16 KiB and marked `parameters_truncated`). Parameter **values** are not returned yet; revisit once the §8 edit-surface value serialization should appear in inspection.
- Tampered, foreign-session, foreign-query and foreign-target cursors are rejected before admission as `INVALID_REQUEST` (no request ID consumed). Revisit if they must be admitted and fail with a distinct code.
- The inspection revision is `SceneIdentity::inspectionRevision()` after `refreshAllMetadata()` each slice; that refresh is O(objects) per slice, which violates the bounded-step intent on huge scenes. Revisit with an incremental metadata signal (see ticket 25).
- While writing the test, removing 149 of 150 freshly created materials and then inspecting segfaulted (not investigated; test was rewritten to avoid it). Worth a diagnosis in identity/scene removal.

## Comments

### 2026-09-28 orchestrator
Resolved by addendum 04 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 140.

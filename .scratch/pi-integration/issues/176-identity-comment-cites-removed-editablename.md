# 176 — Identity.hpp comment cites removed Object::editableName()

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/identity/Identity.hpp

**Kind:** extends

Ticket 153 removed `Object::editableName()`. Every Object name change now goes through `setName()`, which fires `signalObjectMetadataChanged`. The comment above `SceneIdentity::refreshMetadata/refreshAllMetadata` (Identity.hpp ~line 170) still gives `Object::editableName()` as its example of an unsignalled change. Ticket 153 could not edit `src/vsr/integration/` because a concurrent ticket owned it.

Fix: reword the comment. Keep the refreshes as fallbacks for callers without a delegate attached, or drop them if nothing needs them.

### 2026-09-28 orchestrator
Editorial; folded into ticket 139 (comment fix in Identity.hpp). No addendum needed (no behavior change).

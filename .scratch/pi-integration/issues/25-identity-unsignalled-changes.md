# 25 — Decide how identity tracks scene changes that emit no signal

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/identity/

**Kind:** extends

## Assumption taken (ticket 04)

`SceneIdentity` (src/vsr/integration/identity/Identity.hpp) finds changes through the Scene's `MultiUpdateDelegate` and defrag callback. It does not modify `src/vsr/scene/`. Some changes emit no signal:

- `Object::setName` / `editableName()` and metadata setters. **Assumed:** inspection providers call `SceneIdentity::refreshMetadata()` or `refreshAllMetadata()` before they read or return names or metadata. A fingerprint comparison then bumps the inspection revision. Scene and parameter tokens stay unchanged.
- Animation *definitions* (adding or removing bindings in the animation manager, in vsr_app) and layer/layer-node *names*. **Assumed:** these are out of scope for ticket 04. Animation playback changes parameters and transforms, and those changes do emit signals.
- Cameras and renderers are excluded from the scene revision. **Assumed:** they are view state, tracked by `ViewRevisionTracker`.

## Revisit if rejected

If inspection (06) or rendering (12) cannot guarantee that it calls refresh first, add a minimal metadata/name-change signal to `vsr::scene::Object` (a `src/vsr/scene/` hook). Do the same if animation-definition edits must bump the scene revision without host cooperation: add a host-facing `SceneIdentity::noteSceneContentChanged()`, or add an animation-manager hook.

### 2026-09-28 orchestrator
Resolved by addendum 04 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 140.

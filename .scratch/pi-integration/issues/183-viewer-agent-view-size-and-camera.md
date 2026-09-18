# 183 — vsrViewer agent view: fixed size and non-scene camera

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/apps/interactive/viewer/vsrViewer.cpp

**Kind:** extends

Addendum 05 item 4 says that the agent view's fixed camera "initially matches the viewport camera". It does not say what the view's dimensions are or how the camera is represented. Ticket 143 assumed the following:

- The view is a fixed 640x360 (view-owned, within 1..1024). The value is not derived from the viewport size, which is often above 1024 and changes whenever the window resizes.
- The camera is a private ANARI camera on the helide device, not a scene object. At setup it takes the pose of `ctx->view.manipulator`, plus `fovy` from the scene's default camera. When no viewport has framed the scene yet (manipulator distance is infinite), it uses `RenderIndex::computeDefaultView()`. Because the camera is not a scene object, `ViewRevisionTracker` has no camera binding, and the view revision changes only with dimensions.
- The renderer is a private helide `default` renderer.

Revisit if rejected: the view could track the viewport's aspect ratio (clamped to 1024), or use a scene camera object so that inspection and edits reach it. The second option would change the view revision whenever that camera is edited.

### 2026-09-28 orchestrator
Resolved by addendum 07 (ratified as implemented). No follow-up ticket.

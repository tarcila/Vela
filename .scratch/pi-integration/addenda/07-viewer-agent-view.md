# Addendum 07 — vsrViewer agent view: fixed size and private camera

**Extends:** addendum 05 item 4, spec §1 and §12. **Source:** hole 183, raised by ticket 143. **Decided by:** orchestrator, ratifying the implementer's choice.

## Decision
vsrViewer's agent view, `agent`, is a fixed **640×360** helide view. It has its own private offscreen Image Pipeline, RenderIndex and camera. The camera is posed from the viewport manipulator when the integration is set up, or from the default view if the scene has not been framed yet. The camera is not a scene object, so agents can neither inspect nor edit it. The view revision changes when the view's dimensions or pipeline configuration change. The headless host (ticket 145) uses the same pattern: a fixed-size view with a fixed camera.

## Why
- **Fixed size:** interactive viewport sizes often exceed the 1–1024 limit in §12 and change on every resize. Tying the agent view to the viewport would reject renders, or invalidate tokens constantly.
- **Private camera:** the v1 edit surface in §8 excludes cameras. A private camera keeps camera changes out of the agent's reach, as that surface intends.

Exposing a scene camera, or letting the viewport camera follow, is deferred.

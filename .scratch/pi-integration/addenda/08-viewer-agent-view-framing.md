# Addendum 08 — vsrViewer agent view frames the scene bounds

**Extends:** addendum 07. **Source:** hole 213, raised by ticket 202. **Decided by:** orchestrator.

## Decision
The agent view's private camera frames the scene's world bounds, the same way the viewport's reset-view does. It is posed once the scene has bounds: at integration setup if the bounds are already known, otherwise on the first frame that has them. It no longer depends on the interactive viewport having been framed first. The pose stays fixed afterwards (addendum 07), and re-posing changes the view revision.

## Why
In a non-interactive launch (Xvfb, no user input) the viewport has not framed the scene before the agent view is set up, so the agent's first image showed only part of the main object. §Problem Statement requires the first workflow to inspect "a prepared scene and image", and that image should show the main object.

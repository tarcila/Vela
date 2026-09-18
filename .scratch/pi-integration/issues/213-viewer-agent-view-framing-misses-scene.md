# 213 — vsrViewer agent view frames only part of the scene

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/apps/interactive/viewer/vsrViewer.cpp

**Kind:** extends

Found during ticket 202. Under Xvfb, `vsrViewer --agentIntegration -obj <sphere+ground fixture>` was rendered through `vsr_render` / `vsr_render_image`. The 640×360 image is not black. It shows the lower half of the sphere at the top edge, on black, and no ground plane is visible. The main viewport had not framed the scene by the time the view was set up (Xvfb, no interaction). The view camera therefore comes from the manipulator pose or from `computeDefaultView()` (see 183 / addendum 07). That pose does not frame the scene the way the headless host does, which shows a "sphere on grey floor".

Assumption kept for 202: the parity criterion is met, because the object and the edit are visible. Better framing is a quality issue, not a parity failure.

Revisit: set up the agent view camera after the viewport's first reset-view, or frame the scene bounds directly. The ground may also be missing because the default light does not reach it, or because of a winding or normal issue in the fixture. Check the fixture in the main viewport before changing the camera.

### 2026-09-28 orchestrator
Accepted as extends via addendum 08; ready-for-agent.

## Comments

### 2026-09-29 iteration — resolved
The ground was not a lighting or winding problem. The OBJ has no mtl, so both meshes share the `default` material, and the default light lights them. The ground was simply out of frame: the viewport manipulator pose had not been reset yet. `AgentView::frameSceneBounds()` now poses the private camera from `computeDefaultView()` once the world reports finite bounds. That happens at setup when the bounds are already known, and otherwise in `uiFrameStart()`. When `uiFrameStart()` poses it, the view revision is bumped through `notePipelineChanged()` (addendum 08). The pose is independent of the viewport manipulator and is fixed afterwards. `docs/pi-attachment.md` still says "(ticket 213)" in its viewer row; that is stale but outside this ticket's Files.
Verified: `cmake --build _out/_cmake --config Release` → ok; `ctest -C Release` → 34/34 pass. On a fresh `Xvfb :213 -noreset`, ran `vsrViewer --agentIntegration -obj /tmp/t18-fixture.obj` with LD_LIBRARY_PATH = build dir + dev-shell. Driving vsrMcpProxy through /tmp/t213-drive.py (discover → inspect → render) produced /tmp/t213-img.png (640×360), which shows the whole sphere sitting on the whole ground quad. The viewer and Xvfb were killed.
Holes filed: none

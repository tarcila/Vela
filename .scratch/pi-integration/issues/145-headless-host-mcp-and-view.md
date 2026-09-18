# 145 — Make the headless tutorial host serve MCP with a renderable view

**Status:** resolved
**Milestone:** 1
**Blocked by:** 141
**Files:** src/apps/tutorial/

Contract: `.scratch/pi-integration/spec.md` plus addendum 02 (item 4). The spec and the addenda are authoritative; read them first. Business behavior is tested at the public headless integration API, with deterministic clients and a controlled clock.

## Current behavior

`vsrTutorialIntegrationHeadless` is read-only, publishes no descriptor and registers no view (hole 85).

## Desired behavior

- When built with MCP, the host builds the spec §8 procedural fixture: a sphere above a ground plane, matte materials, one directional light and a fixed camera, rendered with helide.
- It starts the loopback listener, publishes the descriptor and prints only its path.
- It registers one helide view with real sync and recovery hooks, grants read/render/edit/resynchronize, and runs the service loop.
- A documented `curl` or scripted loopback session against the running host (commands and output in a ticket comment) shows attach → discover → inspect → edit → render → restore succeeding.

## Out of scope

- Viewer changes.
- The Pi proxy.

## Comments

### 2026-09-28 iteration — failed
Implementer stopped mid-task without a final report or Status change (last note: "extend the script with render → edit → render → restore → detach and rerun against a fresh host"). Uncommitted edits to src/apps/tutorial/vsrTutorialIntegrationHeadless.cpp parked as WIP lzttynzolroo. Retry salvages from it.

### 2026-09-29 iteration — resolved
Salvaged WIP lzttynzolroo verbatim (host was complete; only the verification was missing). The host builds the §8 fixture (sphere above a ground plane, matte materials, directional `sun`) and one 640x360 helide `agent` view with its own offscreen pipeline and fixed camera. Scene grants: read/edit/resync. View grants: read/render. The normal-sync hook confirms and rebinds the RenderIndex world, and recovery repopulates it. MCP registers query/lookup/edit/render tools, publishes the descriptor and prints only its path, then runs service()+pump() until SIGINT/SIGTERM.
Verified: `cmake --build _out/_cmake --config RelWithDebInfo --target vsrTutorialIntegrationHeadless vsrIntegrationTests` → ok; `_out/_cmake/RelWithDebInfo/vsrIntegrationTests` → All tests passed (6135 assertions in 152 test cases); `--exit-after-activation` → prints descriptor path, rc=0.
Live run: `XDG_RUNTIME_DIR=/tmp/ws145-rt LD_LIBRARY_PATH=$PWD/_out/_cmake/RelWithDebInfo:$LD_LIBRARY_PATH _out/_cmake/RelWithDebInfo/vsrTutorialIntegrationHeadless &`. Then ran `/tmp/ws145-drive.sh`, which is ticket 143's curl script (`/tmp/ws143-drive.sh`) with the runtime dir changed and the edit target set to `name.text=="sphere_material"`. Trimmed output:
- stdout: `/tmp/ws145-rt/vsr/vsr-mcp-3493….json`
- initialize → `{"name":"vsr","version":"1"}`, followed by attach ok.
- tools/list → vsr_discover, vsr_inspect, vsr_operation_status, vsr_cancel, vsr_edit, vsr_restore, vsr_render, vsr_render_image, vsr_resynchronize
- discover → scene 0 {read,edit,resync}; view 0 {read,render} with view_revision
- inspect → ok, 13 items (4 arrays, surfaces sphere/ground, geometries sphere/triangle, materials default/sphere_material/ground_material, light sun, camera default)
- render → ok 640x360 capture 1; vsr_render_image PNG 640x360 RGBA (red sphere on grey plane, checked visually)
- edit sphere_material color → [0,0,1] → ok, committed, and it returns a batch_ref, scene_revision …1.14 and version 2.
- render at new token → ok, capture 2. A render at the old token → `superseded`.
- restore batch → ok, committed, scene_revision …1.15, version 3
- resynchronize → {ready:true,state:ready}; the vsr/detach call → `{}`.
The host was killed with SIGTERM and logged "integration stopped". No host process remains.
Holes filed: none

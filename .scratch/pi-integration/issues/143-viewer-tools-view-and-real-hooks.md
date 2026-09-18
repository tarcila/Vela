# 143 — Give vsrViewer MCP tools, a renderable helide view and real synchronization hooks

**Status:** resolved
**Milestone:** 1
**Blocked by:** 141, 142
**Files:** src/apps/interactive/viewer/vsrViewer.cpp, src/apps/interactive/viewer/CMakeLists.txt

Contract: `.scratch/pi-integration/spec.md` plus addendum 05 (item 4). The spec and the addenda are authoritative; read them first. Business behavior is tested at the public headless integration API, with deterministic clients and a controlled clock.

## Current behavior

With `--agentIntegration`, vsrViewer:
- lists no MCP tools (hole 135);
- registers no view;
- grants no render;
- has normal-sync and recovery hooks that return `true` unconditionally (hole 110).

## Desired behavior

With `--agentIntegration`:
- vsrViewer registers the query, lookup, edit and render/resync tool families.
- It registers one view backed by a helide device with its own offscreen Image Pipeline, whose fixed camera initially matches the viewport. It grants read/render/edit/resynchronize on the scene and view.
- Its normal-sync hook and recovery callback confirm that the view's RenderIndex/pipeline is updated for the current scene. They return false on failure rather than `true` unconditionally.

Verified by starting the viewer with no display (use the existing offscreen or headless mechanism if there is one; otherwise a documented `curl` loopback session against a real viewer started under a virtual display if available):
- `tools/list` returns the tools;
- a curl-driven attach → discover → inspect → edit → render → restore sequence succeeds.
If no display is available, document the exact commands and the result obtained in a ticket comment. Also verify vsrViewer builds with the integration and MCP off.

## Out of scope

- The Pi proxy (ticket 144).
- Changing the interactive viewport's rendering.

## Comments

### 2026-09-29 iteration — resolved
vsrViewer with `--agentIntegration` now registers query/lookup/edit/render tool families, one `agent` view (helide device, own offscreen ImagePipeline with an AnariSceneRenderPass, fixed perspective camera copied from the viewport manipulator — falling back to the RenderIndex default view when no viewport has framed yet — 640x360), and grants read/edit/resync on the scene and read/render on the view. Normal-sync hook re-confirms the view's RenderIndex world and binds it into the pipeline; recovery repopulates the RenderIndex then confirms; both return false when the index/pipeline/world is missing. RenderService is pumped at the viewer safe point.
Verified: `cmake --build _out/_cmake --config Release --target vsrViewer vsrIntegrationTests` → ok; `_out/_cmake/Release/vsrIntegrationTests` → All tests passed (6529 assertions in 146 test cases); integration OFF: `cmake -S . -B _out/_cmake_off -G Ninja -DCMAKE_BUILD_TYPE=Release -DVSR_USE_INTEGRATION=OFF -DVSR_BUILD_APPS=ON -DVSR_BUILD_INTERACTIVE_APPS=ON -DVSR_BUILD_UI_LIBRARY=ON -DVSR_USE_CUDA=OFF && cmake --build _out/_cmake_off --target vsrViewer` → rc=0.
Live run: `Xvfb :143 &`, then `DISPLAY=:143 XDG_RUNTIME_DIR=/tmp/ws143-rt LD_LIBRARY_PATH=_out/_cmake/Release:$LD_LIBRARY_PATH _out/_cmake/Release/vsrViewer --agentIntegration` (default empty scene); a curl script read the descriptor (host/port/path/launch_credential) and POSTed JSON-RPC with `Host`, `Authorization: Bearer <cred>`, `VSR-Session-ID`/`VSR-Session-Proof: Bearer <secret>`. Results: initialize ok; vsr/attach ok; tools/list → [vsr_discover, vsr_inspect, vsr_operation_status, vsr_cancel, vsr_edit, vsr_restore, vsr_render, vsr_render_image, vsr_resynchronize]; discover → scene 0 {read,edit,resync} + view 0 {read,render} with view_revision; inspect → ok (default material + camera); render → ok 640x360, vsr_render_image PNG 640x360 RGBA; edit material color [1,0,0]→[0,0,1] → ok committed with batch_ref + new scene_revision; render at new token → ok (capture 2); render at old token → superseded; restore → ok committed; re-inspect → color [1,0,0]; resynchronize → {ready:true,state:ready}; vsr/detach ok. A second attach while the first session is leased → `occupied` (expected). Scene had no geometry, so images show background only.
Holes filed: 183-viewer-agent-view-size-and-camera

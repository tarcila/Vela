# 202 — Rerun the Pi workflow on vsrViewer with visible geometry

**Status:** resolved
**Milestone:** 1
**Blocked by:** 201
**Files:** docs/pi-attachment.md

Split from ticket 18. The headless half of spec §14 gate 5 passed through Pi and `vsrMcpProxy`. On vsrViewer the same Pi workflow ran mechanically: discover, inspect, render, image, edit, fresh render, restore and detach all committed. Every image was black because of ticket 201, so visual parity was not shown. The viewer-side second-proxy, idle-renewal and abandonment checks were not rerun either. They were run only against the headless host.

## Acceptance
- [x] Once 201 is fixed, start `vsrViewer --agentIntegration` on a fixture with a sphere, a ground plane and a light, under Xvfb. A separate `pi -p --mcp-config` run must complete inspect → image → edit → fresh render → restore → detach, and the images must show the object and the change.
- [x] Second `vsrMcpProxy` refused, idle > 120 s kept, SIGKILL + lease expiry frees the slot (reuse `.scratch/pi-integration/t18-lease.sh`).
- [x] Update the vsrViewer column of the table in docs/pi-attachment.md.

## Comments

### 2026-09-29 iteration — resolved
Ran `vsrViewer --agentIntegration -obj /tmp/t18-fixture.obj` (sphere, ground and the default light) on a fresh `Xvfb -noreset` display, using Release build-tree binaries. LD_LIBRARY_PATH must be the build dir *plus* the dev-shell path; with only the build dir, helide fails to load and no descriptor is published. A separate `pi -p --mcp-config /tmp/t202-mcp.json --no-builtin-tools` run (Sonnet 5) did inspect (10 objects) → render and image (red sphere, not black) → edit of the `default` material to [0,1,1] → fresh render (cyan sphere; I checked the PNGs extracted from the session) → restore (back to [1,0,0]) → detach (a fresh proxy attached right after). The lease checks ran on a second viewer instance, in parallel with the Pi run: second proxy `attach failed`; SIGKILL, then denied at 2 s and attached at 127 s; stdio `vsr/attach` returned `method not found`. The idle check was redone with a valid `request_id`, because t18-lease.sh's discover lacks one and returns `invalid_request`: at 152 s idle a second proxy was still refused and discover was accepted under the same session_id. Framing is poor: the sphere is cut off at the top and the ground is not visible. Filed as 213; it does not affect parity. I updated the docs/pi-attachment.md viewer column and open items. All viewers, Xvfb instances, proxies and scripts were killed.
Verified: `cmake --build _out/_cmake --config Release --target vsrViewer vsrMcpProxy` → ok. The pi run → rc=0, with a 5-step table. `grep -c` for the launch_credential, Bearer, secret and proof over the session JSONL, stdout and stderr → 0/0/0/0 each. /tmp/t202-lease.log and /tmp/t202-idle.log as above.
Holes filed: 213-viewer-agent-view-framing-misses-scene

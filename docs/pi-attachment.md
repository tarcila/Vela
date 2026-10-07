# Attaching Pi to a VSR application

Status: **works end to end on the headless host and on vsrViewer.** The
vsrViewer column was rerun after ticket 201 (ticket 202). Nothing here claims
a result that was not observed.

## Prerequisites

- **Model:** any Pi model that can call tools. The historical runs below used
  `nvidia-inference/us/azure/anthropic/eccn-claude-sonnet-5`.
- **Images:** `vsr_render_image` returns PNG. Judging "is the object clearer"
  needs an **image-capable** model (the `images` column of
  `pi --list-models`). Text-only models can run the workflow but cannot see
  the result.
- **Pi MCP support:** current Pi has built-in MCP support; installing
  `pi-mcp-adapter` is not required. A legacy extension that registers `/mcp`
  replaces the built-in support, so disable that extension if using the
  procedure below.
- **VSR:** a build with `VSR_USE_INTEGRATION` and `VSR_USE_INTEGRATION_MCP`,
  which also builds `vsrMcpProxy` (see [tool documentation](../src/apps/tools/README.md)).
  Agent rendering uses a real device: `helide` on the headless host; on
  vsrViewer the first `VSR_ANARI_LIBRARIES` entry, else `ANARI_LIBRARY`,
  else `helide`, which an agent can change with `vsr_select_device`. Install freshly built libraries before
  testing, because an installed-library search path can override build-tree
  lookup. For example, after configuring those options:

  ```bash
  cmake --build _out/_cmake --parallel
  cmake --install _out/_cmake
  ```

  Substitute your build directory if it is not `_out/_cmake`.
- **Why the VSR proxy is still needed:** `vsrMcpProxy` privately handles VSR
  attachment, session proof, independent renewal, and detach. Built-in Pi MCP
  support connects to this stdio proxy, not directly to the loopback HTTP URL.
  Pi is a client; it is not embedded in the VSR application.

## Attachment procedure

### 1. Start the application and get its descriptor path

For a display-free test with a procedural sphere and ground scene, leave this
running in one terminal:

```bash
./_out/_cmake/vsrTutorialIntegrationHeadless
```

Or open your own scene in the viewer:

```bash
./_out/_cmake/vsrViewer --agentIntegration -obj /path/to/model.obj
```

The **descriptor is a JSON connection file created by the application**, not
something you write yourself. It contains the loopback endpoint and a private
launch credential. The headless host prints its path; the viewer shows it in
**Agent Activity** and logs it as `[vsrViewer] MCP descriptor: <path>` (grep
the log for `MCP descriptor:`). On Linux it normally looks like:

```text
/run/user/1000/vsr/vsr-mcp-0123456789abcdef0123456789abcdef.json
```

The actual user ID and launch ID will differ. Copy the exact path for the
running instance. If needed, list paths without reading their contents:

```bash
ls "$XDG_RUNTIME_DIR"/vsr/vsr-mcp-*.json
```

The directory is 0700 and the file is 0600. Never paste the file's contents
into a prompt or session: the proxy, not the model, reads the credential.
Restarting the application creates a new path. Orderly shutdown removes the
file; a crash can leave an inert stale file. Do not automatically select the
first file if several application instances have published descriptors.

### 2. Configure Pi to launch the proxy

Merge this entry into `~/.pi/agent/mcp.json` (user-level), or `.pi/mcp.json`
(project-local). Preserve any other servers already configured:

```json
{
  "mcpServers": {
    "vsr": {
      "command": "/absolute/path/to/_out/_cmake/vsrMcpProxy",
      "args": ["/run/user/1000/vsr/vsr-mcp-0123456789abcdef0123456789abcdef.json"],
      "exposure": "direct"
    }
  }
}
```

Both paths above are examples: replace the executable path with your build's
proxy and the argument with the current application's descriptor path. The
config contains only the path, never a copied credential. Keep launch-specific
configuration local; do not commit it to the repository.

Start Pi from the same dev-shell environment as VSR so the proxy inherits its
library search paths. If you configure `env.LD_LIBRARY_PATH` explicitly,
**preserve the existing paths as well as adding the build/library directory**;
replacing them can hide `helide` and prevent view setup.

Project-local config requires project trust. Approve only a project you trust;
for a one-shot run, `pi --approve -p "<prompt>"` explicitly grants that trust
for the process. User-level config does not require project trust. The old
extension-specific `--mcp-config` procedure in the historical results below
is not needed for built-in MCP support.

### 3. Connect and exercise the workflow

Start a fresh `pi` session. Use `/mcp` to check the `vsr` connection and its
nine tools. After editing configuration in a running session, use `/reload`
and inspect `/mcp` again. Direct exposure declares tools with names such as
`mcp__vsr__vsr_discover`; no separate tool-search step is required.

For a smoke test, ask:

> Using only the VSR MCP tools, discover the scene and view, inspect the
> materials, and render a baseline image. Change one material's color using
> fresh revision tokens, render again, then restore that edit batch and
> verify the original color is back. Report the commit and restoration
> outcomes. Poll operation status until each admitted operation is terminal.

Expect a nonblank baseline, a visibly changed material color, and the original
color after restoration. Use an image-capable model to judge the images.
The viewer's Agent Activity window records the same operations.

The tools are `vsr_discover`, `vsr_inspect`, `vsr_operation_status`,
`vsr_cancel`, `vsr_edit`, `vsr_restore`, `vsr_render`, `vsr_render_image`, and
`vsr_resynchronize`. See the [carrier and tool contract](../src/vsr/integration/mcp/README.md)
for argument schemas, revision tokens, and stable outcome codes.

### Editing object parameters

Object edits validate existing parameters by stored type and introspected
metadata, not a matte/directional allowlist. Inspection parameter entries
expose an `editable` Boolean, typed `min`/`max` values and `string_values`
choices when present. These describe value support, not permission; keep the
existing scene edit grant and copy fresh object handles and integer versions.

Supported values are stored `BOOL` as JSON Boolean; `STRING` without embedded
NUL, enforcing enum choices when present; signed/unsigned 8-, 16-, 32-, and
64-bit integer scalars/vec2/vec3/vec4 as integral JSON numbers within both type
range and `[-(2^53-1), 2^53-1]`; and float32/float64 scalars/vec2/vec3/vec4
as finite JSON numbers within type range. Vectors require exactly the stored
component count. Matching `min`/`max` metadata applies per component, and
`COLOR` usage retains `[0,1]`. Parameter `enabled` is preserved and does not
restrict value editing. The five stored float32 matrices are also editable:
MAT2/MAT3/MAT4 use exactly 2×2/3×3/4×4 nested row arrays; MAT2x3/MAT3x4
use exactly 2×3/3×4 (rows × columns). The stored type selects the shape, with
packed index `column*rows+row`. Matching matrix bounds apply component-wise
before and after ties-to-even binary32 conversion. Object matrices may be
non-affine and accept no `space` argument; node transforms remain affine.
References, double matrices, other types, and parameter creation are unsupported.
Not every readable value is writable: the legacy `values` mirror covers generic
supported float32 scalar/vec2/vec3/vec4 values,
not exclusive edit authority. Stored camera edits do not control the viewport
or agent capture camera. Atomicity, versioning, and conditional restore remain
unchanged; see the [live placement guide](live-placement-editing.md).

### Controlling and recovering the interactive camera

Enable **Allow agent camera control** in Agent Activity for this launch only.
It is separate from scene edits and resets off on every launch. Discover the
actual interactive view ID; `camera_control.supported` and `.enabled` are
separate facts. Never substitute the independent agent view.

Call `vsr_set_view_camera` with a fresh `request_id`, that `view_id`, world-space
`position`, forward `direction`, `up`, and optionally vertical `fovy_deg` in
(0,180). No EDIT or RENDER grant is needed for a supported interactive viewport;
view and bound-Scene READ still apply. Poll the `set_view_camera` operation.
Initial permission denial is HTTP 403 / `camera_control_disabled`, with no ID
consumed; revocation before execution yields a noncommitting terminal outcome.
The receipt includes effective camera, Scene/view revisions and view identity.
It is not proof that a new frame has appeared.

Camera Poses has a host-owned **Before agent** entry for each controllable view.
Successful fresh moves replace only that entry, not a user pose with the same
name. Ordinary navigation does not clear it. Selecting it restores the previous
complete controller/camera configuration, including parameter values and enabled
flags, and advances the view revision without creating another slot.
Retired/rebound identities are unavailable. Recovery is local and remains usable
when camera permission is off or the agent disconnects.

Use the addressed viewport's presented-frame tools for visual verification,
only on a qualified binding. Require matching view identity, rendered view
revision and camera from `anari_render_start`, matching chosen Scene revision,
and `stale=false`. Poll with fresh IDs for at most 120 seconds. Timeout,
superseded poses and unavailable bindings are not successful visual acceptance;
an independent render is not evidence of the user's viewport.

For isolated controller verification after a fresh CPU build/install, run
`bash scripts/test-viewport-camera.sh build-agent`. It creates a separate
private configuration/runtime and authenticated Xvfb display, exercises the
real viewport binding plus orbit/pan/zoom and recovery, and leaves any existing
viewer/unsaved Scene untouched. Set `VSR_CAMERA_TEST_DISPLAY` if display 93 is
occupied. Private Xvfb uses `-noreset` because reset-enabled startup was
intermittent on this host; readiness is authenticated and bounded, not a fixed
startup delay. This default selection tests controller behavior only.

For receipt-to-presentation acceptance on the existing Linux SDL CPU-ready
binding, after build/install run:

```bash
VSR_CAMERA_TEST_FILTER='[.viewer-camera-frame]' \
  bash scripts/test-viewport-camera.sh build-agent
```

The test warms the natural viewport, commits an agent move, chooses the
post-move Scene revision, and polls fresh frame operations for at most 120
seconds. It checks the exact view identity and receipt revision, rendered
camera/projection/FOV, Scene revision, `anari_render_start` and `stale=false`,
retrieves the PNG, then restores Before agent. Its `CAMERA_FRAME_ACCEPTANCE`
line records the actual host/SDL renderer/device/binding and both receipts.
Helide selection alone is not qualification: the existing `VIEWER_CPU_READY`
source must acquire the real final `host_sdl_upload` buffer and complete the
checked SDL presentation. Unknown/interop uploads, unsupported display scale,
unavailable results and timeout do not pass. No independent render or desktop
capture is used. The normal suite supplies loopback delayed/stale frames,
second-move and user-input supersession, historical replay, timeout and
unavailable-source contracts.

### Inspecting Images

Array items retain format summaries and add actual `mapped` state plus
`image_info`. Available `image_info` carries an opaque equality-only
`data_version`, dimensions, ANARI element type, channel count, Color Space and
`row_order: top_down`. Unsupported/unavailable states carry a reason and omit
those payload fields. Availability is not write permission, and a mapped Image
can still be supported. Ordinary host writes advance the completed token;
inspection and read-only access do not. Sampler image references remain Array
handles with summaries.

PNG Image Export/Import is not implemented in this tracking slice. Discovery
reports that truthfully; do not infer file-exchange tools from Array support.
See [Image tracking](image-data-tracking.md) for host prerequisites and cache
lifetime semantics.

### Editing a placement's world position

For complete local/world examples, filters and bounded cursor flow, node versus
object identity, typed/unavailable values, and Restore controls, see the
[live placement guide](live-placement-editing.md). Its edit examples explicitly
map inspection's `node_edit_version` to the entry's `version` and explain when
`structural_version` and `ancestry_version` are required.

Scene objects own parameters; layer nodes place those objects. To move a
placement, inspect `kind: "nodes"` and use its `handle`, `node_edit_version`,
`structural_version`, and `ancestry_version`. The scene must explicitly expose
node edits. A `vsr_edit` node entry uses `target_kind: "node"`, those tokens
(`version` carries `node_edit_version`), `parameter: "position"`, a three-number
`value`, and `space: "world"`. Omitting `space` means local coordinates.

World position is the location of the node's **local origin**, not its bounds or
geometric center. Conversion uses the proposed final ancestor matrices, so
parent/child entry order does not change the result. Only translation changes;
shear/reflection in the node's linear part is preserved. Parents must be finite,
affine and invertible, and the converted local point must fit float32. Singular
parents reject without repair or fallback. World rotation, scale and matrix
replacement are not supported.

Reinspect after ancestor transform writes, including change-back: stale world
ancestry conflicts, while unrelated sibling edits do not. Restore validates the
batch's post-commit ancestry, so a parent/child batch is immediately restorable;
a subsequent ancestor transform write prevents restoration, even if changed
back. Restore is conditional and one-way, not general undo.

### 4. Disconnect and reconnect

Exit Pi to detach the proxy and release the controller session; it logs
`vsrMcpProxy: detached`. Only one client can hold the session at a time. A
second proxy fails with `vsrMcpProxy: attach failed` / `occupied`.

After restarting the application, update the descriptor argument before
reconnecting. If the host or proxy was killed rather than shut down normally,
a stale descriptor or an unexpired controller lease can remain; see the
historical lease checks below.

## What was observed (2026-09-29)

Setup: build-tree Release binaries with an explicit `LD_LIBRARY_PATH`, and a
private `XDG_RUNTIME_DIR` under `/tmp`. Each workflow ran as its own
`pi -p --mcp-config … --no-builtin-tools` process with the Sonnet 5 model.
The viewer ran as `vsrViewer --agentIntegration -obj <fixture>` on a fresh
`Xvfb -noreset` display. The fixture was an OBJ with a `sphere` and a
`ground` mesh; the viewer adds its default directional light. The viewer's
`LD_LIBRARY_PATH` must keep the dev-shell entries after the build dir,
otherwise the `helide` library is not found and the agent view is not
created (`agent view setup failed (helide)` in the log, and no descriptor).
The viewer lease checks ran against a second viewer instance, because the
slot is single-client and the Pi run held the first one.

| Check | Headless host | vsrViewer |
|---|---|---|
| App started before Pi, descriptor published | yes | yes |
| Pi → proxy attach; tool list (9 tools) | yes | yes |
| `vsr_discover` → scene 0 + view 0 with revisions | yes | yes |
| `vsr_inspect` | 13 items (sphere, ground, materials, sun, camera) | 10 objects (2 arrays, `sphere` and `ground` surfaces and geometries, `default` material, `mainDistantLight`, camera, renderer) |
| `vsr_render` + `vsr_render_image` (PNG sent to the model as an image) | yes. The model described a "muted red sphere on grey floor" | yes. 640×360 PNG, not black: a red sphere against black. At the time the camera framed only the lower part of the sphere and the ground was out of frame; fixed since, see below |
| Edit to make the main object clearer | `sphere_material.color` [0.8,0.1,0.1] → [1,0.75,0]; committed, version 1→2 | `default` material color [1,0,0] → [0,1,1]; committed, version 1→2 |
| Fresh render at the new scene revision | yes. The model saw a "vivid golden-yellow sphere" | yes. The sphere is now bright cyan; the model and a pixel check (red channel 0, green/blue up to 235) both confirm it |
| `vsr_restore` of the batch, then re-inspect | committed (version 3), original value back | committed (version 3), original value back |
| Detach on Pi exit | yes (`vsrMcpProxy: detached`) | yes. A fresh proxy attached seconds after Pi exited, well inside the 120 s lease |
| Launch credential in the Pi session JSONL or stdout/stderr | 0 hits | 0 hits |
| `Bearer`, `secret`, `proof` in the Pi session JSONL | 0 hits | 0 hits |
| Second client while held (raw `vsr/attach` with launch auth) | `occupied` | `occupied` (ticket 143) |
| Second `vsrMcpProxy` while held | exits with `attach failed` | exits with `attach failed` |
| Stdio client sends `vsr/attach` through the proxy | refused: `method not found`, not forwarded | refused: `method not found`, not forwarded |
| Idle renewal: proxy idle 150 s (> 120 s lease), no traffic | session kept. The slot was still held at 152 s and no renewal failure was logged | session kept. A second proxy at 152 s got `attach failed`, and a `vsr_discover` after the idle period was accepted under the same `session_id` |
| Abandonment: proxy SIGKILLed without detach | new attach 2 s later denied; new attach 127 s later **succeeds** (lease expired) | same: denied 2 s later; **succeeds** 127 s later |

Only the public `session_id` shows up in model-visible output, inside
`operation_ref` objects, as spec §5 intends. The session secret never leaves
the proxy's memory.

The idle, abandonment and second-proxy checks drove `vsrMcpProxy` directly over
a FIFO rather than through a model (`.scratch/pi-integration/t18-lease.sh`). The host
has no way to shorten the lease, so the checks used the real 120 s lease with
30 s renewal.

`t18-lease.sh` sends `vsr_discover` without `request_id`, so its tool calls
come back `invalid_request`. They still exercise the transport, but not
session validity. The viewer idle check therefore repeated the run with a
valid `request_id`, and both calls were accepted.

## Resolved since the run

- 213 (addendum 08): the vsrViewer agent view's private camera now frames the
  scene's world bounds, like the viewport's reset-view. It is posed at setup
  when the bounds are known, otherwise on the first frame that has them, and
  no longer waits for the interactive viewport to be framed. The ground was
  never a lighting or winding problem, only out of frame. A rerun on Xvfb
  with the same fixture showed the whole sphere on the whole ground quad.

## Open items

None.

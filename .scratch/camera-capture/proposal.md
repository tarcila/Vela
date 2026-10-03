# DRAFT — Camera & Capture increment (proposal, NOT an approved spec)

Status: historical design proposal, now synthesized into [spec.md](spec.md),
marked ready-for-agent after the user approved the recommended first scope.
The spec is the implementation contract; exploratory choices/caps below are
not hidden requirements or outstanding blockers. No tickets or capture code
have been produced. Capture behavior is not runtime-verified. The initial planner stopped before returning a report;
this retained draft has been checked/corrected against source by the parent.
Existing CUDA algorithms are now independently runtime-verified, not this
proposed capture implementation.
Scope: gaps 3, 7, 10, 11, 12 of `.scratch/vsr-mcp-gaps.md` (follow-on to the
completed `.scratch/broad-editing/spec.md`, which put these out of scope).

## 1. Code facts (as read)

There are three camera ownership roles. The viewport can bind a selected Scene
camera to its manipulator/render pass; the agent camera is separate. These
bindings must be explicit rather than inferred from an arbitrary camera object:

| Thing | Owner | Inspectable today | Drives |
|---|---|---|---|
| Scene camera objects (`Scene::defaultCamera()`, ANARI_CAMERA pool) | Scene / source archive | yes, parameter values via `vsr_inspect` (broad-editing) | can drive selected interactive viewport cameras through host bindings; editing an arbitrary stored camera does not guarantee control of a particular registered view |
| Interactive viewport camera (Viewport/BaseViewport manipulator plus selected Scene camera) | vsrViewer UI/host binding | selected Scene object is readable; binding/manipulator state is not a registered-view contract | main view(s) on the user-selected Device Identifier (e.g. barney) |
| Agent view camera (`AgentView::camera` in `src/apps/interactive/viewer/vsrViewer.cpp`) | vsrViewer integration host | no (only folded into view revision) | `vsr_render` only |

- `AgentView` loads **hard-coded `helide`** (`loadDevice("helide")`), its own RenderIndex, renderer `"default"` with default params, own ImagePipeline. Fixed 640×360. It copies only `fovy` from the scene default camera; pose is framed once from world bounds (`frameSceneBounds`) and never moves. So agent renders do **not** have viewport parity (gap 5's background mismatch is a symptom).
- `RenderService` (`src/vsr/integration/render/RevisionRender.hpp`): one `RenderBackend` per view; dims view-owned, `MAX_RENDER_DIMENSION = 1024`, requests carrying `width/height` are rejected; one frame in flight per view (busy otherwise); 30 s deadline; release() may defer indefinitely; late images never succeed.
- `AgentView::begin()` is **synchronous on the UI safe point**: two `pipeline.render()` calls. `poll()` is DONE immediately. N samples via this path would block the viewer for N frames.
- `ViewRevisionTracker` (`identity/Identity.hpp`) folds camera handle + params, renderer, dims, pipeline into the view revision; `setCamera` expects an `ObjectHandle` (scene object), but the agent camera is a raw ANARI handle, so viewer uses `notePipelineChanged()` when posing.
- Bounds: only `RenderIndex::worldBounds()` = ANARI world `bounds` property with `ANARI_WAIT` (device-dependent, may block / empty). No per-node bounds API; no CPU-cached geometry bounds in vsr_scene.
- Image transport: PNG base64 in MCP `image` content (`RenderTools.hpp`), never a file path. Limits: activity retained-image budget 32 MiB (`Activity.hpp`), proxy response cap 16 MiB (`McpProxy.hpp` `maxResponseBytes`), MCP request body 64 KiB. 1280×720 RGBA is 3.69 MB before encoding (about 4.92 MB base64 plus PNG/JSON overhead). 1920×1080 is 8.29 MB raw/about 11.06 MB base64 and can fit the 16 MiB response cap; 2048×2048 worst-case RGBA cannot. Budget complete response bytes and encoder overhead, not axis limits alone.
- ADR 0031 (Studio: no wire file transfer, files server side) concerns SciVis Studio; this increment does not touch Studio Shots or the source archive.
- Offline workaround (`.scratch/scripts/render_checkmatte.lua`) uses a separate process/device and a synchronous `renderToFile` loop of 256 completed pipeline renders. That binding treats denoise as a renderer parameter without verifying backend support. Completed frames, requested renderer pixelSamples, and actual accumulated samples are not universally identical; capture must report its quality contract honestly and reject unsupported requested features.

## 2. Recommendation (summary)

**Immutable per-request capture first; persistent agent-view camera second; never touch the interactive viewport camera.**

A new operation `vsr_capture` = `{request_id, view_id, scene_revision, view_revision, camera: {position, direction, up, fovy_deg} | "view", width, height, samples, denoise?}`.
The camera is a value in the request, applied to a **capture-owned** ANARI camera on the agent view's device, never written into the scene, the agent view's persistent camera, or the viewport manipulator. Result carries provenance and the exact camera used.

Why: no restore/undo story is needed for temporary capture values, the interactive
camera stays unchanged, and a queen's-eye image needs no persistent edit.
Existing request-ID/canonicalization/replay machinery still must cover every
camera/quality/policy input; idempotency is not automatic. Persistent agent-view
camera editing is a later separately authorized convenience, not a prerequisite.

## 3. Design decisions (proposed)

- **Camera value type** — `{position, direction, up, fovy (degrees), aspect derived from width/height}`; validated finite, non-zero, direction·up not parallel, fovy in (0,180). Optional helper `look_at` (eye+target) normalized server-side. Echo the normalized camera in the result.
- **Capture resources are separate from the interactive frame** — prefer a
  capture-owned camera, renderer, frame and offscreen pipeline on the registered
  view's actual device/RenderIndex, conditional on a qualified host adapter.
  Sharing avoids duplicate scene uploads but not contention/shared-world risks.
  Immutable request values do not freeze the scene: preserve revision/readiness
  guards and supersede changed work. One capture per view; cancellation/shutdown
  retain reservations and held capacity until safe drain/release, even after
  partial setup failure.
- **Progressive, qualified sampling** — qualify setup/commit, submission,
  polling, mapping/copying, encoding and cleanup, not merely one render call per
  pump. Current AgentView/pipeline waits cannot be reused unchanged. Distinguish
  requested quality, completed frames, and backend-confirmed accumulated samples;
  never call 256 pipeline polls 256 samples. Unsupported/unresponsive bindings
  reject or remain unexposed, rather than freezing the UI or silently degrading.
  Keep revision checks throughout and at completion; no late success.
- **Capture policy (host-configured, advertised in authorized discovery/view info and tool metadata, not the strict v1 launch descriptor)** — initial caps are product proposals, not approved values. The 1280×720/256-quality target must fit. Budget pixels, every allocated frame/AOV channel, device/host scratch, normalization, PNG/base64/JSON copies and retained results; a capture is not necessarily one RGBA framebuffer. Known size/quality/memory failures reject before admission. Actual encoded-size failures are known after encoding and must terminate honestly without partial image success; reserve a conservative upper bound before rendering if pre-admission size guarantees are promised.
- **Truthful provenance** — result includes `device_identifier` actually used, renderer subtype, samples completed, denoise applied, `parity: "agent_device"`. If the agent view's device differs from the viewport's device, say so; never fall back silently. If the requested/configured device fails to load → `unavailable`, not helide.
- **Agent device choice** — prefer an explicit host binding to the selected viewport's actual Device Identifier/renderer when parity is desired, and report any configured difference. `loadDevice` caches by parsed Device Identifier and returns the existing retained device; it does not automatically create a separate instance. `acquireRenderIndex` is keyed by actual device handle and rejects scene mismatch. Prefer capture-owned camera/frame/pipeline on an explicitly suitable existing device/index; measure any deliberate independent-device/index allocation rather than assuming it is free.
- **Renderer params** — capture copies the *main viewport's* renderer parameters only if Product Choice C approves; otherwise uses agent renderer and says so.
- **Bounds (gap 3)** — scene and selected-node/subtree bounds should be cached/provider-owned observations with explicit provenance, revision and unavailable reasons. Per-node bounds are important to the queen's crown/eye-placement workflow, not just a cosmetic later feature. Host-resident geometry can be scanned incrementally within work/scratch budgets and transformed through node dependencies; device-only/proxy data without host-provided bounds stays unavailable. `ANARI_NO_WAIT` avoids requesting a wait but does not prove a driver property call is bounded or GPU-work-free. Any driver-bound acquisition needs its own explicit host lifecycle; do not disguise it as cheap inspection.
- **Image file saving (gap 12)** — client side: extend `vsrMcpProxy` (trusted adapter, runs as user) with an opt-in `--save-dir <dir>`; a proxy-local tool `vsr_save_image {operation_ref, name}` fetches via `vsr_render_image`, decodes, writes `<dir>/<sanitized name>.png` (no path separators, no overwrite unless flag). Server gains no filesystem rights.
- **Permissions** — immutable capture should retain the existing view read/render authority plus explicit host exposure and advertised limits; a new authorization capability is not automatically necessary. Persistent camera edits would require separately agreed write authority rather than silently borrowing render permission. No new authentication mechanism, Lua/shell, broader material writes or server filesystem authority is proposed.
- **Domain terms** — use "agent view", "capture", "Device Identifier", "Image Pipeline". Do not call it "Shot" (Studio term) or "screenshot of viewport".

## 4. Product choices needing approval

A. **Phase order**: per-request immutable capture first (recommended), persistent agent-view camera (`vsr_set_view_camera` with view revision CAS + `vsr_reset_view_camera`) second — or both together?
B. **Agent device**: match main viewport's Device Identifier by default (parity, more GPU memory, e.g. barney/CUDA) vs keep helide default and report it?
C. **Renderer parameter source** for captures: mirror viewport renderer params / scene renderer object / agent defaults?
D. **Capture caps** default values (proposed 2048/axis, 4 MPix, 1024 spp, 120 s, 8 MiB PNG).
E. **File save** via proxy `--save-dir` only (recommended), or also a server-side allow-listed dir (requires explicit host flag)?
F. **Bounds phase 1** world-only vs including per-node CPU bounds.

## 5. Vertical slices

1. **Truthful agent view provenance** — report Device Identifier/renderer in view descriptor and render result; make device configurable (no silent helide). Verify: test_IntegrationMcpTools asserts provenance fields; failed device load → view not registered / `unavailable`.
2. **Capture camera + dims, 1 sample** — `vsr_capture` with camera value and per-request dims within policy; capture-owned ANARI camera; main viewport untouched. Verify: headless tutorial (`vsrTutorialIntegrationHeadless`) oracle below; unit tests for validation and pre-admission rejects.
3. **Progressive samples + deadline/cancel** — PENDING across pumps, cancellation and supersession release resources; no late success. Verify: fake backend tests counting begin/release; busy until release.
4. **Encoded-size policy** — `item_too_large` before transport; proxy 16 MiB never hit by policy. Verify: test with oversized synthetic image.
5. **Proxy save helper** — `vsr_save_image` in `vsrMcpProxy`, sanitized names, opt-in dir. Verify: test_IntegrationMcpProxy with temp dir; rejects `../x`, absolute paths, overwrite.
6. **Cached scene/node bounds** sufficient to place an eye above a selected
   piece: bounded host-resident geometry/provider path with explicit unavailable
   status and revision provenance. Verify independently worked transformed
   geometry bounds and no hidden GPU downloads/waits. Backend property acquisition
   is a separate qualified lifecycle, not a promise from NO_WAIT.
7. (Phase 2) persistent agent-view camera with view-revision CAS and reset.

## 6. Automated oracle for "capture from queen" (no main-view perturbation)

Headless: use a synthetic scene with distinguishable objects (e.g. red sphere
at A and blue box at B), explicit known lighting or suitable emissive surfaces,
and an independently worked camera/ray oracle. Put the camera outside the
sphere's bounds (above/in front of A), looking at B, not inside A's geometry.
At 64×36 on a qualified device:
- a known region sees blue; a deliberately different camera sees the independently
  expected other object/background rather than relying on an aesthetic guess;
- result echoes normalized camera, dims, samples, device;
- scene revision, agent view revision and a stand-in "main" manipulator/camera parameters are bit-identical before/after;
- repeat with stale revision → superseded, no backend start.
For the real chess scene: manual/QA only (not claimed here).

## 7. Risks

- Independent ANARI devices/indexes may duplicate scene resources; shared-device captures can still contend with the viewport. Measure device/frame/index costs and concurrency before choosing defaults. MAX_STREAM_EVENTS=100K is a Pi subagent output limit, not a CUDA stream/event resource limit. Actual GPU algorithms now pass device parity and Compute Sanitizer checks; GPU ANARI capture/GUI interop remain to be verified.
- Driver bounds may be unsupported or require work even with `ANARI_NO_WAIT`; retain explicit cached/provider status rather than guaranteeing nonblocking behavior from that flag alone.
- Progressive accumulation semantics vary by device. Report completed frames
  separately from confirmed samples, do not claim either from poll count, and
  reject unsupported quality requests rather than silently ignoring them.
- Current async restart can expose a frame that predates configuration. The
  capture adapter must prove freshness/ownership and correct accumulation reset;
  blindly rendering twice is not a general freshness or sampling guarantee.
- Renderer/pipeline snapshotting must be owned and represented in capture
  provenance/dependencies, including presentation passes and supported quality.
  Same device name alone is not viewport parity.

## 8. Focused architecture decision (source-grounded advice)

The advisor recommends sharing an explicitly registered device/index with
capture-owned resources, conditional on backend qualification. The headless
module should deal in immutable CaptureSpec values and policy/resource quotes,
not raw ANARI handles. Estimation/qualification can remain internal to admission;
if a public quote is useful it grants no authority or reservation and must be
revalidated when submitting. Unknown driver-private allocations must be marked
honestly, and a binding requiring an unestablishable hard bound cannot promise
bounded capture.

If sharing stalls the UI, perturbs viewport accumulation, or is unsafe during
scene updates, disable that binding until an explicitly provisioned independent
worker/device/index or isolated process passes the same tests. A second cached
loadDevice call is not isolation. Required automated tests include UI heartbeat,
memory/cancellation/drain, stale input rejection, replay, and independently
camera-dependent images with bit-identical main-view state before/after.

